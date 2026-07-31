#include "parts/bbd_engine.h"

namespace spky {

namespace {
// The dither floor. A few LSBs, injected at the write tick -- see
// BbdLine::SetDither for why the engine needs a noise floor the model does not
// otherwise have.
constexpr float kDither = 4e-5f;
}  // namespace

void BbdEngine::init(float sample_rate) {
    _sr = sample_rate > 0.f ? sample_rate : 48000.f;
    _l.Init(_sr, nullptr, 0);
    _r.Init(_sr, nullptr, 0);
    _buf_ok = false;
    _win = bbd_music::window(_cycle);
    _recompute();
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
    _recompute();
}

void BbdEngine::reset() {
    _l.Reset();
    _r.Reset();
    _in_l = 0.f;
    _in_r = 0.f;
}

void BbdEngine::set_cycle(float seconds) {
    _cycle = seconds > 0.f ? seconds : 1.f;
    _recompute();
}

void BbdEngine::set_targets(const float* t, float /*tune*/) {
    // SOURCE -> DRIVE, the dirt inside the loop. Both setters carry unchanged-
    // value guards inside BbdEcho's callee; putting DRIVE on a lane defeats
    // them permanently, which is a handful of transcendentals per block.
    const float drive = clampf(t[LANE_SOURCE], 0.f, 1.f);
    _l.SetDrive(drive);
    _r.SetDrive(drive);
    // MOTION -> FEEDBACK. Flux's law, kept: without dividing bbd_drive_gain
    // back out the bloom point slides from 0.57 to 0.14 across DRIVE, and since
    // LANE_SOURCE *is* DRIVE the plane would drive the loop through
    // self-oscillation via a lane that is not the feedback lane.
    const float fb = clampf(t[LANE_MOTION], 0.f, 1.f) * 1.2f
                     / bbd_drive_gain(drive);
    _l.SetFeedback(fb);
    _r.SetFeedback(fb);

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
    } else if (_latched) {
        _f_clk = bbd_music::clock_step(_win, _pitch);
        _latched = false;
    }
    // Either way the stage count follows, so SIZE keeps moving the rhythm
    // between fires while the clock -- and therefore the pitch -- holds.
    _stages = bbd_music::stages_for(_win, _f_clk);
    _l.SetStages(_stages);
    _r.SetStages(_stages);
}

void BbdEngine::process_in(float inL, float inR) {
    _in_l = inL;
    _in_r = inR;
}

void BbdEngine::process(float& outL, float& outR) {
    if (!_buf_ok) { outL = 0.f; outR = 0.f; return; }
    const float wl = _l.Process(_in_l, _f_clk);
    const float wr = _r.Process(_in_r, _f_clk);
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
