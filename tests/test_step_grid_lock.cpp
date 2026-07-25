#include <doctest/doctest.h>
#include <vector>
#include "mod/super_modulator.h"
#include "mod/lane_len.h"
#include "mod/divisions.h"
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
