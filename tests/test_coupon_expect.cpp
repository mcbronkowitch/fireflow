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

namespace {

constexpr int kN = shell::scan_steps(shell::kCouponChain);
static_assert(kN == 24, "the fixtures below are written out step by step");

// What the fabricated coupon actually produced on 2026-09-17: one block from
// the rescanning probe over USB-CDC, all seven pots at the counter-clockwise
// stop. These are measurements, not invented numbers, and they are kept
// because this exact board reported four failures when it was judged against
// absolute full scale -- every one of them the yardstick and not the board.
constexpr uint16_t kMeasured[kN] = {
    1,     63485, 1, 0, 1, 63485, 7,     1,
    31716, 31737, 0, 0, 0, 0,     0,     0,
    0,     63486, 0, 0, 1, 63485, 31758, 0};

// A synthetic scan with one value per expectation class, for the cases where
// the question is the guard rather than the board.
struct Scan
{
    uint16_t v[kN];
};

Scan make_scan(uint16_t low, uint16_t high, uint16_t mid) {
    Scan s{};
    for(int i = 0; i < kN; ++i)
        switch(shell::coupon_expect(i)) {
            case shell::Expect::Low: s.v[i] = low; break;
            case shell::Expect::High: s.v[i] = high; break;
            case shell::Expect::Mid: s.v[i] = mid; break;
            case shell::Expect::Unchecked: s.v[i] = 0; break;
        }
    return s;
}

} // namespace

TEST_CASE("coupon span: the board measured on the bench passes whole") {
    const shell::Span span = shell::coupon_span(kMeasured, kN);
    REQUIRE(span.valid);
    CHECK(span.rail == 63485);
    CHECK(span.zero == 0);

    for(int s = 0; s < kN; ++s)
        CHECK(shell::coupon_verdict(shell::coupon_expect(s), kMeasured[s],
                                    span));

    // And the reason this file changed: the same rail ties, judged against
    // full scale, were 2050 counts short of the old bracket. The board was
    // clean; 65535 was never the number it had to reach.
    CHECK(63485 < 65535 - shell::kRailMargin);
}

TEST_CASE("coupon span: a collapsed rail is not allowed to become the rail") {
    // The whole hazard of self-calibration in one case. Every tie is
    // internally consistent -- the rails agree with each other, the grounds
    // agree with each other, the divider sits at exactly half -- so a check
    // that only asks "is everything proportional?" passes a board whose
    // supply is at 46 %. The floor is what refuses it.
    const Scan s = make_scan(0, 30000, 15000);
    const shell::Span span = shell::coupon_span(s.v, kN);
    CHECK_FALSE(span.valid);
    CHECK(shell::coupon_verdict(shell::Expect::High, 30000, span) == false);
    CHECK(shell::coupon_verdict(shell::Expect::Mid, 15000, span) == false);
    CHECK(shell::coupon_verdict(shell::Expect::Low, 0, span) == false);
}

TEST_CASE("coupon span: one open tie poisons the yardstick") {
    Scan s = make_scan(0, 63485, 31742);
    s.v[5] = 0;  // R_HI2 open: the mean would still look plausible
    const shell::Span span = shell::coupon_span(s.v, kN);
    CHECK_FALSE(span.valid);
}

TEST_CASE("coupon span: a lifted ground shifts the scale and is refused") {
    const Scan s = make_scan(5000, 63485, 34242);
    CHECK_FALSE(shell::coupon_span(s.v, kN).valid);
}

TEST_CASE("coupon span: swapped rails do not span backwards") {
    const Scan s = make_scan(63485, 0, 31742);
    CHECK_FALSE(shell::coupon_span(s.v, kN).valid);
}

TEST_CASE("coupon span: no scan, no yardstick") {
    CHECK_FALSE(shell::coupon_span(nullptr, kN).valid);
    CHECK_FALSE(shell::coupon_span(kMeasured, 0).valid);
    // Step 0 alone is a pot wiper: no rail tie, no ground tie, no span.
    CHECK_FALSE(shell::coupon_span(kMeasured, 1).valid);
}

TEST_CASE("coupon expect: the verdict brackets are not a matter of taste") {
    using shell::Expect;
    const Scan s = make_scan(0, 63485, 31742);
    const shell::Span span = shell::coupon_span(s.v, kN);
    REQUIRE(span.valid);
    const int mid = (span.zero + span.rail) / 2;

    // A rail tie through 0 ohms has nothing to pull it off the rail, so the
    // margin is generous on purpose -- it catches an open or a swapped net,
    // it does not grade noise.
    CHECK(shell::coupon_verdict(Expect::Low, span.zero, span));
    CHECK(shell::coupon_verdict(Expect::Low, span.zero + shell::kRailMargin,
                                span));
    CHECK_FALSE(shell::coupon_verdict(
        Expect::Low, span.zero + shell::kRailMargin + 1, span));

    CHECK(shell::coupon_verdict(Expect::High, span.rail, span));
    CHECK(shell::coupon_verdict(Expect::High, span.rail - shell::kRailMargin,
                                span));
    CHECK_FALSE(shell::coupon_verdict(
        Expect::High, span.rail - shell::kRailMargin - 1, span));

    // Mid is half the measured span, not half of full scale.
    CHECK(shell::coupon_verdict(Expect::Mid,
                                static_cast<uint16_t>(mid), span));
    CHECK(shell::coupon_verdict(
        Expect::Mid, static_cast<uint16_t>(mid - shell::kMidMargin), span));
    CHECK_FALSE(shell::coupon_verdict(
        Expect::Mid, static_cast<uint16_t>(mid - shell::kMidMargin - 1),
        span));
    CHECK(shell::coupon_verdict(
        Expect::Mid, static_cast<uint16_t>(mid + shell::kMidMargin), span));
    CHECK_FALSE(shell::coupon_verdict(
        Expect::Mid, static_cast<uint16_t>(mid + shell::kMidMargin + 1),
        span));

    // An unfitted pot floats. Anything it reads is allowed, including 0 --
    // and it stays allowed even when the span itself is gone, because an
    // Unchecked channel never claimed anything to begin with.
    const shell::Span dead{0, 0, false};
    CHECK(shell::coupon_verdict(Expect::Unchecked, 0, span));
    CHECK(shell::coupon_verdict(Expect::Unchecked, 65535, span));
    CHECK(shell::coupon_verdict(Expect::Unchecked, 65535, dead));
}
