#include "fx/drag.h"
#include <cmath>

using namespace spky;

void spky::derive_intervals(const RhythmView& rv, int32_t out[2]) {
    out[0] = out[1] = drag_tuning::kNone;
    if (!rv.valid) return;

    int32_t g0 = rv.gap[0];
    int32_t g1 = rv.gap[1];
    if (g0 < drag_tuning::kMinGap || g1 < drag_tuning::kMinGap) return;

    const float mean = 0.5f * (static_cast<float>(g0) + static_cast<float>(g1));
    const float tol  = drag_tuning::kUniformTol * mean;
    if (std::fabs(static_cast<float>(g0) - mean) <= tol &&
        std::fabs(static_cast<float>(g1) - mean) <= tol) {
        g1 = static_cast<int32_t>(drag_tuning::kUniformSpread * static_cast<float>(g0));
        if (g1 < drag_tuning::kMinGap) return;   // too short to limp audibly
    }

    // No upper bound. A long interval is a slow, dark echo -- which RATE can
    // already ask for -- and bbd_clock_hz clamps the short end on its own. With
    // a single line there is nothing for a clamped value to collide with.
    out[0] = g0;
    out[1] = g1;
}
