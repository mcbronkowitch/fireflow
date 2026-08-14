// engine/param_table.h
#pragma once
#include "instrument.h"

namespace spky {

// A draw range, engine units. It was declared in the terrain layer's tuning
// table and moved out on 2026-08-06 so a consumer that only needed to name a
// range did not have to drag the whole table in to get it. That layer was
// deleted 2026-08-14 and the type currently has no consumer left; it survives
// as the vocabulary a panel or a generator would use to state a parameter's
// draw range against the table below.
struct Span { float lo, hi; };

// Every parameter reachable through apply_param() / apply_mode_and_steps()
// below -- the set the deleted terrain layer owned, kept because it is the
// engine's own parameter table and the only enumeration of the whole surface
// in one place. Ranges are ENGINE units (the surface is not uniformly
// 0..1 -- FILT/VARIATION/CHOKE are bipolar,
// ENGINE/SCALE/ROOT/FORM/SONG/STEPS are discrete). steps==0 -> continuous.
// The RES ceiling 0.75 encodes the by-ear resonance cap as a hard limit.
//
// Verified against the real headers (not the brief's placeholder guesses).
// These ranges are ENGINE facts. Where a bullet mentions Fireflow at all it is
// as corroboration, never as the source -- a Fireflow control can be deleted
// or merged (it has been) without any of these numbers moving.
//
// The Fireflow <-> terrain correspondence this table once served is history:
// the converter and the layer it fed were deleted 2026-08-14. Its parameter
// map is kept for the reasoning only, at
// docs/attic/flow-fireflow-param-map.md. Do not re-derive a mapping here, and
// do not let this comment grow one.
//
// - ENGINE: engine/parts/engine_iface.h's EngineId runs
//   ENGINE_TEST_TONE=0 .. ENGINE_BBD=5, ENGINE_COUNT=6 -- so 0..5, 6 steps,
//   not 0..4/5. apply_param() below hands EngineId(i) straight to
//   Instrument::set_engine(), so the table must cover the real enum, not
//   host/vcv/src/Fireflow.cpp's UI remap (which drops ENGINE_TEST_TONE and
//   renumbers the rest for its own knob -- a host-side translation, not the
//   engine's id space). Re-verified 2026-08-12: still true, and the remap is
//   Fireflow.cpp:649-656.
// - FORM: engine/mod/song_form.h uses Principle from engine/mod/phrase_gen.h,
//   whose kCount is 5 (TwoMotif, OneMotif, Hierarchical, CallResponse,
//   Ostinato) -- so 0..4, 5 steps, not 0..3/4. FORM HAS NO Fireflow CONTROL:
//   the 2026-08-09 control reduction deleted the FORM_A/B configSwitch this
//   comment used to cite and folded FORM into the SONG knob, which now walks
//   a curated 14-rung ladder through the 5x7 (Principle, SongMode) grid
//   (engine/mod/song_ladder.h, song_ladder_at()) -- 14 of the 35 pairs, all
//   five Principle values among them. The range above is unaffected: it is
//   the engine's, and the ladder cannot reach outside it.
// - SONG: engine/mod/song_form.h's SongMode has kCount 7 (AAAB, ABAB, ABBB,
//   Build, Rotate, Mirror, Off) and Instrument::set_song() clamps through
//   clamp_song() to 0..kCount-1 -- so 0..6, 7 steps, not 0..3/4. Fireflow's
//   own SONG_A/B is NO LONGER a 0..6 configSwitch: since the same control
//   reduction it is configSwitch(0.f, kSongLadderCount-1 == 13.f, ..., 14
//   labels) (Fireflow.cpp:396-398) and its value is a LADDER RUNG, not a
//   SongMode. The engine range stands; the corroboration did not.
// - STEPS: 2..16, 15 steps. The floor of 2 is this table's own: it is the
//   count a caller may issue through apply_mode_and_steps() below, and it
//   never reaches 0 or 1 because "off" is a mode, not a step count. Fireflow
//   does NOT match it: STEPS_A/B is configParam(c.id, 0.f, 16.f, init,
//   "Steps") (Fireflow.cpp:411), because 0 is how a Fireflow deck says "STEP
//   off" (set_step(p, steps > 0, steps), Fireflow.cpp:843). Here that state
//   is P_MODE, which is why the ranges differ on purpose.
// - LINK: NOT unipolar's brief placeholder (-1..1). Flux::set_link()
//   (engine/fx/flux.cpp:126-128) clamps to [0,1], and Fireflow.cpp's own
//   LINK_A/B configParam is 0..1 (Fireflow.cpp:334-335) -- the bipolar
//   surface was retired in a deliberate migration (see migrate_legacy_link()
//   at the patch-load site, Fireflow.cpp:1074). So 0..1 continuous, not
//   -1..1. Re-verified 2026-08-12; only the line numbers had drifted.
#define SPKY_PARAMS(X) \
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
  X(P_TEMPO_BPM, 50.f, 140.f, 0) \
  /* The instrument's operating mode. 0 = FLOW/free (lanes breathe in their
     own kLaneRatio relationships, no grid), 1 = STEP/synced (step sequencer
     on the divisions.h ladder). ONE global value, not one per deck:
     Instrument::set_sync is global (instrument.h:274), so a per-deck mode
     would need SYNC on and off at once. */ \
  X(P_MODE,       0.f, 1.f,  2) \
  /* PACE: the global modulation time-stretch. 0.5 = x1. Carries a live
     offset rather than a base value. */ \
  X(P_PACE,       0.f, 1.f, 0)

enum ParamId {
#define SPKY_ENUM(id, lo, hi, st) id,
  SPKY_PARAMS(SPKY_ENUM)
#undef SPKY_ENUM
  P_COUNT
};

struct ParamInfo { const char* name; float lo, hi; int steps; };

constexpr ParamInfo kParams[P_COUNT] = {
#define SPKY_INFO(id, lo_, hi_, st) { #id, lo_, hi_, st },
  SPKY_PARAMS(SPKY_INFO)
#undef SPKY_INFO
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
    // PACE is normalized 0..1 and pace_mult() (mod/divisions.h) is the curve;
    // set_pace takes the knob position, not the multiplier, so this is a
    // plain forward like every other continuous row.
    case P_PACE:       in.set_pace(v); break;
    // P_MODE, P_STEPS_A and P_STEPS_B are deliberately NOT handled here.
    // set_step() takes mode AND count together and set_sync() is global, so
    // routing them needs all three values at once -- which this per-param,
    // stateless function cannot see. apply_mode_and_steps() below owns them.
    case P_MODE: case P_STEPS_A: case P_STEPS_B: break;
    default: break;
    }
}

// The three parameters apply_param() refuses, issued as the one unit they
// have to be: set_step() takes mode and count together and set_sync() is
// global, so a per-parameter, stateless setter cannot see all three at once.
// Flow::push_mode_and_steps() used to own this and was deleted with the flow
// layer (removal spec
// docs/superpowers/specs/2026-08-14-flow-glow-removal-design.md, 4.2); a
// panel driving the instrument through kParams needs it or it cannot set the
// operating mode or either step count.
//
// This forces both decks' STEP state and SYNC together, so it gives up the
// per-deck freedom Instrument actually has: set_step() is per-deck
// (instrument.h:91) while set_sync() is the only global piece
// (instrument.h:424), and the shipped Fireflow VCV module uses that freedom
// every day -- host/vcv/src/Fireflow.cpp:892 calls
// `set_step(p, steps > 0, steps)` inside a per-deck loop, so deck A can run
// STEP while deck B runs free. A caller that needs that split cannot reach
// it through this function and must call set_step()/set_sync() directly.
inline void apply_mode_and_steps(Instrument& in, bool step_mode,
                                 int steps_a, int steps_b) {
    in.set_sync(step_mode);
    in.set_step(PART_A, step_mode, steps_a);
    in.set_step(PART_B, step_mode, steps_b);
}

} // namespace spky
