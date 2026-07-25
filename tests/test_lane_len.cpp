#include <doctest/doctest.h>
#include "mod/lane_len.h"
#include "mod/divisions.h"
using namespace spky;

TEST_CASE("lane_len: the default phrase yields the 4/6/8/12/16 set") {
    CHECK(lane_slots(LANE_SOURCE, 8, 1.f) ==  4);
    CHECK(lane_slots(LANE_LEVEL,  8, 1.f) ==  6);
    CHECK(lane_slots(LANE_PITCH,  8, 1.f) ==  8);
    CHECK(lane_slots(LANE_MOTION, 8, 1.f) == 12);
    CHECK(lane_slots(LANE_SIZE,   8, 1.f) == 16);
}

TEST_CASE("lane_len: TIDE stretches the texture lanes and never PITCH") {
    CHECK(lane_slots(LANE_SOURCE, 8, 0.5f) ==  8);
    CHECK(lane_slots(LANE_SIZE,   8, 0.5f) == 32);
    CHECK(lane_slots(LANE_MOTION, 8, 0.5f) == 24);
    CHECK(lane_slots(LANE_LEVEL,  8, 0.5f) == 12);
    CHECK(lane_slots(LANE_PITCH,  8, 0.5f) ==  8);
    CHECK(lane_slots(LANE_PITCH,  8, 4.f)  ==  8);
}

TEST_CASE("lane_len: clamps at both ends") {
    // 1 slot would pin the lane to phase 0 and emit a constant value.
    CHECK(lane_slots(LANE_SOURCE,  2, 1.f)   ==  2);
    CHECK(lane_slots(LANE_SIZE,   16, 0.25f) == 64);   // wants 128
}

TEST_CASE("lane_len: odd phrase lengths round half away from zero") {
    CHECK(lane_slots(LANE_SOURCE, 5, 1.f) ==  3);   // 2.50
    CHECK(lane_slots(LANE_LEVEL,  5, 1.f) ==  4);   // 3.75
    CHECK(lane_slots(LANE_MOTION, 5, 1.f) ==  8);   // 7.50
    CHECK(lane_slots(LANE_SIZE,   5, 1.f) == 10);
}

TEST_CASE("lane_len: every panel-reachable combination stays inside bounds") {
    for (int s = 2; s <= 16; ++s)
        for (int t = 0; t < kTideCount; ++t)
            for (int l = 0; l < LANE_COUNT; ++l) {
                const int n = lane_slots(l, s, kTideRatios[t]);
                CHECK(n >= kLaneSlotsMin);
                CHECK(n <= kLaneSlotsMax);
            }
}
