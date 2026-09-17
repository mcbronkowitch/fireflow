#include "settle_probe.h"

#include "cycles.h"
#include "mux_scan.h"
#include "settle_plan.h"

// Raw HAL, not libDaisy's AdcHandle: see the note above adc_init() for why
// the public API cannot serve this measurement at all.
#include <stm32h7xx_hal.h>

namespace shell {

namespace {

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

// The scan step for (group, channel): group 0's channels sit at the start of
// the step space, group 1's after all of group 0's -- see mux_plan.h's
// scan_steps()/step_pattern() and kCouponChain's {16, 8} channel counts.
int step_of(int group, int ch)
{
    return group == 0 ? ch : kCouponChain.channels[0] + ch;
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
// never again -- adc_select() below only reconfigures the channel, never
// re-triggers HAL_ADCEx_Calibration_Start().
ADC_HandleTypeDef g_adc{};

void adc_init(bench::Board& hw)
{
    hw.StopAdc();                    // public on DaisyPatchSM; ADC1 is now free

    g_adc.Instance                      = ADC1;
    g_adc.Init.ClockPrescaler           = ADC_CLOCK_ASYNC_DIV2;   // 12.29 MHz
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

    HAL_ADC_Init(&g_adc);
    HAL_ADCEx_Calibration_Start(&g_adc, ADC_CALIB_OFFSET,
                                ADC_SINGLE_ENDED);   // ONCE, never in the loop
}

// The working sampling time -- what kConversionNs predicts against, and what
// every pass except the ADC-clock calibration pass (fix round 2, item 3)
// runs under. kSampleTimeLong exists only for that one pass.
constexpr uint32_t kSampleTimeWorking = ADC_SAMPLETIME_16CYCLES_5;
constexpr uint32_t kSampleTimeLong    = ADC_SAMPLETIME_387CYCLES_5;

// Runs when the sense pin or the sampling time changes -- once per channel
// pair (or once per side of the clock-calibration pass), never inside the
// timed path.
void adc_select_time(uint32_t channel, uint32_t sampling_time)
{
    ADC_ChannelConfTypeDef cfg{};
    cfg.Channel      = channel;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = sampling_time;
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    cfg.Offset       = 0;
    HAL_ADC_ConfigChannel(&g_adc, &cfg);
}

// adc_select(channel) is the interface Task 5 consumes -- kept to that exact
// name and signature. It always selects the working sampling time; only the
// clock-calibration pass below reaches for adc_select_time() directly.
void adc_select(uint32_t channel)
{
    adc_select_time(channel, kSampleTimeWorking);
}

// --- Step 4: the measurement primitive ---
//
// One conversion. Returns the raw 16-bit value and, if asked, writes the
// start-to-EOC span in DWT cycles.
//
// Polling happens AFTER the aperture opens, so it costs wall clock and
// nothing else. The span is what makes section 7a's G4 possible: it
// brackets the start-to-aperture latency plus the conversion, and the
// conversion time is known (kConversionNs below) -- but ONLY because this
// function never calls HAL_ADC_Stop(). Fix round 1: the first cut of this
// file called HAL_ADC_Stop() at the end of every sample, which clears ADEN;
// the next call's HAL_ADC_Start() then took ADC_Enable()'s slow path and
// busy-waited on ADC_FLAG_RDY *inside* [t0, t1] -- measured on the board at
// ~6 us, larger than the entire 0..6400 ns delay grid this instrument
// exists to resolve. The ADC is enabled once by adc_warm_up() below and left
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
constexpr uint32_t kTimeoutSentinel   = 0xFFFFFFFFu;

// Lifetime count of sample_now() calls that hit kPollTimeoutCycles. Printed
// every block so a timeout is visible in the log instead of silently
// widening lat_max_ns or corrupting a calibration mean.
uint32_t g_adc_timeouts = 0;

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

// ADC_SAMPLETIME_16CYCLES_5 plus 8.5 cycles of 16-bit conversion is 25 ADC
// cycles, and at 12.29 MHz that is 2034 ns. Subtracting it from a real
// (non-timed-out) span leaves the start-to-aperture latency. DERIVED, not
// measured -- if it is wrong the latency comes out negative, which G4
// refuses.
constexpr int32_t kConversionNs = 2034;

// Runs the ADC's first conversion to completion, once, before any timed
// sample and before any channel is treated as parked. HAL_ADC_Start() only
// takes the fast "already enabled, no conversion ongoing" path once ADEN has
// been set and a first regular conversion has completed at least once; this
// call is what pays that one-time enable cost off the clock. Must run AFTER
// adc_select() -- the channel/rank configuration has to exist before a
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
bool adc_warm_up()
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

void park(MuxScan& chain, bench::Board& hw, int group, int ch)
{
    chain.write_chain(chain_word(kCouponChain, step_pattern(kCouponChain, step_of(group, ch)),
                                  0u));
    hw.Delay(1);
}

} // namespace

void run_settle_probe(bench::Board& hw)
{
    cycles_init();
    adc_init(hw);
    hw.StartLog(false);

    MuxScan chain;
    chain.init();

    const SettlePair& p0 = kSettlePlan[0];
    adc_select(channel_of_group(p0.group));

    // A probe that cannot start must say it cannot start. This is the first
    // line printed after "Daisy is online" (StartLog's own banner), before
    // the calibration or the forever loop, so a warm-up stall is visible
    // even though it would otherwise leave the board silent -- fix round 2,
    // item 1. ok=0 does not stop the rest of this function: every later ADC
    // access is bounded the same way, so the run continues and reports
    // whatever it can (see g_adc_timeouts in every SHELL_SETTLE_CAL line)
    // rather than reaching a second unbounded wait.
    const bool adc_warm_ok = adc_warm_up();
    hw.PrintLine("SHELL_SETTLE_WARMUP ok=%d", adc_warm_ok ? 1 : 0);

    park(chain, hw, p0.group, p0.from_ch);

    // ADC-clock calibration (fix round 2, item 3): kConversionNs assumes
    // 12.29 MHz from ADC_CLOCK_ASYNC_DIV2's datasheet value, and every
    // lat_*_ns this file has ever printed depends on that assumption being
    // right. Measure it instead of trusting it: run two spans back to back
    // on the SAME parked channel, identical in every way except the
    // configured sampling time, so every fixed cost (register overhead, the
    // clock-domain synchronization, the 8.5-cycle conversion, any interrupt
    // that slips past the mask) cancels in the difference and only the
    // extra ADC cycles from the longer sampling time remain --
    // 387.5 - 16.5 = 371 of them. This file does not do that division or
    // convert either span to nanoseconds; the controller does:
    // (span_long - span_short) / 371 = core cycles per ADC cycle, and
    // 480 MHz divided by that is the real ADC clock.
    const int32_t clk_span_short = mean_span_of_repeats(kRepeats);   // kSampleTimeWorking, already selected

    adc_select_time(channel_of_group(p0.group), kSampleTimeLong);
    const int32_t clk_span_long = mean_span_of_repeats(kRepeats);

    adc_select_time(channel_of_group(p0.group), kSampleTimeWorking);   // restore -- every later pass needs this

    // clk_span_short/clk_span_long are measured exactly once, here -- see
    // above for why re-running the 387.5-cycle pass every block would cost
    // real time to keep reprinting a constant. Only the PRINT of them moves
    // into the forever loop below (fix round 3): hw.StartLog(false) does not
    // wait for a host, so a one-shot print here goes out within milliseconds
    // of boot, long before Windows finishes re-enumerating the USB-CDC
    // device after flashing -- the line existed and was correct but nobody
    // could ever read it, the same failure shape as a probe that scans once
    // and reprints a frozen buffer forever. Do not move this back out.

    // Calibration (scope change from the brief's step 7): park permanently on
    // BOTH ends of P0's step and take kRepeats conversions at each, instead
    // of temporarily parking on to_ch, reflashing, reading and restoring.
    // from_ch is tied to A+3V3 through 0 ohms, to_ch to AGND through 0 ohms,
    // so this alone answers step 1's channel-number question: the right
    // constants put cal_from near full scale and cal_to near zero; wrong
    // ones put them equal, or both at some implausible value.
    //
    // Already parked on from_ch above (for the clock-calibration pass), so
    // cal_from's park is not repeated here.
    const int32_t cal_from = mean_of_repeats(kRepeats);

    park(chain, hw, p0.group, p0.to_ch);
    const int32_t cal_to = mean_of_repeats(kRepeats);

    // Park back on from_ch -- P0's settled reference -- for the latency pass
    // that follows, forever.
    park(chain, hw, p0.group, p0.from_ch);

    while(1)
    {
        // The configuration line comes FIRST and the end marker always
        // arrives, even on a pass that measured nothing. A reader that can
        // only recognise a complete block is the point: a truncated one must
        // be discarded rather than half-believed.
        //
        // sample_cycles=165 is ADC_SAMPLETIME_16CYCLES_5 expressed in tenths,
        // so it stays an integer for %d -- PrintLine() is the lightweight
        // printf and the existing probes stay on %d for that reason.
        //
        // adc_khz=12290 is ASSUMED, not measured -- it is ADC_CLOCK_ASYNC_DIV2's
        // datasheet value, the same assumption kConversionNs is built on, and
        // it is exactly what SHELL_SETTLE_CLK below (span_short_cyc/
        // span_long_cyc) exists to check. It sits beside real measurements in
        // this same block; do not read it as one.
        hw.PrintLine("SHELL_SETTLE_CFG sample_cycles=165 adc_khz=12290 "
                     "repeats=%d grid_step_ns=%d grid_points=%d park_ns=%d",
                     kRepeats, kGridStepNs, kGridPoints,
                     static_cast<int>(kParkNs));

        // Printed every pass, beside SHELL_SETTLE_CAL, even though
        // clk_span_short/clk_span_long were measured once at startup and
        // never change (fix round 3): hw.StartLog(false) does not wait for a
        // host, so the one-shot print this replaced went out within
        // milliseconds of boot and no reader could ever open the port in
        // time -- confirmed absent from 223 captured lines across a normal
        // run and 110 seconds across a deliberate RESET. A measurement
        // nobody can observe is not a measurement. Do not move this back to
        // a one-shot print before the loop.
        hw.PrintLine("SHELL_SETTLE_CLK span_short_cyc=%d span_long_cyc=%d "
                     "smp_short_tenths=%d smp_long_tenths=%d",
                     clk_span_short, clk_span_long, 165, 3875);

        // The instrument measuring itself. Parked and fully settled, so the
        // only thing that varies between these conversions is the
        // instrument.
        //
        // A timed-out repeat (span == kTimeoutSentinel) is excluded from
        // every one of these reductions rather than folded in: it is not a
        // measurement of the instrument's latency, it is the instrument
        // failing to respond, and averaging that in would silently widen
        // lat_max_ns into something that LOOKS like jitter instead of
        // reading as the fault it is. valid_n divides the sums, not
        // kRepeats, for the same reason.
        int32_t lat_min = 0x7FFFFFFF, lat_max = -0x7FFFFFFF;
        int64_t lat_sum = 0;
        int32_t val_min = 0x7FFFFFFF, val_max = -0x7FFFFFFF;
        int     valid_n = 0;
        for(int i = 0; i < kRepeats; ++i)
        {
            uint32_t span = 0;
            const int32_t v = sample_now(&span);
            if(span == kTimeoutSentinel) continue;
            const int32_t l = static_cast<int32_t>(cycles_to_ns(span)) - kConversionNs;
            if(l < lat_min) lat_min = l;
            if(l > lat_max) lat_max = l;
            lat_sum += l;
            if(v < val_min) val_min = v;
            if(v > val_max) val_max = v;
            ++valid_n;
        }
        // -1 rather than a divide-by-zero, a silently zeroed mean, or the
        // 0x7FFFFFFF/-0x7FFFFFFF init values leaking into print, if every
        // repeat in this block timed out. gates_ok is already an
        // unconditional 0, but none of these fields may look like a number
        // when they are not one.
        const bool    all_timed_out = valid_n == 0;
        const int32_t lat_mean = all_timed_out ? -1 : static_cast<int32_t>(lat_sum / valid_n);
        if(all_timed_out) { lat_min = -1; lat_max = -1; }
        const int32_t b0 = all_timed_out ? -1 : (val_max - val_min);  // G2's measured noise floor

        // gates_ok is always 0 here -- Task 5 computes it via settle_gates().
        // Do not read this pass as a verdict. timeouts is the LIFETIME count
        // since boot (see g_adc_timeouts) so a single unlucky block does not
        // hide behind an otherwise-clean-looking next one.
        hw.PrintLine("SHELL_SETTLE_CAL lat_mean_ns=%d lat_min_ns=%d "
                     "lat_max_ns=%d b0=%d cal_from=%d cal_to=%d timeouts=%d "
                     "gates_ok=%d",
                     lat_mean, lat_min, lat_max, b0, cal_from, cal_to,
                     static_cast<int>(g_adc_timeouts), 0);
        hw.PrintLine("SHELL_SETTLE_END");
        hw.Delay(1000);
    }
}

} // namespace shell
