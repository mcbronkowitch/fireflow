#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include <vector>
#include "fx/part_fx.h"
using namespace spky;

static std::vector<float> s_pf_l(Flux::kMaxSamples);
static std::vector<float> s_pf_r(Flux::kMaxSamples);

// fxv helper: boot bases with individual overrides
static void fill(float* v, float grit, float time, float mix, float send, float fb) {
    v[FXT_GRIT_INT] = grit;
    v[FXT_FLUX_TIME] = time;
    v[FXT_FX_MIX] = mix;
    v[FXT_REV_SEND] = send;
    v[FXT_FLUX_FB] = fb;
}

// One sample with these targets. Part pushes its targets once per raster tick;
// a test that pushes them every sample is the tightest schedule there is.
static void run(PartFx& fx, float& l, float& r, float& sl, float& sr, const float* v) {
    fx.set_targets(v);
    fx.process(l, r, sl, sr);
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
        run(fx, l, r, sl, sr, v);
    }
}

TEST_CASE("part_fx: both blocks off is bit-exact dry, send 0 is exact zero") {
    PartFx fx;
    fx.init(48000.f, s_pf_l.data(), s_pf_r.data());
    float v[FXT_COUNT];
    fill(v, 0.3f, 0.4f, 1.f, 0.f, 0.45f);
    for (int i = 0; i < 2000; ++i) {
        float s = 0.4f * std::sin(0.013f * i);
        float l = s, r = s, sl = 1.f, sr = 1.f;
        run(fx, l, r, sl, sr, v);
        CHECK(l == s);
        CHECK(r == s);
        CHECK(sl == 0.f);
        CHECK(sr == 0.f);
    }
}

TEST_CASE("part_fx: FX MIX 0 keeps the dry signal even with grit engaged") {
    PartFx fx;
    fx.init(48000.f, s_pf_l.data(), s_pf_r.data());
    fx.set_fx_on(FxBlock::Grit, true, true);
    float v[FXT_COUNT];
    fill(v, 0.9f, 0.4f, 0.f, 0.f, 0.f);
    for (int i = 0; i < 2000; ++i) {
        float s = 0.4f * std::sin(0.013f * i);
        float l = s, r = s, sl, sr;
        run(fx, l, r, sl, sr, v);
        CHECK(l == doctest::Approx(s).epsilon(1e-6));
    }
}

TEST_CASE("part_fx: FX MIX 1 with grit on changes the signal") {
    PartFx fx;
    fx.init(48000.f, s_pf_l.data(), s_pf_r.data());
    fx.set_fx_on(FxBlock::Grit, true, true);
    float v[FXT_COUNT];
    fill(v, 0.9f, 0.4f, 1.f, 0.f, 0.f);
    int diff = 0;
    for (int i = 0; i < 4800; ++i) {
        float s = 0.4f * std::sin(0.028f * i);
        float l = s, r = s, sl, sr;
        run(fx, l, r, sl, sr, v);
        if (std::fabs(l - s) > 1e-4f) ++diff;
    }
    CHECK(diff > 1000);
}

TEST_CASE("part_fx: send taps post-FX at the equal-power gain") {
    PartFx fx;
    fx.init(48000.f, s_pf_l.data(), s_pf_r.data());
    float v[FXT_COUNT];
    fill(v, 0.3f, 0.4f, 1.f, 1.f, 0.45f);   // send fully open
    // prime the smoothers (first process snaps), then measure
    float l = 0.f, r = 0.f, sl, sr;
    run(fx, l, r, sl, sr, v);
    for (int i = 1; i < 200; ++i) {
        float s = 0.4f * std::sin(0.013f * i);
        l = s; r = s;
        run(fx, l, r, sl, sr, v);
        CHECK(sl == doctest::Approx(l));    // sin(pi/2) = 1: send == post-fx out
    }
}

TEST_CASE("part_fx: comp default 0 leaves chain and send bit-exact") {
    PartFx fx;
    fx.init(48000.f, s_pf_l.data(), s_pf_r.data());
    const float fxv[FXT_COUNT] = {0.f, 0.5f, 1.f, 0.5f, 0.f};
    for (int i = 0; i < 4800; ++i) {
        float s = 0.5f * std::sin(6.2831853f * 220.f * i / 48000.f);
        float l = s, r = s, sl = 0.f, sr = 0.f;
        run(fx, l, r, sl, sr, fxv);
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
        fx.init(48000.f, s_pf_l.data(), s_pf_r.data());
        fx.set_comp(amount);
        double acc = 0.0;
        int n = 0;
        for (int i = 0; i < 96000; ++i) {
            float s = 0.05f * std::sin(6.2831853f * 220.f * i / 48000.f);  // quiet!
            float l = s, r = s, sl = 0.f, sr = 0.f;
            run(fx, l, r, sl, sr, fxv);
            if (i >= 24000) { acc += sl * sl; ++n; }
        }
        return std::sqrt((float)(acc / n));
    };
    CHECK(send_rms(1.f) > send_rms(0.f) * 1.5f);   // full-wet motivation, verified
}

TEST_CASE("part_fx: FXT_FLUX_TIME reaches tape delay -- and RATE still sets the base") {
    // The lane moves tape time relative to the synced RATE ladder.
    PartFx fx;
    fx.init(48000.f, s_pf_l.data(), s_pf_r.data());
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_bpm(120.f);
    fx.set_flux_rate(3);                  // "1/4" -> 0.5 s
    fx.set_flux_mix(1.f);
    float v[FXT_COUNT];
    fill(v, 0.f, 0.5f, 1.f, 0.f, 0.f);    // FXT_FLUX_TIME neutral
    for (int i = 0; i < 40000; ++i) {
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        run(fx, l, r, sl, sr, v);
    }
    const float neutral = fx.flux().delay_current_for_test();
    CHECK(neutral > 0.f);
    fill(v, 0.f, 1.f, 1.f, 0.f, 0.f);     // FXT_FLUX_TIME hard up -> x4
    for (int i = 0; i < 2000; ++i) {      // past both target smoothers
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        run(fx, l, r, sl, sr, v);
    }
    INFO("neutral=" << neutral << " modulated=" << fx.flux().delay_current_for_test());
    CHECK(fx.flux().delay_current_for_test() > neutral * 3.f);
}

TEST_CASE("part_fx: the FLUX TIME lane rides the 2 ms path, so a 4 Hz vibrato survives") {
    // PartFx's own OnePole is 2 ms (part_fx.cpp:12). A 4 Hz sine on the lane
    // must still swing the clock by most of its range; through the 30 ms
    // ladder slew it would be a ~5 Hz low-pass and would not.
    PartFx fx;
    fx.init(48000.f, s_pf_l.data(), s_pf_r.data());
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_bpm(120.f);
    fx.set_flux_rate(3);
    fx.set_flux_mix(1.f);
    float v[FXT_COUNT];
    fill(v, 0.f, 0.5f, 1.f, 0.f, 0.f);
    for (int i = 0; i < 40000; ++i) {
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        run(fx, l, r, sl, sr, v);
    }
    float lo = 1e30f, hi = 0.f;
    for (int i = 0; i < 24000; ++i) {     // 0.5 s = two vibrato cycles
        const float m = 0.5f + 0.4f * std::sin(TWO_PI * 4.f * i / 48000.f);
        fill(v, 0.f, m, 1.f, 0.f, 0.f);
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        run(fx, l, r, sl, sr, v);
        if (i > 12000) {
            lo = std::min(lo, fx.flux().delay_current_for_test());
            hi = std::max(hi, fx.flux().delay_current_for_test());
        }
    }
    INFO("lo=" << lo << " hi=" << hi << " ratio=" << hi / lo);
    // Full depth 0.1..0.9 is a ratio of 2^(4*0.8) = 9.19; the 2 ms smoother
    // takes some of it back. Anything above 5 proves the vibrato survived.
    CHECK(hi / lo > 5.f);
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
    std::vector<float> s_pf_tap_l(Flux::kMaxSamples);
    std::vector<float> s_pf_tap_r(Flux::kMaxSamples);
    PartFx fx;
    fx.init(48000.f, s_pf_tap_l.data(), s_pf_tap_r.data());
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
        run(fx, l, r, sl, sr, v);
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
    std::vector<float> s_pf_tap2_l(Flux::kMaxSamples);
    std::vector<float> s_pf_tap2_r(Flux::kMaxSamples);
    PartFx fx;
    fx.init(48000.f, s_pf_tap2_l.data(), s_pf_tap2_r.data());
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
    run(fx, l, r, sl, sr, v);
    CHECK(fx.tape_tap() == 0.f);
}

TEST_CASE("part_fx: tape_tap drops to exact 0 the sample FLUX disengages, GRIT alone does not feed it") {
    // Same claim as above, but GRIT stays on through the transition, so the
    // outer branch keeps running and the reset has to come from the INNER
    // `if (_flux.engaged())` gate instead -- a naive version that instead
    // fed _tap_dc.Process(0.f) every sample (trusting the zero echo
    // difference rather than checking engaged()) would read a slowly-
    // decaying tail off the DC blocker's filter memory here, not a hard 0.
    std::vector<float> s_pf_tap3_l(Flux::kMaxSamples);
    std::vector<float> s_pf_tap3_r(Flux::kMaxSamples);
    PartFx fx;
    fx.init(48000.f, s_pf_tap3_l.data(), s_pf_tap3_r.data());
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
    run(fx, l, r, sl, sr, v);
    CHECK(fx.tape_tap() == 0.f);   // instantaneous, not a decaying tail
}

// The target smoothers run only on a sample that follows set_targets() or
// while one of them is still gliding (fault R4: in the all-noon patch all five
// sat idle and still cost 13 k cycles per deck per block). Skipping is only
// allowed where OnePole::process would have returned its value unchanged, so
// the smoothed values must equal five plain per-sample OnePoles fed the same
// targets, bit for bit, on the raster Part pushes on.
TEST_CASE("part_fx: idle smoothers are skipped without changing a single value") {
    PartFx fx;
    fx.init(48000.f, s_pf_l.data(), s_pf_r.data());
    OnePole ref[FXT_COUNT];
    for (auto& s : ref) s.init(48000.f, 0.002f);

    float v[FXT_COUNT];
    fill(v, 0.3f, 0.5f, 1.f, 0.25f, 0.45f);
    float target[FXT_COUNT];
    for (int i = 0; i < FXT_COUNT; ++i) target[i] = v[i];
    fx.set_targets(v);
    for (int i = 0; i < FXT_COUNT; ++i) ref[i].reset(v[i]);

    // One schedule step per 96-sample tick: big steps (glides that outlast a
    // tick), sub-threshold nudges (OnePole's 0.0005 fast path keeps its old
    // value), a return to an earlier value mid-glide, and long idle stretches.
    const float steps[][FXT_COUNT] = {
        {0.3f, 0.5f, 1.f, 0.25f, 0.45f},       // idle
        {0.9f, 0.5f, 1.f, 0.25f, 0.45f},       // GRIT jumps: a glide
        {0.9f, 0.5003f, 1.f, 0.25f, 0.45f},    // FLUX TIME nudged below 0.0005
        {0.9f, 0.5003f, 0.f, 0.25f, 0.45f},    // FX MIX jumps
        {0.1f, 0.5003f, 0.f, 0.2502f, 0.45f},  // GRIT back down, SEND nudged
        {0.1f, 0.5003f, 0.f, 0.2502f, 0.45f},  // idle
        {0.1f, 0.5003f, 0.f, 0.2502f, 0.45f},  // idle
        {0.1f, 0.5003f, 0.6f, 0.2502f, 1.f},   // two at once
    };
    int samples = 0, mismatches = 0;
    for (int round = 0; round < 4; ++round) {
        for (const auto& st : steps) {
            for (int i = 0; i < FXT_COUNT; ++i) v[i] = target[i] = st[i];
            fx.set_targets(v);
            for (int s = 0; s < 96; ++s) {
                float l = 0.f, r = 0.f, sl, sr;
                fx.process(l, r, sl, sr);
                for (int i = 0; i < FXT_COUNT; ++i) {
                    const float want = ref[i].process(target[i]);
                    if (fx.smoothed_for_test(i) != want) ++mismatches;
                }
                ++samples;
            }
        }
    }
    INFO("samples=" << samples);
    CHECK(mismatches == 0);
    // The schedule really glides across tick boundaries: a 2 ms one-pole needs
    // more than one tick to cover a 0.8 step, so a version that ran the
    // smoothers only on the sample after set_targets() would stall mid-glide.
    // RED, 2026-10-24: with the "still gliding" condition removed this case
    // failed with 8 157 mismatches; with set_targets() not flagging the push,
    // 8 019 (and both FXT_FLUX_TIME cases above failed too).
}


namespace {
// One 4 s run of a PartFx with FLUX and GRIT on: targets pushed once per
// 96 samples as Part does, every setter input moving (GRIT intensity, FLUX
// feedback and time, with jumps the 2 ms smoothers glide over), and both
// blocks off from 1.5 s to 50 samples past 2.5 s. The targets jump at 1.6 s
// and then hold, so they have settled when the blocks come back -- off the
// 96-sample push grid, where nothing but the setters left due while the
// blocks were off can hand them the new values.
std::vector<float> setter_run(bool skip) {
    PartFx::set_setter_skip_for_test(skip);
    std::fill(s_pf_l.begin(), s_pf_l.end(), 0.f);
    std::fill(s_pf_r.begin(), s_pf_r.end(), 0.f);
    static PartFx fx;
    fx.init(48000.f, s_pf_l.data(), s_pf_r.data());
    fx.set_fx_on(FxBlock::Grit, true, true);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_grit_mode(GritMode::Drive);
    fx.set_grit_mix(0.6f);
    std::vector<float> out;
    uint32_t rng = 12345u;
    float v[FXT_COUNT];
    for (int i = 0; i < 48000 * 4; ++i) {
        if (i % 96 == 0) {
            const int t = i / 96;
            if (i >= 48000 * 16 / 10 && i < 48000 * 26 / 10)
                fill(v, 0.95f, 0.8f, 0.8f, 0.3f, 0.9f);           // the jump, then held
            else
                fill(v, 0.2f + 0.6f * float((t / 7) % 5) / 4.f,   // GRIT intensity steps
                     0.5f + 0.3f * float((t / 11) % 3 - 1),      // FLUX time
                     0.8f, 0.3f,
                     0.3f + 0.5f * float((t / 5) % 4) / 3.f);    // FLUX feedback
            fx.set_targets(v);
        }
        if (i == 48000 * 3 / 2)      { fx.set_fx_on(FxBlock::Grit, false); fx.set_fx_on(FxBlock::Flux, false); }
        if (i == 48000 * 5 / 2 + 50) { fx.set_fx_on(FxBlock::Grit, true);  fx.set_fx_on(FxBlock::Flux, true); }
        rng = rng * 1664525u + 1013904223u;
        float l = (float(rng >> 8) / 16777216.f - 0.5f) * 0.5f, r = -l, sl, sr;
        fx.process(l, r, sl, sr);
        out.push_back(l); out.push_back(r); out.push_back(sl); out.push_back(fx.tape_tap());
    }
    PartFx::set_setter_skip_for_test(true);
    return out;
}
} // namespace

TEST_CASE("part_fx: the FX setters run only when their inputs moved, without changing a bit") {
    // Fault R4: GRIT's intensity check and FLUX's set_feedback / set_time_mod
    // ran on every engaged sample although they only ever see the smoothed
    // targets. Run with them on every sample (the reference) and with them
    // only when due: every output bit equal.
    const std::vector<float> ref = setter_run(false);
    const std::vector<float> dut = setter_run(true);
    REQUIRE(ref.size() == dut.size());
    long diffs = 0;
    for (size_t i = 0; i < ref.size(); ++i)
        diffs += std::memcmp(&ref[i], &dut[i], sizeof(float)) != 0;
    CHECK(diffs == 0);
    // The run is not trivially equal: the targets did move the sound.
    long moved = 0;
    for (size_t i = 4; i < ref.size(); i += 4) moved += ref[i] != ref[i - 4];
    CHECK(moved > 100000);
}
