#include "probe_adc.h"

#include "cycles.h"
#include "settle_plan.h"

// Raw HAL, not libDaisy's AdcHandle: see the note above init() for why
// the public API cannot serve this measurement at all.
#include <stm32h7xx_hal.h>

namespace shell {
namespace probe_adc {

// --- Step 1: the ADC1 channel numbers, derived, not guessed ---
//
// ADC_9  = DaisyPatchSM::A2 = Pin(PORTA, 1)
//          (lib/libDaisy/src/daisy_patch_sm.h:264, daisy_patch_sm.cpp:18)
//        = PIN_CHN_17 (lib/libDaisy/src/per/adc.cpp:26)
//        -> ADC_CHANNEL_17. This is group 0's sense pin (the 4067).
// ADC_10 = DaisyPatchSM::A3 = Pin(PORTA, 0)
//          (daisy_patch_sm.h:265, daisy_patch_sm.cpp:19)
//        = PIN_CHN_16 (per/adc.cpp:25)
//        -> ADC_CHANNEL_16. This is group 1's sense pin (the 4051).
//
// libDaisy's own adc_channel_from_pin() (per/adc.cpp) is file-static and
// cannot be called from here, so these constants are carried by hand with
// the citation chain above instead of computed. DERIVED from source, not
// measured -- the calibration pass below is what proves them on hardware.
constexpr uint32_t kAdcChannelGroup0 = ADC_CHANNEL_17;  // ADC_9
constexpr uint32_t kAdcChannelGroup1 = ADC_CHANNEL_16;  // ADC_10

uint32_t channel_of_group(int group)
{
    return group == 0 ? kAdcChannelGroup0 : kAdcChannelGroup1;
}

// --- Step 3: take ADC1 and configure it ---
//
// libDaisy still owns the PINS: hw.Init() has already configured A2/A3 as
// analog inputs and brought up the ADC clock tree through MspInit, and none
// of that is duplicated here. What is taken over is ADC1 itself.
//
// The public API was unusable for exactly one reason: AdcHandle::Start()
// recalibrates, so a measurement loop that started and stopped the ADC would
// recalibrate inside its own timed path. Calibration happens once, here, and
// never again -- select() below only reconfigures the channel, never
// re-triggers HAL_ADCEx_Calibration_Start().
static ADC_HandleTypeDef g_adc{};

// The three HAL statuses this file used to throw away. HAL_ADC_Init(),
// HAL_ADCEx_Calibration_Start() and HAL_ADC_ConfigChannel() each return one,
// and a failure in any of them stops nothing: the probe goes on to poll a
// register and print plausible integers taken from an ADC that was never set
// up, which is exactly the failure an instrument built to refuse its own bad
// runs must not be able to hide. warm_up() was bounded and given its own
// `ok=` field for this reason in fix round 2; these three are the rest of
// that argument.
//
// Flags rather than return values: these calls are made from several places
// that have nothing to do with each other, and the question a reader of the
// log asks is "did any of this fail on this board", not "which call". cfg_ok
// is a fold -- it goes false on the first rejected channel configuration and
// never comes back -- so one flag covers every select_time() the run ever
// makes, including the per-pair rung selections inside the sweep.
static bool g_adc_init_ok = false;
static bool g_adc_cal_ok  = false;
static bool g_adc_cfg_ok  = true;

void init(bench::Board& hw)
{
    hw.StopAdc();                    // public on DaisyPatchSM; ADC1 is now free

    g_adc.Instance                      = ADC1;
    g_adc.Init.ClockPrescaler           = ADC_CLOCK_ASYNC_DIV2;   // measured 6.146 MHz, not the 12.29 MHz this comment claimed through fix round 3 -- see SHELL_SETTLE_CLK / task-4-report.md fix round 4
    g_adc.Init.Resolution               = ADC_RESOLUTION_16B;
    g_adc.Init.ScanConvMode             = ADC_SCAN_DISABLE;
    g_adc.Init.EOCSelection             = ADC_EOC_SINGLE_CONV;
    g_adc.Init.LowPowerAutoWait         = DISABLE;
    g_adc.Init.ContinuousConvMode       = DISABLE;
    g_adc.Init.NbrOfConversion          = 1;
    g_adc.Init.DiscontinuousConvMode    = DISABLE;
    g_adc.Init.ExternalTrigConv         = ADC_SOFTWARE_START;
    g_adc.Init.ExternalTrigConvEdge     = ADC_EXTERNALTRIGCONVEDGE_NONE;
    g_adc.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;  // no DMA
    g_adc.Init.Overrun                  = ADC_OVR_DATA_OVERWRITTEN;
    g_adc.Init.OversamplingMode         = DISABLE;                // OVS_NONE

    g_adc_init_ok = HAL_ADC_Init(&g_adc) == HAL_OK;
    g_adc_cal_ok  = HAL_ADCEx_Calibration_Start(&g_adc, ADC_CALIB_OFFSET,
                                                ADC_SINGLE_ENDED)
                   == HAL_OK;   // ONCE, never in the loop
}

// The working sampling time -- what run_settle_probe()'s working-conversion
// arithmetic predicts against (fix round 4; was kConversionNs), and what
// every pass except the ADC-clock calibration pass (fix round 2, item 3)
// runs under. kSampleTimeLong exists only for that one pass.
constexpr uint32_t kSampleTimeWorking = ADC_SAMPLETIME_16CYCLES_5;
constexpr uint32_t kSampleTimeLong    = ADC_SAMPLETIME_387CYCLES_5;

// settle_plan.h/.cpp's kSamplingLadderTenths stays pure and host-testable, so
// it carries no HAL type -- this is the HAL ADC_SAMPLETIME_* constant at the
// same index, for Task 5's per-pair sweep to hand to select_time(). Order
// matches kSamplingLadderTenths exactly (settle_plan.h says so; both are
// ADC_SAMPLETIME_1CYCLE_5 .. ADC_SAMPLETIME_810CYCLES_5 in HAL enum order).
constexpr uint32_t kSampleTimeByRung[kSamplingLadderLen] = {
    ADC_SAMPLETIME_1CYCLE_5,    ADC_SAMPLETIME_2CYCLES_5,
    ADC_SAMPLETIME_8CYCLES_5,   ADC_SAMPLETIME_16CYCLES_5,
    ADC_SAMPLETIME_32CYCLES_5,  ADC_SAMPLETIME_64CYCLES_5,
    ADC_SAMPLETIME_387CYCLES_5, ADC_SAMPLETIME_810CYCLES_5,
};

uint32_t sample_time_for_rung(int rung)
{
    // Clamped, not asserted, and clamped to the LONGEST rung: that is the
    // answer settle_plan.cpp's sample_time_index_for() already gives for an
    // impedance no rung covers, so the two agree at the edge instead of one
    // of them reading past the end of an array inside a timed path.
    if(rung < 0) rung = 0;
    if(rung >= kSamplingLadderLen) rung = kSamplingLadderLen - 1;
    return kSampleTimeByRung[rung];
}

// Runs when the sense pin or the sampling time changes -- once per channel
// pair (or once per side of the clock-calibration pass), never inside the
// timed path.
void select_time(uint32_t channel, uint32_t sampling_time)
{
    ADC_ChannelConfTypeDef cfg{};
    cfg.Channel      = channel;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = sampling_time;
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    cfg.Offset       = 0;
    if(HAL_ADC_ConfigChannel(&g_adc, &cfg) != HAL_OK) g_adc_cfg_ok = false;
}

// select(channel) is the interface Task 5 consumes -- kept to that exact
// name and signature. It always selects the working sampling time; only the
// clock-calibration pass below reaches for select_time() directly.
void select(uint32_t channel)
{
    select_time(channel, kSampleTimeWorking);
}

// --- Step 4: the measurement primitive ---
//
// One conversion. Returns the raw 16-bit value and, if asked, writes the
// start-to-EOC span in DWT cycles.
//
// Polling happens AFTER the aperture opens, so it costs wall clock and
// nothing else. The span is what makes section 7a's G4 possible: it
// brackets the start-to-aperture latency plus the conversion, and the
// conversion time is computed in run_settle_probe() from the measured ADC
// clock (fix round 4; kConversionNs, a fixed literal, is gone) -- but ONLY
// because this function never calls HAL_ADC_Stop(). Fix round 1: the first cut of this
// file called HAL_ADC_Stop() at the end of every sample, which clears ADEN;
// the next call's HAL_ADC_Start() then took ADC_Enable()'s slow path and
// busy-waited on ADC_FLAG_RDY *inside* [t0, t1] -- measured on the board at
// ~6 us, larger than the entire 0..6400 ns delay grid this instrument
// exists to resolve. The ADC is enabled once by warm_up() below and left
// enabled for the program's whole life; do not re-add a Stop() here.
//
// Fix round 2, item 2: HAL_ADC_Start()/HAL_ADC_GetValue() are gone from this
// function too, even though round 1 already put the ADC on its fast path.
// HAL_ADC_Start()'s fast path still takes a lock, updates a state-machine
// field and clears three flags before it reaches the register write that
// starts the conversion -- all of it still inside [t0, t1]. Measured on the
// board after round 1: lat_mean_ns ~2900, larger than the entire 0..6400 ns
// delay grid this instrument exists to resolve (see the round-2 report
// section for the board numbers). What remains here is the direct register
// sequence:
//   - Start: `ADC1->CR |= ADC_CR_ADSTART;`. This is what
//     LL_ADC_REG_StartConversion() itself reduces to
//     (stm32h7xx_ll_adc.h:7030 -- `MODIFY_REG(ADCx->CR, ADC_CR_BITS_PROPERTY_RS, ADC_CR_ADSTART)`,
//     a masked write that clears CR's other self-clearing ["read-set",
//     property RS] command bits before setting ADSTART, so a stale ADSTP/
//     ADDIS/JADSTART left set would not leak through). A plain `|=` is
//     equivalent here because nothing in this program ever leaves those
//     bits set between calls -- this file drives ADC1 exclusively and every
//     conversion it starts is left to complete or to time out before the
//     next one starts.
//   - Poll: `ADC1->ISR & ADC_ISR_EOC`, the same flag __HAL_ADC_GET_FLAG()
//     reads, read directly instead of through the macro's handle
//     indirection.
//   - Read: `ADC1->DR`. HAL_ADC_GetValue()'s own doc comment
//     (stm32h7xx_hal_adc.c:2327) says "Reading register DR automatically
//     clears ADC flag EOC", and its body (line 2353) is nothing but
//     `return hadc->Instance->DR;` -- confirmed by reading the function, not
//     assumed from the comment alone. No explicit flag clear is added here;
//     the hardware does it on the DR read.
//
// The poll is bounded and the timed section is interrupt-masked:
//   - kPollTimeoutCycles caps the EOC wait so a genuinely stuck ADC goes
//     silent-with-a-flag instead of hanging the board with no clue why.
//     On timeout, *span_cycles is set to kTimeoutSentinel and the caller
//     must not treat that as a real span.
//   - PRIMASK is saved and restored, not unconditionally cleared/set, so
//     this function cannot turn interrupts ON if it was called with them
//     already off. The mask covers only [t0, t1]: USB CDC and the audio
//     clock both run as interrupts on this board, and one landing inside
//     the timed window is exactly the outlier this instrument must exclude,
//     not report as the instrument's own latency.
//
// kPollTimeoutCycles also has to clear the ADC-clock calibration pass'
// long (387.5 ADC cycle) sampling time, not just the working 16.5-cycle one
// -- see kSampleTimeLong below. At an assumed 12.29 MHz that long conversion
// is ~30 us; sized here to still clear it even if the real ADC clock turns
// out as slow as 3 MHz (396 total ADC cycles / 3 MHz = ~132 us).
constexpr uint32_t kPollTimeoutCycles = 144000u;  // 300 us at 480 MHz

// Lifetime count of sample_now() calls that hit kPollTimeoutCycles. Printed
// every block so a timeout is visible in the log instead of silently
// widening lat_max_ns or corrupting a calibration mean.
static uint32_t g_adc_timeouts = 0;

uint16_t sample_now(uint32_t* span_cycles)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    const uint32_t t0 = cycles_now();
    ADC1->CR |= ADC_CR_ADSTART;   // see the block comment above for why not HAL_ADC_Start()

    bool timed_out = true;
    while((cycles_now() - t0) < kPollTimeoutCycles)
    {
        if((ADC1->ISR & ADC_ISR_EOC) != 0u)
        {
            timed_out = false;
            break;
        }
    }
    const uint32_t t1 = cycles_now();
    const uint16_t v  = timed_out ? 0u : static_cast<uint16_t>(ADC1->DR);   // read clears EOC

    __set_PRIMASK(primask);

    if(timed_out) ++g_adc_timeouts;
    if(span_cycles) *span_cycles = timed_out ? kTimeoutSentinel : (t1 - t0);
    return v;
}

// Runs the ADC's first conversion to completion, once, before any timed
// sample and before any channel is treated as parked. HAL_ADC_Start() only
// takes the fast "already enabled, no conversion ongoing" path once ADEN has
// been set and a first regular conversion has completed at least once; this
// call is what pays that one-time enable cost off the clock. Must run AFTER
// select() -- the channel/rank configuration has to exist before a
// conversion on it means anything, even a discarded one. Stays on HAL: this
// function is outside the timed path, so HAL's checks are worth having.
//
// Fix round 2, item 1: this poll used to be unbounded -- exactly the failure
// mode removed from sample_now() in round 1, just relocated one function
// away, and running before the first SHELL_SETTLE_CFG line, so a stall here
// left the board silent with no sentinel and no counter. It is now bounded
// by the same kPollTimeoutCycles the timed path uses, and returns whether it
// completed so the caller can say so even if the probe never gets running --
// a probe that cannot start must say it cannot start, not go quiet.
bool warm_up()
{
    const uint32_t t0 = cycles_now();
    HAL_ADC_Start(&g_adc);
    while((cycles_now() - t0) < kPollTimeoutCycles)
    {
        if(__HAL_ADC_GET_FLAG(&g_adc, ADC_FLAG_EOC) != 0u)
        {
            (void)HAL_ADC_GetValue(&g_adc);
            return true;
        }
    }
    return false;
}

// Mean of `repeats` conversions on whatever channel/mux address is currently
// parked. Used for the calibration read of pair 0's two rail-tied ends --
// only the mean is needed there, latency is not the question.
int32_t mean_of_repeats(int repeats)
{
    int64_t sum = 0;
    for(int i = 0; i < repeats; ++i) sum += sample_now(nullptr);
    return static_cast<int32_t>(sum / repeats);
}

// Mean SPAN (in DWT cycles), not value, of `repeats` conversions on whatever
// channel/sampling time is currently selected -- excluding any repeat that
// hit the poll timeout, same as the latency pass below. Used only by the
// ADC-clock calibration pass (fix round 2, item 3): everywhere else cares
// about the converted value, not how long it took.
int32_t mean_span_of_repeats(int repeats)
{
    int64_t sum     = 0;
    int     valid_n = 0;
    for(int i = 0; i < repeats; ++i)
    {
        uint32_t span = 0;
        (void)sample_now(&span);
        if(span == kTimeoutSentinel) continue;
        sum += span;
        ++valid_n;
    }
    return valid_n > 0 ? static_cast<int32_t>(sum / valid_n) : -1;
}

// The three HAL statuses and the timeout count, readable but not writable
// from outside: the state itself stays file-local.
uint32_t timeouts() { return g_adc_timeouts; }
bool     init_ok() { return g_adc_init_ok; }
bool     cal_ok() { return g_adc_cal_ok; }
bool     cfg_ok() { return g_adc_cfg_ok; }

Clock measure_clock(int repeats, uint32_t channel)
{
    Clock c{};

    // Selected explicitly rather than relying on the caller having done it.
    // settle_probe.cpp reached this pass with the working time already
    // selected by its own adc_select(); saying so here makes the pass
    // self-contained without changing what runs.
    select_time(channel, kSampleTimeWorking);
    c.span_short_cyc = mean_span_of_repeats(repeats);

    select_time(channel, kSampleTimeLong);
    c.span_long_cyc = mean_span_of_repeats(repeats);

    select_time(channel, kSampleTimeWorking);   // restore

    c.ok = c.span_short_cyc > 0 && c.span_long_cyc > c.span_short_cyc;
    c.core_cyc_per_adc_cyc =
        c.ok ? static_cast<double>(c.span_long_cyc - c.span_short_cyc) / 371.0
             : 0.0;

    // 16.5 sampling + 8.5 conversion = 25 ADC cycles.
    constexpr double kWorkingTotalAdcCycles = 25.0;
    const double working_conversion_core_cyc =
        kWorkingTotalAdcCycles * c.core_cyc_per_adc_cyc;
    c.working_conversion_ns =
        c.ok ? static_cast<int32_t>(cycles_to_ns(static_cast<uint32_t>(
                   working_conversion_core_cyc + 0.5)))
             : -1;
    c.measured_adc_khz =
        c.ok ? static_cast<int32_t>(480000.0 / c.core_cyc_per_adc_cyc + 0.5) : -1;
    c.pre_adstart_overhead_core_cyc =
        c.ok ? (static_cast<double>(c.span_short_cyc) - working_conversion_core_cyc)
             : 0.0;
    return c;
}

int32_t offset_ns_for_rung(const Clock& c, int rung)
{
    if(!c.ok) return -1;
    if(rung < 0) rung = 0;
    if(rung >= kSamplingLadderLen) rung = kSamplingLadderLen - 1;
    const double sampling_window_core_cyc =
        (static_cast<double>(kSamplingLadderTenths[rung]) / 10.0)
        * c.core_cyc_per_adc_cyc;
    return static_cast<int32_t>(cycles_to_ns(static_cast<uint32_t>(
        c.pre_adstart_overhead_core_cyc + sampling_window_core_cyc + 0.5)));
}

} // namespace probe_adc
} // namespace shell
