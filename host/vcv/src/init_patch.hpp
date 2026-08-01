#pragma once

namespace spkyvcv {

// Panel snapshot from drone.vcvm (2026-07-28), in stable ParamId order.
//
// It replaced the sampler.vcvm snapshot of 2026-07-24 wholesale: 46 of the 84
// values moved. Two were NOT taken from the file --
//
//   LINK_B  the preset had 0.002664 from its former bipolar travel. Zeroed.
//           LINK_A was already 0. Both remain at 0, now the neutral end of the
//           unipolar THIN control rather than the centre of a bipolar control.
//
//   STAGES_B  the preset's 1.0 IS wanted here (user, 2026-07-28) and was taken.
//           An earlier note in this file warned against restoring exactly that
//           value, because 1.0 was also the pre-BBD-redesign ROT default and a
//           re-bake could bring it back by accident. That warning has now been
//           answered once, deliberately: A stays at 0.8, B is 1.0, and the two
//           decks differ on purpose. Do not "even them up".
static constexpr float kInitParamDefaults[] = {
     0.116716892f, // RATE_A
     0.000000000f, // SHAPE_A
     0.695181072f, // DENSITY_A
     0.995180666f, // SMOOTH_A
     0.000000000f, // RANGE_A
     0.000000000f, // MELODY_A
     0.612047195f, // MOD_A
     0.000000000f, // TUNE_A
     0.185333401f, // ATTACK_A
     0.322666585f, // DECAY_A
     0.319000006f, // RES_A
     0.458666444f, // SUB_A
     0.438666672f, // SOURCE_A
     0.864000380f, // FLUX_A
     0.000000000f, // GRIT_A
     0.629666805f, // COMP_A
    16.000000000f, // STEPS_A
     0.000000000f, // ENGINE_A
     1.000000000f, // GRITMODE_A
     0.000000000f, // STEP_A
     2.000000000f, // FORM_A = HIERARCHICAL
     0.000000000f, // NEWPHRASE_A
     0.000000000f, // SONG_A = AAAB
     0.202409565f, // RATE_B
     0.899999678f, // SHAPE_B
     0.644577920f, // DENSITY_B
     0.613253355f, // SMOOTH_B
     0.000000000f, // RANGE_B
    -1.000000000f, // MELODY_B
     0.357831180f, // MOD_B
     0.000000000f, // TUNE_B
     0.093333311f, // ATTACK_B
     0.450666398f, // DECAY_B
     0.217333555f, // RES_B
     0.319999605f, // SUB_B
     0.177333504f, // SOURCE_B
     1.000000000f, // FLUX_B
     0.000000000f, // GRIT_B
     0.561333418f, // COMP_B
    16.000000000f, // STEPS_B
     3.000000000f, // ENGINE_B
     0.000000000f, // GRITMODE_B
     0.000000000f, // STEP_B
     2.000000000f, // FORM_B = HIERARCHICAL
     0.000000000f, // NEWPHRASE_B
     0.000000000f, // SONG_B = AAAB
     0.785541892f, // MORPH
     1.000000000f, // SYNC
     0.169333577f, // TEMPO
     1.000000000f, // COUPLE
     5.000000000f, // SCALE
     0.958666623f, // DRIFT
     0.000000000f, // SPOT
     0.482666761f, // MASTER_DRIVE
     0.000000000f, // SETTLE
     0.869332671f, // REV_SIZE
     0.790665507f, // REV_DECAY
     0.761333108f, // REV_TONE
     0.862999976f, // REV_DIFF
     0.484000504f, // REV_SMEAR
     0.237000003f, // REV_MOD
     0.000000000f, // CHOKE
    -0.172999933f, // FILT_A
    -0.199999630f, // FILT_B
     0.000000000f, // TIDE
     0.392727494f, // FLUXRATE_A
     0.254666120f, // FLUXRATE_B
     0.285667986f, // FLUXFB_A
     0.555337131f, // FLUXFB_B
     0.000000000f, // COLOR_A
     0.469879329f, // COLOR_B
     0.000000000f, // LINK_A
     0.000000000f, // LINK_B
     0.800000012f,  // STAGES_A
     1.000000000f,  // STAGES_B
     0.000000000f, // REC_A
     0.000000000f, // REC_B
     0.422665179f, // REV_MIX_A
     0.613332987f, // REV_MIX_B
     0.000000000f, // SHUFFLE
     0.171428576f, // DETUNE_A = 6 / 35
     0.171428576f, // DETUNE_B = 6 / 35
     0.200000003f, // DRIVE_A
     0.200000003f, // DRIVE_B
};

static_assert(sizeof(kInitParamDefaults) / sizeof(kInitParamDefaults[0])
                  == NUM_PARAMS,
              "init snapshot must contain one value for every ParamId");

inline float initParamDefault(int id) {
    return kInitParamDefaults[id];
}

} // namespace spkyvcv
