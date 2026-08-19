#include "feed/feed_engine.h"
#include "synth/synth_engine.h"
#include "util/math.h"
#include <cmath>

namespace spky {

static_assert(FeedEngine::kCtrlInterval == SynthEngine::kCtrlInterval,
              "FEED's control raster must be the instrument's, or its glides "
              "and Part::_control_tick's pushes fall off each other's grid");

void FeedEngine::init(float sample_rate) {
    _sr = sample_rate > 1.f ? sample_rate : 48000.f;
    _env.init(_sr);
    _bank.init(_sr);
    _rng.seed(_seed);
    _draw_individual();
    _ctrl_ctr = 0;
    _rebuild_allocation();
}

void FeedEngine::set_targets(const float* t, float /*tune*/) {
    _bond     = clampf(t[LANE_SOURCE], 0.f, 1.f);
    _spread_n = clampf(t[LANE_SIZE],   0.f, 1.f);
    _pitch_n  = clampf(t[LANE_PITCH],  0.f, 1.f);
    _depth_n  = clampf(t[LANE_MOTION], 0.f, 1.f);
    _level    = clampf(t[LANE_LEVEL],  0.f, 1.f);
}

void FeedEngine::process(float& outL, float& outR) {
    if (--_ctrl_ctr <= 0) { _ctrl_ctr = kCtrlInterval; _control_tick(); }
    _env.process();   // per sample; the meter reads it through voice_env()
    outL = 0.f;       // Task 5: wires the bank through the ceiling
    outR = 0.f;
}

void FeedEngine::trigger(float pitch_norm) {
    _chord[0] = clampf(pitch_norm, 0.f, 1.f);
    _chord_n = 1;
    _env.trigger();   // Task 6: the accent, and the retune landing as a glide
}

void FeedEngine::trigger_chord(const float* p, int n) {
    if (n < 1) return;   // Task 7: the whole chord, not just its root
    _chord[0] = clampf(p[0], 0.f, 1.f);
    _chord_n = 1;
    _env.trigger();
}

void FeedEngine::set_chord(const float* /*p*/, int /*n*/) {}   // Task 7

void FeedEngine::set_cycle(float seconds) {
    _cycle_s = seconds > 1e-4f ? seconds : 1e-4f;
}

void FeedEngine::set_flow(bool flow) { _flow = flow; }   // Task 6: auto-retrigger

void FeedEngine::set_hold(bool on) { _hold = on; }       // Task 6: CHOKE

void FeedEngine::set_width(float n) { _width = clampf(n, 0.f, 1.f); }

void FeedEngine::set_accent(float a) { _accent = clampf(a, 0.f, 1.f); }

void FeedEngine::set_attack(float n) { _rise_n = clampf(n, 0.f, 1.f); }   // Task 6

void FeedEngine::set_decay(float n) { _fall_n = clampf(n, 0.f, 1.f); }    // Task 6

void FeedEngine::set_resonance(float n) { _ratio = clampf(n, 0.f, 1.f); } // Task 8

void FeedEngine::set_sub(float n) { _sub_n = clampf(n, 0.f, 1.f); }       // Task 8

void FeedEngine::set_filt(float t) { _damp_t = clampf(t, -1.f, 1.f); }    // Task 8

void FeedEngine::reseed(uint32_t s) { _rng.seed(s); }                     // Task 9

float FeedEngine::pair_hz_for_test(int /*i*/) const { return 0.f; }        // Task 5
float FeedEngine::pair_amp_for_test(int /*i*/) const { return 0.f; }       // Task 5
float FeedEngine::pair_fb_amount_for_test(int /*i*/) const { return 0.f; } // Task 5

void FeedEngine::_control_tick() {}          // Task 5

void FeedEngine::_rebuild_allocation() {}    // Task 5

void FeedEngine::_draw_individual() {}       // Task 5

}  // namespace spky
