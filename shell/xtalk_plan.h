#pragma once

// What the crosstalk probe holds still, what it fires at it, and how it
// decides. Data and pure arithmetic with no hardware type in it, for the same
// reason as mux_plan.h and settle_plan.h: this is where a wrong word is a
// visible line instead of a board that "looks quiet".
//
// Every chain word here is DERIVED -- from hardware/coupon/scripts/netlist.py
// for which 595 output carries which control net, and from mux_plan.h's
// kCouponChain for the bit layout. Nothing in this file may be quoted as
// measured.
//
// Spec: ../docs/superpowers/specs/2026-09-18-coupon-crosstalk-probe-design.md
#include <cstdint>

#include "mux_plan.h"
#include "settle_plan.h"

namespace shell {

// How the aggressor reaches the board. Printed as kind=%d, so the numbering
// is part of the output format and may not be reordered.
enum class XtalkKind : uint8_t
{
    Silent    = 0,   // no chain access at all for the whole grid
    Latch     = 1,   // shift word_b and pulse RCLK; t0 is that edge
    ShiftOnly = 2,   // shift word_a, no RCLK pulse; the 595 outputs do not move
    Static    = 3,   // hold one word, 64 conversions, no grid
};

// A channel that is read while something else on the board moves.
struct XtalkVictim
{
    int      group;       // 0 = the 4067 on ADC_9, 1 = the 4051 on ADC_10
    int      channel;
    uint32_t r_src_ohm;   // switch Ron plus what the netlist wires; the
                          // attribution axis (spec section 2)
};

inline constexpr int kXtalkVictims = 5;

// In the HEADER and `inline constexpr`, like kCouponChain and unlike
// kSettlePlan, and that is not a style choice: xtalk_plan.cpp builds every
// chain word in kXtalkPlan from this table at compile time, and a constant
// expression may not read a merely-`const` object defined in another
// translation unit -- or in the same one. Putting the table here is what
// lets the word builder run in the constant evaluator, which is where a
// wrong word is a compile error rather than a quiet run.
//
// Spec section 2. Two 5150 ohm dividers, one on each mux, because a
// disturbance that differs between the 4067 and the 4051 is in the chip and
// not in the board; one 650 ohm divider as the impedance control; and one
// 0 ohm tie per mux as the instrument's zero. A delta that grows with R_src
// is charge or current arriving at the node; one that is flat across 0, 650
// and 5150 ohms is in the ground, the reference or the ADC.
//
// The seven pots being unpopulated does not matter to a victim: a pot at mid
// travel is a 5 kohm static source, which REF_A already is.
inline constexpr XtalkVictim kXtalkVictimTable[kXtalkVictims] = {
    // group ch  R_src
    {0,  8, 5150},   // REF_A,  10k/10k divider on the 4067  (netlist.py REFS)
    {1,  6, 5150},   // REF_C,  the same on the 4051
    {0,  9,  650},   // REF_B,  1k/1k divider: the impedance control
    {0, 10,  150},   // R_SP10, spare tied to AGND through 0 R
    {1,  3,  150},   // R_LO3,  the same on the 4051
};

// How many address lines the victim's mux actually reads. The 4067 uses all
// four; the 4051 uses three (netlist.py:163, "the 8:1 uses three of the
// four"). That asymmetry is not a detail -- it is the entire reason MUX_A3
// can be an aggressor against a victim on the 4051 and against nothing else.
constexpr int address_bits_of_group(int group)
{
    return group == 0 ? 4 : 3;
}

constexpr uint32_t victim_address_mask(int group)
{
    return ((1u << address_bits_of_group(group)) - 1u) << kCouponChain.addr_shift;
}

// Enables are active low and one bit each: bit enable_shift + 0 is EN16
// (netlist.py, U_SR1.QE), bit enable_shift + 1 is EN8 (U_SR1.QF).
constexpr uint32_t victim_enable_mask(int group)
{
    return 1u << (kCouponChain.enable_shift + group);
}

constexpr uint32_t other_enable_mask(int group)
{
    return 1u << (kCouponChain.enable_shift + (1 - group));
}

// The whole LED field, bits led_shift .. led_shift + led_bits - 1.
constexpr uint32_t led_field_mask()
{
    return ((1u << kCouponChain.led_bits) - 1u) << kCouponChain.led_shift;
}

// A chain word that selects `v` and takes every other bit from `rest`.
//
// The victim's address bits and its own enable bit are FORCED; everything
// else -- the other mux's enable, A3 when the victim sits on the 4051, and
// the whole LED field -- comes from `rest` and is what a case varies. That is
// what makes "the victim never changes channel" a property of the word
// builder rather than of every hand-written table entry.
constexpr uint32_t xtalk_word(const XtalkVictim& v, uint32_t rest)
{
    const uint32_t addr_mask = victim_address_mask(v.group);
    const uint32_t en_mask   = victim_enable_mask(v.group);
    const uint32_t addr
        = (static_cast<uint32_t>(v.channel) << kCouponChain.addr_shift) & addr_mask;
    // Clearing en_mask out of `rest` is what enables the victim's mux:
    // active low.
    return (rest & ~(addr_mask | en_mask)) | addr;
}

// One entry of the plan table (spec section 4).
struct XtalkCase
{
    uint8_t   row;            // the spec section 4 table row, 1..10
    int       group;          // the victim's mux
    int       channel;        // the victim's channel, held for the whole case
    uint32_t  r_src_ohm;      // the victim's, from the netlist
    uint32_t  word_a;         // the chain word before the event
    uint32_t  word_b;         // what the event latches (== word_a for a control)
    XtalkKind kind;
    bool      needs_rv4;      // true for row 6, the one variant section 2 flags
    bool      prints_inline;  // row 10: PrintLine after every grid point
};

inline constexpr int kXtalkCases = 58;
extern const XtalkCase kXtalkPlan[kXtalkCases];

// The delay the shipping scan gives a mux address before it reads it.
//
// READ FROM shell/, never chosen here. MuxScan::step() reads the sense pins
// for the step it wrote LAST time and only then clocks out the next address
// (mux_scan.cpp:110-131), and mux_scan.h's header comment says so in as many
// words: "the address is clocked out at the end of one audio block and
// sampled at the start of the next, so the block period IS the settle
// window". There is no named constant in shell/ to read -- the value IS the
// audio block period, so the probe asks the board for the block size and the
// sample rate at runtime and prints both derivation inputs beside the result.
//
// 64-bit intermediate on purpose: block_size * 1e9 leaves 32 bits for any
// block size above 4, and a 96-sample block would come back as 1.1 s instead
// of 2 ms -- a plausible-looking number, which is the worst kind.
constexpr uint32_t scan_settle_ns(int block_size, int sample_rate_hz)
{
    if(block_size <= 0 || sample_rate_hz <= 0) return 0u;
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(block_size) * 1000000000ull)
        / static_cast<uint64_t>(sample_rate_hz));
}

} // namespace shell
