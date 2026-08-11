// tests/test_flow_overlay.cpp
//
// The base overlay (spec 2026-08-11 flow-patch-transfer §4). Two claims, and
// the second one is the whole design: an overlay reaches every kBaseRules
// parameter, and reaches NO story-owned parameter.
#include "doctest/doctest.h"
#include "flow/terrain.h"
#include "flow/taste.h"

using namespace spky::flow;

TEST_CASE("is_base_rule agrees with the kBaseRules table") {
    // Derived from the table on both sides on purpose -- but the SUBJECT is
    // the function and the EXPECTATION is the raw table, so a function that
    // stopped reading the table would fail here.
    bool in_table[P_COUNT] = {};
    for (int i = 0; i < kBaseRuleCount; ++i) in_table[kBaseRules[i].param] = true;
    for (int p = 0; p < P_COUNT; ++p) CHECK(is_base_rule(p) == in_table[p]);

    int n = 0;
    for (int p = 0; p < P_COUNT; ++p) if (is_base_rule(p)) ++n;
    CHECK(n == kBaseRuleCount);
    // Pins the two facts the plan's Background section states. If taste.h
    // legitimately grows a base rule, update BOTH numbers together and say so
    // in the commit -- do not delete the assertion.
    CHECK(kBaseRuleCount == 38);
    CHECK(is_base_rule(P_COMP_B));
    CHECK_FALSE(is_base_rule(P_COMP_A));
}

TEST_CASE("an overlay reaches every base-rule parameter") {
    TerrainState st; st.master = 0x51A7E1u;
    const Terrain plain = generate(st, nullptr);

    BaseOverlay ov;
    // A value that differs from the drawn one for every row: take the opposite
    // end of each parameter's own range.
    for (int i = 0; i < kBaseRuleCount; ++i) {
        const int p = kBaseRules[i].param;
        if (p == P_ENGINE_A || p == P_ENGINE_B) {
            // kParams[P_ENGINE_*] is 0..5, and 0 is ENGINE_TEST_TONE while 5 is
            // ENGINE_BBD -- neither carries. Task 2 rejects a carrier-less pair
            // whole, so the range flip the other rows use would make this case
            // assert against a rejected overlay. Pick a carrier that differs
            // from the drawn value instead; the claim is unchanged.
            const int drawn = int(plain.base[p] + 0.5f);
            for (int k = 0; k < 3; ++k)
                if (kCarrierEngine[k] != drawn) { ov.v[p] = float(kCarrierEngine[k]); break; }
        } else {
            const float mid = 0.5f * (kParams[p].lo + kParams[p].hi);
            ov.v[p] = plain.base[p] < mid ? kParams[p].hi : kParams[p].lo;
        }
        ov.has[p] = true;
    }
    const Terrain over = generate(st, &ov);

    for (int i = 0; i < kBaseRuleCount; ++i) {
        const int p = kBaseRules[i].param;
        // P_ENGINE_A/B and the constrained rows may be moved again by
        // apply_constraints(); assert the overlay MOVED the value rather than
        // that it landed exactly, so this gate does not fight §4.2's last word.
        CHECK(over.base[p] != doctest::Approx(plain.base[p]));
    }
}

TEST_CASE("an overlay reaches no story-owned parameter") {
    TerrainState st; st.master = 0x7A11E5u;
    const Terrain plain = generate(st, nullptr);

    BaseOverlay ov;
    for (int p = 0; p < P_COUNT; ++p) {
        if (is_base_rule(p)) continue;
        ov.v[p]   = kParams[p].hi;   // as far from any drawn floor as the range allows
        ov.has[p] = true;
    }
    const Terrain over = generate(st, &ov);

    for (int p = 0; p < P_COUNT; ++p)
        CHECK(over.base[p] == doctest::Approx(plain.base[p]));
}

TEST_CASE("a null overlay leaves generate unchanged") {
    TerrainState st; st.master = 0xBEEF01u;
    const Terrain a = generate(st);
    const Terrain b = generate(st, nullptr);
    for (int p = 0; p < P_COUNT; ++p) CHECK(a.base[p] == doctest::Approx(b.base[p]));
    CHECK(a.arch == b.arch);
    CHECK(a.a_carries == b.a_carries);
}

TEST_CASE("an out-of-range overlay value is clamped on the way in") {
    TerrainState st; st.master = 0xC1A11Fu & 0xFFFFFFu;   // any master
    BaseOverlay ov;
    ov.v[P_TUNE_A] = 40.f;      // P_TUNE_A is 0..1
    ov.has[P_TUNE_A] = true;
    const Terrain t = generate(st, &ov);
    CHECK(t.base[P_TUNE_A] <= kParams[P_TUNE_A].hi);
    CHECK(t.base[P_TUNE_A] >= kParams[P_TUNE_A].lo);
}
