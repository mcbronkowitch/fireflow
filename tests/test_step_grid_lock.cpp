#include <doctest/doctest.h>
#include <vector>
#include "mod/super_modulator.h"
#include "mod/lane_len.h"
#include "mod/divisions.h"
#include "mod/shuffle_grid.h"
using namespace spky;

TEST_CASE("steplock: STEP gives each lane its own slot count") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);

    CHECK(m.lane_slots_for_test(LANE_SOURCE) ==  4);
    CHECK(m.lane_slots_for_test(LANE_LEVEL)  ==  6);
    CHECK(m.lane_slots_for_test(LANE_PITCH)  ==  8);
    CHECK(m.lane_slots_for_test(LANE_MOTION) == 12);
    CHECK(m.lane_slots_for_test(LANE_SIZE)   == 16);
}

TEST_CASE("steplock: TIDE moves slot counts in STEP, not rates") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);
    const float r = m.lane_rate_hz_for_test(LANE_PITCH);

    m.set_tide(0.25f);                       // ladder rung x1/2
    REQUIRE(kTideRatios[tide_index(0.25f)] == doctest::Approx(0.5f));
    CHECK(m.lane_rate_hz_for_test(LANE_PITCH) == doctest::Approx(r));
    CHECK(m.lane_slots_for_test(LANE_SOURCE) ==  8);
    CHECK(m.lane_slots_for_test(LANE_SIZE)   == 32);
    CHECK(m.lane_slots_for_test(LANE_MOTION) == 24);
    CHECK(m.lane_slots_for_test(LANE_LEVEL)  == 12);
    CHECK(m.lane_slots_for_test(LANE_PITCH)  ==  8);   // the phrase, always
}

TEST_CASE("steplock: the deck step count follows the pitch lane") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);

    int last = m.pitch_cur_step();
    int changes = 0;
    for (int i = 0; i < 200000; ++i) {
        m.process();
        if (m.pitch_cur_step() != last) { last = m.pitch_cur_step(); ++changes; }
    }
    REQUIRE(changes > 8);
    // The count starts at 0 on the first step, so it trails the change count
    // by exactly one.
    CHECK(m.deck_step_for_test() == changes - 1);
}

TEST_CASE("steplock: FLOW keeps the old ratios, TIDE and mod_scale") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(false, 8);
    m.set_rate_scale(1.f, 2.f);

    const float pitch = m.lane_rate_hz_for_test(LANE_PITCH);
    CHECK(m.lane_rate_hz_for_test(LANE_SOURCE)
          == doctest::Approx(pitch * 2.f * 2.f));      // mod_scale x ratio
    CHECK(m.lane_rate_hz_for_test(LANE_MOTION)
          == doctest::Approx(pitch * 2.f * 0.75f));
    for (int i = 0; i < LANE_COUNT; ++i)
        CHECK(m.lane_slots_for_test(i) == 8);          // no per-lane slots
}

TEST_CASE("steplock: FLOW lane ratios are unchanged on the phase") {
    SuperModulator m;
    m.init(48000.f, 42u);
    m.set_rate(0.3f);
    m.set_step(false, 8);
    for (int i = 0; i < ModLane::kTickInterval; ++i) m.process();
    const float pitch = m.lane_phase(LANE_PITCH);
    CHECK(m.lane_phase(LANE_SOURCE) == doctest::Approx(pitch * 2.00f));
    CHECK(m.lane_phase(LANE_SIZE)   == doctest::Approx(pitch * 0.50f));
    CHECK(m.lane_phase(LANE_MOTION) == doctest::Approx(pitch * 0.75f));
    CHECK(m.lane_phase(LANE_LEVEL)  == doctest::Approx(pitch * 1.50f));
}

TEST_CASE("steplock: a live SHUFFLE turn does not clamp the follower phase") {
    // Bug (review of 4c90027): SuperModulator::process() derives the deck's
    // follow fraction from a mirrored SHUFFLE target that moves the instant
    // set_shuffle() is called, but the PITCH lane's own _shuffle_latched --
    // the amount that actually produced its _phase -- only updates on an
    // even-indexed step entry. A live SHUFFLE turn landing on an odd PITCH
    // step therefore computes the fraction against boundaries that never
    // produced that phase, and shuffle_step_fraction's clamp to [0,1] pins
    // every texture lane's phase to a slot edge until the next even entry.
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.2f);          // slow: a STEP takes tens of thousands of
                                // samples, far more than one 96-sample tick,
                                // so PITCH cannot cross a boundary mid-test
    m.set_shuffle(0.2f);
    m.set_step(true, 8);

    // lane_fired() latches until the next follow() call (once per raster
    // tick), so it has to be sampled right at the tick, not on every sample
    // -- otherwise the same fire gets counted on all 96 samples it persists.
    int call_idx = 0;
    int fires_source = 0;
    auto step_once = [&]() {
        m.process();
        const bool is_tick = (call_idx % ModLane::kTickInterval) == 0;
        ++call_idx;
        if (is_tick && m.lane_fired(LANE_SOURCE)) ++fires_source;
    };

    // Advance until PITCH sits well inside an ODD step (clear of the entry
    // boundary, so the margin below cannot itself cross into the next step).
    while (m.pitch_cur_step() < 0 || m.pitch_cur_step() % 2 == 0) step_once();
    for (int i = 0; i < 50; ++i) step_once();
    REQUIRE(m.pitch_cur_step() % 2 == 1);
    const int odd_step = m.pitch_cur_step();

    m.set_shuffle(0.9f);       // the live SHUFFLE turn, mid odd PITCH step

    // One more raster tick: any 96 consecutive process() calls contain
    // exactly one texture-lane follow() update.
    for (int i = 0; i < ModLane::kTickInterval; ++i) step_once();

    REQUIRE(m.pitch_cur_step() == odd_step);   // no boundary crossed meanwhile

    // The amount PITCH's phase (and so the deck's fraction) was actually
    // built from -- the one every follower's position must agree with.
    const float pitch_amt = m.pitch_shuffle_latched_for_test();
    for (int i = 0; i < LANE_COUNT; ++i) {
        if (i == LANE_PITCH) continue;
        const int slots = m.lane_slots_for_test(i);
        const float phase = m.lane_phase(i);
        const int step = shuffle_step_index(phase, slots, pitch_amt);
        const float frac = shuffle_step_fraction(phase, step, slots, pitch_amt);
        CHECK(frac > 0.f);
        CHECK(frac < 1.f);
    }

    // The fix must not disturb the deck-step/fire-count bookkeeping: one
    // fire per distinct deck step, including the cold-start landing.
    CHECK(fires_source == m.deck_step_for_test() + 1);
}
