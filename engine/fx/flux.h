#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include "Utility/dsp.h"
#include "fx/drag.h"
#include "fx/fx_util.h"
#include "fx/tape_echo.h"
#include "mod/divisions.h"

namespace spky {

inline constexpr size_t kTapeSamples = 262144;

// Stereo, caller-memory tape echo behind a click-free SoftSwitch. The block
// is full-wet internally and adds its two independent returns to the dry
// signal at FLUX MIX.
class Flux {
public:
    static constexpr size_t kMaxSamples = kTapeSamples;

    void init(float sample_rate, float* left, float* right);
    void set_on(bool on, bool immediate = false) { _sw.set_on(on, immediate); }
    bool is_on() const { return _sw.is_on(); }
    bool engaged() const { return _buf_ok && (_sw.is_on() || !_sw.is_idle()); }
    bool has_buffers() const { return _buf_ok; }
    void set_bpm(float bpm);
    void set_rate(int slice_idx);
    float delay_time() const { return _delay_time; }
    void set_feedback(float norm);
    void set_mix(float norm);
    void set_time_mod(float norm);
    void set_link(float norm);
    void set_rhythm(const RhythmView& rv);
    void process(float& l, float& r);

    float delay_target_for_test() const { return _dt_target; }
    float delay_current_for_test() const { return _dt_current; }
    float drag_time_s() const { return _dt_target; }
    float gate_for_test() const { return _gate; }
    int thin_n_for_test(int i) const { return _thin_n[i]; }

private:
    void recompute_time(bool immediate);
    void update_time_target(bool immediate);
    void apply_drag();
    void refresh_repeat_scheduler();
    void update_thin_pattern();
    void advance_gate();

    TapeEcho<kTapeSamples> _echo_l;
    TapeEcho<kTapeSamples> _echo_r;
    SoftSwitch _sw;
    float _mix_lin = 0.f;
    bool _buf_ok = false;
    float _sr = 48000.f;
    float _bpm = 120.f;
    int _rate_idx = 3;
    float _delay_time = 0.5f;
    float _dt_current = 0.5f;
    float _dt_target = 0.5f;
    float _dt_coef = 1.f;
    float _time_mult = 1.f;
    float _time_mod_norm = -1.f;
    float _fb_norm = -1.f;

    float _link = 0.f;
    float _drag = 0.f;
    int32_t _drag_iv[2] = {0, 0};
    int _drag_i = 0;
    float _repeat_phase_samples = 0.f;
    float _repeat_period_samples = 0.f;
    bool _drag_active = false;

    float _thin = 0.f;
    int32_t _rhy_gap[2] = {0, 0};
    bool _rhy_valid = false;
    int _thin_n[2] = {1, 1};
    int _thin_i = 0;
    int _thin_count = 0;
    float _gate = 1.f;
    float _gate_target = 1.f;
    float _gate_coef = 1.f;
};

} // namespace spky
