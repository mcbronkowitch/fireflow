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

    // All THREE verbs are refused, not just new_full -- and a refused press
    // changes nothing at all, so no blend even starts.
    CHECK(f.locked());
    f.new_partial(1u << M_BRIGHT); f.tick();
    CHECK(f.state().master == m0);
    CHECK(f.state().reroll[M_BRIGHT] == 0);
    f.undo(); f.tick();
    CHECK(f.state().master == m0);
    CHECK(f.blend_phase() == doctest::Approx(1.f));

    // set_lock() ITSELF is never refused -- an `if (_locked) return;` at the
    // top of it would leave a locked terrain locked forever, which is the
    // single most plausible way to get this wrong.
    f.set_lock(false);
    CHECK_FALSE(f.locked());
    f.new_full(); for (int i = 0; i < 1000; ++i) f.tick();
    CHECK(f.state().master != m0);
    f.new_partial(1u << M_DIRT); for (int i = 0; i < 1000; ++i) f.tick();
    CHECK(f.state().reroll[M_DIRT] == 1);
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

// Nearest step index for a discrete param's value, on kParams' own grid.
static int step_of(int p, float v) {
    const float size = (kParams[p].hi - kParams[p].lo)
                     / float(kParams[p].steps - 1);
    return int(std::floor((v - kParams[p].lo) / size + 0.5f));
}

TEST_CASE("flow NEW: a mid-blend re-press never bare-switches a discrete") {
    // Resolution 4 for the DISCRETE half. A discrete that has not yet reached
    // its switch point must never be handed the value of the terrain the
    // player just walked away from -- least of all outside a duck, and least
    // of all to sit there for the 1.5 s until the new switch point. It holds
    // what it is showing and changes exactly once, at the new blend's own
    // scheduled phase.
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = 0xC0FFEE; f.wake(s);

    // Every macro stays at 0 for the whole case, so each terrain's candidate
    // for a discrete is exactly its base -- which lets us predict what the
    // abandoned terrain WOULD have pushed.
    const int c0 = terrain_of(f).a_carries ? 0 : 1;
    const int cp[6] = { c0 ? P_ENGINE_B : P_ENGINE_A,
                        c0 ? P_FORM_B   : P_FORM_A,
                        c0 ? P_SONG_B   : P_SONG_A,
                        c0 ? P_STEPS_B  : P_STEPS_A,
                        P_SCALE, P_ROOT };   // globals ride the carrier slot
    float orig[6];
    for (int i = 0; i < 6; ++i) orig[i] = f.param_now(cp[i]);

    f.new_full();                                 // press 1
    // Hold the seed to a stable carrier role across the two presses: if the
    // decks swapped roles the pending set would change and "one change" would
    // stop being the right assertion for these six params.
    REQUIRE((terrain_of(f).a_carries ? 0 : 1) == c0);
    // ...and to a genuinely observable bug: the old, wrong behaviour pushed
    // prv[p] through quantize_hyst, which only moves when the value is more
    // than a full step away. At least one of the six must be that far.
    int far = 0;
    for (int i = 0; i < 6; ++i)
        if (std::abs(step_of(cp[i], terrain_of(f).base[cp[i]])
                   - step_of(cp[i], orig[i])) >= 2) ++far;
    CAPTURE(far);
    REQUIRE(far > 0);

    for (int k = 0; k < 60; ++k) f.tick();        // phase 0.1: still pending
    REQUIRE(f.blend_phase() < kCarrierStaggerFrac);
    for (int i = 0; i < 6; ++i) {
        CAPTURE(kParams[cp[i]].name);
        REQUIRE(f.param_now(cp[i]) == doctest::Approx(orig[i]));
    }

    f.new_full();                                 // press 2, mid-stagger
    REQUIRE((terrain_of(f).a_carries ? 0 : 1) == c0);
    f.tick();                                     // THE tick the bug fired on
    for (int i = 0; i < 6; ++i) {
        CAPTURE(kParams[cp[i]].name);
        CHECK(f.param_now(cp[i]) == doctest::Approx(orig[i]));
    }
    // ...and it stays held right up to the new blend's own switch point.
    int changes[6] = {};
    float last[6];
    for (int i = 0; i < 6; ++i) last[i] = f.param_now(cp[i]);
    for (int k = 0; k < 140; ++k) {               // phase 141/600 = 0.235
        f.tick();
        for (int i = 0; i < 6; ++i)
            if (f.param_now(cp[i]) != last[i])
                { ++changes[i]; last[i] = f.param_now(cp[i]); }
    }
    CHECK(f.blend_phase() < kCarrierStaggerFrac);
    for (int i = 0; i < 6; ++i) { CAPTURE(kParams[cp[i]].name);
                                  CHECK(changes[i] == 0); }
    // Past the switch point the whole slot lands, once, and stays.
    for (int k = 0; k < 1000; ++k) {
        f.tick();
        for (int i = 0; i < 6; ++i)
            if (f.param_now(cp[i]) != last[i])
                { ++changes[i]; last[i] = f.param_now(cp[i]); }
    }
    int moved = 0;
    for (int i = 0; i < 6; ++i) {
        CAPTURE(kParams[cp[i]].name); CAPTURE(changes[i]);
        CHECK(changes[i] <= 1);                   // never twice
        if (changes[i]) ++moved;
    }
    CHECK(moved > 0);          // ...and the slot did do something, so the
                               // "never twice" above is not vacuous
}

TEST_CASE("flow NEW: the BODY FILT floor holds at every tick of a blend") {
    // The named hard constraint (§4) that terrain.cpp's apply_constraints can
    // only half enforce: it clamps each terrain's FILT curve under THAT
    // terrain's engine assignment, but the blend interpolates P_FILT_A/B
    // between two terrains clamped under DIFFERENT assignments. A deck pushed
    // as ENGINE_BODY therefore rode the outgoing terrain's legally un-floored
    // FILT (taste.h's BRIGHT "dawn" story draws bp0 to -0.55 on purpose) for
    // up to the whole 6 s ramp -- measured -0.5485 at worst, which costs a
    // BODY deck -13.77 dB against the -0.3 floor. The guard that makes this
    // true at every tick lives in Flow::recompute_and_push and is keyed on
    // the deck's CURRENTLY PUSHED engine.
    //
    // Macros stay at 0 (the calm/ember corner: BRIGHT's FILT curve is at its
    // bp0, the lowest it ever draws) and the Instrument is null -- the whole
    // property is about the value Flow pushes, which param_now() reports, so
    // no audio is needed and 40 masters stay cheap.
    float worst = 0.f; int worst_master = 0;
    int body_blend_ticks = 0, blends_with_body = 0;
    for (uint32_t k = 1; k <= 40; ++k) {
        Flow f; f.init(nullptr, 100.f);
        TerrainState s; s.master = k; f.wake(s);
        f.new_full();
        bool body_here = false;
        for (int i = 0; i < 620; ++i) {   // 6.2 s: the ramp plus a settled tail
            f.tick();
            const bool blending = f.blend_phase() < 1.f;
            for (int d = 0; d < 2; ++d) {
                const int ep = d ? P_ENGINE_B : P_ENGINE_A;
                const int fp = d ? P_FILT_B   : P_FILT_A;
                if (int(f.param_now(ep) + 0.5f) != ENGINE_BODY) continue;
                if (blending) { ++body_blend_ticks; body_here = true; }
                const float v = f.param_now(fp);
                if (v < worst) { worst = v; worst_master = int(k); }
            }
        }
        if (body_here) ++blends_with_body;
    }
    CAPTURE(worst_master); CAPTURE(worst);
    CHECK(worst >= kBodyFiltFloor);
    // ...and the seed range really does sample the window the bug lived in:
    // a deck pushed as BODY *while a blend is running*. Without these the
    // CHECK above could pass by never reaching BODY at all. (They are not the
    // falsifiability proof -- the CHECK is: removing the runtime clamp takes
    // `worst` to -0.4770 on this very seed range.)
    REQUIRE(body_blend_ticks > 1000);
    REQUIRE(blends_with_body >= 10);
}

TEST_CASE("flow NEW: param_now never leaves the parameter's range") {
    // The continuity offset is a CONSTANT correction held across a blend, so
    // a macro moving during one can push the combined value past a
    // parameter's range even though both terrains' candidates sit inside it.
    // apply_param clamps at the engine boundary, but param_now() is a public
    // observer -- Plan B's display reads it and the change guard compares
    // against it -- so it must never report a value the engine would have
    // clamped. Presses land every 50 ticks, deep inside the 600-tick ramp, so
    // the residual is essentially always live.
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = 0x5A17; f.wake(s);
    Rng r; r.seed(0xBADC0DE);
    float worst = 0.f, worst_v = 0.f; int worst_p = 0;
    for (int i = 0; i < 8000; ++i) {
        if ((i % 50) == 0) f.new_full();
        for (int m = 0; m < MACRO_COUNT; ++m) f.set_macro(m, r.next_unipolar());
        f.tick();
        for (int p = 0; p < P_COUNT; ++p) {
            const float v = f.param_now(p);
            const float over = kParams[p].lo - v > v - kParams[p].hi
                             ? kParams[p].lo - v : v - kParams[p].hi;
            if (over > worst) { worst = over; worst_v = v; worst_p = p; }
        }
    }
    CAPTURE(kParams[worst_p].name); CAPTURE(worst_v); CAPTURE(worst);
    CHECK(worst <= 1e-6f);
}

TEST_CASE("flow NEW: knobs stay live through the blend (resolution 3)") {
    // The blend is between two TERRAINS, not two frozen value vectors. Early
    // in a blend the outgoing terrain still owns ~97 % of the output, so an
    // implementation that evaluated it at the macro values frozen at press
    // time would leave the knobs very nearly dead for six seconds.
    //
    // Reference: a second Flow on the same terrain that never presses NEW and
    // receives the same sweep. Early in the blend the two must agree to
    // within the blend phase's worth of the distance between the terrains.
    int tested_macros = 0;
    for (int m = 0; m < MACRO_COUNT; ++m) {
        Instrument i1; i1.init(48000.f);
        Instrument i2; i2.init(48000.f);
        Flow f; f.init(&i1, 100.f);
        Flow g; g.init(&i2, 100.f);
        TerrainState s; s.master = 0xA11CE; f.wake(s); g.wake(s);
        float base_v[P_COUNT];
        for (int p = 0; p < P_COUNT; ++p) base_v[p] = g.param_now(p);

        f.new_full();                             // only f blends
        for (int k = 1; k <= 20; ++k) {           // sweep this macro 0 -> 1
            const float v = float(k) / 20.f;
            f.set_macro(m, v); g.set_macro(m, v);
            f.tick(); g.tick();
        }
        REQUIRE(f.blend_phase() < 0.05f);         // outgoing still dominant

        int checked = 0;
        const MacroMap& mm = terrain_of(g).map[m];
        for (int t = 0; t < mm.n_targets; ++t) {
            const int p = mm.targets[t].param;
            // Discretes step; SIZE/DECAY lag behind the sweep on the SPACE
            // slew; REVMIX_A/B are under the blend's duck. What is left is a
            // clean read of "did the outgoing terrain follow the knob".
            if (kParams[p].steps > 0) continue;
            if (p == P_REV_SIZE || p == P_REV_DECAY) continue;
            if (p == P_REVMIX_A || p == P_REVMIX_B) continue;
            const float span = kParams[p].hi - kParams[p].lo;
            const float moved = std::fabs(g.param_now(p) - base_v[p]) / span;
            if (moved < 0.20f) continue;          // this target barely sweeps
            ++checked;
            CAPTURE(m); CAPTURE(kParams[p].name); CAPTURE(moved);
            CHECK(std::fabs(f.param_now(p) - g.param_now(p)) / span < 0.10f);
        }
        if (checked) ++tested_macros;
    }
    // SPACE's whole target set is slewed or ducked, so it is legitimately not
    // readable this way; every other macro must have contributed.
    CHECK(tested_macros >= MACRO_COUNT - 1);
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
