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
    CHECK(shell::kGridPoints == 65);
    CHECK(shell::kGridStepNs == 100);
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
    shell::RunSummary s = clean_summary();
    s.knee_ns[0] = 200;                  // P0, a reference pair
    CHECK_FALSE(shell::settle_gates(s).g1_knee);
    s = clean_summary();
    s.knee_ns[5] = 200;                  // P5, the other one
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
    shell::RunSummary s = clean_summary();
    s.lat_min_ns = 300;
    s.lat_max_ns = 400;
    CHECK(shell::settle_gates(s).g4_jitter);
    s.lat_max_ns = 401;
    CHECK_FALSE(shell::settle_gates(s).g4_jitter);
}

TEST_CASE("settle gates: G4 refuses a latency the conversion model forbids") {
    // A negative mean latency means start-to-EOC came back shorter than the
    // 25 ADC cycles the conversion is supposed to take. Then the 2034 ns
    // subtraction is wrong, and nothing downstream of it is trustworthy --
    // including the delays every other pair was measured at.
    shell::RunSummary s = clean_summary();
    s.lat_mean_ns = -1;
    CHECK_FALSE(shell::settle_gates(s).g4_jitter);
}
