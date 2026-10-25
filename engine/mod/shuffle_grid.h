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

// shuffle_step_index() for a caller that already holds a likely answer: one
// interval test instead of the search, the search when the guess misses. The
// same result for every input -- the boundaries rise strictly, so the search's
// answer is the one step whose interval [b(i), b(i+1)) (outer edges open)
// holds the clamped phase, and that is exactly what the test checks.
inline int shuffle_step_index_near(float phase, int steps, float amount, int guess) {
    if (steps < 1) steps = 1;
    const float p = clampf(phase, 0.f, 1.f);
    if (guess >= 0 && guess < steps &&
        (guess == 0 || p >= shuffle_boundary_phase(guess, steps, amount)) &&
        (guess + 1 >= steps || p < shuffle_boundary_phase(guess + 1, steps, amount)))
        return guess;
    return shuffle_step_index(phase, steps, amount);
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

// shuffle_phase_for_position() for a position already inside [0, steps): the
// fmod there returns such a position unchanged, so this is the same value
// without the libm call.
inline float shuffle_phase_in_grid(float step_position, int steps, float amount) {
    if (steps < 1) steps = 1;
    const int step = static_cast<int>(step_position);
    const float frac = step_position - static_cast<float>(step);
    const float start = shuffle_boundary_phase(step, steps, amount);
    const float end = shuffle_boundary_phase(step + 1, steps, amount);
    return start + frac * (end - start);
}

} // namespace spky
