// tests/test_glow_ui.cpp
#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include "vcv/src/glow_ui.hpp"
#include "flow/taste.h"
#include "flow/terrain_code.h"

using namespace spky;
using namespace spky::flow;
using namespace spkyvcv;

TEST_CASE("glow: the house code is a decodable terrain code") {
    TerrainState st;
    CHECK(decode_code(kHouseCode, st));
}

TEST_CASE("glow: CV jacks map to the five CV-carrying macros, not WANDER") {
    CHECK(kCvMacro[0] == M_MOTION);
    CHECK(kCvMacro[1] == M_DENSITY);
    CHECK(kCvMacro[2] == M_BRIGHT);
    CHECK(kCvMacro[3] == M_DIRT);
    CHECK(kCvMacro[4] == M_SPACE);
    for (int i = 0; i < 5; ++i) CHECK(kCvMacro[i] != M_WANDER);
}

TEST_CASE("glow: unipolar 0..10 V spans the macro range") {
    CHECK(cv_to_macro(0.f)  == doctest::Approx(0.f));
    CHECK(cv_to_macro(5.f)  == doctest::Approx(0.5f));
    CHECK(cv_to_macro(10.f) == doctest::Approx(1.f));
    // Out-of-range voltages are NOT clamped here -- Flow::set_cv clamps the
    // sum, and clamping twice would silently hide a hot patch cable.
    CHECK(cv_to_macro(-5.f) == doctest::Approx(-0.5f));
    CHECK(cv_to_macro(15.f) == doctest::Approx(1.5f));
}

TEST_CASE("glow: the knob tracker reports travel, once, per macro") {
    float v[MACRO_COUNT] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float d[MACRO_COUNT];
    KnobTracker kt;
    kt.prime(v);
    CHECK_FALSE(kt.deltas(v, d));            // nothing moved
    for (int m = 0; m < MACRO_COUNT; ++m) CHECK(d[m] == doctest::Approx(0.f));

    v[M_BRIGHT] = 0.62f;
    CHECK(kt.deltas(v, d));
    CHECK(d[M_BRIGHT] == doctest::Approx(0.12f));
    CHECK(d[M_MOTION] == doctest::Approx(0.f));

    // The same position on the next tick is no longer travel.
    CHECK_FALSE(kt.deltas(v, d));
    CHECK(d[M_BRIGHT] == doctest::Approx(0.f));

    // Direction is irrelevant: the decoder marks on absolute travel.
    v[M_BRIGHT] = 0.5f;
    CHECK(kt.deltas(v, d));
    CHECK(d[M_BRIGHT] == doctest::Approx(0.12f));
}

TEST_CASE("glow: an unprimed tracker reports no travel on its first look") {
    // A module that has just been added must not spray six phantom deltas
    // into the gesture decoder on its first control tick.
    float v[MACRO_COUNT] = {0.f, 0.2f, 0.4f, 0.6f, 0.8f, 1.f};
    float d[MACRO_COUNT];
    KnobTracker kt;
    CHECK_FALSE(kt.deltas(v, d));
    for (int m = 0; m < MACRO_COUNT; ++m) CHECK(d[m] == doctest::Approx(0.f));
}

TEST_CASE("glow: every LED signature stays in range and is distinguishable") {
    const int leds[] = { Gesture::LED_IDLE, Gesture::LED_BLEND,
                         Gesture::LED_MARKED, Gesture::LED_UNDO_ARMED,
                         Gesture::LED_LOCKED, Gesture::LED_REFUSE };
    for (int led : leds) {
        float lo = 2.f, hi = -1.f;
        for (int k = 0; k < 400; ++k) {
            const double t = 0.005 * k;          // two seconds at 200 Hz
            const float b = led_level(led, 0.5f, t);
            CHECK(b >= 0.f);
            CHECK(b <= 1.f);
            lo = std::fmin(lo, b);
            hi = std::fmax(hi, b);
        }
        // Locked is solid; idle is a steady dim glow. Everything else moves.
        if (led == Gesture::LED_LOCKED) {
            CHECK(lo == doctest::Approx(1.f));
        } else if (led == Gesture::LED_IDLE) {
            CHECK(hi == lo);
            CHECK(hi < 0.2f);
        } else {
            CHECK(hi - lo > 0.4f);
        }
    }
}

TEST_CASE("glow: MARKED and REFUSE are not the same light") {
    // Both flicker; a player must still be able to tell "I marked a macro"
    // from "the module refused me".
    int differ = 0;
    for (int k = 0; k < 200; ++k) {
        const double t = 0.005 * k;
        if (std::fabs(led_level(Gesture::LED_MARKED, 1.f, t) -
                      led_level(Gesture::LED_REFUSE, 1.f, t)) > 0.25f)
            ++differ;
    }
    CHECK(differ > 40);
}

TEST_CASE("glow: a saved payload restores the terrain, the lock and the undo slot") {
    Instrument inst;
    inst.init(48000.f);
    Flow fl;
    fl.init(&inst, 100.f);

    TerrainState house;
    REQUIRE(decode_code(kHouseCode, house));
    fl.wake(house);
    REQUIRE(fl.new_full());                  // fills the undo slot
    fl.set_lock(true);

    const GlowSave s = glow_capture(fl);
    CHECK(s.lock);
    CHECK(s.have_undo);
    CHECK(std::strlen(s.code) == size_t(kTerrainCodeLen));

    Instrument inst2;
    inst2.init(48000.f);
    Flow fl2;
    fl2.init(&inst2, 100.f);
    CHECK(glow_restore(fl2, s));
    CHECK(fl2.state().master == fl.state().master);
    for (int m = 0; m < MACRO_COUNT; ++m)
        CHECK(fl2.state().reroll[m] == fl.state().reroll[m]);
    CHECK(fl2.locked() == fl.locked());
    CHECK(fl2.can_undo());
    CHECK(fl2.undo_state().master == fl.undo_state().master);
    // A restore is bookkeeping, not a gesture: no blend may be in flight.
    CHECK(fl2.blend_phase() == doctest::Approx(1.f));
}

TEST_CASE("glow: a malformed saved code changes nothing") {
    Instrument inst;
    inst.init(48000.f);
    Flow fl;
    fl.init(&inst, 100.f);
    TerrainState house;
    REQUIRE(decode_code(kHouseCode, house));
    fl.wake(house);

    GlowSave bad;
    std::strcpy(bad.code, "F1-NOTHEX00-000000000000");
    bad.lock = true;
    CHECK_FALSE(glow_restore(fl, bad));
    CHECK(fl.state().master == house.master);
    CHECK_FALSE(fl.locked());
}
