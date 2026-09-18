#include "settle_plan.h"

namespace shell {

// Section 6's table. Each target's source impedance is the switch on-
// resistance plus what the netlist wires to that channel, and the two
// reference channels bracket the pot range the panel will actually use --
// REF_A at 5.15k sits on the model's 20k-pot row, REF_B an order of
// magnitude below it.
const SettlePair kSettlePlan[kSettlePairs] = {
    // group from  to   R_src  9.01 tau  reference
    {0, 1, 10, 150, 88, true},      // P0  R_HI1 (A+3V3) -> R_SP10 (AGND)
    {0, 7, 8, 5150, 3016, false},   // P1  R_LO2 (AGND)  -> REF_A
    {0, 5, 8, 5150, 3016, false},   // P2  R_HI2 (A+3V3) -> REF_A
    {0, 7, 9, 650, 381, false},     // P3  R_LO2 (AGND)  -> REF_B
    {1, 7, 6, 5150, 1856, false},   // P4  R_LO4 (AGND)  -> REF_C
    {1, 5, 3, 150, 54, true},       // P5  R_HI4 (A+3V3) -> R_LO3 (AGND)
};

int d_settle_index(const Point* pts, int n, int32_t settled)
{
    if(pts == nullptr || n <= 0) return -1;

    // Walk backwards. The definition is "settled from here on", so the answer
    // is the start of the final settled run -- which a forward scan can only
    // find by rescanning the tail at every candidate.
    int first = -1;
    for(int i = n - 1; i >= 0; --i)
    {
        const int32_t d = pts[i].mean - settled;
        const int32_t a = (d < 0) ? -d : d;
        if(a > kSettleCounts) break;
        first = i;
    }
    return first;
}

Gates settle_gates(const RunSummary& s)
{
    Gates g{};

    // G1: both reference pairs must collapse to the first grid step or the
    // next. They step between 0 ohm ties, so anything slower is the
    // instrument, and the section 7 subtraction would be a guess.
    g.g1_knee = true;
    for(int p = 0; p < kSettlePairs; ++p)
    {
        if(!kSettlePlan[p].is_reference) continue;
        if(s.knee_ns[p] < 0 || s.knee_ns[p] > kKneeMaxNs) g.g1_knee = false;
    }

    // G2: the measured noise floor must leave the mean's own uncertainty well
    // inside the decision threshold.
    g.g2_floor = s.b0 >= 0 && s.b0 <= kFloorMaxCounts;

    // G3 (fix round 4): the per-point MEANS in the settled region must agree
    // with EACH OTHER within kSettleCounts -- peak to peak, directly, with no
    // b0-scaled floor, because the mean of kRepeats conversions is already
    // far quieter than the raw single-sample band the old version of this
    // gate bounded and needs no floor to avoid over-refusing a quiet run.
    // See RunSummary::settled_mean_spread's comment in settle_plan.h for the
    // three good knees that raw-per-sample version refused.
    //
    // This gate reads the same INPUTS as d_settle_index() -- the per-point
    // means -- but it is NOT the same statistic, and the difference is not
    // cosmetic:
    //   - d_settle_index() bounds |mean[i] - settled| <= kSettleCounts for
    //     every i >= knee: a deviation from a REFERENCE. That is section 6's
    //     criterion, half an LSB of 12 bit.
    //   - this gate bounds mean_max - mean_min over the same region:
    //     PEAK TO PEAK, with no reference in it.
    // Finding a knee at all therefore already bounds the peak-to-peak spread
    // at 2 * kSettleCounts, by construction -- two points can sit at most one
    // full band apart, at +kSettleCounts and -kSettleCounts from the
    // reference. So G3 at kSettleCounts is deliberately about 2x STRICTER
    // than the spec's own criterion, not a restatement of it, and a run that
    // fails it has still met section 6's criterion at every settled point.
    // What such a run has not shown is a settled region tighter than half the
    // band the knee rule allows. Read the failure that way and no other; the
    // spec's section 7a never defined this gate in terms commensurable with
    // its own section 6, and reading it as "the board fails the criterion"
    // is the error that reading produced once already.
    //
    // settled_mean_spread already excludes the transient before each pair's
    // own knee and is 0 when no pair contributed a settled region, which G1
    // already catches separately.
    g.g3_band = s.settled_mean_spread >= 0 && s.settled_mean_spread <= kSettleCounts;

    // G4: aperture jitter inside one grid step, and a latency the conversion
    // model does not forbid. A negative mean means settle_probe.cpp's
    // measured conversion-time subtraction (fix round 4: computed from the
    // live-measured ADC clock, not a fixed literal) came out larger than the
    // real span, which invalidates every delay in the run.
    g.g4_jitter = s.lat_mean_ns >= 0 && s.lat_max_ns >= s.lat_min_ns
                  && (s.lat_max_ns - s.lat_min_ns) <= kJitterMaxNs;

    return g;
}

// --- Fix round 4: the sampling-time choice, per pair ---
//
// SHELL_SETTLE_CLK (shell/settle_probe.cpp, fix rounds 2-3) measured the
// ADC1 clock directly on the coupon board: span_short_cyc=2311,
// span_long_cyc=31286, smp_short_tenths=165, smp_long_tenths=3875 -- i.e.
// (31286-2311)/371 = 78.0997 core cycles per ADC cycle at the 480 MHz core
// clock, which is 6.146 MHz, not the 12.29 MHz ADC_CLOCK_ASYNC_DIV2's own
// comment (and this whole spec) assumed. Exactly half -- one prescaler step.
//
// This constant is the measured span pair itself, not a hand-rounded "6.146
// MHz", so the derivation stays reproducible from the board reading in
// task-4-report.md's fix-round-4 section rather than from a transcription of
// it.
constexpr double kClkSpanShortCyc = 2311.0;
constexpr double kClkSpanLongCyc  = 31286.0;
constexpr double kClkDeltaAdcCyc  = 371.0;   // 387.5 - 16.5 sampling cycles
constexpr double kCoreClockHz     = 480e6;

constexpr double kMeasuredAdcHz =
    kCoreClockHz * kClkDeltaAdcCyc / (kClkSpanLongCyc - kClkSpanShortCyc);

// This is a fixed hardware/firmware clock-configuration constant, not a
// per-boot variable: ADC_CLOCK_ASYNC_DIV2 and the PLL3 config that feeds it
// are compiled into the firmware, so the same build produces the same ratio
// every boot (modulo the sub-percent measurement noise fix round 2's
// SHELL_SETTLE_CAL already showed on the timed path). That determinism is
// exactly why it is safe to bake in here, in a function this file's own
// tests run on the host with no board at all -- unlike
// shell/settle_probe.cpp's per-boot latency arithmetic (fix round 4), which
// recomputes this same ratio fresh every boot instead of trusting a baked-in
// figure, because THAT number is something the probe measures itself and
// must never also exist as a literal. The two are different questions: one
// is "what did THIS boot's ADC do" (measure, always), the other is "which
// fixed sampling rung does a fixed hardware constant require" (a design-time
// decision that has to be answered identically on a host with no board, so
// it needs a constant to answer with -- the most defensible one available is
// the freshly measured clock, not the wrong 12.29 MHz assumption it
// replaces).
//
// R_ADC and C_ADC are NOT re-derived here: they are the exact figures
// already cited in docs/hardware/settle-budget.md section 1 (reused by
// tools/settle_budget.py's `terms()`), which this project's own tau9_ns
// column (above) was already computed alongside. Repeating the citation
// rather than inventing a new one:
//   C_ADC = 4 pF   -- "ST, STM32H7 sample-and-hold" -- class: datasheet
//   R_ADC = 2000 ohm -- "ST community figure for slow channels" -- class:
//           estimate, explicitly NOT datasheet-verbatim (settle-budget.md
//           section 1 also notes it "enters term B only... never the
//           binding term", i.e. getting it wrong does not change much)
constexpr double kRAdcOhm   = 2000.0;
constexpr double kCAdcFarad = 4e-12;

// 9.01 = ln(2^13), half an LSB of 12 bit -- the same criterion
// tools/settle_budget.py's `_time_constants(12)` computes and this whole
// spec already rounds to "9.01" everywhere else (tau9_ns above, the test
// file's kLn8192).
constexpr double kAcqNSigma = 9.01;

const int kSamplingLadderTenths[kSamplingLadderLen] = {
    15, 25, 85, 165, 325, 645, 3875, 8105,
};

int sample_time_index_for(uint32_t r_src_ohm)
{
    const double tau_acq_s  = (static_cast<double>(r_src_ohm) + kRAdcOhm) * kCAdcFarad;
    const double required_s = kAcqNSigma * tau_acq_s;

    for(int i = 0; i < kSamplingLadderLen; ++i)
    {
        const double window_s =
            (static_cast<double>(kSamplingLadderTenths[i]) / 10.0) / kMeasuredAdcHz;
        if(window_s >= required_s) return i;
    }
    // No rung covers it -- the longest one is the best available answer,
    // not a crash or an out-of-range index. A caller comparing a real knee
    // against this pair's offset will see that offset be large and the
    // acquisition genuinely underprovisioned, which is a finding, not a bug
    // in this function.
    return kSamplingLadderLen - 1;
}

} // namespace shell
