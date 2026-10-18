#include <doctest/doctest.h>
#include "instrument.h"
#include "fx/limiter.h"
#include "mod/song_form.h"
#include "fx/flux.h"
#include "../bench/audition/init_patch.h"
#include "control/led_law.hpp"
#include <cmath>
#include <cstdlib>
#include <vector>

using namespace spky;

static spkyled::LedInputs latched() {
    spkyled::LedInputs in;
    in.mod_latched = true;
    return in;
}

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

TEST_CASE("led: kLedSteps is the coupon's measured step count") {
    // PROVISIONAL, like the constant itself: 8 stands in until the coupon
    // session writes docs/hardware/p6b2-led-and-touch-measured.md, and that
    // session may change it. When it does, change this pin in the same commit.
    CHECK(spkyled::kLedSteps == 8);
}

TEST_CASE("led: duty matches the gamma reference at every step") {
    // The reference is the law as it was written before the thresholds
    // replaced the per-call pow: max(1, int(v^gamma * (steps-1) + 0.5)).
    // The threshold table is exact except on a boundary itself, where float
    // formulations can round either way -- so intensities within 1e-5 of a
    // threshold t_k = ((k - 0.5) / (steps - 1))^(1/gamma) are skipped.
    int compared = 0;
    for (int steps : {4, 6, 8, 16}) {
        for (int i = 1; i <= 1000; ++i) {
            const double v = i / 1000.0;
            bool nearEdge = false;
            for (int k = 1; k < steps; ++k) {
                const double tk = std::pow((k - 0.5) / (steps - 1), 1.0 / 2.2);
                if (std::fabs(v - tk) < 1e-5) nearEdge = true;
            }
            if (nearEdge) continue;
            int want = static_cast<int>(std::pow(v, 2.2) * (steps - 1) + 0.5);
            if (want < 1) want = 1;
            CHECK_MESSAGE(spkyled::duty(static_cast<float>(v), steps) == want,
                          "steps ", steps, " v ", v);
            ++compared;
        }
    }
    CHECK(compared > 3900);                     // the skip is rare, not a hole
}

TEST_CASE("led: duty re-tables when the step count changes") {
    // One cached table serves one step count at a time; alternating must not
    // leak the previous count's thresholds.
    // 0.6^2.2 = 0.325: the step is 5 of 15, 1 of 3, 2 of 5 and 2 of 7. A
    // mid-range intensity, because 1.0 clears every threshold of a stale table
    // too and would hide the leak.
    for (int round = 0; round < 3; ++round) {
        CHECK(spkyled::duty(0.6f, 16) == 5);
        CHECK(spkyled::duty(0.6f, 4)  == 1);
        CHECK(spkyled::duty(0.6f, 6)  == 2);
        CHECK(spkyled::duty(0.6f, 8)  == 2);    // the shipped count has its own table
        CHECK(spkyled::duty(1.f, 16) == 15);
    }
}

TEST_CASE("led: duty_from_light is the direct rounding, lifted to one step") {
    for (int steps : {4, 6, 8, 16}) {
        for (int i = 1; i <= 1000; ++i) {
            const float l = static_cast<float>(i) / 1000.f;
            int want = static_cast<int>(l * (steps - 1) + 0.5f);
            if (want < 1) want = 1;
            CHECK_MESSAGE(spkyled::duty_from_light(l, steps) == want,
                          "steps ", steps, " light ", l);
        }
        CHECK(spkyled::duty_from_light(0.f, steps) == 0);
    }
}

TEST_CASE("led: ffctl::LightId carries every lamp the law writes") {
    CHECK((int)ffctl::NUM_LIGHTS == 17);
    CHECK((int)ffctl::MODBTN_L == 15);
    CHECK((int)ffctl::SHIFTBTN_L == 16);
}

TEST_CASE("led G6: every light is written") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_rate(0, 2.0f);
    inst.set_range(0, 1.f);
    inst.set_depth(0, 1.f);

    spkyled::Panel panel;
    int duty[ffctl::NUM_LIGHTS];
    for (int i = 0; i < ffctl::NUM_LIGHTS; ++i) duty[i] = -1;

    const float dt = 1.f / 750.f;
    const int steps = 16;
    settle(inst, 200);
    spkyled::fill(inst, panel, dt, steps, {}, duty);

    for (int i = 0; i < ffctl::NUM_LIGHTS; ++i)
        CHECK_MESSAGE(duty[i] >= 0, "light ", i, " was never written");

    // GATE: commit c3ac938 claims the gate lamps keep the behaviour they had
    // -- straight through, no envelope. Read the live state rather than
    // assuming it, so this is a check on the mapping, not on the sequencer.
    CHECK(duty[ffctl::GATE_A_L] == (inst.gate(0) ? steps - 1 : 0));
    // MODBTN: unlatched, it shows the limiter. The instrument is idle here
    // (default drive, no signal pushed near the knee), so it must read dark.
    CHECK(duty[ffctl::MODBTN_L] == 0);
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
    int duty[ffctl::NUM_LIGHTS] = {};
    const float dt = 1.f / 750.f;
    const int steps = 16;
    spkyled::fill(inst, panel, dt, steps, {}, duty);
    CHECK(duty[ffctl::SONG_A_L] == 0);          // first fill arms, no flash

    float l = 0.f, r = 0.f;
    for (int i = 0; i < 200000; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if (inst.active_pattern(0) == 1) break;
    }
    REQUIRE(inst.active_pattern(0) == 1);
    spkyled::fill(inst, panel, dt, steps, {}, duty);
    CHECK(duty[ffctl::SONG_A_L] == steps - 1);  // A→B flash

    const int hold = static_cast<int>(spkyled::kSongFlash / dt) + 2;
    for (int k = 0; k < hold / 2; ++k)
        spkyled::fill(inst, panel, dt, steps, {}, duty);
    CHECK(duty[ffctl::SONG_A_L] == steps - 1);  // still lit mid-flash
    for (int k = hold / 2; k < hold; ++k)
        spkyled::fill(inst, panel, dt, steps, {}, duty);
    CHECK(duty[ffctl::SONG_A_L] == 0);          // dark after 150 ms

    for (int i = 0; i < 200000; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if (inst.active_pattern(0) == 0) break;
    }
    REQUIRE(inst.active_pattern(0) == 0);
    spkyled::fill(inst, panel, dt, steps, {}, duty);
    CHECK(duty[ffctl::SONG_A_L] == steps - 1);  // B→A same flash
}

TEST_CASE("led S2: no flash on first fill") {
    Instrument inst;
    inst.init(48000.f);
    arm_song_deck(inst);
    process_until_pattern(inst, 1);

    spkyled::Panel panel;
    int duty[ffctl::NUM_LIGHTS] = {};
    spkyled::fill(inst, panel, 1.f / 750.f, 16, {}, duty);
    CHECK(duty[ffctl::SONG_A_L] == 0);
}

TEST_CASE("led S3: OFF stays dark") {
    Instrument inst;
    inst.init(48000.f);
    arm_song_deck(inst);
    inst.set_song(0, static_cast<int>(SongMode::Off));

    spkyled::Panel panel;
    int duty[ffctl::NUM_LIGHTS] = {};
    const float dt = 1.f / 750.f;
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 50000; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if ((i % 64) == 0) {
            spkyled::fill(inst, panel, dt, 16, {}, duty);
            CHECK(duty[ffctl::SONG_A_L] == 0);
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
    int duty[ffctl::NUM_LIGHTS];
    const float dt = 1.f / 750.f;
    const int steps = 16;

    spkyled::fill(inst, panel, dt, steps, {}, duty);
    CHECK(duty[ffctl::TEMPO_L] == steps - 1);     // downbeat

    settle(inst, 6000);                             // 0.125 s, phase ~0.25
    spkyled::fill(inst, panel, dt, steps, {}, duty);
    CHECK(duty[ffctl::TEMPO_L] == 0);             // pulse already over

    settle(inst, 18000);                            // rest of the beat
    spkyled::fill(inst, panel, dt, steps, {}, duty);
    CHECK(duty[ffctl::TEMPO_L] == steps - 1);     // next downbeat
}

TEST_CASE("led law: MODBTN lamp double-pulses while the latch holds") {
    Instrument inst;
    inst.init(48000.f);

    spkyled::Panel p;
    int duty[ffctl::NUM_LIGHTS] = {0};
    const float dt = 1.f / 750.f;           // the control rate used in Rack
    const int steps = 16;
    const int ticks = 1500;                 // two seconds of the law

    SUBCASE("dark at every phase while the latch is open") {
        for (int i = 0; i < ticks; ++i) {
            spkyled::fill(inst, p, dt, steps, {}, duty);
            REQUIRE(duty[ffctl::MODBTN_L] == 0);
            REQUIRE(duty[ffctl::SHIFTBTN_L] == 0);  // no input, no switch: dark
        }
    }

    SUBCASE("hard on/off, and it really goes dark") {
        // MODBTN_L has two jobs (latch, else limiter, spec 2026-10-07 §5).
        // Latched, the pulse must take over the whole duty range; an idle
        // limiter alone would stay at 0 and fail sawOn.
        bool sawOn = false, sawOff = false;
        for (int i = 0; i < ticks; ++i) {
            spkyled::fill(inst, p, dt, steps, latched(), duty);
            const int d = duty[ffctl::MODBTN_L];
            REQUIRE((d == 0 || d == steps - 1));   // a mode, never a level
            sawOn  = sawOn  || d == steps - 1;
            sawOff = sawOff || d == 0;
        }
        CHECK(sawOn);
        CHECK(sawOff);   // the steady lamp this replaced never reaches here
    }

    // The shape is the point. Counting edges alone cannot tell a double
    // pulse from an even blink of the same rate, so this measures the RUN
    // LENGTHS: every lit run one flash long, and the dark runs falling into
    // exactly two families -- the short gap inside a pair, and the long tail
    // between pairs.
    SUBCASE("two flashes and a long tail, not an even blink") {
        // Expected lengths are derived from the constants, never counted:
        // all three are by-eye tuning candidates, and hardcoded tick counts
        // would start failing on arithmetic the first time they are retuned.
        const int expOn  = static_cast<int>(spkyled::kModPulseOn  / dt + 0.5f);
        const int expGap = static_cast<int>(spkyled::kModPulseGap / dt + 0.5f);
        const int expTail = static_cast<int>(
            (spkyled::kModPulsePeriod - 2.f * spkyled::kModPulseOn
             - spkyled::kModPulseGap) / dt + 0.5f);
        const int expPairs = static_cast<int>(ticks * dt / spkyled::kModPulsePeriod);

        // The tail has to dominate the gap, or the pair stops reading as a
        // pair. This is a gate on the constants themselves, not on the code.
        CHECK(expTail > 2 * expGap);

        std::vector<int> onRun, offRun;
        bool prev = false, firstOn = false;
        int  run  = 0;
        for (int i = 0; i < ticks; ++i) {
            spkyled::fill(inst, p, dt, steps, latched(), duty);
            const bool on = duty[ffctl::MODBTN_L] != 0;
            if (i == 0) { prev = firstOn = on; run = 1; continue; }
            if (on == prev) { ++run; continue; }
            (prev ? onRun : offRun).push_back(run);
            prev = on;
            run  = 1;
        }
        // The window cuts the first run short (the trailing one is never
        // pushed, so only the leading partial needs dropping).
        REQUIRE(!onRun.empty());
        REQUIRE(!offRun.empty());
        if (firstOn) onRun.erase(onRun.begin());
        else         offRun.erase(offRun.begin());

        REQUIRE(!onRun.empty());
        for (int r : onRun)
            CHECK(std::abs(r - expOn) <= 2);        // every flash the same

        int shorts = 0, longs = 0;
        for (int r : offRun) {
            if (std::abs(r - expGap) <= 2) ++shorts;
            else if (std::abs(r - expTail) <= 2) ++longs;
            else FAIL("dark run of " << r << " ticks is neither gap nor tail");
        }
        CHECK(shorts > 0);
        CHECK(longs > 0);
        CHECK(shorts == longs);                     // one gap per tail: a PAIR
        CHECK(longs >= expPairs - 1);               // and the cadence is right
        CHECK(longs <= expPairs);
    }
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

// ---- The 2026-10-07 lamp meanings (spec 2026-10-18 rev-a-p6b2 §4.2) --------

namespace {

constexpr float kBlockDt = 64.f / 48000.f;     // one fill() per 64 samples

struct LampTape {
    std::vector<float> echo[PART_COUNT][2];
    LampTape() {
        for (int p = 0; p < PART_COUNT; ++p)
            for (int ch = 0; ch < 2; ++ch)
                echo[p][ch].resize(Flux::kMaxSamples);
    }
    FxMem mem() {
        FxMem m;
        for (int p = 0; p < PART_COUNT; ++p)
            for (int ch = 0; ch < 2; ++ch)
                m.echo[p][ch] = echo[p][ch].data();
        return m;
    }
};

// Run `seconds`, one fill() per 64 samples; returns how many blocks the LVL
// lamp of `part` was lit and dark, and fails if a reading disagrees with the
// level it was taken from.
struct LitDark { int lit = 0; int dark = 0; };

LitDark run_lvl(Instrument& inst, spkyled::Panel& panel, int part, float seconds,
                const int steps) {
    LitDark r;
    int duty[ffctl::NUM_LIGHTS] = {};
    const int id = part == 0 ? ffctl::LVL_A_L : ffctl::LVL_B_L;
    const int blocks = static_cast<int>(seconds * 48000.f / 64.f);
    for (int b = 0; b < blocks; ++b) {
        settle(inst, 64);
        spkyled::fill(inst, panel, kBlockDt, steps, {}, duty);
        const bool above = inst.deck_out_level(part) > spkyled::kLvlFloor;
        REQUIRE(duty[id] == (above ? steps - 1 : 0));
        (above ? r.lit : r.dark)++;
    }
    return r;
}

}  // namespace

TEST_CASE("led: LVL is lit exactly while the deck delivers signal") {
    const int steps = spkyled::kLedSteps;

    SUBCASE("a playing deck is lit, at full, and a deck at LVL 0 is dark") {
        Instrument inst;
        inst.init(48000.f);
        audition::apply_init_patch(inst);
        spkyled::Panel panel;
        settle(inst, 96000);                                   // 2 s
        const LitDark on = run_lvl(inst, panel, 0, 2.f, steps);
        CHECK(on.lit > 0);
        CHECK(on.dark == 0);

        inst.set_part_level(0, 0.f);
        settle(inst, 24000);
        const LitDark off = run_lvl(inst, panel, 0, 2.f, steps);
        CHECK(off.lit == 0);
        CHECK(off.dark > 0);
    }

    SUBCASE("a STEP deck goes dark between its notes and lit with them") {
        Instrument inst;
        inst.init(48000.f);
        audition::apply_init_patch(inst);
        inst.set_voice_decay(1, 0.f);                          // deck B: WAVE, STEP
        spkyled::Panel panel;
        settle(inst, 96000);
        const LitDark d = run_lvl(inst, panel, 1, 10.f, steps);
        CHECK_MESSAGE(d.lit > 0, "lit ", d.lit, " dark ", d.dark);
        CHECK_MESSAGE(d.dark > 0, "lit ", d.lit, " dark ", d.dark);
    }
}

TEST_CASE("led: FTIME flashes once per FLUX period, only while FLUX is engaged") {
    const int steps = spkyled::kLedSteps;
    int duty[ffctl::NUM_LIGHTS] = {};

    SUBCASE("engaged: one flash per period") {
        LampTape tape;
        Instrument inst;
        inst.init(48000.f, tape.mem());
        inst.set_fx_on(PART_A, FxBlock::Flux, true);
        inst.set_flux_mix(PART_A, 1.f);
        inst.set_flux_rate(PART_A, 1);
        settle(inst, 4800);
        REQUIRE(inst.flux_engaged(PART_A));
        const float delay = inst.flux_delay_target_for_test(PART_A);
        REQUIRE(delay > 0.1f);
        REQUIRE(delay < 5.f);

        spkyled::Panel panel;
        int flashes = 0, lit_blocks = 0;
        bool was = false;
        for (int b = 0; b < 10 * 750; ++b) {                   // 10 s
            settle(inst, 64);
            spkyled::fill(inst, panel, kBlockDt, steps, {}, duty);
            const bool on = duty[ffctl::FTIME_A_L] != 0;
            REQUIRE((duty[ffctl::FTIME_A_L] == 0 || duty[ffctl::FTIME_A_L] == steps - 1));
            if (on && !was) ++flashes;
            if (on) ++lit_blocks;
            was = on;
            CHECK(duty[ffctl::FTIME_B_L] == 0);                // the other deck is idle
        }
        CHECK(flashes > 0);
        CHECK(std::fabs(static_cast<float>(flashes) - 10.f / delay) <= 1.f);
        // A flash, not a steady light.
        const float lit_s = static_cast<float>(lit_blocks) * kBlockDt;
        CHECK(lit_s <= static_cast<float>(flashes + 1) * (spkyled::kFtimeFlash + kBlockDt));
        CHECK(lit_s > 0.f);
    }

    SUBCASE("switching FLUX off ends a flash in progress at once") {
        LampTape tape;
        Instrument inst;
        inst.init(48000.f, tape.mem());
        inst.set_fx_on(PART_A, FxBlock::Flux, true);
        inst.set_flux_mix(PART_A, 1.f);
        inst.set_flux_rate(PART_A, 1);
        settle(inst, 4800);
        REQUIRE(inst.flux_engaged(PART_A));
        spkyled::Panel panel;
        int guard = 0;
        do {                                                   // run to the start of a flash
            settle(inst, 64);
            spkyled::fill(inst, panel, kBlockDt, steps, {}, duty);
        } while (duty[ffctl::FTIME_A_L] == 0 && ++guard < 10 * 750);
        REQUIRE(duty[ffctl::FTIME_A_L] != 0);

        inst.set_fx_on(PART_A, FxBlock::Flux, false);
        int blocks = 0;
        do {
            settle(inst, 64);
            spkyled::fill(inst, panel, kBlockDt, steps, {}, duty);
            ++blocks;
        } while (inst.flux_engaged(PART_A) && blocks < 750);
        REQUIRE_FALSE(inst.flux_engaged(PART_A));
        CAPTURE(blocks);
        CHECK(duty[ffctl::FTIME_A_L] == 0);
    }

    SUBCASE("FLUX MIX 0: never lit") {
        LampTape tape;
        Instrument inst;
        inst.init(48000.f, tape.mem());
        // The block's switch follows MIX > 0 (control_law), so MIX 0 is the
        // switch off; flux_engaged() is that switch.
        inst.set_fx_on(PART_A, FxBlock::Flux, false);
        inst.set_flux_mix(PART_A, 0.f);
        inst.set_flux_rate(PART_A, 1);
        spkyled::Panel panel;
        for (int b = 0; b < 10 * 750; ++b) {
            settle(inst, 64);
            spkyled::fill(inst, panel, kBlockDt, steps, {}, duty);
            REQUIRE(duty[ffctl::FTIME_A_L] == 0);
            REQUIRE(duty[ffctl::FTIME_B_L] == 0);
        }
    }
}

TEST_CASE("led: the SHIFT lamp shows the input level") {
    const int steps = spkyled::kLedSteps;
    Instrument inst;
    inst.init(48000.f);
    spkyled::Panel panel;
    int duty[ffctl::NUM_LIGHTS] = {};
    spkyled::fill(inst, panel, kBlockDt, steps, {}, duty);     // arms the switch pulse

    float l = 0.f, r = 0.f;
    for (int i = 0; i < 9600; ++i) {                           // 0.2 s of a half-scale sine
        const float x = 0.5f * std::sin(6.2831853f * 440.f * static_cast<float>(i) / 48000.f);
        inst.process(&x, &x, &l, &r, 1);
    }
    spkyled::fill(inst, panel, kBlockDt, steps, {}, duty);
    CHECK(inst.input_peak() > 0.4f);
    CHECK(duty[ffctl::SHIFTBTN_L] == spkyled::duty(inst.input_peak(), steps));
    CHECK(duty[ffctl::SHIFTBTN_L] > 0);
    CHECK(duty[ffctl::SHIFTBTN_L] < steps - 1);                // a level, not the pulse

    settle(inst, 24000);                                       // silence
    spkyled::fill(inst, panel, kBlockDt, steps, {}, duty);
    CHECK(duty[ffctl::SHIFTBTN_L] == 0);
}

TEST_CASE("led: a latched SHIFT lamp is steady at full") {
    const int steps = spkyled::kLedSteps;
    Instrument inst;
    inst.init(48000.f);
    spkyled::Panel panel;
    int duty[ffctl::NUM_LIGHTS] = {};
    spkyled::LedInputs in;
    in.shift_latched = true;
    for (int i = 0; i < 100; ++i) {
        spkyled::fill(inst, panel, kBlockDt, steps, in, duty);
        REQUIRE(duty[ffctl::SHIFTBTN_L] == steps - 1);
    }
}

TEST_CASE("led: the first fill() arms the switch pulse, it does not fire it") {
    // Both epochs tick once at boot, so a law that compared against zero would
    // pulse the SHIFT lamp at power-on.
    const int steps = spkyled::kLedSteps;
    Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst);
    REQUIRE(inst.engine_epoch(PART_A) + inst.engine_epoch(PART_B) + inst.scale_epoch() > 0u);

    spkyled::Panel panel;
    int duty[ffctl::NUM_LIGHTS] = {};
    for (int i = 0; i < 400; ++i) {                            // 0.5 s of the law
        spkyled::fill(inst, panel, 1.f / 750.f, steps, {}, duty);
        REQUIRE(duty[ffctl::SHIFTBTN_L] == 0);
    }
}

namespace {

// After the epoch moved: full for kSwitchPulse, then back to the input level
// (silence here, so dark).
void check_switch_pulse(Instrument& inst, spkyled::Panel& panel, const int steps) {
    const float dt = 1.f / 750.f;
    int duty[ffctl::NUM_LIGHTS] = {};
    spkyled::fill(inst, panel, dt, steps, {}, duty);
    CHECK(duty[ffctl::SHIFTBTN_L] == steps - 1);
    const int n = static_cast<int>(spkyled::kSwitchPulse / dt);
    for (int k = 1; k < n / 2; ++k) spkyled::fill(inst, panel, dt, steps, {}, duty);
    CHECK(duty[ffctl::SHIFTBTN_L] == steps - 1);               // still lit mid-pulse
    for (int k = n / 2; k < n + 3; ++k) spkyled::fill(inst, panel, dt, steps, {}, duty);
    CHECK(duty[ffctl::SHIFTBTN_L] == 0);                       // back to the input level
}

}  // namespace

TEST_CASE("led: an engine switch pulses the SHIFT lamp once it sounds") {
    const int steps = spkyled::kLedSteps;
    Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst);
    settle(inst, 4800);
    spkyled::Panel panel;
    int duty[ffctl::NUM_LIGHTS] = {};
    spkyled::fill(inst, panel, 1.f / 750.f, steps, {}, duty);  // arm
    CHECK(duty[ffctl::SHIFTBTN_L] == 0);

    const EngineId target = inst.engine_id(PART_A) == ENGINE_WAVE ? ENGINE_SYNTH : ENGINE_WAVE;
    inst.set_engine(PART_A, target);
    spkyled::fill(inst, panel, 1.f / 750.f, steps, {}, duty);
    CHECK(duty[ffctl::SHIFTBTN_L] == 0);                       // armed, not yet sounding

    settle(inst, 4800);
    REQUIRE(inst.engine_id(PART_A) == target);
    check_switch_pulse(inst, panel, steps);
}

TEST_CASE("led: a scale change pulses the SHIFT lamp") {
    const int steps = spkyled::kLedSteps;
    Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst);
    settle(inst, 4800);
    spkyled::Panel panel;
    int duty[ffctl::NUM_LIGHTS] = {};
    spkyled::fill(inst, panel, 1.f / 750.f, steps, {}, duty);  // arm
    CHECK(duty[ffctl::SHIFTBTN_L] == 0);

    const uint32_t e0 = inst.scale_epoch();
    inst.set_scale(3);
    if (inst.scale_epoch() == e0) inst.set_scale(4);           // 3 was already the scale
    REQUIRE(inst.scale_epoch() == e0 + 1);
    check_switch_pulse(inst, panel, steps);
}

TEST_CASE("led: CLK and RST flash on their pulses") {
    const int steps = spkyled::kLedSteps;
    Instrument inst;
    inst.init(48000.f);
    const float dt = 1.f / 750.f;
    const int n = static_cast<int>(spkyled::kJackFlash / dt);

    SUBCASE("CLK") {
        spkyled::Panel panel;
        int duty[ffctl::NUM_LIGHTS] = {};
        spkyled::fill(inst, panel, dt, steps, {}, duty);
        CHECK(duty[ffctl::CLK_L] == 0);
        spkyled::LedInputs in;
        in.clock_pulse = true;
        spkyled::fill(inst, panel, dt, steps, in, duty);       // the one call that carries the edge
        CHECK(duty[ffctl::CLK_L] == steps - 1);
        CHECK(duty[ffctl::RST_L] == 0);
        for (int k = 1; k < n / 2; ++k) spkyled::fill(inst, panel, dt, steps, {}, duty);
        CHECK(duty[ffctl::CLK_L] == steps - 1);                // held without the edge
        for (int k = n / 2; k < n + 3; ++k) spkyled::fill(inst, panel, dt, steps, {}, duty);
        CHECK(duty[ffctl::CLK_L] == 0);
    }

    SUBCASE("RST") {
        spkyled::Panel panel;
        int duty[ffctl::NUM_LIGHTS] = {};
        spkyled::fill(inst, panel, dt, steps, {}, duty);
        CHECK(duty[ffctl::RST_L] == 0);
        spkyled::LedInputs in;
        in.reset_pulse = true;
        spkyled::fill(inst, panel, dt, steps, in, duty);
        CHECK(duty[ffctl::RST_L] == steps - 1);
        CHECK(duty[ffctl::CLK_L] == 0);
        for (int k = 1; k < n / 2; ++k) spkyled::fill(inst, panel, dt, steps, {}, duty);
        CHECK(duty[ffctl::RST_L] == steps - 1);
        for (int k = n / 2; k < n + 3; ++k) spkyled::fill(inst, panel, dt, steps, {}, duty);
        CHECK(duty[ffctl::RST_L] == 0);
    }
}
