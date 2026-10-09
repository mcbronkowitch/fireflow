// control/params.hpp is generated from the same PARAMS list as VCV's
// generated_panel.hpp. These checks hold the two copies together and pin the
// range table against what configControls() used to hard-code.
#include <doctest/doctest.h>
#include <cmath>
#include "control/params.hpp"
#include "vcv/src/generated_panel.hpp"
#include "vcv/src/init_patch.hpp"

TEST_CASE("params: ffctl ids are VCV's ids") {
    CHECK(int(ffctl::NUM_PARAMS) == int(spkyvcv::NUM_PARAMS));
    CHECK(ffctl::PART_STRIDE == spkyvcv::PART_STRIDE);
    CHECK(int(ffctl::RATE_B) == int(spkyvcv::RATE_B));
    CHECK(int(ffctl::PAN_B) == int(spkyvcv::PAN_B));
    CHECK(int(ffctl::MODBTN) == int(spkyvcv::MODBTN));
}

TEST_CASE("params: mod layer and init values are VCV's") {
    constexpr int n = sizeof(ffctl::kModLayer) / sizeof(ffctl::kModLayer[0]);
    REQUIRE(n == int(sizeof(spkyvcv::kModLayer) / sizeof(spkyvcv::kModLayer[0])));
    for (int i = 0; i < n; ++i) {
        CHECK(ffctl::kModLayer[i].soundId == spkyvcv::kModLayer[i].soundId);
        CHECK(ffctl::kModLayer[i].depthId == spkyvcv::kModLayer[i].depthId);
        CHECK(ffctl::kModLayer[i].kind == spkyvcv::kModLayer[i].kind);
        CHECK(ffctl::kModLayer[i].slot == spkyvcv::kModLayer[i].slot);
        CHECK(ffctl::kModLayer[i].part == spkyvcv::kModLayer[i].part);
    }
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i)
        CHECK(ffctl::kInitParamDefaults[i] == spkyvcv::kInitParamDefaults[i]);
}

TEST_CASE("params: every init value lies in its range") {
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) {
        INFO("param " << i);
        CHECK(ffctl::kInitParamDefaults[i] >= ffctl::kParamRange[i].lo);
        CHECK(ffctl::kInitParamDefaults[i] <= ffctl::kParamRange[i].hi);
    }
}

TEST_CASE("params: ranges are configControls()'s") {
    using namespace ffctl;
    auto is = [](int id, float lo, float hi, bool snap) {
        INFO("param " << id);
        CHECK(kParamRange[id].lo == lo);
        CHECK(kParamRange[id].hi == hi);
        CHECK(kParamRange[id].snap == snap);
    };
    is(RATE_A, 0.f, 1.f, false);
    is(CHOKE, -1.f, 1.f, false);   is(PULL, -1.f, 1.f, false);
    is(FILT_B, -1.f, 1.f, false);  is(PAN_A, -1.f, 1.f, false);
    is(MELODY_A, -1.f, 1.f, false); is(GRIT_B, -1.f, 1.f, false);
    is(SCALE, 0.f, 12.f, true);    is(SONG_A, 0.f, 13.f, true);
    is(FLUXRATE_B, 0.f, 11.f, true); is(STEPS_A, 0.f, 16.f, true);
    is(ENGINE_B, 0.f, 5.f, true);  is(REC_A, 0.f, 1.f, true);
    is(MODBTN, 0.f, 1.f, true);    is(MODD_RATE_A, -1.f, 1.f, false);
    is(TEMPO, 0.f, 1.f, false);    is(DRIFT, 0.f, 1.f, false);
}
