#include <doctest/doctest.h>
#include <cmath>
#include "fx/part_fx.h"
using namespace spky;

static float s_pf_l[Flux::kMaxSamples];
static float s_pf_r[Flux::kMaxSamples];

// fxv helper: boot bases with individual overrides
static void fill(float* v, float grit, float time, float mix, float send, float fb) {
    v[FXT_GRIT_INT] = grit;
    v[FXT_FLUX_TIME] = time;
    v[FXT_FX_MIX] = mix;
    v[FXT_REV_SEND] = send;
    v[FXT_FLUX_FB] = fb;
}

// Push a single impulse through, then silence, until the echo genuinely
// returns (task-9-review.md, Minor 4). A fixed sample count tied to a
// specific BPM/rate-index -> delay-time mapping turns red the day somebody
// retunes that mapping, for a reason that has nothing to do with the tap
// itself. This decouples the two: the loop stops the instant tape_tap()
// goes nonzero, whatever the actual delay setting resolves to. Bounded
// generously (2 s) so a genuine regression still fails loudly rather than
// looping forever.
static void warm_up_tape_tap(PartFx& fx, const float* v) {
    for (int i = 0; i < 96000 && fx.tape_tap() == 0.f; ++i) {
        float s = (i == 0) ? 0.6f : 0.f;
        float l = s, r = s, sl, sr;
        fx.process(l, r, sl, sr, v);
    }
}

TEST_CASE("part_fx: both blocks off is bit-exact dry, send 0 is exact zero") {
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    float v[FXT_COUNT];
    fill(v, 0.3f, 0.4f, 1.f, 0.f, 0.45f);
    for (int i = 0; i < 2000; ++i) {
        float s = 0.4f * std::sin(0.013f * i);
        float l = s, r = s, sl = 1.f, sr = 1.f;
        fx.process(l, r, sl, sr, v);
        CHECK(l == s);
        CHECK(r == s);
        CHECK(sl == 0.f);
        CHECK(sr == 0.f);
    }
}

TEST_CASE("part_fx: FX MIX 0 keeps the dry signal even with grit engaged") {
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    fx.set_fx_on(FxBlock::Grit, true, true);
    float v[FXT_COUNT];
    fill(v, 0.9f, 0.4f, 0.f, 0.f, 0.f);
    for (int i = 0; i < 2000; ++i) {
        float s = 0.4f * std::sin(0.013f * i);
        float l = s, r = s, sl, sr;
        fx.process(l, r, sl, sr, v);
        CHECK(l == doctest::Approx(s).epsilon(1e-6));
    }
}

TEST_CASE("part_fx: FX MIX 1 with grit on changes the signal") {
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    fx.set_fx_on(FxBlock::Grit, true, true);
    float v[FXT_COUNT];
    fill(v, 0.9f, 0.4f, 1.f, 0.f, 0.f);
    int diff = 0;
    for (int i = 0; i < 4800; ++i) {
        float s = 0.4f * std::sin(0.028f * i);
        float l = s, r = s, sl, sr;
        fx.process(l, r, sl, sr, v);
        if (std::fabs(l - s) > 1e-4f) ++diff;
    }
    CHECK(diff > 1000);
}

TEST_CASE("part_fx: send taps post-FX at the equal-power gain") {
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    float v[FXT_COUNT];
    fill(v, 0.3f, 0.4f, 1.f, 1.f, 0.45f);   // send fully open
    // prime the smoothers (first process snaps), then measure
    float l = 0.f, r = 0.f, sl, sr;
    fx.process(l, r, sl, sr, v);
    for (int i = 1; i < 200; ++i) {
        float s = 0.4f * std::sin(0.013f * i);
        l = s; r = s;
        fx.process(l, r, sl, sr, v);
        CHECK(sl == doctest::Approx(l));    // sin(pi/2) = 1: send == post-fx out
    }
}

TEST_CASE("part_fx: comp default 0 leaves chain and send bit-exact") {
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    const float fxv[FXT_COUNT] = {0.f, 0.5f, 1.f, 0.5f, 0.f};
    for (int i = 0; i < 4800; ++i) {
        float s = 0.5f * std::sin(6.2831853f * 220.f * i / 48000.f);
        float l = s, r = s, sl = 0.f, sr = 0.f;
        fx.process(l, r, sl, sr, fxv);
        CHECK(l == s);                                   // FX off + comp 0 = dry bits
        // The send law is still sin(mix * pi/2) -- what changed (2026-07-22 CPU
        // hunt) is who evaluates it: fast_sin instead of libm, because this call
        // site ran 192 libm sinf per 96-sample block on the two-part instrument
        // and was a third of the whole FX-off chain's cost. fast_sin is EXACT at
        // the endpoints (mix 0 -> silent send, mix 1 -> unity, both still bit-
        // exact -- the two test cases above are unchanged and still pass) and
        // carries up to 1.2e-3 absolute error in between, which is 1e-3 relative
        // here at mix 0.5 -- past doctest's default 1.19e-5 epsilon. The gain
        // error is 0.009 dB on a reverb send. Widening the epsilon keeps the
        // assertion's intent (the law, and that comp 0 does not touch it); it
        // does not weaken it into "any gain will do".
        CHECK(sl == doctest::Approx(s * std::sin(0.5f * 1.5707963f)).epsilon(2e-3));
    }
}

TEST_CASE("part_fx: comp sits BEFORE the send tap — the send gets louder too") {
    const float fxv[FXT_COUNT] = {0.f, 0.5f, 1.f, 0.8f, 0.f};
    auto send_rms = [&](float amount) {
        PartFx fx;
        fx.init(48000.f, s_pf_l, s_pf_r);
        fx.set_comp(amount);
        double acc = 0.0;
        int n = 0;
        for (int i = 0; i < 96000; ++i) {
            float s = 0.05f * std::sin(6.2831853f * 220.f * i / 48000.f);  // quiet!
            float l = s, r = s, sl = 0.f, sr = 0.f;
            fx.process(l, r, sl, sr, fxv);
            if (i >= 24000) { acc += sl * sl; ++n; }
        }
        return std::sqrt((float)(acc / n));
    };
    CHECK(send_rms(1.f) > send_rms(0.f) * 1.5f);   // full-wet motivation, verified
}

TEST_CASE("part_fx: FXT_FLUX_TIME reaches the clock -- and RATE still sets the base") {
    // The 2026-07-17 spec retired this target; the BBD reactivates it. This
    // case is the inverse of the one it replaces: the lane MUST move the
    // clock now, while the ladder still decides what it moves relative to.
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_bpm(120.f);
    fx.set_flux_rate(3);                  // "1/4" -> 0.5 s
    fx.set_flux_mix(1.f);
    float v[FXT_COUNT];
    fill(v, 0.f, 0.5f, 1.f, 0.f, 0.f);    // FXT_FLUX_TIME neutral
    for (int i = 0; i < 40000; ++i) {
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        fx.process(l, r, sl, sr, v);
    }
    const float neutral = fx.flux().clock_hz();
    CHECK(neutral > 0.f);
    fill(v, 0.f, 1.f, 1.f, 0.f, 0.f);     // FXT_FLUX_TIME hard up -> x4
    for (int i = 0; i < 2000; ++i) {      // past PartFx's 2 ms smoother
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        fx.process(l, r, sl, sr, v);
    }
    INFO("neutral=" << neutral << " modulated=" << fx.flux().clock_hz());
    CHECK(fx.flux().clock_hz() > neutral * 3.f);
}

TEST_CASE("part_fx: the FLUX TIME lane rides the 2 ms path, so a 4 Hz vibrato survives") {
    // PartFx's own OnePole is 2 ms (part_fx.cpp:12). A 4 Hz sine on the lane
    // must still swing the clock by most of its range; through the 30 ms
    // ladder slew it would be a ~5 Hz low-pass and would not.
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_bpm(120.f);
    fx.set_flux_rate(3);
    fx.set_flux_mix(1.f);
    float v[FXT_COUNT];
    fill(v, 0.f, 0.5f, 1.f, 0.f, 0.f);
    for (int i = 0; i < 40000; ++i) {
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        fx.process(l, r, sl, sr, v);
    }
    float lo = 1e30f, hi = 0.f;
    for (int i = 0; i < 24000; ++i) {     // 0.5 s = two vibrato cycles
        const float m = 0.5f + 0.4f * std::sin(TWO_PI * 4.f * i / 48000.f);
        fill(v, 0.f, m, 1.f, 0.f, 0.f);
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        fx.process(l, r, sl, sr, v);
        if (i > 12000) {
            lo = std::min(lo, fx.flux().clock_hz());
            hi = std::max(hi, fx.flux().clock_hz());
        }
    }
    INFO("lo=" << lo << " hi=" << hi << " ratio=" << hi / lo);
    // Full depth 0.1..0.9 is a ratio of 2^(4*0.8) = 9.19; the 2 ms smoother
    // takes some of it back. Anything above 5 proves the vibrato survived.
    CHECK(hi / lo > 5.f);
}

TEST_CASE("part_fx: DRIVE and STAGES reach FLUX") {
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_stages(0.4f);
    fx.set_drive(0.6f);
    float v[FXT_COUNT];
    fill(v, 0.f, 0.5f, 1.f, 0.f, 0.f);
    for (int i = 0; i < 20000; ++i) {
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        fx.process(l, r, sl, sr, v);
    }
    CHECK(fx.flux().stages() == doctest::Approx(2048).epsilon(0.01));
    CHECK(fx.flux().drive_norm_for_test() == doctest::Approx(0.6f));
}

TEST_CASE("part_fx: tape_tap exposes the FLUX echo, not the mix, and starts at exact 0") {
    // Task 9 (spec 2026-07-26 body-resonator, §6). The brief's original test
    // body here (fx.set_flux(0.6f), init(sr) with no echo buffers) does not
    // compile against this tree -- there is no set_flux(), and init() needs
    // the injected echo buffers, per this file's own idiom (top of file).
    // The two claims it was written to pin are unchanged:
    //   1. tape_tap() reads exactly 0.f before any audio has been processed.
    //   2. Once FLUX is engaged and fed signal, tape_tap() goes nonzero.
    // A third is added here because it is the other half of spec §6's own
    // wording ("not the mixed output"): the tap must go nonzero even at
    // FX MIX = 0, where the part's own dry/wet output never moves at all.
    //
    // task-9-review.md Minor 5: the `CHECK(l == s)` below reads `l` AFTER
    // `_comp.process(l, r)` (part_fx.cpp), not the pre-Comp FX-MIX-0 value
    // directly -- it holds only because Comp defaults to `_amount_target =
    // 0.f`, its own documented "bit-exact bypass" (comp.h). Not reset here
    // on purpose (this test is about the tap, and PartFx's own default IS
    // that bypass), but the dependency is real: if one-knob comp ever grew
    // a nonzero default, this is the assertion that would start failing,
    // for a reason that has nothing to do with the tape tap.
    static float s_pf_tap_l[Flux::kMaxSamples], s_pf_tap_r[Flux::kMaxSamples];
    PartFx fx;
    fx.init(48000.f, s_pf_tap_l, s_pf_tap_r);
    CHECK(fx.tape_tap() == 0.f);   // claim 1

    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_flux_mix(1.f);
    fx.set_bpm(120.f);
    fx.set_flux_rate(3);           // "1/4" @ 120 bpm -> 0.5 s, ~24000 samples
    float v[FXT_COUNT];
    // FXT_FLUX_TIME (index 1) at 0.5f, its new neutral (Task 8): at 0.f it is
    // a x0.25 clock multiplier, quadrupling this test's delay from 0.5 s to
    // 2 s -- past the 48000-sample (1 s) window below. 0.f here used to be
    // an inert placeholder; it no longer is, so this test asks for the
    // no-op value instead of accidentally exercising FXT_FLUX_TIME at all.
    fill(v, 0.f, 0.5f, 0.f, 0.f, 0.5f);   // FX MIX (index 2) left at 0.f on purpose

    bool tap_nonzero = false;
    for (int i = 0; i < 48000; ++i) {
        float s = (i == 0) ? 0.6f : 0.f;   // one impulse, then silence
        float l = s, r = s, sl, sr;
        fx.process(l, r, sl, sr, v);
        CHECK(l == s);                     // FX MIX 0: the part's own output never moves
        if (fx.tape_tap() != 0.f) tap_nonzero = true;
    }
    CHECK(tap_nonzero);   // claim 2 + the "not the mix" claim above, together
}

TEST_CASE("part_fx: tape_tap drops to exact 0 the sample FLUX disengages, whole branch skipped") {
    // Addendum D: "When FLUX is not engaged the tap must be exactly 0.f. It
    // must not freeze at a stale value from the last time it was on."
    // process() has two separate reset paths for this -- the outer `else`
    // (whole `_grit.engaged() || _flux.engaged()` branch skipped, GRIT never
    // engaged here) and, inside the branch, the inner `if (_flux.engaged())`
    // (exercised by the next TEST_CASE, where GRIT stays on). This case
    // isolates the outer one: if it silently dropped, _tape_tap would keep
    // returning whatever the branch last computed instead of 0.f, because
    // skipping the branch means process() never touches _tape_tap at all.
    static float s_pf_tap2_l[Flux::kMaxSamples], s_pf_tap2_r[Flux::kMaxSamples];
    PartFx fx;
    fx.init(48000.f, s_pf_tap2_l, s_pf_tap2_r);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_flux_mix(1.f);
    fx.set_bpm(120.f);
    fx.set_flux_rate(3);
    float v[FXT_COUNT];
    fill(v, 0.f, 0.f, 1.f, 0.f, 0.5f);
    warm_up_tape_tap(fx, v);
    REQUIRE(fx.tape_tap() != 0.f);   // the echo really is live going into this test

    // GRIT was never turned on, so this single call takes the branch
    // condition straight from true (FLUX engaged) to false (nothing
    // engaged) -- the outer `else` is the ONLY reset path that can run.
    fx.set_fx_on(FxBlock::Flux, false, true);
    float l = 0.f, r = 0.f, sl, sr;
    fx.process(l, r, sl, sr, v);
    CHECK(fx.tape_tap() == 0.f);
}

TEST_CASE("part_fx: tape_tap drops to exact 0 the sample FLUX disengages, GRIT alone does not feed it") {
    // Same claim as above, but GRIT stays on through the transition, so the
    // outer branch keeps running and the reset has to come from the INNER
    // `if (_flux.engaged())` gate instead -- a naive version that instead
    // fed _tap_dc.Process(0.f) every sample (trusting the zero echo
    // difference rather than checking engaged()) would read a slowly-
    // decaying tail off the DC blocker's filter memory here, not a hard 0.
    static float s_pf_tap3_l[Flux::kMaxSamples], s_pf_tap3_r[Flux::kMaxSamples];
    PartFx fx;
    fx.init(48000.f, s_pf_tap3_l, s_pf_tap3_r);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_flux_mix(1.f);
    fx.set_bpm(120.f);
    fx.set_flux_rate(3);
    float v[FXT_COUNT];
    fill(v, 0.f, 0.f, 1.f, 0.f, 0.5f);
    warm_up_tape_tap(fx, v);
    REQUIRE(fx.tape_tap() != 0.f);   // the echo really is live going into this test

    // Turn FLUX off but GRIT on: the outer branch stays taken.
    fx.set_fx_on(FxBlock::Flux, false, true);
    fx.set_fx_on(FxBlock::Grit, true, true);
    float l = 0.f, r = 0.f, sl, sr;
    fx.process(l, r, sl, sr, v);
    CHECK(fx.tape_tap() == 0.f);   // instantaneous, not a decaying tail
}

TEST_CASE("part_fx: tape_tap's soft clip bounds the raw BBD echo, which is not bounded to unity") {
    // Ported for the BBD rewrite (2026-07-27 whole-branch review, finding 3)
    // from the tape-era case this replaces: "part_fx: tape_tap's soft clip
    // bounds the raw taps, which skip Flux's own tanh" (task-9-review.md,
    // Important 1), deleted with the tap bank (e004a3d) with no replacement
    // -- deleting either this stage or the DC block below left the full
    // suite green, so neither promise had a witness.
    //
    // fast_tanh(_tap_dc.Process(echo_mono)) is the whole of what lets
    // tape_tap()'s doc comment (part_fx.h) promise the tap is "safe to feed
    // straight into a resonator's excitation input". BbdEcho::Process is NOT
    // bounded at 1 the way the old EchoDelay was: fast_tanh only clamps the
    // SATURATOR stage inside the loop (sat_out_ = kSatCeil = 0.9, bbd.h), and
    // the compander's Expand() stage runs AFTER that, multiplying by up to
    // kCompCeilE = 4 -- so the line's own output genuinely exceeds unity
    // (tests/test_bbd.cpp's "max DRIVE and max FEEDBACK together still
    // cannot run away" measures peak = 1.54485 at DRIVE 1, FEEDBACK 1.2).
    // DRIVE maxed and FEEDBACK near its 1.2 ceiling is that same regime,
    // reached here through PartFx's own knobs rather than BbdEcho directly.
    static float s_pf_clip_l[Flux::kMaxSamples], s_pf_clip_r[Flux::kMaxSamples];
    PartFx fx;
    fx.init(48000.f, s_pf_clip_l, s_pf_clip_r);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_flux_mix(1.f);
    fx.set_drive(1.f);
    float v[FXT_COUNT];
    fill(v, 0.f, 0.5f, 1.f, 0.f, 1.f);   // FEEDBACK maxed: norm 1 -> 1.2 raw (Flux::set_feedback)

    double maxabs = 0.0;
    for (int i = 0; i < 480000; ++i) {   // 10 s: matches test_bbd's own self-oscillation window
        // A single impulse, then silence -- lets the loop build its own
        // sustained self-oscillation rather than tracking a driven tone, the
        // same shape as test_bbd's "still cannot run away" case.
        float s = (i < 32) ? 1.f : 0.f;
        float l = s, r = s, sl, sr;
        fx.process(l, r, sl, sr, v);
        maxabs = std::max(maxabs, (double)std::fabs(fx.tape_tap()));
        REQUIRE(std::fabs(fx.tape_tap()) <= 1.f);
    }
    MESSAGE("maxabs=", maxabs);
    // Premise guard (same idiom as the deleted test it replaces): everything
    // above only means something if the loop actually reached the clipping
    // regime -- `<= 1.f` is trivially true of a signal that never got near
    // 1, so without this line a future tuning change that stopped the loop
    // from blooming past unity would leave the test green while testing
    // nothing.
    //
    // The bound is read backwards through the clip, which is what makes it a
    // premise rather than a preference: maxabs is POST-tanh, so it can never
    // exceed 1 by construction, and asserting a number on it is only ever a
    // statement about what went IN. fast_tanh(x) > 0.85 requires x > 1.25, so
    // this line says "the raw echo exceeded unity by at least 25%" -- exactly
    // the premise the case needs, and the reason the clip is load-bearing.
    //
    // 0.9 originally, lowered when Flux::set_feedback began dividing DRIVE's
    // gain out of the feedback coefficient. The guard fired on that change,
    // correctly: the loop's reachable peak fell from 1.54485 to a measured
    // 1.4659 (max over a stimulus sweep -- burst lengths 1..2000 ms at
    // amplitudes 0.4 and 1.0), and 0.9 needs 1.47. Nothing about the claim
    // changed -- 1.4659 is still 47% above unity, and deleting the fast_tanh
    // still fails the `<= 1.f` REQUIRE above on this very case. Widening the
    // stimulus was tried first and cannot recover 0.9: the sweep's own
    // maximum is below it.
    REQUIRE(maxabs > 0.85);
}

TEST_CASE("part_fx: tape_tap's DC block removes a sustained offset, fast_tanh alone cannot") {
    // Ported for the BBD rewrite (2026-07-27 whole-branch review, finding 3)
    // from the tape-era case this replaces: "part_fx: tape_tap's DC block
    // removes a sustained offset, fast_tanh alone cannot" (task-9-review.md,
    // Important 1), deleted with the tap bank (e004a3d) with no replacement.
    //
    // fast_tanh maps a constant nonzero input to a constant nonzero output
    // forever -- only the DcBlock stage can make a genuinely SUSTAINED
    // offset's running mean fall toward 0. BbdLine::Process (bbd.h) has its
    // own DC feed-through term (`fout_->H * ybbd_old_`, H == 1 by
    // construction and pinned by test_bbd.cpp), so a constant input reaches
    // this tap as a real, sustained DC signal, not a decaying transient -- a
    // tap with the DC block deleted could only wrap that constant through
    // fast_tanh into a DIFFERENT nonzero constant, never make the running
    // mean decay.
    //
    // STAGES is pulled to its minimum here purely so the delay line's own
    // fill time (and the clock-ramp settle) stay short: at the default 8192
    // stages, bbd_clock_hz's ceiling (kClockMaxHz, bbd.h) caps how short a
    // delay is reachable at all, and the point of this test is the DC
    // block's own decay, not how long the line takes to fill.
    static float s_pf_dc_l[Flux::kMaxSamples], s_pf_dc_r[Flux::kMaxSamples];
    PartFx fx;
    fx.init(48000.f, s_pf_dc_l, s_pf_dc_r);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_flux_mix(1.f);
    fx.set_stages(0.f);            // 512 stages: the shortest reachable delay
    fx.set_bpm(600.f);
    fx.set_flux_rate(11);          // fastest sync division ("1/32")
    float v[FXT_COUNT];
    fill(v, 0.f, 0.5f, 1.f, 0.f, 0.45f);

    // Settle the stage/clock slew and fill the delay line with the DC input
    // before measuring, so the "early" window below reads a genuine steady
    // tap rather than the transient onset of the line filling.
    for (int i = 0; i < 3000; ++i) {
        float l = 0.5f, r = 0.5f, sl, sr;
        fx.process(l, r, sl, sr, v);
    }

    double early_sum = 0.0, late_sum = 0.0;
    int early_n = 0, late_n = 0;
    for (int i = 0; i < 96000; ++i) {
        float l = 0.5f, r = 0.5f;       // constant, one-sided drive: a real DC offset
        float sl, sr;
        fx.process(l, r, sl, sr, v);
        if (i < 2000)     { early_sum += fx.tape_tap(); ++early_n; }
        if (i >= 90000)   { late_sum  += fx.tape_tap(); ++late_n; }
    }
    const double early_mean = early_sum / early_n;
    const double late_mean  = late_sum  / late_n;
    MESSAGE("early_mean=", early_mean, " late_mean=", late_mean);
    CHECK(std::fabs(early_mean) > 0.05);                        // the DC genuinely reached the tap
    CHECK(std::fabs(late_mean) < std::fabs(early_mean) * 0.3);  // and the block pulls it toward 0
}

