#pragma once

// The write side of the panel scan, with no hardware type in it -- same
// arrangement as controls.h and for the same reason: this is where a wrong
// address pattern is one visible line instead of a knob that misbehaves on a
// board.
//
// Topology (docs/hardware/io-budget.md §3): up to eight CD74HC4067 share the
// four raw ADC pins; their address lines and their enables ride on the same
// 74HC595 chain that carries the LEDs, which is what makes the whole panel
// cost zero GPIOs and is the reason the 4-bit SD slot fits. One STEP of the
// scan is one address plus one enabled group; the four sense pins are then
// read in parallel, so a step yields four channels.
#include <cstdint>

namespace shell {

// The first of the raw ADC pins, as an index into libDaisy's patch_sm
// channel enum (CV_1..CV_8 = 0..7, then ADC_9 = 8). It is a number here and
// not the enum constant because this header may not include a hardware
// header -- mux_scan.cpp static_asserts the two against each other.
inline constexpr int kSenseAdcBase = 8;

inline constexpr int kMaxGroups = 2;

// One board's chain, as data. Two boards exist: the shipping panel and the
// test coupon, and they differ in every number below. This is a value and
// not a set of #defines so that the host test can run the same assertions
// against both -- a wrong address pattern is a line here and a knob that
// misbehaves on a board there.
struct ChainProfile
{
    int sense_pins;        // raw ADC pins this board populates
    int sense_adc_base;    // index of the first of them in patch_sm's enum
    int groups;            // enable lines, one per group
    int channels[kMaxGroups];        // channels on that group's chip
    int sense_of_group[kMaxGroups];  // sense pin carrying it, -1 = all of them
    int chain_bits;        // bits clocked per step; the bit-bang cost scales
    int addr_shift;
    int enable_shift;
    int led_shift;
    int led_bits;
};

// The shipping panel. 32 = four 74HC595: 19 LEDs (what FireflowHW draws
// today), four address lines, two enables, seven spare. Up to eight
// CD74HC4067 share the four raw ADC pins (io-budget section 3), which is
// what makes the panel cost zero GPIOs. Demand today is 67 pot positions,
// so the 128 channels are headroom, not a plan.
inline constexpr ChainProfile kPanelChain{
    4, kSenseAdcBase, 2, {16, 16}, {-1, -1}, 32, 0, 4, 8, 19};

// The test coupon (hardware/coupon/). Two 74HC595 = 16 bits, eight LEDs, one
// CD74HC4067 on ADC_9 and one CD74HC4051 on ADC_10 -- so the two groups do
// NOT have the same channel count, and each sits on its own sense pin.
// Derivation of the bit order: netlist.py:268 plus MSB-first clocking
// through U_SR1.QH' -> U_SR2.SER.
inline constexpr ChainProfile kCouponChain{
    2, kSenseAdcBase, 2, {16, 8}, {0, 1}, 16, 0, 4, 6, 8};

constexpr int scan_steps(const ChainProfile& p)
{
    int n = 0;
    for(int g = 0; g < p.groups; ++g) n += p.channels[g];
    return n;
}

constexpr int mux_total(const ChainProfile& p)
{
    return scan_steps(p) * p.sense_pins;
}

struct StepPattern
{
    uint8_t address;      // 0..channels[group]-1
    uint8_t enable_mask;  // active low: exactly one group's bit is 0
};

StepPattern step_pattern(const ChainProfile& p, int step);

// The group a step belongs to, or -1 for a step that does not exist.
int group_of_step(const ChainProfile& p, int step);

// The channel a sense pin carries during `step`, or -1 for an index that does
// not exist. Out of range gets an answer instead of an assumption: a
// half-seated chip produces steps nobody planned, and an access past the end
// would be a crash inside the audio callback.
int mux_channel(const ChainProfile& p, int step, int sense);

// The chain word for a step, with `leds` in the LED field.
uint32_t chain_word(const ChainProfile& p, StepPattern s, uint32_t leds);

} // namespace shell
