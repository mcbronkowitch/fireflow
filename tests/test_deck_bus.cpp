#include <doctest/doctest.h>
#include <cmath>
#include "parts/part.h"
#include "util/fast_tanh.h"
using namespace spky;

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
