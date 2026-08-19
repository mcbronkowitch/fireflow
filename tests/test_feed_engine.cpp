// FEED -- the coupled feedback-FM drone engine.
// Spec: docs/superpowers/specs/2026-08-18-feed-coupled-feedback-fm-design.md
//
// P is feed_cfg::kPairs and is a MEASURED number (spec section 8). Nothing in
// this file may assume its value: every loop runs to kPairs and every
// expectation is derived from the named constants in feed_config.h, never from
// their literals.
#include <doctest/doctest.h>
#include "parts/part.h"
#include "feed/feed_engine.h"
#include "part_engine_contract.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace spky;

namespace {

// A FeedEngine at a known state. set_seed() BEFORE init(), the SynthEngineT
// convention (synth_engine.h) -- init() consumes the seed to draw the SPREAD
// signature and the per-pair feedback offsets, so the reverse order measures a
// different object.
FeedEngine fresh_feed(uint32_t seed = 99u) {
    FeedEngine e;
    e.set_seed(seed);
    e.init(48000.f);
    e.set_cycle(1.f);
    return e;
}

// The five lane targets, in the order Part pushes them: SOURCE, SIZE, PITCH,
// MOTION, LEVEL (engine/mod/lane_id.h). Named for what FEED reads them as.
void feed_lanes(FeedEngine& e, float pitch, float bond = 0.f,
                float spread = 0.f, float depth = 0.5f, float level = 1.f) {
    const float t[LANE_COUNT] = { bond, spread, pitch, depth, level };
    e.set_targets(t, 0.5f);
}

std::vector<float> render_l(FeedEngine& e, int n) {
    std::vector<float> out(n);
    for (auto& s : out) { float l = 0.f, r = 0.f; e.process(l, r); s = l; }
    return out;
}

float peak_of(const std::vector<float>& b) {
    float p = 0.f;
    for (float v : b) p = std::max(p, std::fabs(v));
    return p;
}

// Run the engine long enough for every slope to land: kCtrlInterval samples is
// one control tick, and the glide closes over several of them.
void settle(FeedEngine& e, int ticks = 200) {
    float l = 0.f, r = 0.f;
    for (int i = 0; i < ticks * FeedEngine::kCtrlInterval; ++i) e.process(l, r);
}

}  // namespace

TEST_CASE("feed G1: the engine id is appended, never renumbered") {
    // A saved patch stores the id, so moving one silently reassigns every deck
    // that used it (engine_iface.h). This case is the census; the
    // static_assert in test_deck_bus.cpp is the build-time half.
    CHECK(ENGINE_TEST_TONE == 0);
    CHECK(ENGINE_SYNTH == 1);
    CHECK(ENGINE_SAMPLER == 2);
    CHECK(ENGINE_WAVE == 3);
    CHECK(ENGINE_BODY == 4);
    CHECK(ENGINE_BBD == 5);
    CHECK(ENGINE_FEED == 6);
    CHECK(ENGINE_COUNT == 7);
}

TEST_CASE("feed G2: FeedEngine satisfies the universal part-engine contract") {
    // Silence in stays bounded and finite forever; the process_in/
    // consumes_input pairing holds (FEED overrides neither, so the static
    // assert reads both as IPartEngine's); every no-op setter is safe in any
    // order. tests/part_engine_contract.h owns the reasoning.
    check_part_engine_contract<FeedEngine>([](FeedEngine& e) {
        e.set_seed(7u);
        e.init(48000.f);
    });
}

TEST_CASE("feed G3: a FEED deck is a note deck, and the switch completes") {
    Part p;
    p.init(48000.f, 5u);
    float l = 0.f, r = 0.f;
    p.set_engine(ENGINE_FEED);
    for (int i = 0; i < 500; ++i) p.process(l, r);   // 4 ms fade out + in
    REQUIRE(p.engine_id() == ENGINE_FEED);
    // Part derives the note-deck flag as "not SAMPLER and not BBD"
    // (part.cpp:43 and :460), so FEED gets the melodic phrase machinery for
    // free -- and that is exactly the kind of free behaviour that silently
    // stops being true when someone adds an engine to the exclusion list.
    CHECK(p.mod().pitch_lane_is_note_lane_for_test());
}

TEST_CASE("feed G3b: a FEED deck reports its envelope to the meter") {
    // Part::voice_env/active_voices return 0 for any engine they have no arm
    // for, so without one the VCV LED and Instrument's meter go dead on a FEED
    // deck. A coupled network is one sound, not n voices: slot 0 carries the
    // envelope and active_voices() is 1 while audible (spec section 6).
    Part p;
    p.init(48000.f, 5u);
    float l = 0.f, r = 0.f;
    p.set_engine(ENGINE_FEED);
    for (int i = 0; i < 500; ++i) p.process(l, r);
    REQUIRE(p.engine_id() == ENGINE_FEED);
    p.trigger_manual();
    float m = 0.f;
    for (int i = 0; i < 4800; ++i) { p.process(l, r); m = std::max(m, p.max_voice_env()); }
    CHECK(m > 0.1f);
}
