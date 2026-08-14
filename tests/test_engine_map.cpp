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

TEST_CASE("engine-map §1: on a STEP lane _flow_melody picks phrase or waveform") {
    // The last two rows of the lane state table are two behaviours, not one.
    // _flow_melody is an engine-class flag (note deck vs SAMPLER/BBD), and
    // since 2026-08-14 a note lane emits its phrase in STEP as in FLOW while a
    // SAMPLER/BBD lane keeps running the waveform bank. Same seed, same
    // everything else — the streams must diverge, and by far more than the
    // 6e−08 of §5 (measured max deviation 1.086 at this seed).
    auto run = [](bool flow_melody, int steps = 8) {
        ModLane l = make_lane(true, 12345);
        l.set_step(true, steps);
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
    CHECK(max_diff > 0.5f);

    // With the flag off — a SAMPLER or BBD deck — that stream is still the
    // 5-value sine staircase (p2p 2.0), which carries no melodic information.
    {
        ModLane l = make_lane(true, 12345);
        l.set_step(true, 8);
        l.set_flow_melody(false);
        l.set_shape(0.f);
        Sweep s = sweep(l, 20);
        CHECK(s.p2p == doctest::Approx(2.0f).epsilon(0.02));
        CHECK(s.distinct <= 6);
    }

    // With it on — a note deck — it is the phrase: the same small,
    // seed-dependent ambitus the FLOW row has, at SHAPE 0 as at SHAPE 1.
    {
        ModLane l = make_lane(true, 12345);
        l.set_step(true, 8);
        l.set_flow_melody(true);
        l.set_shape(0.f);
        Sweep s = sweep(l, 20);
        CHECK(s.p2p > 0.05f);
        CHECK(s.p2p < 1.0f);
        CHECK(s.distinct >= 2);
        CHECK(s.distinct <= 32);
    }

    // At 8 steps the STEP clock scaling (8/steps) is 1, so the note lane's STEP
    // stream is the FLOW stream sample for sample. This is the identity the old
    // version of this case asserted between the two STEP rows; it holds between
    // the two note-deck rows instead, and only at this step count — measured
    // max deviation 0.298 at 4 steps and 0.526 at 16.
    {
        ModLane l = make_lane(true, 12345);
        l.set_step(false, 8);
        l.set_flow_melody(true);
        l.set_shape(0.f);
        std::vector<float> flow(48000 * 20);
        for (float& v : flow) v = l.process();
        float d = 0.f;
        for (size_t i = 0; i < flow.size(); ++i)
            d = std::max(d, std::fabs(flow[i] - on[i]));
        CHECK(d < 1e-7f);

        std::vector<float> on16 = run(true, 16);
        float d16 = 0.f;
        for (size_t i = 0; i < flow.size(); ++i)
            d16 = std::max(d16, std::fabs(flow[i] - on16[i]));
        CHECK(d16 > 0.1f);
    }

    // The FLOW row of the table, measured on its own rather than inherited
    // through the identity above: the phrase's much smaller, seed-dependent
    // ambitus (map: 0.155..0.822 over ten seeds) and a small value set. Today
    // the two blocks above imply this one, because they assert the STEP and
    // FLOW streams are equal at the 8 steps they run at; that implication is
    // a property of the step count, so this block is what still pins the FLOW
    // row if the step count above ever moves.
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
