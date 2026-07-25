#include <doctest/doctest.h>
#include <cstdlib>
#include <vector>
#include "mod/lane.h"
using namespace spky;

// One shared step grid (spec 2026-07-25 mod-lane-step-grid-lock). The step
// clock is normalized -- _phase_inc = rate/sr * (8/steps) makes one step last
// sr/(8*rate) whatever _steps is -- so lanes at the same rate_hz share their
// boundaries no matter how long their cycles are. These tests pin that down at
// the ModLane level, on the per-sample path, away from the 96-sample raster.
namespace {
constexpr float kSr   = 48000.f;
constexpr float kRate = 2.f;          // step = 3000 samples

void configure(ModLane& l, int slots, float shuffle, float variation) {
    l.set_melodic(false);
    l.init(kSr, 4242u);
    l.set_shuffle(shuffle);
    l.set_step(true, slots);
    l.set_rate_hz(kRate);
    l.set_shape(1.f);
    l.set_smooth(0.f);
    l.set_variation(variation);
}

std::vector<int> fire_samples(int slots, int n, float shuffle = 0.f,
                              float variation = 0.f) {
    ModLane l;
    configure(l, slots, shuffle, variation);
    std::vector<int> out;
    for (int i = 0; i < n; ++i) {
        l.process();
        if (l.fired()) out.push_back(i);
    }
    return out;
}

// Boundary detection reads a free-running float phasor, so two lanes with
// different _phase_inc can cross the same instant one sample apart. That is
// float drift, not misalignment -- see the plan's global constraints.
void check_aligned(const std::vector<int>& got, const std::vector<int>& ref) {
    REQUIRE(got.size() == ref.size());
    for (size_t i = 0; i < ref.size(); ++i)
        CHECK(std::abs(got[i] - ref[i]) <= 1);
}
} // namespace

TEST_CASE("grid: equal rate gives one shared step grid for any slot count") {
    const auto ref = fire_samples(8, 48000);
    REQUIRE(ref.size() >= 16);
    for (int slots : {2, 4, 6, 12, 16, 24, 32})
        check_aligned(fire_samples(slots, 48000), ref);
}

TEST_CASE("grid: SHUFFLE warps every even-slot lane identically") {
    const auto ref = fire_samples(8, 48000, 0.7f);
    REQUIRE(ref.size() >= 16);
    for (int slots : {4, 6, 12, 16})
        check_aligned(fire_samples(slots, 48000, 0.7f), ref);
}

TEST_CASE("grid: an external rate walk overrides the lane's own EVOLVE walk") {
    const auto ref = fire_samples(8, 96000, 0.f, 0.f);

    ModLane dut;
    configure(dut, 8, 0.f, 0.9f);            // GROW: would walk _ev_rate +-20%
    dut.set_ev_rate_external(true, 0.f);
    std::vector<int> got;
    for (int i = 0; i < 96000; ++i) {
        dut.process();
        if (dut.fired()) got.push_back(i);
    }
    check_aligned(got, ref);

    // The lane still walks its own value; it is simply not the one used.
    CHECK(dut.ev_rate() != 0.f);
}

TEST_CASE("grid: an even whole-slot kick keeps the lane on the grid") {
    // SHUFFLE is on deliberately: the kick has to preserve both the position
    // inside the step and the step parity the warp is keyed to. An even jump
    // does; that is why SuperModulator::spot rounds to an even count.
    ModLane ref, dut;
    configure(ref, 8, 0.5f, 0.f);
    configure(dut, 12, 0.5f, 0.f);

    std::vector<int> ref_fires, dut_fires;
    for (int i = 0; i < 96000; ++i) {
        ref.process();
        if (ref.fired()) ref_fires.push_back(i);
        if (i == 30000) dut.kick_steps(4, 0.f);
        dut.process();
        if (dut.fired()) dut_fires.push_back(i);
    }
    // The jump itself fires one extra boundary -- that is the audible stumble.
    // Every OTHER fire must still land on a reference boundary.
    REQUIRE(ref_fires.size() >= 16);
    REQUIRE(dut_fires.size() >= 16);
    for (int f : dut_fires) {
        if (f == 30000) continue;
        bool on_ref = false;
        for (int r : ref_fires)
            if (std::abs(f - r) <= 1) { on_ref = true; break; }
        CHECK(on_ref);
    }
}

TEST_CASE("grid: a zero-slot kick is a no-op, not a rewind to the boundary") {
    // Regression guard for the tempting wrong implementation: snapping to
    // shuffle_phase_for_position(cur_step) drops the fraction inside the step
    // and silently delays every later boundary by it.
    ModLane ref, dut;
    configure(ref, 8, 0.f, 0.f);
    configure(dut, 8, 0.f, 0.f);

    std::vector<int> ref_fires, dut_fires;
    for (int i = 0; i < 96000; ++i) {
        ref.process();
        if (ref.fired()) ref_fires.push_back(i);
        if (i == 30000) dut.kick_steps(0, 0.f);
        dut.process();
        if (dut.fired()) dut_fires.push_back(i);
    }
    check_aligned(dut_fires, ref_fires);
}
