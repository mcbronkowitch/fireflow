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
            l.follow(s, 0.f, 0.f);
            if (l.fired()) ++fires;
        }
        CHECK(fires == 200);          // exactly one boundary per deck step
    }
}

TEST_CASE("follow: the slot index is the deck count modulo the cycle") {
    ModLane l;
    configure(l, 6);
    for (int32_t s = 0; s < 40; ++s) {
        l.follow(s, 0.f, 0.f);
        CHECK(l.cur_step() == static_cast<int>(s % 6));
    }
}

TEST_CASE("follow: repeat calls inside one deck step do not re-fire") {
    ModLane l;
    configure(l, 8);
    l.follow(0, 0.f, 0.f);
    REQUIRE(l.fired());
    for (float frac : {0.25f, 0.5f, 0.75f, 0.99f}) {
        l.follow(0, frac, 0.f);
        CHECK_FALSE(l.fired());
        CHECK(l.cur_step() == 0);
    }
    l.follow(1, 0.f, 0.f);
    CHECK(l.fired());
}

TEST_CASE("follow: a multi-step advance replays every slot in order") {
    // The raster window normally holds at most one deck step, but COUPLE and
    // DRIFT can push pitch_scale up. A skipped slot would drop a wrap event.
    ModLane l;
    configure(l, 4);
    l.follow(0, 0.f, 0.f);
    std::vector<int> seen;
    int wraps = 0;
    for (int32_t s = 3; s <= 24; s += 3) {          // three slots per call
        l.follow(s, 0.f, 0.f);
        seen.push_back(l.cur_step());
        if (l.wrapped()) ++wraps;
    }
    // Landing slots after 3, 6, ..., 24 deck steps in a 4-slot cycle.
    CHECK(seen == std::vector<int>{3, 2, 1, 0, 3, 2, 1, 0});
    // The landing slots alone don't prove the walk ran: a "land only" guard
    // (skip straight to slot_of(pos, slots) instead of stepping through every
    // intervening slot) produces the exact same sequence above, because the
    // final slot after N elapsed steps is the same either way. What only the
    // walk can produce is a wrap EVERY time the walk crosses slot 0 -- and it
    // drives EVOLVE's per-wrap walk and RENEW's regen, which a landing skips
    // entirely. 24 elapsed steps in a 4-slot cycle cross slot 0 exactly 6
    // times (multiples of 4 from 1 to 24: 4, 8, 12, 16, 20, 24), regardless of
    // how the 24 steps are chunked across follow() calls -- verified against
    // slot_of's definition, not assumed.
    CHECK(wraps == 6);
}

TEST_CASE("follow: wrapped() marks the cycle seam, once per cycle") {
    ModLane l;
    configure(l, 4);
    for (int32_t s = 0; s < 13; ++s) {
        l.follow(s, 0.f, 0.f);
        // s == 0 is the cold start. It enters slot 0, but no cycle ended, so
        // no wrap runs -- the same choice tick() makes at its own cold start.
        CHECK(l.wrapped() == (s != 0 && s % 4 == 0));
    }
}

TEST_CASE("follow: arming never runs a cycle wrap, whatever slot it lands on") {
    // Wrap events evolve the pattern that just ENDED. At a cold start none
    // did, and STEP entry restarts the deck count at 0 -- so without this,
    // every switch into STEP would walk EVOLVE once on all four texture lanes
    // and burn RNG draws for a phrase nobody heard.
    for (int32_t start : {0, 1, 4, 8, 13}) {
        ModLane l;
        configure(l, 4);
        l.set_variation(0.9f);        // GROW: a wrap here would walk _ev_*
        l.follow(start, 0.f, 0.f);
        CHECK_FALSE(l.wrapped());
        CHECK(l.fired());
        CHECK(l.cur_step() == static_cast<int>(start % 4));
    }
}

TEST_CASE("follow: a slot nudge offsets the lane and fires immediately") {
    ModLane l;
    configure(l, 8);
    for (int32_t s = 0; s <= 4; ++s) l.follow(s, 0.f, 0.f);
    REQUIRE(l.cur_step() == 4);

    l.nudge_slots(3, 0.f);
    l.follow(4, 0.5f, 0.f);                 // same deck step, mid-step
    CHECK(l.fired());                  // the stumble is audible at once
    CHECK(l.cur_step() == 7);

    l.follow(5, 0.f, 0.f);                  // the offset persists
    CHECK(l.cur_step() == 0);

    // A nudge that crosses the cycle seam is a positional jump, not elapsed
    // time -- pin that it is never mistaken for a genuine wrap (which would
    // also walk EVOLVE and burn RNG draws nobody asked for). nudge_slots()
    // has to move _follow_pos by the same amount as _follow_offset, or the
    // next follow() sees a phantom elapsed distance and replays through
    // every slot in between -- including slot 0, if the jump happens to
    // cross it, exactly as here (slot 4, nudge by +4, straight over the
    // seam to slot 8 == slot 0 of the next cycle).
    ModLane l3;
    configure(l3, 8);
    for (int32_t s = 0; s <= 4; ++s) l3.follow(s, 0.f, 0.f);
    REQUIRE(l3.cur_step() == 4);

    l3.nudge_slots(4, 0.f);                 // 4 -> 8: exactly one full cycle
    l3.follow(4, 0.5f, 0.f);                // same deck step
    CHECK(l3.cur_step() == 0);
    CHECK_FALSE(l3.wrapped());              // the seam crossing is not a wrap
}

TEST_CASE("follow: a zero-slot nudge is a shape kick and nothing more") {
    ModLane l;
    configure(l, 8);
    for (int32_t s = 0; s <= 4; ++s) l.follow(s, 0.f, 0.f);
    REQUIRE(l.cur_step() == 4);

    l.nudge_slots(0, 0.1f);
    l.follow(4, 0.5f, 0.f);                 // same deck step, mid-step
    CHECK_FALSE(l.fired());                 // no new slot -- nothing to report
    CHECK(l.cur_step() == 4);               // slot unchanged

    // Contrast: a non-zero nudge in the exact same situation still fires.
    ModLane l2;
    configure(l2, 8);
    for (int32_t s = 0; s <= 4; ++s) l2.follow(s, 0.f, 0.f);
    REQUIRE(l2.cur_step() == 4);

    l2.nudge_slots(1, 0.1f);
    l2.follow(4, 0.5f, 0.f);
    CHECK(l2.fired());
    CHECK(l2.cur_step() == 5);
}

TEST_CASE("follow: a negative nudge does not stall the lane") {
    ModLane l;
    configure(l, 8);
    for (int32_t s = 0; s <= 4; ++s) l.follow(s, 0.f, 0.f);

    l.nudge_slots(-3, 0.f);
    l.follow(4, 0.5f, 0.f);
    CHECK(l.cur_step() == 1);
    int fires = 0;
    for (int32_t s = 5; s < 15; ++s) { l.follow(s, 0.f, 0.f); if (l.fired()) ++fires; }
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
        a.follow(s, 0.f, 0.f);
        b.follow(s, 0.f, 0.f);
        if (a.fired()) ++fa;
        if (b.fired()) ++fb;
    }
    CHECK(fa == 200000);
    CHECK(fb == 200000);
    CHECK(a.cur_step() == static_cast<int>((200000 - 1) % 8));
    CHECK(b.cur_step() == static_cast<int>((200000 - 1) % 16));
}
