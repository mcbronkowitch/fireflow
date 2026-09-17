#pragma once

// What the test coupon must read on each channel, derived from
// hardware/coupon/scripts/netlist.py. Data logic with no hardware type in
// it, for the same reason as mux_plan.h: this is where a wrong expectation
// is a visible line rather than a board that "looks broken" at the bench.
#include <cstdint>

namespace shell {

enum class Expect
{
    Low,        // tied to AGND through 0 ohms
    High,       // tied to A+3V3 through 0 ohms
    Mid,        // a divider of two equal resistors
    Unchecked,  // a pot wiper: floats until the pot is fitted
};

// How far off a rail a rail-tied channel may read, in 16-bit counts. 2 % of
// full scale. Generous on purpose: this catches an open, a swapped net or a
// dead enable, not noise.
inline constexpr uint16_t kRailMargin = 1311;

// How far off mid scale a divider may read. 5 % of full scale, which covers
// 1 % resistors with room to spare and still fails a divider that is not
// there.
inline constexpr uint16_t kMidMargin = 3277;

// The expectation for a scan step of kCouponChain, or Unchecked for a step
// that does not exist.
Expect coupon_expect(int step);

// Whether a raw 16-bit conversion satisfies an expectation.
bool coupon_verdict(Expect e, uint16_t raw);

} // namespace shell
