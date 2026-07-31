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
// at DECAY maximum.
//
// Operating point: div 1/8 at a 1 s cycle -> T = 125 ms, PITCH 0.9 on the STEP
// grid -> f_clk = 13308 Hz, 3327 stages, DRIVE swept 0 -> 1, DECAY 1. That
// clock is not incidental: it is about the lowest at which all six probe
// octaves (110 .. 3520 Hz) sit below the line's own Nyquist, and a probe above
// f_clk/2 measures fold-around rather than held content.
//
// Bisected for minimum WORST BAND, not zero mean: the gain translates all six
// bands together, so centring on the midrange rather than the mean is what
// minimises the largest error. Achieved 2.26 dB against a floor of
// spread/2 = 2.25; centring on the mean instead cost 0.6 dB. The full trial
// table is in .superpowers/sdd/2026-07-31-bbd-part-engine/task-6-report.md.
constexpr float kFreezeGain = 1.0885f;

// The feedback-path tilt at the freeze's neutral point -- the value that
// flattens the line's round trip as far as one zero can. RESONANCE (a later
// movement) plays the same filter and takes this as its centre, which is why
// it is named rather than inlined.
//
// Measured at the same operating point as kFreezeGain, by minimising the
// SPREAD across the six probe bands with the gain re-centred at each tilt.
// The minimum is shallow and real: 4.50 dB here, 4.52 at 1.28, 4.70 at 1.32,
// 6.56 at 1.0, 16.6 at 0. It does NOT go to zero and cannot -- see the test's
// own comment for the arithmetic (a first-order shelf against the loss pole
// plus a sixth-order fixed chain), which is why the criterion is 2.5 dB.
//
// It is also measured at ONE clock. The corner tracks f_clk/4, but the fixed
// 3600 Hz chain does not move with the clock, so the tilt that neutralises the
// loop at 13.3 kHz is not the one that neutralises it at 1.4 kHz. Spec 9
// already carries residual STAGES-dependence as an open listening question;
// this is that question, measured.
constexpr float kFreezeTilt = 1.30f;

// ATTACK's endpoints: how long the freeze takes to engage and release.
constexpr float kFreezeRampMinS = 0.002f;
constexpr float kFreezeRampMaxS = 2.0f;

// COLOR's width ceiling, in cents of clock spread at COLOR = 1. A STARTING
// POINT left for the ear, not a measured decision -- unlike kFreezeGain/
// kFreezeTilt above, this one was not bisected against anything. It stays
// small on purpose: the stage-count ratio the spread implies is r^2 (see
// _apply_width()), so a few cents already move the stage count -- and hence
// the bandwidth and grain -- audibly, at no rhythmic cost. The owner may move
// this once it is heard on hardware; do not "fix" it by measurement.
constexpr float kWidthMaxCents = 30.f;

// FILT's endpoints, geometric around kLossCoef so the knob centre is EXACTLY
// the physical value -- a knob left alone must change nothing. ASYMMETRIC on
// purpose (code review finding, 2026-07-31): a shared +-2-octave span put the
// NEGATIVE endpoint at a sensible kLossCoef/4 = 0.183, but asked the POSITIVE
// endpoint for kLossCoef*4 = 2.928, which saturates BbdLine::SetLossCoef's
// 0.999 ceiling at t ~= 0.22 -- so the top three quarters of FILT's positive
// travel produced an identical, motionless _loss_a. A knob dead over most of
// one half of its travel is not a knob.
//
// kFiltPosOctaves is DERIVED, not guessed: it solves
// kLossCoef * 2^kFiltPosOctaves == kLossCoefCeiling, so the ceiling is
// reached (up to float rounding) exactly at t = +1, and every positive
// setting up to there actually moves the pole. kFiltNegOctaves keeps the
// original span -- the darkening rate was never in question, only the
// positive side's dead travel -- and is left as an open listening question
// like kWidthMaxCents above, not re-tuned by this fix.
constexpr float kFiltNegOctaves = 2.f;
constexpr float kLossCoefCeiling = 0.999f;   // matches BbdLine::SetLossCoef's own clamp
const float kFiltPosOctaves = std::log2(kLossCoefCeiling / bbd_tuning::kLossCoef);

// DETUNE (menu): the slew the clock chases a moved lane at. Flux records this
// as a deliberate musical value, and since PITCH and SIZE are both driven by
// the plane, it decides HOW the engine answers modulation.
constexpr float kSlewMinS = 0.001f;
constexpr float kSlewMaxS = 0.5f;

}  // namespace

void BbdEngine::init(float sample_rate) {
    _sr = sample_rate > 0.f ? sample_rate : 48000.f;
    _l.Init(_sr, nullptr, 0);
    _r.Init(_sr, nullptr, 0);
    _buf_ok = false;
    // The ramp rate is in samples, so it only becomes real once _sr is.
    _freeze_step = 1.f / (_freeze_ramp_s * _sr);
    // Same reasoning for DETUNE's per-sample multiplier.
    _slew_mul = std::pow(2.f, 1.f / (_slew_s * _sr));
    // The slewed clock starts EXACTLY at the (still-default) target -- a zero
    // would make process()'s glide's target/now ratio non-finite (and,
    // pointlessly, multiplying zero by anything stays zero forever). See
    // bbd_engine.h's member comment.
    _f_now = _f_clk;
    _f_now_r = _f_clk;
    // _refresh_window() derives _win from _cycle and the ladder's current
    // rung (see _recompute()'s T), superseding the plain window(_cycle) this
    // line used to compute directly.
    _refresh_window();
    // _refresh_window() deliberately does not call _apply_freeze() (see its
    // own comment), so without this call _freeze_k stays at its member
    // default of 0 until the first reset()/set_targets()/set_decay()/
    // set_resonance(). Unreached through Part today -- _engine_swap() always
    // runs reset() before this engine is ever live -- but a BbdEngine
    // constructed directly (a new host, a bench) and gated before any of
    // those four calls would push _push_freeze()'s
    // fb = _fb_lane + _freeze*(_freeze_k - _fb_lane) with _freeze_k == 0,
    // i.e. the freeze would MUTE the loop instead of holding it. Calling
    // _apply_freeze() directly here (not through _refresh_window()) is safe
    // for the same reason reset() already calls it unconditionally: _f_clk,
    // _drive, _fb_lane and _decay are all at their real member defaults by
    // this point, so the tilt/gain it seeds are the correct at-rest values,
    // not placeholders.
    _apply_freeze();
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
    // ...and the two lines have to be TOLD, or they keep the frozen loop's
    // coefficients with the input gate reopened. BbdEcho::Reset() clears only
    // STATE (fb_state_, tilt_z_, dc_x1_/dc_y1_); feedback_, tilt_, tilt_c_ and
    // dc_on_ all survive it. process() cannot repair that on its own either:
    // _freeze == _freeze_last, so nothing has "moved" and the per-sample push
    // never fires. Nothing else reaches _apply_freeze() on the swap path
    // Part::_engine_swap() takes in STEP (reset -> set_flow(false) -> set_hold
    // -> set_gate -> set_cycle, none of which recompute), so without this line
    // a deck swapped away mid-freeze comes back with feedback ~= k0, tilt at
    // kFreezeTilt and the DC blocker on until its first set_targets().
    //
    // _fb_lane and _drive are deliberately NOT cleared here. They are lane
    // PARAMETERS, and this class already lets every other one survive a reset
    // -- _mix, _pitch, _cycle, the ladder rung, _f_clk -- precisely because
    // Part re-pushes them. Zeroing these two and not those would be an
    // inconsistency, and the safety argument for it does not hold: the line
    // was just memset to zero, so there is no charge for a stale feedback
    // coefficient to act on.
    _apply_freeze();
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
    _apply_width();
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
    _apply_width();
}

// See the declaration in bbd_engine.h for why this is shared by _recompute()
// and _refresh_window() rather than living in only one of them.
void BbdEngine::_apply_width() {
    // COLOR: a symmetric geometric clock spread with the delay time held on
    // the grid for BOTH lines. f_L = f*r, f_R = f/r, stage counts scaled to
    // match, so both delays remain T and what differs is the stage count --
    // hence the bandwidth and grain (f_clk/4), plus the comb offset from the
    // sub-sample stage rounding.
    //
    // This deliberately gives up two-tone behaviour, which would need
    // DIFFERENT delay times: at T = 500 ms and 50 cents the lines are 29 ms
    // apart on the first repeat and 232 ms apart by the eighth, and the
    // character changes completely with the division. Consequence to know:
    // at self-oscillation both lines sing at 1/T, i.e. in unison.
    //
    // kWidthMaxCents is deliberately small: the stage-count ratio between the
    // two lines is r^2, so a few cents already give an audible brightness
    // split at no rhythmic cost.
    const float cents = _width * kWidthMaxCents;
    const float r = std::pow(2.f, cents * (1.f / 1200.f));
    _f_l = _f_clk * r;
    _f_r = _f_clk / r;
    _st_l = bbd_music::stages_for(_win, _f_l);
    _st_r = bbd_music::stages_for(_win, _f_r);
    _l.SetStages(_st_l);
    _r.SetStages(_st_r);
}

void BbdEngine::set_gate(bool on) {
    // STEP only, and the `!_flow` is NOT an oversight -- do not "fix" it.
    //
    // A FLOW deck's gate is effectively always on (lane.cpp:447-452: FLOW has
    // no per-step gate, so it always fires), so honouring the gate there would
    // leave a FLOW BBD PERMANENTLY frozen -- the deck would never hear its
    // input again. Removing this guard does not enable a feature; it breaks
    // the mode.
    //
    // The price is stated and accepted: with the freeze unreachable in FLOW,
    // ATTACK and DECAY -- which do nothing BUT shape the freeze -- are dead
    // knobs in that mode. Owner's ruling, 2026-07-31: it stands, and it
    // belongs in the manual rather than in a code change. Spec 5.6.
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

void BbdEngine::set_resonance(float n) {
    // How bright the repeats stay. Neutral inverts kLossCoef's tilt; left,
    // they darken faster than physics; right, they brighten and approach the
    // freeze condition. The same filter the freeze needs, so it costs one
    // biquad that is already there -- _apply_freeze() re-derives the
    // crossfade from this new endpoint immediately.
    _res_tilt = (clampf(n, 0.f, 1.f) - 0.5f) * 2.f * kFreezeTilt;
    _apply_freeze();
}

void BbdEngine::set_sub(float n) { _in_gain = clampf(n, 0.f, 1.f); }

void BbdEngine::set_detune(float n) {
    // GEOMETRIC, because pitch tracks the clock RATIO: a linear map from
    // 1 ms to 500 ms would spend nineteen twentieths of the knob's travel
    // above 25 ms. The MULTIPLIER itself -- not just the seconds figure -- is
    // computed here and stored, because process()'s glide runs every sample
    // and engine/** may not call libm there.
    //
    // Code review finding, 2026-07-31: the first version of this glide stored
    // a coefficient `c` and did `next = now*(1 + c*(target/now - 1))`, which
    // is algebraically an exact `next = now + c*(target-now)` -- a plain
    // one-pole applied directly to Hz, NOT the "equal ratios take equal time"
    // shape this comment already claimed. It reached the pathology it was
    // written to avoid by a different route: the raw-Hz GAP shrinks
    // exponentially regardless of how many octaves are left to cross, so a
    // big jump still spent most of its travel on the first octave and
    // crawled through the last one. Measured over a 500 -> 8000 Hz-scale
    // jump: 787 samples to cross the first octave, 1657 to cross the
    // second -- a ~2.1x mismatch where a geometric glide owes exactly 1x.
    //
    // _slew_s is therefore redefined as the time to cross ONE OCTAVE, and
    // _slew_mul is the per-sample multiplicative step whose
    // (_slew_s * _sr)-th power is exactly 2 -- i.e. multiplying (or, when
    // descending, dividing) by _slew_mul every sample crosses any ratio in a
    // time proportional to its size in octaves, wherever on the clock it
    // starts. This is now a constant-RATE portamento rather than an
    // exponential approach -- arguably the more faithful analogue anyway: an
    // analog VCO's RC glide is exponential in the CONTROL VOLTAGE, and CV is
    // log-frequency, so the hardware's glide is already geometric in Hz.
    const float s = kSlewMinS * std::pow(kSlewMaxS / kSlewMinS, clampf(n, 0.f, 1.f));
    _slew_s = s;
    _slew_mul = std::pow(2.f, 1.f / (s * _sr));
}

void BbdEngine::set_filt(float t) {
    // The loss pole, NOT kFilterHz. kFilterHz is constexpr, baked into
    // butterworth_poles(), and its coefficients live in two file-scope
    // singletons that every BbdLine holds raw pointers into -- one deck's knob
    // would retune the whole instrument, a rebuild is 396 transcendentals, and
    // bbd.h:270-281 documents in-place rebuild as a shared-mutable hazard safe
    // only with the audio callback stopped. The loss pole is a per-line scalar
    // with no rebuild cost, and it is the pole that carries the darkness: at
    // 16384 stages the fixed chain contributes only -0.93 dB at 2.5 kHz.
    //
    // Asymmetric span (kFiltNegOctaves vs kFiltPosOctaves -- see their own
    // comment): the two directions have different room before
    // BbdLine::SetLossCoef's [1e-4, kLossCoefCeiling] clamp bites. At t = 0
    // both branches agree (anything*0 = 0, 2^0 = 1), so the centre stays
    // bit-exact regardless of which span is chosen.
    //
    // No clamp here any more, unlike the first version of this setter.
    // BbdLine::SetLossCoef already clamps to the identical [1e-4, 0.999]
    // range, so clamping again here was pure duplication, not a second line
    // of defence -- both clamps bounded the same range, so the second one
    // could only ever be a no-op. (Code review finding, 2026-07-31.)
    const float c = clampf(t, -1.f, 1.f);
    _loss_a = bbd_tuning::kLossCoef
              * std::pow(2.f, (c >= 0.f ? kFiltPosOctaves : kFiltNegOctaves) * c);
    _l.SetLossCoef(_loss_a);
    _r.SetLossCoef(_loss_a);
}

void BbdEngine::_apply_freeze() {
    // CONTROL RATE. Everything in the freeze that costs libm lives here and in
    // no other path: bbd_drive_gain() is a std::pow, and SetFeedbackTilt()'s
    // corner is a std::exp. Both arguments can only change when the plane or
    // the clock moves, i.e. once per control block -- never inside an audio
    // block, because nothing there recomputes _f_clk or _drive. _push_freeze()
    // below does the per-sample work with neither.
    //
    // The three legs of spec 5.6, none of which the rev. 1 scalar-k scheme had:
    //
    // 1. A DC blocker in the feedback path. The Butterworth sections are
    //    normalised H(0)=1 and the loss pole is unity at DC, so any loop gain
    //    above unity at 1 kHz is strictly above unity at DC and grows until it
    //    parks the saturator.
    // 2. The feedback-path tilt, tracking f_clk/4 -- the same filter RESONANCE
    //    plays. The freeze is RESONANCE at its neutral point, not a separate
    //    mechanism. Flattening the line's gain as far as one zero can is also
    //    what keeps the compander's L^2 round trip from running away.
    // 3. DRIVE divided out analytically. bbd_drive_gain spans 1.0..3.98 and the
    //    small-signal loop gain IS feedback * g, so with LANE_SOURCE running,
    //    the plane would otherwise swing the loop gain +-12 dB PER CIRCULATION.
    //    This term is exactly known; leaving it to the ear was wrong.
    _freeze_k = kFreezeGain * _decay / bbd_drive_gain(_drive);
    // The corner. The amount is re-stated by _push_freeze() a line later --
    // one redundant store, in exchange for _push_freeze() being the single
    // writer of the tilt amount and there being no window in which tilt_ and
    // tilt_c_ disagree about which freeze they belong to.
    //
    // The crossfade's UNFROZEN endpoint is _res_tilt (RESONANCE), not the
    // literal 0 this used to be pinned at -- see set_resonance(). At
    // RESONANCE's centre _res_tilt is 0, so an engine that has never touched
    // RESONANCE and is not freezing is still bit-exact through the tilt, same
    // as before this task; away from centre it colours the un-frozen repeats
    // too, which is RESONANCE's whole job. kFreezeTilt (the freeze's own
    // measured operating point) is what the crossfade approaches as
    // _freeze -> 1, regardless of where RESONANCE sits.
    const float corner = _f_clk * 0.25f;
    const float tilt = _res_tilt + _freeze * (kFreezeTilt - _res_tilt);
    _l.SetFeedbackTilt(tilt, corner);
    _r.SetFeedbackTilt(tilt, corner);
    _push_freeze();
}

void BbdEngine::_push_freeze() {
    // PER SAMPLE while the ramp moves. Arithmetic and stores only -- no libm.
    // This is what lets the crossfade run at full resolution instead of the
    // 64-step staircase an exp()-carrying push had to be quantised to.
    _freeze_last = _freeze;
    const bool on = _freeze > 0.f;
    _l.SetFeedbackDcBlock(on);
    _r.SetFeedbackDcBlock(on);
    // The un-frozen endpoint is RESONANCE's own tilt (_res_tilt), not the
    // literal 0 this used to be pinned at -- see set_resonance() and the
    // matching comment in _apply_freeze(). At RESONANCE's centre _res_tilt is
    // 0, exactly identity in BbdEcho::fb_path, so the bit-exact-at-rest case
    // survives; kFreezeTilt is still the point the freeze is centred on.
    const float tilt = _res_tilt + _freeze * (kFreezeTilt - _res_tilt);
    _l.SetFeedbackTiltAmount(tilt);
    _r.SetFeedbackTiltAmount(tilt);
    const float fb = _fb_lane + _freeze * (_freeze_k - _fb_lane);
    _l.SetFeedback(fb);
    _r.SetFeedback(fb);
}

void BbdEngine::process_in(float inL, float inR) {
    // SUB: how much neighbour/audio-in actually arrives. Applied here, once,
    // so the wet path (fed to the lines below) and the dry path (outL/outR's
    // own _in_l/_in_r term) agree about how much signal showed up -- a store
    // and a multiply, safe on the per-sample side.
    _in_l = inL * _in_gain;
    _in_r = inR * _in_gain;
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
    // Every sample it moves, at full resolution: _push_freeze() is stores and
    // arithmetic, so there is nothing left to ration. (It used to call the
    // control-rate _apply_freeze(), which put an exp() and a pow() on the
    // audio path -- quantised to 64 steps to bound the damage. Caching both in
    // _apply_freeze() removed the cost rather than budgeting it, and the
    // staircase went with it.)
    if (_freeze != _freeze_last) _push_freeze();
    const float gate_in = (_choked ? 0.f : 1.f) * (1.f - _freeze);

    // DETUNE's slew (spec 5.8): the clock each line actually runs at chases
    // _f_l/_f_r rather than jumping to them, because pitch tracks the clock
    // RATIO, not its difference -- a linear slew from 500 Hz to 8000 Hz would
    // cross the first octave in a twentieth of the time it spends on the
    // last one. Flux's DRAG interpolates geometrically for exactly this
    // reason, and it doubles as the VCO slew of the real circuit: a division
    // change is click-free AND bends in pitch, like the hardware.
    //
    // CONSTANT RATE, not an exponential approach: a one-pole (even one built
    // from a multiplicative step rather than an additive one) still spends
    // most of its travel on whichever octave it starts nearest and crawls
    // through the rest, because what shrinks each sample is the RATIO left to
    // cross, and an exponential shrinks that ratio fastest when it is
    // largest. What "equal ratios take equal time" actually requires is a
    // FIXED per-sample multiplier: _slew_mul's (_slew_s * _sr)-th power is
    // exactly 2 (see set_detune()), so multiplying by it drives one octave in
    // exactly _slew_s seconds, wherever on the clock it starts, with no
    // per-sample transcendental -- one multiply (or divide) and two compares.
    // Clamped so a step cannot overshoot the target it is chasing.
    auto glide = [](float now, float target, float mul) {
        if (now < target) {
            const float next = now * mul;
            return next > target ? target : next;
        }
        if (now > target) {
            const float next = now / mul;
            return next < target ? target : next;
        }
        return now;
    };
    _f_now = glide(_f_now, _f_l, _slew_mul);
    _f_now_r = glide(_f_now_r, _f_r, _slew_mul);

    // Each line at its OWN slewed clock -- COLOR's split (_apply_width())
    // supplies the TARGETS, DETUNE's glide above supplies what actually
    // reaches the line. _f_clk/_f_l/_f_r stay the un-spread/spread targets
    // for the observers only; nothing in the audio path reads them directly
    // any more.
    const float wl = _l.Process(_in_l * gate_in, _f_now);
    const float wr = _r.Process(_in_r * gate_in, _f_now_r);
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
