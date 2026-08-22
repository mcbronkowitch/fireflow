#pragma once
// Rack-free math of the MOD latch layer's host-computed path. Fireflow.cpp
// keeps only the wiring -- the same arrangement as led_law.hpp, and for the
// same reason: spky_tests can drive this, Rack cannot be linked there.
// Spec: docs/superpowers/specs/2026-08-22-mod-latch-layer-design.md §3b.
namespace spkymod {

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// The deck term: master MOD times the assigned lane's output.
inline float lane_term(float master, float laneOut) {
    return master * laneOut;
}

// The center term: mean of both decks' terms, so both masters down means
// the center is still (spec §2).
inline float center_term(float masterA, float laneA,
                         float masterB, float laneB) {
    return 0.5f * (masterA * laneA + masterB * laneB);
}

// pushed value = clamp(knob + depth * term) in KNOB space, before the
// parameter's own engine mapping. Depth 0 returns the knob untouched --
// bit-exact by early return, which is what the identity gate leans on.
inline float modded(float knob, float depth, float laneTerm,
                    float lo, float hi) {
    if (depth <= 0.f) return knob;
    return clampf(knob + depth * laneTerm, lo, hi);
}

} // namespace spkymod
