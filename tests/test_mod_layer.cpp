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
        CHECK(spkymod::modded(knob, 0.f, 0.83f, -0.4f, -1.f, 1.f) == knob);
        CHECK(spkymod::modded(knob, 0.f, -1.f, 1.f, -1.f, 1.f) == knob);
    }
}

TEST_CASE("mod layer: offset lands in knob space and clamps to the range") {
    // Unchanged in meaning -- this is the positive half, and it must keep
    // producing exactly the floats it produced before the split. The stepTerm
    // passed here is deliberately NOT the continuous one: a positive depth
    // that read it would fail these three lines.
    CHECK(spkymod::modded(0.5f, 1.f, 0.25f, -0.25f, 0.f, 1.f)
          == doctest::Approx(0.75f));
    CHECK(spkymod::modded(0.9f, 1.f, 1.f, -1.f, 0.f, 1.f) == 1.f);      // top clamp
    CHECK(spkymod::modded(-0.9f, 1.f, -1.f, 1.f, -1.f, 1.f) == -1.f);   // bipolar floor
    // lane_term and center_term are untouched by this task; these three lines
    // move only because the case around them does.
    CHECK(spkymod::lane_term(0.5f, -0.8f) == doctest::Approx(-0.4f));
    // both masters down -> the center is still (spec §2)
    CHECK(spkymod::center_term(0.f, 1.f, 0.f, -1.f) == 0.f);
    CHECK(spkymod::center_term(1.f, 0.6f, 1.f, 0.2f) == doctest::Approx(0.4f));
}

TEST_CASE("mod layer: the dead zone makes noon exact and keeps both stops") {
    CHECK(spkymod::depth_of(0.f) == 0.f);
    CHECK(spkymod::depth_of(spkymod::kDepthDead) == 0.f);
    CHECK(spkymod::depth_of(-spkymod::kDepthDead) == 0.f);
    CHECK(spkymod::depth_of(0.02f) == 0.f);
    CHECK(spkymod::depth_of(-0.02f) == 0.f);
    CHECK(spkymod::depth_of(1.f) == doctest::Approx(1.f));
    CHECK(spkymod::depth_of(-1.f) == doctest::Approx(-1.f));
    // just outside the zone is small but not zero -- the zone is a detent,
    // not a chunk out of the axis
    CHECK(spkymod::depth_of(0.0401f) > 0.f);
    CHECK(spkymod::depth_of(0.0401f) < 0.01f);
}

TEST_CASE("mod layer: depth_knob is depth_of's inverse") {
    // This is what lets an engine-backed init depth survive the rescale, so
    // init still sounds exactly like today (spec §2).
    for (float d : {1.f, 0.7f, 0.55f, 0.f, -0.7f, -1.f})
        CHECK(spkymod::depth_of(spkymod::depth_knob(d)) == doctest::Approx(d));
    CHECK(spkymod::depth_knob(0.7f) == doctest::Approx(0.712f));
    CHECK(spkymod::depth_knob(0.55f) == doctest::Approx(0.568f));
    CHECK(spkymod::depth_knob(1.f) == doctest::Approx(1.f));
}

TEST_CASE("mod layer: the sign picks the term, the magnitude scales it") {
    const float cont = 0.5f, step = -0.25f;
    // right of noon: the continuous term, exactly as before the split
    CHECK(spkymod::modded(0.5f, 0.5f, cont, step, 0.f, 1.f)
          == doctest::Approx(0.75f));
    // left of noon: the stepped term, NOT inverted -- 0.5 + 0.5*(-0.25)
    CHECK(spkymod::modded(0.5f, -0.5f, cont, step, 0.f, 1.f)
          == doctest::Approx(0.375f));
    // equal magnitudes, equal terms -> equal results
    CHECK(spkymod::modded(0.5f, -0.5f, cont, cont, 0.f, 1.f)
          == doctest::Approx(spkymod::modded(0.5f, 0.5f, cont, cont, 0.f, 1.f)));
    // clamping still happens in knob space
    CHECK(spkymod::modded(0.9f, -1.f, 0.f, 1.f, 0.f, 1.f) == 1.f);
    CHECK(spkymod::modded(-0.9f, -1.f, 0.f, -1.f, -1.f, 1.f) == -1.f);
}

TEST_CASE("mod layer: kModLayer is exactly the spec's table") {
    const int n = sizeof(kModLayer) / sizeof(kModLayer[0]);
    CHECK(n == 50);          // 48 + PAN A/B (spec 2026-08-30 pan)
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

TEST_CASE("mod layer: init defaults keep today's sound through the dead zone") {
    // "today's sound" is still the rule for the three engine-backed faces.
    // What the factory patch dials on top of them is pinned at the bottom.
    CHECK(initParamDefault(MODBTN) == 0.f);
    // The knob positions are the PRE-IMAGES: 0.7 depth sits at knob 0.712,
    // because depth_of() rescales the axis around the dead zone. What has to
    // stay at the booted engine value is the DEPTH, not the knob.
    CHECK(spkymod::depth_of(initParamDefault(MODD_SOURCE_A))
          == doctest::Approx(1.0f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_DEPTH_A))
          == doctest::Approx(0.7f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_FILT_A))
          == doctest::Approx(0.55f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_SOURCE_B))
          == doctest::Approx(1.0f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_DEPTH_B))
          == doctest::Approx(0.7f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_FILT_B))
          == doctest::Approx(0.55f));
    // Every FX depth still boots at standstill, and so does every HOST depth
    // the factory patch does not dial. NewInit.vcvm (2026-08-29) is the first
    // snapshot to dial any: five HOST faces boot off noon (res/gen_panel.py's
    // INIT_MOD_KNOBS). Spelled out as a literal set rather than read back off
    // the table, so widening the patch's reach is a deliberate edit here too.
    const std::set<int> dialled = {(int)MODD_SUB_B, (int)MODD_DETUNE_A,
                                   (int)MODD_DETUNE_B, (int)MODD_MORPH,
                                   (int)MODD_REV_DIFF};
    for (const auto& t : kModLayer) {
        if (t.kind == MODK_TDEPTH) continue;
        if (dialled.count(t.depthId)) {
            // A dialled default is a raw knob position off the preset, not a
            // _depth_knob pre-image, so it may only land on a HOST face --
            // the engine-backed kinds are pushed as depths and would arrive
            // rescaled. Positive: left of noon would put the face on the
            // lane's S&H twin, which is a different patch, not a deeper one.
            CHECK(t.kind == MODK_HOST);
            CHECK(spkymod::depth_of(initParamDefault(t.depthId)) > 0.f);
        } else {
            CHECK(initParamDefault(t.depthId) == 0.f);
        }
    }
    // The five, by value: knob positions transcribed from the preset, and the
    // depths they resolve to. Both sides are literals -- reading either off
    // gen_panel.py or off depth_of() would let one number move both.
    CHECK(initParamDefault(MODD_SUB_B) == doctest::Approx(0.885333300f));
    CHECK(initParamDefault(MODD_DETUNE_A) == doctest::Approx(0.298666626f));
    CHECK(initParamDefault(MODD_DETUNE_B) == doctest::Approx(0.250666678f));
    CHECK(initParamDefault(MODD_MORPH) == doctest::Approx(0.279517978f));
    CHECK(initParamDefault(MODD_REV_DIFF) == doctest::Approx(0.442666322f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_SUB_B))
          == doctest::Approx(0.880555520f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_DETUNE_A))
          == doctest::Approx(0.269444402f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_DETUNE_B))
          == doctest::Approx(0.219444456f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_MORPH))
          == doctest::Approx(0.249497894f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_REV_DIFF))
          == doctest::Approx(0.419444085f));
}
