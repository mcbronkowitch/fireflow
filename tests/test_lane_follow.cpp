#include <doctest/doctest.h>
#include <vector>
#include "mod/lane.h"
using namespace spky;

// Follower mode (spec 2026-07-25 mod-lane-step-grid-lock). A lane in STEP
// holds no clock: it is told the deck's cumulative step count and where the
// deck sits inside its current step, and derives everything from that. These
// tests drive follow() with hand-written integer counts, so they pin the
// contract without a SuperModulator and without any float clock at all.
namespace {
constexpr float kSr = 48000.f;

void configure(ModLane& l, int slots) {
    l.set_melodic(false);
    l.init(kSr, 4242u);
    l.set_step(true, slots);
    l.set_rate_hz(2.f);        // assigned but unused by a follower
    l.set_shape(1.f);
    l.set_smooth(0.f);
}
} // namespace

TEST_CASE("follow: one deck step is one slot, whatever the cycle length") {
    for (int slots : {2, 3, 4, 6, 8, 12, 16, 32}) {
        ModLane l;
        configure(l, slots);
        int fires = 0;
        for (int32_t s = 0; s < 200; ++s) {
            l.follow(s, 0.f);
            if (l.fired()) ++fires;
        }
        CHECK(fires == 200);          // exactly one boundary per deck step
    }
}

TEST_CASE("follow: the slot index is the deck count modulo the cycle") {
    ModLane l;
    configure(l, 6);
    for (int32_t s = 0; s < 40; ++s) {
        l.follow(s, 0.f);
        CHECK(l.cur_step() == static_cast<int>(s % 6));
    }
}

TEST_CASE("follow: repeat calls inside one deck step do not re-fire") {
    ModLane l;
    configure(l, 8);
    l.follow(0, 0.f);
    REQUIRE(l.fired());
    for (float frac : {0.25f, 0.5f, 0.75f, 0.99f}) {
        l.follow(0, frac);
        CHECK_FALSE(l.fired());
        CHECK(l.cur_step() == 0);
    }
    l.follow(1, 0.f);
    CHECK(l.fired());
}

TEST_CASE("follow: a multi-step advance replays every slot in order") {
    // The raster window normally holds at most one deck step, but COUPLE and
    // DRIFT can push pitch_scale up. A skipped slot would drop a wrap event.
    ModLane l;
    configure(l, 4);
    l.follow(0, 0.f);
    std::vector<int> seen;
    for (int32_t s = 3; s <= 15; s += 3) {          // three slots per call
        l.follow(s, 0.f);
        seen.push_back(l.cur_step());
    }
    // Landing slots after 3, 6, 9, 12, 15 deck steps in a 4-slot cycle.
    CHECK(seen == std::vector<int>{3, 2, 1, 0, 3});
}

TEST_CASE("follow: wrapped() marks the cycle seam, once per cycle") {
    ModLane l;
    configure(l, 4);
    for (int32_t s = 0; s < 13; ++s) {
        l.follow(s, 0.f);
        CHECK(l.wrapped() == (s % 4 == 0));
    }
}

TEST_CASE("follow: a slot nudge offsets the lane and fires immediately") {
    ModLane l;
    configure(l, 8);
    for (int32_t s = 0; s <= 4; ++s) l.follow(s, 0.f);
    REQUIRE(l.cur_step() == 4);

    l.nudge_slots(3, 0.f);
    l.follow(4, 0.5f);                 // same deck step, mid-step
    CHECK(l.fired());                  // the stumble is audible at once
    CHECK(l.cur_step() == 7);

    l.follow(5, 0.f);                  // the offset persists
    CHECK(l.cur_step() == 0);
}

TEST_CASE("follow: a negative nudge does not stall the lane") {
    ModLane l;
    configure(l, 8);
    for (int32_t s = 0; s <= 4; ++s) l.follow(s, 0.f);

    l.nudge_slots(-3, 0.f);
    l.follow(4, 0.5f);
    CHECK(l.cur_step() == 1);
    int fires = 0;
    for (int32_t s = 5; s < 15; ++s) { l.follow(s, 0.f); if (l.fired()) ++fires; }
    CHECK(fires == 10);                // still one boundary per deck step
}

TEST_CASE("follow: two lanes of different length never diverge") {
    // The whole point of the design. An equal-rate implementation drifts about
    // two samples per step here; a follower cannot, because the position is an
    // integer modulo of one shared count.
    ModLane a, b;
    configure(a, 8);
    configure(b, 16);
    int fa = 0, fb = 0;
    for (int32_t s = 0; s < 200000; ++s) {     // ~7 hours of 8-step bars
        a.follow(s, 0.f);
        b.follow(s, 0.f);
        if (a.fired()) ++fa;
        if (b.fired()) ++fb;
    }
    CHECK(fa == 200000);
    CHECK(fb == 200000);
    CHECK(a.cur_step() == static_cast<int>((200000 - 1) % 8));
    CHECK(b.cur_step() == static_cast<int>((200000 - 1) % 16));
}
