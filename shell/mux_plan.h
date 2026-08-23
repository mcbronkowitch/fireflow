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

inline constexpr int kSensePins   = 4;   // raw ADC pins carrying a mux output
inline constexpr int kMuxGroups   = 2;   // 2 groups x 4 chips = 8 chips
inline constexpr int kMuxChannels = 16;  // CD74HC4067
inline constexpr int kScanSteps   = kMuxGroups * kMuxChannels;   // 32
inline constexpr int kMuxTotal    = kScanSteps * kSensePins;     // 128

// Bits clocked out per step, and the number the bit-bang cost scales with --
// so it is a constant with a derivation and not a round figure. 32 = four
// 74HC595: 19 LEDs (what FireflowHW draws today), four address lines, two
// enables, seven spare. Demand today is 67 pot positions (io-budget §3), so
// the 128 channels above are headroom, not a plan.
inline constexpr int kChainBits = 32;

inline constexpr int kAddrShift   = 0;  // four address lines, bits 0..3
inline constexpr int kEnableShift = 4;  // one active-low enable per group
inline constexpr int kLedShift    = 8;  // 19 LED bits from here up

struct StepPattern
{
    uint8_t address;      // 0..kMuxChannels-1
    uint8_t enable_mask;  // active low: exactly one group's bit is 0
};

StepPattern step_pattern(int step);

// The channel a sense pin carries during `step`, or -1 for an index that does
// not exist. Out of range gets an answer instead of an assumption: a
// half-seated chip produces steps nobody planned, and an access past the end
// would be a crash inside the audio callback.
int mux_channel(int step, int sense);

// The chain word for a step, with `leds` in the LED field.
uint32_t chain_word(StepPattern p, uint32_t leds);

} // namespace shell
