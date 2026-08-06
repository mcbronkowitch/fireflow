// tests/test_flow_terrain.cpp
#include "doctest/doctest.h"
#include "flow/terrain.h"
#include "flow/taste.h"
#include "mod/divisions.h"
#include <algorithm>
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
    // MARGIN, if you are reading this because it went red: measured 0.189
    // against 0.20, only 5.5 % of headroom, and that is by construction rather
    // than by luck. The crooked rungs cluster in the middle of the ladder,
    // which is exactly where arp's P_RATE span {.55,.9} and pulse's {.3,.6}
    // sit -- alone they contribute 0.259 and 0.216. Re-weighting the archetype
    // mix toward arp, or widening those spans, trips this bound without
    // anything being wrong. Re-derive from the taste tables before touching it.
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
    //
    // Reachability is asserted as a RATE, not as a maximum. An earlier version
    // checked max(pos) > 0.95 over these 4000 masters, which cannot fail:
    // P(max <= 0.95) = (0.95^(1/s))^4000, about e^-82 at s = 2.5, and it stays
    // negligible until s is in the low thousands -- a skew of 40, which
    // collapses the mean to 0.02, still hits 0.95 easily. Counting the draws
    // above 0.8 instead goes red as soon as the top of the span thins out.
    const BaseRule* shuffle = nullptr;
    for (int i = 0; i < kBaseRuleCount; ++i)
        if (kBaseRules[i].param == P_SHUFFLE) shuffle = &kBaseRules[i];
    REQUIRE(shuffle != nullptr);

    double sum = 0.0; int n = 0, heavy = 0;
    for (uint32_t master = 1; master <= 4000; ++master) {
        TerrainState s; s.master = master;
        const Terrain t = generate(s);
        const Span& sp = shuffle->per_arch[t.arch];
        const float pos = (t.base[P_SHUFFLE] - sp.lo) / (sp.hi - sp.lo);
        sum += pos; ++n;
        if (pos > 0.8f) ++heavy;
    }
    REQUIRE(n == 4000);
    const float mean = float(sum / n);
    CAPTURE(mean);
    CHECK(mean < 0.36f);       // was 0.5 with a uniform draw
    CHECK(mean > 0.20f);       // a skew, not a collapse onto lo
    CAPTURE(heavy);
    // The heavy-shuffle end stays reachable: 1 - 0.8^(1/2.5) predicts about
    // 9 % of 4000, i.e. ~360, and 346 is measured. The floor sits well under
    // that so ordinary seed-set jitter cannot trip it, but a skew that thins
    // the top of the span does -- measured 60 at s = 12, 18 at s = 40, where
    // the discarded max-based version still passed (measured at s = 40; s = 12
    // is the weaker skew, so its max is higher still).
    CHECK(heavy > 100);
}

TEST_CASE("flow terrain: adventure is rare, per domain, and rerolls with it") {
    // a = 1 - u^(1/3), so P(a > x) = (1-x)^3: above 0.5 in 12.5% of draws and
    // above 0.8 in 0.8%. Brave terrain is the rule, outliers the exception.
    //
    // A terrain carries SEVEN of these (spec §7, corrected 2026-08-06): one per
    // macro domain keyed on that macro's own reroll counter, plus one for the
    // base patch keyed on the master alone. Both are measured here, because a
    // per-domain level that was never drawn -- left at zero, or copied from the
    // base level -- would satisfy the range checks and the isolation cases
    // below without being a draw at all.
    int over_half = 0, over_eighty = 0;
    int over_half_bright = 0, over_eighty_bright = 0;
    const int n = 20000;
    for (uint32_t master = 1; master <= uint32_t(n); ++master) {
        TerrainState st; st.master = master;
        const Terrain t = generate(st);
        CHECK(t.adventure_base >= 0.f);
        CHECK(t.adventure_base <= 1.f);
        for (int m = 0; m < MACRO_COUNT; ++m) {
            CHECK(t.adventure[m] >= 0.f);
            CHECK(t.adventure[m] <= 1.f);
        }
        if (t.adventure_base > 0.5f) ++over_half;
        if (t.adventure_base > 0.8f) ++over_eighty;
        if (t.adventure[M_BRIGHT] > 0.5f) ++over_half_bright;
        if (t.adventure[M_BRIGHT] > 0.8f) ++over_eighty_bright;
    }
    const float p50 = float(over_half) / float(n);
    const float p80 = float(over_eighty) / float(n);
    CAPTURE(p50); CAPTURE(p80);
    CHECK(p50 > 0.105f); CHECK(p50 < 0.145f);      // 0.125 expected
    CHECK(p80 > 0.004f); CHECK(p80 < 0.014f);      // 0.008 expected
    // The same shape from a macro domain's own stream. A level that was never
    // drawn gives 0 here and fails the lower bounds.
    const float p50b = float(over_half_bright) / float(n);
    const float p80b = float(over_eighty_bright) / float(n);
    CAPTURE(p50b); CAPTURE(p80b);
    CHECK(p50b > 0.105f); CHECK(p50b < 0.145f);
    CHECK(p80b > 0.004f); CHECK(p80b < 0.014f);

    // The reroll rule, which is the whole reason these are per domain.
    //
    // §7's intent: rerolling a domain redraws THAT domain's nerve, so a wild
    // DENSITY does not stay wild when the player asks for a new DENSITY.
    // Spec 7.3's isolation: it redraws nothing else -- not another macro's
    // nerve, and not the base patch's, which is keyed on the master alone and
    // so cannot move under any counter at all.
    //
    // Both halves are load-bearing and each fails a different wrong design:
    // one per-terrain level keyed on the counter sum (what shipped first) fails
    // the two "unchanged" lines, and a level keyed on nothing -- or dropped
    // entirely -- fails the "changed" line.
    TerrainState st; st.master = 7;
    const Terrain before = generate(st);
    st.reroll[M_DENSITY] = 1;
    const Terrain after = generate(st);
    CHECK(after.adventure[M_DENSITY] != before.adventure[M_DENSITY]);
    CHECK(after.adventure[M_BRIGHT] == before.adventure[M_BRIGHT]);
    CHECK(after.adventure_base     == before.adventure_base);
}

TEST_CASE("flow terrain: at full adventure a span is drawn in full") {
    // a = 1 is the no-op: the whole span, which is how the tables read on
    // their own. Anything less narrows toward the middle.
    Rng r; r.seed(99);
    const Span s{ 0.f, 1.f };
    float lo = 1.f, hi = 0.f;
    for (int i = 0; i < 5000; ++i) {
        const float v = draw_span(r, s, 1.f);
        lo = std::min(lo, v); hi = std::max(hi, v);
    }
    CHECK(lo < 0.02f);
    CHECK(hi > 0.98f);

    float lo0 = 1.f, hi0 = 0.f;
    for (int i = 0; i < 5000; ++i) {
        const float v = draw_span(r, s, 0.f);
        lo0 = std::min(lo0, v); hi0 = std::max(hi0, v);
    }
    CHECK(lo0 > 0.29f);        // middle 40% of 0..1 is 0.30..0.70
    CHECK(hi0 < 0.71f);
    // ADDED beyond the task brief, which asserted only the two bounds above.
    // Those are one-sided: they catch a calm draw that is too WIDE, but a
    // kAdventureNarrow of 0.20 -- or 0.001, or a draw collapsed onto the
    // centre entirely -- satisfies them just as well, so on their own they
    // cannot tell "narrowed to the middle 40%" from "narrowed to nothing".
    // Pinning the other side makes the pair say what kAdventureNarrow is
    // rather than only what it is under. 5000 draws leave the outermost 0.4%
    // of a 0.30..0.70 range unreached with probability (1-0.01)^5000, about
    // e^-50, so the slack here is float noise, not sampling luck.
    CHECK(lo0 < 0.31f);
    CHECK(hi0 > 0.69f);
}
