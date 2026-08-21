#include "init_patch.h"

#include <algorithm>
#include <cmath>

#include "instrument.h"
#include "mod/divisions.h"
#include "mod/song_ladder.h"
#include "../../host/vcv/src/generated_panel.hpp"
#include "../../host/vcv/src/init_patch.hpp"


namespace {

void apply_engine_stages(spky::Instrument& inst, int deck,
                         spky::EngineId engine, float value)
{
    if(engine == spky::ENGINE_BBD)
        inst.set_target_base(deck, spky::LANE_PITCH, value);
}

// GRIT is one bipolar knob (spec 2026-08-09 hw-control-reduction task 4):
// sign picks the mode, magnitude is the mix. The dead zone exists because a
// 9 mm pot on an ADC cannot hit an exact zero -- without it "off" would be
// unreachable on hardware. Mirrors Fireflow.cpp's kGritDead/pushParams.
constexpr float kGritDead = 0.03f;

}  // namespace

namespace audition {

void apply_init_patch(spky::Instrument& inst, const float* values)
{
    using namespace spkyvcv;
    auto value = [values](int id) { return values[id]; };
    auto part = [&](int base, int deck) {
        return value(base + deck * PART_STRIDE);
    };

    inst.set_shuffle(value(SHUFFLE));
    for(int deck = 0; deck < spky::PART_COUNT; ++deck)
    {
        const int engine_value
            = static_cast<int>(std::lround(part(ENGINE_A, deck)));
        const spky::EngineId engine
            = engine_value == 0 ? spky::ENGINE_SYNTH
              : engine_value == 2 ? spky::ENGINE_WAVE
              : engine_value == 3 ? spky::ENGINE_BODY
              : engine_value == 4 ? spky::ENGINE_BBD
              : engine_value == 5 ? spky::ENGINE_FEED
                                  : spky::ENGINE_SAMPLER;
        inst.set_engine(deck, engine);
        const bool sampler = engine == spky::ENGINE_SAMPLER;

        inst.set_rate(deck, part(RATE_A, deck));
        inst.set_shape(deck, part(SHAPE_A, deck));
        inst.set_density(deck, part(DENSITY_A, deck));
        inst.set_smooth(deck, part(SMOOTH_A, deck));
        inst.set_range(deck, part(RANGE_A, deck));
        inst.set_variation(deck, part(MELODY_A, deck));
        inst.set_depth(deck, part(MOD_A, deck));
        inst.set_tune(deck, part(TUNE_A, deck));

        inst.set_voice_attack(deck, part(ATTACK_A, deck));
        inst.set_voice_decay(deck, part(DECAY_A, deck));
        inst.set_voice_resonance(deck, part(RES_A, deck));
        inst.set_voice_filt(deck, value(deck ? FILT_B : FILT_A));
        inst.set_color(deck, value(deck ? COLOR_B : COLOR_A));
        inst.set_voice_sub(deck, part(SUB_A, deck));
        // Quadratic taper: the first ~20 ct is where the fine beating lives,
        // and a linear map would squeeze it into a fifth of the travel now
        // that the ceiling is 105 ct. Mirrors Fireflow.cpp pushParams --
        // including its FEED gate: on a FEED deck this knob is SPREAD and
        // reaches the engine as the LANE_SIZE base below, raw rather than
        // squared, so pushing it as detune as well would be a second, wrong
        // job for the same number.
        if(engine != spky::ENGINE_FEED)
        {
            const float detKnob = part(DETUNE_A, deck);
            inst.set_voice_detune(deck, detKnob * detKnob);
        }

        inst.set_flux_mix(deck, part(FLUX_A, deck));
        // TIME's knob value IS the raw division index now (task 6, spec
        // 2026-08-09 hw-control-reduction) -- no more flux_division_index()
        // round-trip through a 0..1 float, mirroring Fireflow.cpp pushParams.
        inst.set_flux_rate(
            deck,
            static_cast<int>(
                std::lround(value(deck ? FLUXRATE_B : FLUXRATE_A))));
        inst.set_fx_target_base(
            deck,
            spky::FXT_FLUX_FB,
            value(deck ? FLUXFB_B : FLUXFB_A));
        apply_engine_stages(
            inst, deck, engine, value(deck ? STAGES_B : STAGES_A));
        inst.set_link(deck, value(deck ? LINK_B : LINK_A));
        inst.set_fx_on(
            deck, spky::FxBlock::Flux, part(FLUX_A, deck) > 1e-4f);
        // Bipolar: engaged whenever the magnitude clears the dead zone in
        // either direction -- the raw value alone would silently mute the
        // whole CRSH (negative) side.
        inst.set_fx_on(
            deck, spky::FxBlock::Grit,
            std::fabs(part(GRIT_A, deck)) > kGritDead);
        // LVL/COMP: the lower zone is pure output gain (Comp::set_amount(0)
        // is a bit-exact bypass, so it costs no compressor CPU); the top two
        // fifths engage the compressor with make-up, ending at the 0.7 that
        // used to be the knob's working value. kCompShape front-loads the
        // amount so make-up rises evenly in dB instead of dumping half its
        // range into the last tenth of travel -- Fireflow.cpp's copy carries
        // the full derivation. Mirrors Fireflow.cpp's
        // kLvlCompSplit/kCompTop/kCompShape/pushParams LVL/COMP block.
        static constexpr float kLvlCompSplit = 0.6f;
        static constexpr float kCompTop      = 0.7f;
        static constexpr float kCompShape    = 0.6f;
        const float lvlKnob = part(COMP_A, deck);
        inst.set_part_level(deck, std::min(1.f, lvlKnob / kLvlCompSplit));
        inst.set_comp(deck, lvlKnob <= kLvlCompSplit ? 0.f
                             : kCompTop * std::pow(
                                   (lvlKnob - kLvlCompSplit) /
                                   (1.f - kLvlCompSplit), kCompShape));

        inst.sampler_speed_mode(deck, true);
        inst.sampler_reverse(deck, false);
        inst.sampler_feedback(deck, 0.95f);
        inst.sampler_overlap(deck, part(DENSITY_A, deck));
        inst.set_target_base(
            deck, spky::LANE_SOURCE, part(SOURCE_A, deck));
        if(sampler)
            inst.sampler_scan(deck, part(MELODY_A, deck));
        // GENE SIZE rides the lane base in the sampler, SPREAD in FEED, and
        // the else branch parks it at Part's compiled-in 0.5 everywhere else.
        // Mirrors Fireflow.cpp pushParams' samplerPart/feedPart ternary. The
        // FEED arm was missing here until 2026-08-21 and only became audible
        // with FM-INIT.vcvm, the first factory patch to boot a FEED deck: this
        // function was writing 0.5 where the host writes DETUNE_A.
        const bool feed = engine == spky::ENGINE_FEED;
        inst.set_target_base(
            deck,
            spky::LANE_SIZE,
            sampler ? part(SUB_A, deck)
                    : feed ? part(DETUNE_A, deck) : 0.5f);
        // DPTH is the LANE_MOTION base on every engine (spec 2026-08-19
        // voice-knobs-dpth-edge) -- FM index on FEED, width+drift on
        // SYNTH/WAVE, drift alone on BODY, scatter on the sampler, feedback
        // amount on the BBD. Same omission and the same date: while every
        // factory patch booted DPTH at Part's compiled-in 0.5, not writing it
        // and writing it were the same thing.
        inst.set_target_base(
            deck, spky::LANE_MOTION, value(deck ? DEPTH_B : DEPTH_A));
        inst.set_target_active(deck, spky::LANE_PITCH, !sampler);

        // GRIT is one bipolar knob: sign is the mode, magnitude the mix.
        {
            const float gritKnob = part(GRIT_A, deck);
            inst.set_grit_mode(
                deck,
                gritKnob < 0.f ? spky::GritMode::Reduce
                               : spky::GritMode::Drive);
            const float gritMag = std::fabs(gritKnob);
            inst.set_grit_mix(
                deck,
                gritMag <= kGritDead
                    ? 0.f
                    : (gritMag - kGritDead) / (1.f - kGritDead));
        }
        {
            const int steps = static_cast<int>(std::lround(part(STEPS_A, deck)));
            inst.set_step(deck, steps > 0, steps);
        }
        {
            // SONG absorbed FORM (spec 2026-08-09 hw-control-reduction task
            // 3): the snapshot's SONG_A/B value is now a ladder rung index,
            // not a raw SongMode.
            const int rung = static_cast<int>(std::lround(part(SONG_A, deck)));
            const spky::SongRung& r = spky::song_ladder_at(rung);
            inst.set_form(deck, r.form);
            inst.set_song(deck, r.song);
        }
    }

    inst.set_morph(value(MORPH));
    // COUPLE runs both worlds on one axis; mirrors Fireflow.cpp pushParams
    // (spec 2026-08-09 hw-control-reduction task 7). Below the split SYNC
    // is off and couple drives the Kuramoto lock; at or above it SYNC is on
    // and couple sets how tightly the texture lanes follow.
    static constexpr float kCoupleZoneSplit = 0.5f;
    const float coupleKnob = value(COUPLE);
    const bool  grid = coupleKnob >= kCoupleZoneSplit;
    inst.set_sync(grid);
    inst.set_couple(grid
        ? (coupleKnob - kCoupleZoneSplit) / (1.f - kCoupleZoneSplit)
        : coupleKnob / kCoupleZoneSplit);
    // DRIFT's left stop is the old SETL pad (kDriftSettleZone, task 8, spec
    // 2026-08-09 hw-control-reduction); mirrors Fireflow.cpp pushParams'
    // zone mapping. Deliberately does NOT call inst.settle(): this function
    // applies a snapshot once, with no previous tick to compare against, so
    // there is no edge to detect -- Fireflow.cpp's driftSettled exists
    // specifically to suppress settle() on a one-shot restore that lands in
    // the zone (drift_settle_state.hpp), which is exactly the situation
    // every call here already is. Firing settle() unconditionally would be
    // the bug that type exists to prevent, not a mirror of correct host
    // behavior; SPOT/SETTLE were never one-shot-fired here either, for the
    // same reason.
    static constexpr float kDriftSettleZone = 0.02f;
    const float driftKnob = value(DRIFT);
    inst.set_drift(driftKnob <= kDriftSettleZone
        ? 0.f
        : (driftKnob - kDriftSettleZone) / (1.f - kDriftSettleZone));
    inst.set_tide(value(TIDE));
    inst.set_choke(value(CHOKE) * 0.5f);
    inst.set_reverb_size(value(REV_SIZE));
    inst.set_reverb_decay(value(REV_DECAY));
    inst.set_reverb_tone(value(REV_TONE));
    inst.set_reverb_diffusion(value(REV_DIFF));
    inst.set_reverb_mix(spky::PART_A, value(REV_MIX_A));
    inst.set_reverb_mix(spky::PART_B, value(REV_MIX_B));
    // Fixed by ear (spec 2026-08-09 hw-control-reduction task 9); mirrors
    // Fireflow.cpp pushParams' PUSH/SMEAR/WOBL constants.
    inst.set_master_drive(0.40f);
    inst.set_reverb_smear(0.30f);
    inst.set_reverb_mod(0.15f);
    inst.set_scale(static_cast<int>(std::lround(value(SCALE))));
    inst.set_tempo_bpm(40.f + value(TEMPO) * 200.f);
    inst.set_pace(value(PACE));
}

void apply_init_patch(spky::Instrument& inst)
{
    apply_init_patch(inst, spkyvcv::kInitParamDefaults);
}

}  // namespace audition
