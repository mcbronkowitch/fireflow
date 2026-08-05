// engine/flow/flow_params.h
#pragma once
#include "flow/flow_ids.h"
#include "instrument.h"

namespace spky { namespace flow {

// Every parameter the flow layer owns. Ranges are ENGINE units (§2: the
// surface is not uniformly 0..1 -- FILT/VARIATION/CHOKE are bipolar,
// ENGINE/SCALE/ROOT/FORM/SONG/STEPS are discrete). steps==0 -> continuous.
// The RES ceiling 0.75 encodes the by-ear resonance cap as a hard limit.
//
// Verified against the real headers (not the brief's placeholder guesses):
// - ENGINE: engine/parts/engine_iface.h's EngineId runs
//   ENGINE_TEST_TONE=0 .. ENGINE_BBD=5, ENGINE_COUNT=6 -- so 0..5, 6 steps,
//   not 0..4/5. apply_param() below hands EngineId(i) straight to
//   Instrument::set_engine(), so the table must cover the real enum, not
//   host/vcv/src/Fireflow.cpp's UI remap (which drops ENGINE_TEST_TONE and
//   renumbers the rest for its own knob -- a host-side translation, not the
//   engine's id space).
// - FORM: engine/mod/song_form.h uses Principle from engine/mod/phrase_gen.h,
//   whose kCount is 5 (TwoMotif, OneMotif, Hierarchical, CallResponse,
//   Ostinato) -- so 0..4, 5 steps, not 0..3/4. Matches Fireflow.cpp's own
//   FORM_A/B configSwitch(0.f, 4.f, ..., 5 labels).
// - SONG: engine/mod/song_form.h's SongMode has kCount 7 (AAAB, ABAB, ABBB,
//   Build, Rotate, Mirror, Off) and Instrument::set_song() clamps through
//   clamp_song() to 0..kCount-1 -- so 0..6, 7 steps, not 0..3/4. Matches
//   Fireflow.cpp's own SONG_A/B configSwitch(0.f, 6.f, ..., 7 labels).
// - STEPS: host/vcv/src/Fireflow.cpp configures STEPS_A/B as
//   configParam(c.id, 2.f, 16.f, ...) -- matches the brief's 2..16, 15 steps
//   exactly, no change needed.
// - LINK: NOT unipolar's brief placeholder (-1..1). Flux::set_link()
//   (engine/fx/flux.cpp:128) clamps to [0,1], and Fireflow.cpp's own
//   LINK_A/B configParam is 0..1 (Fireflow.cpp:323-324) -- the bipolar
//   surface was retired in a deliberate migration (see the legacy-LINK
//   patch-load remap around Fireflow.cpp:869-903). So 0..1 continuous,
//   not -1..1.
#define SPKY_FLOW_PARAMS(X) \
  X(P_ENGINE_A,   0.f, 5.f,  6)  X(P_ENGINE_B,   0.f, 5.f,  6) \
  X(P_SCALE,      0.f, 12.f, 13) X(P_ROOT,       0.f, 11.f, 12) \
  X(P_FORM_A,     0.f, 4.f,  5)  X(P_FORM_B,     0.f, 4.f,  5) \
  X(P_SONG_A,     0.f, 6.f,  7)  X(P_SONG_B,     0.f, 6.f,  7) \
  X(P_STEPS_A,    2.f, 16.f, 15) X(P_STEPS_B,    2.f, 16.f, 15) \
  X(P_TUNE_A,     0.f, 1.f, 0)   X(P_TUNE_B,     0.f, 1.f, 0) \
  X(P_RATE_A,     0.f, 1.f, 0)   X(P_RATE_B,     0.f, 1.f, 0) \
  X(P_SHAPE_A,    0.f, 1.f, 0)   X(P_SHAPE_B,    0.f, 1.f, 0) \
  X(P_DENSITY_A,  0.f, 1.f, 0)   X(P_DENSITY_B,  0.f, 1.f, 0) \
  X(P_SMOOTH_A,   0.f, 1.f, 0)   X(P_SMOOTH_B,   0.f, 1.f, 0) \
  X(P_RANGE_A,    0.f, 1.f, 0)   X(P_RANGE_B,    0.f, 1.f, 0) \
  X(P_DEPTH_A,    0.f, 1.f, 0)   X(P_DEPTH_B,    0.f, 1.f, 0) \
  X(P_COLOR_A,    0.f, 1.f, 0)   X(P_COLOR_B,    0.f, 1.f, 0) \
  X(P_VARIATION_A,-1.f, 1.f, 0)  X(P_VARIATION_B,-1.f, 1.f, 0) \
  X(P_ATTACK_A,   0.f, 1.f, 0)   X(P_ATTACK_B,   0.f, 1.f, 0) \
  X(P_DECAY_A,    0.f, 1.f, 0)   X(P_DECAY_B,    0.f, 1.f, 0) \
  X(P_RES_A,      0.f, 0.75f, 0) X(P_RES_B,      0.f, 0.75f, 0) \
  X(P_SUB_A,      0.f, 1.f, 0)   X(P_SUB_B,      0.f, 1.f, 0) \
  X(P_FILT_A,    -1.f, 1.f, 0)   X(P_FILT_B,    -1.f, 1.f, 0) \
  X(P_FLUXMIX_A,  0.f, 1.f, 0)   X(P_FLUXMIX_B,  0.f, 1.f, 0) \
  X(P_GRIT_A,     0.f, 1.f, 0)   X(P_GRIT_B,     0.f, 1.f, 0) \
  X(P_COMP_A,     0.f, 1.f, 0)   X(P_COMP_B,     0.f, 1.f, 0) \
  X(P_LINK_A,     0.f, 1.f, 0)   X(P_LINK_B,     0.f, 1.f, 0) \
  X(P_REVMIX_A,   0.f, 1.f, 0)   X(P_REVMIX_B,   0.f, 1.f, 0) \
  X(P_MORPH,      0.f, 1.f, 0)   X(P_COUPLE,     0.f, 1.f, 0) \
  X(P_DRIFT,      0.f, 1.f, 0)   X(P_TIDE,       0.f, 1.f, 0) \
  X(P_CHOKE,     -1.f, 1.f, 0)   X(P_SHUFFLE,    0.f, 1.f, 0) \
  X(P_DRIVE,      0.f, 1.f, 0) \
  X(P_REV_SIZE,   0.f, 1.f, 0)   X(P_REV_DECAY,  0.f, 1.f, 0) \
  X(P_REV_TONE,   0.f, 1.f, 0)   X(P_REV_DIFF,   0.f, 1.f, 0) \
  X(P_REV_SMEAR,  0.f, 1.f, 0)   X(P_REV_MOD,    0.f, 1.f, 0) \
  X(P_TEMPO_BPM, 50.f, 140.f, 0)

enum ParamId {
#define SPKY_FLOW_ENUM(id, lo, hi, st) id,
  SPKY_FLOW_PARAMS(SPKY_FLOW_ENUM)
#undef SPKY_FLOW_ENUM
  P_COUNT
};

struct ParamInfo { const char* name; float lo, hi; int steps; };

constexpr ParamInfo kParams[P_COUNT] = {
#define SPKY_FLOW_INFO(id, lo_, hi_, st) { #id, lo_, hi_, st },
  SPKY_FLOW_PARAMS(SPKY_FLOW_INFO)
#undef SPKY_FLOW_INFO
};

inline float clamp_to(const ParamInfo& pi, float v) {
    if (v < pi.lo) v = pi.lo;
    if (v > pi.hi) v = pi.hi;
    return v;
}

// Route one parameter value to the engine. Discrete params arrive as floats
// and are rounded here; every value is clamped to the table range first.
inline void apply_param(Instrument& in, int param, float v) {
    v = clamp_to(kParams[param], v);
    const int i = int(v + 0.5f);
    switch (param) {
    case P_ENGINE_A:   in.set_engine(PART_A, EngineId(i)); break;
    case P_ENGINE_B:   in.set_engine(PART_B, EngineId(i)); break;
    case P_SCALE:      in.set_scale(i); break;
    case P_ROOT:       in.set_root(PART_A, i); in.set_root(PART_B, i); break;
    case P_FORM_A:     in.set_form(PART_A, i); break;
    case P_FORM_B:     in.set_form(PART_B, i); break;
    case P_SONG_A:     in.set_song(PART_A, i); break;
    case P_SONG_B:     in.set_song(PART_B, i); break;
    case P_STEPS_A:    in.set_step(PART_A, true, i); break;
    case P_STEPS_B:    in.set_step(PART_B, true, i); break;
    case P_TUNE_A:     in.set_tune(PART_A, v); break;
    case P_TUNE_B:     in.set_tune(PART_B, v); break;
    case P_RATE_A:     in.set_rate(PART_A, v); break;
    case P_RATE_B:     in.set_rate(PART_B, v); break;
    case P_SHAPE_A:    in.set_shape(PART_A, v); break;
    case P_SHAPE_B:    in.set_shape(PART_B, v); break;
    case P_DENSITY_A:  in.set_density(PART_A, v); break;
    case P_DENSITY_B:  in.set_density(PART_B, v); break;
    case P_SMOOTH_A:   in.set_smooth(PART_A, v); break;
    case P_SMOOTH_B:   in.set_smooth(PART_B, v); break;
    case P_RANGE_A:    in.set_range(PART_A, v); break;
    case P_RANGE_B:    in.set_range(PART_B, v); break;
    case P_DEPTH_A:    in.set_depth(PART_A, v); break;
    case P_DEPTH_B:    in.set_depth(PART_B, v); break;
    case P_COLOR_A:    in.set_color(PART_A, v); break;
    case P_COLOR_B:    in.set_color(PART_B, v); break;
    case P_VARIATION_A: in.set_variation(PART_A, v); break;
    case P_VARIATION_B: in.set_variation(PART_B, v); break;
    case P_ATTACK_A:   in.set_voice_attack(PART_A, v); break;
    case P_ATTACK_B:   in.set_voice_attack(PART_B, v); break;
    case P_DECAY_A:    in.set_voice_decay(PART_A, v); break;
    case P_DECAY_B:    in.set_voice_decay(PART_B, v); break;
    case P_RES_A:      in.set_voice_resonance(PART_A, v); break;
    case P_RES_B:      in.set_voice_resonance(PART_B, v); break;
    case P_SUB_A:      in.set_voice_sub(PART_A, v); break;
    case P_SUB_B:      in.set_voice_sub(PART_B, v); break;
    case P_FILT_A:     in.set_voice_filt(PART_A, v); break;
    case P_FILT_B:     in.set_voice_filt(PART_B, v); break;
    case P_FLUXMIX_A:  in.set_flux_mix(PART_A, v); break;
    case P_FLUXMIX_B:  in.set_flux_mix(PART_B, v); break;
    case P_GRIT_A:     in.set_grit_mix(PART_A, v); break;
    case P_GRIT_B:     in.set_grit_mix(PART_B, v); break;
    case P_COMP_A:     in.set_comp(PART_A, v); break;
    case P_COMP_B:     in.set_comp(PART_B, v); break;
    case P_LINK_A:     in.set_link(PART_A, v); break;
    case P_LINK_B:     in.set_link(PART_B, v); break;
    case P_REVMIX_A:   in.set_reverb_mix(PART_A, v); break;
    case P_REVMIX_B:   in.set_reverb_mix(PART_B, v); break;
    case P_MORPH:      in.set_morph(v); break;
    case P_COUPLE:     in.set_couple(v); break;
    case P_DRIFT:      in.set_drift(v); break;
    case P_TIDE:       in.set_tide(v); break;
    case P_CHOKE:      in.set_choke(v); break;
    case P_SHUFFLE:    in.set_shuffle(v); break;
    case P_DRIVE:      in.set_master_drive(v); break;
    case P_REV_SIZE:   in.set_reverb_size(v); break;
    case P_REV_DECAY:  in.set_reverb_decay(v); break;
    case P_REV_TONE:   in.set_reverb_tone(v); break;
    case P_REV_DIFF:   in.set_reverb_diffusion(v); break;
    case P_REV_SMEAR:  in.set_reverb_smear(v); break;
    case P_REV_MOD:    in.set_reverb_mod(v); break;
    case P_TEMPO_BPM:  in.set_tempo_bpm(v); break;
    default: break;
    }
}

} } // namespace spky::flow
