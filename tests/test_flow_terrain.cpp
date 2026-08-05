// tests/test_flow_terrain.cpp
#include "doctest/doctest.h"
#include "flow/terrain.h"
#include "flow/taste.h"
using namespace spky::flow;

static TerrainState st(uint32_t m) { TerrainState s; s.master = m; return s; }

TEST_CASE("flow terrain: 10k seeds stay inside taste limits (spec 7.1)") {
    for (uint32_t k = 1; k <= 10000; ++k) {
        Terrain t = generate(st(k * 2654435761u));
        for (int p = 0; p < P_COUNT; ++p) {
            CAPTURE(k); CAPTURE(kParams[p].name);
            CHECK(t.base[p] >= kParams[p].lo);
            CHECK(t.base[p] <= kParams[p].hi);
        }
        for (int m = 0; m < MACRO_COUNT; ++m) {
            const auto& mm = t.map[m];
            CHECK(mm.n_targets >= 1);
            float span_max = 0.f;
            for (int i = 0; i < mm.n_targets; ++i) {
                const auto& c = mm.targets[i];
                bool up = c.bp[4] >= c.bp[0];
                for (int b = 0; b < 5; ++b) {
                    CHECK(c.bp[b] >= kParams[c.param].lo);
                    CHECK(c.bp[b] <= kParams[c.param].hi);
                    if (b) { if (up) CHECK(c.bp[b] >= c.bp[b-1]);
                             else    CHECK(c.bp[b] <= c.bp[b-1]); }
                }
                float norm = (kParams[c.param].hi - kParams[c.param].lo);
                float sp = (c.bp[4] > c.bp[0] ? c.bp[4]-c.bp[0]
                                              : c.bp[0]-c.bp[4]) / norm;
                if (sp > span_max) span_max = sp;
            }
            CHECK(span_max >= kMinSpan);       // no dead knob (spec 7.1)
        }
        // Named constraints (spec 7.1).
        if (int(t.base[P_ENGINE_A] + .5f) == ENGINE_BODY)
            CHECK(t.base[P_FILT_A] >= kBodyFiltFloor);
        if (int(t.base[P_ENGINE_B] + .5f) == ENGINE_BODY)
            CHECK(t.base[P_FILT_B] >= kBodyFiltFloor);
        // Ledger watch item: the FILT floor must ALSO hold on every story
        // curve breakpoint that drives a BODY deck's FILT. taste.h's BRIGHT
        // dawn story deliberately draws FILT bp0 down to -0.55 (below the
        // floor), and this walk is the assertion that goes red if the
        // generator's curve clamp disappears while the base clamp above
        // stays.
        //
        // SCOPE, stated because an earlier version of this comment overclaimed
        // it: this proves the floor for ONE terrain evaluated on its own, and
        // nothing more. It says nothing about a NEW blend, which interpolates
        // FILT between two terrains clamped under different engine
        // assignments and left a BODY deck below the floor for nearly a whole
        // ramp until the runtime guard was added. The blend-time floor is
        // asserted in tests/test_flow_new.cpp ("the BODY FILT floor holds at
        // every tick of a blend"); this case cannot see it.
        {
            bool bodyA = int(t.base[P_ENGINE_A] + .5f) == ENGINE_BODY;
            bool bodyB = int(t.base[P_ENGINE_B] + .5f) == ENGINE_BODY;
            for (int m = 0; m < MACRO_COUNT; ++m)
                for (int i = 0; i < t.map[m].n_targets; ++i) {
                    const auto& c = t.map[m].targets[i];
                    if ((c.param == P_FILT_A && bodyA) ||
                        (c.param == P_FILT_B && bodyB)) {
                        CAPTURE(k); CAPTURE(m);
                        for (int b = 0; b < 5; ++b)
                            CHECK(c.bp[b] >= kBodyFiltFloor);
                    }
                }
        }
        // WHY THIS ONE CANNOT GO RED, said out loud because this project
        // discards tests that cannot: both DENSITY bases are the DENSITY
        // "rate" story's bp0 draw (taste.h, spans {.02,.08} and {.02,.08} --
        // and an UNPICKED variant's targets still take their own bp0 as a
        // base, so there is no path where they come from anywhere else).
        // Every reachable draw is at most 0.08, so `both_hot` is false by
        // construction and the no-double-density clamp in terrain.cpp's
        // apply_constraints never fires either.
        //
        // It stays because what it asserts is the INVARIANT, not the clamp:
        // the day a taste-table edit lifts a DENSITY bp0 span above 0.5 this
        // line goes red on the first seed that draws two hot decks, which is
        // exactly when someone needs to be told the clamp has woken up. Read
        // it as a tripwire on the table, not as coverage of the guard.
        bool both_hot = t.base[P_DENSITY_A] > 0.5f && t.base[P_DENSITY_B] > 0.5f;
        CHECK(!both_hot);
        CHECK(t.weather_n >= kWeatherOscMin);
        CHECK(t.weather_n <= kWeatherOscMax);
    }
}

TEST_CASE("flow terrain: determinism - same state twice, identical terrain") {
    Terrain a = generate(st(0xC0FFEE)), b = generate(st(0xC0FFEE));
    CHECK(a.arch == b.arch);
    for (int p = 0; p < P_COUNT; ++p) CHECK(a.base[p] == b.base[p]);
    for (int m = 0; m < MACRO_COUNT; ++m) {
        CHECK(a.map[m].story == b.map[m].story);
        for (int i = 0; i < a.map[m].n_targets; ++i)
            for (int b5 = 0; b5 < 5; ++b5)
                CHECK(a.map[m].targets[i].bp[b5] == b.map[m].targets[i].bp[b5]);
    }
}

TEST_CASE("flow terrain: archetypes reach the data (spec 7.7, fixed seeds)") {
    // Generous-margin statistical assertion over a FIXED seed set.
    //
    // SUBSTITUTION vs the task brief: the brief sampled base[P_DENSITY_A],
    // but DENSITY_A is story-owned -- its base is the DENSITY story's bp0
    // draw (the calm floor), whose span in taste.h does not depend on the
    // archetype, so the brief's drone-vs-pulse inequality could never
    // separate them (the test would be vacuous at best). P_ATTACK_A is a
    // genuinely archetype-conditioned base rule (drone .5-.95, pulse 0-.15
    // per taste.h), so it proves the same claim the brief was after: the
    // stage-0 archetype pick actually reaches the stage-3 draws.
    double sum[ARCH_COUNT] = {}; int n[ARCH_COUNT] = {};
    for (uint32_t k = 1; k <= 4000; ++k) {
        Terrain t = generate(st(k * 40503u + 7u));
        sum[t.arch] += t.base[P_ATTACK_A]; n[t.arch]++;
    }
    REQUIRE(n[ARCH_DRONE] > 100); REQUIRE(n[ARCH_PULSE] > 100);
    CHECK(sum[ARCH_DRONE]/n[ARCH_DRONE] > sum[ARCH_PULSE]/n[ARCH_PULSE] + 0.05);
}
