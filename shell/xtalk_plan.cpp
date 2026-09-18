#include "xtalk_plan.h"

namespace shell {

// kXtalkVictimTable lives in the header, and its own comment there says why:
// every word below is built from it in the constant evaluator.

namespace {

// Shorthand for the table below. `rest` carries every bit the case varies;
// the victim's own address and enable are forced by xtalk_word().
constexpr uint32_t w(int v, uint32_t rest)
{
    return xtalk_word(kXtalkVictimTable[v], rest);
}

// Victim indices, so the table reads as the spec's table does.
constexpr int A  = 0;   // REF_A,  group 0, ch 8
constexpr int C  = 1;   // REF_C,  group 1, ch 6
constexpr int B  = 2;   // REF_B,  group 0, ch 9
constexpr int S  = 3;   // R_SP10, group 0, ch 10
constexpr int L  = 4;   // R_LO3,  group 1, ch 3

// The quiet baseline for a victim: the other mux disabled, LEDs dark.
constexpr uint32_t base(int v)
{
    return other_enable_mask(kXtalkVictimTable[v].group);
}

constexpr uint32_t kA3   = 8u << kCouponChain.addr_shift;
constexpr uint32_t kEn16 = victim_enable_mask(0);
constexpr uint32_t kEn8  = victim_enable_mask(1);
constexpr uint32_t kLeds = led_field_mask();

// A Silent, ShiftOnly or Static case: one word, no event.
constexpr XtalkCase one(uint8_t row, int v, uint32_t rest, XtalkKind k,
                        bool prints_inline = false)
{
    return XtalkCase{row, kXtalkVictimTable[v].group, kXtalkVictimTable[v].channel,
                     kXtalkVictimTable[v].r_src_ohm, w(v, rest), w(v, rest), k,
                     false, prints_inline};
}

// A Latch case: two words, one edge.
constexpr XtalkCase edge(uint8_t row, int v, uint32_t rest_a, uint32_t rest_b,
                         bool needs_rv4 = false)
{
    return XtalkCase{row, kXtalkVictimTable[v].group, kXtalkVictimTable[v].channel,
                     kXtalkVictimTable[v].r_src_ohm, w(v, rest_a), w(v, rest_b),
                     XtalkKind::Latch, needs_rv4, false};
}

} // namespace

// Spec section 4's table, in the order it runs. Rows 1 and 2 come first
// because every later row's delta is a difference against row 2's curve, and
// G6 is a difference between rows 2 and 1 -- the firmware holds row 1's five
// curves for the whole block for exactly that reason.
//
// WHICH CHANNEL THE AGGRESSOR MUX LANDS ON IS NOT A FREE PARAMETER. The two
// muxes share A0..A2, so when a case enables the aggressor's mux, its address
// is whatever the victim's low bits happen to be: against REF_A (ch 8) and
// R_SP10 (ch 10) that puts the 4051 on RV5 and RV6, wipers that float until
// the pots are fitted. The floating node sits on MUX8_COM = ADC_10, not on
// the victim's ADC_9, so it does not enter the reading -- but word_a and
// word_b are printed, so which channel it was is recoverable from any
// capture, and it should be in any write-up of rows 3 and 9.
const XtalkCase kXtalkPlan[kXtalkCases] = {
    // Row 1: the floor. No chain access and no print for the whole grid.
    // Does the 8-12 count wander of settle-measured.md section 5 need the
    // scan's own activity?
    one(1, A, base(A), XtalkKind::Silent),
    one(1, C, base(C), XtalkKind::Silent),
    one(1, B, base(B), XtalkKind::Silent),
    one(1, S, base(S), XtalkKind::Silent),
    one(1, L, base(L), XtalkKind::Silent),

    // Row 2: the control. A latch pulse with no bit change, so every row
    // below it is a difference that isolates the bit change from the pulse
    // and the shift that precedes it.
    edge(2, A, base(A), base(A)),
    edge(2, C, base(C), base(C)),
    edge(2, B, base(B), base(B)),
    edge(2, S, base(S), base(S)),
    edge(2, L, base(L), base(L)),

    // Row 3: MUX8_EN_N, both directions, against the three victims on the
    // 4067. The moat-crossing bus and the 4051's own switches, into a read
    // taken on the 16:1.
    edge(3, A, 0u,    kEn8), edge(3, A, kEn8, 0u),
    edge(3, B, 0u,    kEn8), edge(3, B, kEn8, 0u),
    edge(3, S, 0u,    kEn8), edge(3, S, kEn8, 0u),

    // Row 4: the mirror image -- MUX16_EN_N against the two victims on the
    // 4051.
    edge(4, C, 0u,     kEn16), edge(4, C, kEn16, 0u),
    edge(4, L, 0u,     kEn16), edge(4, L, kEn16, 0u),

    // Row 5: a bare address edge across the moat. MUX_A3 toggles with the
    // 16:1 DISABLED, so no mux acts on it and what is left is the trace.
    edge(5, C, kEn16,       kEn16 | kA3), edge(5, C, kEn16 | kA3, kEn16),
    edge(5, L, kEn16,       kEn16 | kA3), edge(5, L, kEn16 | kA3, kEn16),

    // Row 6: the same edge with the 16:1 ENABLED, so it switches channel
    // into ADC_9 while ADC_10 is read. REF_C sits at address 110, so A3
    // moves the 16:1 between channel 6 (RV4's wiper, floating until the pot
    // is in) and channel 14 (AGND) -- which is why these four carry
    // needs_rv4 and are reported as skipped until a build flag says the pot
    // is fitted.
    edge(6, C, 0u,   kA3,  true), edge(6, C, kA3,  0u, true),
    edge(6, L, 0u,   kA3,  true), edge(6, L, kA3,  0u, true),

    // Row 7: eight simultaneous current edges through the single ground
    // join. Both directions, every victim.
    edge(7, A, base(A),         base(A) | kLeds), edge(7, A, base(A) | kLeds, base(A)),
    edge(7, C, base(C),         base(C) | kLeds), edge(7, C, base(C) | kLeds, base(C)),
    edge(7, B, base(B),         base(B) | kLeds), edge(7, B, base(B) | kLeds, base(B)),
    edge(7, S, base(S),         base(S) | kLeds), edge(7, S, base(S) | kLeds, base(S)),
    edge(7, L, base(L),         base(L) | kLeds), edge(7, L, base(L) | kLeds, base(L)),

    // Row 8: the DC shift of the star point under eight LEDs. No grid, no
    // edge -- 64 conversions at each of two held states, and the reader
    // differences them.
    one(8, A, base(A),         XtalkKind::Static), one(8, A, base(A) | kLeds, XtalkKind::Static),
    one(8, C, base(C),         XtalkKind::Static), one(8, C, base(C) | kLeds, XtalkKind::Static),
    one(8, B, base(B),         XtalkKind::Static), one(8, B, base(B) | kLeds, XtalkKind::Static),
    one(8, S, base(S),         XtalkKind::Static), one(8, S, base(S) | kLeds, XtalkKind::Static),
    one(8, L, base(L),         XtalkKind::Static), one(8, L, base(L) | kLeds, XtalkKind::Static),

    // Row 9: digital activity with NO control-line change -- 16 bits shifted,
    // RCLK never pulsed, so the 595 storage register never moves. Read from
    // the 74HC595 datasheet, unmeasured on this board: row 9 against row 2 is
    // the check.
    one(9, A, base(A), XtalkKind::ShiftOnly),
    one(9, C, base(C), XtalkKind::ShiftOnly),
    one(9, B, base(B), XtalkKind::ShiftOnly),
    one(9, S, base(S), XtalkKind::ShiftOnly),
    one(9, L, base(L), XtalkKind::ShiftOnly),

    // Row 10: as row 1, but PrintLine after every grid point, as the settle
    // probe does. USB-CDC and DMA activity, which settle-measured.md section
    // 5 names as an unexamined candidate for the 8-12 count wander.
    one(10, A, base(A), XtalkKind::Silent, true),
    one(10, C, base(C), XtalkKind::Silent, true),
    one(10, B, base(B), XtalkKind::Silent, true),
    one(10, S, base(S), XtalkKind::Silent, true),
    one(10, L, base(L), XtalkKind::Silent, true),
};

} // namespace shell
