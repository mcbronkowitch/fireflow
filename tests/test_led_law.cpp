#include <doctest/doctest.h>
#include "instrument.h"
#include "fx/limiter.h"
#include "mod/song_form.h"
#include "vcv/src/led_law.hpp"
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
    // .epsilon(0.05) reads as a 5% band; it is not one. doctest scales
    // epsilon by (1 + max(|lhs|,|rhs|)), so at v = 1 this is a ~0.10 ABSOLUTE
    // band -- against a measured round-trip error of about 0.002 at these
    // four points (0.2493 / 0.5021 / 0.7490 / 1.0 for v = 0.25/0.5/0.75/1.0).
    // Loose on purpose: this gate is for gamma direction, not precision, and
    // 0.002 already proves the curve.
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

TEST_CASE("led G10: a light value survives the law unchanged") {
    // The REC lamp's constants were tuned as light output. Round-tripping them
    // through the gamma must land back on the same brightness, inside the
    // 16-step raster -- and the three states must stay three states.
    // doctest scales epsilon by (1 + max(|lhs|,|rhs|)), not by v alone: at
    // v = 0.15, .epsilon(0.06) is an ABSOLUTE band of +-0.069 -- a +-46%
    // window against a v this small, not the +-6% it reads as. The measured
    // round-trip error at 16 steps is at most 0.033 (2 of 15 steps, at
    // v = 0.50 and v = 0.70), so 0.03 is the tightest epsilon that still
    // clears every point with margin.
    const int steps = 16;
    for (float v : {0.15f, 0.25f, 0.50f, 0.70f, 1.0f}) {
        const float back = static_cast<float>(spkyled::duty_from_light(v, steps))
                         / static_cast<float>(steps - 1);
        CHECK(back == doctest::Approx(v).epsilon(0.03));
    }
    CHECK(spkyled::duty_from_light(0.15f, steps)
          != spkyled::duty_from_light(0.25f, steps));
    CHECK(spkyled::duty_from_light(0.f, steps) == 0);
}

TEST_CASE("led G6: every light is written, and a modulating lane moves") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_rate(0, 2.0f);
    inst.set_range(0, 1.f);
    inst.set_depth(0, 1.f);

    spkyled::Panel panel;
    int duty[spkyvcv::NUM_LIGHTS];
    for (int i = 0; i < spkyvcv::NUM_LIGHTS; ++i) duty[i] = -1;

    const float dt = 1.f / 750.f;
    const int steps = 16;
    settle(inst, 200);
    spkyled::fill(inst, panel, dt, steps, false, duty);

    for (int i = 0; i < spkyvcv::NUM_LIGHTS; ++i)
        CHECK_MESSAGE(duty[i] >= 0, "light ", i, " was never written");

    // GATE: commit c3ac938 claims the gate lamps keep the behaviour they had
    // -- straight through, no envelope. Read the live state rather than
    // assuming it, so this is a check on the mapping, not on the sequencer.
    CHECK(duty[spkyvcv::GATE_A_L] == (inst.gate(0) ? steps - 1 : 0));
    // CEIL: the instrument is idle here (default drive, no signal pushed
    // near the knee), so the ceiling lamp must read dark.
    CHECK(duty[spkyvcv::CEIL_L] == 0);

    // The SOURCE excursion light must actually change over time.
    int lo = 99, hi = -1;
    for (int k = 0; k < 400; ++k) {
        settle(inst, 64);
        spkyled::fill(inst, panel, dt, 16, false, duty);
        lo = std::min(lo, duty[spkyvcv::SRC_A_L]);
        hi = std::max(hi, duty[spkyvcv::SRC_A_L]);
    }
    CHECK(hi > lo);

    // At MOD 0 the same light must go all the way out, not merely dim. Long
    // enough for a full swing to fall below kEnvOff, with margin; derived, so
    // re-tuning either constant does not turn this into an arithmetic failure.
    inst.set_depth(0, 0.f);
    settle(inst, 200);
    const int decay = static_cast<int>(1.5f * spkyled::kEnvFall
                                       * std::log(1.f / spkyled::kEnvOff) / dt);
    for (int k = 0; k < decay; ++k) spkyled::fill(inst, panel, dt, 16, false, duty);
    CHECK(duty[spkyvcv::SRC_A_L] == 0);
}

static void arm_song_deck(Instrument& inst) {
    inst.set_form(0, static_cast<int>(Principle::Hierarchical));
    inst.set_song(0, static_cast<int>(SongMode::AAAB));
    inst.set_rate(0, 1.f);
    inst.set_shape(0, 1.f);
    inst.set_density(0, 1.f);
    inst.set_step(0, true, 8);
}

static void process_until_pattern(Instrument& inst, uint8_t want) {
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 200000; ++i) {
        if (inst.active_pattern(0) == want) return;
        inst.process(nullptr, nullptr, &l, &r, 1);
    }
    FAIL("active_pattern did not reach ", (int)want, " within the safety bound");
}

TEST_CASE("led S1: a snapshot edge produces a flash, then dark") {
    Instrument inst;
    inst.init(48000.f);
    arm_song_deck(inst);
    process_until_pattern(inst, 0);

    spkyled::Panel panel;
    int duty[spkyvcv::NUM_LIGHTS] = {};
    const float dt = 1.f / 750.f;
    const int steps = 16;
    spkyled::fill(inst, panel, dt, steps, false, duty);
    CHECK(duty[spkyvcv::SONG_A_L] == 0);          // first fill arms, no flash

    float l = 0.f, r = 0.f;
    for (int i = 0; i < 200000; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if (inst.active_pattern(0) == 1) break;
    }
    REQUIRE(inst.active_pattern(0) == 1);
    spkyled::fill(inst, panel, dt, steps, false, duty);
    CHECK(duty[spkyvcv::SONG_A_L] == steps - 1);  // A→B flash

    const int hold = static_cast<int>(spkyled::kSongFlash / dt) + 2;
    for (int k = 0; k < hold / 2; ++k)
        spkyled::fill(inst, panel, dt, steps, false, duty);
    CHECK(duty[spkyvcv::SONG_A_L] == steps - 1);  // still lit mid-flash
    for (int k = hold / 2; k < hold; ++k)
        spkyled::fill(inst, panel, dt, steps, false, duty);
    CHECK(duty[spkyvcv::SONG_A_L] == 0);          // dark after 150 ms

    for (int i = 0; i < 200000; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if (inst.active_pattern(0) == 0) break;
    }
    REQUIRE(inst.active_pattern(0) == 0);
    spkyled::fill(inst, panel, dt, steps, false, duty);
    CHECK(duty[spkyvcv::SONG_A_L] == steps - 1);  // B→A same flash
}

TEST_CASE("led S2: no flash on first fill") {
    Instrument inst;
    inst.init(48000.f);
    arm_song_deck(inst);
    process_until_pattern(inst, 1);

    spkyled::Panel panel;
    int duty[spkyvcv::NUM_LIGHTS] = {};
    spkyled::fill(inst, panel, 1.f / 750.f, 16, false, duty);
    CHECK(duty[spkyvcv::SONG_A_L] == 0);
}

TEST_CASE("led S3: OFF stays dark") {
    Instrument inst;
    inst.init(48000.f);
    arm_song_deck(inst);
    inst.set_song(0, static_cast<int>(SongMode::Off));

    spkyled::Panel panel;
    int duty[spkyvcv::NUM_LIGHTS] = {};
    const float dt = 1.f / 750.f;
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 50000; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if ((i % 64) == 0) {
            spkyled::fill(inst, panel, dt, 16, false, duty);
            CHECK(duty[spkyvcv::SONG_A_L] == 0);
        }
    }
}

TEST_CASE("led: TEMPO_L is a metronome pulse on the transport beat") {
    // Wiring gate, not a helper test: fill() used to write TEMPO_L to 0
    // every block (the LED-feedback round deferred it). A 50% square on
    // beat_phase would still be on a quarter of the way through the beat;
    // a pulse must already be off there. 120 BPM so the arithmetic is
    // exact: one beat is 0.5 s = 24000 samples at 48 kHz.
    Instrument inst;
    inst.init(48000.f);
    inst.set_tempo_bpm(120.f);

    spkyled::Panel panel;
    int duty[spkyvcv::NUM_LIGHTS];
    const float dt = 1.f / 750.f;
    const int steps = 16;

    spkyled::fill(inst, panel, dt, steps, false, duty);
    CHECK(duty[spkyvcv::TEMPO_L] == steps - 1);     // downbeat

    settle(inst, 6000);                             // 0.125 s, phase ~0.25
    spkyled::fill(inst, panel, dt, steps, false, duty);
    CHECK(duty[spkyvcv::TEMPO_L] == 0);             // pulse already over

    settle(inst, 18000);                            // rest of the beat
    spkyled::fill(inst, panel, dt, steps, false, duty);
    CHECK(duty[spkyvcv::TEMPO_L] == steps - 1);     // next downbeat
}

TEST_CASE("led law: MODBTN lamp is the latch state") {
    Instrument inst;
    inst.init(48000.f);

    spkyled::Panel p;
    int duty[spkyvcv::NUM_LIGHTS] = {0};
    const float dt = 1.f / 750.f;
    const int steps = 16;

    spkyled::fill(inst, p, dt, steps, /*mod_latched=*/true, duty);
    CHECK(duty[spkyvcv::MODBTN_L] == steps - 1);
    CHECK(duty[spkyvcv::SHIFTBTN_L] == 0);   // SHIFT stays reserved and dark

    spkyled::fill(inst, p, dt, steps, /*mod_latched=*/false, duty);
    CHECK(duty[spkyvcv::MODBTN_L] == 0);
}

TEST_CASE("led: the envelope attacks instantly and falls slowly") {
    spkyled::Lamp lamp;
    const float dt = 1.f / 750.f;               // the control rate used in Rack
    lamp.follow(0.8f, dt);
    CHECK(lamp.env == doctest::Approx(0.8f));   // instant attack
    lamp.follow(0.f, dt);
    CHECK(lamp.env > 0.7f);                     // one tick barely moves it
    // Four release constants' worth, derived rather than counted: kEnvFall is
    // a by-ear tuning candidate, and a hardcoded tick count would start
    // failing on arithmetic the first time it is re-tuned. At 2.67 constants
    // the envelope is still at 0.056 and this assertion would fail without
    // catching anything.
    const int ticks = static_cast<int>(4.f * spkyled::kEnvFall / dt);
    for (int i = 0; i < ticks; ++i) lamp.follow(0.f, dt);
    CHECK(lamp.env < 0.05f);                    // but it does let go
}
