#include <doctest/doctest.h>
#include "fx/drag.h"

using namespace spky;

namespace {
RhythmView view(int32_t g0, int32_t g1) {
    RhythmView rv;
    rv.gap[0] = g0;
    rv.gap[1] = g1;
    rv.valid  = true;
    return rv;
}
}  // namespace

TEST_CASE("derive_intervals: the two gaps come through as durations") {
    int32_t out[2];
    derive_intervals(view(6000, 9000), out);
    CHECK(out[0] == 6000);
    CHECK(out[1] == 9000);      // NOT 15000 -- an interval, not a position
}

TEST_CASE("derive_intervals: an invalid view yields no intervals") {
    RhythmView rv = view(6000, 9000);
    rv.valid = false;
    int32_t out[2];
    derive_intervals(rv, out);
    CHECK(out[0] == drag_tuning::kNone);
    CHECK(out[1] == drag_tuning::kNone);
}

TEST_CASE("derive_intervals: uniform gaps are spread into a limp") {
    // Evenly spaced repeats ARE a plain delay, and RATE already delivers one
    // in sync. The limp is the thing that cannot be had another way.
    int32_t out[2];
    derive_intervals(view(6000, 6000), out);
    CHECK(out[0] == 6000);
    CHECK(out[1] == 4500);      // 0.75 * g0, the MOTION lane's polyrhythm
}

TEST_CASE("derive_intervals: gaps within the tolerance still count as uniform") {
    int32_t out[2];
    derive_intervals(view(6000, 6060), out);    // 1 % apart, inside kUniformTol
    CHECK(out[0] == 6000);      // the guard rewrites only the second gap
    CHECK(out[1] == 4500);
}

TEST_CASE("derive_intervals: gaps outside the tolerance are left alone") {
    int32_t out[2];
    derive_intervals(view(6000, 6600), out);    // 10 % apart
    CHECK(out[0] == 6000);
    CHECK(out[1] == 6600);
}

TEST_CASE("derive_intervals: a buzz-length gap yields nothing") {
    int32_t out[2];
    derive_intervals(view(16, 9000), out);      // below kMinGap
    CHECK(out[0] == drag_tuning::kNone);
    CHECK(out[1] == drag_tuning::kNone);
}

TEST_CASE("derive_intervals: a spread that would fall below kMinGap yields nothing") {
    int32_t out[2];
    derive_intervals(view(40, 40), out);        // uniform; 0.75*40 = 30 < 32
    CHECK(out[0] == drag_tuning::kNone);
    CHECK(out[1] == drag_tuning::kNone);
}
