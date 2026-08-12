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
    }

    detail::set_base(r, P_SUB_B, fp.p[kFfSubB]);
    sink.note(P_SUB_A,
        "NO DESTINATION: Fireflow's deck-A SUB knob has nowhere to go -- "
        "P_SUB_A is not a base rule. On a Sampler deck the same knob is also "
        "re-pointed to GENE SIZE, which flow never writes at all");

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
    sink.note(P_COMP_A,
        "NO DESTINATION: Fireflow's deck-A LVL knob has nowhere to go -- "
        "P_COMP_A is story-owned, not a base rule. With it goes the deck "
        "BALANCE, which flow has no control over on either deck");

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

    detail::set_base(r, P_CHOKE, fp.p[kFfChoke] * 0.5f);
    detail::set_base(r, P_SHUFFLE, fp.p[kFfShuffle]);
    detail::set_base(r, P_REV_DIFF, fp.p[kFfRevDiff]);

    const float bpm = 40.f + fp.p[kFfTempo] * 200.f;
    detail::set_base(r, P_TEMPO_BPM, bpm);
    if (bpm < kParams[P_TEMPO_BPM].lo || bpm > kParams[P_TEMPO_BPM].hi)
        sink.note(P_TEMPO_BPM,
            "CLAMPED: Fireflow's TEMPO knob spans 40..240 BPM, flow's terrain "
            "range is 50..140. The patch's tempo is not the one it will play "
            "at");

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

    // flow has 63 parameters and 38 base rules, so 25 are story-owned. Five of
    // those (FORM_A, SONG_A, STEPS_A, SUB_A, COMP_A) have a Fireflow control of
    // their own and are named individually above, because a knob the owner
    // actually turned deserves better than a bucket. These are the other 20 --
    // and the list is the whole 20, per deck where the parameter is per deck.
    sink.note(kNoteGeneral,
        "NOT TRANSFERABLE AT ALL (no slot, not \"transferred with loss\"): the "
        "20 remaining story-owned parameters -- DENSITY, COLOR, VARY, FILT, "
        "GRIT (its mode as well as its mix) and REVMIX, each on both decks, "
        "plus DRIFT, TIDE, DRIVE and the reverb shape (SIZE, DECAY, TONE, "
        "SMEAR, MOD). The other five story-owned parameters have Fireflow "
        "controls of their own and are named above. Outside flow's parameter "
        "set entirely, and equally lost: sample content, SOURCE, FLUX RATE and "
        "FEEDBACK, DETUNE, STAGES, REC and the excitation bus");

    sink.finish();
    return r;
}

// ---------------------------------------------------------------------------
// format_report
// ---------------------------------------------------------------------------

inline std::string format_report(const TransferReport& r) {
    using namespace spky::flow;
    std::string s;
    int carried = 0;
    for (int p = 0; p < P_COUNT; ++p) if (r.overlay.has[p]) ++carried;

    char head[128];
    if (r.overlay_rejected)
        std::snprintf(head, sizeof head,
                      "Fireflow -> flow: TRANSFER REJECTED, nothing carried\n");
    else
        std::snprintf(head, sizeof head,
                      "Fireflow -> flow: %d of %d base parameters carried\n",
                      carried, kBaseRuleCount);
    s += head;

    for (int i = 0; i < r.note_count; ++i) {
        const TransferNote& n = r.notes[i];
        s += "  ";
        // kParams[p].name already holds the P_* spelling; strip the prefix so
        // the report reads in panel language rather than in enum language.
        if (n.param >= 0 && n.param < P_COUNT) {
            const char* nm = kParams[n.param].name;
            if (std::strncmp(nm, "P_", 2) == 0) nm += 2;
            s += nm;
        } else {
            s += "(patch)";
        }
        s += ": ";
        s += n.reason ? n.reason : "";
        s += "\n";
    }
    return s;
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

} // namespace spkyvcv
