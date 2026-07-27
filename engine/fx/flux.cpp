#include "fx/flux.h"
#include "util/math.h"

using namespace spky;

namespace {
inline float dbfs2lin(float db) { return daisysp::pow10f(db * 0.05f); }
constexpr float kBootStagesNorm = 0.8f;   // 512 * 32^0.8 == 8192, the DMM
}

void Flux::init(float sample_rate, float* buf_l, float* buf_r) {
    _sw.init(sample_rate);
    _sr = sample_rate;
    _buf_ok = (buf_l != nullptr && buf_r != nullptr);
    if (!_buf_ok) return;
    _echo_l.Init(sample_rate, buf_l, kMaxSamples);
    _echo_r.Init(sample_rate, buf_r, kMaxSamples);
    // Short slew: click-free division changes, locks to grid (~30 ms lag).
    _dt_coef = daisysp::fmin(1.f / (0.03f * sample_rate), 1.f);
    _rate_idx = 3;               // boot "1/4"
    _bpm = 120.f;
    _time_mult = 1.f;
    // Both guards restart from an unreachable value: BbdEcho::Init has just
    // reset the drive to 0 and the stage count to its own default, so a
    // repeated push of the value the user already had dialled in must NOT be
    // swallowed. Same trap the tape-era DUST/ROT guards carried, same fix.
    _drive_norm = -1.f;
    _stages_norm = -1.f;
    set_stages(kBootStagesNorm);
    _stage_current = _stage_target;
    _stages_now = static_cast<int>(_stage_current + 0.5f);
    _echo_l.SetStages(_stages_now);
    _echo_r.SetStages(_stages_now);
    recompute_time(true);        // snap the boot delay time
    set_feedback(0.45f);
    set_mix(0.5f);
    set_drive(0.f);
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
    int slice = _rate_idx < 0 ? 0
              : (_rate_idx >= kFluxRateCount ? kFluxRateCount - 1 : _rate_idx);
    float hz = division_hz(kFluxRateOffset + slice, _bpm);
    float t = (hz > 0.f) ? 1.f / hz : 0.5f;
    // The buffer-safety clamp is GONE: delay time is no longer bounded by
    // buffer length, only by how dark the user is willing to go. The 60 s
    // ceiling is a sanity bound against a pathological tempo, not a musical
    // limit -- at 512 stages that is already a 4.3 Hz clock.
    _delay_time = clampf(t, 0.001f, 60.f);
    _dt_target = _delay_time;
    if (immediate) _dt_current = _delay_time;
}

void Flux::set_feedback(float norm) {
    if (!_buf_ok) return;
    // Up to ~120 %: self-oscillation stays reachable, documented behaviour of
    // the original. The bound now comes from saturation WITHIN the loop
    // (BbdEcho) rather than a fast_tanh on the read path.
    float fb = clampf(norm, 0.f, 1.f) * 1.2f;
    _echo_l.SetFeedback(fb);
    _echo_r.SetFeedback(fb);
}

void Flux::set_mix(float norm) {
    if (!_buf_ok) return;
    _mix_lin = dbfs2lin(daisysp::fmap(clampf(norm, 0.f, 1.f), -40.f, 0.f));
}

void Flux::set_drive(float norm) {
    if (!_buf_ok) return;
    const float d = clampf(norm, 0.f, 1.f);
    if (d == _drive_norm) return;
    _drive_norm = d;
    _echo_l.SetDrive(d);
    _echo_r.SetDrive(d);
}

void Flux::set_stages(float norm) {
    if (!_buf_ok) return;
    const float n = clampf(norm, 0.f, 1.f);
    if (n == _stages_norm) return;
    _stages_norm = n;
    // Geometric: 512 * (16384/512)^n == 512 * 32^n. Five octaves of
    // brightness at fixed delay time -- grainy, dark and image-rich at the
    // bottom, clean and fast at the top. Physically this is swapping the
    // chip; no pedal exposes it. Control rate, behind the guard above.
    _stage_target = static_cast<float>(kMinStages)
                  * std::pow(static_cast<float>(kMaxStages) / kMinStages, n);
}

void Flux::set_time_mod(float norm) {
    _time_mult = bbd_time_mult(norm);
}

void Flux::process(float& l, float& r) {
    if (!_buf_ok) return;
    float send = _sw.process();
    if (_sw.is_idle()) return;   // fully off: bit-exact dry

    // Both slews advance exactly ONCE per sample, before anything reads them.
    daisysp::fonepole(_dt_current, _dt_target, _dt_coef);
    daisysp::fonepole(_stage_current, _stage_target, _dt_coef);

    const int stages = static_cast<int>(_stage_current + 0.5f);
    if (stages != _stages_now) {
        _stages_now = stages;
        _echo_l.SetStages(stages);
        _echo_r.SetStages(stages);
    }

    // Base clock from the ladder, then the lane pulls multiplicatively on it,
    // then the ceiling -- applied AFTER the lane, so ladder and lane pushing
    // together still cannot overrun it.
    const float hz = clampf(bbd_clock_hz(_dt_current, stages) * _time_mult,
                            0.f, bbd_tuning::kClockMaxHz);
    _clock_hz = hz;

    l += _echo_l.Process(l * send, hz) * _mix_lin;
    r += _echo_r.Process(r * send, hz) * _mix_lin;
}
