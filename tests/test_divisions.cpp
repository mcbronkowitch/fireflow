#include <doctest/doctest.h>
#include <cmath>
#include <string>
#include "mod/divisions.h"
using namespace spky;

TEST_CASE("divisions: 20 entries, strictly speed-sorted") {
    CHECK(kDivisionCount == 20);
    for (int i = 1; i < kDivisionCount; ++i)
        CHECK(kDivisions[i].cpb > kDivisions[i - 1].cpb);
}

TEST_CASE("divisions: the lane RATE window puts 16th steps at noon") {
    // A name is one lane cycle; a STEP deck plays 8 steps per cycle, so the
    // "1/2" cycle at noon is a 16th-note step (Bastian, 2026-11-04).
    CHECK(division_index(0.f) == 0);
    CHECK(std::string(kDivisions[division_index(0.f)].name) == "64 bars");
    CHECK(std::string(kDivisions[division_index(0.5f)].name) == "1/2");
    CHECK(kDivisions[division_index(0.5f)].cpb * 8.f == 4.f);   // 4 steps per beat
    CHECK(division_index(1.f) == kLaneRateCount - 1);
    CHECK(std::string(kDivisions[division_index(1.f)].name) == "1/8T");
    // The factory patch's rungs, three up from where the preset stored them.
    CHECK(std::string(kDivisions[division_index(0.299500011f)].name) == "2 bars");
    CHECK(std::string(kDivisions[division_index(0.351355359f)].name) == "1 bar");
}

TEST_CASE("divisions: FLUX's slice is the ladder's last twelve") {
    CHECK(std::string(kDivisions[kFluxRateOffset].name) == "1/2");
    CHECK(std::string(kDivisions[kFluxRateOffset + kFluxRateCount - 1].name) == "1/32");
}

TEST_CASE("divisions: hz math at 120 bpm") {
    // 1/4 note = 1 cycle per beat = 2 Hz at 120 bpm
    CHECK(division_hz(11, 120.f) == doctest::Approx(2.f));
    // 8 bars = 32 beats -> 0.0625 Hz
    CHECK(division_hz(3, 120.f) == doctest::Approx(0.0625f));
    // 64 bars = 256 beats -> 1/128 Hz
    CHECK(division_hz(0, 120.f) == doctest::Approx(1.f / 128.f));
    // 1/8T = 3 cycles per beat -> 6 Hz
    CHECK(division_hz(16, 120.f) == doctest::Approx(6.f));
}

TEST_CASE("divisions: nearest_division snaps in log space") {
    CHECK(nearest_division(2.1f, 120.f) == 11);   // just above 1/4 -> 1/4
    CHECK(nearest_division(2.9f, 120.f) == 13);   // 1/4T is 3 Hz at 120
    // Clamps to "8 bars", not to the RATE window's slower entries: the free
    // world's grid gravity kept the ladder it had.
    CHECK(nearest_division(0.001f, 120.f) == kGridFirst);
    CHECK(std::string(kDivisions[kGridFirst].name) == "8 bars");
    CHECK(nearest_division(100.f, 120.f) == kDivisionCount - 1);  // clamps to the fast end
}

TEST_CASE("divisions: free_hz spans 0.02..30 exponentially") {
    CHECK(free_hz(0.f) == doctest::Approx(0.02f));
    CHECK(free_hz(1.f) == doctest::Approx(30.f));
    CHECK(free_hz(0.5f) == doctest::Approx(std::sqrt(0.02f * 30.f)).epsilon(0.001));
}

TEST_CASE("pace: the curve meets exactly 1 at centre") {
    // The whole safety argument for a global time control is that it has a
    // position where it provably does not exist. Exact, not Approx: the
    // no-op claim is a bit-identity claim (spec 2026-08-12 PACE §2).
    CHECK(spky::pace_mult(0.5f) == 1.0f);
    CHECK(spky::pace_mult(-1.f) == spky::pace_mult(0.f));
    CHECK(spky::pace_mult(2.f)  == spky::pace_mult(1.f));
}

TEST_CASE("pace: a power-of-two ladder on the old curve's rung positions") {
    // Exact values at the positions the continuous curve passed through them,
    // so a saved knob on a rung keeps its value -- the factory patch's 0.1
    // (stored as 0.099999927f) among them.
    struct R { float knob; float mult; };
    for (R r : {R{0.f, 1.f / 32.f}, R{0.1f, 1.f / 16.f}, R{0.099999927f, 1.f / 16.f},
                R{0.2f, 1.f / 8.f}, R{0.3f, 1.f / 4.f}, R{0.4f, 1.f / 2.f},
                R{0.5f, 1.f}, R{0.75f, 2.f}, R{1.f, 4.f}}) {
        CAPTURE(r.knob);
        CHECK(spky::pace_mult(r.knob) == r.mult);
    }
    // Between rungs: the nearer one in log2. x1 spans 0.45 .. 0.625.
    CHECK(spky::pace_mult(0.46f) == 1.f);
    CHECK(spky::pace_mult(0.48f) == 1.f);
    CHECK(spky::pace_mult(0.62f) == 1.f);
    CHECK(spky::pace_mult(0.44f) == 0.5f);
    CHECK(spky::pace_mult(0.63f) == 2.f);
    // Every value is a rung, and the ladder never falls.
    float prev = spky::pace_mult(0.f);
    for (int i = 0; i <= 1000; ++i) {
        const float m = spky::pace_mult(float(i) * 0.001f);
        bool on_rung = false;
        for (float rung : spky::kPaceRungs) on_rung = on_rung || m == rung;
        CAPTURE(i);
        CHECK(on_rung);
        CHECK(m >= prev);
        prev = m;
    }
}
