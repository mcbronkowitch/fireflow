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
#include "flow/terrain_code.h"

namespace spkyvcv {

// Panel jack order -> macro (spec 6: five CV jacks, WANDER has none).
inline constexpr int kCvMacro[5] = {
    spky::flow::M_MOTION, spky::flow::M_DENSITY, spky::flow::M_BRIGHT,
    spky::flow::M_DIRT,   spky::flow::M_SPACE
};

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

// Applies a saved payload. Returns false and touches NOTHING if the terrain
// code is malformed -- a corrupt patch must not silently move the player to
// some other instrument. The order is the one flow.h documents: wake clears
// the undo slot, so restoring it comes last.
inline bool glow_restore(spky::flow::Flow& fl, const GlowSave& s) {
    spky::flow::TerrainState st;
    if (!spky::flow::decode_code(s.code, st)) return false;
    spky::flow::TerrainState un = st;
    const bool have = s.have_undo && spky::flow::decode_code(s.undo, un);
    fl.wake(st);
    fl.set_lock(s.lock);
    fl.restore_undo(un, have);
    return true;
}

}  // namespace spkyvcv
