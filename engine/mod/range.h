#pragma once
#include "util/math.h"

namespace spky {

// RANGE mapping (spec): monotonic, minimum = off, opening unipolar through the
// first half, widening to full bipolar at maximum.
//   r <= 0      : off (0)
//   r in 0..0.5 : unipolar, amplitude 0..1
//   r in 0.5..1 : blend unipolar -> full bipolar
// `v` is the bipolar lane value [-1,1].
inline float apply_range(float v, float r) {
    r = clampf(r, 0.f, 1.f);
    if (r <= 0.f) return 0.f;
    float uni = v * 0.5f + 0.5f;          // [0,1]
    if (r <= 0.5f) return uni * (r * 2.f); // unipolar 0..amp
    float t = (r - 0.5f) * 2.f;           // 0..1
    return lerpf(uni, v, t);              // unipolar -> bipolar
}

// RANGE on a lane that carries notes (spec 2026-10-26 pitch-range-law, 4.1):
// a width around the PITCH base, nothing else. apply_range above is an
// amplitude law -- right for a modulation lane, where RANGE 0.5 means "swing
// 0..1 upward from the base" -- but on a note lane its unipolar half leans the
// whole melody up by 1 - r and parks it against the top of the span, which a
// quantizer then collapses onto the root. Here the lane keeps its sign and
// scales by r, so the melody stays centred on the base at every RANGE.
//   r <= 0 : off (0), like apply_range
//   else   : v * r
// The two laws agree at both ends of the knob: 0 at r = 0, and v at r = 1 --
// bit for bit on the 2^-23 grid, within one rounding step (6e-8) elsewhere,
// because apply_range's blend is uni + (v - uni) (tests/test_range.cpp
// "both laws agree at full range"). ModLane::_ranged picks between them on the
// engine class (_note_lane()): the PITCH lane of a deck whose engine plays
// notes gets this one; the SAMPLER's and the BBD's PITCH lane (a read position
// and a delay clock, not a note) and every texture lane keep apply_range.
inline float apply_range_centred(float v, float r) {
    r = clampf(r, 0.f, 1.f);
    if (r <= 0.f) return 0.f;
    return v * r;
}

} // namespace spky
