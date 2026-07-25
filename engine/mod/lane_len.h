#pragma once
#include <cmath>
#include "mod/lane_id.h"

namespace spky {

// STEP-mode cycle lengths (spec 2026-07-25 mod-lane-step-grid-lock).
//
// In STEP every lane runs on the deck's step clock, so the old rate ratio in
// super_modulator.cpp's kLaneRatio becomes a length factor f = 1 / ratio on
// the phrase length. MOTION and LEVEL are deliberately rounded from x3/4 and
// x3/2 to x2/3 and x4/3: that turns the two lanes that could never align into
// clean 2- and 3-relations to the phrase, giving the set 4, 6, 8, 12, 16 at
// STEPS = 8 -- congruent again every 48 steps, or six phrases. The polyrhythm
// is preserved, it is just deliberate now.
inline constexpr float kLaneLenFactor[LANE_COUNT] = {
    0.5f,    // LANE_SOURCE  was x2    -> half the phrase
    2.f,     // LANE_SIZE    was x1/2  -> twice the phrase
    1.f,     // LANE_PITCH   x1        -> the phrase itself
    1.5f,    // LANE_MOTION  x3/4 -> x2/3 -> one and a half phrases
    0.75f,   // LANE_LEVEL   x3/2 -> x4/3 -> three quarters of a phrase
};

// A single slot would put the lane's only boundary at phase 0, so it would
// emit a constant value. 64 bounds the other end; the contour buffer is 32
// slots (ModLane::kSeqSlots) and repeats inside longer cycles, which is
// accepted for texture lanes and documented in the spec.
inline constexpr int kLaneSlotsMin = 2;
inline constexpr int kLaneSlotsMax = 64;

// Slot count of one lane in STEP. `tide` is the ladder ratio (kTideRatios), so
// a slower lane (tide < 1) loops over proportionally more steps. The PITCH
// lane is the phrase itself: it returns `steps` unchanged and never sees TIDE
// or the clamps, because changing it would change the phrase length.
inline int lane_slots(int lane, int steps, float tide) {
    if (lane == LANE_PITCH) return steps;
    if (tide <= 0.f) tide = 1.f;
    const float want =
        static_cast<float>(steps) * kLaneLenFactor[lane] / tide;
    int n = static_cast<int>(std::lround(want));
    if (n < kLaneSlotsMin) n = kLaneSlotsMin;
    if (n > kLaneSlotsMax) n = kLaneSlotsMax;
    return n;
}

} // namespace spky
