// Pins for docs/engine-map.md — the measured facts specs keep citing.
// Each TEST_CASE names the map section it defends. If one of these goes red,
// the engine changed: fix the map in the same commit, do not loosen the gate.
//
// Construction order matters everywhere here: set_melodic() BEFORE init()
// (map §6 — init() branches on _melodic when seeding the pattern RNG).
#include <doctest/doctest.h>
#include <cmath>
#include <set>
#include <vector>
#include "mod/lane.h"
using namespace spky;

namespace {

struct Sweep {
    float p2p;
    int distinct;
    float last;
};

// Run `seconds` of process() at 48 kHz and summarize. SMOOTH 0 so the raw
// target is visible; skip a short warmup so the first latch is behind us.
Sweep sweep(ModLane& l, int seconds) {
    const int warm = 100;
    for (int i = 0; i < warm; ++i) l.process();
    float mn = 1e9f, mx = -1e9f, v = 0.f;
    std::set<float> vals;
    for (int i = 0; i < 48000 * seconds - warm; ++i) {
        v = l.process();
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        vals.insert(v);
    }
    return {mx - mn, static_cast<int>(vals.size()), v};
}

ModLane make_lane(bool melodic, uint32_t seed) {
    ModLane l;
    l.set_melodic(melodic);          // BEFORE init() — map §6
    l.init(48000.f, seed);
    l.set_rate_hz(0.5f);
    l.set_smooth(0.f);
    l.set_range(1.f);
    l.set_variation(0.f);
    return l;
}

} // namespace

TEST_CASE("engine-map §3: SHAPE top quarter is an amplitude fade onto a held offset") {
    // Non-melodic lane in FLOW: above SHAPE 0.75 the bank crossfades toward an
    // S&H slot that is frozen (see §4 case below), so depth falls linearly —
    // p2p = 2*(1 - 4*(sh - 0.75)) — and at 1.0 the modulation is a constant.
    auto measure = [](float shape, float vary, uint32_t seed) {
        ModLane l = make_lane(false, seed);
        l.set_step(false, 8);
        l.set_shape(shape);
        l.set_variation(vary);
        return sweep(l, 30);
    };

    Sweep s090 = measure(0.90f, 0.f, 999);
    CHECK(s090.p2p == doctest::Approx(0.8f).epsilon(0.10));

    Sweep s100 = measure(1.00f, 0.f, 999);
    CHECK(s100.p2p < 1e-3f);

    // "It does not fade to silence": the lane parks on a seed-dependent held
    // value, not on the base. Across a handful of seeds the park point must
    // reach well away from zero (map: up to ±0.53).
    float worst = 0.f;
    for (uint32_t seed : {999u, 12345u, 7u, 4242u, 31337u}) {
        Sweep s = measure(1.00f, 0.f, seed);
        CHECK(s.p2p < 1e-3f);
        worst = std::max(worst, std::fabs(s.last));
    }
    CHECK(worst > 0.15f);

    // VARY is the only thing that makes the corner move again.
    Sweep vary = measure(1.00f, 0.5f, 999);
    CHECK(vary.p2p > 0.4f);
}

TEST_CASE("engine-map §1: _flow_melody is ignored whenever STEP is on") {
    // The last two rows of the lane state table are the same measurement:
    // melodic + STEP produces the identical output stream with the flag on or
    // off. Same seed, same code path — the streams must match sample by sample.
    auto run = [](bool flow_melody) {
        ModLane l = make_lane(true, 12345);
        l.set_step(true, 8);
        l.set_flow_melody(flow_melody);
        l.set_shape(0.f);
        std::vector<float> out(48000 * 20);
        for (float& v : out) v = l.process();
        return out;
    };
    std::vector<float> off = run(false);
    std::vector<float> on  = run(true);
    float max_diff = 0.f;
    for (size_t i = 0; i < off.size(); ++i)
        max_diff = std::max(max_diff, std::fabs(off[i] - on[i]));
    CHECK(max_diff < 1e-7f);

    // And that shared stream is the 5-value sine staircase (p2p 2.0), which
    // carries no melodic information — not the phrase.
    {
        ModLane l = make_lane(true, 12345);
        l.set_step(true, 8);
        l.set_flow_melody(true);
        l.set_shape(0.f);
        Sweep s = sweep(l, 20);
        CHECK(s.p2p == doctest::Approx(2.0f).epsilon(0.02));
        CHECK(s.distinct <= 6);
    }

    // The phrase only exists in FLOW (step off): much smaller, seed-dependent
    // ambitus (map: 0.155..0.840 over ten seeds) and a small value set.
    {
        ModLane l = make_lane(true, 12345);
        l.set_step(false, 8);
        l.set_flow_melody(true);
        l.set_shape(0.f);
        Sweep s = sweep(l, 20);
        CHECK(s.p2p > 0.05f);
        CHECK(s.p2p < 1.0f);
        CHECK(s.distinct >= 2);
        CHECK(s.distinct <= 32);
    }
}

TEST_CASE("engine-map §4: FLOW texture lane at the S&H end is a frozen constant") {
    // _sh_slot() early-returns 0 for a non-melodic lane in FLOW (lane.cpp,
    // `if (!_step_mode && !_flow_melody_on()) return 0;`), so pure S&H is one
    // permanently held value — the observable face of that early return.
    ModLane l = make_lane(false, 999);
    l.set_step(false, 8);
    l.set_shape(1.0f);
    Sweep s = sweep(l, 30);
    CHECK(s.distinct == 1);
}
