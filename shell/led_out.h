#pragma once

// The LED law's duties as the word MuxScan latches once per block (Rev A
// P6b-2 spec section 4.3). The P2 crosstalk rule lets LED bits change only in
// the latch that carries the mux address, so a lamp is dimmed by switching it
// over blocks: a duty pattern repeats every steps-1 blocks.
//
// Pure data logic, no hardware type, host tested (tests/test_led_out.cpp).
//
// Spec: ../docs/superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md
#include <cstdint>

namespace shell {

// Bit led_bit[i] set = lamp i (ffctl::LightId) lit in block `frame`; lamps
// whose led_bit is -1 set nothing. Reads ffctl::NUM_LIGHTS entries of `duty`
// and `led_bit`.
// A lamp of duty d (0..steps-1) is on in block f iff
// floor((f+1)*d/(steps-1)) > floor(f*d/(steps-1)): d = 0 never, d = steps-1
// always, d on-blocks spread evenly over each steps-1 blocks.
uint32_t led_word(const int* duty, int steps, uint32_t frame, const int8_t* led_bit);

} // namespace shell
