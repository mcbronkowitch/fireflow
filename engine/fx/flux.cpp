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
    _gate_coef = daisysp::fmin(1.f / (link_tuning::kGateRampS * sample_rate), 1.f);
    // set_link's guard compares against _link, and this reset also zeroes
    // _drag and _thin below -- so _link must land on the value that
    // actually MATCHES that reset state, which is 0, not an unreachable
    // sentinel like _drive_norm/_stages_norm use below. Those two guard a
    // 0..1 knob, where -1 is outside the range and therefore guaranteed to
    // differ from the next legitimate push; LINK's range is -1..1, so -1 is
    // a real value a user could be sitting on and would wrongly swallow a
    // repeated push of it. 0.f is correct here precisely because it IS the
    // neutral state this reset just put the derived halves into: any other
    // knob position the user pushes next will correctly differ from it, and
    // if they push 0 again that's a no-op on an already-neutral Flux, not a
    // swallowed change. Without this, a re-init (Spotymod::reinit(), called
    // from a sample-rate change, a reset, or a fresh sampler add) leaves
    // _link at whatever the knob last was while _drag/_thin have already
    // been zeroed, so the next pushParams(same value) is silently swallowed
    // and LINK stays dead until the knob physically moves.
    _link = 0.f;
    _thin = 0.f;
    _rhy_gap[0] = _rhy_gap[1] = 0;
    _rhy_valid = false;
    _thin_n[0] = _thin_n[1] = 1;
    _thin_i = _thin_count = 0;
    _gate = _gate_target = 1.f;
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
    update_thin_pattern();
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

// The neighbour's gaps, in whole repeats of the CURRENT ladder time. Rounded,
// not truncated: a gap of 3.6 repeats is heard as 4, not 3.
void Flux::update_thin_pattern() {
    const float rep = _delay_time * _sr;
    for (int i = 0; i < 2; ++i) {
        int n = 1;
        if (rep > 0.f)
            n = static_cast<int>(static_cast<float>(_rhy_gap[i]) / rep + 0.5f);
        if (n < 1) n = 1;                                  // shorter than one
        if (n > link_tuning::kMaxSkip) n = link_tuning::kMaxSkip;
        _thin_n[i] = n;
    }
}

// One repeat further into the pattern. The FIRST repeat of each interval
// sounds and the rest are ducked, which puts the audible event on the
// neighbour's onset rather than before it.
void Flux::advance_gate() {
    ++_thin_count;
    _gate_target = (_thin_count == 1) ? 1.f : (1.f - _thin);
    if (_thin_count >= _thin_n[_thin_i]) {
        _thin_count = 0;
        _thin_i ^= 1;
    }
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
    const bool thinning = (_thin > 0.f && _rhy_valid);
    // Owned by thinning, not by which branch below is taken: the DRAG branch
    // never touches the gate, so if the reset lived only in the !thinning arm
    // of the branch below, crossing the knob from THIN straight to DRAG
    // (without a push landing exactly on 0) would leave a stale duck target
    // in force for as long as DRAG stayed engaged -- the gate branch in
    // process() never switches itself off because _gate never reaches 1.
    if (!thinning) {
        _gate_target = 1.f;
        // Engagement starts at the pattern's first repeat, not wherever the
        // last run of thinning left off -- otherwise re-engaging resumes
        // mid-interval and the pattern is not reproducible push to push.
        _thin_count = 0;
        _thin_i = 0;
    }
    if (_drag <= 0.f || !_drag_active) {
        _dt_target = _delay_time;
        // Thinning needs the same accumulator, stepping on the LADDER time --
        // the clock never moves on that half, which is the entire point.
        _drag_step_len = thinning ? _delay_time * _sr : 0.f;
        if (!thinning) _drag_phase = 0.f;
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
    _thin = n < 0.f ? -n : 0.f;
    apply_drag();
}

void Flux::set_rhythm(const RhythmView& rv) {
    if (!_buf_ok) return;
    // The raw gaps and validity feed the thinning half, which does NOT go
    // through derive_intervals. Stored before the guard below, which only
    // knows about the DRAG intervals.
    _rhy_gap[0] = rv.gap[0];
    _rhy_gap[1] = rv.gap[1];
    _rhy_valid  = rv.valid;
    update_thin_pattern();

    int32_t iv[2];
    derive_intervals(rv, iv);
    const bool active = (iv[0] != drag_tuning::kNone && iv[1] != drag_tuning::kNone);
    if (active == _drag_active && iv[0] == _drag_iv[0] && iv[1] == _drag_iv[1]) {
        // The guard only knows about the DRAG intervals, but _rhy_valid may
        // have just changed above it, and apply_drag is where the thinning
        // half arms the shared accumulator. It has to run. It costs nothing:
        // thinning implies _drag == 0, which takes apply_drag's inert branch,
        // and that branch leaves _drag_phase alone while thinning is running.
        if (_thin > 0.f) apply_drag();
        return;
    }
    _drag_iv[0] = iv[0];
    _drag_iv[1] = iv[1];
    _drag_active = active;
    if (!active) _drag_i = 0;   // apply_drag() below owns _drag_phase
    apply_drag();
}

void Flux::process(float& l, float& r) {
    if (!_buf_ok) return;
    float send = _sw.process();
    if (_sw.is_idle()) return;   // fully off: bit-exact dry

    // One accumulator, two consumers. They are mutually exclusive by
    // construction: _drag and _thin come from opposite signs of one knob.
    const bool thinning = (_thin > 0.f && _rhy_valid);
    const bool dragging = (_drag > 0.f && _drag_active);
    if (thinning || dragging) {
        _drag_phase += 1.f;
        if (_drag_step_len > 0.f && _drag_phase >= _drag_step_len) {
            _drag_phase = 0.f;
            if (dragging) { _drag_i ^= 1; apply_drag(); }
            else            advance_gate();
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

    float el = _echo_l.Process(l * send, hz);
    float er = _echo_r.Process(r * send, hz);
    // The gate keeps running after thinning disengages so the gain ramps back
    // to unity instead of stepping; the snap is what lets the branch switch
    // itself off again and restore the bit-exact path (fonepole never quite
    // arrives -- the same reason _stage_current carries a snap above). The
    // tolerance has to clear the float32 stall floor, not just be "small":
    // measured 4e-6 residual at 48 kHz and ~1.7e-5 at 192 kHz (the stall
    // floor scales with sample rate, since _gate_coef shrinks as 1/sr while
    // the ULP near 1.0 does not), so 1e-6 never actually catches it and this
    // branch could never switch itself off again. 1e-4 clears both with
    // margin and is still 80 dB below unity -- inaudible.
    if (thinning || _gate != 1.f) {
        daisysp::fonepole(_gate, _gate_target, _gate_coef);
        if (std::fabs(_gate - 1.f) < 1e-4f) _gate = 1.f;
        el *= _gate;
        er *= _gate;
    }
    l += el * _mix_lin;
    r += er * _mix_lin;
}
