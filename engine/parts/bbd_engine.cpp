#include "parts/bbd_engine.h"

namespace spky {

namespace {
// The dither floor. A few LSBs, injected at the write tick -- see
// BbdLine::SetDither for why the engine needs a noise floor the model does not
// otherwise have.
constexpr float kDither = 4e-5f;

// k0: the freeze's unity reference, measured with broadband material against
// the per-octave criterion in tests/test_bbd_engine.cpp ("the freeze holds a
// broadband burst per octave"). DECAY trims BELOW it; the acceptance test runs
// at DECAY maximum. Operating point: div 1/2, T = 500 ms, DRIVE swept 0 -> 1,
// DECAY 1, PITCH 0.5 in STEP (f_clk = 1448 Hz, 1448 stages).
//
// Bisected for zero MEAN across the six probe bands; the full trial table is in
// .superpowers/sdd/2026-07-31-bbd-part-engine/task-6-report.md. Read that
// before touching this number: the criterion is MISSED (2-3 of 6 bands outside
// +-1 dB, and the fit does not generalise across noise realisations), and the
// report says why that is structural rather than a matter of a better k0.
constexpr float kFreezeGain = 0.93125f;

// The feedback-path tilt at the freeze's neutral point -- the value that
// inverts kLossCoef so the line's round trip is flat to ~1 and the compander's
// L^2 becomes harmless. RESONANCE (a later movement) plays the same filter and
// takes this as its centre, which is why it is named rather than inlined.
// Measured at the same operating point as kFreezeGain, by minimising the
// SPREAD across the six probe bands with the gain re-bisected to zero mean at
// each tilt. The minimum is shallow and lands at 6.15 dB, not the <= 2 dB the
// criterion needs -- again, see the report.
constexpr float kFreezeTilt = 0.44f;

// ATTACK's endpoints: how long the freeze takes to engage and release.
constexpr float kFreezeRampMinS = 0.002f;
constexpr float kFreezeRampMaxS = 2.0f;

// How far the ramp has to travel before it re-pushes the freeze into the two
// lines. The crossfade itself is per-sample -- it gates the input, which is one
// multiply -- but _apply_freeze() reaches BbdEcho::SetFeedbackTilt, whose
// corner is an exp(), and engine/** may not call libm per sample.
//
// Quantised in TRAVEL rather than in samples so the bound holds at both ends of
// ATTACK: any ramp, 2 ms or 2 s, costs at most 64 pushes (128 exp) in total,
// where a per-sample push would cost 96 at the fast end and 96000 at the slow
// one. Both endpoints are pushed exactly, on the sample they are reached, so a
// settled freeze is never approximate -- only the moving ramp is a staircase,
// and 64 steps of it is finer than the input crossfade beside it needs.
constexpr float kFreezePushStep = 1.f / 64.f;
}  // namespace

void BbdEngine::init(float sample_rate) {
    _sr = sample_rate > 0.f ? sample_rate : 48000.f;
    _l.Init(_sr, nullptr, 0);
    _r.Init(_sr, nullptr, 0);
    _buf_ok = false;
    // The ramp rate is in samples, so it only becomes real once _sr is.
    _freeze_step = 1.f / (_freeze_ramp_s * _sr);
    // _refresh_window() derives _win from _cycle and the ladder's current
    // rung (see _recompute()'s T), superseding the plain window(_cycle) this
    // line used to compute directly.
    _refresh_window();
}

void BbdEngine::init_buffers(float* l, float* r, size_t cells) {
    _buf_ok = (l != nullptr && r != nullptr && cells > 0);
    _l.Init(_sr, l, cells);
    _r.Init(_sr, r, cells);
    // NO per-line seed here, on purpose, and this is the one place to read
    // about it. Both lines run the SAME dither stream, because a mono source
    // through this engine has to come out mono: at zero width, L and R must be
    // bit-identical, and two independent noise streams would break that in a
    // way no listener could name -- a mono signal that quietly is not mono any
    // more, with nothing on the panel to explain it.
    //
    // BbdLine::Reset (fx/bbd.h) seeds its own Rng from one fixed constant, so
    // both lines already agree, and they go on agreeing across every
    // BbdEngine::reset() -- which Part::_engine_swap runs on every activation,
    // i.e. on the only path by which this engine is ever heard. Seeding the two
    // lines apart HERE would therefore not even hold: reset() would put them
    // back in step at the first swap. That is exactly the trap this comment
    // replaces.
    //
    // Scope, so the next reader does not over-read it: this says the NOISE
    // SOURCE is common to both channels. It is not a claim that the two
    // channels stay identical in general -- what decorrelates them is COLOR
    // moving their clocks apart, which is a later movement's work and acts on
    // the signal path, not on the dither.
    _l.SetDither(kDither);
    _r.SetDither(kDither);
    _refresh_window();
}

void BbdEngine::reset() {
    _l.Reset();
    _r.Reset();
    _in_l = 0.f;
    _in_r = 0.f;
    // The charge is gone, so a crossfade that was part-way into holding it is
    // meaningless. Part::_engine_swap() re-pushes set_hold()/set_gate()
    // immediately after this call, so the deck's actual current gate is not
    // lost -- it simply engages from zero, which is what a cleared line wants.
    _freeze = 0.f;
    _freeze_want = 0.f;
    _freeze_last = 0.f;
    _choked = false;
    // Re-arm the cold-start latch: Part::_engine_swap() runs reset() on every
    // activation, and a deck reactivated into STEP must not be stuck on
    // whatever clock this engine happened to hold from its PREVIOUS
    // activation (or its construction, if this is the first one) until a
    // real STEP fire arrives -- the same reasoning as the member default.
    _latched = true;
}

void BbdEngine::set_cycle(float seconds) {
    _cycle = seconds > 0.f ? seconds : 1.f;
    _refresh_window();
}

void BbdEngine::set_targets(const float* t, float /*tune*/) {
    // SOURCE -> DRIVE, the dirt inside the loop. Both setters carry unchanged-
    // value guards inside BbdEcho's callee; putting DRIVE on a lane defeats
    // them permanently, which is a handful of transcendentals per block.
    _drive = clampf(t[LANE_SOURCE], 0.f, 1.f);
    _l.SetDrive(_drive);
    _r.SetDrive(_drive);
    // MOTION -> FEEDBACK. Flux's law, kept: without dividing bbd_drive_gain
    // back out the bloom point slides from 0.57 to 0.14 across DRIVE, and since
    // LANE_SOURCE *is* DRIVE the plane would drive the loop through
    // self-oscillation via a lane that is not the feedback lane.
    //
    // Held, not pushed: the freeze crossfades from this value to k0, so
    // _apply_freeze() -- reached below via _recompute() -- is the only writer.
    _fb_lane = clampf(t[LANE_MOTION], 0.f, 1.f) * 1.2f / bbd_drive_gain(_drive);

    _mix = clampf(t[LANE_LEVEL], 0.f, 1.f);
    _pitch = clampf(t[LANE_PITCH], 0.f, 1.f);
    _ladder.process(clampf(t[LANE_SIZE], 0.f, 1.f));
    _recompute();
}

void BbdEngine::_recompute() {
    const float T = _cycle * bbd_music::kDivs[_ladder.index()];
    _win = bbd_music::window(T);
    if (_flow) {
        _f_clk = bbd_music::clock_flow(_win, _pitch);
        // FLOW has genuinely tracked a real pitch now, so the cold-start
        // grace is spent: without this, an engine that spends its first
        // while in FLOW (where _latched is simply never consulted) would
        // still show _latched == true the moment it later switches to STEP,
        // and the very next set_targets() would snap to the STEP grid with
        // no STEP fire of its own -- the leak the guard in latch_clock() is
        // there to prevent, just arriving from construction instead of from
        // a stray latch_clock() call during FLOW.
        _latched = false;
    } else if (_latched) {
        _f_clk = bbd_music::clock_step(_win, _pitch);
        _latched = false;
    }
    // Either way the stage count follows, so SIZE keeps moving the rhythm
    // between fires while the clock -- and therefore the pitch -- holds.
    _stages = bbd_music::stages_for(_win, _f_clk);
    _l.SetStages(_stages);
    _r.SetStages(_stages);
    // This is the one path on which _f_clk can move, and the freeze's tilt
    // corner tracks _f_clk/4 -- so it is also the one structural path that has
    // to re-push it. It doubles as set_targets()' push of _drive/_fb_lane,
    // which are written just above the call there.
    _apply_freeze();
}

// See the declaration in bbd_engine.h for why this is split from _recompute().
void BbdEngine::_refresh_window() {
    const float T = _cycle * bbd_music::kDivs[_ladder.index()];
    _win = bbd_music::window(T);
    _stages = bbd_music::stages_for(_win, _f_clk);
    _l.SetStages(_stages);
    _r.SetStages(_stages);
}

void BbdEngine::set_gate(bool on) {
    // STEP only. A FLOW deck's gate is effectively always on, so honouring it
    // there would leave a FLOW BBD permanently frozen -- and with the freeze
    // unreachable, ATTACK and DECAY are inert in FLOW. Accepted (spec 5.6),
    // and it belongs in the manual.
    _freeze_want = (!_flow && on) ? 1.f : 0.f;
}

void BbdEngine::set_hold(bool on) { _choked = on; }

void BbdEngine::set_attack(float n) {
    const float s = kFreezeRampMinS
                    * std::pow(kFreezeRampMaxS / kFreezeRampMinS,
                               clampf(n, 0.f, 1.f));
    _freeze_ramp_s = s;
    _freeze_step = 1.f / (s * _sr);
}

void BbdEngine::set_decay(float n) {
    _decay = clampf(n, 0.f, 1.f);
    _apply_freeze();          // it scales the loop gain, so it lands at once
}

void BbdEngine::_apply_freeze() {
    // The three legs of spec 5.6, none of which the rev. 1 scalar-k scheme had:
    //
    // 1. A DC blocker in the feedback path. The Butterworth sections are
    //    normalised H(0)=1 and the loss pole is unity at DC, so any loop gain
    //    above unity at 1 kHz is strictly above unity at DC and grows until it
    //    parks the saturator.
    // 2. The feedback-path tilt, tracking f_clk/4 -- the same filter RESONANCE
    //    plays. The freeze is RESONANCE at its neutral point, not a separate
    //    mechanism. Flattening the line's gain to ~1 is also what makes the
    //    compander's L^2 round trip harmless: L^2 = L = 1.
    // 3. DRIVE divided out analytically. bbd_drive_gain spans 1.0..3.98 and the
    //    small-signal loop gain IS feedback * g, so with LANE_SOURCE running,
    //    the plane would otherwise swing the loop gain +-12 dB PER CIRCULATION.
    //    This term is exactly known; leaving it to the ear was wrong.
    const bool on = _freeze > 0.f;
    _l.SetFeedbackDcBlock(on);
    _r.SetFeedbackDcBlock(on);
    // The un-frozen endpoint is 0 -- exactly identity in BbdEcho::fb_path, so
    // an engine that is not freezing is bit-exact through the tilt. RESONANCE
    // replaces that 0 with its own value in a later movement; kFreezeTilt is
    // the point it will be centred on.
    const float tilt = _freeze * kFreezeTilt;
    _l.SetFeedbackTilt(tilt, _f_clk * 0.25f);
    _r.SetFeedbackTilt(tilt, _f_clk * 0.25f);
    const float k = kFreezeGain * _decay / bbd_drive_gain(_drive);
    const float fb = _fb_lane + _freeze * (k - _fb_lane);
    _l.SetFeedback(fb);
    _r.SetFeedback(fb);
}

void BbdEngine::process_in(float inL, float inR) {
    _in_l = inL;
    _in_r = inR;
}

void BbdEngine::process(float& outL, float& outR) {
    if (!_buf_ok) { outL = 0.f; outR = 0.f; return; }
    // The freeze ramp. Linear in the crossfade, not in dB: this is a mix
    // between "input open, feedback at the lane" and "input closed, feedback at
    // k0", and both endpoints are already the right shape.
    //
    // Linear rather than the one-pole `_freeze += (want - _freeze) * coef` this
    // was first written as, and the reason is arithmetic, not taste: a one-pole
    // never lands. At ATTACK max the increment (1 - _freeze) / 96000 falls
    // below one float ulp while _freeze is still ~3e-3 short of 1, so it STICKS
    // there -- the input would stay permanently 0.3 % open and the loop sit
    // 0.3 % under k0, forever, with no denormal floor able to see it. A linear
    // step of _freeze_step per sample reaches the endpoint exactly, in exactly
    // _freeze_ramp_s seconds, which is also what ATTACK claims to mean.
    if (_freeze < _freeze_want) {
        _freeze += _freeze_step;
        if (_freeze > _freeze_want) _freeze = _freeze_want;
    } else if (_freeze > _freeze_want) {
        _freeze -= _freeze_step;
        if (_freeze < _freeze_want) _freeze = _freeze_want;
    }
    // ...but the PUSH is control rate -- see kFreezePushStep.
    const float moved = _freeze - _freeze_last;
    if (moved > kFreezePushStep || moved < -kFreezePushStep
        || (moved != 0.f && _freeze == _freeze_want)) {
        _freeze_last = _freeze;
        _apply_freeze();
    }
    const float gate_in = (_choked ? 0.f : 1.f) * (1.f - _freeze);
    const float wl = _l.Process(_in_l * gate_in, _f_clk);
    const float wr = _r.Process(_in_r * gate_in, _f_clk);
    // The engine's stated bound. The expander's 4x ceiling puts the raw return
    // above full scale in the self-oscillating regime (measured 1.387, +2.8
    // dBFS, at DRIVE 1 / FEEDBACK 1 with the bound deleted -- see
    // tests/test_bbd_engine.cpp "the output stays inside its stated bound"),
    // there is no per-deck limiter, and the reverb send taps BEFORE the master
    // one. Same idiom as part.h:350 and part.cpp:385.
    outL = fast_tanh(_in_l + _mix * (wl - _in_l));
    outR = fast_tanh(_in_r + _mix * (wr - _in_r));
}

}  // namespace spky
