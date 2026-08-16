#pragma once
// The panel's LED display law. Rack-free on purpose so spky_tests can drive
// it -- the same arrangement as bbd_edge_state.hpp. Fireflow.cpp keeps only
// the wiring. Spec: docs/superpowers/specs/2026-08-16-led-feedback-design.md
#include <algorithm>
#include <cmath>

namespace spkyled {

// Trough of a breath, as a fraction of the envelope. A FIXED floor would make
// an idle lane and a gently moving one look alike, and would push every
// breath's bottom through the same dark band -- which at this instrument's
// slow rates reads as a loose contact. By-ear candidate, not an invariant.
constexpr float kFloor = 0.28f;

// Perceived lightness goes roughly as the cube root of duty, so perceptual
// linearity needs duty = intensity^gamma with gamma above 1. That puts the
// midpoint duty BELOW the linear midpoint; a linear ramp looks static across
// its top half.
constexpr float kGamma = 2.2f;

// Envelope release, in seconds. Long enough to hold through a breath at
// audible rates, short enough to let go when modulation stops. NOTE the
// limit this implies: for lane cycles much longer than this the envelope
// follows the excursion directly rather than holding its peak, so a very
// slow lane tracks its own position instead of showing a steady depth. It is
// still never dark while it moves, which is what the design needs.
constexpr float kEnvFall = 2.0f;

// One light's state: a peak-tracked envelope of |excursion|, i.e. the
// modulation DEPTH.
struct Lamp {
    float env = 0.f;

    void follow(float excursion, float dt) {
        const float a = std::fabs(excursion);
        if (a >= env) { env = a; return; }           // instant attack
        const float k = dt / (kEnvFall + dt);        // one-pole release
        env += (a - env) * k;
        if (env < 1e-6f) env = 0.f;
    }
};

// The envelope sets the ceiling, the instantaneous excursion breathes inside
// it, and the trough scales with depth. Three readings fall out of one
// expression: dark (nothing modulating), dim breath (shallow), bright breath
// (deep).
inline float intensity(float env, float excursion) {
    if (env <= 0.f) return 0.f;
    const float a   = std::fabs(excursion);
    const float rel = a >= env ? 1.f : a / env;
    return env * (kFloor + (1.f - kFloor) * rel);
}

// Quantised duty, 0 .. steps-1. `steps` is the mux width -- 16 with 16:1
// parts, 8 with 8:1, and that choice is still open, so it is a parameter.
// Every non-zero intensity must reach at least one step: naive quantisation
// of a gamma curve sends the bottom third of every breath to zero, which
// would read as "nothing is modulating" and destroy the whole distinction.
inline int duty(float intens, int steps) {
    if (intens <= 0.f) return 0;
    const float v = std::min(1.f, intens);
    const int   q = static_cast<int>(std::pow(v, kGamma) * (steps - 1) + 0.5f);
    return q < 1 ? 1 : q;
}

} // namespace spkyled
