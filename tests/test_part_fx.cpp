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

TEST_CASE("part_fx: synced rate + BPM place the echo, not FXT_FLUX_TIME") {
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_flux_mix(1.f);              // 0 dB wet
    fx.set_bpm(120.f);
    fx.set_flux_rate(3);              // "1/4" @120 -> 0.5 s
    float v[FXT_COUNT];
    fill(v, 0.f, 0.99f, 1.f, 0.f, 0.f);   // FXT_FLUX_TIME = 0.99 must NOT move the echo
    int idx = -1;
    for (int i = 0; i < 30000; ++i) {
        float l = (i == 0) ? 1.f : 0.f;
        float r = l, sl, sr;
        fx.process(l, r, sl, sr, v);
        if (i > 100 && std::fabs(l) > 1e-3f) { idx = i; break; }
    }
    CHECK(idx >= 23900);
    CHECK(idx <= 24200);             // ~24000 (0.5 s), independent of v[FXT_FLUX_TIME]
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
    fill(v, 0.f, 0.f, 0.f, 0.f, 0.5f);   // FX MIX (index 2) left at 0.f on purpose

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

