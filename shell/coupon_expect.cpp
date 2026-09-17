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

Span coupon_span(const uint16_t* raw, int steps)
{
    Span span{0, 0, false};
    if(raw == nullptr || steps <= 0) return span;

    uint32_t hi_sum = 0, lo_sum = 0;
    int      hi_n = 0, lo_n = 0;
    uint16_t hi_min = 65535, hi_max = 0, lo_min = 65535, lo_max = 0;

    for(int s = 0; s < steps; ++s)
    {
        const Expect e = coupon_expect(s);
        if(e == Expect::High)
        {
            hi_sum += raw[s];
            ++hi_n;
            if(raw[s] < hi_min) hi_min = raw[s];
            if(raw[s] > hi_max) hi_max = raw[s];
        }
        else if(e == Expect::Low)
        {
            lo_sum += raw[s];
            ++lo_n;
            if(raw[s] < lo_min) lo_min = raw[s];
            if(raw[s] > lo_max) lo_max = raw[s];
        }
    }
    if(hi_n == 0 || lo_n == 0) return span;

    span.zero = static_cast<uint16_t>(lo_sum / static_cast<uint32_t>(lo_n));
    span.rail = static_cast<uint16_t>(hi_sum / static_cast<uint32_t>(hi_n));

    // Four conditions, and every one of them is here because dropping it
    // would let a broken board calibrate itself into a pass:
    //   ties agree  -- one open tie averages into a plausible mean
    //   rail floor  -- a collapsed rail is otherwise adopted AS the rail
    //   zero ceiling-- a lifted AGND shifts the whole scale with it
    //   ordered     -- swapped rails would still span, just backwards
    span.valid = (hi_max - hi_min) <= kTieSpread
                 && (lo_max - lo_min) <= kTieSpread && span.rail >= kRailFloor
                 && span.zero <= kRailMargin && span.rail > span.zero;
    return span;
}

bool coupon_verdict(Expect e, uint16_t raw, const Span& span)
{
    // An unfitted or unasserted channel claims nothing, so it cannot fail --
    // not even for want of a yardstick.
    if(e == Expect::Unchecked) return true;
    if(!span.valid) return false;

    const int v   = raw;
    const int z   = span.zero;
    const int r   = span.rail;
    const int mid = (z + r) / 2;

    switch(e)
    {
        case Expect::Low: return v <= z + kRailMargin;
        case Expect::High: return v >= r - kRailMargin;
        case Expect::Mid: return v >= mid - kMidMargin && v <= mid + kMidMargin;
        case Expect::Unchecked: break;
    }
    return true;
}

} // namespace shell
