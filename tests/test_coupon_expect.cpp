// What the coupon must read, per channel. The table is derived from
// hardware/coupon/scripts/netlist.py and nothing else; if the two disagree,
// the netlist wins and this file is wrong.
#include <doctest/doctest.h>
#include "../shell/coupon_expect.h"
#include "../shell/mux_plan.h"

TEST_CASE("coupon expect: the 4067's rail ties and dividers") {
    using shell::Expect;
    // MUX16 channel plan, proof/review.md section 2:
    //  0,2,4,6  RV1..RV4 wipers      -- pots, not asserted
    //  1,5      R_HI1/R_HI2 -> A+3V3
    //  3,7      R_LO1/R_LO2 -> AGND
    //  8        REF_A 10k/10k        -- mid scale
    //  9        REF_B 1k/1k          -- mid scale
    //  10..15   R_SP10..15 -> AGND
    const Expect want[16] = {
        Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
        Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
        Expect::Mid,       Expect::Mid,  Expect::Low,       Expect::Low,
        Expect::Low,       Expect::Low,  Expect::Low,       Expect::Low};
    for(int a = 0; a < 16; ++a)
        CHECK(shell::coupon_expect(a) == want[a]);
}

TEST_CASE("coupon expect: the 4051's rail ties and divider") {
    using shell::Expect;
    // MUX8 starts at step 16. Channels: 0,2,4 = RV5..RV7 wipers; 1,5 =
    // R_HI3/R_HI4 -> A+3V3; 3,7 = R_LO3/R_LO4 -> AGND; 6 = REF_C 10k/10k.
    const Expect want[8] = {
        Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
        Expect::Unchecked, Expect::High, Expect::Mid,       Expect::Low};
    for(int a = 0; a < 8; ++a)
        CHECK(shell::coupon_expect(16 + a) == want[a]);
}

TEST_CASE("coupon expect: a step that does not exist is unchecked") {
    CHECK(shell::coupon_expect(-1) == shell::Expect::Unchecked);
    CHECK(shell::coupon_expect(shell::scan_steps(shell::kCouponChain))
          == shell::Expect::Unchecked);
}

TEST_CASE("coupon expect: the verdict brackets are not a matter of taste") {
    using shell::Expect;
    // 16-bit conversions, full scale 65535. A rail tie through 0 ohms has
    // nothing to pull it off the rail, so the margin is generous on purpose
    // -- it is there to catch an open or a swapped net, not to grade noise.
    CHECK(shell::coupon_verdict(Expect::Low, 0));
    CHECK(shell::coupon_verdict(Expect::Low, shell::kRailMargin));
    CHECK_FALSE(shell::coupon_verdict(Expect::Low, shell::kRailMargin + 1));

    CHECK(shell::coupon_verdict(Expect::High, 65535));
    CHECK(shell::coupon_verdict(Expect::High, 65535 - shell::kRailMargin));
    CHECK_FALSE(shell::coupon_verdict(Expect::High,
                                      65535 - shell::kRailMargin - 1));

    // Mid scale is 32767 or 32768; both dividers are two equal resistors.
    CHECK(shell::coupon_verdict(Expect::Mid, 32768));
    CHECK(shell::coupon_verdict(Expect::Mid, 32768 - shell::kMidMargin));
    CHECK_FALSE(shell::coupon_verdict(Expect::Mid,
                                      32768 - shell::kMidMargin - 1));

    // An unfitted pot floats. Anything it reads is allowed, including 0.
    CHECK(shell::coupon_verdict(Expect::Unchecked, 0));
    CHECK(shell::coupon_verdict(Expect::Unchecked, 65535));
}
