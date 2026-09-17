// The six channel pairs the settle probe steps between, and the grid it
// steps them on. Derived from the settle probe spec section 6 and from
// hardware/coupon/scripts/netlist.py; if the two disagree, the netlist wins
// and this file is wrong.
#include <cmath>
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
