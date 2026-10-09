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

// PAN's mirror, and the only place in this layer where one deck's face is
// driven by the OTHER deck's modulation (spec 2026-08-30 pan, amended
// 2026-08-30).
//
// Why PAN is the exception: every other paired face is interesting when both
// decks move together. PAN is not -- a common-mode pan is just the whole mix
// walking to one side, and it is what the layer produced before this, reliably
// so: measured 2026-08-30 on two SuperModulators with the real seeds from
// instrument.cpp (0x1234abcd / 0x9e3779b9), FLOW, 48 kHz, 120 s per run, the
// two decks' LANE_SIZE outputs correlate at r = +1.0000 whenever the decks
// share a RATE -- 98.7% of samples on the same side, 0.0% opposite. The
// non-melodic lane is seed-independent, so different seeds buy no spread. At
// the factory RATEs (.112 / .164) it is r = +0.21, still same-side biased.
//
// Note the MASTER passed here is deck A's, not deck B's: the caller hands this
// deck A's lane AND deck A's master, so the mirror is exact in amount and not
// only in direction. The cost is that MOD_B no longer reaches PAN_B -- deck
// B's per-parameter off switch is its own depth ring instead. This is the same
// shape of exception center_term() already is (a deck-column face that does
// not follow one deck's master), and it is the reason the ModTarget row for
// PAN_B still reads part 1: its DEPTH is deck B's, only the source is not.
inline float mirror_term(float master, float laneOut) {
    return -lane_term(master, laneOut);
}

// The center term: mean of both decks' terms, so both masters down means
// the center is still (spec §2).
inline float center_term(float masterA, float laneA,
                         float masterB, float laneB) {
    return 0.5f * (masterA * laneA + masterB * laneB);
}

// The depth knobs are bipolar around noon since spec 2026-08-22
// mod-sh-split: right of noon the lane's continuous output, left of noon its
// S&H twin, noon a standstill. Noon needs a dead zone for the same reason
// GRIT's kGritDead exists (Fireflow.cpp): a 9 mm pot on an ADC cannot hit an
// exact zero, so without a zone "off" would be unreachable on hardware. The
// remainder is rescaled so both stops still reach a full +-1.
//
// 0.04 where kGritDead and kPullDead are both 0.03 -- the spec's figure
// (2026-08-22 mod-sh-split-design §5), untried against 0.03 by ear. See
// docs/by-ear-decisions.md; changing it moves the init pre-images.
constexpr float kDepthDead = 0.04f;

// Knob position -> depth. Inside the zone the answer is exactly 0.f, which
// is what modded()'s identity early return leans on.
inline float depth_of(float raw) {
    const float m = raw < 0.f ? -raw : raw;
    if (m <= kDepthDead) return 0.f;
    const float d = (m - kDepthDead) / (1.f - kDepthDead);
    return raw < 0.f ? -d : d;
}

// The inverse: which knob position yields depth `d`. Exists because the
// engine-backed faces boot with a depth (1.0 / 0.7 / 0.55) and their init
// knob positions have to be the PRE-IMAGES of those, or the dead zone would
// quietly shave every one of them and init would stop sounding like today.
// gen_panel.py computes INIT_DEFAULTS with the same arithmetic; this is the
// C++ side of that one source, and tests/test_mod_layer.cpp pins the round
// trip.
inline float depth_knob(float d) {
    if (d == 0.f) return 0.f;
    const float m   = d < 0.f ? -d : d;
    const float raw = kDepthDead + m * (1.f - kDepthDead);
    return d < 0.f ? -raw : raw;
}

// pushed value = clamp(knob + |depth| * term) in KNOB space, before the
// parameter's own engine mapping, where `term` is the continuous lane term
// right of noon and the S&H one left of it.
//
// A negative depth does NOT invert the modulation -- it selects the other
// reading and scales by the magnitude. This is the same rule Part::_mod_term
// applies engine-side (part.cpp); the two must not drift, or the LEDs (which
// read _mod_term) would report one thing while a host-computed neighbour did
// another.
//
// Depth 0 returns the knob untouched -- bit-exact by early return, which is
// what the identity gate leans on. `depth` here is already through
// depth_of(), so inside the dead zone it is exactly 0.f.
inline float modded(float knob, float depth, float contTerm, float stepTerm,
                    float lo, float hi) {
    if (depth == 0.f) return knob;
    const float term = depth < 0.f ? stepTerm : contTerm;
    const float mag  = depth < 0.f ? -depth : depth;
    return clampf(knob + mag * term, lo, hi);
}

} // namespace spkymod
