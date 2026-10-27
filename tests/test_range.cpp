#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include "mod/range.h"
using namespace spky;

TEST_CASE("range: minimum is off") {
    CHECK(apply_range( 1.f, 0.f) == doctest::Approx(0.f));
    CHECK(apply_range(-1.f, 0.f) == doctest::Approx(0.f));
    CHECK(apply_range( 0.f, 0.f) == doctest::Approx(0.f));
}

TEST_CASE("range: full unipolar at mid travel") {
    CHECK(apply_range( 1.f, 0.5f) == doctest::Approx(1.f));
    CHECK(apply_range(-1.f, 0.5f) == doctest::Approx(0.f));
    CHECK(apply_range( 0.f, 0.5f) == doctest::Approx(0.5f));
}

TEST_CASE("range: full bipolar at maximum") {
    CHECK(apply_range( 1.f, 1.f) == doctest::Approx( 1.f));
    CHECK(apply_range(-1.f, 1.f) == doctest::Approx(-1.f));
    CHECK(apply_range( 0.f, 1.f) == doctest::Approx( 0.f));
}

TEST_CASE("range: monotonic in v for every fixed r") {
    for (int ri = 0; ri <= 10; ++ri) {
        float r = ri / 10.f;
        float prev = apply_range(-1.f, r);
        for (int vi = -9; vi <= 10; ++vi) {
            float cur = apply_range(vi / 10.f, r);
            CHECK(cur >= prev - 1e-5f);
            prev = cur;
        }
    }
}

// The note-lane law (spec 2026-10-26 pitch-range-law, 4.1). One multiply, so
// the cases compare for exact equality.
TEST_CASE("range: centred law is off at zero") {
    CHECK(apply_range_centred( 1.f, 0.f) == 0.f);
    CHECK(apply_range_centred(-1.f, 0.f) == 0.f);
    CHECK(apply_range_centred( 1.f, -0.2f) == 0.f);   // r clamps to 0
}

TEST_CASE("range: centred law is a pure width") {
    for (float v : {-1.f, -0.5f, 0.3f, 1.f}) {
        for (float r : {0.1f, 0.25f, 0.5f, 0.75f}) {
            CAPTURE(v);
            CAPTURE(r);
            CHECK(apply_range_centred(v, r) == v * r);
        }
    }
    // The centre stays the centre. apply_range gives 0.5 here: its unipolar
    // half is what leans a melody upward.
    CHECK(apply_range_centred(0.f, 0.5f) == 0.f);
}

TEST_CASE("range: both laws agree at full range") {
    // apply_range(v, 1) is lerpf(uni, v, 1) = uni + (v - uni), which is v bit
    // for bit on a 2^-23 grid (spec 2026-10-26 pitch-range-law P4: all
    // 16 777 217 values of it) and one rounding step off for some values
    // OFF that grid -- 415 of 2001 evenly spaced values, at most 2^-24
    // (measured 2026-10-26). A real lane value is not on the grid, so "a deck
    // at RANGE 1 is untouched by the new law" holds to one ulp, not to the
    // bit; the bit-for-bit half is pinned on the grid, the other half as a
    // bound that a different law (differences of order 0.1 and up) cannot meet.
    int differing = 0;
    for (long k = 0; k <= (1L << 24); ++k) {
        const float v = -1.f + static_cast<float>(k) * (1.f / 8388608.f);
        const float a = apply_range_centred(v, 1.f);
        const float b = apply_range(v, 1.f);
        if (std::memcmp(&a, &b, sizeof a) != 0) ++differing;
    }
    CHECK(differing == 0);
    float worst = 0.f;
    for (int i = 0; i <= 2000; ++i) {
        const float v = -1.f + 2.f * static_cast<float>(i) / 2000.f;
        worst = std::fmax(worst, std::fabs(apply_range_centred(v, 1.f) -
                                           apply_range(v, 1.f)));
    }
    CHECK(worst <= 1.2e-7f);
}
