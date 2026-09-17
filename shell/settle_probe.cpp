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

    HAL_ADC_Init(&g_adc);
    HAL_ADCEx_Calibration_Start(&g_adc, ADC_CALIB_OFFSET,
                                ADC_SINGLE_ENDED);   // ONCE, never in the loop
}

// The working sampling time -- what run_settle_probe()'s working-conversion
// arithmetic predicts against (fix round 4; was kConversionNs), and what
// every pass except the ADC-clock calibration pass (fix round 2, item 3)
// runs under. kSampleTimeLong exists only for that one pass.
constexpr uint32_t kSampleTimeWorking = ADC_SAMPLETIME_16CYCLES_5;
constexpr uint32_t kSampleTimeLong    = ADC_SAMPLETIME_387CYCLES_5;

// settle_plan.h/.cpp's kSamplingLadderTenths stays pure and host-testable, so
// it carries no HAL type -- this is the HAL ADC_SAMPLETIME_* constant at the
// same index, for Task 5's per-pair sweep to hand to adc_select_time(). Order
// matches kSamplingLadderTenths exactly (settle_plan.h says so; both are
// ADC_SAMPLETIME_1CYCLE_5 .. ADC_SAMPLETIME_810CYCLES_5 in HAL enum order).
constexpr uint32_t kSampleTimeByRung[kSamplingLadderLen] = {
    ADC_SAMPLETIME_1CYCLE_5,    ADC_SAMPLETIME_2CYCLES_5,
    ADC_SAMPLETIME_8CYCLES_5,   ADC_SAMPLETIME_16CYCLES_5,
    ADC_SAMPLETIME_32CYCLES_5,  ADC_SAMPLETIME_64CYCLES_5,
    ADC_SAMPLETIME_387CYCLES_5, ADC_SAMPLETIME_810CYCLES_5,
};

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
// conversion time is computed in run_settle_probe() from the measured ADC
// clock (fix round 4; kConversionNs, a fixed literal, is gone) -- but ONLY
// because this function never calls HAL_ADC_Stop(). Fix round 1: the first cut of this
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

// Fix round 4, item 1: there used to be a kConversionNs = 2034 here --
// "16.5 sampling + 8.5 conversion = 25 ADC cycles, and at 12.29 MHz that is
// 2034 ns." SHELL_SETTLE_CLK (below) measured the real ADC clock at
// 6.146 MHz, exactly half of that assumption -- one prescaler step -- so
// 2034 ns under-subtracted by 2034 ns, and that shortfall was the entire
// "latency" fix rounds 1-3 spent three rounds chasing out of software that
// was never the cause. A number this probe can measure must not also exist
// as a literal: run_settle_probe() below now computes the working sampling
// pass's conversion time (and every pair's offset) from THIS boot's
// measured core-cycles-per-ADC-cycle ratio instead. See task-4-report.md's
// fix-round-4 section for the derivation and the board numbers.

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

// --- Task 5: the sweep ---
//
// One grid point: park on the pair's from_ch for kParkNs, latch to to_ch
// taking t0 from the 595 chain's own latch edge, spin until d_ns has
// elapsed, convert. Repeated kRepeats times; mean/min/max come back for
// d_settle_index() and for G2/G3's floor and band.
//
// No bench::Board& parameter (brief's step 1 pseudocode carried one and
// never used it) -- every wait in here spins on the DWT counter via
// cycles_now(), not on anything the board handle owns.
Point measure_point(MuxScan& chain, const SettlePair& sp, uint32_t d_ns)
{
    const uint32_t park_word
        = chain_word(kCouponChain,
                     step_pattern(kCouponChain, step_of(sp.group, sp.from_ch)), 0u);
    const uint32_t test_word
        = chain_word(kCouponChain,
                     step_pattern(kCouponChain, step_of(sp.group, sp.to_ch)), 0u);
    const uint32_t d_cycles = ns_to_cycles(d_ns);
    const uint32_t park_cycles = ns_to_cycles(kParkNs);

    int64_t sum = 0;
    int32_t lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    for(int r = 0; r < kRepeats; ++r)
    {
        chain.write_chain(park_word);
        // Named park_t0, not p0: run_settle_probe() below has its own p0 --
        // kSettlePlan[0], a SettlePair -- and this function must not read as
        // sharing it even though the two never actually collide (separate
        // function scopes, no shadowing).
        const uint32_t park_t0 = cycles_now();
        while(cycles_now() - park_t0 < park_cycles) { }

        const uint32_t t0 = chain.write_chain_timed(test_word);
        // t = 0 is the 595 latch edge write_chain_timed() hands back, not the
        // call site -- see its own doc comment. `cycles_now() - t0` on
        // uint32_t wraps correctly with no special case needed: the DWT
        // counter is free-running and unsigned specifically so this
        // subtraction is always valid, even across a wrap. Do not "fix" it.
        while(cycles_now() - t0 < d_cycles) { }

        // nullptr: nothing in this function reads the per-repeat span (that
        // came from the brief's own pseudocode, not a defect introduced
        // here) -- the aperture-jitter measurement that DOES need spans is
        // the lat pass in run_settle_probe(), on the parked reference.
        const int32_t v = sample_now(nullptr);
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
    }
    return Point{static_cast<int32_t>(sum / kRepeats), lo, hi};
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

    // ADC-clock calibration (fix round 2, item 3): the deleted kConversionNs
    // assumed 12.29 MHz from ADC_CLOCK_ASYNC_DIV2's datasheet value, and
    // every lat_*_ns this file has ever printed depended on that assumption
    // being right. Measure it instead of trusting it: run two spans back to
    // back on the SAME parked channel, identical in every way except the
    // configured sampling time, so every fixed cost (register overhead, the
    // clock-domain synchronization, the 8.5-cycle conversion, any interrupt
    // that slips past the mask) cancels in the difference and only the
    // extra ADC cycles from the longer sampling time remain --
    // 387.5 - 16.5 = 371 of them.
    //
    // Fix round 4: this file now DOES do that division and the ns
    // conversion itself (core_cyc_per_adc_cyc, working_conversion_ns,
    // measured_adc_khz, offset_ns[] below) -- SHELL_SETTLE_CLK's raw spans
    // are still printed unconverted every block (fix round 3) so the
    // derivation stays independently checkable, but the probe no longer
    // waits on the controller to do the arithmetic before it can correct
    // its own latency numbers.
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

    // Fix round 4, item 1: every duration below is computed from THIS boot's
    // measured clk_span_short/clk_span_long, never from a re-assumed
    // literal. has_clk guards the case where the clock-calibration pass
    // itself came back invalid (mean_span_of_repeats() returns -1 when every
    // repeat timed out) -- everything downstream then prints -1 rather than
    // a divide-by-zero or a number computed from a ratio that does not mean
    // anything.
    const bool   has_clk = clk_span_short > 0 && clk_span_long > clk_span_short;
    const double core_cyc_per_adc_cyc =
        has_clk ? static_cast<double>(clk_span_long - clk_span_short) / 371.0 : 0.0;

    // The working sampling pass's own conversion time -- 16.5 sampling +
    // 8.5 conversion = 25 ADC cycles -- replacing the deleted kConversionNs.
    constexpr double kWorkingTotalAdcCycles = 25.0;
    const double      working_conversion_core_cyc =
        kWorkingTotalAdcCycles * core_cyc_per_adc_cyc;
    const int32_t working_conversion_ns =
        has_clk ? static_cast<int32_t>(cycles_to_ns(static_cast<uint32_t>(
                      working_conversion_core_cyc + 0.5)))
                : -1;

    // adc_khz for SHELL_SETTLE_CFG below (fix round 4, item 4): the measured
    // value, replacing the wrong 12.29 MHz assumption.
    const int32_t measured_adc_khz =
        has_clk ? static_cast<int32_t>(480000.0 / core_cyc_per_adc_cyc + 0.5) : -1;

    // The true start-to-aperture overhead, isolated from the sampling
    // window itself: span_short already includes it plus the working
    // pass's own 25-cycle conversion, so subtracting that conversion's core
    // cycles back out leaves just the overhead. Reused below for every
    // pair's offset, not only P0's -- see the per-pair loop.
    const double pre_adstart_overhead_core_cyc =
        has_clk ? (static_cast<double>(clk_span_short) - working_conversion_core_cyc) : 0.0;

    // Fix round 4, item 3: per pair, offset = pre_adstart_overhead + that
    // pair's OWN sampling window -- NOT its conversion cycles. The S&H cap
    // is acquired, and therefore already correct, at the END of the
    // sampling window; the conversion cycles that follow only digitize what
    // is already captured, so they do not delay the instant that needs the
    // node to have settled. The rung comes from sample_time_index_for()
    // (settle_plan.h/.cpp), the same pure, host-tested function the sweep
    // below (Task 5) uses to actually select each pair's ADC channel.
    //
    // Task 5's per-pair sweep, below, reuses rung_idx[]/offset_ns[] computed
    // right here rather than recomputing them: "true settle = d + offset"
    // needs this pair's offset, and offset_ns is printed on its own, beside
    // SHELL_SETTLE_CAL, so a reader can check the arithmetic without
    // trusting SHELL_SETTLE_KNEE's at_or_below_offset flag blind.
    int32_t rung_idx[kSettlePairs];
    int32_t offset_ns[kSettlePairs];
    for(int p = 0; p < kSettlePairs; ++p)
    {
        rung_idx[p] = sample_time_index_for(kSettlePlan[p].r_src_ohm);
        const double sampling_window_core_cyc =
            (static_cast<double>(kSamplingLadderTenths[rung_idx[p]]) / 10.0)
            * core_cyc_per_adc_cyc;
        offset_ns[p] = has_clk
            ? static_cast<int32_t>(cycles_to_ns(static_cast<uint32_t>(
                  pre_adstart_overhead_core_cyc + sampling_window_core_cyc + 0.5)))
            : -1;
    }

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
        // The sweep below (Task 5) reconfigures the ADC's channel and
        // sampling time per pair and leaves the mux parked on whichever
        // grid point it measured last. Put both back to P0's own from_ch at
        // the working sampling time before anything else this iteration
        // does -- the instrument-measuring-itself pass a few lines down has
        // to see the exact same node every single iteration, or lat_mean_ns
        // stops meaning "the instrument's own latency" and starts meaning
        // "whatever the previous sweep happened to visit last". A no-op on
        // the very first iteration (already parked there by the setup
        // above); cheap (hw.Delay(1) against a sweep costing ~0.3 s) on
        // every iteration after.
        adc_select(channel_of_group(p0.group));
        park(chain, hw, p0.group, p0.from_ch);

        // The configuration line comes FIRST and the end marker always
        // arrives, even on a pass that measured nothing. A reader that can
        // only recognise a complete block is the point: a truncated one must
        // be discarded rather than half-believed.
        //
        // sample_cycles=165 is ADC_SAMPLETIME_16CYCLES_5 expressed in tenths,
        // so it stays an integer for %d -- PrintLine() is the lightweight
        // printf and the existing probes stay on %d for that reason.
        //
        // adc_khz now prints the MEASURED value (fix round 4, item 4) --
        // computed once at startup from SHELL_SETTLE_CLK's own two spans,
        // the same measurement that overturned the previously assumed
        // 12.29 MHz (this line printed a bare "12290" literal through fix
        // round 3; the real clock is 6.146 MHz, exactly half). -1 means
        // has_clk was false -- the clock-calibration pass itself came back
        // invalid and nothing downstream of it should be trusted either.
        hw.PrintLine("SHELL_SETTLE_CFG sample_cycles=165 adc_khz=%d "
                     "repeats=%d grid_step_ns=%d grid_points=%d park_ns=%d",
                     measured_adc_khz, kRepeats, kGridStepNs, kGridPoints,
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

        // Fix round 4, item 3: printed every pass, beside SHELL_SETTLE_CAL,
        // for the same observability reason SHELL_SETTLE_CLK moved into the
        // loop in fix round 3 -- offset_ns[]/rung_idx[] were computed once,
        // above, since none of it changes boot to boot. This line establishes
        // the offset half of "true settle = d + offset"; the sweep below
        // (Task 5) prints the other half in SHELL_SETTLE_KNEE, with its own
        // at_or_below_offset flag for the pairs whose true settle falls
        // below what's printed here.
        for(int p = 0; p < kSettlePairs; ++p)
        {
            hw.PrintLine("SHELL_SETTLE_OFFSET pair=%d offset_ns=%d smp_tenths=%d",
                         p, offset_ns[p], kSamplingLadderTenths[rung_idx[p]]);
        }

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
        // has_clk guards the whole pass: kConversionNs is gone (fix round 4,
        // item 1), so a bad clock measurement now means there is nothing
        // correct to subtract at all, not a wrong-but-present number. The
        // valid_n == 0 path below already prints -1 for exactly this shape
        // of "nothing usable this block".
        if(has_clk)
        {
            for(int i = 0; i < kRepeats; ++i)
            {
                uint32_t span = 0;
                const int32_t v = sample_now(&span);
                if(span == kTimeoutSentinel) continue;
                const int32_t l =
                    static_cast<int32_t>(cycles_to_ns(span)) - working_conversion_ns;
                if(l < lat_min) lat_min = l;
                if(l > lat_max) lat_max = l;
                lat_sum += l;
                if(v < val_min) val_min = v;
                if(v > val_max) val_max = v;
                ++valid_n;
            }
        }
        // -1 rather than a divide-by-zero, a silently zeroed mean, or the
        // 0x7FFFFFFF/-0x7FFFFFFF init values leaking into print, if every
        // repeat in this block timed out (or has_clk was false). gates_ok
        // comes from the sweep below now (Task 5), not a hardcoded 0 -- but
        // none of these fields may look like a number when they are not one.
        const bool    all_timed_out = valid_n == 0;
        const int32_t lat_mean = all_timed_out ? -1 : static_cast<int32_t>(lat_sum / valid_n);
        if(all_timed_out) { lat_min = -1; lat_max = -1; }
        const int32_t b0 = all_timed_out ? -1 : (val_max - val_min);  // G2's measured noise floor

        // --- Task 5: the sweep ---
        //
        // Streamed, not buffered (Task 5 fix round 1, item 1): each
        // SHELL_SETTLE line prints the instant its own point is measured,
        // inside the loop below. Only the CURRENT pair's kGridPoints points
        // (a few hundred bytes) are ever kept at once -- d_settle_index()
        // needs the whole curve to find "the start of the final settled
        // run" (a forward scan alone cannot do that), but never more than
        // one pair's worth. This task's first round buffered all 6 pairs'
        // points so SHELL_SETTLE_CAL's gates_ok could print before the
        // per-point lines; that print order was itself the bug (a dispatch
        // error, not the brief), so the fix is printing CAL, the knees, the
        // gates and the end marker only AFTER the sweep below, matching the
        // brief -- not carrying the whole sweep in memory to satisfy a print
        // order that should not have existed.
        RunSummary summary{};
        summary.b0                  = b0;
        summary.lat_min_ns          = lat_min;
        summary.lat_max_ns          = lat_max;
        summary.lat_mean_ns         = lat_mean;
        summary.widest_settled_band = 0;

        int32_t d_settle_ns_print[kSettlePairs];
        bool    at_or_below_offset[kSettlePairs];
        // Fix round 2, item 1: the yardstick every knee is judged against
        // was never printed, so a reader had no way to check a single knee
        // against the data in front of them. Both settled_raw_pre[] (the
        // value d_settle_index() actually uses) and settled_raw_post[]
        // (item 2, below) are kept for the SHELL_SETTLE_KNEE line.
        int32_t settled_raw_pre[kSettlePairs];
        int32_t settled_raw_post[kSettlePairs];
        // Last point's mean minus settled_raw_pre, signed -- makes "never
        // settled" interpretable: a reader can tell nine counts short from
        // nine hundred instead of only seeing knee_ns == -1.
        int32_t residual_counts[kSettlePairs];

        for(int p = 0; p < kSettlePairs; ++p)
        {
            const SettlePair& sp = kSettlePlan[p];

            // This pair's OWN rung (amendment 4), not kSampleTimeWorking.
            // rung_idx[p] came from sample_time_index_for() at startup and is
            // reused here, not recomputed (amendment 4).
            adc_select_time(channel_of_group(sp.group), kSampleTimeByRung[rung_idx[p]]);

            // The settled reference for THIS pair, READ (PRE) at the end of
            // a long park on to_ch, not assumed from the divider's nominal
            // value -- that is what makes the curve a comparison instead of
            // a prediction checked against itself. d_settle_index() below
            // uses THIS read, not the post one (fix round 2, item 2), so the
            // knee's meaning does not change between rounds.
            chain.write_chain(chain_word(
                kCouponChain, step_pattern(kCouponChain, step_of(sp.group, sp.to_ch)), 0u));
            const uint32_t s0_pre = cycles_now();
            while(cycles_now() - s0_pre < ns_to_cycles(kParkNs)) { }
            const int32_t settled_pre = sample_now(nullptr);

            // This pair only, overwritten by the next one -- see the
            // "streamed, not buffered" comment above.
            Point pts[kGridPoints];
            for(int i = 0; i < kGridPoints; ++i)
            {
                pts[i] = measure_point(chain, sp, grid_ns(i));
                hw.PrintLine("SHELL_SETTLE pair=%d sense=%d from=%d to=%d d_ns=%d "
                             "n=%d mean=%d min=%d max=%d",
                             p, sp.group, sp.from_ch, sp.to_ch,
                             static_cast<int>(grid_ns(i)), kRepeats,
                             pts[i].mean, pts[i].min, pts[i].max);
            }

            // A second read of the SAME settled reference, AFTER the sweep
            // (POST) -- fix round 2, item 2. This design cannot otherwise
            // tell settling from drift: the grid is swept in increasing d,
            // so elapsed wall-clock time and commanded delay grow together,
            // and a slow thermal creep across the sweep's own ~0.6 s would
            // look exactly like a slow tail. Pre and post read the same
            // to_ch the same way; only the wall-clock time between them
            // differs. Not interpreted here -- see the SHELL_SETTLE_KNEE
            // comment below for what the two numbers would mean.
            chain.write_chain(chain_word(
                kCouponChain, step_pattern(kCouponChain, step_of(sp.group, sp.to_ch)), 0u));
            const uint32_t s0_post = cycles_now();
            while(cycles_now() - s0_post < ns_to_cycles(kParkNs)) { }
            const int32_t settled_post = sample_now(nullptr);

            settled_raw_pre[p]  = settled_pre;
            settled_raw_post[p] = settled_post;
            residual_counts[p]  = pts[kGridPoints - 1].mean - settled_pre;

            const int idx = d_settle_index(pts, kGridPoints, settled_pre);
            summary.knee_ns[p] = (idx < 0) ? -1 : static_cast<int32_t>(grid_ns(idx));

            // idx == 0 means the FIRST grid point (0 ns commanded delay) was
            // already inside the band: the true settle instant -- d + this
            // pair's own offset_ns, the SHELL_SETTLE_OFFSET line above -- is
            // at or below the offset, i.e. faster than this instrument's own
            // zero point can resolve. That is not "0 ns settle time"; the
            // node did not settle instantaneously, the instrument simply
            // cannot see anything faster than its own aperture closing.
            // d_settle_ns prints -1 (impossible as a real duration, the same
            // sentinel already used for "never settled") whenever idx <= 0,
            // and at_or_below_offset is the only field that then tells the
            // two apart: idx == 0 is a PASS (that collapse to grid point
            // zero is G1's entire job, section 7), idx < 0 across the WHOLE
            // grid means the pair never settled at all. A reader who
            // ignores the flag still cannot mistake either -1 for a
            // measured time, and one who reads it cannot conflate the two
            // very different reasons behind it. Confirmed against the
            // coupon board (fix round 1): pair 0 and pair 5, the two
            // reference pairs, both came back idx == 0.
            at_or_below_offset[p] = (idx == 0);
            d_settle_ns_print[p]  = (idx > 0) ? static_cast<int32_t>(grid_ns(idx)) : -1;

            // G3's widest_settled_band (Task 5 fix round 1, item 3): counted
            // only from this pair's own knee onward, never the transient
            // before it -- see the field's comment in settle_plan.h for the
            // measured coupon-board numbers (934 ns down to 81 ns on pair 1,
            // 1140 down to 149 on pair 2) that make the transient unfit to
            // compare against a floor derived from the settled state. A pair
            // with no knee (idx < 0) contributes nothing: its own failure to
            // settle already shows up in knee_ns and must not also corrupt
            // this gate.
            if(idx >= 0)
            {
                int32_t widest_this_pair = 0;
                for(int i = idx; i < kGridPoints; ++i)
                {
                    const int32_t band = pts[i].max - pts[i].min;
                    if(band > widest_this_pair) widest_this_pair = band;
                }
                if(widest_this_pair > summary.widest_settled_band)
                    summary.widest_settled_band = widest_this_pair;
            }
        }

        const Gates gates = settle_gates(summary);

        hw.PrintLine("SHELL_SETTLE_CAL lat_mean_ns=%d lat_min_ns=%d "
                     "lat_max_ns=%d b0=%d cal_from=%d cal_to=%d timeouts=%d "
                     "gates_ok=%d",
                     lat_mean, lat_min, lat_max, b0, cal_from, cal_to,
                     static_cast<int>(g_adc_timeouts), gates.ok() ? 1 : 0);

        // The block prints the knees whether or not the gates passed, beside
        // the gates -- it does not suppress numbers, it labels them. A
        // reader that sees gates_ok=0 must not quote a single d_settle_ns;
        // read_settle.py (Task 6) enforces that.
        //
        // settled_raw_pre/settled_raw_post (fix round 2): printed side by
        // side, not reduced to a verdict here. If they agree within a
        // couple of counts, the sweep's own ~0.6 s of wall-clock time did
        // not move the node and a slow tail in SHELL_SETTLE is real
        // settling; if they differ by roughly a slow tail's own creep, that
        // creep is thermal drift across the sweep, not settling, and
        // d_settle_index() (which uses PRE, never POST) is comparing every
        // point against a reference the node had already drifted away from
        // by the time later points were measured. Which of those this run
        // shows is for the reader to read off the two numbers, not for this
        // comment to declare.
        for(int p = 0; p < kSettlePairs; ++p)
        {
            hw.PrintLine("SHELL_SETTLE_KNEE pair=%d d_settle_ns=%d at_or_below_offset=%d "
                         "predicted_ns=%d reference=%d settled_raw_pre=%d "
                         "settled_raw_post=%d residual_counts=%d",
                         p, d_settle_ns_print[p], at_or_below_offset[p] ? 1 : 0,
                         static_cast<int>(kSettlePlan[p].tau9_ns),
                         kSettlePlan[p].is_reference ? 1 : 0,
                         settled_raw_pre[p], settled_raw_post[p], residual_counts[p]);
        }

        hw.PrintLine("SHELL_SETTLE_GATES g1=%d g2=%d g3=%d g4=%d",
                     gates.g1_knee ? 1 : 0, gates.g2_floor ? 1 : 0,
                     gates.g3_band ? 1 : 0, gates.g4_jitter ? 1 : 0);
        hw.PrintLine("SHELL_SETTLE_END");

        // Sweep state (last pair's channel/sampling time, mux parked on its
        // last grid point) is NOT restored here: the top of the next
        // iteration does it, right before that iteration's own lat pass
        // needs it (adc_select(channel_of_group(p0.group)) + park() there).
        // Restoring in two places would only be two places to keep in sync.
        hw.Delay(1000);
    }
}

} // namespace shell
