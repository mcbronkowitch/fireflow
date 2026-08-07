// host/vcv/src/glow_ui.hpp
//
// FireFlow Glow's module logic that needs no Rack type: CV scaling, knob
// travel, LED signatures and the persistence payload. Kept out of Glow.cpp
// so the desktop doctest suite can test it headlessly -- the same split
// form_song_migration.hpp and bbd_edge_state.hpp already use.
//
// No <rack.hpp>, no jansson, no widgets. Glow.cpp is the only file that
// knows what a Module is.
#pragma once
#include <cmath>
#include "flow/flow.h"
#include "flow/flow_ids.h"
#include "flow/gesture.h"
#include "flow/taste.h"
#include "flow/terrain_code.h"
#include "pitch/quantizer.h"

namespace spkyvcv {

// Panel jack order -> macro (spec 6: five CV jacks, WANDER has none).
inline constexpr int kCvMacro[5] = {
    spky::flow::M_MOTION, spky::flow::M_DENSITY, spky::flow::M_BRIGHT,
    spky::flow::M_DIRT,   spky::flow::M_SPACE
};

// Knob position -> ScaleId for Glow's SCALE switch (spec 2026-08-07 §3.1).
// ScaleId is ordered by provenance (modes, pentatonics, exotic); the knob is
// ordered by FRICTION, so the travel runs calm -> sharp: the two scales with
// neither a minor second nor a tritone first, then the seven-note modes (which
// all contain both, a property of seven notes in twelve), then the
// hirajoshi/pygmy/kumoi bucket, then the exotics.
//
// That ordering is not re-derived by feel -- it is kScaleW (taste.h) read
// descending, and test_glow_ui.cpp pins the two together so a retune of the
// weights cannot leave this table quietly stale.
inline constexpr int kScaleKnobOrder[spky::SCALE_LIST_COUNT] = {
    spky::SCALE_MIN_PENT, spky::SCALE_MAJ_PENT,                   // 0.1750
    spky::SCALE_AEOLIAN,  spky::SCALE_DORIAN,
    spky::SCALE_MIXO,     spky::SCALE_LYDIAN,                     // 0.1125
    spky::SCALE_HIRAJOSHI, spky::SCALE_PYGMY, spky::SCALE_KUMOI,  // 0.0667
    spky::SCALE_PHRYGIAN, spky::SCALE_HIJAZ,
    spky::SCALE_HARM_MIN, spky::SCALE_WHOLE,                      // 0.0250
};

// Switch position -> what Flow::set_scale_override wants. Position 0 is AUTO,
// and so is anything out of range: a corrupt patch must not retune the
// instrument to whatever scale happens to sit at index 0.
inline int scale_of_knob(int pos) {
    if (pos < 1 || pos > spky::SCALE_LIST_COUNT) return -1;
    return kScaleKnobOrder[pos - 1];
}

// Unipolar Eurorack convention: 0..10 V spans the macro's whole travel.
// Deliberately NOT clamped -- Flow::set_cv clamps the knob+CV+weather sum,
// and clamping here as well would just hide how hot an input is running.
inline float cv_to_macro(float volts) { return volts * 0.1f; }

// Physical knob travel between control ticks, for the NEW gesture decoder's
// "hold and turn a knob to mark it" (spec 5). Absolute travel: which way the
// player turned is not part of the gesture.
struct KnobTracker {
    float last[spky::flow::MACRO_COUNT] = {};
    bool  primed = false;

    void prime(const float* v) {
        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m) last[m] = v[m];
        primed = true;
    }

    // Writes |travel| per macro into out[]; returns true if anything moved.
    // The first look at an unprimed tracker reports nothing: a freshly added
    // module must not hand the decoder six phantom deltas.
    bool deltas(const float* v, float* out) {
        bool any = false;
        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m) {
            const float d = primed ? std::fabs(v[m] - last[m]) : 0.f;
            out[m] = d;
            if (d > 0.f) any = true;
            last[m] = v[m];
        }
        primed = true;
        return any;
    }
};

// Press/release edges for the NEW button. flow::Gesture wants button(down)
// exactly once per transition: telling it "down" on every control tick would
// restart the hold timer forever, and undo and lock could never fire.
struct GestureBridge {
    bool prevDown = false;
    bool edge(bool down) {
        const bool changed = down != prevDown;
        prevDown = down;
        return changed;
    }
};

// The module's own refusal flash. gesture.h's own LED_REFUSE window
// (_refuse_t) is only reachable from a real button(down=false, ...) release
// -- see the guard at the top of that function -- so it can never be set
// from the corrective path Glow.cpp takes when Flow declines an op the
// decoder already let through (nothing to undo, an empty macro mask). Only
// the module knows that happened, so the module owns this flash too.
struct RefuseFlash {
    // Kept far enough in the past that a fresh instance does NOT read as
    // "just refused" -- same reasoning gesture.h gives at _refuse_t's
    // initialiser: _now and this timestamp must never start close enough
    // together for active() to read true before mark() is ever called.
    double at = -1e18;
    void mark(double now_s) { at = now_s; }
    bool active(double now_s) const {
        return now_s - at < spky::flow::kRefuseFlashS;
    }
};

// The LED signatures of spec 5's gesture table, as brightness in 0..1.
// A pure function of (state, blend phase, flow clock) so it can be tested
// without a running module.
inline float led_level(int led, float blend_phase, double t) {
    using G = spky::flow::Gesture;
    const double kTwoPi = 6.283185307179586;
    switch (led) {
        case G::LED_LOCKED:
            return 1.f;                                   // solid while locked
        case G::LED_REFUSE:                               // fast hard blink
            return std::fmod(t, 0.1) < 0.05 ? 1.f : 0.f;
        case G::LED_MARKED:                               // faster, dimmer flicker
            return std::fmod(t, 0.05) < 0.025 ? 0.85f : 0.15f;
        case G::LED_UNDO_ARMED: {                         // two short pulses, then rest
            const double p = std::fmod(t, 1.0);
            return (p < 0.09 || (p >= 0.18 && p < 0.27)) ? 1.f : 0.05f;
        }
        case G::LED_BLEND: {                              // breathes through the blend
            const float depth = 1.f - blend_phase;        // widest at press, closing
            const float breath =
                0.5f - 0.5f * float(std::cos(kTwoPi * 0.8 * t));
            return 0.12f + 0.88f * breath * (0.35f + 0.65f * depth);
        }
        default:
            return 0.06f;                                 // idle: a dim ember
    }
}

// Spec 4's clock-override rule: an external clock overrides the terrain's
// own tempo while pulses keep arriving; the terrain's tempo returns once the
// clock has been silent for `timeoutS` (falls back "after about two
// seconds", per host/vcv/README.md). `fallback` is whatever the terrain
// itself is pushing this tick (Flow::param_now(P_TEMPO_BPM)); `clkPeriod` is
// samples between the last two edges (0 = never seen one); `clkSamples` is
// samples since the last edge. A measured tempo outside 20..400 BPM is
// treated as a mis-read, not a real tempo, and falls back too.
inline float clock_bpm(float fallback, float clkPeriod, float clkSamples,
                        float sr, float timeoutS) {
    if (clkPeriod > 1.f && sr > 0.f && clkSamples < sr * timeoutS) {
        const float measured = 60.f * sr / clkPeriod;
        if (measured >= 20.f && measured <= 400.f) return measured;
    }
    return fallback;
}

// Exactly what a patch stores (spec 5: current terrain code, lock, undo slot).
struct GlowSave {
    char code[spky::flow::kTerrainCodeLen + 1] = {};
    char undo[spky::flow::kTerrainCodeLen + 1] = {};
    bool lock = false;
    bool have_undo = false;
};

inline GlowSave glow_capture(const spky::flow::Flow& fl) {
    GlowSave s;
    spky::flow::encode_code(fl.state(), s.code, int(sizeof s.code));
    spky::flow::encode_code(fl.undo_state(), s.undo, int(sizeof s.undo));
    s.lock = fl.locked();
    s.have_undo = fl.can_undo();
    return s;
}

// The decoded, ready-to-apply half of a saved payload -- everything
// glow_restore() needs to hand to a live Flow, minus the actual Flow calls.
// Split out (fix round 3) so a caller that must NOT write to Flow directly
// -- Glow.cpp's UI-thread menu/JSON handlers, which stage a payload for the
// audio thread instead -- can still get the same validated, decoded POD.
struct GlowRestorePlan {
    spky::flow::TerrainState state;
    spky::flow::TerrainState undo;
    bool have_undo = false;
    bool lock = false;
};

// Decodes and validates a saved payload into `out`. Returns false and
// touches `out` not at all if the terrain code is malformed -- same
// contract as glow_restore() below, which is now built on top of this.
inline bool glow_restore_plan(const GlowSave& s, GlowRestorePlan& out) {
    spky::flow::TerrainState st;
    if (!spky::flow::decode_code(s.code, st)) return false;
    spky::flow::TerrainState un = st;
    const bool have = s.have_undo && spky::flow::decode_code(s.undo, un);
    out.state = st;
    out.undo = un;
    out.have_undo = have;
    out.lock = s.lock;
    return true;
}

// Applies a saved payload. Returns false and touches NOTHING if the terrain
// code is malformed -- a corrupt patch must not silently move the player to
// some other instrument. The order is the one flow.h documents: wake clears
// the undo slot, so restoring it comes last.
inline bool glow_restore(spky::flow::Flow& fl, const GlowSave& s) {
    GlowRestorePlan plan;
    if (!glow_restore_plan(s, plan)) return false;
    fl.wake(plan.state);
    fl.set_lock(plan.lock);
    fl.restore_undo(plan.undo, plan.have_undo);
    return true;
}

}  // namespace spkyvcv
