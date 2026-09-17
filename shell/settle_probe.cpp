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

// Runs when the sense pin changes -- once per channel pair, never inside the
// timed path.
void adc_select(uint32_t channel)
{
    ADC_ChannelConfTypeDef cfg{};
    cfg.Channel      = channel;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_16CYCLES_5;   // what kConversionNs predicts against
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    cfg.Offset       = 0;
    HAL_ADC_ConfigChannel(&g_adc, &cfg);
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
constexpr uint32_t kPollTimeoutCycles = 48000u;  // 100 us at 480 MHz, ~50x kConversionNs
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
    HAL_ADC_Start(&g_adc);

    bool timed_out = true;
    while((cycles_now() - t0) < kPollTimeoutCycles)
    {
        if(__HAL_ADC_GET_FLAG(&g_adc, ADC_FLAG_EOC) != 0u)
        {
            timed_out = false;
            break;
        }
    }
    const uint32_t t1 = cycles_now();
    const uint16_t v  = timed_out ? 0u : static_cast<uint16_t>(HAL_ADC_GetValue(&g_adc));

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
// conversion on it means anything, even a discarded one.
void adc_warm_up()
{
    HAL_ADC_Start(&g_adc);
    while(__HAL_ADC_GET_FLAG(&g_adc, ADC_FLAG_EOC) == 0u) { }
    (void)HAL_ADC_GetValue(&g_adc);
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
    adc_warm_up();   // pays the one-time ADC_Enable() wait here, off the clock

    // Calibration (scope change from the brief's step 7): park permanently on
    // BOTH ends of P0's step and take kRepeats conversions at each, instead
    // of temporarily parking on to_ch, reflashing, reading and restoring.
    // from_ch is tied to A+3V3 through 0 ohms, to_ch to AGND through 0 ohms,
    // so this alone answers step 1's channel-number question: the right
    // constants put cal_from near full scale and cal_to near zero; wrong
    // ones put them equal, or both at some implausible value.
    park(chain, hw, p0.group, p0.from_ch);
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
        hw.PrintLine("SHELL_SETTLE_CFG sample_cycles=165 adc_khz=12290 "
                     "repeats=%d grid_step_ns=%d grid_points=%d park_ns=%d",
                     kRepeats, kGridStepNs, kGridPoints,
                     static_cast<int>(kParkNs));

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
