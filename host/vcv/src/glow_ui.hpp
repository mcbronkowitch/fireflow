// host/vcv/src/glow_ui.hpp
//
// FireFlow Glow's tonality tables, the refusal flash and the persistence
// payload -- the module logic that needs no Rack type. Kept out of Glow.cpp
// so the desktop doctest suite can test it headlessly -- the same split
// form_song_migration.hpp, touch_pads.hpp and bbd_edge_state.hpp already use.
//
// The Simple Touch 2 surface (2026-08-11) removed this file's other half: the
// CV jacks, the external clock and the NEW button's LED are not on the board,
// so kCvMacro, cv_to_macro, clock_bpm and led_level went with them. KnobTracker
// and GestureBridge went too -- the hold-and-turn gesture became a menu item
// with nothing to track, and the rising-edge rule now lives once, as
// PadGesture::prime in touch_pads.hpp, where it is tested.
//
// No <rack.hpp>, no jansson, no widgets. Glow.cpp is the only file that
// knows what a Module is.
#pragma once
#include "flow/flow.h"
#include "flow/flow_ids.h"
#include "flow/taste.h"
#include "flow/terrain_code.h"
#include "pitch/quantizer.h"

namespace spkyvcv {

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

// A saved ROOT override -> what Flow::set_root_override wants. -1 is AUTO,
// and so is anything outside 0..11: the same rule scale_of_knob applies, for
// the same reason -- a corrupt patch must not silently transpose the
// instrument to C. Lives here rather than in Glow.cpp so the desktop suite can
// test it; Glow.cpp passes a plain int, keeping jansson out of the signature.
inline int clamp_root_override(int raw) {
    return (raw >= 0 && raw <= 11) ? raw : -1;
}

// The module's own refusal flash. Flow's verbs return bool, and a refusal is
// the only thing the player can see when Flow declines: a locked generator, an
// empty undo slot, a pad whose place does not decode. Nothing else knows that
// happened, so the module owns this flash -- Glow.cpp paints it onto the live
// pad's collar.
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

// Exactly what a patch stores OF THE TERRAIN: current code, lock, undo slot.
// The tonality overrides (spec 2026-08-07 §3) are module settings rather than
// terrain state and are saved by Glow.cpp directly, not through here.
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
