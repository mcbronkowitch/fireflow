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
    phase = clampf(phase, 0.f, 0.99999994f);
    const float pos = phase * static_cast<float>(steps);
    int step = static_cast<int>(pos);
    if ((step & 1) && pos < static_cast<float>(step) + shuffle_amount(amount) / 3.f)
        --step;
    if (step < 0) step = 0;
    if (step >= steps) step = steps - 1;
    return step;
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
