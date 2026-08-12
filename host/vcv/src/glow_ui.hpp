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

// The three standing values a patch carries -- ROOT, SCALE and GENRE -- reach
// this file as a plain int out of jansson, so each gets the same shape of
// validation: a total function, tested by the desktop suite, that turns
// anything a hand-edited patch can hold into something the engine accepts.
// (Glow.cpp keeps only the json_is_integer type check; jansson stays out of
// these signatures so the tests need no Rack and no JSON.)
//
// There is no scale_of_knob() any more. It converted a KNOB POSITION into a
// ScaleId, and the Simple Touch 2 surface has no scale knob: the switch gates
// the menu's value instead of selecting one, and the menu indexes
// kScaleKnobOrder directly. What survived it is the rule below -- out of range
// is a fallback, never index 0 by accident.

// A saved ROOT override -> what Flow::set_root_override wants. -1 is AUTO,
// and so is anything outside 0..11 -- a corrupt patch must not silently
// transpose the instrument to C.
inline int clamp_root_override(int raw) {
    return (raw >= 0 && raw <= 11) ? raw : -1;
}

// A saved SCALE -- the value the SCALE switch gates (spec §4.3) -- into a
// ScaleId. Out of range falls back to the module's own default rather than
// leaving the switch gating a scale that does not exist. That the default
// happens to BE index 0 is a coincidence of the ScaleId order, not the rule:
// the rule is "the boot value", and the fallback is spelled that way.
inline int clamp_menu_scale(int raw) {
    return (raw >= 0 && raw < spky::SCALE_LIST_COUNT) ? raw : spky::SCALE_AEOLIAN;
}

// A saved GENRE constraint -> what Flow::set_genre wants. ARCH_ANY is "draw
// from everything", and so is anything outside 0..ARCH_COUNT-1.
//
// Not a memory hazard: draw_new()'s genre branch filters candidates through
// arch_of() and simply never matches (terrain.cpp). That is exactly why it
// needs clamping -- an unmatchable constraint makes every draw fall out of the
// loop and return the same default TerrainState, which reads as a broken
// generator rather than as a corrupt patch.
inline int clamp_genre(int raw) {
    return (raw >= 0 && raw < spky::flow::ARCH_COUNT) ? raw
                                                      : spky::flow::ARCH_ANY;
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

// Exactly what a patch stores OF THE TERRAIN: current code and undo slot.
// The tonality overrides (spec 2026-08-07 §3) are module settings rather than
// terrain state and are saved by Glow.cpp directly, not through here.
//
// The LOCK is NOT here, and its absence is a decision rather than an omission.
// Since the Simple Touch 2 surface the lock is a pure function of the assigned
// switch's position: Glow.cpp's controlTick pushes
// `swLockPos >= 0 && lock_switch(swLockPos)` into Flow on EVERY tick, before
// it applies anything staged. So a saved lock could only survive one control
// period -- about two milliseconds -- before the switch's answer overwrote it,
// in every configuration, whether or not a switch is assigned to LOCK. Storing
// it would be a second, invisible source of truth for a state that already has
// exactly one control (spec §4.3, "one control, one truth"), and a restore path
// that reads as though it applied. Flow::locked()/set_lock() stay -- they are
// the engine's, and controlTick is their caller.
//
// The two overlays (spec 2026-08-11 §6) are here for the same reason the undo
// CODE is: the twelve places keep their own bases in Place, but the place
// actually PLAYING does not live in that array -- Flow holds it -- and neither
// does the undo slot's. Without these two a reload would restore every pad's
// base and lose the one being heard, which is Task 3's bug one level up.
//
// They are BaseOverlay rather than the encoded string. The terrain is a char[]
// because a code IS a string -- 24 characters, fixed, and the thing a player
// copies. An overlay's string is variable-length and nothing reads it but the
// JSON layer, so the struct stays a struct and Glow.cpp encodes at the edge,
// through the same encode_base/decode_base the places and the clipboard use.
struct GlowSave {
    char code[spky::flow::kTerrainCodeLen + 1] = {};
    char undo[spky::flow::kTerrainCodeLen + 1] = {};
    bool have_undo = false;
    spky::flow::BaseOverlay base;             // the live place's base
    bool have_base = false;
    spky::flow::BaseOverlay undo_base;        // the undo slot's own base
    bool have_undo_base = false;
};

inline GlowSave glow_capture(const spky::flow::Flow& fl) {
    GlowSave s;
    spky::flow::encode_code(fl.state(), s.code, int(sizeof s.code));
    spky::flow::encode_code(fl.undo_state(), s.undo, int(sizeof s.undo));
    s.have_undo = fl.can_undo();
    if (const spky::flow::BaseOverlay* ov = fl.overlay()) {
        s.base = *ov;
        s.have_base = true;
    }
    // The SLOT's overlay is asked for SEPARATELY, exactly as undo_state() is
    // asked for separately from state(), and for the same reason: the two can
    // differ. wake() and begin_blend() do set _undo_overlay from _overlay, and
    // undo() swaps two values that descend from the same wake() -- so across
    // the gesture verbs the pair really is one value. It is restore_undo()
    // that breaks it (flow.cpp:242-246), and glow_restore() below is a caller:
    // it wakes with the live base and then hands the SLOT's own base to
    // restore_undo. A divergent pair is therefore a state a loaded Flow is
    // routinely in, and deriving this field from `base` would collapse it on
    // the next save -- a pair that survives exactly one load.
    if (const spky::flow::BaseOverlay* uov = fl.undo_overlay()) {
        s.undo_base = *uov;
        s.have_undo_base = true;
    }
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
    // Carried through untouched: an overlay has no encoded form to validate at
    // this stage -- Glow.cpp already decoded it, and a string that did not
    // decode arrived here as "no base" rather than as a bad one.
    spky::flow::BaseOverlay base;
    bool have_base = false;
    spky::flow::BaseOverlay undo_base;
    bool have_undo_base = false;
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
    out.base = s.base;
    out.have_base = s.have_base;
    out.undo_base = s.undo_base;
    out.have_undo_base = s.have_undo_base;
    return true;
}

// Applies a saved payload. Returns false and touches NOTHING if the terrain
// code is malformed -- a corrupt patch must not silently move the player to
// some other instrument. The order is the one flow.h documents: wake clears
// the undo slot, so restoring it comes last. The lock is not applied here --
// see the note on GlowSave; controlTick owns it.
//
// Each overlay goes to the verb that owns the state it belongs to, and a place
// with no base passes nullptr rather than an empty overlay: flow.h is explicit
// that nullptr CLEARS, and "a zeroed patch" is a different instrument from
// "the terrain as drawn".
inline bool glow_restore(spky::flow::Flow& fl, const GlowSave& s) {
    GlowRestorePlan plan;
    if (!glow_restore_plan(s, plan)) return false;
    fl.wake(plan.state, plan.have_base ? &plan.base : nullptr);
    fl.restore_undo(plan.undo, plan.have_undo,
                    plan.have_undo_base ? &plan.undo_base : nullptr);
    return true;
}

}  // namespace spkyvcv
