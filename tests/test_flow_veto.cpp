// tests/test_flow_veto.cpp
#include "doctest/doctest.h"
#include "flow/taste.h"
#include "flow/flow_params.h"
#include "flow/flow.h"
#include "instrument.h"
#include <string>
using namespace spky::flow;

namespace {
// CAPTURE streams a bare const char* as a pointer, not the text -- wrap it so
// a failure's CAPTURE dump names the param instead of printing an address.
std::string pname(int p) { return kParams[p].name; }

const Veto* veto_for(int param) {
    for (int i = 0; i < kVetoCount; ++i)
        if (kVetos[i].param == param) return &kVetos[i];
    return nullptr;
}
} // namespace

TEST_CASE("flow veto: no table span may leave a veto band") {
    // This is the enforcement. A veto that only held at runtime would let a
    // broken table ship silently; here a bad span is a red build.
    //
    // `checked` counts SPANS actually compared against a veto (the
    // veto_for()-found rows), not rows scanned -- a veto table that stopped
    // matching any param would leave both loops below iterating happily and
    // asserting nothing, the same silently-empty-scan shape this branch was
    // burned by elsewhere. kVetoCount is 6 params; every one of them appears
    // in kBaseRules and/or kStories, so the floor below is non-vacuous today.
    int checked = 0;
    for (int i = 0; i < kBaseRuleCount; ++i) {
        const Veto* v = veto_for(kBaseRules[i].param);
        if (!v) continue;
        for (int a = 0; a < ARCH_COUNT; ++a) {
            CAPTURE(pname(kBaseRules[i].param)); CAPTURE(a);
            CHECK(kBaseRules[i].per_arch[a].lo >= v->lo);
            CHECK(kBaseRules[i].per_arch[a].hi <= v->hi);
            ++checked;
        }
    }
    for (int s = 0; s < kStoryCount; ++s)
        for (int t = 0; t < kStories[s].n_targets; ++t) {
            const auto& c = kStories[s].targets[t];
            const Veto* v = veto_for(c.param);
            if (!v) continue;
            for (int b = 0; b < 5; ++b) {
                CAPTURE(kStories[s].name); CAPTURE(pname(c.param));
                CAPTURE(b);
                CHECK(c.bp[b].lo >= v->lo);
                CHECK(c.bp[b].hi <= v->hi);
                ++checked;
            }
        }
    REQUIRE(checked > 0);
}

TEST_CASE("flow veto: the table is well formed and inside kParams") {
    for (int i = 0; i < kVetoCount; ++i) {
        CAPTURE(pname(kVetos[i].param));
        CHECK(kVetos[i].lo < kVetos[i].hi);
        CHECK(kVetos[i].lo >= kParams[kVetos[i].param].lo);
        CHECK(kVetos[i].hi <= kParams[kVetos[i].param].hi);
        // The runtime clamp (flow.cpp, recompute_and_push) runs AFTER
        // quantize_hyst -- it clamps the already-quantized value, not the
        // pre-quantize continuous one. A veto on a discrete param would
        // therefore push the pushed value off that param's step grid
        // whenever it clamped, which is a different (and worse) bug than a
        // veto miss. All six current entries are continuous; pin it so a
        // future discrete veto is caught here instead of surfacing as an
        // off-grid discrete value at runtime.
        CHECK(kParams[kVetos[i].param].steps == 0);
    }
}

TEST_CASE("flow veto: a macro moved mid-blend cannot breach a veto") {
    // The mechanism this guards, and the ONLY one the clamp is for:
    // flow.cpp's blend line clamps to kParams, not to the veto band. _resid
    // is frozen at press time and is nonzero only when NEW is pressed again
    // mid-flight; with a nonzero residual a macro moved during that second
    // ramp can push the sum outside even though both terrains are legal.
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

    // The clamp saturates at the band edge, so an exact float equality on
    // kVetos[v].lo/.hi is near-certain evidence the clamp actually fired
    // rather than the sweep merely staying inside the band by luck -- EXCEPT
    // for P_DRIVE, whose DIRT "heat" story holds it at a literal, degenerate
    // {0.f, 0.f} span for bp0-bp2 (taste.h): most of its curve is
    // deterministically exactly 0.0, which is also kVetos' P_DRIVE lo, with
    // no clamp involved. Verified by sabotage: disabling the re-press below
    // still gives edge hits on P_DRIVE alone (got == 0.0 from the flat
    // span), while every other veto param's bp draws are continuous within
    // their span and essentially never land exactly on a bound by chance. So
    // P_DRIVE is excluded here and the other five are the signal. If a
    // future change (begin_blend, the stagger, kBlendS, the tick rate, the
    // "% 30" re-press cadence) stops the residual from re-forming mid-ramp,
    // this count drops to 0 and the CHECK below turns the test red instead
    // of it staying green while exercising nothing.
    //
    // CORRECTED 2026-08-06 (final review): a bare edge_hits count over ALL
    // five non-DRIVE params is a weaker exercise-proof than it looks, because
    // the same coincidence excluding P_DRIVE also applies, less obviously, to
    // P_REV_MOD's lo (0.00) and P_REVMIX_A/B's hi (1.00): kParams for all
    // three of those params is a plain 0..1, so clamp_to(kParams, ...) alone
    // -- the ordinary range clamp every param gets, nothing veto-specific --
    // produces exactly those values whenever a draw or the blend saturates
    // the physical range, with the veto clamp never in the loop. A hit there
    // still proves the RESIDUAL formed (the mechanism the test exists to
    // exercise), so it is not worthless -- but only a hit on a bound that
    // sits STRICTLY INSIDE the param's own kParams range proves the VETO
    // clamp itself fired, because kParams' own clamp cannot produce that
    // value on its own.
    //
    // WHICH PARAMS ACTUALLY DO THAT, MEASURED rather than assumed from the
    // band positions alone: of the five interior bounds available (COMP_A
    // 0.40/0.60, COMP_B 0.40/0.60, REV_MOD's 0.25, REVMIX_A/B's 0.08), this
    // sweep (60 masters, the same run rechecked at 400) lands an interior hit
    // on P_COMP_A only. COMP_B, REV_MOD and REVMIX_A/B never do, at either
    // sample size -- their pre-veto values apparently never overshoot far
    // enough for THIS sweep to push them past their interior bound, though
    // their edge_hits (the loose overall counter above) are still nonzero.
    // No mechanism for the difference is claimed here -- COMP_A alone gets a
    // story curve with more range than COMP_B's near-constant base rule,
    // which is a plausible candidate, but it was not isolated. Do not tighten
    // this to require all five without re-measuring first.
    long edge_hits = 0;                          // kept: loose overall sanity
    bool interior_hit[kVetoCount] = {};           // per-param, strictly-inside bound
    bool lo_interior[kVetoCount], hi_interior[kVetoCount];
    for (int v = 0; v < kVetoCount; ++v) {
        const auto& pi = kParams[kVetos[v].param];
        lo_interior[v] = kVetos[v].lo > pi.lo + 1e-6f;
        hi_interior[v] = kVetos[v].hi < pi.hi - 1e-6f;
    }

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
                CAPTURE(pname(kVetos[v].param)); CAPTURE(got);
                CHECK(got >= kVetos[v].lo - 1e-5f);
                CHECK(got <= kVetos[v].hi + 1e-5f);
                if (kVetos[v].param != P_DRIVE &&
                    (got == kVetos[v].lo || got == kVetos[v].hi)) {
                    ++edge_hits;
                    if ((got == kVetos[v].lo && lo_interior[v]) ||
                        (got == kVetos[v].hi && hi_interior[v]))
                        interior_hit[v] = true;
                }
            }
        }
    }

    CHECK(edge_hits > 0);
    // What this actually proves, per param: a hit on a bound that sits
    // strictly inside kParams' own range cannot come from the ordinary
    // clamp_to(kParams, ...) every param already gets, so it is specific
    // evidence the veto clamp itself fired mid-blend, not just the range
    // clamp every param has anyway. Required only for P_COMP_A -- the sole
    // param this sweep measurably drives past an interior bound (see the
    // comment above edge_hits' declaration). The other four non-DRIVE params
    // stay covered by edge_hits > 0 above, which is real but weaker: it shows
    // the residual formed and the value landed on SOME veto bound, without
    // this test being able to say the veto clamp (rather than kParams' own
    // clamp) is what put it there.
    for (int v = 0; v < kVetoCount; ++v) {
        if (kVetos[v].param != P_COMP_A) continue;
        CAPTURE(pname(kVetos[v].param));
        CHECK(interior_hit[v]);
    }
}

TEST_CASE("flow veto: adventure never reaches past a veto") {
    // The wildest terrain still gets no WOBBLE above 0.25. If this ever goes
    // red, the adventure widening has been applied somewhere it must not be.
    //
    // The filter is what makes this case worth having, and also what could
    // make it worthless: P(a > 0.9) is 0.1% for any single level, so a scan
    // that forgot to count would report green having tested nothing at all --
    // the silently-empty scan that has already been caught twice on this
    // branch. The REQUIRE below pins that the sample is real.
    //
    // The filter takes the MAXIMUM over all seven of a terrain's levels (spec
    // §7, corrected 2026-08-06: one per macro domain plus one for the base
    // patch). Every veto param is reachable from both sides -- P_DRIVE and
    // P_REV_MOD are story-owned, so their bases are curve bp0 draws made under
    // a MACRO's level, while a base-rule veto param draws under the base
    // level -- and filtering on the base level alone would leave the storied
    // ones tested only at whatever nerve they happened to have. Seven chances
    // at 0.1% each also gives ~140 qualifying terrains instead of ~20.
    int high = 0;
    for (uint32_t master = 1; master <= 20000; ++master) {
        TerrainState st; st.master = master;
        const Terrain t = generate(st);
        float a = t.adventure_base;
        for (int m = 0; m < MACRO_COUNT; ++m)
            if (t.adventure[m] > a) a = t.adventure[m];
        if (a < 0.9f) continue;
        ++high;
        for (int v = 0; v < kVetoCount; ++v) {
            CAPTURE(master); CAPTURE(pname(kVetos[v].param));
            CHECK(t.base[kVetos[v].param] >= kVetos[v].lo - 1e-5f);
            CHECK(t.base[kVetos[v].param] <= kVetos[v].hi + 1e-5f);
        }
    }
    CAPTURE(high);
    // WHAT `high` COUNTS, said plainly here because the number is easy to
    // read as stronger than it is (stated 2026-08-06, Task 7): it is the
    // number of TERRAINS in which at least one of the seven levels reached
    // 0.9, measured at 128 of these 20 000 masters. It is NOT 128 terrains per
    // veto'd param, and it is not 128 chances at any one of them. The filter
    // is a union over seven levels, and which level did the qualifying is
    // spread across all seven -- measured 13 base-patch and 9 to 25 per macro
    // domain, about 18 apiece. So a veto param whose base is drawn under ONE
    // particular level is exercised at high adventure roughly 18 times here,
    // not 128. The floor is set well under the total so seed-set jitter cannot
    // trip it while a filter that stopped matching does.
    REQUIRE(high >= 40);
}
