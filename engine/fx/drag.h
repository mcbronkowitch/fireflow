#pragma once
#include <cstdint>
#include "mod/rhythm_view.h"

namespace spky {

namespace drag_tuning {

// "No usable interval." 0 is safe as the sentinel because a usable interval is
// always >= kMinGap.
constexpr int32_t kNone = 0;

// Below this a gap is a buzz, not a rhythm -- and 0.75 * g would round toward a
// second interval equal to the first, defeating the uniformity guard.
constexpr int32_t kMinGap = 32;

// Gaps count as uniform when both lie within this fraction of their mean. A
// fraction, not an absolute count: at 240 samples a 2-sample jitter must not
// read as non-uniform, at 30000 a 50-sample drift must not read as uniform.
constexpr float kUniformTol = 0.02f;

// The spread applied when the guard fires: the MOTION lane's x3/4 ratio, a
// polyrhythm the instrument already runs.
constexpr float kUniformSpread = 0.75f;

}  // namespace drag_tuning

// LINK's negative half. These live beside drag_tuning because both halves are
// consumers of the same neighbour rhythm, but the thinning path deliberately
// does NOT go through derive_intervals -- see the link spec's section 2.1.
namespace link_tuning {

// The sparse end of the pattern. Beyond sixteen repeats between audible ones
// the result stops reading as an echo pattern; clamping keeps something
// audible rather than silently muting the control, and at a 1/16 rung sixteen
// repeats is a bar.
constexpr int kMaxSkip = 16;

// Edge on the gate's gain. The only new smoother in this design, and it sits
// on a LEVEL rather than on the clock, so it cannot interact with the 30 ms
// delay-time slew the DRAG half depends on. Without it a gate on a continuous
// signal clicks.
constexpr float kGateRampS = 0.003f;

}  // namespace link_tuning

// Turn the other deck's published rhythm into two repeat intervals, in samples.
// Pure: no state, no sample rate, no bounds.
//
// This is the tape-era `derive_offsets` (engine/fx/taps.cpp on `main`, deleted
// at e004a3d) with its two hard-won rules intact and its output re-read as
// DURATIONS rather than positions behind a write head.
//
// The uniformity guard earns its keep for a sharper reason here than it did
// there. On tape the argument was "evenly spaced taps ARE a delay". Here RATE
// is already tempo-synced to divisions, so an echo locked to an even neighbour
// rhythm is a sound the instrument already makes -- the limp is what cannot be
// had any other way, and this guard is what guarantees one.
//
// out[i] == drag_tuning::kNone means "no usable rhythm"; both entries are set
// together, never one of them.
void derive_intervals(const RhythmView& rv, int32_t out[2]);

}  // namespace spky
