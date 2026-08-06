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

#include "flow/flow.h"
#include "instrument.h"

TEST_CASE("flow veto: a macro moved mid-blend cannot breach a veto") {
    // The mechanism this guards, and the ONLY one the clamp is for:
    // flow.cpp's blend line clamps to kParams, not to the veto band, and its
    // own comment says the sum can exceed a param's range even when both
    // terrains are inside it. _resid is frozen at press time while prv[]
    // re-evaluates live, so moving a knob DURING the ramp is what breaks it.
    //
    // A single press with a smooth macro sweep provably CANNOT reach this:
    // _resid is exactly zero on a fresh press from a settled terrain (cont_now
    // and cand_cur are the same value, same tick), so the combined value is a
    // plain convex combination of prv[p] and cur[p], both of which task 1's
    // build-time test already proves sit inside the veto band -- a convex
    // combination of two in-band points cannot leave the band. Checked
    // empirically too: 300 masters swept smoothly across a full 6 s blend,
    // zero breaches. _resid only goes nonzero on a RE-press mid-flight
    // (begin_blend's own doc comment: "a re-press lands mid-flight"), which
    // is exactly the real gesture that must not be able to break a veto --
    // mashing NEW while sweeping the macros fast. So this test presses NEW
    // repeatedly through the ramp while every macro square-waves across its
    // full range every 3 ticks (30 ms), the fastest a knob or CV lane can
    // plausibly move -- a static or single-press macro grid would never reach
    // this, and did not (see above).
    spky::Instrument inst;
    inst.init(48000.f);
    Flow f;
    f.init(&inst, 100.f);

    for (uint32_t master = 1; master <= 60; ++master) {
        TerrainState st; st.master = master;
        f.wake(st);
        for (int m = 0; m < MACRO_COUNT; ++m) f.set_macro(m, 0.5f);
        for (int i = 0; i < 40; ++i) f.tick();

        REQUIRE(f.new_full());              // start a blend

        // Sweep every macro across its full travel while the ramp runs, and
        // mash NEW every 30 ticks (0.3 s) so a genuine mid-flight residual
        // keeps re-forming through the whole window.
        const int ticks = int(kBlendS * 100.f) + 20;
        for (int i = 0; i < ticks; ++i) {
            const bool phase = (i / 3) % 2 == 0;
            for (int m = 0; m < MACRO_COUNT; ++m)
                f.set_macro(m, (m % 2) ? (phase ? 1.f : 0.f) : (phase ? 0.f : 1.f));
            if (i > 0 && i % 30 == 0) f.new_full();
            f.tick();
            for (int v = 0; v < kVetoCount; ++v) {
                const float got = f.param_now(kVetos[v].param);
                CAPTURE(master); CAPTURE(i);
                CAPTURE(kParams[kVetos[v].param].name); CAPTURE(got);
                CHECK(got >= kVetos[v].lo - 1e-5f);
                CHECK(got <= kVetos[v].hi + 1e-5f);
            }
        }
    }
}
