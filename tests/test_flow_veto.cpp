// tests/test_flow_veto.cpp
#include "doctest/doctest.h"
#include "flow/taste.h"
#include "flow/flow_params.h"
using namespace spky::flow;

namespace {
const Veto* veto_for(int param) {
    for (int i = 0; i < kVetoCount; ++i)
        if (kVetos[i].param == param) return &kVetos[i];
    return nullptr;
}
} // namespace

TEST_CASE("flow veto: no table span may leave a veto band") {
    // This is the enforcement. A veto that only held at runtime would let a
    // broken table ship silently; here a bad span is a red build.
    for (int i = 0; i < kBaseRuleCount; ++i) {
        const Veto* v = veto_for(kBaseRules[i].param);
        if (!v) continue;
        for (int a = 0; a < ARCH_COUNT; ++a) {
            CAPTURE(kParams[kBaseRules[i].param].name); CAPTURE(a);
            CHECK(kBaseRules[i].per_arch[a].lo >= v->lo);
            CHECK(kBaseRules[i].per_arch[a].hi <= v->hi);
        }
    }
    for (int s = 0; s < kStoryCount; ++s)
        for (int t = 0; t < kStories[s].n_targets; ++t) {
            const auto& c = kStories[s].targets[t];
            const Veto* v = veto_for(c.param);
            if (!v) continue;
            for (int b = 0; b < 5; ++b) {
                CAPTURE(kStories[s].name); CAPTURE(kParams[c.param].name);
                CAPTURE(b);
                CHECK(c.bp[b].lo >= v->lo);
                CHECK(c.bp[b].hi <= v->hi);
            }
        }
}

TEST_CASE("flow veto: the table is well formed and inside kParams") {
    for (int i = 0; i < kVetoCount; ++i) {
        CAPTURE(kParams[kVetos[i].param].name);
        CHECK(kVetos[i].lo < kVetos[i].hi);
        CHECK(kVetos[i].lo >= kParams[kVetos[i].param].lo);
        CHECK(kVetos[i].hi <= kParams[kVetos[i].param].hi);
    }
}
