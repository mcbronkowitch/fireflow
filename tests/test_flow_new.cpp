// tests/test_flow_new.cpp
//
// The NEW gesture family (spec §5): full NEW, partial reroll, the single
// undo slot and the terrain lock -- plus the blend they all share (a
// kBlendS ramp between two live terrains, discrete switches staggered
// texture-then-carrier under a duck).
#include "doctest/doctest.h"
#include "flow/flow.h"
#include "flow/taste.h"
#include <algorithm>
#include <cmath>
using namespace spky;
using namespace spky::flow;

TEST_CASE("flow NEW: partial reroll touches only its domain (spec 7.3)") {
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = 0xFEED; f.wake(s);
    Terrain before = terrain_of(f);
    f.new_partial(1u << M_BRIGHT);
    // run the blend to completion
    for (int i = 0; i < 1000; ++i) f.tick();
    Terrain after = terrain_of(f);
    // BRIGHT's own targets may move; every parameter outside the BRIGHT
    // domain is identical (proves per-stream isolation, spec 7.3).
    bool in_domain[P_COUNT] = {};
    for (int t = 0; t < after.map[M_BRIGHT].n_targets; ++t)
        in_domain[after.map[M_BRIGHT].targets[t].param] = true;
    for (int t = 0; t < before.map[M_BRIGHT].n_targets; ++t)
        in_domain[before.map[M_BRIGHT].targets[t].param] = true;
    for (int p = 0; p < P_COUNT; ++p)
        if (!in_domain[p]) { CAPTURE(kParams[p].name);
                             CHECK(after.base[p] == before.base[p]); }
    CHECK(f.state().reroll[M_BRIGHT] == 1);
    CHECK(f.state().reroll[M_MOTION] == 0);
}

TEST_CASE("flow NEW: undo returns, lock refuses") {
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = 0xFACE; f.wake(s);
    uint32_t m0 = f.state().master;
    f.new_full(); for (int i = 0; i < 1000; ++i) f.tick();
    CHECK(f.state().master != m0);
    f.undo();     for (int i = 0; i < 1000; ++i) f.tick();
    CHECK(f.state().master == m0);
    f.set_lock(true);
    f.new_full(); f.tick();
    CHECK(f.state().master == m0);               // refused
}

TEST_CASE("flow NEW: re-press retargets from the interpolated state") {
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = 0xD1CE; f.wake(s);
    // Settled start values, for the travel check at the end.
    float start[P_COUNT];
    for (int p = 0; p < P_COUNT; ++p) start[p] = f.param_now(p);
    f.new_full();
    for (int i = 0; i < 50; ++i) f.tick();       // mid-blend
    float mid = f.param_now(P_REV_SIZE);
    float before[P_COUNT];
    for (int p = 0; p < P_COUNT; ++p) before[p] = f.param_now(p);
    f.new_full();                                 // retarget mid-flight
    f.tick();
    float step1 = f.param_now(P_REV_SIZE);
    CHECK(std::fabs(step1 - mid) < 0.05f);        // no jump at retarget
    CHECK(f.blend_phase() < 1.f);

    // The two lines above are weak on their own: P_REV_SIZE rides the
    // kSpaceSlewS one-pole, whose coefficient at 100 Hz is 0.004 -- it
    // cannot move more than 0.4 % of its remaining travel in a single tick
    // no matter where the ramp restarts, so the 0.05 tolerance passes even
    // for an implementation that snaps the origin to the OUTGOING terrain's
    // settled value. The property the test is named for lives on the
    // unslewed params, so scan them all.
    //   Excluded: the discretes (they step by design), SIZE/DECAY (slewed),
    //   and REVMIX_A/B (the duck deliberately steps that deck's send toward
    //   wet at the instant of its discrete switch -- see the duck note in
    //   flow.cpp).
    auto scanned = [](int p) {
        return kParams[p].steps == 0 && p != P_REV_SIZE && p != P_REV_DECAY
            && p != P_REVMIX_A && p != P_REVMIX_B;
    };
    int worst_p = 0; float worst = 0.f;
    for (int p = 0; p < P_COUNT; ++p) {
        if (!scanned(p)) continue;
        const float span = kParams[p].hi - kParams[p].lo;
        const float d = std::fabs(f.param_now(p) - before[p]) / span;
        if (d > worst) { worst = d; worst_p = p; }
    }
    CAPTURE(kParams[worst_p].name); CAPTURE(worst);
    CHECK(worst < 0.02f);

    // ...and that check is not vacuous: the first blend still had real
    // travel left when the second press landed. 50 ticks is 1/12 of the
    // kBlendS ramp, so a param already 1 % of its span from its start is
    // heading for ~12 % -- an implementation that restarted from the
    // outgoing terrain's settled value would jump ~11 %, five times the
    // tolerance above.
    float travel = 0.f;
    for (int p = 0; p < P_COUNT; ++p) {
        if (!scanned(p)) continue;
        const float span = kParams[p].hi - kParams[p].lo;
        travel = std::max(travel, std::fabs(before[p] - start[p]) / span);
    }
    CAPTURE(travel);
    REQUIRE(travel > 0.01f);
}

TEST_CASE("flow NEW: discrete decks switch staggered, under a duck") {
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    // Seed picked so BOTH decks change engine across the press -- otherwise
    // "deck X has not switched yet" is unobservable and the stagger checks
    // would pass vacuously. The REQUIREs below hold the seed to that.
    TerrainState s; s.master = 0xC0FFEE; f.wake(s);
    const int eng_p[2] = { P_ENGINE_A, P_ENGINE_B };
    const int mix_p[2] = { P_REVMIX_A, P_REVMIX_B };
    const float eng0[2] = { f.param_now(P_ENGINE_A), f.param_now(P_ENGINE_B) };
    const float mix0[2] = { f.param_now(P_REVMIX_A), f.param_now(P_REVMIX_B) };

    f.new_full();
    // The stagger is scheduled off the INCOMING terrain's roles.
    const Terrain& nt = terrain_of(f);
    const int carrier = nt.a_carries ? 0 : 1;
    const int texture = 1 - carrier;
    const float eng1[2] = { nt.base[P_ENGINE_A], nt.base[P_ENGINE_B] };
    REQUIRE(eng1[texture] != eng0[texture]);
    REQUIRE(eng1[carrier] != eng0[carrier]);

    f.tick();                                    // phase 1/600
    CHECK(f.param_now(eng_p[texture]) == doctest::Approx(eng1[texture]));
    CHECK(f.param_now(eng_p[carrier]) == doctest::Approx(eng0[carrier]));
    // The texture deck switched under a duck: its send is above what the
    // macros alone asked for, and never below it.
    CHECK(f.param_now(mix_p[texture]) > mix0[texture] + 1e-3f);

    // t = 1.00 s, phase 100/600 = 0.167. The carrier's switch is at t = 1.5 s
    // and its duck window opens at 1.25 s, so this sample is duck-free.
    for (int i = 0; i < 99; ++i) f.tick();
    const float carrier_mix_pre = f.param_now(mix_p[carrier]);

    for (int i = 0; i < 48; ++i) f.tick();       // t = 1.48, phase 0.2467
    CHECK(f.blend_phase() < kCarrierStaggerFrac);
    CHECK(f.param_now(eng_p[carrier]) == doctest::Approx(eng0[carrier]));

    for (int i = 0; i < 4; ++i) f.tick();        // t = 1.52, phase 0.2533
    CHECK(f.blend_phase() > kCarrierStaggerFrac);
    CHECK(f.param_now(eng_p[carrier]) == doctest::Approx(eng1[carrier]));
    // The carrier's own duck peaks at its switch, a quarter of the ramp in.
    CHECK(f.param_now(mix_p[carrier]) > carrier_mix_pre + 1e-3f);

    for (int i = 0; i < 1000; ++i) f.tick();
    CHECK(f.blend_phase() == doctest::Approx(1.f));
}

TEST_CASE("flow NEW: a finished blend lands where a wake on the target would") {
    // The end state must not depend on the path: once the ramp, the ducks
    // and the SPACE slew have all settled, every pushed value has to equal
    // what a cold wake on the same TerrainState produces at the same point
    // on the weather clock. This is what catches a blend that leaves a
    // residue behind -- a stuck duck, an un-decayed continuity offset, or a
    // discrete parked one hysteresis step off.
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = 0x77AA; f.wake(s);
    f.set_macro(M_BRIGHT, 0.6f); f.set_macro(M_SPACE, 0.4f);
    f.set_macro(M_MOTION, 0.5f); f.set_macro(M_WANDER, 0.8f);
    f.new_full();
    const TerrainState target = f.state();
    for (int i = 0; i < 5000; ++i) f.tick();     // 50 s: ramp + slew + duck

    Instrument in2; in2.init(48000.f);
    Flow g; g.init(&in2, 100.f);
    g.wake(target);                              // same wall clock (t = 0)
    g.set_macro(M_BRIGHT, 0.6f); g.set_macro(M_SPACE, 0.4f);
    g.set_macro(M_MOTION, 0.5f); g.set_macro(M_WANDER, 0.8f);
    for (int i = 0; i < 5000; ++i) g.tick();

    REQUIRE(f.now_s() == doctest::Approx(g.now_s()));
    for (int p = 0; p < P_COUNT; ++p) {
        CAPTURE(kParams[p].name);
        CHECK(f.param_now(p)
              == doctest::Approx(g.param_now(p)).epsilon(0.0005));
    }
}

TEST_CASE("flow NEW: undo is one slot and undo-after-undo redoes") {
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = 0x1234; f.wake(s);
    f.undo();                                     // no accepted press yet
    CHECK(f.state().master == 0x1234u);
    CHECK(f.blend_phase() == doctest::Approx(1.f));   // nothing started

    f.new_full(); for (int i = 0; i < 1000; ++i) f.tick();
    const uint32_t m1 = f.state().master;
    f.new_full(); for (int i = 0; i < 1000; ++i) f.tick();
    const uint32_t m2 = f.state().master;
    REQUIRE(m1 != m2);
    f.undo(); for (int i = 0; i < 1000; ++i) f.tick();
    CHECK(f.state().master == m1);                // one slot deep only
    f.undo(); for (int i = 0; i < 1000; ++i) f.tick();
    CHECK(f.state().master == m2);                // undo-after-undo = redo
}

TEST_CASE("flow NEW: the press chain is deterministic from the woken state") {
    Instrument in1; in1.init(48000.f);
    Instrument in2; in2.init(48000.f);
    Flow a; a.init(&in1, 100.f);
    Flow b; b.init(&in2, 100.f);
    TerrainState s; s.master = 0xBEEF; a.wake(s); b.wake(s);
    uint32_t seen[4];
    for (int i = 0; i < 4; ++i) {
        a.new_full(); for (int k = 0; k < 700; ++k) a.tick();
        seen[i] = a.state().master;
        b.new_full(); for (int k = 0; k < 700; ++k) b.tick();
        CHECK(b.state().master == seen[i]);
    }
    // Consecutive presses land somewhere else every time.
    for (int i = 1; i < 4; ++i) CHECK(seen[i] != seen[i - 1]);
}
