#pragma once

// What the settle probe steps between, and how it decides. Data and pure
// arithmetic with no hardware type in it, for the same reason as mux_plan.h
// and coupon_expect.h: this is where a wrong constant is a visible line
// rather than a board that "looks slow" at the bench.
//
// Every number here is DERIVED -- from netlist.py for the channels and the
// resistors, from settle-budget.md's model for the predictions. The point of
// the run is to break or confirm them, so nothing in this file may be quoted
// as measured.
#include <cstdint>

namespace shell {

// One measured step: from a fully settled source channel to a target whose
// source impedance is known from the netlist.
struct SettlePair
{
    int      group;         // 0 = the 4067 on ADC_9, 1 = the 4051 on ADC_10
    int      from_ch;       // mux channel, parked and settled before the step
    int      to_ch;         // mux channel under test
    uint32_t r_src_ohm;     // switch Ron plus whatever the netlist wires
    uint32_t tau9_ns;       // 9.01 tau prediction, derived
    bool     is_reference;  // a 0 ohm step: the instrument's own zero (sec. 7)
};

inline constexpr int kSettlePairs = 6;
extern const SettlePair kSettlePlan[kSettlePairs];

// Delay grid: 0 to 6400 ns in 100 ns steps.
inline constexpr int kGridStepNs = 100;
inline constexpr int kGridPoints = 65;

constexpr uint32_t grid_ns(int i)
{
    return static_cast<uint32_t>(i) * static_cast<uint32_t>(kGridStepNs);
}

// Repeats per grid point. Not decoration: min and max across these are where
// jitter in the start-to-aperture latency shows up, and section 7a's G2 reads
// the spread of the settled point as the instrument's noise floor.
inline constexpr int kRepeats = 64;

// Half an LSB of 12 bit on the 16-bit scale: a 12-bit LSB is 16 counts.
inline constexpr int kSettleCounts = 8;

// How long the probe parks on the source channel before stepping. The park is
// the reference the whole curve is measured against, so it is not "past the
// prediction" but far past it -- 6.6x the slowest of them.
inline constexpr uint32_t kParkNs = 20000;

} // namespace shell
