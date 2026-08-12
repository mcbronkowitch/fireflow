// host/vcv/src/flow_patch_bridge.hpp
//
// The Fireflow -> flow converter (spec 2026-08-11 §5), and the report it owes.
//
// THE AUTHORITY FOR EVERY MAPPING IN THIS FILE IS
// docs/flow-fireflow-param-map.md -- one row per base-rule parameter, each
// either mapped with its conversion or marked UNREACHABLE with a reason. That
// table was written before this code and verified row by row against
// Fireflow.cpp. If a mapping is not in it, this file does not perform it; if
// this file and the table ever disagree, the table wins and this file is the
// bug.
//
// Two rules the table exists to enforce, both of which have already cost this
// project its factory sound once (see the fireflow-control-merge-init-trap
// memory):
//
//   1. The knob's name is not the parameter's name. P_DEPTH_* comes from the
//      knob printed MOD, P_COMP_B from the knob printed LVL, P_FORM_B from the
//      knob printed SONG.
//   2. STORE WHAT FIREFLOW PUSHED TO THE ENGINE, NOT WHAT THE KNOB READ. For
//      ENGINE, CHOKE, COUPLE, TEMPO and COMP (LVL), Fireflow.cpp transforms the
//      knob before handing it to the engine, while flow's apply_param() hands
//      the overlay value to the SAME setter raw. Copying the knob for one of
//      those is exactly the shape of the four conversion changes that silently
//      moved the factory sound.
//
// THE REPORT IS THE DELIVERABLE. A converter that carries what it can and says
// nothing about the rest looks right and loses a third of the tonality without
// a word. Everything that cannot be carried -- and everything that is carried
// but will be rewritten at runtime -- gets a note.
//
// No <rack.hpp>, no jansson, no widgets, and the input is a plain float array
// rather than a Module: the same split touch_pads.hpp and glow_ui.hpp already
// use, so the desktop doctest suite can exercise this headlessly.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "flow/flow_params.h"
#include "flow/taste.h"
#include "flow/terrain.h"
#include "generated_panel.hpp"
#include "mod/song_ladder.h"
#include "parts/engine_iface.h"

namespace spkyvcv {

// ---------------------------------------------------------------------------
// The index names
// ---------------------------------------------------------------------------
//
// ALIASES OF generated_panel.hpp's `enum ParamId`, not a transcription of it.
// The generated header is the authority on these numbers, so this file asks it
// rather than restating it: a name bound to `ENGINE_A` cannot drift from
// `ENGINE_A` the way a hand-copied `17` can, and there is nothing left for a
// static_assert to check.
//
// Including it costs nothing this file was protecting. `generated_panel.hpp`
// has no #include of its own and names no Rack type -- it is plain enums, PODs
// and constexpr tables -- and `tests/test_seed_audition_init.cpp` already
// includes it in this same headless suite. The only real cost is that its
// enumerators land in `spkyvcv`, which is a cost `Fireflow.cpp` and that test
// already pay.
//
// The kFf* names still exist rather than the raw enumerators being used
// directly, for one reason: at a call site, `ENGINE_A` and `MOD_A` read as
// engine ids and modulation, which is exactly the confusion the map warns
// about. `kFfEngineA` and `kFfModA` read as panel indices.
inline constexpr int kFfPartStride = PART_STRIDE;

inline constexpr int kFfRateA    = RATE_A;
inline constexpr int kFfShapeA   = SHAPE_A;
inline constexpr int kFfDensityA = DENSITY_A;
inline constexpr int kFfSmoothA  = SMOOTH_A;
inline constexpr int kFfRangeA   = RANGE_A;
inline constexpr int kFfMelodyA  = MELODY_A;
inline constexpr int kFfModA     = MOD_A;      // prints "MOD", drives P_DEPTH_A
inline constexpr int kFfTuneA    = TUNE_A;
inline constexpr int kFfAttackA  = ATTACK_A;
inline constexpr int kFfDecayA   = DECAY_A;
inline constexpr int kFfResA     = RES_A;
inline constexpr int kFfSubA     = SUB_A;
inline constexpr int kFfSourceA  = SOURCE_A;
inline constexpr int kFfFluxA    = FLUX_A;
inline constexpr int kFfGritA    = GRIT_A;
inline constexpr int kFfCompA    = COMP_A;     // prints "LVL", drives P_COMP_A
inline constexpr int kFfStepsA   = STEPS_A;
inline constexpr int kFfEngineA  = ENGINE_A;
inline constexpr int kFfDetuneA  = DETUNE_A;
inline constexpr int kFfSongA    = SONG_A;

inline constexpr int kFfRateB    = RATE_B;
inline constexpr int kFfShapeB   = SHAPE_B;
inline constexpr int kFfDensityB = DENSITY_B;
inline constexpr int kFfSmoothB  = SMOOTH_B;
inline constexpr int kFfRangeB   = RANGE_B;
inline constexpr int kFfMelodyB  = MELODY_B;
inline constexpr int kFfModB     = MOD_B;
inline constexpr int kFfTuneB    = TUNE_B;
inline constexpr int kFfAttackB  = ATTACK_B;
inline constexpr int kFfDecayB   = DECAY_B;
inline constexpr int kFfResB     = RES_B;
inline constexpr int kFfSubB     = SUB_B;
inline constexpr int kFfSourceB  = SOURCE_B;
inline constexpr int kFfFluxB    = FLUX_B;
inline constexpr int kFfGritB    = GRIT_B;
inline constexpr int kFfCompB    = COMP_B;     // prints "LVL", drives P_COMP_B
inline constexpr int kFfStepsB   = STEPS_B;
inline constexpr int kFfEngineB  = ENGINE_B;
inline constexpr int kFfDetuneB  = DETUNE_B;
inline constexpr int kFfSongB    = SONG_B;     // prints "SONG", drives FORM *and* SONG

// Appended globals -- NOT part-strided. Indexing these by kFfPartStride is the
// bug Fireflow.cpp guards against with an explicit ternary at every one of
// their push sites; this file does the same.
inline constexpr int kFfMorph     = MORPH;
inline constexpr int kFfTempo     = TEMPO;
inline constexpr int kFfCouple    = COUPLE;    // prints "FREE|GRID"; also the sync zone
inline constexpr int kFfScale     = SCALE;
inline constexpr int kFfDrift     = DRIFT;
inline constexpr int kFfRevSize   = REV_SIZE;
inline constexpr int kFfRevDecay  = REV_DECAY;
inline constexpr int kFfRevTone   = REV_TONE;
inline constexpr int kFfRevDiff   = REV_DIFF;
inline constexpr int kFfChoke     = CHOKE;
inline constexpr int kFfFiltA     = FILT_A;
inline constexpr int kFfFiltB     = FILT_B;
inline constexpr int kFfTide      = TIDE;
inline constexpr int kFfFluxRateA = FLUXRATE_A;
inline constexpr int kFfFluxRateB = FLUXRATE_B;
inline constexpr int kFfFluxFbA   = FLUXFB_A;
inline constexpr int kFfFluxFbB   = FLUXFB_B;
inline constexpr int kFfColorA    = COLOR_A;
inline constexpr int kFfColorB    = COLOR_B;
inline constexpr int kFfLinkA     = LINK_A;
inline constexpr int kFfLinkB     = LINK_B;
inline constexpr int kFfStagesA   = STAGES_A;
inline constexpr int kFfStagesB   = STAGES_B;
inline constexpr int kFfRecA      = REC_A;
inline constexpr int kFfRecB      = REC_B;
inline constexpr int kFfRevMixA   = REV_MIX_A;
inline constexpr int kFfRevMixB   = REV_MIX_B;
inline constexpr int kFfShuffle   = SHUFFLE;
inline constexpr int kFfPace      = PACE;

inline constexpr int kFireflowParamCount = NUM_PARAMS;

// ---------------------------------------------------------------------------
// The patch, the report
// ---------------------------------------------------------------------------

// A Fireflow patch as this converter needs to see it: the raw param array, plus
// the one piece of Fireflow state that is NOT a param and still changes what
// the engine was. testTone re-points a Sampler deck to ENGINE_TEST_TONE
// (Fireflow.cpp:655) -- no knob position says it, and it does not transfer.
struct FireflowPatch {
    float p[kFireflowParamCount] = {};
    bool  test_tone[2] = {};
};

// param == kNoteGeneral for a note that no single flow parameter owns.
inline constexpr int kNoteGeneral = -1;

struct TransferNote { int param; const char* reason; };

inline constexpr int kMaxNotes = spky::flow::P_COUNT + 8;

struct TransferReport {
    spky::flow::BaseOverlay overlay;
    TransferNote notes[kMaxNotes] = {};
    int  note_count = 0;
    bool overlay_rejected = false;
};

namespace detail {

// Collects notes without ever truncating in silence: if the array fills, the
// LAST slot is spent saying so rather than on the last note that happened to
// fit. Unreachable at the current note count -- kMaxNotes is P_COUNT + 8 and
// the converter emits at most a couple of dozen -- which is exactly why it must
// not be an assert: a future row that pushes it over must degrade honestly.
struct NoteSink {
    TransferReport& r;
    int dropped = 0;
    explicit NoteSink(TransferReport& rep) : r(rep) {}
    void note(int param, const char* reason) {
        if (r.note_count < kMaxNotes) {
            r.notes[r.note_count].param  = param;
            r.notes[r.note_count].reason = reason;
            ++r.note_count;
        } else {
            ++dropped;
        }
    }
    void finish() {
        if (dropped > 0)
            r.notes[kMaxNotes - 1] = { kNoteGeneral,
                "REPORT FULL: further notes were dropped -- this transfer lost "
                "more than is listed above" };
    }
};

// Fireflow's own per-part read: part A's id plus the stride. Only legal for the
// controls that ARE strided -- see the appended block above.
inline float pp(const FireflowPatch& fp, int id_a, int part) {
    return fp.p[id_a + part * kFfPartStride];
}

// Write one overlay entry. The is_base_rule() guard is deliberate: it asks
// taste.h rather than trusting this file's transcription of which rows are base
// rules, so a row that leaves kBaseRules stops being written here on the same
// day it stops being honoured by generate(). The clamp is the same one
// generate() applies on the way in (terrain.cpp:494) -- doing it here as well
// is what lets the report say a value WAS clamped instead of leaving the caller
// to discover it in the terrain.
inline void set_base(TransferReport& r, int p, float v) {
    if (!spky::flow::is_base_rule(p)) return;
    r.overlay.v[p]   = spky::flow::clamp_to(spky::flow::kParams[p], v);
    r.overlay.has[p] = true;
}

// Fireflow.cpp:649-656, verbatim in structure: the knob is a UI numbering that
// drops ENGINE_TEST_TONE and renumbers the rest, and anything that is not
// 0/2/3/4 falls through to the Sampler. The test tone is a Sampler-only
// override driven by module state, not by this knob, and it never transfers --
// taste.h lists it in neither kCarrierEngine nor kTextureEngine, so the
// generator may not roll it and the converter may not write it.
inline int engine_from_knob(float knob) {
    const int k = int(std::lround(knob));
    switch (k) {
    case 0:  return spky::ENGINE_SYNTH;
    case 2:  return spky::ENGINE_WAVE;
    case 3:  return spky::ENGINE_BODY;
    case 4:  return spky::ENGINE_BBD;
    default: return spky::ENGINE_SAMPLER;
    }
}

// Fireflow.cpp:633-641. The LVL knob is a split control: below the split it is
// pure output gain (set_part_level), above it the compressor amount on a power
// curve. flow has no slot for the gain half at all, so only this half travels.
inline float comp_from_lvl(float lvl) {
    static constexpr float kLvlCompSplit = 0.6f;
    static constexpr float kCompTop      = 0.7f;
    static constexpr float kCompShape    = 0.6f;
    if (lvl <= kLvlCompSplit) return 0.f;
    return kCompTop * std::pow((lvl - kLvlCompSplit) / (1.f - kLvlCompSplit),
                               kCompShape);
}

// Fireflow.cpp:877-879. GRIT is one bipolar knob: the sign picks the mode
// (Reduce below zero, Drive above -- set_grit_mode, not carried, see the map),
// the magnitude past a dead zone is the mix flow actually has a slot for.
// kGritDead matches Fireflow.cpp's own constant (:592) exactly -- it exists so
// a 9 mm pot that cannot hit an exact zero still reaches "off".
inline float grit_from_knob(float knob) {
    static constexpr float kGritDead = 0.03f;
    const float mag = std::fabs(knob);
    if (mag <= kGritDead) return 0.f;
    return (mag - kGritDead) / (1.f - kGritDead);
}

// taste.h's kVetos, read rather than transcribed -- the COMP band is a by-ear
// ruling (spotykach-by-ear-decisions) and has already been retuned twice.
inline bool veto_band(int param, float& lo, float& hi) {
    for (int i = 0; i < spky::flow::kVetoCount; ++i)
        if (spky::flow::kVetos[i].param == param) {
            lo = spky::flow::kVetos[i].lo;
            hi = spky::flow::kVetos[i].hi;
            return true;
        }
    return false;
}

} // namespace detail

// ---------------------------------------------------------------------------
// The conversion
// ---------------------------------------------------------------------------

inline TransferReport to_flow_base(const FireflowPatch& fp) {
    using namespace spky::flow;
    TransferReport r;
    detail::NoteSink sink(r);

    // --- The engine pair, first, because it can reject the whole transfer ---
    const int eng[2] = { detail::engine_from_knob(fp.p[kFfEngineA]),
                         detail::engine_from_knob(fp.p[kFfEngineB]) };

    if (!is_carrier_engine(eng[0]) && !is_carrier_engine(eng[1])) {
        // The "loud pair" (map, "Overlay-level effects", 1). generate() drops
        // such an overlay WHOLE (terrain.cpp:486-497) because no carrier means
        // no role structure to hang the rest on. Refusing here too keeps one
        // rule in two places; two places that disagreed about half-application
        // would be worse than one that refuses.
        r.overlay_rejected = true;
        sink.note(kNoteGeneral,
            "REJECTED WHOLE: neither deck holds a carrier engine (SYNTH, WAVE "
            "or BODY). generate() drops such an overlay entirely, so nothing "
            "at all was carried -- not even the parameters that would have "
            "converted cleanly");
        sink.note(P_ENGINE_A,
            "part of the rejected pair (SAMPLER/BBD on both decks)");
        sink.note(P_ENGINE_B,
            "part of the rejected pair (SAMPLER/BBD on both decks)");
        sink.finish();
        return r;
    }

    // COUPLE's zone split, read early because it decides more than P_COUPLE:
    // it is the only thing in Fireflow that drives set_sync, so it is also
    // P_MODE, and P_MODE is what tells the per-deck RANGE note below whether
    // the BBD cap can apply at all. Both writes happen further down, in the
    // Global block, where the map's rows for them are.
    static constexpr float kCoupleZoneSplit = 0.5f;
    const float coupleKnob = fp.p[kFfCouple];
    const bool  grid = coupleKnob >= kCoupleZoneSplit;   // true -> STEP/synced

    detail::set_base(r, P_ENGINE_A, float(eng[0]));
    detail::set_base(r, P_ENGINE_B, float(eng[1]));
    for (int p = 0; p < 2; ++p) {
        if (fp.test_tone[p] && eng[p] == spky::ENGINE_SAMPLER)
            sink.note(p ? P_ENGINE_B : P_ENGINE_A,
                "the deck was running the TEST TONE, which is module state "
                "rather than a knob position and has no flow representation "
                "(taste.h rolls it in neither role table). Carried as SAMPLER");
    }

    // --- Discrete world picks ---
    detail::set_base(r, P_SCALE, fp.p[kFfScale]);

    // P_ROOT: UNREACHABLE. Fireflow has no ROOT control at all -- set_root
    // appears zero times in Fireflow.cpp -- so the terrain's own stage-2 root
    // draw stands (terrain.cpp:435). Reported EVERY time, unconditionally: a
    // converter that quietly left ROOT unset looks correct and hands the patch
    // to a key it never had.
    sink.note(P_ROOT,
        "UNREACHABLE: Fireflow has no ROOT control, so the terrain draws its "
        "own root. The transferred patch keeps its scale but not its key");

    // SONG_B is one knob for two flow parameters: it is a rung on the curated
    // 14-rung ladder through the (Principle, SongMode) grid, and FORM was
    // deleted as a control in the 2026-08-09 reduction. Nothing is lost in the
    // transfer -- a Fireflow patch could only ever hold a ladder pair anyway.
    {
        const int rung = int(std::lround(fp.p[kFfSongB]));
        const spky::SongRung& sr = spky::song_ladder_at(rung);
        detail::set_base(r, P_FORM_B, float(sr.form));
        detail::set_base(r, P_SONG_B, float(sr.song));
    }

    const int steps[2] = { int(std::lround(fp.p[kFfStepsA])),
                           int(std::lround(fp.p[kFfStepsB])) };
    if (steps[1] >= 2) {
        detail::set_base(r, P_STEPS_B, float(steps[1]));
    } else if (steps[1] == 1) {
        detail::set_base(r, P_STEPS_B, kParams[P_STEPS_B].lo);
        sink.note(P_STEPS_B,
            "CLAMPED: Fireflow allows a 1-step deck, flow's step range starts "
            "at 2. Carried as 2");
    } else {
        sink.note(P_STEPS_B,
            "NOT CARRIED: STEPS 0 is how a Fireflow deck says STEP is off. In "
            "flow that state is P_MODE, not a step count, so the deck's step "
            "length is left to the terrain and only the mode transfers");
    }
    sink.note(P_STEPS_A,
        "NO DESTINATION: Fireflow's deck-A STEPS knob has nowhere to go. "
        "P_STEPS_A is story-owned in flow (DENSITY's rate story), not a base "
        "rule, so an overlay entry for it would be read by nothing");

    // --- Rate, pitch and shape (per deck) ---
    for (int p = 0; p < 2; ++p) {
        const int tune  = p ? P_TUNE_B  : P_TUNE_A;
        const int rate  = p ? P_RATE_B  : P_RATE_A;
        const int shape = p ? P_SHAPE_B : P_SHAPE_A;
        const int smth  = p ? P_SMOOTH_B: P_SMOOTH_A;
        const int rng   = p ? P_RANGE_B : P_RANGE_A;
        const int depth = p ? P_DEPTH_B : P_DEPTH_A;

        detail::set_base(r, tune,  detail::pp(fp, kFfTuneA,   p));
        detail::set_base(r, rate,  detail::pp(fp, kFfRateA,   p));
        detail::set_base(r, shape, detail::pp(fp, kFfShapeA,  p));
        detail::set_base(r, smth,  detail::pp(fp, kFfSmoothA, p));
        detail::set_base(r, rng,   detail::pp(fp, kFfRangeA,  p));
        // The panel prints MOD; the parameter is DEPTH. Verified from the call
        // site (set_depth(p, pp(MOD_A, p))), not from the caption -- and this
        // engine has a set_depth naming collision besides (spotykach-gotchas).
        detail::set_base(r, depth, detail::pp(fp, kFfModA,    p));

        // The cap is FLOW-mode only (flow.cpp:555-559 tests !_mode_now), and
        // this transfer has already decided the mode from COUPLE's zone -- so
        // on a patch that converts to STEP the cap cannot apply and the note
        // would be false.
        if (eng[p] == spky::ENGINE_BBD && !grid)
            sink.note(rng,
                "carried in full, but this deck converts to a BBD in FLOW "
                "mode, where RANGE is capped at runtime (kBbdFlowRangeMax, "
                "flow.cpp:555-559). The stored value stands; what is heard may "
                "be lower");

        // --- Envelope and voice colour ---
        const int atk = p ? P_ATTACK_B : P_ATTACK_A;
        const int dec = p ? P_DECAY_B  : P_DECAY_A;
        const int res = p ? P_RES_B    : P_RES_A;
        detail::set_base(r, atk, detail::pp(fp, kFfAttackA, p));
        detail::set_base(r, dec, detail::pp(fp, kFfDecayA,  p));
        const float resKnob = detail::pp(fp, kFfResA, p);
        detail::set_base(r, res, resKnob);
        if (resKnob > kParams[res].hi)
            sink.note(res,
                "CLAMPED: Fireflow's RES runs the full 0..1, flow caps it at "
                "0.75 -- the by-ear resonance ceiling, a hard limit in the "
                "parameter table (flow_params.h:21, :86)");

        // --- FX sends ---
        const int flux = p ? P_FLUXMIX_B : P_FLUXMIX_A;
        const float fluxMix = detail::pp(fp, kFfFluxA, p);
        detail::set_base(r, flux, fluxMix);
        if (fluxMix > 1e-4f)
            sink.note(flux,
                "carried, but currently inaudible under Glow: Fireflow gates "
                "the FLUX block with set_fx_on(), which nothing in engine/flow/ "
                "ever calls, and SoftSwitch defaults off. A pre-existing gap in "
                "the flow layer, not a conversion loss");

        // GRIT_A/B joined the base rules on 2026-08-12, from the deleted DIRT
        // story. Store what Fireflow pushes to set_grit_mix, not the knob --
        // the dead-zone formula is the exact shape of the four conversion
        // changes this file's header warns about.
        const int grit = p ? P_GRIT_B : P_GRIT_A;
        const float gritKnob = detail::pp(fp, kFfGritA, p);
        detail::set_base(r, grit, detail::grit_from_knob(gritKnob));
        if (std::fabs(gritKnob) > 1e-4f)
            sink.note(grit,
                "carried, but currently inaudible under Glow: Fireflow gates "
                "the GRIT block with set_fx_on(), which nothing in engine/flow/ "
                "ever calls, and SoftSwitch defaults off. A pre-existing gap in "
                "the flow layer, not a conversion loss");
        // The sign is the OTHER half of this knob -- it picks Reduce vs Drive
        // (set_grit_mode) -- and flow has no P_GRIT_MODE at all, so it cannot
        // travel with the magnitude that does.
        if (gritKnob < 0.f)
            sink.note(grit,
                "NOT CARRIED: the knob's sign chose Reduce mode (set_grit_mode); "
                "flow has no P_GRIT_MODE, so only the magnitude travels and it "
                "is heard, if the block is ever switched on, as Drive instead");
    }

    // SUB is strided (Fireflow.cpp:576 pushes it with pp(SUB_A, p)), unlike
    // COLOR and LINK below. P_SUB_A became a base rule on 2026-08-12, when
    // M_DENSITY's "thick" variant was deleted -- before that this knob had no
    // destination at all.
    detail::set_base(r, P_SUB_A, detail::pp(fp, kFfSubA, 0));
    detail::set_base(r, P_SUB_B, fp.p[kFfSubB]);
    for (int p = 0; p < 2; ++p)
        if (eng[p] == spky::ENGINE_SAMPLER)
            sink.note(p ? P_SUB_B : P_SUB_A,
                "carried as the sub level, but HALF of this knob is lost: on a "
                "Sampler deck Fireflow additionally re-points SUB to LANE_SIZE "
                "as GENE SIZE (Fireflow.cpp:820-824), which flow never writes");

    // COLOR is the CHORD SIZE, not a timbre tint -- ChordBuilder::set_color
    // counts tones over fixed zone edges (pitch/chord.h), so this row decides
    // whether a deck plays one note or four. It became a base rule on
    // 2026-08-12; until then a carried patch's chords did not travel at all,
    // and on half of all terrains no macro could bring them back.
    //
    // THE EXPLICIT INDEX, not the stride: COLOR_A/B are appended params, and
    // Fireflow.cpp:575 pushes them with its own ternary for exactly this
    // reason. pp(kFfColorA, 1) would read a different control entirely.
    detail::set_base(r, P_COLOR_A, fp.p[kFfColorA]);
    detail::set_base(r, P_COLOR_B, fp.p[kFfColorB]);

    // --- The LVL knob, which is not the comp knob ---
    {
        const float lvl  = fp.p[kFfCompB];
        const float comp = detail::comp_from_lvl(lvl);
        detail::set_base(r, P_COMP_B, comp);
        float lo = 0.f, hi = 1.f;
        const bool vetoed = detail::veto_band(P_COMP_B, lo, hi);
        if (vetoed && (comp < lo || comp > hi))
            sink.note(P_COMP_B,
                "REWRITTEN AT RUNTIME: the LVL knob's compressor half converts "
                "cleanly, but taste.h's veto band confines P_COMP_B to "
                "0.40..0.60 (a by-ear ruling), so this value will not be heard "
                "as stored. LVL's other half -- the deck's output level -- has "
                "no flow destination at all");
        else
            sink.note(P_COMP_B,
                "carried as the compressor amount Fireflow pushed, not as the "
                "knob position: LVL is a split control whose lower zone is "
                "deck output level, and flow never calls set_part_level, so "
                "both decks sit at 1.0. taste.h's veto band (0.40..0.60) has "
                "the last word on what remains");
    }
    // P_COMP_A joined P_COMP_B's veto band on 2026-08-12, when it left the
    // DIRT story -- identical formula, identical band, read off deck A's own
    // LVL knob. Modelled on the P_COMP_B block above rather than duplicated
    // by hand, so the two cannot drift apart the way the four merged-control
    // conversions once did.
    {
        const float lvl  = fp.p[kFfCompA];
        const float comp = detail::comp_from_lvl(lvl);
        detail::set_base(r, P_COMP_A, comp);
        float lo = 0.f, hi = 1.f;
        const bool vetoed = detail::veto_band(P_COMP_A, lo, hi);
        if (vetoed && (comp < lo || comp > hi))
            sink.note(P_COMP_A,
                "REWRITTEN AT RUNTIME: the LVL knob's compressor half converts "
                "cleanly, but taste.h's veto band confines P_COMP_A to "
                "0.40..0.60 (a by-ear ruling), so this value will not be heard "
                "as stored. LVL's other half -- the deck's output level -- has "
                "no flow destination at all");
        else
            sink.note(P_COMP_A,
                "carried as the compressor amount Fireflow pushed, not as the "
                "knob position: LVL is a split control whose lower zone is "
                "deck output level, and flow never calls set_part_level, so "
                "both decks sit at 1.0. taste.h's veto band (0.40..0.60) has "
                "the last word on what remains");
    }
    // The second fact the old "P_COMP_A is story-owned" note also carried,
    // kept on its own now that COMP_A has a destination and a note of its own
    // above: LVL's lower zone is output level on EITHER deck, and flow never
    // calls set_part_level at all (see both notes above), so the deck
    // BALANCE a Fireflow patch was built with -- which deck sits louder
    // against the other -- never transfers, independent of whatever the
    // compressor half does.
    sink.note(kNoteGeneral,
        "NOT CARRIED: the deck BALANCE Fireflow's LVL knobs set (their lower "
        "zone, output level) has no flow destination on either deck -- flow "
        "never calls set_part_level, so both decks always sit at output level "
        "1.0 regardless of where LVL was set");

    // Appended params: the explicit index, never the stride (Fireflow.cpp:593
    // carries the same warning at its own push site).
    detail::set_base(r, P_LINK_A, fp.p[kFfLinkA]);
    detail::set_base(r, P_LINK_B, fp.p[kFfLinkB]);

    // --- Global ---
    detail::set_base(r, P_MORPH, fp.p[kFfMorph]);

    // COUPLE runs two worlds on one axis (the zone split read above). Store the
    // RESCALED half-zone value, because that is what Fireflow handed to
    // set_couple -- the knob position itself is not a couple amount.
    detail::set_base(r, P_COUPLE,
        grid ? (coupleKnob - kCoupleZoneSplit) / (1.f - kCoupleZoneSplit)
             : coupleKnob / kCoupleZoneSplit);

    // TIDE scales the four TEXTURE lanes against the PITCH lane, x1/4..x4 off
    // one knob (tide_free / kTideRatios, mod/divisions.h). A base rule since
    // 2026-08-12: while the terrain owned it, a carried patch kept its RATE
    // exactly and still lost its motion, because the texture lanes could run
    // at four times the speed the patch was built at.
    detail::set_base(r, P_TIDE, fp.p[kFfTide]);

    detail::set_base(r, P_CHOKE, fp.p[kFfChoke] * 0.5f);
    detail::set_base(r, P_SHUFFLE, fp.p[kFfShuffle]);
    detail::set_base(r, P_REV_DIFF, fp.p[kFfRevDiff]);

    // PACE is a new row, added the same day the PACE work deleted the DIRT
    // story. The knob is already the normalized 0..1 position set_pace
    // wants -- direct, nothing to transform -- and it is deliberately a base
    // rule rather than a story target: a story-owned parameter is
    // unreachable from a BaseOverlay by construction, which would throw away
    // a transferred patch's own speed the same way TIDE once did.
    detail::set_base(r, P_PACE, fp.p[kFfPace]);

    const float bpm = 40.f + fp.p[kFfTempo] * 200.f;
    detail::set_base(r, P_TEMPO_BPM, bpm);
    if (bpm < kParams[P_TEMPO_BPM].lo || bpm > kParams[P_TEMPO_BPM].hi)
        sink.note(P_TEMPO_BPM,
            "CLAMPED: Fireflow's TEMPO knob spans 40..240 BPM, flow's terrain "
            "range is 50..140. The patch's tempo is not the one it will play "
            "at");
    // Unconditional, like the P_ROOT note and for the same reason: this one is
    // invisible from the overlay. The value converts and is applied, and then
    // the HOST overwrites it -- Glow re-pushes its TEMPO fader on every control
    // tick (Glow.cpp:1039-1042), because the terrain owns tempo and re-pushes
    // it on every terrain change, so a fader that did not re-assert itself
    // would be undone by the next wake. The left fader is assigned to TEMPO by
    // default and boots at mid travel, which is 95 BPM. A carried tempo is
    // therefore heard only if that fader is set to `off` or parked on it.
    sink.note(P_TEMPO_BPM,
        "REWRITTEN AT RUNTIME: the value converts and is applied, and then "
        "Glow's TEMPO fader overwrites it every control tick. That fader is "
        "assigned by default and boots at 95 BPM. Set the fader's target to "
        "`off` to hear the patch's own tempo. In FLOW mode this costs nothing "
        "-- the mod lanes run on absolute Hz there and ignore BPM entirely -- "
        "but in STEP mode it moves the whole grid");

    // P_MODE is not an independent control on either side. In Fireflow the zone
    // split of COUPLE is the whole of set_sync, and in flow P_MODE is the whole
    // of set_sync -- so the value IS determined by the patch, and leaving it
    // unset would hand a hand-authored stepped patch to the terrain's mode
    // coin, which is worse. What it cannot represent is a mixed patch: flow's
    // one P_MODE drives set_sync AND both decks' step flags, while Fireflow
    // takes each deck's step flag from that deck's own STEPS knob.
    detail::set_base(r, P_MODE, grid ? 1.f : 0.f);
    if (grid != (steps[0] > 0) || grid != (steps[1] > 0))
        sink.note(P_MODE,
            "LOSSY: the decks disagree with the global grid. Fireflow takes "
            "each deck's step flag from its own STEPS knob while set_sync is "
            "global; flow's single P_MODE drives set_sync and BOTH decks' step "
            "flags. A patch with one deck free and one stepped has no "
            "representation, and the COUPLE zone decided it for both");

    sink.note(P_SONG_A,
        "NO DESTINATION: Fireflow's deck-A SONG ladder (its FORM and its SONG "
        "both) has nowhere to go -- P_FORM_A and P_SONG_A are story-owned in "
        "flow, not base rules. Only deck B's structure transfers");

    // flow has 64 parameters and 47 base rules, so 17 are story-owned. Three
    // of those (FORM_A, SONG_A, STEPS_A) have a Fireflow control of their own
    // and are named individually above, because a knob the owner actually
    // turned deserves better than a bucket. These are the other 14 -- and the
    // list is the whole 14, per deck where the parameter is per deck.
    //
    // The count moved from 21 on 2026-08-12, when the PACE work deleted the
    // DIRT story outright and all four of its targets left this list: GRIT_A
    // and GRIT_B are now carried with their own note above (beside FLUXMIX's,
    // in the per-deck block), and COMP_A moved in beside COMP_B. DRIVE also
    // became a base rule that day, but Fireflow still has no control for it
    // at all -- unlike P_ROOT's row, its map entry names no report
    // obligation, so a transferred patch's DRIVE stays exactly what it always
    // was: the terrain's own draw, unmentioned here as before.
    sink.note(kNoteGeneral,
        "NOT TRANSFERABLE AT ALL (no slot, not \"transferred with loss\"): the "
        "14 remaining story-owned parameters -- DENSITY, VARY, FILT and "
        "REVMIX, each on both decks, plus DRIFT and the reverb shape (SIZE, "
        "DECAY, TONE, SMEAR, MOD). The other three story-owned parameters "
        "have Fireflow controls of their own and are named above. Outside "
        "flow's parameter set entirely, and equally lost: sample content, "
        "SOURCE, FLUX RATE and FEEDBACK, DETUNE, STAGES, REC and the "
        "excitation bus");

    sink.finish();
    return r;
}

// ---------------------------------------------------------------------------
// format_report
// ---------------------------------------------------------------------------

namespace detail {

// The one head line, so the full report and the menu summary cannot come to
// disagree about how much was carried.
inline std::string report_head(const TransferReport& r) {
    using namespace spky::flow;
    int carried = 0;
    for (int p = 0; p < P_COUNT; ++p) if (r.overlay.has[p]) ++carried;

    char head[128];
    if (r.overlay_rejected)
        std::snprintf(head, sizeof head,
                      "Fireflow -> flow: TRANSFER REJECTED, nothing carried");
    else
        std::snprintf(head, sizeof head,
                      "Fireflow -> flow: %d of %d base parameters carried",
                      carried, kBaseRuleCount);
    return std::string(head);
}

// kParams[p].name already holds the P_* spelling; strip the prefix so a report
// reads in panel language rather than in enum language.
inline std::string note_label(int param) {
    using namespace spky::flow;
    if (param < 0 || param >= P_COUNT) return "(patch)";
    const char* nm = kParams[param].name;
    if (std::strncmp(nm, "P_", 2) == 0) nm += 2;
    return std::string(nm);
}

} // namespace detail

inline std::string format_report(const TransferReport& r) {
    std::string s = detail::report_head(r);
    s += "\n";
    for (int i = 0; i < r.note_count; ++i) {
        const TransferNote& n = r.notes[i];
        s += "  ";
        s += detail::note_label(n.param);
        s += ": ";
        s += n.reason ? n.reason : "";
        s += "\n";
    }
    return s;
}

// ---------------------------------------------------------------------------
// wrap_lines -- report text as a menu can hold it
// ---------------------------------------------------------------------------
//
// A block of report text as a menu can hold it: one entry per line, no blanks,
// wrapped. Two things stop raw report text from being that list, and neither is
// a Rack question:
//
//   * it terminates EVERY line with '\n', so the obvious split leaves a
//     trailing empty piece -- a blank menu row with no explanation;
//   * Rack's MenuLabel widens the menu to fit its text, and the notes here run
//     to three hundred characters. Unwrapped, one of them drags the context
//     menu off the screen and the report is unreadable precisely when it
//     matters.
//
// Pure string work with no Rack type in sight, so it lives here where the
// desktop suite gates it rather than in Fireflow.cpp where only the plugin
// build would ever see it.
//
// NOTHING IS DROPPED. A word longer than `columns` gets its own over-long line
// instead of being cut: this report may be ugly, it may not be incomplete.
// Continuation lines are indented past their line's own indent, so a wrapped
// note reads as one note and not as two.
namespace detail {

inline std::vector<std::string> wrap_lines(const std::string& text,
                                           int columns) {
    // Narrower than this and even a bare parameter name plus its indent would
    // wrap, which turns the guarantee above into a column of single words.
    if (columns < 24) columns = 24;
    std::vector<std::string> out;

    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t nl = text.find('\n', start);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(start, nl - start);
        start = nl + 1;

        const std::size_t lead = line.find_first_not_of(' ');
        if (lead == std::string::npos) continue;   // blank, and the trailing ''
        const std::string cont(lead + 2, ' ');

        std::string cur(lead, ' ');
        bool empty = true;
        std::size_t w = lead;
        while (w < line.size()) {
            std::size_t e = line.find(' ', w);
            if (e == std::string::npos) e = line.size();
            const std::string word = line.substr(w, e - w);
            w = e;
            while (w < line.size() && line[w] == ' ') ++w;
            if (word.empty()) continue;
            if (!empty && cur.size() + 1 + word.size() > std::size_t(columns)) {
                out.push_back(cur);
                cur   = cont;
                empty = true;
            }
            if (!empty) cur += ' ';
            cur += word;
            empty = false;
        }
        if (!empty) out.push_back(cur);
    }
    return out;
}

} // namespace detail

// ---------------------------------------------------------------------------
// The menu summary
// ---------------------------------------------------------------------------
//
// The full report is a dozen notes of paragraph-length prose, which comes to
// about fifty menu rows once wrapped (measured: 48 for a plain patch at 64
// columns). That buries every item under it and is unreadable exactly where it
// needs to be read. The menu gets the heaviest losses only -- six rows for the
// same patch -- and the whole thing travels on the clipboard, where a text
// editor can hold it.
//
// "Heaviest" is these tags, and the table is the selection rule: a note whose
// reason opens with one of them says the patch will not sound the way it looks
// -- it was refused, it has no Fireflow source at all, it was moved to fit, or
// the runtime will overwrite it. The tags a note can carry that are NOT here
// ("NO DESTINATION", "NOT CARRIED", "NOT TRANSFERABLE AT ALL") all say the same
// structural thing -- flow has no slot for that control and never did -- which
// is a property of the two instruments and not of the patch in hand.
//
// The tail line counts what it left out, so a tag renamed in to_flow_base
// without being renamed here cannot hide a loss: it moves into the count.
inline constexpr const char* kSevereTags[] = {
    "REJECTED WHOLE",
    "UNREACHABLE",
    "CLAMPED",
    "REWRITTEN AT RUNTIME",
    "LOSSY",
    "REPORT FULL",
};
inline constexpr int kSevereTagCount =
    int(sizeof kSevereTags / sizeof *kSevereTags);

// The tag this reason opens with, or nullptr if it is not one of the heavy
// ones. Exported rather than kept in detail:: so the suite can check the table
// against the reasons to_flow_base actually writes.
inline const char* severe_tag(const char* reason) {
    if (!reason) return nullptr;
    for (int i = 0; i < kSevereTagCount; ++i)
        if (std::strncmp(reason, kSevereTags[i],
                         std::strlen(kSevereTags[i])) == 0)
            return kSevereTags[i];
    return nullptr;
}

inline std::vector<std::string> report_summary_lines(const TransferReport& r,
                                                     int columns = 64) {
    std::string s = detail::report_head(r);
    s += "\n";
    int shown = 0;
    for (int i = 0; i < r.note_count; ++i) {
        const char* tag = severe_tag(r.notes[i].reason);
        if (!tag) continue;
        // The tag, not the reason. The reason is three sentences of why, which
        // is what the clipboard copy is for; here the point is WHICH parameter
        // and HOW badly, in one glance.
        s += "  ";
        s += detail::note_label(r.notes[i].param);
        s += ": ";
        s += tag;
        s += "\n";
        ++shown;
    }
    char tail[128];
    std::snprintf(tail, sizeof tail,
                  "%d of %d notes shown -- the whole report is on the "
                  "clipboard\n", shown, r.note_count);
    s += tail;
    return detail::wrap_lines(s, columns);
}

// ---------------------------------------------------------------------------
// The one textual encoding
// ---------------------------------------------------------------------------
//
// `NAME:value;` pairs keyed on kParams[p].name, one string used by pool.tsv,
// Glow's JSON and the clipboard alike (Tasks 7-8). One format, one round-trip
// test; a second encoding in a second language is what its gate exists to
// catch.
//
// Both halves ask taste.h's is_base_rule(): the encoder skips a story-owned
// entry, the decoder REFUSES one. The asymmetry is deliberate -- the encoder's
// input is an in-memory overlay this code built, the decoder's is text somebody
// typed, and only one of those can be wrong on purpose.

inline std::string encode_base(const spky::flow::BaseOverlay& ov) {
    using namespace spky::flow;
    std::string s;
    char buf[96];
    for (int p = 0; p < P_COUNT; ++p) {
        if (!ov.has[p]) continue;
        // The same is_base_rule() question set_base() and decode_into() ask, so
        // the partition is enforced in all three directions and this encoder can
        // never emit a string its own decoder refuses. Unreachable today --
        // every overlay that reaches here came from to_flow_base or from a
        // decode, and both filter already -- which is exactly why it is a
        // `continue` and not an assert: nothing may depend on it firing.
        if (!is_base_rule(p)) continue;
        // %.9g is the round-trip width for a float: fewer digits and a decoded
        // overlay is a DIFFERENT patch from the encoded one, silently.
        std::snprintf(buf, sizeof buf, "%s:%.9g;", kParams[p].name,
                      double(ov.v[p]));
        s += buf;
    }
    return s;
}

namespace detail {

inline bool decode_into(const char* text, spky::flow::BaseOverlay& out) {
    using namespace spky::flow;
    if (!text) return false;
    for (const char* q = text; *q; ) {
        // Whitespace between tokens, and whole '#' lines, are skipped. This is
        // what lets ONE clipboard string carry both the overlay and the report
        // that says what it lost (clipboard_base below) -- the encoded pairs on
        // the first line, the report commented out under them, readable in any
        // text editor and still pasteable onto a pad.
        //
        // An EXTENSION, not a loosening. Every string that decoded before still
        // decodes to the same overlay, and the all-or-nothing contract is
        // untouched: a malformed token still returns false and still leaves the
        // caller with an empty overlay rather than a plausible half. What
        // changed is only that some strings which used to be refused for
        // holding trailing prose are now read, and the prose is required to
        // announce itself with a '#'.
        //
        // THE CONSEQUENCE, stated because it is load-bearing rather than
        // incidental: a string made ONLY of whitespace and '#' lines -- "   ",
        // "\n", a pasted Markdown document -- now decodes SUCCESSFULLY, as the
        // EMPTY base. That is not a slip. A rejected transfer's clipboard is
        // exactly that shape (its overlay half is the empty string and the
        // whole payload is the report, commented out), and "a rejected
        // transfer's clipboard string pastes as an empty base" in
        // tests/test_flow_patch_bridge.cpp requires it. On the Glow side an
        // empty base is a real answer that CLEARS the pad's overlay, so this
        // reaches a destructive path: base_for_pad returns true with has =
        // false, and the paste menu announces it ("Clipboard: an EMPTY flow
        // base -- pasting CLEARS the pad") before the pad is clicked. Anything
        // that tightened this -- refusing comment-only strings -- would have to
        // give a rejected transfer a different clipboard shape first.
        if (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') { ++q; continue; }
        if (*q == '#') {
            while (*q && *q != '\n') ++q;
            continue;
        }
        const char* colon = std::strchr(q, ':');
        const char* semi  = std::strchr(q, ';');
        if (!semi || !colon || colon > semi) return false;
        const size_t nlen = size_t(colon - q);
        int found = -1;
        for (int p = 0; p < P_COUNT; ++p)
            if (std::strlen(kParams[p].name) == nlen &&
                std::strncmp(kParams[p].name, q, nlen) == 0) { found = p; break; }
        if (found < 0) return false;
        // The same is_base_rule() guard set_base() applies on the way IN, and
        // for the same reason: taste.h owns the base/story partition, and one
        // authority has to answer for both directions.
        //
        // Rejecting rather than dropping. An entry for a story-owned parameter
        // would be ignored by generate() -- Task 1 made that unreachable by
        // construction -- so nothing would break; it would just be a string
        // that claims to carry more than it can, which is the failure this
        // whole file is written against. This decoder is the entry point for
        // hand-edited pool.tsv rows and for clipboard text, so it reads
        // strings no converter wrote, and a person who typed P_DENSITY_A into
        // a base column deserves a refusal rather than a base that quietly
        // omits the one thing they added. It is also the rule the partial
        // parse below already gets: all or nothing, never a plausible half.
        if (!is_base_rule(found)) return false;
        const size_t vlen = size_t(semi - colon - 1);
        char val[64];
        if (vlen == 0 || vlen >= sizeof val) return false;
        std::memcpy(val, colon + 1, vlen);
        val[vlen] = '\0';
        char* end = nullptr;
        const float v = std::strtof(val, &end);
        // The whole token must be the number. A partial parse is how
        // "P_TUNE_A:0.5P_RES_B:0.3;" -- one missing separator -- would
        // otherwise decode to a plausible half patch.
        if (end != val + vlen) return false;
        out.v[found]   = v;
        out.has[found] = true;
        q = semi + 1;
    }
    return true;
}

} // namespace detail

// All or nothing: a rejected string leaves `out` empty rather than
// half-written, so a caller that ignores the return value gets no overlay
// instead of a plausible wrong one. The empty string is VALID and decodes to no
// overlay -- that is a place with no hand-authored patch, not a zeroed one.
// A string naming a parameter that is not a base rule is REJECTED, not
// filtered -- see the guard in decode_into.
inline bool decode_base(const char* text, spky::flow::BaseOverlay& out) {
    spky::flow::BaseOverlay tmp;
    const bool ok = detail::decode_into(text, tmp);
    out = ok ? tmp : spky::flow::BaseOverlay{};
    return ok;
}

// ---------------------------------------------------------------------------
// The clipboard, both halves of it
// ---------------------------------------------------------------------------
//
// What Fireflow's "Copy patch as flow base" puts on the clipboard: the encoded
// overlay on the first line, then the WHOLE report commented out beneath it.
// The menu shows only the heavy notes (report_summary_lines); this is where the
// rest of them live, and it is plain text, so reading it costs opening an
// editor rather than scrolling a context menu.
//
// The result must still paste. decode_base skips '#' lines and inter-token
// whitespace precisely so that it does, and the round trip of this complete
// string -- not merely of its first line -- is what the suite gates.
inline std::string clipboard_base(const TransferReport& r) {
    std::string s = encode_base(r.overlay);
    s += "\n";
    const std::string rep = format_report(r);
    std::size_t start = 0;
    while (start < rep.size()) {
        std::size_t nl = rep.find('\n', start);
        if (nl == std::string::npos) nl = rep.size();
        s += "# ";
        s.append(rep, start, nl - start);
        s += "\n";
        start = nl + 1;
    }
    return s;
}

// The paste decision, whole: what a pad should become given whatever text the
// clipboard is holding. Three answers, and the middle one is the subtle one.
//
//   * false            -- nothing usable (no clipboard at all, or a string that
//                         is not a flow base: a terrain code, a shopping list).
//                         The caller must leave the pad exactly as it was;
//                         `out` and `has` are not to be read.
//   * true, has=false  -- the EMPTY base, which decodes cleanly and means "no
//                         hand-authored patch". It is also what a REJECTED
//                         transfer copies, and it is a real answer rather than
//                         a failure: the pad is cleared back to playing its
//                         drawn terrain. Recording it as has=true instead would
//                         hand wakePad a pointer to an overlay with nothing in
//                         it and let the place claim a patch it has not got.
//   * true, has=true   -- a real base. The pad takes it.
//
// Note that the middle answer is DESTRUCTIVE if the pad already carried a base:
// pasting a rejected transfer onto a good place discards it. That is deliberate
// -- "paste this patch here" should leave the pad holding what the clipboard
// holds, and a paste that silently declined to clear would be the same lie in
// the other direction -- but it is exactly why Glow.cpp names the clipboard's
// state in the menu ABOVE the pad list, so the clearing is announced before it
// is clicked rather than discovered afterwards.
inline bool base_for_pad(const char* clip, spky::flow::BaseOverlay& out,
                         bool& has) {
    using namespace spky::flow;
    BaseOverlay tmp;
    if (!clip || !decode_base(clip, tmp)) return false;
    bool any = false;
    for (int p = 0; p < P_COUNT; ++p) any = any || tmp.has[p];
    out = tmp;
    has = any;
    return true;
}

} // namespace spkyvcv
