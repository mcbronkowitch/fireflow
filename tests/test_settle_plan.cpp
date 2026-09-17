// The six channel pairs the settle probe steps between, and the grid it
// steps them on. Derived from the settle probe spec section 6 and from
// hardware/coupon/scripts/netlist.py; if the two disagree, the netlist wins
// and this file is wrong.
#include <cmath>
#include <vector>
#include <doctest/doctest.h>
#include "../shell/settle_plan.h"
#include "../shell/mux_plan.h"

TEST_CASE("settle plan: the grid is monotonic and ends where the spec says") {
    // Fix round 5: the SPAN (0..6400 ns) is unchanged since section 6, but
    // the step is not -- 138 ns of measured aperture jitter on the coupon
    // board (task-4-report.md, fix round 5) means a 100 ns step claimed
    // resolution the instrument does not have. 65 points at 100 ns became
    // 33 points at 200 ns; the boundary this test checks (grid_ns(last) ==
    // 6400) is exactly the invariant that survives the change.
    CHECK(shell::kGridPoints == 33);
    CHECK(shell::kGridStepNs == 200);
    CHECK(shell::grid_ns(0) == 0u);
    CHECK(shell::grid_ns(shell::kGridPoints - 1) == 6400u);
    for(int i = 1; i < shell::kGridPoints; ++i)
        CHECK(shell::grid_ns(i) > shell::grid_ns(i - 1));
}

TEST_CASE("settle plan: every channel exists on the mux it names") {
    for(int p = 0; p < shell::kSettlePairs; ++p) {
        const shell::SettlePair& sp = shell::kSettlePlan[p];
        CAPTURE(p);
        REQUIRE((sp.group == 0 || sp.group == 1));
        const int n = shell::kCouponChain.channels[sp.group];
        CHECK(sp.from_ch >= 0);
        CHECK(sp.from_ch < n);
        CHECK(sp.to_ch >= 0);
        CHECK(sp.to_ch < n);
        CHECK(sp.from_ch != sp.to_ch);
    }
}

TEST_CASE("settle plan: the predictions are recomputed, not copied") {
    // The model, from settle-budget.md section 1: C_node is C_COM plus 15 pF
    // of stray -- 50 + 15 on the 4067, 25 + 15 on the 4051. The criterion is
    // 9.01 tau, which is ln(8192), half an LSB of 12 bit.
    const double kC[2]   = {65e-12, 40e-12};
    const double kLn8192 = 9.0109;

    for(int p = 0; p < shell::kSettlePairs; ++p) {
        const shell::SettlePair& sp = shell::kSettlePlan[p];
        CAPTURE(p);
        const double tau = static_cast<double>(sp.r_src_ohm) * kC[sp.group];
        const double ns  = tau * kLn8192 * 1e9;
        // Half a nanosecond of slack: the table is integers rounded from the
        // model, so the largest legitimate gap is half a unit. A full
        // nanosecond of slack is too loose -- P3's model value (380.71) sits
        // close enough to the midpoint that an off-by-one typo (380 vs. the
        // correct 381) would slip under a 1.0 ns bound undetected.
        CHECK(std::abs(ns - static_cast<double>(sp.tau9_ns)) < 0.5);
    }
}

TEST_CASE("settle plan: the source impedances follow from the netlist") {
    // Switch on-resistance is 150 ohms in the model. The rail ties add
    // nothing (0 ohm links, netlist.py NEIGHBOURS); the dividers add their
    // two legs in parallel:
    //   REF_A  10k / 10k -> 5000
    //   REF_B   1k /  1k ->  500
    //   REF_C  10k / 10k -> 5000
    const uint32_t kRon = 150;
    CHECK(shell::kSettlePlan[0].r_src_ohm == kRon +    0);  // P0 rail tie
    CHECK(shell::kSettlePlan[1].r_src_ohm == kRon + 5000);  // P1 REF_A
    CHECK(shell::kSettlePlan[2].r_src_ohm == kRon + 5000);  // P2 REF_A
    CHECK(shell::kSettlePlan[3].r_src_ohm == kRon +  500);  // P3 REF_B
    CHECK(shell::kSettlePlan[4].r_src_ohm == kRon + 5000);  // P4 REF_C
    CHECK(shell::kSettlePlan[5].r_src_ohm == kRon +    0);  // P5 rail tie
}

TEST_CASE("settle plan: exactly two pairs are the instrument's own zero") {
    // Section 7: P0 and P5 step between channels tied to a rail through
    // 0 ohms, so their settle is the instrument and not the board. Anything
    // else marked reference would silently become a subtrahend.
    int refs = 0;
    for(int p = 0; p < shell::kSettlePairs; ++p)
        if(shell::kSettlePlan[p].is_reference) ++refs;
    CHECK(refs == 2);
    CHECK(shell::kSettlePlan[0].is_reference);
    CHECK(shell::kSettlePlan[5].is_reference);
    // One per mux, or one of the two muxes has no zero point of its own.
    CHECK(shell::kSettlePlan[0].group == 0);
    CHECK(shell::kSettlePlan[5].group == 1);
}

TEST_CASE("settle plan: the park is long enough to be called settled") {
    // The park IS the reference the curve is compared against, so it must be
    // far past the slowest prediction rather than merely past it.
    uint32_t slowest = 0;
    for(int p = 0; p < shell::kSettlePairs; ++p)
        if(shell::kSettlePlan[p].tau9_ns > slowest)
            slowest = shell::kSettlePlan[p].tau9_ns;
    CHECK(shell::kParkNs >= 5 * slowest);
    // And the grid must reach past the slowest prediction, or the run can
    // only ever report "did not settle" for that pair.
    CHECK(shell::grid_ns(shell::kGridPoints - 1) > 2 * slowest);
}

namespace {

// A curve that is flat at `settled` from `knee` onward and far away before
// it, with a fixed band on every point.
std::vector<shell::Point> make_curve(int knee, int32_t settled, int32_t band) {
    std::vector<shell::Point> pts;
    for(int i = 0; i < shell::kGridPoints; ++i) {
        const int32_t m = (i >= knee) ? settled : settled - 4000;
        pts.push_back(shell::Point{m, m - band / 2, m + band / 2});
    }
    return pts;
}

// A summary that passes every gate, for tests that break exactly one thing.
shell::RunSummary clean_summary() {
    shell::RunSummary s{};
    for(int p = 0; p < shell::kSettlePairs; ++p) s.knee_ns[p] = 100;
    s.b0          = 12;
    s.widest_band = 30;
    s.lat_min_ns  = 300;
    s.lat_max_ns  = 340;
    s.lat_mean_ns = 320;
    return s;
}

} // namespace

TEST_CASE("settle reduction: the knee is the first point that stays settled") {
    const std::vector<shell::Point> c = make_curve(7, 31742, 4);
    CHECK(shell::d_settle_index(c.data(), shell::kGridPoints, 31742) == 7);
}

TEST_CASE("settle reduction: a point that settles and leaves does not count") {
    // The whole reason the definition says "and stays within it": a curve
    // that crosses the band early and wanders back out has not settled, and
    // reporting the first crossing would report a ringing node as a fast one.
    std::vector<shell::Point> c = make_curve(7, 31742, 4);
    c[3] = shell::Point{31742, 31740, 31744};   // an early visit
    CHECK(shell::d_settle_index(c.data(), shell::kGridPoints, 31742) == 7);
}

TEST_CASE("settle reduction: a curve that never settles says so") {
    std::vector<shell::Point> c = make_curve(7, 31742, 4);
    c[shell::kGridPoints - 1].mean = 31742 - 4000;
    CHECK(shell::d_settle_index(c.data(), shell::kGridPoints, 31742) == -1);
}

TEST_CASE("settle reduction: the bracket is half an LSB of 12 bit") {
    std::vector<shell::Point> c = make_curve(0, 31742, 4);
    c[0].mean = 31742 + shell::kSettleCounts;
    CHECK(shell::d_settle_index(c.data(), shell::kGridPoints, 31742) == 0);
    c[0].mean = 31742 + shell::kSettleCounts + 1;
    CHECK(shell::d_settle_index(c.data(), shell::kGridPoints, 31742) == 1);
}

TEST_CASE("settle gates: a clean run passes all four") {
    const shell::Gates g = shell::settle_gates(clean_summary());
    CHECK(g.g1_knee);
    CHECK(g.g2_floor);
    CHECK(g.g3_band);
    CHECK(g.g4_jitter);
    CHECK(g.ok());
}

TEST_CASE("settle gates: G1 refuses a reference knee more than one step out") {
    // Fix round 5: kKneeMaxNs now derives from kGridStepNs (200, was 100),
    // so the boundary is computed rather than a literal that would have
    // silently sat exactly ON the new limit instead of past it.
    shell::RunSummary s = clean_summary();
    s.knee_ns[0] = shell::kKneeMaxNs + 1;  // P0, a reference pair
    CHECK_FALSE(shell::settle_gates(s).g1_knee);
    s = clean_summary();
    s.knee_ns[5] = shell::kKneeMaxNs + 1;  // P5, the other one
    CHECK_FALSE(shell::settle_gates(s).g1_knee);
    // A slow knee on a pair under test is the measurement, not a fault.
    s             = clean_summary();
    s.knee_ns[1]  = 3100;
    CHECK(shell::settle_gates(s).g1_knee);
}

TEST_CASE("settle gates: G1 refuses a reference pair that never settled") {
    shell::RunSummary s = clean_summary();
    s.knee_ns[0] = -1;
    CHECK_FALSE(shell::settle_gates(s).g1_knee);
}

TEST_CASE("settle gates: G2 refuses a floor that swallows the criterion") {
    // The load-bearing gate. d_settle decides on the mean of 64 repeats,
    // whose spread is sigma/8, and 64 counts peak-to-peak across 64 samples
    // is about 5.1 sigma -- so the ceiling puts the mean's uncertainty at a
    // fifth of the 8-count threshold. Above it every curve still LOOKS like
    // a curve, which is exactly why this must fail loudly.
    shell::RunSummary s = clean_summary();
    s.b0          = shell::kFloorMaxCounts;
    s.widest_band = shell::kFloorMaxCounts;
    CHECK(shell::settle_gates(s).g2_floor);
    s.b0 = shell::kFloorMaxCounts + 1;
    CHECK_FALSE(shell::settle_gates(s).g2_floor);
}

TEST_CASE("settle gates: G3 measures the band against the measured floor") {
    shell::RunSummary s = clean_summary();
    s.b0          = 10;
    s.widest_band = 30;
    CHECK(shell::settle_gates(s).g3_band);
    s.widest_band = 31;
    CHECK_FALSE(shell::settle_gates(s).g3_band);
}

TEST_CASE("settle gates: G3 never demands better than the criterion itself") {
    // A band below the decision threshold cannot change a verdict, so an
    // exceptionally quiet run must not fail for being quiet. Without the
    // floor at kSettleCounts, b0 = 1 would demand every point inside
    // 3 counts and fail a perfectly good run.
    shell::RunSummary s = clean_summary();
    s.b0          = 1;
    s.widest_band = shell::kSettleCounts;
    CHECK(shell::settle_gates(s).g3_band);
}

TEST_CASE("settle gates: G4 refuses aperture jitter wider than a grid step") {
    // Fix round 5: kJitterMaxNs now derives from kGridStepNs (200, was 100)
    // -- the boundary here is computed from it rather than a literal that
    // would have silently tested the wrong threshold after the grid step
    // changed.
    shell::RunSummary s = clean_summary();
    s.lat_min_ns = 300;
    s.lat_max_ns = 300 + shell::kJitterMaxNs;
    CHECK(shell::settle_gates(s).g4_jitter);
    s.lat_max_ns = 300 + shell::kJitterMaxNs + 1;
    CHECK_FALSE(shell::settle_gates(s).g4_jitter);
}

TEST_CASE("settle gates: G4 refuses a latency the conversion model forbids") {
    // A negative mean latency means start-to-EOC came back shorter than the
    // 25 ADC cycles the conversion is supposed to take. Then the
    // conversion-time subtraction is wrong (settle_probe.cpp computes it
    // from the measured ADC clock as of fix round 4; it used to be a fixed
    // 2034 ns), and nothing downstream of it is trustworthy -- including the
    // delays every other pair was measured at.
    shell::RunSummary s = clean_summary();
    s.lat_mean_ns = -1;
    CHECK_FALSE(shell::settle_gates(s).g4_jitter);
}

// --- Fix round 4: the sampling-time choice, per pair ---
//
// Recomputed here, not copied from shell/settle_plan.cpp -- same pattern as
// "the predictions are recomputed, not copied" above. R_ADC/C_ADC are
// docs/hardware/settle-budget.md section 1's figures (C_ADC: "ST, STM32H7
// sample-and-hold", datasheet; R_ADC: "ST community figure for slow
// channels", explicitly NOT datasheet-verbatim). kAdcHz is SHELL_SETTLE_CLK's
// own board reading (span_short_cyc=2311, span_long_cyc=31286,
// Δsampling=371 ADC cycles, 480 MHz core clock) -- see
// shell/settle_plan.cpp's citation comment for the full derivation.
namespace {

constexpr double kRAdcOhmT   = 2000.0;
constexpr double kCAdcFaradT = 4e-12;
constexpr double kAcqNSigmaT = 9.01;
constexpr double kAdcHzT     = 480e6 * 371.0 / (31286.0 - 2311.0);

double required_acq_s(uint32_t r_src_ohm) {
    return kAcqNSigmaT * (static_cast<double>(r_src_ohm) + kRAdcOhmT) * kCAdcFaradT;
}

double rung_window_s(int idx) {
    return (static_cast<double>(shell::kSamplingLadderTenths[idx]) / 10.0) / kAdcHzT;
}

} // namespace

TEST_CASE("sample time ladder: the ladder itself matches the HAL's eight sampling times") {
    const int kExpectedTenths[shell::kSamplingLadderLen] =
        {15, 25, 85, 165, 325, 645, 3875, 8105};
    for(int i = 0; i < shell::kSamplingLadderLen; ++i) {
        CAPTURE(i);
        CHECK(shell::kSamplingLadderTenths[i] == kExpectedTenths[i]);
    }
}

TEST_CASE("sample time ladder: a source impedance just under a rung's limit "
          "picks that rung, just over picks the next") {
    // The boundary between rung 3 (16.5 cycles) and rung 4 (32.5 cycles):
    // the largest r_src_ohm at which 16.5 cycles is still >= 9.01 tau_acq.
    // Derived from the same formula sample_time_index_for() uses, not
    // hand-copied, so a rounding difference between this file and
    // settle_plan.cpp cannot silently misalign the test with the code it
    // checks.
    const double boundary_r = rung_window_s(3) / (kAcqNSigmaT * kCAdcFaradT) - kRAdcOhmT;
    CAPTURE(boundary_r);
    REQUIRE(boundary_r > 100.0);  // sanity: nowhere near uint32_t/margin edge cases

    const uint32_t just_under = static_cast<uint32_t>(boundary_r) - 1;
    const uint32_t just_over  = static_cast<uint32_t>(boundary_r) + 2;

    CHECK(shell::sample_time_index_for(just_under) == 3);
    CHECK(shell::sample_time_index_for(just_over) == 4);
}

TEST_CASE("sample time ladder: index 0 is enough for the coupon's own smallest impedance") {
    // r_src_ohm = 0 is the floor -- if even that needed more than the
    // shortest rung, the ladder would be useless for this board.
    CHECK(shell::sample_time_index_for(0) == 0);
}

TEST_CASE("sample time ladder: an impedance past the last rung's reach still "
          "gets an answer, not a crash") {
    // 9.01 tau for a huge r_src_ohm exceeds even 810.5 cycles' window --
    // sample_time_index_for() must return the longest rung as its best
    // available answer rather than an out-of-range index.
    CHECK(shell::sample_time_index_for(100000000u) == shell::kSamplingLadderLen - 1);
}

TEST_CASE("sample time ladder: every pair in kSettlePlan gets a rung that "
          "actually satisfies 9.01 tau for its own impedance") {
    for(int p = 0; p < shell::kSettlePairs; ++p) {
        const shell::SettlePair& sp = shell::kSettlePlan[p];
        CAPTURE(p);
        const int idx = shell::sample_time_index_for(sp.r_src_ohm);
        REQUIRE(idx >= 0);
        REQUIRE(idx < shell::kSamplingLadderLen);

        const double required_s = required_acq_s(sp.r_src_ohm);
        CHECK(rung_window_s(idx) >= required_s);

        // Not merely A rung that covers it -- THE shortest one. A function
        // that always answered the slowest rung would pass the check above
        // and still be the wrong function; this is what makes the test able
        // to fail on that bug.
        if(idx > 0) CHECK(rung_window_s(idx - 1) < required_s);
    }
}

TEST_CASE("settle gates: G1 and G4's grid-step gates derive from kGridStepNs, "
          "not a second and third copy of it") {
    // Fix round 5: kKneeMaxNs and kJitterMaxNs both mean "one grid step".
    // Before this round they were two separate literal 100s that had to be
    // kept equal to kGridStepNs (and to each other) by hand -- this test is
    // what would have caught a future edit that changed the grid step
    // without changing both gates to match.
    CHECK(shell::kKneeMaxNs == shell::kGridStepNs);
    CHECK(shell::kJitterMaxNs == shell::kGridStepNs);
}
