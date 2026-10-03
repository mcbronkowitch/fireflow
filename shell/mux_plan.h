#pragma once

// The write side of the panel scan, with no hardware type in it -- same
// arrangement as controls.h and for the same reason: this is where a wrong
// address pattern is one visible line instead of a knob that misbehaves on a
// board.
//
// Two step models (spec 2026-10-02-rev-a-p6a-panel-scan-design.md section 3.1):
//
//   sequential -- one group (chip) is enabled per step, and the steps walk
//                 group 0's channels, then group 1's. Every coupon probe was
//                 measured this way (kCouponChain), and SHELL_MUX_PROBE priced
//                 its CPU cost on kPanelChain.
//   parallel   -- each sense pin owns the groups wired to it, in group order,
//                 and at step k every sense pin enables the group holding its
//                 k-th channel. The address lines are shared by every chip, so
//                 every enabled group sees the same address. A sense pin whose
//                 channels are exhausted has all of its groups disabled. This
//                 is Rev A (P2 section 3: one mux per sense pin per step).
#include <cstdint>

namespace shell {

// The first of the raw ADC pins, as an index into libDaisy's patch_sm
// channel enum (CV_1..CV_8 = 0..7, then ADC_9 = 8). It is a number here and
// not the enum constant because this header may not include a hardware
// header -- mux_scan.cpp static_asserts the two against each other.
inline constexpr int kSenseAdcBase = 8;

// Rev A has ten muxes, each with its own enable (P2 section 3).
inline constexpr int kMaxGroups = 10;

// One board's chain, as data. This is a value and not a set of #defines so
// that the host test can run the same assertions against every profile -- a
// wrong address pattern is a line here and a knob that misbehaves on a board
// there.
struct ChainProfile
{
    int sense_pins;        // raw ADC pins this board populates
    int sense_adc_base;    // index of the first of them in patch_sm's enum
    int groups;            // enable lines, one per group
    int channels[kMaxGroups];        // channels on that group's chip
    int sense_of_group[kMaxGroups];  // sense pin carrying it, -1 = all of them
                                     // (sequential profiles only)
    int chain_bits;        // bits clocked per step; the bit-bang cost scales
    int addr_shift;
    int enable_shift;
    int led_shift;
    int led_bits;
    int button_bit;        // index into the bits shifted out of the 165, -1 = none
    int  addr_bits      = 4;      // address lines on the chain
    bool parallel_sense = false;  // the step model, see the top of this file
};

// The pre-P2 panel draft: four 74HC595 = 32 bits, 19 LEDs, four address
// lines, two 16-channel groups on all four sense pins. It is no longer the
// panel -- Rev A is kRevaChain in generated_panel_map.h -- but it is the
// profile SHELL_MUX_PROBE's CPU cost was measured against
// (docs/bench/2026-08-23-978cbaf-shell-mux-placement.md), and images with
// neither SHELL_COUPON_PROBE nor SHELL_PANEL_SCAN keep it.
inline constexpr ChainProfile kPanelChain{
    4, kSenseAdcBase, 2, {16, 16}, {-1, -1}, 32, 0, 4, 8, 19, -1};

// The test coupon (hardware/coupon/). Two 74HC595 = 16 bits, eight LEDs, one
// CD74HC4067 on ADC_9 and one CD74HC4051 on ADC_10 -- so the two groups do
// NOT have the same channel count, and each sits on its own sense pin.
// Derivation of the bit order: netlist.py:267 plus MSB-first clocking
// through U_SR1.QH' -> U_SR2.SER. Sequential: every coupon probe was
// measured with exactly these patterns.
inline constexpr ChainProfile kCouponChain{
    2, kSenseAdcBase, 2, {16, 8}, {0, 1}, 16, 0, 4, 6, 8, 7};

// The coupon's wiring, scanned with Rev A's model: the 4067 and the 4051
// enabled together on their separate sense pins for steps 0-7, the 4067
// alone for 8-15. The coupon play image runs it so the coupon rehearses Rev
// A's pattern (spec section 3.2).
inline constexpr ChainProfile kCouponPlayChain{
    2, kSenseAdcBase, 2, {16, 8}, {0, 1}, 16, 0, 4, 6, 8, 7, 4, true};

// One mux input, named by group (chip) and channel.
struct MuxChannel
{
    int group;
    int ch;
};

// The channels of every group wired to `sense`.
constexpr int sense_channels(const ChainProfile& p, int sense)
{
    int n = 0;
    for(int g = 0; g < p.groups; ++g)
        if(p.sense_of_group[g] == sense) n += p.channels[g];
    return n;
}

constexpr int scan_steps(const ChainProfile& p)
{
    int n = 0;
    if(!p.parallel_sense)
    {
        for(int g = 0; g < p.groups; ++g) n += p.channels[g];
        return n;
    }
    for(int s = 0; s < p.sense_pins; ++s)
        if(sense_channels(p, s) > n) n = sense_channels(p, s);
    return n;
}

constexpr int mux_total(const ChainProfile& p)
{
    return scan_steps(p) * p.sense_pins;
}

struct StepPattern
{
    uint8_t  address;      // the shared address lines
    uint16_t enable_mask;  // active low, one bit per group
};

// The chain's address and enables for `step`. A step that does not exist
// parks the scan with every enable off.
StepPattern step_pattern(const ChainProfile& p, int step);

// The group a SEQUENTIAL step belongs to, or -1 for a step that does not
// exist -- and always -1 on a parallel profile, where a step has one group
// per sense pin; ask group_at() there.
int group_of_step(const ChainProfile& p, int step);

// The group the scan reads on sense pin `sense` during `step`, or -1 when no
// live channel reaches that pin (it does not exist, its group is not the
// enabled one, or -- parallel -- its channels are exhausted and the node
// floats). Both models.
int group_at(const ChainProfile& p, int step, int sense);

// The channel group_at()'s group is on during `step`, or -1 where group_at()
// says -1.
int channel_at(const ChainProfile& p, int step, int sense);

// The index g_mux_raw stores (step, sense) under, or -1 for an index that
// does not exist. This is an index bijection over (step, sense) pairs, not a
// claim about the board: callers that store values must ask sense_live()
// first, because a pin with no live channel floats.
int mux_channel(const ChainProfile& p, int step, int sense);

// Whether sense pin `sense` carries a live channel during `step`:
// group_at() >= 0.
bool sense_live(const ChainProfile& p, int step, int sense);

// The step that selects channel `ch` on group `group`, or -1 out of range --
// an out-of-range address would still select SOME channel and hand back a
// foreign knob's voltage. The bound is the group's own: the coupon's two
// groups are 16 and 8 channels.
int step_of(const ChainProfile& p, int group, int ch);

// The chain word for a step, with `leds` in the LED field. The address is
// masked to addr_bits, the enables to the profile's groups, the LEDs to
// led_bits, so no field can reach another.
uint64_t chain_word(const ChainProfile& p, StepPattern s, uint32_t leds);

// Which bit of the 74HC165 return stream carries the board's button, counted
// from the first bit shifted out, or -1 if the board has none.
int button_bit(const ChainProfile& p);

} // namespace shell
