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

// How far two 0 ohm ties to the SAME net may read apart before the pair stops
// being a yardstick. 0.5 % of full scale. Measured 2026-09-17 the four rail
// ties spanned 2 counts, so this is three orders of magnitude of slack -- it
// exists to notice that one tie is open, not to grade the supply.
inline constexpr uint16_t kTieSpread = 328;

// The lowest reading that may still be called "the rail". Self-calibration
// must not be allowed to adopt a collapsed supply as its own reference and
// then pass all seventeen asserting channels against it: an open JP_3V3, a
// loaded-down regulator or a mux that never enables would all read as a
// perfectly consistent board. 90 % of full scale.
inline constexpr uint16_t kRailFloor = 58982;

// The yardstick the board brings with it. The rail ties are 0 ohm links to
// A+3V3 and AGND, so they are not merely channels to check -- they are the
// calibration, and everything else is judged against them.
//
// This is not a refinement. Measured 2026-09-17, the rail ties read 63485 of
// 65535 while the three dividers sat within 0.08 % of half of THAT: the ADC's
// reference and the coupon's analog rail are simply not the same node, and
// they differ by 3.1 %. Judged against absolute full scale all four rail ties
// failed and the board looked broken. Judged against the span it is what it
// is -- a clean board behind a reference that reads high. Full scale grades
// the reference; the span grades the board.
struct Span
{
    uint16_t zero;   // what the AGND ties read
    uint16_t rail;   // what the A+3V3 ties read
    bool     valid;  // false when the ties disagree or the rail is not one
};

// The expectation for a scan step of kCouponChain, or Unchecked for a step
// that does not exist.
Expect coupon_expect(int step);

// The span a completed scan measured for itself. `raw` holds `steps` entries
// in scan order. An invalid span is a verdict in its own right: it means the
// board never established a yardstick, and nothing that asserts can pass.
Span coupon_span(const uint16_t* raw, int steps);

// Whether a raw 16-bit conversion satisfies an expectation, judged against
// the span the same scan measured.
bool coupon_verdict(Expect e, uint16_t raw, const Span& span);

} // namespace shell
