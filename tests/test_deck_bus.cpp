#include <doctest/doctest.h>
#include <cmath>
#include "parts/part.h"
#include "util/fast_tanh.h"
using namespace spky;

#include "instrument.h"
#include <vector>

// Deck A monitors deck B through the bus; deck B makes a signal. Deck A's
// output at sample n must be the bound applied to deck B's output at n-1,
// for every CHOKE position -- that is the whole contract.
static void check_latency_one_sample(float choke, int src, int dst) {
    Instrument inst;
    inst.init(48000.f);                       // no FxMem: the FX chain stays off
    inst.set_engine(src, ENGINE_TEST_TONE);
    inst.set_engine(dst, ENGINE_SAMPLER);
    inst.sampler_monitor(dst, true);
    inst.set_excitation_sources(dst, true, /*other_deck=*/true, /*audio_in=*/false);
    inst.set_excitation_sources(src, true, /*other_deck=*/false, /*audio_in=*/false);
    inst.set_choke(choke);

    const int kN = 512;
    std::vector<float> tap_src(kN), tap_dst(kN);
    float outL[1], outR[1];
    for (int n = 0; n < kN; ++n) {
        inst.process(nullptr, nullptr, outL, outR, 1);
        tap_src[n] = inst.deck_tap(src, 0);
        tap_dst[n] = inst.deck_tap(dst, 0);
    }

    // Skip the engine-swap fade and the first control block. set_engine()
    // above moves each deck away from the boot default (ENGINE_SYNTH), which
    // is a full swap, not a one-way fade: SoftSwitch (fx_util.h) fades OUT
    // (192 samples) before the swap and fades back IN (192 more) afterwards,
    // so the transition is not idle again -- and outL/outR not scaled down by
    // a still-rising `fade` -- until sample ~384, not the ~192 a single 4 ms
    // Hann ramp would suggest. Measured directly (debug dump, since removed):
    // the diff between dst[n] and fast_tanh(src[n-1]) is a smooth, shrinking
    // residual through n ~ 383 and is exactly 0.0 from n = 384 on. 400 keeps
    // a margin over that measured boundary.
    // Non-vacuity: `checked` proves the source deck was above the noise
    // floor at some sample, which rules out a silent ENGINE_TEST_TONE making
    // the loop pass by never exercising the tap at all. It does NOT prove
    // the tap actually varied sample to sample -- a constant DC source would
    // satisfy `|tap_src[n-1]| > 1e-4f` at every n while making the
    // one-sample-shift comparison above vacuous (a stuck value trivially
    // equals its own shifted copy). ENGINE_TEST_TONE happens to emit a
    // >=110 Hz sine, so today's test does discriminate -- but that is a
    // property of the fixture, not of this guard, so it is pinned directly
    // below rather than left to happen to be true.
    int checked = 0;
    bool varied = false;
    for (int n = 400; n < kN; ++n) {
        CHECK(tap_dst[n] == doctest::Approx(fast_tanh(tap_src[n - 1])));
        if (std::fabs(tap_src[n - 1]) > 1e-4f) ++checked;
        if (tap_src[n] != tap_src[n - 1]) varied = true;
    }
    CHECK(checked > 0);        // the source must actually have been sounding
    CHECK(varied);             // and not merely sounding at a stuck DC value
}

TEST_CASE("deck bus: one sample of latency, both directions, at every CHOKE") {
    for (float choke : {-1.f, -0.5f, 0.f, 0.5f, 1.f}) {
        check_latency_one_sample(choke, PART_A, PART_B);
        check_latency_one_sample(choke, PART_B, PART_A);
    }
}

// Settle the 4 ms engine-swap fade (192 samples at 48 kHz) plus the control
// raster, so `fade` is exactly 1.0 and the engine is the one we asked for.
static void settle(Part& p) {
    float l, r, sl, sr;
    for (int i = 0; i < 1000; ++i) { p.set_deck_in(0.f, 0.f); p.process(0.f, 0.f, l, r, sl, sr); }
}

TEST_CASE("deck bus: with the source off the tap never reaches the engine") {
    Part p;
    p.init(48000.f, 5);
    p.set_engine(ENGINE_SAMPLER);
    p.sampler().set_monitor(true);
    p.set_excitation_sources(true, /*other_deck=*/false, /*audio_in=*/false);
    settle(p);

    float l, r, sl, sr;
    p.set_deck_in(10.f, -10.f);          // absurd on purpose
    p.process(0.f, 0.f, l, r, sl, sr);
    CHECK(l == 0.f);                     // exactly zero, not approximately
    CHECK(r == 0.f);
}

TEST_CASE("deck bus: with the source on the tap reaches the engine") {
    Part p;
    p.init(48000.f, 5);
    p.set_engine(ENGINE_SAMPLER);
    p.sampler().set_monitor(true);
    p.set_excitation_sources(true, /*other_deck=*/true, /*audio_in=*/false);
    settle(p);

    float l, r, sl, sr;
    p.set_deck_in(0.01f, -0.01f);
    p.process(0.f, 0.f, l, r, sl, sr);
    CHECK(l == doctest::Approx(fast_tanh(0.01f)));
    CHECK(r == doctest::Approx(fast_tanh(-0.01f)));
}

TEST_CASE("deck bus: the engine input is bounded") {
    Part p;
    p.init(48000.f, 5);
    p.set_engine(ENGINE_SAMPLER);
    p.sampler().set_monitor(true);
    p.set_excitation_sources(true, /*other_deck=*/true, /*audio_in=*/false);
    settle(p);

    float l, r, sl, sr;
    for (float drive : {1.f, 10.f, 1000.f}) {
        p.set_deck_in(drive, -drive);
        p.process(0.f, 0.f, l, r, sl, sr);
        CHECK(std::fabs(l) <= 1.f);
        CHECK(std::fabs(r) <= 1.f);
        CHECK(std::isfinite(l));
        CHECK(std::isfinite(r));
    }
}

// TWO engines override IPartEngine::consumes_input() to return true:
// SamplerEngine (sampler_engine.h) and BbdEngine (bbd_engine.h). The other
// four -- TEST_TONE, SYNTH, WAVE, BODY -- keep the base default of false
// (engine_iface.h), so Part::process's `if (_engine_wants_in)` guard -- the
// outer guard around the process_in() call site in part.h -- never even calls
// process_in() for them. The `if (_src_deck)` check nested inside that guard
// is therefore structurally unreachable for those four, not merely untested
// by them. (Line numbers deliberately not cited here: this comment already
// went stale once, within this same branch, when the SPKY_DECK_BUS guard was
// inserted above these two ifs and shifted them eight lines -- naming the
// symbols instead of the lines is what keeps this comment from rotting the
// same way again.) So for those four the bit-identity check below proves
// something narrower but still real: no *unconditional* side effect was
// introduced anywhere else in Part::process.
//
// What ENGINE_BBD changed, and what it did not. It did NOT change the
// membership of "the other four" -- BBD replaced SAMPLER's uniqueness, not
// any of those four engines' silence about the guard, and the same four are
// still structurally unreachable. What it DID falsify is the sentence that
// used to follow: "SAMPLER is the one engine today where the guard is
// reachable." There are now two such engines, and for a BBD deck the
// `if (_src_deck)` check is the ONLY thing standing between the hostile tap
// and a delay line that would then circulate it through its own feedback
// path. So BBD, like SAMPLER, is a case that can actually go RED here, and
// it is set up below to be one: both parts get real line memory, because
// BbdEngine::process returns silence with nullptr buffers (documented:
// "nullptr -> the deck is silent") and a silent engine absorbs a leaking tap
// instead of exposing it, which would make exactly the case this comment
// promises to prove the vacuous one.
//
// ENGINE_COUNT below is a build-time tripwire, not a runtime check: it fires
// at compile time the moment a new EngineId is added, forcing whoever adds
// the next engine to come back here, extend the list, and re-check both
// claims above -- "structurally unreachable for the other four" (which needs
// the new engine to leave consumes_input() at its default) and "reachable,
// and proven so, for SAMPLER and BBD" (which needs the new engine to be
// given whatever memory it takes to be audible here, or it joins the list
// vacuously). The consumes_input() census pinned directly below it is the
// runtime half of the same guard.
static_assert(ENGINE_COUNT == 6,
              "a new EngineId was added -- extend the engine list in the "
              "test below, and re-check both claims above: 'structurally "
              "unreachable for the other four' (does the new engine override "
              "consumes_input()?) and 'proven directly for SAMPLER and BBD' "
              "(is the new engine actually audible in the sweep, or does it "
              "join it vacuously?)");

// The runtime half of the tripwire. Everything the comment above argues rests
// on WHICH engines override consumes_input(), and that is not observable from
// an EngineId: an engine that implements process_in and forgets this override
// (the pairing engine_iface.h warns about, which nothing enforces) would
// silently drop out of the "reachable" set and back into the "structurally
// unreachable" one, taking the sweep below with it and changing nothing that
// any assertion could see. Pinned by construction, on the engine objects
// themselves rather than through Part, because Part exposes no accessor for
// its test tone.
TEST_CASE("deck bus: exactly two engines consume their input, and it is "
          "SAMPLER and BBD") {
    TestToneEngine tone;
    SynthEngine    synth;
    WaveEngine     wave;
    BodyEngine     body;
    SamplerEngine  sampler;
    BbdEngine      bbd;
    CHECK_FALSE(tone.consumes_input());
    CHECK_FALSE(synth.consumes_input());
    CHECK_FALSE(wave.consumes_input());
    CHECK_FALSE(body.consumes_input());
    CHECK(sampler.consumes_input());
    CHECK(bbd.consumes_input());
}

// Line memory for the two BBD decks in the sweep below. Static, the idiom
// every other test in this tree uses for a buffer the engine's no-heap
// contract makes the caller own.
static float s_dbus_bbd[2][2][BbdEngine::kCells];

TEST_CASE("deck bus: with the source off, a hostile tap changes nothing -- "
          "proven directly for SAMPLER and BBD, structurally for the rest") {
    for (EngineId e : {ENGINE_TEST_TONE, ENGINE_SYNTH, ENGINE_SAMPLER,
                       ENGINE_WAVE, ENGINE_BODY, ENGINE_BBD}) {
        // Six engines share one loop body, so a bit-identity failure that did
        // not name the engine would send the next reader through all six.
        INFO("engine ", static_cast<int>(e));
        Part a, b;
        a.init(48000.f, 7, nullptr, nullptr, nullptr, 0, s_dbus_bbd[0][0], s_dbus_bbd[0][1]);
        b.init(48000.f, 7, nullptr, nullptr, nullptr, 0, s_dbus_bbd[1][0], s_dbus_bbd[1][1]);
        a.set_engine(e);     b.set_engine(e);
        // Give the engines something to play, or a silent engine makes this
        // pass vacuously -- see the non-silence guard below.
        for (Part* p : {&a, &b}) {
            // LEVEL is pinned to a constant 0.5 with its lane switched OFF, and
            // that is what makes the BBD case discriminating instead of vacuous:
            // LANE_LEVEL *is* the BBD's MIX (bbd_engine.cpp), and at MIX exactly
            // 1.0 the engine's own dry term cancels -- in + 1*(wet - in) == wet --
            // so a tap that leaked into process_in would stay invisible here until
            // one whole delay period later, far outside this 4000-sample window at
            // any musical division. Measured with the _src_deck guard forced open:
            // at LEVEL 1.0 all six engines PASS (the leak is real and undetected),
            // at LEVEL 0.5 engine 5 fails within ~190 samples. The lane is switched
            // off as well so that 0.5 is exact rather than a value a live lane can
            // momentarily clamp back up to 1.0.
            p->set_target_active(LANE_LEVEL, false);
            p->set_target_base(LANE_LEVEL, 0.5f);
            p->mod().set_rate(0.5f);
            // Only matters when e == ENGINE_SAMPLER (a harmless dead flag on
            // the other four's inactive SamplerEngine instance): with the
            // monitor on, SamplerEngine mixes its dry input straight into
            // the output (sampler_engine.cpp:955), so a hostile tap that
            // leaked past a broken _src_deck guard would be audible here,
            // not silently absorbed by _monitor's default-off state.
            p->sampler().set_monitor(true);
        }
        // b is handed a hostile tap it must ignore; a is never told anything.
        float al, ar, asl, asr, bl, br, bsl, bsr;
        float peak = 0.f;
        for (int i = 0; i < 4000; ++i) {
            a.process(0.f, 0.f, al, ar, asl, asr);
            b.set_deck_in(3.f, -3.f);
            b.process(0.f, 0.f, bl, br, bsl, bsr);
            REQUIRE(bl == al);          // bit-identical, not Approx
            REQUIRE(br == ar);
            peak = std::max(peak, std::fabs(al));
        }
        // The sampler runs silent with no buffer (documented: "nullptr ->
        // runs silent"), so it is exempt -- it is covered by Tasks 1 and 4,
        // which drive it through the monitor path. Every other engine must
        // actually have sounded, or the identity above proved nothing. BBD is
        // NOT exempt: it is handed line memory above precisely so it is not,
        // and it clears this floor on its dither alone (BbdLine::SetDither),
        // with no input and no note -- which is also the proof its process()
        // ran at all rather than taking the silent no-buffer path.
        if (e != ENGINE_SAMPLER) {
            INFO("engine ", static_cast<int>(e), " produced silence");
            CHECK(peak > 1e-6f);
        }
    }
}

// Runs the sampler<->sampler scenario for 10 s at 48 kHz with a hot,
// constant, asymmetric input, and reports the output peak plus each deck's
// final tap. `routed` selects whether other_deck is actually wired in --
// false reproduces the "routing never established" case in-tree, so the
// dead-routing baseline below is measured fresh on every run rather than
// hardcoded from a one-off exploration.
static float run_mutual_scenario(bool routed, float* tapA_out = nullptr,
                                  float* tapB_out = nullptr) {
    Instrument inst;
    inst.init(48000.f);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_engine(p, ENGINE_SAMPLER);
        inst.sampler_monitor(p, true);
        inst.set_excitation_sources(p, true, /*other_deck=*/routed, /*audio_in=*/true);
    }

    const int kBlock = 96;
    std::vector<float> inl(kBlock, 0.5f), inr(kBlock, -0.5f);
    std::vector<float> outL(kBlock), outR(kBlock);
    float peak = 0.f;
    for (int b = 0; b < 48000 * 10 / kBlock; ++b) {
        inst.process(inl.data(), inr.data(), outL.data(), outR.data(), kBlock);
        for (int i = 0; i < kBlock; ++i) {
            REQUIRE(std::isfinite(outL[i]));
            REQUIRE(std::isfinite(outR[i]));
            peak = std::max({peak, std::fabs(outL[i]), std::fabs(outR[i])});
        }
    }
    if (tapA_out) *tapA_out = inst.deck_tap(PART_A, 0);
    if (tapB_out) *tapB_out = inst.deck_tap(PART_B, 0);
    return peak;
}

TEST_CASE("deck bus: sampler <-> sampler mutual routing stays finite") {
    // Dead-routing baseline, measured here rather than assumed: with
    // other_deck off, SamplerEngine's dry-at-unity monitor still passes the
    // constant +-0.5 input straight through the MORPH/reverb mix, settling
    // at a fixed, non-zero peak that has nothing to do with the cross-deck
    // bus. A broken _src_deck guard (routing silently never established)
    // would make the "live" run below indistinguishable from this number.
    const float baseline_peak = run_mutual_scenario(/*routed=*/false);

    float tapA = 0.f, tapB = 0.f;
    const float mutual_peak = run_mutual_scenario(/*routed=*/true, &tapA, &tapB);
    MESSAGE("baseline_peak=", baseline_peak, " mutual_peak=", mutual_peak,
            " tapA=", tapA, " tapB=", tapB);

    // Liveness: the mutual loop must clear the dead-routing baseline by a
    // margin no unrouted run could produce (measured live/dead gap here is
    // ~0.29; 0.15 leaves comfortable room on both sides without being loose
    // enough to pass a no-op).
    CHECK(mutual_peak > baseline_peak + 0.15f);
    CHECK(std::fabs(tapA) > 1e-3f);
    CHECK(std::fabs(tapB) > 1e-3f);

    // Bound: per channel this is a two-step loop, x[n] = bound(k + x[n-2])
    // with k = +-0.5 the constant input. Three failure modes are worth
    // naming, because isfinite() on the FINAL output cannot tell any of
    // them apart: the master Limiter (instrument.cpp, applied to l/r after
    // MORPH, downstream of _deck_tap) hard-ceilings whatever reaches it to
    // ~1.0, so a genuinely unbounded internal recursion and a merely
    // saturating one both come out of `mutual_peak` looking the same
    // (measured: a wrong-ordered bound below ALSO reads mutual_peak == 1,
    // just like the correct one's 0.998 -- too close to separate with an
    // output-side ceiling). The discriminating quantity is `_deck_tap`
    // itself, captured before the limiter and before the MORPH mix:
    //  - bound correctly on the sum (fast_tanh(el + _deck_in_l), part.h):
    //    converges to the fixed point of x = tanh(0.5 + x) -- measured
    //    deck_tap ~ 0.881, comfortably inside fast_tanh's contract
    //    |y| <= 1.0 (fast_tanh.h: hard-clamped on the return value, not
    //    merely on the threshold).
    //  - bound on the wrong side (el + fast_tanh(_deck_in_l)): still a
    //    contraction, since the exogenous term is a fixed constant rather
    //    than something that grows -- so it does NOT diverge to inf/NaN
    //    either. It converges instead to the fixed point of
    //    x = 0.5 + tanh(x) -- measured deck_tap ~ 1.381, which breaches
    //    fast_tanh's |y| <= 1.0 contract because the sum was never actually
    //    passed through the bound.
    //  - bound removed entirely: x[n] = 0.5 + x[n-2] is unbounded linear
    //    growth -- not inf/NaN inside a 10 s run, but it blows past 1.0
    //    within a few hundred samples, so the same ceiling catches it too.
    // 1.0 is not a round number chosen for convenience: it is fast_tanh's
    // own hard-clamped range, so any deck_tap that exceeds it is direct,
    // structural proof the bound was not applied to that sum.
    //
    // Scoped to this fixture: `inst.init(48000.f)` above leaves the FX chain
    // off (no FxMem passed in), so `_deck_tap` here is exactly the engine's
    // raw, unbounded-except-for-fast_tanh output. With the FX chain engaged
    // (Grit/Flux/Comp/Reverb, as bench's `inst_worst_deck_bus` row runs),
    // gain staged downstream of `_deck_tap` can legitimately push a deck's
    // post-FX output above 1.0 -- fast_tanh only bounds what reaches the
    // *engine input*, not what a deck emits after its own FX chain. The
    // `<= 1.0f` proof above is specific to this fixture's FX-off setup, not
    // a claim that `deck_tap` is bounded in general.
    CHECK(std::fabs(tapA) <= 1.0f);
    CHECK(std::fabs(tapB) <= 1.0f);
}

