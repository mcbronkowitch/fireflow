#include "fx/flux.h"
#include "util/math.h"

using namespace spky;

namespace {
inline float dbfs2lin(float db) { return daisysp::pow10f(db * 0.05f); }
}

void Flux::init(float sample_rate, float* left, float* right) {
    _sw.init(sample_rate);
    _sr = sample_rate;
    // Construct the tape-time LUT before a null-memory return so a later
    // audio-thread modulation push never performs the table's one-time pow.
    _time_mod_norm = 0.5f;
    _time_mult = tape_time_mult(_time_mod_norm);
    _buf_ok = left != nullptr && right != nullptr;
    if (!_buf_ok) return;

    _echo_l.Init(sample_rate, left);
    _echo_r.Init(sample_rate, right);
    _dt_coef = daisysp::fmin(1.f / (0.03f * sample_rate), 1.f);
    _gate_coef = daisysp::fmin(1.f / (link_tuning::kGateRampS * sample_rate), 1.f);
    _rate_idx = 3;
    _bpm = 120.f;
    _repeat_phase_samples = 0.f;
    _repeat_period_samples = 0.f;
    _link = 0.f;
    _thin = 0.f;
    _rhy_gap[0] = _rhy_gap[1] = 0;
    _rhy_valid = false;
    _thin_n[0] = _thin_n[1] = 1;
    _thin_i = _thin_count = 0;
    _gate = _gate_target = 1.f;
    _fb_norm = -1.f;
    recompute_time(true);
    set_feedback(0.45f);
    set_mix(0.5f);
}

void Flux::set_bpm(float bpm) {
    if (bpm == _bpm) return;
    _bpm = bpm;
    recompute_time(false);
}

void Flux::set_rate(int slice_idx) {
    if (slice_idx == _rate_idx) return;
    _rate_idx = slice_idx;
    recompute_time(false);
}

void Flux::recompute_time(bool immediate) {
    if (!_buf_ok) return;
    const int slice = _rate_idx < 0 ? 0
                    : (_rate_idx >= kFluxRateCount ? kFluxRateCount - 1 : _rate_idx);
    const float hz = division_hz(kFluxRateOffset + slice, _bpm);
    _delay_time = hz > 0.f ? 1.f / hz : 0.5f;
    update_thin_pattern();
    update_time_target(immediate);
}

void Flux::update_time_target(bool immediate) {
    const float max_s = static_cast<float>(kTapeSamples - 2) / _sr;
    _dt_target = clampf(_delay_time * _time_mult, 1.f / _sr, max_s);
    if (immediate) _dt_current = _dt_target;
    refresh_repeat_scheduler();
}

void Flux::set_feedback(float norm) {
    if (!_buf_ok) return;
    const float n = clampf(norm, 0.f, 1.f);
    if (n == _fb_norm) return;
    _fb_norm = n;
    const float feedback = n * 1.2f;
    _echo_l.SetFeedback(feedback);
    _echo_r.SetFeedback(feedback);
}

void Flux::set_mix(float norm) {
    if (!_buf_ok) return;
    _mix_lin = dbfs2lin(daisysp::fmap(clampf(norm, 0.f, 1.f), -40.f, 0.f));
}

void Flux::set_time_mod(float norm) {
    const float n = clampf(norm, 0.f, 1.f);
    if (n == _time_mod_norm) return;
    _time_mod_norm = n;
    _time_mult = tape_time_mult(n);
    if (!_buf_ok) return;
    update_time_target(false);
}

void Flux::update_thin_pattern() {
    const float rep = _delay_time * _sr;
    for (int i = 0; i < 2; ++i) {
        int n = 1;
        if (rep > 0.f)
            n = static_cast<int>(static_cast<float>(_rhy_gap[i]) / rep + 0.5f);
        if (n < 1) n = 1;
        if (n > link_tuning::kMaxSkip) n = link_tuning::kMaxSkip;
        _thin_n[i] = n;
    }
}

void Flux::advance_gate() {
    ++_thin_count;
    _gate_target = (_thin_count == 1) ? 1.f : (1.f - _thin);
    if (_thin_count >= _thin_n[_thin_i]) {
        _thin_count = 0;
        _thin_i ^= 1;
    }
}

void Flux::refresh_repeat_scheduler() {
    if (_thin > 0.f && _rhy_valid) {
        // Stable musical grid: FXT time modulation moves the tape head but does
        // not reset this counter every sample.
        _repeat_period_samples = _delay_time * _sr;
    } else {
        _repeat_period_samples = 0.f;
        _repeat_phase_samples = 0.f;
    }
}

void Flux::set_link(float norm) {
    if (!_buf_ok) return;
    const float n = clampf(norm, 0.f, 1.f);
    if (n == _link) return;
    _link = n;
    _thin = n;
    const bool thinning = (_thin > 0.f && _rhy_valid);
    if (!thinning) {
        _gate_target = 1.f;
        _thin_count = 0;
        _thin_i = 0;
    }
    refresh_repeat_scheduler();
}

void Flux::set_rhythm(const RhythmView& rv) {
    if (!_buf_ok) return;
    _rhy_gap[0] = rv.gap[0];
    _rhy_gap[1] = rv.gap[1];
    _rhy_valid = rv.valid;
    update_thin_pattern();
    if (!_rhy_valid) {
        _gate_target = 1.f;
        _thin_count = 0;
        _thin_i = 0;
    }
    refresh_repeat_scheduler();
}

void Flux::flush_lines() {
    // One memset per switch-off (2 x 1 MB). Desktop-cheap; the Daisy shell
    // will need an amortized clear before this engine runs on hardware.
    _echo_l.Reset();
    _echo_r.Reset();
    _line_dirty = false;
}

void Flux::process(float& l, float& r) {
    if (!_buf_ok) return;
    const bool was_idle = _sw.is_idle();
    const float k = _sw.process();
    if (_sw.is_idle()) {
        // The fall just completed. PartFx stops calling us the moment
        // engaged() drops, so this transition sample is the last chance
        // to flush the stale take (audit finding 2: a frozen line used
        // to replay, full-level, on the next ON).
        if (!was_idle && _line_dirty) flush_lines();
        return;
    }

    const bool thinning = _thin > 0.f && _rhy_valid;
    if (thinning) {
        _repeat_phase_samples += 1.f;
        if (_repeat_period_samples > 0.f &&
            _repeat_phase_samples >= _repeat_period_samples) {
            _repeat_phase_samples = 0.f;
            advance_gate();
        }
    }

    daisysp::fonepole(_dt_current, _dt_target, _dt_coef);
    const float samples = _dt_current * _sr;
    // The line always hears the live input; the RAMP rides the return —
    // the signal that is actually audible (GRIT's pattern, grit.cpp:99).
    // The old form gated the send, i.e. audio already in the past.
    float wet_l = _echo_l.Process(l, samples);
    float wet_r = _echo_r.Process(r, samples);
    _line_dirty = true;
    if (thinning || _gate != 1.f) {
        daisysp::fonepole(_gate, _gate_target, _gate_coef);
        if (std::fabs(_gate - 1.f) < 1e-4f) _gate = 1.f;
        wet_l *= _gate;
        wet_r *= _gate;
    }
    l += wet_l * _mix_lin * k;
    r += wet_r * _mix_lin * k;
}
