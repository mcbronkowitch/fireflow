// tests/test_flow_terrain.cpp
#include "doctest/doctest.h"
#include "flow/terrain.h"
#include "flow/taste.h"
#include "mod/divisions.h"
#include <cstring>
using namespace spky;                  // kDivisions / division_index
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
        // The no-double-density constraint, stated the way apply_constraints
        // actually enforces it: min(base A, base B) <= 0.5.
        //
        // NOT "never both strictly above 0.5", which is what this line used
        // to say. The clamp pulls the LOWER deck down to EXACTLY 0.5f, so
        // `A > .5 && B > .5` is false by construction under every possible
        // taste table -- it could not fail, and the comment that claimed it
        // was a tripwire on the table was simply wrong. Measured: lifting
        // both DENSITY bp0 spans to {.85,.95} left that version green.
        //
        // This version does fail, and here is exactly when. With today's
        // spans both DENSITY bases are the DENSITY "rate" story's bp0 draw
        // ({.02,.08} and {.02,.08}; an unpicked variant's targets still take
        // their own bp0 as a base, so there is no other path), so the
        // measured max of min(A,B) over these 10 000 seeds is 0.0798 and the
        // stock table clears the bound with or without the clamp. Remove the
        // clamp under a table that CAN reach two hot decks and it goes red at
        // once: with those {.85,.95} spans, max min(A,B) is 0.5000 exactly
        // while the clamp holds and 0.9496 with it gone -- all 10 000 seeds
        // failing. So this asserts the guard's postcondition, and nothing
        // about how likely today's tables are to need it.
        const float lower = t.base[P_DENSITY_A] < t.base[P_DENSITY_B]
                          ? t.base[P_DENSITY_A] : t.base[P_DENSITY_B];
        CHECK(lower <= 0.5f);
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

TEST_CASE("flow terrain: a drone's picked DENSITY 'rate' story carries its "
          "window into the Terrain") {
    // The archetype window (spec 2026-08-06 §4) lives in taste.h's static
    // kStories table (StoryVariant::arch_window); every test_flow_taste.cpp
    // case only reads THAT table. This proves the window actually gets
    // COPIED into Terrain::window at generate() time (terrain.cpp stage 4's
    // `if (picked) { ... t.window[m] = sv.arch_window[t.arch]; }`), not just
    // tabulated -- a regression that stopped doing the copy would leave
    // kStories correct and every taste test green while the terrain the
    // runtime actually uses silently reverted to the unwindowed {0,1}.
    //
    // Checked by EQUALITY against the table entry, not a loose "hi <= 0.5"
    // bound: Terrain::window has no default member initialiser, so an
    // uncopied window zero-inits to {0,0} along with the rest of the
    // struct -- and 0.0 <= 0.5 passes a loose bound just as well as the real
    // 0.45 does, so that version of this test cannot actually go red when
    // the copy is missing (measured: proving this test RED by deleting the
    // copy left it green under the loose bound). Equality against the
    // source table catches both a missing copy (0 != .45) and any future
    // drift between the two.
    bool found = false;
    for (uint32_t master = 1; master <= 20000 && !found; ++master) {
        Terrain t = generate(st(master));
        if (t.arch != ARCH_DRONE) continue;
        const int story = t.map[M_DENSITY].story;
        if (std::strcmp(kStories[story].name, "rate") != 0) continue;
        found = true;
        CAPTURE(master);
        const Span& want = kStories[story].arch_window[ARCH_DRONE];
        CHECK(t.window[M_DENSITY].lo == want.lo);
        CHECK(t.window[M_DENSITY].hi == want.hi);
        CHECK(t.window[M_DENSITY].hi <= 0.5f);   // the rule itself, spot-checked
    }
    // Not "assume one exists" -- if 20 000 masters never draw a drone that
    // picks "rate" (drone is the heaviest weight, kArchWeight, and DENSITY
    // has only two variants, so this should be common), that is itself a
    // finding worth reporting, not something to route around.
    REQUIRE(found);
}

TEST_CASE("flow terrain: synced rates prefer the straight rungs") {
    // divisions.h's ladder is speed-sorted, so dotted and triplet rungs sit
    // BETWEEN the straight ones -- a uniform draw hits them roughly half the
    // time in the middle of the range. They stay reachable (this is a weight,
    // not a veto), they just get rare.
    auto crooked = [](int idx) {
        const char* n = kDivisions[idx].name;
        for (const char* c = n; *c; ++c) if (*c == '.' || *c == 'T') return true;
        return false;
    };
    int total = 0, odd = 0;
    for (uint32_t master = 1; master <= 4000; ++master) {
        TerrainState st; st.master = master;
        const Terrain t = generate(st);
        if (t.base[P_MODE] < 0.5f) continue;            // free mode has no ladder
        for (int p : { P_RATE_A, P_RATE_B }) {
            ++total;
            if (crooked(division_index(t.base[p]))) ++odd;
        }
    }
    REQUIRE(total > 500);
    const float share = float(odd) / float(total);
    CAPTURE(share);
    CHECK(share < 0.20f);      // was roughly 0.5 with a uniform draw
    CHECK(share > 0.01f);      // still reachable -- a weight, not a veto
}

TEST_CASE("flow terrain: step counts prefer 8 and 16") {
    int total = 0, preferred = 0;
    for (uint32_t master = 1; master <= 4000; ++master) {
        TerrainState st; st.master = master;
        const Terrain t = generate(st);
        const int s = int(t.base[P_STEPS_B] + 0.5f);
        ++total;
        if (s == 8 || s == 16) ++preferred;
    }
    const float share = float(preferred) / float(total);
    CAPTURE(share);
    CHECK(share > 0.45f);
    CHECK(share < 0.95f);      // other counts still happen
}

TEST_CASE("flow terrain: SHUFFLE leans to the low end of its span") {
    // ADDED beyond the task brief, which shipped kShuffleSkew with no
    // assertion at all. SHUFFLE has no rungs to weight, so its bias is a skew
    // inside the drawn span (taste.h) -- measured here as the mean position
    // INSIDE that span, which is what the skew actually moves. u^kShuffleSkew
    // with the table's 2.5 predicts 1/3.5 = 0.286 against a uniform draw's
    // 0.5. The lower bound is the half that matters: it is a skew, not a
    // narrowing, so heavy shuffle must stay reachable.
    const BaseRule* shuffle = nullptr;
    for (int i = 0; i < kBaseRuleCount; ++i)
        if (kBaseRules[i].param == P_SHUFFLE) shuffle = &kBaseRules[i];
    REQUIRE(shuffle != nullptr);

    double sum = 0.0; int n = 0; float top = 0.f;
    for (uint32_t master = 1; master <= 4000; ++master) {
        TerrainState s; s.master = master;
        const Terrain t = generate(s);
        const Span& sp = shuffle->per_arch[t.arch];
        const float pos = (t.base[P_SHUFFLE] - sp.lo) / (sp.hi - sp.lo);
        sum += pos; ++n;
        if (pos > top) top = pos;
    }
    REQUIRE(n == 4000);
    const float mean = float(sum / n);
    CAPTURE(mean);
    CHECK(mean < 0.36f);       // was 0.5 with a uniform draw
    CHECK(mean > 0.20f);       // a skew, not a collapse onto lo
    CAPTURE(top);
    CHECK(top > 0.95f);        // the heavy-shuffle end stays reachable
}
