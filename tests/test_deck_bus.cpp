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
    int checked = 0;
    for (int n = 400; n < kN; ++n) {
        CHECK(tap_dst[n] == doctest::Approx(fast_tanh(tap_src[n - 1])));
        if (std::fabs(tap_src[n - 1]) > 1e-4f) ++checked;
    }
    CHECK(checked > 0);        // the source must actually have been sounding
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

