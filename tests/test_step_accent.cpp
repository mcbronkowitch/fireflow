// The STEP accent: per-note velocity and decay from the groove rank.
// Spec: docs/superpowers/specs/2026-08-15-step-accent-design.md
//
// Setup note: set_step() does NOT regenerate the groove -- the next cycle
// wrap does. Every helper below therefore runs the lane past a wrap before it
// believes anything it reads, and the accents are collected over a whole
// cycle delimited by wrap_count_for_test(), never by a hand-computed sample
// count (the STEP clock scales the cycle by 8/steps, so a fixed count is a
// different fraction of a cycle at every STEPS value).
#include <doctest/doctest.h>
#include "mod/lane.h"
#include "parts/part.h"
#include <algorithm>
#include <set>
#include <vector>

using namespace spky;

namespace {

// A note deck in STEP: melodic AND flow_melody, which is what Part pushes for
// SYNTH/WAVE/BODY. set_melodic() BEFORE init() -- docs/engine-map.md section 6.
ModLane note_step(uint32_t seed, int steps) {
    ModLane l;
    l.set_melodic(true);
    l.init(48000.f, seed);
    l.set_flow_melody(true);
    l.set_step(true, steps);
    l.set_rate_hz(0.5f);
    l.set_shape(0.f);
    l.set_smooth(0.f);
    l.set_range(1.f);
    l.set_variation(0.f);
    return l;
}

constexpr int kSampleCap = 8'000'000;   // ~166 s: far past any cycle here

void run_to_wrap(ModLane& l, uint32_t target) {
    for (int i = 0; i < kSampleCap; ++i) {
        l.process();
        if (l.wrap_count_for_test() >= target) return;
    }
    FAIL("lane never reached the requested wrap count");
}

// The accents emitted over exactly one cycle, in fire order, measured after
// the groove has settled.
std::vector<float> accents_in_cycle(ModLane& l) {
    run_to_wrap(l, 2);
    const uint32_t end = l.wrap_count_for_test() + 1;
    std::vector<float> out;
    for (int i = 0; i < kSampleCap; ++i) {
        l.process();
        if (l.fired()) out.push_back(l.note_accent());
        if (l.wrap_count_for_test() >= end) return out;
    }
    FAIL("lane never wrapped while collecting");
    return out;
}

const int kStepSet[] = {4, 8, 16};
const uint32_t kSeeds[] = {999u, 12345u, 7u, 4242u};

}  // namespace

TEST_CASE("accent G1: the anchor is at full strength, whatever DENSE is") {
    for (int steps : kStepSet) {
        for (uint32_t seed : kSeeds) {
            CAPTURE(steps);
            CAPTURE(seed);

            ModLane sparse = note_step(seed, steps);
            sparse.set_density(0.f);
            std::vector<float> a_sparse = accents_in_cycle(sparse);
            REQUIRE(a_sparse.size() == 1);          // k == 1: only the anchor fires
            CHECK(a_sparse[0] == doctest::Approx(0.f));

            // The contrast is part of the gate, not decoration: without it a
            // stub returning a constant 0 would pass this case.
            ModLane dense = note_step(seed, steps);
            dense.set_density(1.f);
            std::vector<float> a_dense = accents_in_cycle(dense);
            REQUIRE(!a_dense.empty());
            CHECK(*std::max_element(a_dense.begin(), a_dense.end()) > 0.9f);
        }
    }
}

TEST_CASE("accent G2: at DENSE 1 the contour is the whole rank scale") {
    for (int steps : kStepSet) {
        for (uint32_t seed : kSeeds) {
            CAPTURE(steps);
            CAPTURE(seed);
            ModLane l = note_step(seed, steps);
            l.set_density(1.f);
            std::vector<float> a = accents_in_cycle(l);

            // One note per step: this is also what pins L == the STEPS count.
            REQUIRE(a.size() == static_cast<size_t>(steps));
            std::set<float> uniq(a.begin(), a.end());
            CHECK(uniq.size() == static_cast<size_t>(steps));   // a permutation
            CHECK(*uniq.begin() == doctest::Approx(0.f));
            CHECK(*uniq.rbegin() == doctest::Approx(1.f));
        }
    }
}

TEST_CASE("accent G3: FLOW reports 0, including right after leaving STEP") {
    ModLane l = note_step(12345u, 8);
    l.set_density(1.f);
    run_to_wrap(l, 2);

    bool saw_nonzero = false;
    for (int i = 0; i < 400000; ++i) {
        l.process();
        if (l.fired() && l.note_accent() != 0.f) saw_nonzero = true;
    }
    REQUIRE(saw_nonzero);      // the STEP leg really produced accents

    l.set_step(false, 8);
    bool leaked = false;
    for (int i = 0; i < 400000; ++i) {
        l.process();
        if (l.note_accent() != 0.f) leaked = true;
    }
    CHECK_FALSE(leaked);
}

TEST_CASE("accent: a STEP deck pushes its note accent into the active engine") {
    Part part;
    part.init(48000.f, 0xabcd1234u);          // null FX memory is fine here
    part.set_engine(ENGINE_SYNTH);
    part.mod().set_tempo_bpm(120.f);
    part.mod().set_rate(0.8f);
    part.mod().set_density(1.f);
    part.set_step(true, 8);

    float l = 0.f, r = 0.f;
    float seen_max = 0.f;
    bool seen_any = false;
    for (int i = 0; i < 48000 * 8; ++i) {
        part.process(l, r);
        const float a = part.synth().accent_for_test();
        if (a > 0.f) { seen_any = true; seen_max = std::max(seen_max, a); }
    }
    CHECK(seen_any);              // the push happens at all
    CHECK(seen_max > 0.9f);       // and it carries the whole range, not a floor

    // FLOW must not push a stale accent into the drone.
    part.set_step(false, 8);
    for (int i = 0; i < 48000; ++i) part.process(l, r);
    float flow_max = 0.f;
    for (int i = 0; i < 48000 * 4; ++i) {
        part.process(l, r);
        flow_max = std::max(flow_max, part.synth().accent_for_test());
    }
    CHECK(flow_max == doctest::Approx(0.f));
}
