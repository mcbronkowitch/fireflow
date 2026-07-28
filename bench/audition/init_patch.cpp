#include "init_patch.h"

#include <cmath>

#include "instrument.h"
#include "mod/divisions.h"
#include "../../host/vcv/src/generated_panel.hpp"
#include "../../host/vcv/src/init_patch.hpp"


namespace audition {

void apply_init_patch(spky::Instrument& inst)
{
    using namespace spkyvcv;
    auto value = [](int id) { return initParamDefault(id); };
    auto part = [&](int base, int deck) {
        return value(base + deck * PART_STRIDE);
    };

    inst.set_shuffle(value(SHUFFLE));
    for(int deck = 0; deck < spky::PART_COUNT; ++deck)
    {
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
        inst.set_voice_detune(deck, value(deck ? DETUNE_B : DETUNE_A));

        inst.set_flux_mix(deck, part(FLUX_A, deck));
        inst.set_flux_rate(
            deck,
            spky::flux_division_index(value(deck ? FLUXRATE_B : FLUXRATE_A)));
        inst.set_fx_target_base(
            deck,
            spky::FXT_FLUX_FB,
            value(deck ? FLUXFB_B : FLUXFB_A));
        inst.set_grit_mix(deck, part(GRIT_A, deck));
        inst.set_drive(deck, value(deck ? DRIVE_B : DRIVE_A));
        inst.set_stages(deck, value(deck ? STAGES_B : STAGES_A));
        inst.set_drag(deck, value(deck ? DRAG_B : DRAG_A));
        inst.set_fx_on(
            deck, spky::FxBlock::Flux, part(FLUX_A, deck) > 1e-4f);
        inst.set_fx_on(
            deck, spky::FxBlock::Grit, part(GRIT_A, deck) > 1e-4f);
        inst.set_comp(deck, part(COMP_A, deck));

        const int engine_value
            = static_cast<int>(std::lround(part(ENGINE_A, deck)));
        const spky::EngineId engine
            = engine_value == 0 ? spky::ENGINE_SYNTH
              : engine_value == 2 ? spky::ENGINE_WAVE
                                  : spky::ENGINE_SAMPLER;
        inst.set_engine(deck, engine);
        const bool sampler = engine == spky::ENGINE_SAMPLER;

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

        inst.set_grit_mode(
            deck,
            part(GRITMODE_A, deck) > 0.5f ? spky::GritMode::Reduce
                                          : spky::GritMode::Drive);
        inst.set_step(
            deck,
            part(STEP_A, deck) > 0.5f,
            static_cast<int>(std::lround(part(STEPS_A, deck))));
        inst.set_form(
            deck, static_cast<int>(std::lround(part(FORM_A, deck))));
        inst.set_song(
            deck, static_cast<int>(std::lround(part(SONG_A, deck))));
    }

    inst.set_morph(value(MORPH));
    inst.set_couple(value(COUPLE));
    inst.set_drift(value(DRIFT));
    inst.set_tide(value(TIDE));
    inst.set_sync(value(SYNC) > 0.5f);
    inst.set_choke(value(CHOKE) * 0.5f);
    inst.set_reverb_size(value(REV_SIZE));
    inst.set_reverb_decay(value(REV_DECAY));
    inst.set_reverb_tone(value(REV_TONE));
    inst.set_reverb_diffusion(value(REV_DIFF));
    inst.set_reverb_mix(spky::PART_A, value(REV_MIX_A));
    inst.set_reverb_mix(spky::PART_B, value(REV_MIX_B));
    inst.set_reverb_smear(value(REV_SMEAR));
    inst.set_reverb_mod(value(REV_MOD));
    inst.set_master_drive(value(MASTER_DRIVE));
    inst.set_scale(static_cast<int>(std::lround(value(SCALE))));
    inst.set_tempo_bpm(40.f + value(TEMPO) * 200.f);
}

}  // namespace audition
