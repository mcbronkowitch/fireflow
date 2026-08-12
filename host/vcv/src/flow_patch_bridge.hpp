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
#include "mod/song_ladder.h"
#include "parts/engine_iface.h"

namespace spkyvcv {

// ---------------------------------------------------------------------------
// The index mirror
// ---------------------------------------------------------------------------
//
// generated_panel.hpp's `enum ParamId`, transcribed. It is NOT included here:
// it is generated from res/gen_panel.py and drags Rack types in with it, which
// would cost this header its headless build. The mirror is made safe by a
// static_assert per constant in Fireflow.cpp (Task 8) -- that assert, not this
// comment and not any plan text, is the authority on these numbers. Counted
// from the enum at flow-patch-transfer HEAD: part A occupies [0, 20), part B
// [20, 40), and 28 appended globals follow, so NUM_PARAMS == 68.
inline constexpr int kFfPartStride = 20;

inline constexpr int kFfRateA    = 0;
inline constexpr int kFfShapeA   = 1;
inline constexpr int kFfDensityA = 2;
inline constexpr int kFfSmoothA  = 3;
inline constexpr int kFfRangeA   = 4;
inline constexpr int kFfMelodyA  = 5;
inline constexpr int kFfModA     = 6;    // prints "MOD", drives P_DEPTH_A
inline constexpr int kFfTuneA    = 7;
inline constexpr int kFfAttackA  = 8;
inline constexpr int kFfDecayA   = 9;
inline constexpr int kFfResA     = 10;
inline constexpr int kFfSubA     = 11;
inline constexpr int kFfSourceA  = 12;
inline constexpr int kFfFluxA    = 13;
inline constexpr int kFfGritA    = 14;
inline constexpr int kFfCompA    = 15;   // prints "LVL", drives P_COMP_A
inline constexpr int kFfStepsA   = 16;
inline constexpr int kFfEngineA  = 17;
inline constexpr int kFfDetuneA  = 18;
inline constexpr int kFfSongA    = 19;

inline constexpr int kFfRateB    = 20;
inline constexpr int kFfShapeB   = 21;
inline constexpr int kFfDensityB = 22;
inline constexpr int kFfSmoothB  = 23;
inline constexpr int kFfRangeB   = 24;
inline constexpr int kFfMelodyB  = 25;
inline constexpr int kFfModB     = 26;
inline constexpr int kFfTuneB    = 27;
inline constexpr int kFfAttackB  = 28;
inline constexpr int kFfDecayB   = 29;
inline constexpr int kFfResB     = 30;
inline constexpr int kFfSubB     = 31;
inline constexpr int kFfSourceB  = 32;
inline constexpr int kFfFluxB    = 33;
inline constexpr int kFfGritB    = 34;
inline constexpr int kFfCompB    = 35;   // prints "LVL", drives P_COMP_B
inline constexpr int kFfStepsB   = 36;
inline constexpr int kFfEngineB  = 37;
inline constexpr int kFfDetuneB  = 38;
inline constexpr int kFfSongB    = 39;   // prints "SONG", drives FORM *and* SONG

// Appended globals -- NOT part-strided. Indexing these by kFfPartStride is the
// bug Fireflow.cpp guards against with an explicit ternary at every one of
// their push sites; this file does the same.
inline constexpr int kFfMorph     = 40;
inline constexpr int kFfTempo     = 41;
inline constexpr int kFfCouple    = 42;  // prints "FREE|GRID"; also the sync zone
inline constexpr int kFfScale     = 43;
inline constexpr int kFfDrift     = 44;
inline constexpr int kFfRevSize   = 45;
inline constexpr int kFfRevDecay  = 46;
inline constexpr int kFfRevTone   = 47;
inline constexpr int kFfRevDiff   = 48;
inline constexpr int kFfChoke     = 49;
inline constexpr int kFfFiltA     = 50;
inline constexpr int kFfFiltB     = 51;
inline constexpr int kFfTide      = 52;
inline constexpr int kFfFluxRateA = 53;
inline constexpr int kFfFluxRateB = 54;
inline constexpr int kFfFluxFbA   = 55;
inline constexpr int kFfFluxFbB   = 56;
inline constexpr int kFfColorA    = 57;
inline constexpr int kFfColorB    = 58;
inline constexpr int kFfLinkA     = 59;
inline constexpr int kFfLinkB     = 60;
inline constexpr int kFfStagesA   = 61;
inline constexpr int kFfStagesB   = 62;
inline constexpr int kFfRecA      = 63;
inline constexpr int kFfRecB      = 64;
inline constexpr int kFfRevMixA   = 65;
inline constexpr int kFfRevMixB   = 66;
inline constexpr int kFfShuffle   = 67;

inline constexpr int kFireflowParamCount = 68;   // == NUM_PARAMS

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

        if (eng[p] == spky::ENGINE_BBD)
            sink.note(rng,
                "carried in full, but a BBD deck in FLOW mode has its RANGE "
                "capped at runtime (kBbdFlowRangeMax, flow.cpp:555-559). The "
                "stored value stands; what is heard may be lower");

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

    // COUPLE runs two worlds on one axis; the zone split is also the only
    // thing in Fireflow that drives set_sync. Store the RESCALED half-zone
    // value, because that is what Fireflow handed to set_couple.
    const float coupleKnob = fp.p[kFfCouple];
    static constexpr float kCoupleZoneSplit = 0.5f;
    const bool grid = coupleKnob >= kCoupleZoneSplit;
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

    sink.note(kNoteGeneral,
        "NOT TRANSFERABLE AT ALL (no slot, not \"transferred with loss\"): the "
        "25 story-owned parameters -- FILT, COLOR, VARY, DENSITY, GRIT (its "
        "mode as well as its mix), REVMIX, DRIFT, TIDE, DRIVE and the reverb "
        "shape -- plus everything outside flow's parameter set entirely: "
        "sample content, SOURCE, FLUX RATE and FEEDBACK, DETUNE, STAGES, REC "
        "and the excitation bus");

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

inline std::string encode_base(const spky::flow::BaseOverlay& ov) {
    using namespace spky::flow;
    std::string s;
    char buf[96];
    for (int p = 0; p < P_COUNT; ++p) {
        if (!ov.has[p]) continue;
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
inline bool decode_base(const char* text, spky::flow::BaseOverlay& out) {
    spky::flow::BaseOverlay tmp;
    const bool ok = detail::decode_into(text, tmp);
    out = ok ? tmp : spky::flow::BaseOverlay{};
    return ok;
}

} // namespace spkyvcv
