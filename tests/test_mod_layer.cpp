#include <doctest/doctest.h>
#include "vcv/src/mod_layer.hpp"
#include "vcv/src/generated_panel.hpp"
#include "mod/lane_id.h"
#include "fx/part_fx.h"
#include "vcv/src/init_patch.hpp"
#include <set>

using namespace spkyvcv;

// Depth 0 must reproduce the plain knob push EXACTLY -- the early return in
// modded() makes bit-equality honest here (no arithmetic touches the value).
// This is the "init sounds like today" gate.
TEST_CASE("mod layer: depth 0 is the identity") {
    for (float knob : {0.f, 0.1337f, 0.5f, 0.99f, 1.f, -0.73f}) {
        CHECK(spkymod::modded(knob, 0.f, 0.83f, -1.f, 1.f) == knob);
        CHECK(spkymod::modded(knob, 0.f, -1.f, -1.f, 1.f) == knob);
    }
}

TEST_CASE("mod layer: offset lands in knob space and clamps to the range") {
    CHECK(spkymod::modded(0.5f, 1.f, 0.25f, 0.f, 1.f) == doctest::Approx(0.75f));
    CHECK(spkymod::modded(0.9f, 1.f, 1.f, 0.f, 1.f) == 1.f);       // top clamp
    CHECK(spkymod::modded(-0.9f, 1.f, -1.f, -1.f, 1.f) == -1.f);   // bipolar floor
    CHECK(spkymod::lane_term(0.5f, -0.8f) == doctest::Approx(-0.4f));
    // both masters down -> the center is still (spec §2)
    CHECK(spkymod::center_term(0.f, 1.f, 0.f, -1.f) == 0.f);
    CHECK(spkymod::center_term(1.f, 0.6f, 1.f, 0.2f) == doctest::Approx(0.4f));
}

TEST_CASE("mod layer: kModLayer is exactly the spec's table") {
    const int n = sizeof(kModLayer) / sizeof(kModLayer[0]);
    CHECK(n == 48);
    std::set<int> depthIds, soundIds;
    int centers = 0, tdepth = 0, fxdepth = 0;
    for (int i = 0; i < n; ++i) {
        const auto& t = kModLayer[i];
        CHECK(t.soundId != t.depthId);
        CHECK(t.depthId > REC_B);            // appended block only
        CHECK(t.depthId < NUM_PARAMS);
        CHECK(depthIds.insert(t.depthId).second);
        CHECK(soundIds.insert(t.soundId).second);
        CHECK(t.part <= 2);
        if (t.part == 2) ++centers;
        if (t.kind == MODK_TDEPTH) {
            ++tdepth;
            CHECK(t.slot < spky::LANE_COUNT);
            CHECK(t.slot != spky::LANE_PITCH);   // the anchor stays
        } else if (t.kind == MODK_FXDEPTH) {
            ++fxdepth;
            CHECK(t.slot < spky::FXT_COUNT);
        } else {
            CHECK(t.kind == MODK_HOST);
            CHECK(t.slot < spky::LANE_COUNT);
        }
    }
    CHECK(centers == 6);
    CHECK(tdepth == 6);      // TIMB/DPTH/FILT x two decks
    CHECK(fxdepth == 6);     // MIX/FB/SEND x two decks
    // excluded faces never appear as a sound target
    for (int excluded : {(int)GRIT_A, (int)GRIT_B, (int)FLUXRATE_A, (int)FLUXRATE_B,
                         (int)STAGES_A, (int)STAGES_B, (int)MOD_A, (int)MOD_B,
                         (int)TEMPO, (int)SHUFFLE, (int)PACE, (int)DRIFT,
                         (int)COUPLE, (int)CHOKE, (int)SCALE, (int)STEPS_A,
                         (int)STEPS_B, (int)SONG_A, (int)SONG_B, (int)ENGINE_A,
                         (int)ENGINE_B, (int)REC_A, (int)REC_B, (int)MODBTN})
        CHECK(soundIds.count(excluded) == 0);
}

TEST_CASE("mod layer: init defaults keep today's sound") {
    CHECK(initParamDefault(MODBTN) == 0.f);
    CHECK(initParamDefault(MODD_SOURCE_A) == doctest::Approx(1.0f));
    CHECK(initParamDefault(MODD_DEPTH_A) == doctest::Approx(0.7f));
    CHECK(initParamDefault(MODD_FILT_A) == doctest::Approx(0.55f));
    for (const auto& t : kModLayer)
        if (t.kind != MODK_TDEPTH)
            CHECK(initParamDefault(t.depthId) == 0.f);
}
