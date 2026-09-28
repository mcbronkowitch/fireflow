// Round four's pot table, held against the coupon's own channel plan. If
// this file and hardware/coupon/scripts/design.py disagree, design.py wins
// and this file is wrong.
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
#include <cstring>
#include <doctest/doctest.h>
#include "../shell/coupon_expect.h"
#include "../shell/mux_plan.h"
#include "../shell/pot_plan.h"

TEST_CASE("pot plan: each pot's neighbours sit at opposite rails") {
    // The rig step 5b calls for: only with the neighbours at opposite
    // rails does a step onto the wiper show short settling at all.
    for(int i = 0; i < shell::kPotCount; ++i) {
        const shell::Pot& p = shell::kPots[i];
        CAPTURE(i);
        const int hi = shell::step_of(shell::kCouponChain, p.group, p.hi_ch);
        const int lo = shell::step_of(shell::kCouponChain, p.group, p.lo_ch);
        const int me = shell::step_of(shell::kCouponChain, p.group, p.channel);
        // A step of -1 reads as Unchecked, so a wrong channel number must
        // fail here rather than pass the wiper check below vacuously.
        REQUIRE(hi >= 0);
        REQUIRE(lo >= 0);
        REQUIRE(me >= 0);
        CHECK(shell::coupon_expect(hi) == shell::Expect::High);
        CHECK(shell::coupon_expect(lo) == shell::Expect::Low);
        CHECK(shell::coupon_expect(me) == shell::Expect::Unchecked);
    }
}

TEST_CASE("pot plan: the fitted values are design.py's POT_VALUES") {
    // design.py: RV2 10k, RV4 20k, RV6 10k -- and what is soldered on the
    // board (hardware/coupon/order-bom.md, "The pots").
    CHECK(std::strcmp(shell::kPots[0].name, "RV2") == 0);
    CHECK(shell::kPots[0].r_track_ohm == 10000u);
    CHECK(std::strcmp(shell::kPots[1].name, "RV4") == 0);
    CHECK(shell::kPots[1].r_track_ohm == 20000u);
    CHECK(std::strcmp(shell::kPots[2].name, "RV6") == 0);
    CHECK(shell::kPots[2].r_track_ohm == 10000u);
}

TEST_CASE("pot plan: the mid-travel source impedance is R/4 plus Ron") {
    // A linear pot at 50/50 is two R/2 halves in parallel: R/4. Plus the
    // same 150 ohm switch Ron every other table on this board carries.
    CHECK(shell::pot_r_src_mid(10000u) == 2650u);
    CHECK(shell::pot_r_src_mid(20000u) == 5150u);
}
