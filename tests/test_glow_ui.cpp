// tests/test_glow_ui.cpp
#include <doctest/doctest.h>
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

TEST_CASE("glow: a refuse flash is active only within its window after mark") {
    // The module's own refusal signal: Flow declines an op (locked generator,
    // empty undo slot, a pad whose place does not decode) by returning false,
    // and only the module knows it happened. This is that signal's headless
    // coverage.
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

TEST_CASE("glow: the scale knob travels from least to most friction") {
    // A permutation check alone would test the table, not the feature. The
    // monotonicity check is what catches a kScaleW retune that reorders the
    // groups and silently leaves the knob travel no longer running calm to
    // sharp.
    bool seen[spky::SCALE_LIST_COUNT] = {};
    for (int i = 0; i < spky::SCALE_LIST_COUNT; ++i) {
        const int s = spkyvcv::kScaleKnobOrder[i];
        REQUIRE(s >= 0);
        REQUIRE(s < spky::SCALE_LIST_COUNT);
        CHECK(!seen[s]);
        seen[s] = true;
    }
    for (int i = 1; i < spky::SCALE_LIST_COUNT; ++i)
        CHECK(spky::flow::kScaleW[spkyvcv::kScaleKnobOrder[i]] <=
              spky::flow::kScaleW[spkyvcv::kScaleKnobOrder[i - 1]]);
}

TEST_CASE("glow: knob position 0 is AUTO, the rest are scales") {
    CHECK(spkyvcv::scale_of_knob(0) == -1);
    for (int p = 1; p <= spky::SCALE_LIST_COUNT; ++p)
        CHECK(spkyvcv::scale_of_knob(p) == spkyvcv::kScaleKnobOrder[p - 1]);
    // Out of range reads as AUTO rather than as scale 0 -- a corrupt patch
    // must not silently retune the instrument to Aeolian.
    CHECK(spkyvcv::scale_of_knob(-3) == -1);
    CHECK(spkyvcv::scale_of_knob(99) == -1);
}

TEST_CASE("glow: a saved root override outside 0..11 reads as AUTO") {
    // Spec 5 asks for the root override's JSON round-trip under test, and the
    // non-obvious half of it is the validation, not the jansson call: Rack's
    // Param::setValue does not clamp and paramsFromJson writes straight
    // through, so a hand-edited patch reaches this with anything at all.
    // Glow.cpp keeps only the json_is_integer type check and hands the number
    // here, which is why this is testable without rack.hpp.
    for (int r = 0; r <= 11; ++r) CHECK(spkyvcv::clamp_root_override(r) == r);
    CHECK(spkyvcv::clamp_root_override(12) == -1);
    CHECK(spkyvcv::clamp_root_override(99) == -1);
    CHECK(spkyvcv::clamp_root_override(-1) == -1);      // the AUTO sentinel
    CHECK(spkyvcv::clamp_root_override(-7) == -1);
}
