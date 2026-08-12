#include <doctest/doctest.h>
#include <cmath>
#include <string>
#include "mod/divisions.h"
using namespace spky;

TEST_CASE("divisions: 17 entries, strictly speed-sorted") {
    CHECK(kDivisionCount == 17);
    for (int i = 1; i < kDivisionCount; ++i)
        CHECK(kDivisions[i].cpb > kDivisions[i - 1].cpb);
}

TEST_CASE("divisions: knob endpoints and center land on the right rungs") {
    CHECK(division_index(0.f) == 0);                       // 8 bars
    CHECK(division_index(1.f) == kDivisionCount - 1);      // 1/32
    CHECK(std::string(kDivisions[division_index(0.5f)].name) == "1/4");
}

TEST_CASE("divisions: hz math at 120 bpm") {
    // 1/4 note = 1 cycle per beat = 2 Hz at 120 bpm
    CHECK(division_hz(8, 120.f) == doctest::Approx(2.f));
    // 8 bars = 32 beats -> 0.0625 Hz
    CHECK(division_hz(0, 120.f) == doctest::Approx(0.0625f));
    // 1/8T = 3 cycles per beat -> 6 Hz
    CHECK(division_hz(13, 120.f) == doctest::Approx(6.f));
}

TEST_CASE("divisions: nearest_division snaps in log space") {
    CHECK(nearest_division(2.1f, 120.f) == 8);    // just above 1/4 -> 1/4
    CHECK(nearest_division(2.9f, 120.f) == 10);   // 1/4T is 3 Hz at 120
    CHECK(nearest_division(0.001f, 120.f) == 0);  // clamps to the slow end
    CHECK(nearest_division(100.f, 120.f) == 16);  // clamps to the fast end
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
    CHECK(spky::pace_mult(0.f)  == doctest::Approx(1.f / 32.f));
    CHECK(spky::pace_mult(1.f)  == doctest::Approx(4.f));
    CHECK(spky::pace_mult(0.75f) == doctest::Approx(2.f));
    // Monotone rising across the knee, and clamped outside 0..1.
    float prev = spky::pace_mult(0.f);
    for (int i = 1; i <= 100; ++i) {
        const float m = spky::pace_mult(float(i) * 0.01f);
        CHECK(m > prev);
        prev = m;
    }
    CHECK(spky::pace_mult(-1.f) == spky::pace_mult(0.f));
    CHECK(spky::pace_mult(2.f)  == spky::pace_mult(1.f));
}
