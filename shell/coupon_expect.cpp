#include "coupon_expect.h"

#include "mux_plan.h"

namespace shell {

namespace {

// MUX16, proof/review.md section 2. Pot wipers sit on the even channels of
// the first eight, with their two neighbours at opposite rails -- that
// arrangement is requirement 5 and the reason the rails appear twice.
constexpr Expect kMux16[16] = {
    Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
    Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
    Expect::Mid,       Expect::Mid,  Expect::Low,       Expect::Low,
    Expect::Low,       Expect::Low,  Expect::Low,       Expect::Low};

constexpr Expect kMux8[8] = {
    Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
    Expect::Unchecked, Expect::High, Expect::Mid,       Expect::Low};

} // namespace

Expect coupon_expect(int step)
{
    const int g = group_of_step(kCouponChain, step);
    if(g < 0) return Expect::Unchecked;
    const int addr = (g == 0) ? step : step - kCouponChain.channels[0];
    return (g == 0) ? kMux16[addr] : kMux8[addr];
}

bool coupon_verdict(Expect e, uint16_t raw)
{
    switch(e)
    {
        case Expect::Low: return raw <= kRailMargin;
        case Expect::High: return raw >= 65535 - kRailMargin;
        case Expect::Mid:
            return raw >= 32768 - kMidMargin && raw <= 32768 + kMidMargin;
        case Expect::Unchecked: break;
    }
    return true;
}

} // namespace shell
