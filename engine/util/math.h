#pragma once
#include <cstdint>
#include <cstring>

namespace spky {

static constexpr float TWO_PI = 6.28318530717958647692f;

inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

// "Re-running the setter with this value would compute exactly what it
// computed last time": the guard of the unchanged-value early-outs
// (SuperModulator, ModLane::set_smooth). Bitwise rather than `==` so the
// equivalence is exact by construction: `==` calls -0.f and +0.f equal and
// a NaN unequal to itself, and the recompute is a function of the bits.
inline bool same_bits(float a, float b) {
    uint32_t ua, ub;
    std::memcpy(&ua, &a, sizeof ua);
    std::memcpy(&ub, &b, sizeof ub);
    return ua == ub;
}

} // namespace spky
