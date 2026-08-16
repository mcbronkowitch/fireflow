#pragma once
// The panel's LED display law. Rack-free on purpose so spky_tests can drive
// it -- the same arrangement as bbd_edge_state.hpp. Fireflow.cpp keeps only
// the wiring. Spec: docs/superpowers/specs/2026-08-16-led-feedback-design.md
#include <algorithm>
#include <cmath>
#include "instrument.h"
#include "generated_panel.hpp"

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

// Envelope floor: below this a lamp is off, not merely dim. It has to exist and
// it has to be well above a denormal guard, because duty()'s lift holds any
// positive intensity at one step -- so this constant, not the release, is what
// makes "dark" reachable. At 1e-6 a lamp would sit at one step of fifteen for
// 27 s after modulation stopped. 0.02 is 3.9 release constants, about 8 s, and
// stays an order of magnitude below the shallowest breath the lift protects.
// By-ear candidate, not an invariant.
constexpr float kEnvOff = 0.02f;

// One light's state: a peak-tracked envelope of |excursion|, i.e. the
// modulation DEPTH.
struct Lamp {
    float env = 0.f;

    void follow(float excursion, float dt) {
        const float a = std::fabs(excursion);
        if (a >= env) { env = a; return; }           // instant attack
        const float k = dt / (kEnvFall + dt);        // one-pole release
        env += (a - env) * k;
        if (env < kEnvOff) env = 0.f;
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

struct Panel {
    Lamp  lamp[spkyvcv::NUM_LIGHTS];
    float blink = 0.f;                 // free-running, for the phrase lamps
};

// The whole panel in one call, so a gate can assert that every light is
// written -- six lamps sat on this plate for months with no LightId and
// nothing noticed. Writes exactly NUM_LIGHTS entries of duty_out.
inline void fill(const spky::Instrument& inst, Panel& p, float dt,
                 int steps, int* duty_out) {
    using namespace spkyvcv;

    // Written, not skipped. FLOW, TEMPO and SYNC need host state this round
    // does not wire, the two pad lamps need the latch that spec 3.4 leaves to
    // the round that builds MOD and SHIFT, and REC is filled in the next task.
    // A blanket zero at the top of this function would make the gate below --
    // "every light is written" -- pass without asserting anything.
    for (int id : {FLOW_A_L, FLOW_B_L, TEMPO_L, SYNC_L,
                   MODBTN_L, SHIFTBTN_L, REC_A_L, REC_B_L})
        duty_out[id] = 0;

    p.blink += dt;
    if (p.blink >= 1.f) p.blink -= 1.f;

    struct Slot { int id; int lane; };
    static const Slot kExc[8] = {
        {SRC_A_L, spky::LANE_SOURCE}, {SRC_B_L, spky::LANE_SOURCE},
        {FLT_A_L, spky::LANE_SIZE},   {FLT_B_L, spky::LANE_SIZE},
        {CLR_A_L, spky::LANE_MOTION}, {CLR_B_L, spky::LANE_MOTION},
        {LVL_A_L, spky::LANE_LEVEL},  {LVL_B_L, spky::LANE_LEVEL},
    };
    for (int i = 0; i < 8; ++i) {
        const int part = i & 1;
        const float e  = inst.lane_excursion(part, kExc[i].lane);
        p.lamp[kExc[i].id].follow(e, dt);
        duty_out[kExc[i].id] = duty(intensity(p.lamp[kExc[i].id].env, e), steps);
    }

    // Phrase: steady for snapshot A, double-pulse for B. Brightness is the
    // channel the excursion lights use, so this one is carried by shape.
    const int songId[2] = {SONG_A_L, SONG_B_L};
    for (int part = 0; part < 2; ++part) {
        const bool b = inst.active_pattern_for_test(part) != 0;
        const bool on = b ? (p.blink < 0.15f || (p.blink > 0.3f && p.blink < 0.45f))
                          : true;
        duty_out[songId[part]] = on ? steps - 1 : 0;
    }

    // Straight through, no envelope: this reports that a note is sounding, not
    // that one sounded recently. The code it replaces smoothed the gate with a
    // 0.42 ms one-pole, which at the LED update rate is indistinguishable from
    // following it directly.
    const int gateId[2] = {GATE_A_L, GATE_B_L};
    for (int part = 0; part < 2; ++part)
        duty_out[gateId[part]] = inst.gate(part) ? steps - 1 : 0;

    duty_out[CEIL_L] = duty(inst.limiter_squash(), steps);
}

} // namespace spkyled
