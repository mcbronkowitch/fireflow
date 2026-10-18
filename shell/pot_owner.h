#pragma once

// Pot ownership (Rev A P6b-2 spec section 2): every parameter has a stored
// value and the stored value is what sounds. A pot does not own its parameter
// until it is moved past a threshold from a reference reading; the moment it
// is, the parameter jumps to the pot's position and follows it. No pickup.
//
// Pure data logic, no hardware type, host tested (tests/test_pot_owner.cpp).
// The firmware wiring is pot_row_step() (panel_step.h), between pot_filter()
// and the knob vector; panel_scan.cpp calls it once per pot reading.
//
// Spec: ../docs/superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md
#include <cstdint>
#include "scan_value.h"   // kPotHysteresis, for the static_assert below

namespace shell {

// MEASURED on the coupon, 2026-10-18 (docs/hardware/p6b2-led-and-touch-measured.md):
// RV2/RV4/RV6 untouched for 20 minutes with LED_1 switching, worst raw
// max-min per one-second window 60 / 67 / 29 counts. The rule, fixed before
// the measurement, is max(2 x worst spread, 3 x kPotHysteresis) rounded up to
// a multiple of 16 = max(134, 48) -> 144.
inline constexpr int kPotTouch = 144;

// A threshold at or below kPotHysteresis would count as a move a wander that
// pot_filter() still treats as noise -- its band, and the snap regions at the
// stops, are kPotHysteresis counts wide -- and a pot nobody touched could take
// its parameter. Whatever the coupon session measures, it must stay above.
static_assert(kPotTouch > kPotHysteresis,
              "kPotTouch must sit above kPotHysteresis (spec section 2)");

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
