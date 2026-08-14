// GENERATED once on 2026-08-14 by tools/dump_points.cpp,
// which was then deleted. See the removal plan, task 1.
// The generator is gone, so THIS FILE IS NOW HAND-MAINTAINED: any future
// edit to it (or to the enum it is keyed against) is a human edit, with no
// program left to regenerate it or check it against the enum.
//
// These are the four operating points tests/test_param_impact.cpp
// used to draw from the terrain generator on every build. The
// generator is gone; the points are not. deck_audible() still runs
// live against each one -- see load_points() in the test.
//
// EACH `v[P_COUNT]` BELOW IS POSITIONAL, bound to the ParamId enum in
// engine/param_table.h by array index, with only a trailing `// P_NAME`
// comment for a human to cross-check by eye. That has two very different
// consequences for editing the enum in engine/param_table.h:
//
//  - APPENDING a new ParamId at the end (right before P_COUNT) is genuinely
//    harmless. Every existing row here keeps its old index and its old
//    meaning; the short aggregate initializer below zero-fills the new,
//    unused tail slot, and nothing else changes.
//  - INSERTING a new ParamId anywhere else silently shifts every later row
//    down by one position. C++ accepts a short aggregate initializer without
//    a warning, so this file keeps compiling -- but from the insertion point
//    on, every value is now paired with the wrong parameter, and the tail
//    that has nowhere left to go is zero-filled. tests/test_param_impact.cpp
//    then goes red, but it reports "these parameters died", which is a
//    misleading diagnosis for "your frozen points are stale".
//
// HOW TO TELL WHICH HAPPENED: compare P_COUNT and P_MODE/P_PACE against
// tests/test_param_table.cpp's inventory-marker case (`P_MODE == 62`,
// `P_PACE == P_COUNT - 1`). If that case still passes unmodified, nothing
// was inserted above P_MODE and this file's rows are still aligned. If you
// changed the enum and that case reddened as intended, re-derive these
// vectors (there is no generator left -- re-measure by hand, or shift the
// affected rows to match the new indices) before trusting a red run here.
#pragma once
#include "param_table.h"

namespace spky {

constexpr int kPer = 2;

struct FrozenPoint {
    const char* origin;
    float v[P_COUNT];
    bool  step;
    int   steps_a, steps_b;
};

inline constexpr FrozenPoint kFlowPoints[kPer] = {
  { "master 1, FLOW", {
      4.0f, // P_ENGINE_A
      4.0f, // P_ENGINE_B
      2.0f, // P_SCALE
      6.0f, // P_ROOT
      1.0f, // P_FORM_A
      2.0f, // P_FORM_B
      1.0f, // P_SONG_A
      4.0f, // P_SONG_B
      10.0f, // P_STEPS_A
      8.0f, // P_STEPS_B
      0.582404256f, // P_TUNE_A
      0.415198267f, // P_TUNE_B
      0.472123623f, // P_RATE_A
      0.419931263f, // P_RATE_B
      0.442628443f, // P_SHAPE_A
      0.444082648f, // P_SHAPE_B
      0.439053357f, // P_DENSITY_A
      0.390495628f, // P_DENSITY_B
      0.321487665f, // P_SMOOTH_A
      0.37478599f, // P_SMOOTH_B
      0.33106184f, // P_RANGE_A
      0.332367778f, // P_RANGE_B
      0.375614315f, // P_DEPTH_A
      0.379201591f, // P_DEPTH_B
      0.439802229f, // P_COLOR_A
      0.419255346f, // P_COLOR_B
      0.398875982f, // P_VARIATION_A
      0.396701425f, // P_VARIATION_B
      0.0808138326f, // P_ATTACK_A
      0.107730851f, // P_ATTACK_B
      0.246147126f, // P_DECAY_A
      0.31208393f, // P_DECAY_B
      0.245045662f, // P_RES_A
      0.299377561f, // P_RES_B
      0.373233885f, // P_SUB_A
      0.382589996f, // P_SUB_B
      0.0863982737f, // P_FILT_A
      0.080051139f, // P_FILT_B
      0.229908779f, // P_FLUXMIX_A
      0.225242376f, // P_FLUXMIX_B
      0.134162173f, // P_GRIT_A
      0.141437382f, // P_GRIT_B
      0.56300658f, // P_COMP_A
      0.557472944f, // P_COMP_B
      0.170167714f, // P_LINK_A
      0.286355585f, // P_LINK_B
      0.478769869f, // P_REVMIX_A
      0.440403372f, // P_REVMIX_B
      0.555217266f, // P_MORPH
      0.347220063f, // P_COUPLE
      0.242812887f, // P_DRIFT
      0.515877604f, // P_TIDE
      0.0f, // P_CHOKE
      0.110991724f, // P_SHUFFLE
      0.0935838521f, // P_DRIVE
      0.550684631f, // P_REV_SIZE
      0.576180637f, // P_REV_DECAY
      0.480363995f, // P_REV_TONE
      0.655307472f, // P_REV_DIFF
      0.349993765f, // P_REV_SMEAR
      0.114876121f, // P_REV_MOD
      91.1968918f, // P_TEMPO_BPM
      0.0f, // P_MODE
      0.5f, // P_PACE
    }, false, 10, 8 },
  { "master 2, FLOW", {
      3.0f, // P_ENGINE_A
      1.0f, // P_ENGINE_B
      2.0f, // P_SCALE
      6.0f, // P_ROOT
      1.0f, // P_FORM_A
      2.0f, // P_FORM_B
      1.0f, // P_SONG_A
      2.0f, // P_SONG_B
      4.0f, // P_STEPS_A
      4.0f, // P_STEPS_B
      0.38437447f, // P_TUNE_A
      0.515850902f, // P_TUNE_B
      0.0690603778f, // P_RATE_A
      0.150230661f, // P_RATE_B
      0.117253065f, // P_SHAPE_A
      0.101717211f, // P_SHAPE_B
      0.132790327f, // P_DENSITY_A
      0.105084717f, // P_DENSITY_B
      0.76441741f, // P_SMOOTH_A
      0.754668117f, // P_SMOOTH_B
      0.254094511f, // P_RANGE_A
      0.30223161f, // P_RANGE_B
      0.335272163f, // P_DEPTH_A
      0.543176115f, // P_DEPTH_B
      0.653238297f, // P_COLOR_A
      0.641325295f, // P_COLOR_B
      0.384867609f, // P_VARIATION_A
      0.333378077f, // P_VARIATION_B
      0.757963419f, // P_ATTACK_A
      0.698486209f, // P_ATTACK_B
      0.737282515f, // P_DECAY_A
      0.77143538f, // P_DECAY_B
      0.253967017f, // P_RES_A
      0.291260213f, // P_RES_B
      0.4293468f, // P_SUB_A
      0.215408742f, // P_SUB_B
      0.126671791f, // P_FILT_A
      0.0615344606f, // P_FILT_B
      0.174638748f, // P_FLUXMIX_A
      0.314266503f, // P_FLUXMIX_B
      0.113334186f, // P_GRIT_A
      0.0336342268f, // P_GRIT_B
      0.547110856f, // P_COMP_A
      0.522404253f, // P_COMP_B
      0.458004922f, // P_LINK_A
      0.34259814f, // P_LINK_B
      0.481633127f, // P_REVMIX_A
      0.418499142f, // P_REVMIX_B
      0.596520603f, // P_MORPH
      0.343058914f, // P_COUPLE
      0.312935293f, // P_DRIFT
      0.412494719f, // P_TIDE
      0.0f, // P_CHOKE
      4.35130896e-05f, // P_SHUFFLE
      0.0600556508f, // P_DRIVE
      0.574680746f, // P_REV_SIZE
      0.615386426f, // P_REV_DECAY
      0.465753794f, // P_REV_TONE
      0.747400463f, // P_REV_DIFF
      0.262450486f, // P_REV_SMEAR
      0.123326555f, // P_REV_MOD
      64.3865738f, // P_TEMPO_BPM
      0.0f, // P_MODE
      0.5f, // P_PACE
    }, false, 4, 4 },
};

inline constexpr FrozenPoint kStepPoints[kPer] = {
  { "master 3, STEP", {
      4.0f, // P_ENGINE_A
      4.0f, // P_ENGINE_B
      1.0f, // P_SCALE
      8.0f, // P_ROOT
      0.0f, // P_FORM_A
      2.0f, // P_FORM_B
      0.0f, // P_SONG_A
      4.0f, // P_SONG_B
      8.0f, // P_STEPS_A
      4.0f, // P_STEPS_B
      0.37105608f, // P_TUNE_A
      0.580794275f, // P_TUNE_B
      0.6875f, // P_RATE_A
      0.5f, // P_RATE_B
      0.644438863f, // P_SHAPE_A
      0.443576097f, // P_SHAPE_B
      0.408827245f, // P_DENSITY_A
      0.355454832f, // P_DENSITY_B
      0.327308059f, // P_SMOOTH_A
      0.276291966f, // P_SMOOTH_B
      0.53165108f, // P_RANGE_A
      0.465314686f, // P_RANGE_B
      0.504079461f, // P_DEPTH_A
      0.474504918f, // P_DEPTH_B
      0.171159804f, // P_COLOR_A
      0.164393738f, // P_COLOR_B
      0.310019791f, // P_VARIATION_A
      0.376438916f, // P_VARIATION_B
      0.129196703f, // P_ATTACK_A
      0.20597519f, // P_ATTACK_B
      0.165792286f, // P_DECAY_A
      0.157650188f, // P_DECAY_B
      0.149128363f, // P_RES_A
      0.350378066f, // P_RES_B
      0.370909929f, // P_SUB_A
      0.233362153f, // P_SUB_B
      0.124613494f, // P_FILT_A
      0.124712244f, // P_FILT_B
      0.137902111f, // P_FLUXMIX_A
      0.232209027f, // P_FLUXMIX_B
      0.156781018f, // P_GRIT_A
      0.206250578f, // P_GRIT_B
      0.529881716f, // P_COMP_A
      0.559217334f, // P_COMP_B
      0.218105361f, // P_LINK_A
      0.160383299f, // P_LINK_B
      0.519084454f, // P_REVMIX_A
      0.403231978f, // P_REVMIX_B
      0.547420144f, // P_MORPH
      0.266380608f, // P_COUPLE
      0.269628525f, // P_DRIFT
      0.543698192f, // P_TIDE
      0.0f, // P_CHOKE
      0.382861853f, // P_SHUFFLE
      0.221250355f, // P_DRIVE
      0.566972077f, // P_REV_SIZE
      0.597016752f, // P_REV_DECAY
      0.468500048f, // P_REV_TONE
      0.645909011f, // P_REV_DIFF
      0.308255285f, // P_REV_SMEAR
      0.112915367f, // P_REV_MOD
      85.5476227f, // P_TEMPO_BPM
      1.0f, // P_MODE
      0.5f, // P_PACE
    }, true, 8, 4 },
  { "master 8, STEP", {
      1.0f, // P_ENGINE_A
      4.0f, // P_ENGINE_B
      5.0f, // P_SCALE
      4.0f, // P_ROOT
      1.0f, // P_FORM_A
      3.0f, // P_FORM_B
      1.0f, // P_SONG_A
      1.0f, // P_SONG_B
      4.0f, // P_STEPS_A
      7.0f, // P_STEPS_B
      0.504173517f, // P_TUNE_A
      0.355049491f, // P_TUNE_B
      0.25f, // P_RATE_A
      0.1875f, // P_RATE_B
      0.195671588f, // P_SHAPE_A
      0.0562787838f, // P_SHAPE_B
      0.135651812f, // P_DENSITY_A
      0.141675323f, // P_DENSITY_B
      0.713853419f, // P_SMOOTH_A
      0.700972438f, // P_SMOOTH_B
      0.171504408f, // P_RANGE_A
      0.20753485f, // P_RANGE_B
      0.435132802f, // P_DEPTH_A
      0.52862227f, // P_DEPTH_B
      0.539438725f, // P_COLOR_A
      0.687223911f, // P_COLOR_B
      0.378367364f, // P_VARIATION_A
      0.342901647f, // P_VARIATION_B
      0.641960382f, // P_ATTACK_A
      0.671426892f, // P_ATTACK_B
      0.742894888f, // P_DECAY_A
      0.69691211f, // P_DECAY_B
      0.292685121f, // P_RES_A
      0.35751763f, // P_RES_B
      0.416006446f, // P_SUB_A
      0.495584309f, // P_SUB_B
      0.113015607f, // P_FILT_A
      0.101828143f, // P_FILT_B
      0.402365088f, // P_FLUXMIX_A
      0.339851558f, // P_FLUXMIX_B
      0.0767824799f, // P_GRIT_A
      0.090533331f, // P_GRIT_B
      0.511634946f, // P_COMP_A
      0.51146692f, // P_COMP_B
      0.465037405f, // P_LINK_A
      0.349200338f, // P_LINK_B
      0.479919255f, // P_REVMIX_A
      0.417705089f, // P_REVMIX_B
      0.520383f, // P_MORPH
      0.367906868f, // P_COUPLE
      0.29342404f, // P_DRIFT
      0.463470519f, // P_TIDE
      0.0f, // P_CHOKE
      0.0741160884f, // P_SHUFFLE
      0.0622235648f, // P_DRIVE
      0.578576207f, // P_REV_SIZE
      0.597267628f, // P_REV_DECAY
      0.476316422f, // P_REV_TONE
      0.771671534f, // P_REV_DIFF
      0.319188148f, // P_REV_SMEAR
      0.121465854f, // P_REV_MOD
      69.9121094f, // P_TEMPO_BPM
      1.0f, // P_MODE
      0.5f, // P_PACE
    }, true, 4, 7 },
};

}  // namespace spky
