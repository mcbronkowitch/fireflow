#include <doctest/doctest.h>
#include "instrument.h"
#include "fx/limiter.h"
#include <cmath>

using namespace spky;

// A settled instrument on deck 0, texture lanes moving.
static void settle(Instrument& inst, int blocks = 2000) {
    float l = 0.f, r = 0.f;
    for (int i = 0; i < blocks; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
}

TEST_CASE("led G0: the excursion is the modulation alone, never the knob") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_rate(0, 0.5f);
    inst.set_range(0, 1.f);
    inst.set_target_base(0, LANE_SOURCE, 0.9f);

    SUBCASE("MOD 0 means no excursion, however high the knob sits") {
        inst.set_depth(0, 0.f);
        settle(inst);
        CHECK(inst.lane_excursion(0, LANE_SOURCE) == doctest::Approx(0.f));
        CHECK(inst.target_value(0, LANE_SOURCE) == doctest::Approx(0.9f));
    }
    SUBCASE("MOD up means the excursion moves while the knob does not") {
        inst.set_depth(0, 1.f);
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 4000; ++i) {
            settle(inst, 20);
            const float e = inst.lane_excursion(0, LANE_SOURCE);
            lo = std::fmin(lo, e);
            hi = std::fmax(hi, e);
        }
        CHECK(hi - lo > 0.1f);          // it actually swings
        CHECK(hi <= 1.0f);
        CHECK(lo >= -1.0f);
    }
}

// Feed a sine of the given amplitude through a settled limiter and return
// the squash it reports at the end.
static float squash_at(float knob, float amp) {
    Limiter lim;
    lim.init();
    lim.set_drive(knob);
    for (int i = 0; i < 20000; ++i) { float a = 0.f, b = 0.f; lim.process(a, b); }
    for (int i = 0; i < 4000; ++i) {
        float s = amp * std::sin(6.2831853f * 300.f * i / 48000.f);
        float l = s, r = s;
        lim.process(l, r);
    }
    return lim.squash();
}

TEST_CASE("led G9: the ceiling observer tracks the bend, and clears again") {
    // Ordered on purpose: written as independent cases the "clears again"
    // clause passes from the init value and could never catch a stale
    // reading. At DRIVE 0 on purpose: _pre == 1 there, and that is the only
    // setting where the transparent early return in Limiter::process is
    // reachable at all -- so it is the only setting where the observer's
    // two failure modes exist.
    Limiter lim;
    lim.init();
    lim.set_drive(0.f);
    for (int i = 0; i < 20000; ++i) { float a = 0.f, b = 0.f; lim.process(a, b); }

    // A sustained tone past the knee. The lamp must not blink at the
    // waveform's zero crossings, so sample it at every phase and take the
    // worst reading, not the last one.
    float worst = 1e9f;
    for (int i = 0; i < 4000; ++i) {
        const float s = 0.95f * std::sin(6.2831853f * 300.f * i / 48000.f);
        float l = s, r = s;
        lim.process(l, r);
        if (i > 2000) worst = std::fmin(worst, lim.squash());   // past the attack
    }
    CHECK(worst > 0.f);

    for (int i = 0; i < 48000; ++i) {                // silence, long enough to settle
        float a = 0.f, b = 0.f;
        lim.process(a, b);
    }
    CHECK(lim.squash() == doctest::Approx(0.f));

    // The band the design exists for: bending, but no gain reduction yet.
    CHECK(squash_at(0.40f, 0.60f) > 0.f);
}

#include "vcv/src/led_law.hpp"

TEST_CASE("led G1: dark means zero modulation, and nothing else does") {
    CHECK(spkyled::duty(spkyled::intensity(0.f, 0.f), 16) == 0);
    CHECK(spkyled::duty(spkyled::intensity(0.5f, 0.f), 16) > 0);
    CHECK(spkyled::duty(spkyled::intensity(0.5f, 0.5f), 16) > 0);
}

TEST_CASE("led G2: no non-zero intensity is quantised away to off") {
    for (int i = 1; i <= 10000; ++i) {
        const float v = static_cast<float>(i) / 10000.f;
        CHECK(spkyled::duty(v, 16) >= 1);
    }
    CHECK(spkyled::duty(0.f, 16) == 0);
}

TEST_CASE("led G3: the step count is the mux width and every step is reached") {
    for (int steps : {8, 16}) {
        bool seen[64] = {false};
        for (int i = 0; i <= 100000; ++i) {
            const float v = static_cast<float>(i) / 100000.f;
            const int d = spkyled::duty(v, steps);
            REQUIRE(d >= 0);
            REQUIRE(d < steps);
            seen[d] = true;
        }
        for (int d = 0; d < steps; ++d)
            CHECK_MESSAGE(seen[d], "step ", d, " of ", steps, " unreachable");
    }
}

TEST_CASE("led G4: gamma runs in the perceptual direction") {
    const int steps = 16;
    const int mid   = spkyled::duty(0.5f, steps);
    const int lin   = static_cast<int>(0.5f * (steps - 1) + 0.5f);
    CHECK(mid < lin - 1);                       // measurably BELOW linear
    // ... and it is perceptually linear: duty^(1/gamma) tracks the input.
    // Measured on a fine ladder rather than on the panel's 16 steps: down
    // there the raster dominates the curve -- duty(0.25, 16) is 1, and
    // (1/15)^(1/2.2) is 0.292 against 0.25, a 14% error that says nothing
    // about gamma. At 256 steps this tests the law instead of the raster.
    for (float v : {0.25f, 0.5f, 0.75f, 1.0f}) {
        const float d = static_cast<float>(spkyled::duty(v, 256)) / 255.f;
        CHECK(std::pow(d, 1.f / spkyled::kGamma) == doctest::Approx(v).epsilon(0.05));
    }
}

TEST_CASE("led G5: the trough scales with depth") {
    const float deep    = spkyled::intensity(0.9f, 0.f);
    const float shallow = spkyled::intensity(0.2f, 0.f);
    CHECK(deep > shallow);
    // A lane frozen at its own peak stays bright rather than fading out.
    CHECK(spkyled::intensity(0.9f, 0.9f) == doctest::Approx(0.9f));
}

TEST_CASE("led: the envelope attacks instantly and falls slowly") {
    spkyled::Lamp lamp;
    const float dt = 1.f / 750.f;               // the control rate used in Rack
    lamp.follow(0.8f, dt);
    CHECK(lamp.env == doctest::Approx(0.8f));   // instant attack
    lamp.follow(0.f, dt);
    CHECK(lamp.env > 0.7f);                     // one tick barely moves it
    // 8 s is four times kEnvFall; at 5.3 s the envelope is still at 0.056 and
    // this would fail on arithmetic rather than on a defect.
    for (int i = 0; i < 6000; ++i) lamp.follow(0.f, dt);
    CHECK(lamp.env < 0.05f);                    // but it does let go
}
