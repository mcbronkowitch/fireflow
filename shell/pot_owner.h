#pragma once

// Pot ownership (Rev A P6b-2 spec section 2): every parameter has a stored
// value and the stored value is what sounds. A pot does not own its parameter
// until it is moved past a threshold from a reference reading; the moment it
// is, the parameter jumps to the pot's position and follows it. No pickup.
//
// Pure data logic, no hardware type, host tested (tests/test_pot_owner.cpp).
// The firmware wiring sits in panel_scan.cpp, between pot_filter() and the
// knob vector.
//
// Spec: ../docs/superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md
#include <cstdint>

namespace shell {

// PROVISIONAL. 3 x kPotHysteresis (16). The coupon session that measures the
// raw spread of an untouched pot with the LEDs switching has not happened yet
// (docs/hardware/p6b2-led-and-touch-measured.md); its rule is
//   max(2 x worst spread, 3 x kPotHysteresis), rounded up to a multiple of 16.
// This value may change when that session records its numbers.
inline constexpr int kPotTouch = 48;

struct PotOwner
{
    uint16_t ref        = 0;      // the reading the move is measured from
    int      target     = -2;     // the parameter id this pot currently drives
    bool     has_ref    = false;
    bool     owned      = false;
};

enum class Own { Idle, Took, Owned };

// Stop owning; the next valid reading becomes the new reference.
void pot_release(PotOwner& o);

// One reading of one pot.
//   span_valid false -> Idle, nothing taken (no reference either).
//   A target different from o.target releases first and records the new one.
//   Without a reference the raw word becomes it (Idle).
//   Owned stays Owned.
//   |raw - ref| > touch -> Took (the caller writes the pot's CURRENT position,
//   not the reference); the pot is owned from here on.
Own pot_own(PotOwner& o, uint16_t raw, int target, bool span_valid, int touch);

} // namespace shell
