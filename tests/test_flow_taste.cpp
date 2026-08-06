// tests/test_flow_taste.cpp
#include "doctest/doctest.h"
#include "flow/taste.h"
#include "flow/flow_params.h"
using namespace spky::flow;

TEST_CASE("flow taste: static data is internally consistent") {
    // Base rules stay inside the param table's legal range.
    for (int i = 0; i < kBaseRuleCount; ++i)
        for (int a = 0; a < ARCH_COUNT; ++a) {
            const auto& r = kBaseRules[i];
            REQUIRE(r.param >= 0);
            REQUIRE(r.param < P_COUNT);
            CAPTURE(kParams[r.param].name);
            CHECK(r.per_arch[a].lo >= kParams[r.param].lo);
            CHECK(r.per_arch[a].hi <= kParams[r.param].hi);
            CHECK(r.per_arch[a].lo <= r.per_arch[a].hi);
        }
    // Every macro has at least one story; DENSITY has two.
    int per_macro[MACRO_COUNT] = {};
    for (int s = 0; s < kStoryCount; ++s) per_macro[kStories[s].macro]++;
    for (int m = 0; m < MACRO_COUNT; ++m) CHECK(per_macro[m] >= 1);
    CHECK(per_macro[M_DENSITY] == 2);
    // Story breakpoint spans: inside the param range, and monotone in the
    // direction bp0 -> bp4 (lo bounds non-decreasing or non-increasing).
    for (int s = 0; s < kStoryCount; ++s)
        for (int t = 0; t < kStories[s].n_targets; ++t) {
            const auto& c = kStories[s].targets[t];
            REQUIRE(c.param >= 0);
            REQUIRE(c.param < P_COUNT);
            CAPTURE(kParams[c.param].name);
            bool up = c.bp[4].lo >= c.bp[0].lo;
            for (int b = 0; b < 5; ++b) {
                CHECK(c.bp[b].lo >= kParams[c.param].lo);
                CHECK(c.bp[b].hi <= kParams[c.param].hi);
                CHECK(c.bp[b].lo <= c.bp[b].hi);
                if (b > 0) {
                    if (up) CHECK(c.bp[b].lo >= c.bp[b-1].lo);
                    else    CHECK(c.bp[b].lo <= c.bp[b-1].lo);
                }
            }
        }
    // MOTION's and DIRT's Q4 cells may exceed centers (risk zone) but the
    // hard param limit still caps them: already covered by the range check.
    // Archetype weights: drone is the heaviest.
    for (int a = 1; a < ARCH_COUNT; ++a)
        CHECK(kArchWeight[ARCH_DRONE] >= kArchWeight[a]);

    // No param may appear in kBaseRules AND any story: story targets take
    // their base from the story's own bp values, so a base-rule row for a
    // storied param would fight the story generator.
    for (int i = 0; i < kBaseRuleCount; ++i)
        for (int s = 0; s < kStoryCount; ++s)
            for (int t = 0; t < kStories[s].n_targets; ++t)
                CHECK(kBaseRules[i].param != kStories[s].targets[t].param);

    // Coverage in the other direction: every ParamId that no story owns
    // must have a base rule, or the generator would have nothing to draw
    // from for it. (This is the check that goes red when a row is missing.)
    bool storied[P_COUNT] = {};
    for (int s = 0; s < kStoryCount; ++s)
        for (int t = 0; t < kStories[s].n_targets; ++t)
            storied[kStories[s].targets[t].param] = true;
    bool based[P_COUNT] = {};
    for (int i = 0; i < kBaseRuleCount; ++i)
        based[kBaseRules[i].param] = true;
    for (int p = 0; p < P_COUNT; ++p) {
        CAPTURE(kParams[p].name);
        CHECK((storied[p] || based[p]));
    }
}

TEST_CASE("flow taste: drones get round LFOs only") {
    // waveforms.h shape_value morphs sine(0) -> triangle(.25) -> ramp(.5) ->
    // pulse(.75) -> S&H(1). From the ramp up the lane emits a discontinuity
    // per cycle, and that is what makes a drone read as rhythmic. So a drone
    // may only draw the sine..triangle quarter. This is mechanical, not taste.
    const int shape[2] = { P_SHAPE_A, P_SHAPE_B };
    for (int i = 0; i < kBaseRuleCount; ++i)
        for (int k = 0; k < 2; ++k)
            if (kBaseRules[i].param == shape[k]) {
                CAPTURE(kParams[shape[k]].name);
                CHECK(kBaseRules[i].per_arch[ARCH_DRONE].hi <= 0.25f);
                // The other archetypes stay wildcards: nothing collected says
                // an arp may not have an angular LFO.
                CHECK(kBaseRules[i].per_arch[ARCH_ARP].hi > 0.25f);
            }
}
