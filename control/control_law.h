#pragma once
// The control law both hosts run (spec docs/superpowers/specs/
// 2026-10-09-rev-a-p6b1-shared-control-law-design.md). This is what was
// Fireflow::pushParams(): knob values in parameter units in, Instrument setter
// calls out. No Rack type, no hardware type, no file I/O. Templated on the
// instrument so tests/test_control_law.cpp can run it on a recorder; both
// hosts instantiate it with spky::Instrument.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <type_traits>
#include "instrument.h"
#include "mod/song_ladder.h"
#include "control/params.hpp"
#include "control/mod_layer.hpp"
#include "control/bbd_edge_state.hpp"
#include "control/song_rung_state.hpp"
#include "control/drift_settle_state.hpp"

namespace control {

// COUPLE swallowed the SYNC switch (task 7, spec 2026-08-09
// hw-control-reduction): SYNC was the right-hand end of COUPLE's own axis.
// Below the split the knob is the FREE world (couple drives the Kuramoto
// lock); at or above it the GRID world (couple sets how tightly the texture
// lanes follow). Each half sweeps 0..1, so the grid world keeps its full
// spread -- "on the grid but breathing" stays reachable. Shared by the
// RATE/TIDE tooltips (Fireflow.cpp) and _tick() below; mirrored (not shared -- see
// res/test_panel.py) in bench/audition/init_patch.cpp.
inline constexpr float kCoupleZoneSplit = 0.5f;

// GRIT is one bipolar knob (spec 2026-08-09 hw-control-reduction task
// 4): sign picks the mode, magnitude is the mix. The dead zone exists
// because a 9 mm pot on an ADC cannot hit an exact zero -- without it
// "off" would be unreachable on hardware. Shared between the fx_on gate
// below and the mode/mix push further down so both agree on what
// "engaged" means.
inline constexpr float kGritDead = 0.03f;

// What the host's menus feed the law, per deck. Defaults = a fresh
// spkyvcv::SamplerPartState (host/vcv/src/sampler_ui.hpp), so a host that
// passes {} -- the firmware -- runs exactly what a fresh VCV deck runs.
struct DeckOptions {
    int   tape_idx          = 1;      // SamplerPartState::tapeIdx (1 = Tape)
    bool  reverse           = false;  // SamplerPartState::reverse
    float feedback          = 0.95f;  // SamplerPartState::feedback
    bool  test_tone         = false;  // SamplerPartState::testTone
    bool  excite_tape       = true;   // SamplerPartState::exciteTape
    bool  excite_other_deck = false;  // SamplerPartState::exciteOtherDeck
    bool  excite_audio_in   = false;  // SamplerPartState::exciteAudioIn
};

struct Options {
    DeckOptions deck[2];
    // The host's CLOCK measurement in BPM, 0 when it has none. Accepted in
    // 20..400 only; outside that (or 0) the TEMPO knob sets the tempo.
    float measured_bpm = 0.f;
};

// What the law used to write back into Rack. The host applies them (VCV) or
// ignores them (the firmware: a physical pot cannot be turned back).
struct Events {
    bool bbd_edge[2]    = {};   // genuine entry into BBD: FLUX to 0, exciteOtherDeck on
    bool rec_started[2] = {};   // REC began: the buffer no longer matches its source
};

// The host's seam inside one deck's block: called right after set_engine and
// the excitation bus, where VCV's factory autoload used to sit.
struct NoHooks { template <class Inst> void after_engine(int, int, Inst&) {} };

template <class Inst, class Hooks = NoHooks>
class ControlLawT {
public:
    ControlLawT() {
        for (int i = 0; i < ffctl::NUM_PARAMS; ++i) _modIdxBySound[i] = -1;
        for (size_t i = 0; i < sizeof(ffctl::kModLayer) / sizeof(ffctl::kModLayer[0]); ++i)
            _modIdxBySound[ffctl::kModLayer[i].soundId] = (int)i;
    }
    // VCV's onReset: song rung + drift settle. NOT the BBD edge -- onReset
    // never re-armed it, and folding the two entry points would change VCV.
    void on_reset()   { for (auto& s : _songRung) s.rearm(); _driftSettled.rearm(); }
    // A patch restore (VCV's live dataFromJson, the firmware's boot): all three.
    void on_restore() { for (auto& b : _bbdEdge) b.rearm(); on_reset(); }

    // knobs: all ffctl::NUM_PARAMS values in parameter units, exactly what
    // VCV's params[] holds.
    // The hook-less overload exists only for the hook-less law (the firmware):
    // a host with real hooks calling this would silently drop them (VCV's
    // factory autoload), so it must use the four-argument form.
    Events tick(const float* knobs, const Options& opt, Inst& inst) {
        static_assert(std::is_same<Hooks, NoHooks>::value,
                      "a law with Hooks must be ticked with its hooks");
        NoHooks h; return _tick(knobs, opt, inst, h);
    }
    Events tick(const float* knobs, const Options& opt, Inst& inst, Hooks& hooks) {
        return _tick(knobs, opt, inst, hooks);
    }

private:
    const float* _k = nullptr;
    // Edge-detects the ENG switch landing on BBD, so the FLUX-off and
    // excite-other-deck defaults below (spec 5.11/5.12) apply once on a
    // genuine player-driven transition and never fight a player who
    // deliberately turns them back on afterward. on_restore() re-arms it, so
    // a RESTORE (fresh add, whole-patch load, Ctrl+D duplicate, the firmware's
    // boot) adopts whatever ENG it finds as the baseline and fires nothing
    // when the instrument already runs that engine. Known exception, open
    // item (Ruling 10, measured): an engine switch lands inside process()
    // blocks after the tick that asks for it, so a restore onto BBD over a
    // live non-BBD deck fires the edge on block 2 with or without the
    // re-arm. Full reasoning, and why this is its own unit-tested type
    // rather than inline bools, lives in bbd_edge_state.hpp.
    spkyvcv::BbdEdgeState _bbdEdge[spky::PART_COUNT];
    // MOD latch layer state for one control tick: the lane outputs and the two
    // masters, sampled once at the top of this law's tick so every mv() read in the
    // same tick sees the same modulation frame (spec 2026-08-22 §3b).
    float _laneOut[spky::PART_COUNT][spky::LANE_COUNT] = {};
    float _laneOutStepped[spky::PART_COUNT][spky::LANE_COUNT] = {};
    float _modMaster[spky::PART_COUNT] = {};
    // Reverse index into kModLayer, keyed by SOUND param id: -1 means the face
    // owns no depth param. Built once in the constructor -- mv() runs per param
    // per control tick and must not scan the table.
    int _modIdxBySound[ffctl::NUM_PARAMS];
    // Tracks the SONG knob's current rung so tick() can detect a genuine
    // rung change and re-roll the phrase -- SONG swallowed FORM and the NEW
    // pad (spec 2026-08-09 hw-control-reduction task 3). Seeded/rearm shape
    // (song_rung_state.hpp) so a RESTORED rung -- patch load, preset load,
    // module add, or Initialize -- adopts as a baseline instead of firing.
    spkyvcv::SongRungState _songRung[spky::PART_COUNT];
    // Edge-detects DRIFT parking at its own left stop -- the old SETL pad's
    // job, folded into the knob's lower kDriftSettleZone (spec 2026-08-09
    // hw-control-reduction task 8). Same seeded/rearm shape as bbdEdge/
    // songRung above, for the same reason: a RESTORED DRIFT already in the
    // zone must adopt as a baseline instead of firing settle() on load. See
    // drift_settle_state.hpp.
    spkyvcv::DriftSettleState _driftSettled;

    float prm(int id) const { return _k[id]; }
    // Read a per-part param: baseId is the PART A enum, part in {0,1}.
    float pp(int baseA, int part) const { return _k[baseA + part * ffctl::PART_STRIDE]; }
    bool  ppb(int baseA, int part) const { return pp(baseA, part) > 0.5f; }

    // MOD-layer read of a host-computed sound param: knob + depth * MOD *
    // lane, in KNOB space, clamped to the param's own declared range (spec
    // 2026-08-22 §3b). Non-targets and the engine-backed faces fall straight
    // through to the knob -- their modulation happens inside the Part, so
    // adding a host-side term here would modulate them twice.
    //
    // At init every host-computed depth is 0 and modded() returns the knob by
    // early return, so this whole layer is a no-op on a fresh patch: the push
    // stream is exactly what it was before the layer existed.
    // No `part` parameter (fix round 2): the master/lane index the term
    // needs is `t.part`, read out of the very kModLayer row this function
    // already looks up by soundId -- a caller-supplied part that disagreed
    // with it used to be silently obeyed instead of the row's own part,
    // which is the same shape of cross-deck aliasing the DEPTH_A/DEPTH_B fix
    // above closed (F1). All eight call sites already passed a part matching
    // t.part (verified by inspection, fix round 2), so this is a no-op in
    // behaviour; it just removes the ability to get it wrong.
    float mv(int soundId) const {
        using namespace ffctl;
        const float v = prm(soundId);
        const int mi = _modIdxBySound[soundId];
        if (mi < 0) return v;
        const ModTarget& t = kModLayer[mi];
        if (t.kind != MODK_HOST) return v;
        // The depth before the terms: a face at noon is the knob, exactly --
        // modded() returns `knob` untouched for depth 0 -- and both terms are
        // pure, so building them first for a face at noon only spent cycles.
        const float depth = spkymod::depth_of(prm(t.depthId));
        if (depth == 0.f) return v;
        // t.part == 2 marks a center-column target: both decks mixed, so both
        // masters down means the center is still. Both readings are built the
        // same way -- the sum of two staircases is itself a staircase (spec
        // 2026-08-22 mod-sh-split §5), so the center needs no extra clock.
        //
        // PAN_B is the layer's one cross-deck row: it reads deck A's lane
        // through deck A's MASTER, negated, so the two PANs always open the
        // stereo image instead of walking the whole mix to one side (why, and
        // the measurement behind it: spkymod::mirror_term). Both readings are
        // mirrored, continuous and S&H alike -- mirroring only one would flip
        // the symmetry the moment the depth ring crosses noon.
        //
        // The exception is keyed off the soundId HERE rather than handed in by
        // the caller, for the same reason mv() lost its `part` parameter in
        // fix round 2: the row decides, never the call site. t.part stays 1
        // for that row, and truthfully so -- PAN_B's DEPTH is still deck B's
        // ring. Only the source moved. What it costs: MOD_B no longer reaches
        // PAN_B, so deck B's pan is switched off at its own depth ring.
        const bool mirror = (soundId == PAN_B);
        const float term = (t.part == 2)
            ? spkymod::center_term(_modMaster[0], _laneOut[0][t.slot],
                                   _modMaster[1], _laneOut[1][t.slot])
            : mirror
            ? spkymod::mirror_term(_modMaster[0], _laneOut[0][t.slot])
            : spkymod::lane_term(_modMaster[t.part], _laneOut[t.part][t.slot]);
        const float stepTerm = (t.part == 2)
            ? spkymod::center_term(_modMaster[0], _laneOutStepped[0][t.slot],
                                   _modMaster[1], _laneOutStepped[1][t.slot])
            : mirror
            ? spkymod::mirror_term(_modMaster[0], _laneOutStepped[0][t.slot])
            : spkymod::lane_term(_modMaster[t.part],
                                 _laneOutStepped[t.part][t.slot]);
        return spkymod::modded(v, depth,
                               term, stepTerm,
                               ffctl::kParamRange[soundId].lo, ffctl::kParamRange[soundId].hi);
    }
    // Strided twin of pp(). Only valid inside the part blocks, exactly like
    // pp() itself -- the appended pairs (COLOR/LINK/FILT/FLUX/FLUXFB/REV_MIX/
    // DEPTH/STAGES/PAN) must go through mv(p ? X_B : X_A). mvp() still takes
    // `part`: it needs it to build the strided soundId, same as pp() does.
    float mvp(int baseA, int part) const { return mv(baseA + part * ffctl::PART_STRIDE); }

    template <class H>
    Events _tick(const float* knobs, const Options& opt, Inst& inst, H& hooks) {
        using namespace ffctl;
        _k = knobs;
        Events ev;
        // Sample the modulation frame once per control tick (spec 2026-08-22
        // §3b), before any mv() read: one frozen frame per tick means deck A's
        // first knob and deck B's last knob see the same lane positions, and
        // the center's mix of both decks is taken at one instant.
        for (int p = 0; p < 2; ++p) {
            _modMaster[p] = pp(MOD_A, p);
            for (int s = 0; s < spky::LANE_COUNT; ++s) {
                _laneOut[p][s]        = inst.lane_output(p, s);
                _laneOutStepped[p][s] = inst.lane_output_stepped(p, s);
            }
        }

        // STEP entry latches the groove target immediately. Push the shared
        // amount before either deck sees its FLOW->STEP transition so both
        // decks latch the value from this same control update.
        inst.set_shuffle(prm(SHUFFLE));
        for (int p = 0; p < 2; ++p) {
            inst.set_rate(p, mvp(RATE_A, p));
            inst.set_shape(p, mvp(SHAPE_A, p));
            inst.set_density(p, mvp(DENSITY_A, p));
            inst.set_smooth(p, mvp(SMOOTH_A, p));
            inst.set_range(p, mvp(RANGE_A, p));
            // MOD is the per-deck master in both modes and is never itself
            // modulated (spec §2) -- a raw pp() read on purpose.
            inst.set_depth(p, pp(MOD_A, p));
            inst.set_tune(p, mvp(TUNE_A, p));

            inst.set_voice_attack(p, mvp(ATTACK_A, p));
            inst.set_voice_decay(p, mvp(DECAY_A, p));
            inst.set_voice_resonance(p, mvp(RES_A, p));
            // FILT is engine-backed (its depth writes _tdepth[LANE_SIZE] in
            // step 6 below), so the knob stays the raw trim it always was.
            inst.set_voice_filt(p, prm(p ? FILT_B : FILT_A));
            inst.set_color(p, mv(p ? COLOR_B : COLOR_A));
            inst.set_voice_sub(p, mvp(SUB_A, p));
            // Quadratic taper: the first ~20 ct is where the fine beating
            // lives, and a linear map would squeeze it into a fifth of the
            // travel now that the ceiling is 105 ct.
            //
            // Not on a FEED deck. There DETUNE means SPREAD and gets to the
            // engine as the LANE_SIZE base further down -- the sampler's
            // SUB -> LANE_SIZE re-point, one entry further down the same
            // ledger. It is passed RAW there, not squared: FEED owns its own
            // curve in feed_cfg's two-segment SPREAD map, and applying
            // DetuneQuantity's square on top would compress the single-digit
            // region the spec reserves for the lower half.
            if (inst.engine_id(p) != spky::ENGINE_FEED) {
                // The MOD-layer term lands in KNOB space, before the square
                // (spec §3b): modulating the mapped value would make the same
                // depth mean a different number of cents at every knob
                // position.
                const float detKnob = mvp(DETUNE_A, p);
                inst.set_voice_detune(p, detKnob * detKnob);
            }

            inst.set_flux_mix(p, pp(FLUX_A, p));
            inst.set_flux_rate(p, (int)std::lround(
                prm(p ? FLUXRATE_B : FLUXRATE_A)));
            inst.set_fx_target_base(p, spky::FXT_FLUX_FB,
                prm(p ? FLUXFB_B : FLUXFB_A));
            // The tape multiplier keeps its modulation sink but loses its knob:
            // 0.5 is the neutral multiplier (tape_time_mult(0.5) == 1), so CV
            // and the mod lanes still bend the tape while the panel does not.
            inst.set_fx_target_base(p, spky::FXT_FLUX_TIME, 0.5f);
            // Appended params are outside the stride, so pp() would compute the
            // wrong id — the explicit ternary is required (see FLUXRATE/FLUXFB).
            inst.set_link(p, mv(p ? LINK_B : LINK_A));
            // STAGES itself is pushed further down, alongside samplerPart's
            // analogous re-point gate -- it needs this tick's dispatched
            // engine_id(p), which set_engine (below) hasn't set yet here.
            // The FX blocks are gated by an explicit on/off (a pad on hardware,
            // a scenario action on the desktop). VCV has no such pad, so the mix
            // knob doubles as the on switch: knob up == engaged. At 0 the block
            // stays idle and the whole chain is skipped (bit-exact bypass).
            inst.set_fx_on(p, spky::FxBlock::Flux, pp(FLUX_A, p) > 1e-4f);
            // GRIT is bipolar now: "engaged" means the knob has cleared the
            // dead zone in either direction, not just a positive value --
            // the raw value alone would silently mute the whole CRSH
            // (negative) side (see kGritDead and this tick's grit block).
            inst.set_fx_on(p, spky::FxBlock::Grit,
                            std::fabs(pp(GRIT_A, p)) > kGritDead);
            // LVL/COMP: the lower zone is pure output gain (Comp::set_amount(0)
            // is a bit-exact bypass, so it costs no compressor CPU); the top
            // two fifths engage the compressor with make-up, ending at the 0.7
            // that used to be the knob's working value.
            //
            // Both the split and the shape are about loudness per degree of
            // travel. Comp::update_curve makes make-up strongly superlinear in
            // the amount (_makeup_db = -_thr_db * (1 - 1/ratio) * 0.9, with
            // ratio = 1 + 9a^2), so a LINEAR ramp across a narrow zone dumps
            // most of its gain into the last few degrees: at the old 0.8 split
            // the final tenth of the knob was worth +11.2 dB while the tenth
            // just below the seam was worth +1.2 dB. A tenfold step change in
            // sensitivity exactly where the hand crosses over reads as the
            // volume pulling away at the top, which is what it was doing.
            //
            // Widening the zone alone does not fix that -- the a^2 term simply
            // moves the same cliff to the right. kCompShape is the other half:
            // raising the zone position to 0.6 front-loads the amount so
            // make-up grows nearly LINEARLY IN dB across the zone (3.6..4.8 dB
            // per tenth of travel, against 5.3 then 11.2 before). The exponent
            // is fitted to update_curve's law above; change one and the other
            // stops being right.
            //
            // kCompTop stays 0.7: full travel still reaches the compressor
            // character the old knob was habitually parked at.
            static constexpr float kLvlCompSplit = 0.6f;
            static constexpr float kCompTop      = 0.7f;
            static constexpr float kCompShape    = 0.6f;
            // One face, one read: the MOD-layer term is applied once here, so
            // the gain leg and the compressor leg stay two halves of the same
            // knob travel rather than drifting apart under modulation.
            const float lvlKnob = mvp(COMP_A, p);
            inst.set_part_level(p, std::min(1.f, lvlKnob / kLvlCompSplit));
            inst.set_comp(p, lvlKnob <= kLvlCompSplit ? 0.f
                             : kCompTop * std::pow(
                                   (lvlKnob - kLvlCompSplit) /
                                   (1.f - kLvlCompSplit), kCompShape));

            // PAN goes through mv() so the MOD ring's host-computed term is
            // included; at boot the depth is 0 and mv() returns the knob by early
            // return, so this is bit-identical to pushing the raw param.
            //
            // Deck B's mirror is NOT applied here -- mv() recognises PAN_B by
            // its own soundId and swaps the lane source itself, so this call
            // site stays the same shape as every other one. See mv().
            //
            // mv(p ? PAN_B : PAN_A), NOT mvp(PAN_A, p): mvp() adds p * PART_STRIDE
            // and is only valid for params inside part_controls(). PAN is an
            // APPENDED pair, so its two ids are not a stride apart -- see the
            // comment on mvp itself, which names the appended pairs that
            // have to take this route.
            inst.set_pan(p, mv(p ? PAN_B : PAN_A));

            // Saved ENG meanings remain 0 = Synth and 1 = Sampler; 2 adds
            // Wave, 3 Body, 4 the BBD, 5 FEED. Each new engine needs its own
            // explicit arm here -- anything that isn't 0/2/3/4/5 still falls
            // through to Sampler (or the dev test tone), which is also why old
            // patches keep their exact meaning. The test tone stays a
            // Sampler-only override.
            const int eng = static_cast<int>(std::round(pp(ENGINE_A, p)));
            const spky::EngineId id =
                eng == 0 ? spky::ENGINE_SYNTH :
                eng == 2 ? spky::ENGINE_WAVE :
                eng == 3 ? spky::ENGINE_BODY :
                eng == 4 ? spky::ENGINE_BBD :
                eng == 5 ? spky::ENGINE_FEED :
                opt.deck[p].test_tone ? spky::ENGINE_TEST_TONE : spky::ENGINE_SAMPLER;
            inst.set_engine(p, id);

            // The excitation bus is patch state (design spec §6), pushed
            // every control tick like the other per-part settings below --
            // cheap, idempotent, and correct after a patch load without a
            // separate "apply on restore" path.
            inst.set_excitation_sources(p, opt.deck[p].excite_tape,
                                         opt.deck[p].excite_other_deck,
                                         opt.deck[p].excite_audio_in);

            // Host hook, after set_engine and the excitation bus: VCV's
            // factory autoload runs here (Fireflow.cpp, VcvHooks).
            hooks.after_engine(p, eng, inst);

            inst.sampler_speed_mode(p, opt.deck[p].tape_idx != 0);
            inst.sampler_reverse(p, opt.deck[p].reverse);
            inst.sampler_feedback(p, opt.deck[p].feedback);

            // REC is a latch, so its value IS the desired state -- an edge
            // trigger would miss a state restored from a saved patch. The
            // engine's set_recording is idempotent, and sampler_record flips
            // monitoring with it, so pushing every control tick is correct.
            // On a synth part REC is inert: ENG is the only mode selector.
            // NOT ppb(REC_A, p): REC is not part-strided (see the static_assert
            // block near the top of host/vcv/src/Fireflow.cpp, where the VCV
            // accessors live).
            const bool wantRec = prm(p ? REC_B : REC_A) > 0.5f
                                 && inst.engine_id(p) == spky::ENGINE_SAMPLER;
            if (wantRec != inst.sampler_is_recording(p)) {
                inst.sampler_record(p, wantRec);
                // path/factoryLoaded mean "the buffer still holds exactly
                // what that source provided" -- once recording starts, the
                // buffer no longer matches either source, so the part must
                // stop claiming one.
                if (wantRec) ev.rec_started[p] = true;
            }

            // --- sampler control surface (spec 2026-07-21 morphagene-controls) ---
            // Four knobs that do nothing in the sampler's FLOW cloud get a
            // job of their own. The param ids do not change, so no saved
            // patch moves; only what the knob means when ENG says Sampler.
            //
            // set_density above keeps firing unconditionally -- the "push to
            // both, let the inactive side ignore it" pattern the voice row
            // already uses. set_variation left that pattern when MELODY became
            // SCAN-only on a Sampler deck (spec 2026-08-03); it is pushed
            // below, behind the same samplerPart gate. DENS is the one knob
            // that genuinely does two things in sampler STEP mode: it still
            // thins the groove gate AND now sets grain overlap. Both meanings
            // now follow the same modulated read (fix round 2, spec §8): the
            // groove gate already went through mv()/mvp() above, and a raw
            // pp() here would let a DENS mod depth move the gate without
            // moving overlap, splitting one wreathed knob's face in two.
            const bool samplerPart = inst.engine_id(p) == spky::ENGINE_SAMPLER;
            inst.sampler_overlap(p, mvp(DENSITY_A, p));
            inst.set_target_base(p, spky::LANE_SOURCE, pp(SOURCE_A, p));

            // Ledger of every lane base this function re-points per engine, so
            // the next addition has one place to check itself against rather
            // than re-discovering the rule by breaking it a third time:
            //   - LANE_SIZE:  sampler (SUB_A -> GENE SIZE) and FEED
            //                 (DETUNE_A -> SPREAD), restored to 0.5f below
            //                 when the deck is neither.
            //   - LANE_PITCH: BBD-only (STAGES_A/B). Other engines retain
            //                 their existing base; this movement only rehomes
            //                 the preserved STAGES state while BBD is active.
            //   - LANE_MOTION: the DPTH knob's base, on every engine, since
            //                 2026-08-19 (no more FEED-only ternary). Before
            //                 2026-08-18 this host never wrote this base at
            //                 all, so the only thing that could reach
            //                 LANE_MOTION in Rack was MOD.
            const bool bbdPart = inst.engine_id(p) == spky::ENGINE_BBD;
            const bool feedPart = inst.engine_id(p) == spky::ENGINE_FEED;
            // STAGES is orphaned by movement 3 and becomes the LANE_PITCH base
            // on a BBD deck. Re-pointing a knob per engine is not new -- the
            // sampler already moves SUB_A to LANE_SIZE as GENE SIZE.
            //
            // STAGES_A/B are appended params (outside the stride, like
            // DRIVE/LINK above), so pp(STAGES_A, p) is wrong for Part B: it
            // would read params[STAGES_A + PART_STRIDE], which is not
            // STAGES_B (when this was written it was past the end of the
            // params array; the exact index has moved with the layout). The
            // explicit ternary is required, exactly as for DRIVE/LINK.
            //
            if (bbdPart)
                inst.set_target_base(p, spky::LANE_PITCH,
                    prm(p ? STAGES_B : STAGES_A));

            if (_bbdEdge[p].tick(bbdPart)) {
                // Entry into BBD, in practice a player-driven one (see
                // bbd_edge_state.hpp: a restore is re-armed and adopts its ENG
                // as the baseline; the one known exception is noted at
                // _bbdEdge above).
                //
                // FLUX defaults disengaged (spec 5.11). The BBD's output is
                // already six poles at 3600 Hz plus a loss pole breathing under
                // a compander, and its gappy repeats are its most distinctive
                // trait -- which a tape echo behind it fills in. The player can
                // add it back; the default should not be darker-and-smeared.
                // The host does the write: VCV drops its FLUX knob to 0.
                // The silence trap's first half (spec 5.12): a BBD deck with no
                // source selected is an FX unit wired to nothing. Default the
                // neighbouring deck ON. Audio-in already reaches process_in
                // unconditionally through Part::process; what the checkbox gates
                // is the cross-deck bus (movement 1, Part::_src_deck), and that
                // is what makes resampling work without external cabling.
                // The host does this too: VCV sets its exciteOtherDeck.
                ev.bbd_edge[p] = true;
            }

            // SCAN nur fuer Sampler-Parts (K-03). Der urspruengliche Grund --
            // set_scan -> scan_rate enthielt im unteren Zweig ein std::pow,
            // und bei ctrlDiv = 16 waren das bis zu 6000 Aufrufe/s im
            // Audio-Callback fuer eine Engine, die niemand hoert -- ist mit
            // der linearen Kurve (spec 2026-07-23 sampler-performance-fixes)
            // weg: scan_rate() ruft kein pow mehr auf. Das Gate bleibt
            // trotzdem, jetzt aus demselben Grund wie beim sampler-only SIZE-
            // Routing weiter unten: SCAN treibt ein sampler-eigenes Stueck
            // Zustand (_scan_rate), das ein Synth-Deck nie liest, und es dort
            // unbedingt zu schreiben waere nur Arbeit ohne Wirkung. Das ist
            // ein Konsistenz-, kein Kosten-Argument mehr.
            //
            // Kein Soft-Takeover hier, und das ist eine Entscheidung, keine
            // Luecke. Der Review vom 2026-07-22 meldete als F-07, dass der
            // erste ENG-Flip den Lesekopf sofort losrasen laesst: MELO traegt
            // im Synth VARIATION, steht im Init-Patch an den Extremen
            // (-0.728 und -1.0), und als SCAN gelesen sind das jetzt -0.97x
            // und -4x Realtime rueckwaerts -- mit dem neuen Maximum naeher an
            // Realtime, nicht weiter davon weg. Das stimmt -- aber es ist
            // genau das Verhalten, das README.md unter "Known limitations"
            // ausdruecklich waehlt: die Knopfposition gilt ueber den
            // Engine-Wechsel hinweg, ohne getrenntes Gedaechtnis und ohne
            // Soft-Takeover, weil die Hardware kein Soft-Takeover hat und
            // beide Seiten dasselbe tun sollen. Eine Sperre einzubauen hiesse,
            // diese Linie zu verlassen -- und sie ueber Patch-Laden hinweg
            // dicht zu bekommen verlangt genau das persistente Gedaechtnis,
            // das dort ausgeschlossen ist. Offen fuer den Autor des
            // Instruments, nicht fuer die Engine.
            //
            // MELODY is one knob with one meaning per engine (spec 2026-08-03
            // vcv-engine-aware-captions): VARY off the Sampler, SCAN on it.
            // Both jobs at once is why SCAN had to be printed permanently
            // beside MELO. Variation parks at 0 (LOOP) here, the same shape
            // as the LANE_SIZE gate below, which parks at 0.5f off the
            // Sampler. The cost is deliberate and recorded in the spec: a
            // Sampler deck no longer renews its phrases on its own, and NEW
            // is the gesture that asks for a fresh pair.
            inst.set_variation(p, samplerPart ? 0.f : mvp(MELODY_A, p));
            if (samplerPart) inst.sampler_scan(p, mvp(MELODY_A, p));

            // GENE SIZE rides the lane base in the sampler, SPREAD in FEED.
            // The else branch is load-bearing -- a base left behind on an
            // engine flip would silently stick.
            //
            // Both re-pointed reads go through mv() too: a conditional face
            // follows its FACE, not its engine wiring (spec §4, last
            // paragraph). SUB is a modulated face on a synth deck, so it stays
            // one on a sampler deck even though the value now lands on a lane
            // base -- same for DTUN on FEED.
            if (samplerPart) {
                inst.set_target_base(p, spky::LANE_SIZE,   mvp(SUB_A, p));
            } else if (feedPart) {
                inst.set_target_base(p, spky::LANE_SIZE,   mvp(DETUNE_A, p));
            } else {
                inst.set_target_base(p, spky::LANE_SIZE,   0.5f);
            }

            // DPTH writes LANE_MOTION's base on every engine, because every
            // engine reads that lane: width (and drift) on SYNTH/WAVE, drift
            // alone on BODY, scatter on the sampler, the feedback amount on
            // the BBD, the FM index on FEED. This host never wrote the base at
            // all until 2026-08-18, so all six had a control whose ends the
            // player could not reach; FEED got the repair first, through a
            // ternary that pinned the other five to Part's compiled-in 0.5.
            // The knob's init default IS that 0.5 (and IS feed_cfg::kDepthBase),
            // so an untouched patch writes exactly what the ternary wrote --
            // the sampler excepted, which halves the base (sampler_config.h).
            //
            // NOT pp(DEPTH_A, p). DEPTH_A/B are APPENDED ids (69/70), not a
            // part-strided pair, so pp() computed params[69 + 20] = params[89]
            // for deck B -- measured 2026-08-22. Before the MOD layer appended
            // its 49 params that index was past the end of the params vector
            // (undefined); after, it silently aliased MODD_DENSITY_B, so
            // raising deck B's DENS mod depth would have driven deck B's
            // LANE_MOTION base. That is the hazard the static_assert block at
            // the top of host/vcv/src/Fireflow.cpp calls "UPGRADED, not gone", and it is now
            // guarded mechanically by res/test_panel.py's
            // strided_accessor_issues(), which derives the legal pp() bases
            // from the generator. Explicit ternary, exactly as REC/STAGES/
            // LINK/COLOR do three lines up.
            //
            // Consequence, stated so nobody has to rediscover it: deck B's
            // LANE_MOTION base at init moves from 0.0 (MODD_DENSITY_B's
            // default, read by accident) to 0.5 (DEPTH_B's own default).
            // Deck B's init sound changes and a listening pass is owed.
            inst.set_target_base(p, spky::LANE_MOTION,
                                 prm(p ? DEPTH_B : DEPTH_A));

            // Stable pitch in the sampler: the lane still FIRES (that is what
            // keeps STEP triggering alive -- Part::process reads the fire as
            // _mod.lane_fired(LANE_PITCH), part.h:258, while _active gates
            // modulation only, part.cpp:101), it just stops moving the pitch.
            // Sample material and a synth deck can then sit in the same key.
            inst.set_target_active(p, spky::LANE_PITCH, !samplerPart);

            // GRIT is one bipolar knob: sign is the mode, magnitude the mix.
            // The dead zone exists because a 9 mm pot on an ADC cannot hit an
            // exact zero -- without it "off" would be unreachable on hardware.
            const float gritKnob = prm(p ? GRIT_B : GRIT_A);
            inst.set_grit_mode(p, gritKnob < 0.f ? spky::GritMode::Reduce
                                                 : spky::GritMode::Drive);
            const float gritMag = std::fabs(gritKnob);
            inst.set_grit_mix(p, gritMag <= kGritDead ? 0.f
                                 : (gritMag - kGritDead) / (1.f - kGritDead));
            const int steps = (int)std::round(pp(STEPS_A, p));
            inst.set_step(p, steps > 0, steps);

            // SONG walks a curated 14-rung ladder through (Principle, SongMode)
            // (spec 2026-08-09 hw-control-reduction task 3) -- FORM and the NEW
            // pad are gone. songRung[p].tick() debounces the pot (so a value
            // parked on a seam does not re-quantise every tick) AND absorbs a
            // RESTORED rung as a baseline rather than a turn (song_rung_state.hpp)
            // -- see on_reset()/on_restore(), called from Fireflow.cpp's
            // onReset()/dataFromJson() and the firmware's control_boot(). A
            // rung change re-rolls the phrase exactly as the retired NEW pad
            // used to, and in the sampler additionally punches a fresh grain
            // -- the playhead returns to ORGANIZE and a grain spawns
            // immediately, without which the long end of GENE SIZE is
            // unplayable.
            const float songNorm = pp(SONG_A, p) /
                                   float(spky::kSongLadderCount - 1);
            if (_songRung[p].tick(songNorm, spky::kSongLadderCount)) {
                inst.new_phrase(p);          // turn the knob, get a new melody
                // Fires once per rung detent; inherited the retired NEW
                // pad's Sampler punch. Whether every detent should punch, or
                // only some, is still an open by-ear question -- on this
                // plan's listening checklist.
                if (samplerPart) inst.sampler_punch(p);
            }
            const spky::SongRung& r = spky::song_ladder_at(_songRung[p].rung);
            inst.set_form(p, r.form);
            inst.set_song(p, r.song);
        }

        // Engine-backed mod depths (spec 2026-08-22 §3a): TIMB/DPTH/FILT write
        // the Part's own _tdepth slots, MIX/FB/SEND the FX row -- active iff
        // the depth is off noon. Nothing else in this host writes those slots,
        // so this loop is their sole owner. The init snapshot repeats back the
        // KNOB POSITIONS, which since the bipolar split are the pre-images
        // (1.0 / 0.712 / 0.568 and three zeroes); through depth_of they reach
        // the engine as the booted depths 1.0 / 0.7 / 0.55 and three zeroes.
        //
        // The engine already multiplies its own master MOD into the texture
        // lanes, so no modMaster factor appears here -- that is the whole
        // reason these six faces do NOT take the host-computed path.
        for (const auto& t : kModLayer) {
            // The kind before depth_of: 38 of the 50 rows are host-computed
            // faces whose depth mv() reads, and computing it here for them
            // was dead work (out of line at -Os, so the compiler kept it).
            if (t.kind != MODK_TDEPTH && t.kind != MODK_FXDEPTH) continue;
            // Through depth_of, not raw: noon needs its dead zone here too,
            // and a negative depth is what tells Part to read the lane's S&H
            // twin (spec 2026-08-22 mod-sh-split §4).
            const float d = spkymod::depth_of(prm(t.depthId));
            if (t.kind == MODK_TDEPTH) {
                inst.set_target_depth(t.part, t.slot, d);
            } else if (t.kind == MODK_FXDEPTH) {
                inst.set_fx_target_depth(t.part, t.slot, d);
                // Active on EITHER side of noon now -- the old `d > 0.f`
                // would have left every S&H FX target pinned to its base.
                inst.set_fx_target_active(t.part, t.slot, d != 0.f);
            }
        }

        inst.set_morph(mv(MORPH));
        // COUPLE runs both worlds on one axis (kCoupleZoneSplit, declared
        // above). Below the split SYNC is off and couple drives the
        // Kuramoto lock; at or above it SYNC is on and couple sets how
        // tightly the texture lanes follow. Each half sweeps 0..1, so the
        // grid world keeps its full spread -- "on the grid but breathing"
        // is a real state and must stay reachable.
        const float coupleKnob = prm(COUPLE);
        const bool  grid = coupleKnob >= kCoupleZoneSplit;
        inst.set_sync(grid);
        inst.set_couple(grid
            ? (coupleKnob - kCoupleZoneSplit) / (1.f - kCoupleZoneSplit)
            : coupleKnob / kCoupleZoneSplit);
        // The left stop IS the old SETL pad: Center::settle() is drift_target = 0
        // plus a ~1 s glide of EVOLVE and kick, so the button always lived at
        // the end of this axis. Edge-triggered via driftSettled -- a knob
        // parked at the stop must not re-fire the glide on every control
        // tick, and a patch that RESTORES with DRIFT already parked there
        // must not panic on the very first tick either (drift_settle_state.hpp).
        static constexpr float kDriftSettleZone = 0.02f;
        const float driftKnob = prm(DRIFT);
        const bool  driftInZone = driftKnob <= kDriftSettleZone;
        if (_driftSettled.tick(driftInZone)) inst.settle();
        inst.set_drift(driftInZone
            ? 0.f
            : (driftKnob - kDriftSettleZone) / (1.f - kDriftSettleZone));
        inst.set_tide(mv(TIDE));
        inst.set_choke(prm(CHOKE));   // continuous -1..+1, engine quantises zones
        inst.set_pull(prm(PULL));     // continuous -1..+1, engine holds the dead zone
        // The room's four shape knobs are center targets: mixed from both
        // decks' SIZE lanes (mv() takes the center branch on t.part == 2), so
        // the reverb breathes with whichever deck is actually moving. SEND is
        // NOT here -- it is per-deck and engine-backed (step 6 above).
        inst.set_reverb_size(mv(REV_SIZE));
        inst.set_reverb_decay(mv(REV_DECAY));
        inst.set_reverb_tone(mv(REV_TONE));
        inst.set_reverb_diffusion(mv(REV_DIFF));
        inst.set_reverb_mix(spky::PART_A, prm(REV_MIX_A));
        inst.set_reverb_mix(spky::PART_B, prm(REV_MIX_B));
        // Fixed by ear (spec 2026-08-09 hw-control-reduction task 9): PUSH
        // sat at 0.40 in every patch, and once the limiter rides, DRIVE
        // stops controlling dirt anyway. SMEAR ("smear ... 0.3 sowas") and
        // WOBL/MOD ("wobbel fest auf .1 - .2") are the same kind of decision
        // -- the owner never moved them either. The engine API (set_master_
        // drive/set_reverb_smear/set_reverb_mod) is unchanged so the render
        // host and its scenarios can still drive them.
        inst.set_master_drive(0.40f);
        inst.set_reverb_smear(0.30f);
        inst.set_reverb_mod(0.15f);
        inst.set_scale((int)std::round(prm(SCALE)));

        // Tempo: an external clock (one pulse per beat) overrides the knob.
        float bpm = 40.f + prm(TEMPO) * 200.f;
        if (opt.measured_bpm >= 20.f && opt.measured_bpm <= 400.f) bpm = opt.measured_bpm;
        inst.set_tempo_bpm(bpm);
        inst.set_pace(prm(PACE));
        return ev;
    }
};

using ControlLaw = ControlLawT<spky::Instrument>;

} // namespace control
