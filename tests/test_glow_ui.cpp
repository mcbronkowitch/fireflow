// tests/test_glow_ui.cpp
#include <doctest/doctest.h>
#include <cmath>
#include <cstdio>
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
    std::snprintf(bad.code, sizeof bad.code, "%s", "F1-NOTHEX00-000000000000");
    bad.lock = true;
    CHECK_FALSE(glow_restore(fl, bad));
    CHECK(fl.state().master == house.master);
    CHECK_FALSE(fl.locked());
}

TEST_CASE("glow: the button bridge reports each edge exactly once") {
    GestureBridge b;
    CHECK_FALSE(b.edge(false));         // still up
    CHECK(b.edge(true));                // press
    CHECK_FALSE(b.edge(true));          // held: NOT an edge
    CHECK_FALSE(b.edge(true));
    CHECK(b.edge(false));               // release
    CHECK_FALSE(b.edge(false));
}

TEST_CASE("glow: a refuse flash is active only within its window after mark") {
    // The module's own refusal signal: Flow can decline an op the decoder
    // let through (nothing to undo, an empty macro mask), and gesture.h's
    // own _refuse_t cannot be reached from outside a real release -- see
    // the fix-round-1 note above RefuseFlash's definition. This is that
    // signal's headless coverage.
    RefuseFlash rf;
    const double t = 12.5;                 // plausible mid-session timestamp
    CHECK_FALSE(rf.active(t));             // fresh: not "just refused"
    CHECK_FALSE(rf.active(0.0));

    rf.mark(t);
    CHECK(rf.active(t));
    CHECK(rf.active(t + spky::flow::kRefuseFlashS - 1e-6));
    CHECK_FALSE(rf.active(t + spky::flow::kRefuseFlashS));
    CHECK_FALSE(rf.active(t + spky::flow::kRefuseFlashS + 1.0));
}

TEST_CASE("glow: clock_bpm falls back to the terrain's own tempo with no clock connected") {
    // period 0 == "no edge has ever arrived" (Glow.cpp's clkPeriod default).
    CHECK(clock_bpm(120.f, /*clkPeriod=*/0.f, /*clkSamples=*/0.f,
                     /*sr=*/48000.f, /*timeoutS=*/2.f) == doctest::Approx(120.f));
}

TEST_CASE("glow: clock_bpm reports the measured tempo for a valid clock") {
    // 120 BPM at 48 kHz -> 24000 samples/beat.
    const float sr = 48000.f, period = 24000.f;
    const float bpm = clock_bpm(90.f, period, /*clkSamples=*/100.f, sr, 2.f);
    CHECK(bpm == doctest::Approx(120.f));
}

TEST_CASE("glow: clock_bpm falls back once the clock has timed out") {
    const float sr = 48000.f, period = 24000.f;      // a real, valid period...
    // ...but clkSamples has already passed sr * timeoutS since the last edge.
    const float bpm = clock_bpm(90.f, period, /*clkSamples=*/sr * 2.f + 1.f, sr, 2.f);
    CHECK(bpm == doctest::Approx(90.f));
}

TEST_CASE("glow: clock_bpm falls back for a measurement outside 20..400 BPM") {
    const float sr = 48000.f;
    // Too slow: 10 BPM -> period = 60*sr/10 = 288000 samples.
    CHECK(clock_bpm(90.f, 288000.f, 100.f, sr, 2.f) == doctest::Approx(90.f));
    // Too fast: 500 BPM -> period = 60*sr/500 = 5760 samples.
    CHECK(clock_bpm(90.f, 5760.f, 100.f, sr, 2.f) == doctest::Approx(90.f));
    // Just inside the window at both ends stays measured.
    CHECK(clock_bpm(90.f, 60.f * sr / 20.f, 100.f, sr, 2.f) == doctest::Approx(20.f));
    CHECK(clock_bpm(90.f, 60.f * sr / 400.f, 100.f, sr, 2.f) == doctest::Approx(400.f));
}

TEST_CASE("glow: a held button reaches the lock threshold through the bridge") {
    // The regression this guards: feeding button(true) every tick instead of
    // only on the edge restarts the hold timer forever, so the 5 s lock
    // gesture can never fire and the module has no way to be locked.
    Gesture g;
    GestureBridge b;
    bool locked = false;
    double t = 0.0;
    const double dt = 1.0 / 100.0;
    for (int i = 0; i < 800; ++i, t += dt) {   // eight seconds held down
        if (b.edge(true)) g.button(true, t, locked);
        g.tick(t, /*can_undo=*/false);
        const GestureOut op = g.poll();
        if (op.op == GestureOut::LOCK_TOGGLE) locked = !locked;
    }
    CHECK(locked);
}
