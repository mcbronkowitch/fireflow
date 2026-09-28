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

// The panel's span until part 2 gives the control PCB its own tie channels
// (spec section 8): the rail the coupon's 0 ohm ties read through libDaisy's
// path on 2026-09-17, 63485 of 65535. One board's reading, not a property of
// the design.
inline constexpr Span kPanelSpan{0, 63485, true};

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
