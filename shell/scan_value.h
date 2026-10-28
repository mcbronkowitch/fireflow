#pragma once

// The value path from a raw mux word to a normalised control value: the
// span, a clamp, a hysteresis band and a snap at both stops. No hardware
// type, host tested (tests/test_scan_value.cpp) -- the same arrangement as
// mux_plan.h and controls.h.
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
// section 5.
#include <cstdint>
#include "coupon_expect.h"

namespace shell {

// The hysteresis band, in raw counts: the widest max - min any of the
// coupon's seven pots showed in the scan-check image's arm S, across every
// complete block of docs/hardware/captures/scan-check-capture-c04ba77.txt,
// rounded up to a multiple of 16 and at least 16 (spec section 4).
// shell/test_read_scan_check.py recomputes it from the capture and fails if
// the two differ.
inline constexpr int kPotHysteresis = 16;

// Rev A's band, per sense pin (fault R3, docs/hardware/reva-bring-up.md rows
// 5.3/5.5): the coupon's 16 is too narrow on the real panel. The same rule
// as above, the worst untouched one-second raw spread rounded up to a
// multiple of 16: SENSE_0 57 counts at mid travel (2026-10-24) -> 64,
// SENSE_1-3 23 (2026-10-28) -> 32. SENSE_0's excess grows with the wiper's
// source impedance; a C_SENSE0 fitted later would let its band come down.
inline constexpr int kRevaHysteresis[4] = {64, 32, 32, 32};

// The band for a row on sense pin `sense` (ControlEntry::sense):
// kRevaHysteresis on Rev A's four pins, kPotHysteresis for a row that records
// no pin (-1, the coupon's tables).
constexpr int pot_hysteresis(int sense)
{
    return sense >= 0 && sense < 4 ? kRevaHysteresis[sense] : kPotHysteresis;
}

// The span Rev A measures for itself once per sweep from its two calibration
// channels, CAL_GND and CAL_3V3 (spec 2026-10-02 section 3.5): coupon_span()'s
// rail floor and zero ceiling. There is no tie spread to check -- one channel
// per rail -- and the two bounds already order the pair.
Span panel_span(uint16_t zero, uint16_t rail);

// (raw - zero) / (rail - zero), clamped to 0..1. The clamp belongs here, on
// the reading side: only the reader knows the span. An invalid or inverted
// span gives 0 rather than a division by zero or a negative scale.
float span_normalize(uint16_t raw, const Span& span);

struct PotFilter
{
    uint16_t last_raw = 0;      // the raw word of the last EMITTED value
    float    last_v   = 0.0f;
    bool     emitted  = false;
};

// Whether this reading should reach the engine. Emits (true, *out = v) when
//   - the span is valid, and
//   - the channel has never emitted, or the raw word has moved more than
//     `hysteresis` counts from the last EMITTED raw word, or the reading lies
//     in a snap region whose value (0 or 1) differs from the last emitted
//     one -- so a stop is reachable from inside the band.
// Snap regions: raw <= zero + hysteresis is 0, raw >= rail - hysteresis is 1.
bool pot_filter(PotFilter& f, uint16_t raw, const Span& span, int hysteresis,
                float* out);

} // namespace shell
