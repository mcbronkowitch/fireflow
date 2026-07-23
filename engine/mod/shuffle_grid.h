#pragma once

#include <cmath>

#include "util/math.h"

namespace spky {

inline float shuffle_amount(float amount) {
    return clampf(amount, 0.f, 1.f);
}

inline float shuffle_step_length(int step, int steps, float amount) {
    if (steps < 1) steps = 1;
    if (step < 0) step = 0;
    if (step >= steps) step = steps - 1;
    const float d = shuffle_amount(amount) / 3.f;
    if ((step & 1) == 0)
        return step + 1 < steps ? 1.f + d : 1.f;
    return 1.f - d;
}

inline float shuffle_boundary_phase(int boundary, int steps, float amount) {
    if (steps < 1) steps = 1;
    if (boundary <= 0) return 0.f;
    if (boundary >= steps) return 1.f;
    float pos = static_cast<float>(boundary);
    if (boundary & 1) pos += shuffle_amount(amount) / 3.f;
    return pos / static_cast<float>(steps);
}

inline int shuffle_step_index(float phase, int steps, float amount) {
    if (steps < 1) steps = 1;
    phase = clampf(phase, 0.f, 1.f);

    // Search the same computed boundaries used by the clock walker. Deriving
    // the index independently from phase * steps is not numerically closed:
    // a boundary rounded by shuffle_boundary_phase() can multiply back to a
    // value just below its source step and be assigned to the previous slot.
    // The strict comparison makes every interval [boundary(i), boundary(i+1)).
    int lo = 0;
    int hi = steps;
    while (lo + 1 < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (phase < shuffle_boundary_phase(mid, steps, amount))
            hi = mid;
        else
            lo = mid;
    }
    return lo;
}

inline float shuffle_step_fraction(float phase, int step, int steps, float amount) {
    const float start = shuffle_boundary_phase(step, steps, amount);
    const float end = shuffle_boundary_phase(step + 1, steps, amount);
    return clampf((phase - start) / (end - start), 0.f, 1.f);
}

inline float shuffle_phase_for_position(float step_position, int steps, float amount) {
    if (steps < 1) steps = 1;
    step_position = std::fmod(step_position, static_cast<float>(steps));
    if (step_position < 0.f) step_position += static_cast<float>(steps);
    int step = static_cast<int>(step_position);
    float frac = step_position - static_cast<float>(step);
    float start = shuffle_boundary_phase(step, steps, amount);
    float end = shuffle_boundary_phase(step + 1, steps, amount);
    return start + frac * (end - start);
}

} // namespace spky
