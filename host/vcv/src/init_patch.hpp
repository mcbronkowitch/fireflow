#pragma once

namespace spkyvcv {

// Panel snapshot from sampler.vcvm (2026-07-24), in stable ParamId order.
static constexpr float kInitParamDefaults[] = {
     0.204668641f, // RATE_A
     0.615999997f, // SHAPE_A
     0.695181072f, // DENSITY_A
     1.000000000f, // SMOOTH_A
     0.844060600f, // RANGE_A
    -0.768963635f, // MELODY_A
     0.603614450f, // MOD_A
     0.500000000f, // TUNE_A
     0.000000000f, // ATTACK_A
     0.322666585f, // DECAY_A
     0.319000006f, // RES_A
     0.458666444f, // SUB_A
     0.500000000f, // SOURCE_A
     0.677333534f, // FLUX_A
     0.000000000f, // GRIT_A
     0.629666805f, // COMP_A
    16.000000000f, // STEPS_A
     0.000000000f, // ENGINE_A
     1.000000000f, // GRITMODE_A
     1.000000000f, // STEP_A
     0.000000000f, // PRINCIPLE_A
     0.000000000f, // NEWPHRASE_A
     0.000000000f, // TRIGGER_A
     0.186747193f, // RATE_B
     0.600000024f, // SHAPE_B
     0.319397628f, // DENSITY_B
     0.300000012f, // SMOOTH_B
     0.261445761f, // RANGE_B
    -0.691566467f, // MELODY_B
     0.344578236f, // MOD_B
     0.500000000f, // TUNE_B
     0.000000000f, // ATTACK_B
     0.450666636f, // DECAY_B
     0.379000008f, // RES_B
     0.400000000f, // SUB_B
     0.500000000f, // SOURCE_B
     0.462666422f, // FLUX_B
     0.057000086f, // GRIT_B
     0.710999966f, // COMP_B
     8.000000000f, // STEPS_B
     1.000000000f, // ENGINE_B
     0.000000000f, // GRITMODE_B
     1.000000000f, // STEP_B
     0.000000000f, // PRINCIPLE_B
     0.000000000f, // NEWPHRASE_B
     0.000000000f, // TRIGGER_B
     0.492770702f, // MORPH
     1.000000000f, // SYNC
     0.500000000f, // TEMPO
     1.000000000f, // COUPLE
     4.000000000f, // SCALE
     0.000000000f, // DRIFT
     0.000000000f, // SPOT
     0.790666699f, // MASTER_DRIVE
     0.000000000f, // SETTLE
     0.642665863f, // REV_SIZE
     0.663998663f, // REV_DECAY
     0.761333108f, // REV_TONE
     0.862999976f, // REV_DIFF
     0.484000504f, // REV_SMEAR
     0.237000003f, // REV_MOD
     0.000000000f, // CHOKE
     0.064333424f, // FILT_A
    -0.246000007f, // FILT_B
     0.500000000f, // TIDE
     0.392727494f, // FLUXRATE_A
     0.363636374f, // FLUXRATE_B
     0.285667986f, // FLUXFB_A
     0.439336449f, // FLUXFB_B
     0.000000000f, // COLOR_A
     0.000000000f, // COLOR_B
     1.000000000f, // DUST_A
     1.000000000f, // DUST_B
     1.000000000f, // ROT_A
     1.000000000f, // ROT_B
     0.000000000f, // REC_A
     0.000000000f, // REC_B
     0.430665255f, // REV_MIX_A
     0.212000355f, // REV_MIX_B
     1.000000000f, // SHUFFLE
     0.171428576f, // DETUNE_A = 6 / 35
     0.171428576f, // DETUNE_B = 6 / 35
};

static_assert(sizeof(kInitParamDefaults) / sizeof(kInitParamDefaults[0])
                  == NUM_PARAMS,
              "init snapshot must contain one value for every ParamId");

static constexpr int kInitPrinciple[] = {2, 0};
static_assert(sizeof(kInitPrinciple) / sizeof(kInitPrinciple[0]) == 2,
              "init principle must contain one value per part");

inline float initParamDefault(int id) {
    return kInitParamDefaults[id];
}

} // namespace spkyvcv
