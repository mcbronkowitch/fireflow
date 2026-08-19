#pragma once
#include <cstdint>
#include "feed/feed_config.h"
#include "feed/feed_pair.h"      // a forward-declared FeedBank cannot be a
                                 // by-value member, so this include is not
                                 // optional and the two headers ship together
#include "mod/rng.h"
#include "parts/engine_iface.h"
#include "pitch/chord.h"
#include "synth/env.h"
#include "util/math.h"           // clampf, in the inline setters below

namespace spky {

// FEED: a fixed ring of feed_cfg::kPairs two-operator FM pairs per deck,
// running continuously. A trigger retunes the ring and injects energy through
// one Env; it does not start it. See the spec for the topology; feed_pair.h
// owns the hot loop and this class owns everything at control rate.
class FeedEngine : public IPartEngine {
public:
    static constexpr int kCtrlInterval = feed_cfg::kCtrlInterval;
    static constexpr int kMaxChord     = ChordBuilder::kMaxNotes;

    void set_seed(uint32_t seed) { _seed = seed; }   // call BEFORE init

    void init(float sample_rate) override;
    void set_targets(const float* t, float tune) override;
    void trigger(float pitch_norm) override;
    void trigger_chord(const float* pitches_norm, int n) override;
    void set_chord(const float* pitches_norm, int n) override;
    void process(float& outL, float& outR) override;
    void set_cycle(float seconds) override;
    void set_flow(bool flow) override;
    void set_hold(bool on) override;
    void set_width(float n) override;
    void set_accent(float a) override;

    // VOICE edit layer. The names are the FEED captions, the setters are the
    // interface Part forwards through -- see the control map in the plan.
    void set_attack(float n);      // RISE
    void set_decay(float n);       // FALL, and FLOOR in its top quarter
    void set_resonance(float n);   // RATIO
    void set_sub(float n);         // SUB
    void set_filt(float t);        // DAMP, bipolar
    // DETUNE means SPREAD on a FEED deck, and it gets there as the LANE_SIZE
    // base (host/vcv/src/Fireflow.cpp), not through this setter. Kept as an
    // explicit no-op rather than left unimplemented: Part::set_voice_detune
    // forwards to every melodic engine in one line, and an engine missing from
    // that line is the failure class engine_iface.h's process_in/
    // consumes_input comment is about.
    void set_detune(float /*n*/) {}

    // NEW: redraw the deck's individual -- the SPREAD signature and the
    // per-pair feedback offsets (spec section 3.4). The only randomness in the
    // engine, and it is not on the audio path.
    void reseed(uint32_t s);

    int   active_voices() const { return _env.active() ? 1 : 0; }
    float voice_env(int v) const { return v == 0 ? _env.value() : 0.f; }

    // --- observation (tests). Not used on the audio path. ---
    float pair_hz_for_test(int i) const;
    float pair_amp_for_test(int i) const;
    float pair_fb_amount_for_test(int i) const;
    float ratio_for_test() const  { return _ratio; }
    float spread_ct_for_test() const { return _spread_ct; }
    float floor_for_test() const  { return _floor_n; }
    int   voiced_tones_for_test() const { return _voiced_n; }

private:
    void _control_tick();
    void _rebuild_allocation();
    void _draw_individual();

    FeedBank _bank;
    Env      _env;
    Rng      _rng;

    uint32_t _seed = 0x46454544u;   // "FEED"
    float    _sr = 48000.f;
    int      _ctrl_ctr = 0;

    // lanes
    float _bond = 0.f;
    float _spread_n = 0.f;
    float _pitch_n = 0.5f;
    float _depth_n = feed_cfg::kDepthBase;
    float _level = 1.f;

    // voice row
    float _rise_n = 0.5f;
    float _fall_n = 0.5f;
    float _floor_n = 0.f;
    float _ratio = 1.f;
    float _sub_n = 0.f;
    float _damp_t = 0.f;
    float _accent = 0.f;
    float _width = 1.f;

    // derived
    float _spread_ct = 0.f;
    float _cycle_s = 1.f;
    bool  _flow = false;
    bool  _hold = false;

    // chord surface
    float _chord[kMaxChord] = { 0.5f, 0.f, 0.f, 0.f };
    int   _chord_n = 1;
    int   _voiced_n = 1;

    // NEW's individual
    float _spread_sig[feed_cfg::kPairs] = {};
    float _fb_offset[feed_cfg::kPairs] = {};

    // SUB
    float _sub_phase = 0.f;
    float _sub_inc = 0.f;
};

}  // namespace spky
