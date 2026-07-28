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
    _drag = 0.f;
    _drag_iv[0] = _drag_iv[1] = 0;
    _drag_i = 0;
    _drag_phase = 0.f;
    _drag_step_len = 0.f;
    _drag_active = false;
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
    apply_drag();
    if (immediate) _dt_current = _dt_target;
}

void Flux::set_feedback(float norm) {
    if (!_buf_ok) return;
    _fb_norm = clampf(norm, 0.f, 1.f);
    apply_feedback();
}

// FEEDBACK's coefficient, with DRIVE's gain divided back out.
//
// BbdEcho's saturator sits INSIDE the loop, so its gain multiplies the loop
// gain directly: the loop sees feedback * g. That is what a real BBD does, and
// BbdEcho stays that faithful part -- but it means the FEEDBACK knob addresses
// a different circuit at every DRIVE setting. Measured on the coupled law
// (350 ms, 4096 stages): the knob position producing a 15 s tail slid from
// 0.57 at DRIVE 0 to 0.14 at DRIVE 1, and from a quarter DRIVE upward most of
// FEEDBACK's travel was runaway. Dividing here restores the plan's original
// intent -- "FEEDBACK must not mean something different at every DRIVE" --
// which an earlier attempt had bought by shrinking the saturator's CEILING
// instead, costing 14 dB of echo level and making DRIVE inaudible. This
// touches neither sat_out_ nor the input path: the first repeat's level and
// distortion still rise across the whole knob, unchanged (measured peak
// 0.512 -> 1.279). Only the tail length stops moving.
//
// The residual is deliberate: the fed-back signal enters the saturator 1/g
// quieter, so repeats compound less dirt than the coupled law gave. The first
// repeat -- which carries the audible DRIVE cue -- is untouched.
//
// Flux, not BbdEcho, is the right home. Flux is already the layer that turns
// musical intent into physics (BPM and divisions into a delay time, a 0..1
// knob into a stage count); this is the same kind of mapping. BbdEcho remains
// a plain BBD whose loop gain honestly equals feedback * g.
void Flux::apply_feedback() {
    // Up to ~120 %: self-oscillation stays reachable, documented behaviour of
    // the original. The bound now comes from saturation WITHIN the loop
    // (BbdEcho) rather than a fast_tanh on the read path.
    const float fb = _fb_norm * 1.2f / bbd_drive_gain(_drive_norm);
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
    // DRIVE just moved the loop gain, so the feedback coefficient that keeps
    // the bloom point fixed moved with it. Order-independent: a host may push
    // these two in either order, and every DRIVE change re-derives FEEDBACK
    // from the knob position rather than from the coefficient now in force.
    apply_feedback();
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

// The single writer of _dt_target.
//
// Geometric, not linear: pitch tracks the clock RATIO directly, so a linear
// blend would put the perceived midpoint in the wrong place. Same reasoning
// that gave the modulation lane its x1/4..x4 mapping.
//
// _drag_step_len is the step's length in SAMPLES of the interpolated time, not
// of the neighbour's raw interval -- the echo's repeat interval is what is
// actually in force, so stepping on it is what makes "one interval per repeat"
// true at every DRAG setting rather than only at 1.
void Flux::apply_drag() {
    if (_drag <= 0.f || !_drag_active) {
        _dt_target = _delay_time;
        _drag_step_len = 0.f;
        _drag_phase = 0.f;
        return;
    }
    const float target = static_cast<float>(_drag_iv[_drag_i]) / _sr;
    _dt_target = std::pow(_delay_time, 1.f - _drag) * std::pow(target, _drag);
    _drag_step_len = _dt_target * _sr;
}

void Flux::set_link(float norm) {
    if (!_buf_ok) return;
    const float n = clampf(norm, -1.f, 1.f);
    if (n == _link) return;      // apply_drag runs two powf; do not run per push
    _link = n;
    _drag = n > 0.f ? n : 0.f;
    apply_drag();
}

void Flux::set_rhythm(const RhythmView& rv) {
    if (!_buf_ok) return;
    int32_t iv[2];
    derive_intervals(rv, iv);
    const bool active = (iv[0] != drag_tuning::kNone && iv[1] != drag_tuning::kNone);
    if (active == _drag_active && iv[0] == _drag_iv[0] && iv[1] == _drag_iv[1]) return;
    _drag_iv[0] = iv[0];
    _drag_iv[1] = iv[1];
    _drag_active = active;
    if (!active) { _drag_i = 0; _drag_phase = 0.f; }
    apply_drag();
}

void Flux::process(float& l, float& r) {
    if (!_buf_ok) return;
    float send = _sw.process();
    if (_sw.is_idle()) return;   // fully off: bit-exact dry

    // DRAG's step. One add and one compare per sample when engaged, nothing
    // but the compare when it is not -- which is what keeps DRAG 0 on the
    // same path it has always been on.
    if (_drag > 0.f && _drag_active) {
        _drag_phase += 1.f;
        if (_drag_phase >= _drag_step_len) {
            _drag_phase = 0.f;
            _drag_i ^= 1;
            apply_drag();
        }
    }

    // Both slews advance exactly ONCE per sample, before anything reads them.
    daisysp::fonepole(_dt_current, _dt_target, _dt_coef);
    daisysp::fonepole(_stage_current, _stage_target, _dt_coef);
    // fonepole's float32 recurrence stalls before full convergence at large
    // magnitudes -- at kMaxStages (16384) its ULP is comparable to the
    // shrinking per-sample increment near the target, so it never quite gets
    // there (measured: settles ~0.7 stages short). Stage count is an integer
    // quantity, so snapping once the slew is within one stage of its target
    // is below anything downstream can observe, and it is what makes
    // kMaxStages an actually reachable STAGES setting rather than a name
    // fully clockwise can never produce.
    if (std::fabs(_stage_target - _stage_current) < 1.f) _stage_current = _stage_target;

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
