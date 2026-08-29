#include <doctest/doctest.h>
#include "mod/lane.h"
#include <cmath>
#include <set>

using namespace spky;

// A FLOW texture lane built the way SuperModulator builds one:
// set_melodic(false) BEFORE init() (docs/engine-map.md §6 -- init() branches
// on _melodic and a reversed order measures a different object), then the
// FLOW push the VCV host actually makes. set_step(false, 0) clamps _steps to
// 1, which is the whole reason kShFlowSlots exists (spec §7 probe 1).
static void make_flow_lane(ModLane& l, float hz, float smooth) {
    l.set_melodic(false);
    l.init(48000.f, 12345);
    l.set_step(false, 0);
    l.set_rate_hz(hz);
    l.set_shape(0.f);
    l.set_smooth(smooth);
    l.set_range(1.f);
    l.set_variation(0.f);
}

TEST_CASE("lane S&H: a FLOW texture lane holds between kShFlowSlots edges") {
    ModLane l;
    make_flow_lane(l, 0.5f, 0.7f);        // 0.5 Hz -> 10 cycles in 20 s

    int prev = -1, edges = 0, violations = 0;
    float shadow = 0.f;
    std::set<float> contDistinct, heldDistinct;
    const int ticks = int(48000.f * 20.f) / ModLane::kTickInterval;
    for (int i = 0; i < ticks; ++i) {
        const float v = l.tick();
        const int slot = ModLane::step_index(l.phase(), ModLane::kShFlowSlots);
        if (slot != prev) { prev = slot; shadow = v; ++edges; }
        // The invariant: the held value is the value latched at the last slot
        // change, and nothing else ever moves it. Counted rather than
        // CHECKed per iteration -- a CHECK inside a 10 000-call loop costs
        // more than the lane does and buries the real failure in noise.
        if (l.stepped_output() != shadow) ++violations;
        contDistinct.insert(v);
        heldDistinct.insert(l.stepped_output());
    }
    CHECK(violations == 0);
    // 10 cycles x 8 slots + the initial latch (measured 2026-08-23)
    CHECK(edges == 81);
    // The point of the feature: the held stream is orders of magnitude
    // coarser than its continuous twin. Measured 32 against 3931; the bound
    // is deliberately loose -- this gate is about the order of magnitude,
    // not about reproducing a float count.
    CHECK(heldDistinct.size() * 20 < contDistinct.size());
}

TEST_CASE("lane S&H: the FLOW grid survives kRateFreeMax") {
    // At 30 Hz a cycle is 1600 samples, i.e. ~16.7 tick() calls, so one of
    // the 8 slots is barely 2 calls wide. This is the case a per-sample
    // probe cannot see: if the 96-sample raster could skip a slot, it would
    // skip it here. Measured 4801 = 600 cycles x 8 + 1 (2026-08-23).
    ModLane l;
    make_flow_lane(l, 30.f, 0.7f);

    int prev = -1, edges = 0, violations = 0;
    float shadow = 0.f;
    const int ticks = int(48000.f * 20.f) / ModLane::kTickInterval;
    for (int i = 0; i < ticks; ++i) {
        const float v = l.tick();
        const int slot = ModLane::step_index(l.phase(), ModLane::kShFlowSlots);
        if (slot != prev) { prev = slot; shadow = v; ++edges; }
        if (l.stepped_output() != shadow) ++violations;
    }
    CHECK(violations == 0);
    CHECK(edges == 4801);
}

TEST_CASE("lane S&H: _steps is NOT the FLOW grid") {
    // The claim the spec's first draft made and probe 1 killed: on the VCV
    // host _steps is 1 in FLOW, so a _steps-derived grid never changes slot
    // and the held output would be a flat line (measured p2p 0.0000 over
    // 20 s). This gate is what stops a future "simplification" back to
    // _steps.
    ModLane l;
    make_flow_lane(l, 0.5f, 0.7f);
    CHECK(l.steps() == 1);                       // the state the host pushes
    float mn = 1e9f, mx = -1e9f;
    const int ticks = int(48000.f * 20.f) / ModLane::kTickInterval;
    for (int i = 0; i < ticks; ++i) {
        l.tick();
        const float h = l.stepped_output();
        if (h < mn) mn = h;
        if (h > mx) mx = h;
    }
    CHECK(mx - mn > 0.5f);       // it moves; a _steps grid gives exactly 0
}

TEST_CASE("lane S&H: in STEP at SMOOTH 0 both halves are the same signal") {
    // The follower is already a staircase, so sampling it changes nothing.
    // Measured max|held - continuous| == 0.000000 over 20 s (2026-08-23).
    // This is the row that tells the owner where the feature is audible and
    // where it is not.
    ModLane l;
    l.set_melodic(false);
    l.init(48000.f, 12345);
    l.set_step(true, 8);
    l.set_rate_hz(0.5f);
    l.set_shape(0.f);
    l.set_smooth(0.f);
    l.set_range(1.f);
    l.set_variation(0.f);

    float worst = 0.f;
    for (int i = 0; i < 48000 * 20; ++i) {
        const float v = l.process();
        worst = std::fmax(worst, std::fabs(l.stepped_output() - v));
    }
    CHECK(worst == 0.f);
}

TEST_CASE("lane S&H: in STEP at SMOOTH 0.7 the held stream collapses") {
    // Same lane, SMOOTH up: continuous visits 149 673 distinct values, held
    // 14 (measured 2026-08-23). The gate is the order of magnitude.
    ModLane l;
    l.set_melodic(false);
    l.init(48000.f, 12345);
    l.set_step(true, 8);
    l.set_rate_hz(0.5f);
    l.set_shape(0.f);
    l.set_smooth(0.7f);
    l.set_range(1.f);
    l.set_variation(0.f);

    int prev = -1, violations = 0;
    float shadow = 0.f;
    std::set<float> contDistinct, heldDistinct;
    for (int i = 0; i < 48000 * 20; ++i) {
        const float v = l.process();
        if (l.cur_step() != prev) { prev = l.cur_step(); shadow = v; }
        if (l.stepped_output() != shadow) ++violations;
        contDistinct.insert(v);
        heldDistinct.insert(l.stepped_output());
    }
    CHECK(violations == 0);
    // measured: 14 held against 149 673 continuous
    CHECK(heldDistinct.size() * 1000 < contDistinct.size());
}
