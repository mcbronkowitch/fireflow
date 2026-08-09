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
        // that the ceiling is 105 ct. Mirrors Fireflow.cpp pushParams.
        const float detKnob = part(DETUNE_A, deck);
        inst.set_voice_detune(deck, detKnob * detKnob);

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
        // is a bit-exact bypass, so it costs no compressor CPU); the top
        // fifth engages the compressor with make-up, ending at the 0.7 that
        // used to be the knob's working value. Mirrors Fireflow.cpp's
        // kLvlCompSplit/kCompTop/pushParams LVL/COMP block.
        static constexpr float kLvlCompSplit = 0.8f;
        static constexpr float kCompTop      = 0.7f;
        const float lvlKnob = part(COMP_A, deck);
        inst.set_part_level(deck, std::min(1.f, lvlKnob / kLvlCompSplit));
        inst.set_comp(deck, lvlKnob <= kLvlCompSplit ? 0.f
                             : (lvlKnob - kLvlCompSplit) /
                               (1.f - kLvlCompSplit) * kCompTop);

        inst.sampler_speed_mode(deck, true);
        inst.sampler_reverse(deck, false);
        inst.sampler_feedback(deck, 0.95f);
        inst.sampler_overlap(deck, part(DENSITY_A, deck));
        inst.set_target_base(
            deck, spky::LANE_SOURCE, part(SOURCE_A, deck));
        if(sampler)
            inst.sampler_scan(deck, part(MELODY_A, deck));
        inst.set_target_base(
            deck,
            spky::LANE_SIZE,
            sampler ? part(SUB_A, deck) : 0.5f);
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
}

void apply_init_patch(spky::Instrument& inst)
{
    apply_init_patch(inst, spkyvcv::kInitParamDefaults);
}

}  // namespace audition
