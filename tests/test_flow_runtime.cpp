// tests/test_flow_runtime.cpp
//
// The flow runtime core (spec §3-§5): story-curve evaluation, CV sums,
// weather, discrete hysteresis, the SPACE slew, and the one control tick.
#include "doctest/doctest.h"
#include "flow/flow.h"
#include "flow/taste.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
using namespace spky;
using namespace spky::flow;

static Flow make(Instrument& in, uint32_t seed) {
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = seed; f.wake(s);
    return f;
}

// How many macros' stories curve param p in this terrain. Shared targets
// (BRIGHT and SPACE both curve REVMIX_A / REV_DECAY) combine by the
// farthest-from-base rule, which is deliberately NOT a per-macro monotone
// blend -- the monotone sweep below skips them; the rule has its own test.
static int macros_targeting(const Terrain& t, int p) {
    int n = 0;
    for (int m = 0; m < MACRO_COUNT; ++m)
        for (int i = 0; i < t.map[m].n_targets; ++i)
            if (t.map[m].targets[i].param == p) { ++n; break; }
    return n;
}

TEST_CASE("flow runtime: macro sweep is monotone per target (spec 7.2)") {
    Instrument in; in.init(48000.f);
    Flow f = make(in, 0xA11CE);
    TerrainState s; s.master = 0xA11CE;
    for (int m = 0; m < MACRO_COUNT; ++m) {
        // capture each storied target at 33 knob positions, check monotone
        const auto& mm = terrain_of(f).map[m];
        for (int t = 0; t < mm.n_targets; ++t) {
            const int p = mm.targets[t].param;
            if (macros_targeting(terrain_of(f), p) > 1) continue;  // shared
            // Park every macro at 0 and re-wake (same state -> identical
            // terrain) so the SPACE slew and the hysteresis states land
            // settled at the k=0 value. A sweep that starts mid-flight
            // would open by slewing DOWN toward its own start point and
            // fail the monotone check for reasons that are about leftover
            // state, not about the curves. Parking MOTION at 0 also
            // becalms the weather for every macro except MOTION's own
            // sweep -- and weather never targets MOTION, so that sweep is
            // clean too.
            for (int m2 = 0; m2 < MACRO_COUNT; ++m2) f.set_macro(m2, 0.f);
            f.wake(s);
            float prev = 0.f; bool first = true;
            bool up = mm.targets[t].bp[4] >= mm.targets[t].bp[0];
            for (int k = 0; k <= 32; ++k) {
                f.set_macro(m, k / 32.f); f.tick();
                float v = f.param_now(p);
                // Discrete targets ride hysteresis: stepped output, still
                // non-strictly monotone under a monotone sweep, so the
                // same non-strict check covers them.
                if (!first) { if (up) CHECK(v >= prev - 1e-6f);
                              else    CHECK(v <= prev + 1e-6f); }
                prev = v; first = false;
            }
        }
    }
}

TEST_CASE("flow runtime: weather is bounded, deterministic, becalmed (7.5)") {
    Instrument in; in.init(48000.f);
    // NOT the brief's 0xB0B0: that terrain aims all three of its weather
    // oscillators at MOTION and BRIGHT, so DENSITY and SPACE see exactly
    // zero weather and every assertion below passes vacuously (0 <= 0.1,
    // 0 == 0). 0xB0BD weathers DENSITY (depth .086, period 454 s) and
    // SPACE (depth .094, period 815 s), one oscillator each.
    Flow f = make(in, 0xB0BD);
    f.set_macro(M_MOTION, 1.f);
    float mn = 1.f, mx = -1.f;
    for (int i = 0; i < 100000; ++i) {           // 1000 s at 100 Hz
        f.tick();
        float w = f.eff_macro(M_DENSITY) - 0.f;  // knob 0, cv 0 -> pure weather
        mn = std::min(mn, w); mx = std::max(mx, w);
    }
    CHECK(mx <= kWeatherDepthMax + 1e-4f);
    CHECK(mn >= -1e-6f);                          // clamped at 0
    CHECK(mx > 0.01f);   // the bound above must not pass by there being
                         // no weather at all -- see the seed note.
    // Two runs agree (pure function of sub-seed and time).
    Instrument in2; in2.init(48000.f);
    Flow g = make(in2, 0xB0BD);
    g.set_macro(M_MOTION, 1.f);
    for (int i = 0; i < 500; ++i) { g.tick(); }
    Instrument in3; in3.init(48000.f);
    Flow h = make(in3, 0xB0BD);
    h.set_macro(M_MOTION, 1.f);
    for (int i = 0; i < 500; ++i) { h.tick(); }
    CHECK(g.eff_macro(M_SPACE) == h.eff_macro(M_SPACE));
    // MOTION at 0 becalms: weather contribution is 0.
    Instrument in4; in4.init(48000.f);
    Flow z = make(in4, 0xB0BD);
    z.set_macro(M_MOTION, 0.f);
    for (int i = 0; i < 5000; ++i) z.tick();
    CHECK(z.eff_macro(M_DENSITY) == doctest::Approx(0.f));
}

TEST_CASE("flow runtime: discrete targets switch once per crossing (7.6)") {
    Instrument in; in.init(48000.f);
    // NOT the brief's 0x5EED: under that terrain the WANDER story's FORM_A
    // curve only spans 0.946..1.154 across the hover band below -- it never
    // reaches a step seam, so the test could not go red no matter what the
    // quantizer did. 0x5EED5 spans 1.322..1.607, putting the 1<->2 seam at
    // 1.5 inside the band (a pass-through quantizer flips 13 times there).
    Flow f = make(in, 0x5EED5);
    // Sweep WANDER slowly up and down around a FORM threshold; count changes.
    int changes = 0; float last = f.param_now(P_FORM_A);
    for (int i = 0; i <= 4000; ++i) {
        float k = 0.70f + 0.02f * std::sin(i * 0.01f);   // hovers at a seam
        f.set_macro(M_WANDER, k); f.tick();
        float v = f.param_now(P_FORM_A);
        if (v != last) { changes++; last = v; }
    }
    CAPTURE(changes);
    CHECK(changes <= 2);   // with hysteresis; without it this is dozens
}

TEST_CASE("flow runtime: shared target takes the candidate farther from base") {
    // P_REVMIX_A is curved by BOTH the BRIGHT story (ember: REVMIX rides
    // HIGH at knob 0) and the SPACE story (near-dry: REVMIX sits LOW at
    // knob 0). The combine rule pushes the candidate FARTHEST from the
    // terrain base (tie -> lower macro index): deterministic, and whichever
    // knob is actually doing something wins, so neither knob feels dead.
    Instrument in; in.init(48000.f);
    Flow f = make(in, 0xA11CE);
    f.set_macro(M_BRIGHT, 0.f); f.set_macro(M_SPACE, 0.f); f.tick();
    const Terrain& t = terrain_of(f);
    float vb = -1.f, vs = -1.f;                  // eff 0 -> bp[0] exactly
    for (int i = 0; i < t.map[M_BRIGHT].n_targets; ++i)
        if (t.map[M_BRIGHT].targets[i].param == P_REVMIX_A)
            vb = t.map[M_BRIGHT].targets[i].bp[0];
    for (int i = 0; i < t.map[M_SPACE].n_targets; ++i)
        if (t.map[M_SPACE].targets[i].param == P_REVMIX_A)
            vs = t.map[M_SPACE].targets[i].bp[0];
    REQUIRE(vb >= 0.f); REQUIRE(vs >= 0.f);
    // The base is SPACE's own bp[0] (the later macro wrote it in stage 4),
    // so SPACE's candidate distance is exactly 0 and BRIGHT's ember value
    // must be the strictly-farther one -- and the pushed value.
    const float base = t.base[P_REVMIX_A];
    CHECK(std::fabs(vb - base) > std::fabs(vs - base));
    const float expect = std::fabs(vb - base) > std::fabs(vs - base) ? vb : vs;
    CHECK(f.param_now(P_REVMIX_A) == doctest::Approx(expect));

    // The check above does NOT discriminate: at both knobs 0, SPACE's
    // candidate IS the base (stage 4 wrote SPACE's bp[0] there last), so
    // its distance is 0, and BRIGHT is also the lower macro index -- the
    // assertion passes identically under "farthest from base", "lowest
    // index wins" and "first writer wins". This second position is the
    // discriminating one. With BRIGHT at 0 and SPACE at 1, SPACE's
    // candidate (0.938530, d=0.890415) beats BRIGHT's ember (0.858469,
    // d=0.810354), so the HIGHER macro index must win: under either
    // index-priority rule this line reads 0.858469 and goes red.
    f.set_macro(M_BRIGHT, 0.f); f.set_macro(M_SPACE, 1.f); f.tick();
    float vb1 = 0.f, vs1 = 0.f;
    for (int i = 0; i < t.map[M_BRIGHT].n_targets; ++i)
        if (t.map[M_BRIGHT].targets[i].param == P_REVMIX_A)
            vb1 = t.map[M_BRIGHT].targets[i].bp[0];   // BRIGHT still at eff 0
    for (int i = 0; i < t.map[M_SPACE].n_targets; ++i)
        if (t.map[M_SPACE].targets[i].param == P_REVMIX_A)
            vs1 = t.map[M_SPACE].targets[i].bp[4];    // SPACE at eff 1
    CHECK(std::fabs(vs1 - base) > std::fabs(vb1 - base));   // SPACE farther
    CHECK(vs1 > vb1);                    // and it is NOT the lower-index one
    CHECK(f.param_now(P_REVMIX_A) == doctest::Approx(vs1));

    // Shared-target monotonicity (the macro sweep test skips these params
    // because the rule is not a per-macro blend). Both candidates sit
    // ABOVE the base here, so "farthest from base" reduces to max(vb, vs)
    // -- a max of a constant and a rising curve, which is non-decreasing
    // AND continuous through the winner flip near the top of the sweep.
    // (That is a property of this configuration, not of the rule: if the
    // two candidates straddled the base, the flip would be a jump.)
    f.set_macro(M_BRIGHT, 0.f);
    float prev = 0.f;
    for (int k = 0; k <= 32; ++k) {
        f.set_macro(M_SPACE, k / 32.f); f.tick();
        const float v = f.param_now(P_REVMIX_A);
        if (k) { CAPTURE(k); CHECK(v >= prev - 1e-6f); }
        prev = v;
    }
}

TEST_CASE("flow runtime: a drone's DENSITY knob at max samples the archetype "
          "window, not the raw knob position") {
    // Pins the APPLICATION half of the archetype-window mechanism (spec
    // 2026-08-06 §4). test_flow_terrain.cpp proves the table value reaches
    // Terrain::window; this proves Flow::eval_terrain (flow.cpp) actually
    // REMAPS eff[m] through that window before sampling the story curve,
    // rather than the window sitting on the Terrain unused. eval_curve is
    // private to flow.cpp (an anonymous-namespace function, not declared in
    // any header), so the expected value is recomputed here with the same
    // piecewise-linear formula on the quadrant grid {0,.25,.5,.75,1} that
    // flow.cpp's eval_curve implements.
    auto eval_curve_ref = [](const Curve& c, float x) {
        float g = x * 4.f;
        int i = int(g);
        if (i > 3) i = 3;
        return c.bp[i] + (c.bp[i + 1] - c.bp[i]) * (g - float(i));
    };

    uint32_t found_master = 0;
    for (uint32_t master = 1; master <= 20000 && !found_master; ++master) {
        TerrainState s; s.master = master;
        const Terrain t = generate(s);
        if (t.arch != ARCH_DRONE) continue;
        if (std::strcmp(kStories[t.map[M_DENSITY].story].name, "rate") != 0)
            continue;
        found_master = master;
    }
    REQUIRE(found_master != 0);   // see test_flow_terrain.cpp for the same
                                   // scan; report rather than weaken if empty

    Instrument in; in.init(48000.f);
    Flow f = make(in, found_master);
    for (int m = 0; m < MACRO_COUNT; ++m) f.set_macro(m, 0.f);
    f.set_macro(M_DENSITY, 1.f);
    for (int i = 0; i < 5; ++i) f.tick();       // settle; not blending, so one
                                                 // tick would already do it

    const auto& mm = terrain_of(f).map[M_DENSITY];
    const Curve* c = nullptr;
    for (int i = 0; i < mm.n_targets; ++i)
        if (mm.targets[i].param == P_DENSITY_A) c = &mm.targets[i];
    REQUIRE(c != nullptr);

    // The window is {0,.45} for a drone's "rate" story (taste.h), so knob
    // 1.0 must sample the curve at x = 0.45, not x = 1.0.
    const float expect_windowed = eval_curve_ref(*c, 0.45f);
    const float raw_knob        = eval_curve_ref(*c, 1.f);
    CAPTURE(found_master);
    CHECK(f.param_now(P_DENSITY_A) == doctest::Approx(expect_windowed));
    // And provably NOT the unwindowed reading -- the two differ here (the
    // "rate" curve's Q4 rises further than its Q2/Q3 lift), so this is a
    // real discriminator, not a coincidence of two equal numbers.
    CHECK(raw_knob != doctest::Approx(expect_windowed));
    CHECK(f.param_now(P_DENSITY_A) != doctest::Approx(raw_knob));
}

TEST_CASE("flow runtime: set_ctrl_hz retimes the tick and leaves live state alone") {
    // VCV changes sample rate at runtime, and the render host already derives
    // ctrl_hz from the scenario's rate. Before this verb the only way to
    // change it was init(), which also wipes lock, undo, blend, both macro
    // arrays and the clock -- i.e. a host could not follow a rate change
    // without rebuilding the instrument around it.
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = 0x5E7C; f.wake(s);
    f.set_macro(M_BRIGHT, 0.7f); f.set_cv(M_SPACE, 0.2f);
    f.new_full();
    for (int i = 0; i < 60; ++i) f.tick();       // mid-blend
    f.set_lock(true);

    const TerrainState before_state = f.state();
    const float  before_phase = f.blend_phase();
    const double before_t     = f.now_s();
    const float  before_eff[MACRO_COUNT] = { f.eff_macro(0), f.eff_macro(1),
                                             f.eff_macro(2), f.eff_macro(3),
                                             f.eff_macro(4), f.eff_macro(5) };
    float before_p[P_COUNT];
    for (int p = 0; p < P_COUNT; ++p) before_p[p] = f.param_now(p);

    f.set_ctrl_hz(400.f);

    // Nothing moved: the call is bookkeeping, not a reset and not a push.
    CHECK(f.state().master == before_state.master);
    CHECK(f.blend_phase() == doctest::Approx(before_phase));
    CHECK(f.now_s() == doctest::Approx(before_t));
    CHECK(f.locked());
    CHECK(f.can_undo());
    for (int m = 0; m < MACRO_COUNT; ++m) {
        CAPTURE(m); CHECK(f.eff_macro(m) == doctest::Approx(before_eff[m]));
    }
    for (int p = 0; p < P_COUNT; ++p) {
        CAPTURE(kParams[p].name);
        CHECK(f.param_now(p) == doctest::Approx(before_p[p]));
    }
    // ...and the state it preserved was worth preserving: a blend really was
    // in flight and the macros really were somewhere other than their
    // defaults, so the four CHECKs above are not comparing zeroes.
    REQUIRE(before_phase > 0.f);
    REQUIRE(before_phase < 1.f);
    REQUIRE(before_eff[M_BRIGHT] > 0.5f);

    // The tick period followed the new rate.
    f.set_lock(false);
    const double t0 = f.now_s();
    for (int i = 0; i < 400; ++i) f.tick();
    CHECK(f.now_s() - t0 == doctest::Approx(1.0).epsilon(1e-9));
}

TEST_CASE("flow runtime: the SPACE slew keeps its wall-clock time constant across a rate change") {
    // kSpaceSlewS is a duration in SECONDS, so the one-pole coefficient is a
    // function of the tick period -- a set_ctrl_hz that forgot to re-derive
    // it would make the room drift four times too fast at 4x the rate, which
    // is the whole reason the verb exists rather than a bare `_ctrl_hz = hz`.
    //
    // Reference: an identical Flow that ran at 400 Hz from init(). Both are
    // driven for the same WALL-CLOCK second after the same target step, so
    // they must land in the same place.
    Instrument i1; i1.init(48000.f);
    Instrument i2; i2.init(48000.f);
    Flow f; f.init(&i1, 100.f);      // then switched to 400 Hz below
    Flow g; g.init(&i2, 400.f);      // 400 Hz all along
    TerrainState s; s.master = 0x5175; f.wake(s); g.wake(s);
    f.set_macro(M_SPACE, 0.f); g.set_macro(M_SPACE, 0.f);
    f.tick(); g.tick();
    const float start = f.param_now(P_REV_SIZE);
    REQUIRE(g.param_now(P_REV_SIZE) == doctest::Approx(start));

    f.set_ctrl_hz(400.f);
    f.set_macro(M_SPACE, 1.f); g.set_macro(M_SPACE, 1.f);
    for (int i = 0; i < 400; ++i) { f.tick(); g.tick(); }   // 1.0 s of travel

    const float moved = f.param_now(P_REV_SIZE) - start;
    CAPTURE(start); CAPTURE(moved);
    REQUIRE(moved > 0.05f);                       // the slew really is moving
    CHECK(f.param_now(P_REV_SIZE)
          == doctest::Approx(g.param_now(P_REV_SIZE)).epsilon(0.002));
}

TEST_CASE("flow runtime: a BBD deck in FLOW keeps its bend inside the budget") {
    Instrument in; in.init(48000.f);
    int seen_bbd_flow = 0, seen_free_deck = 0, seen_bbd_step_free = 0;
    for (uint32_t k = 1; k <= 400; ++k) {
        Flow f = make(in, k * 2654435761u);
        const bool step = terrain_of(f).base[P_MODE] > 0.5f;
        for (int m = 0; m < MACRO_COUNT; ++m) f.set_macro(m, 0.5f);
        for (int i = 0; i < 200; ++i) f.tick();   // past the wake transient
        for (int d = 0; d < 2; ++d) {
            const int ep = d ? P_ENGINE_B : P_ENGINE_A;
            const int rp = d ? P_RANGE_B  : P_RANGE_A;
            const bool bbd = int(f.param_now(ep) + .5f) == ENGINE_BBD;
            CAPTURE(k); CAPTURE(d); CAPTURE(f.param_now(rp));
            if (bbd && !step) {
                ++seen_bbd_flow;
                CHECK(f.param_now(rp) <= kBbdFlowRangeMax);
            } else if (f.param_now(rp) > kBbdFlowRangeMax) {
                ++seen_free_deck;
                if (bbd && step) ++seen_bbd_step_free;
            }
        }
    }
    // Non-vacuous in THREE directions, one per condition the clamp branches
    // on. seen_bbd_flow pins the clamp itself: a BBD deck in FLOW has to
    // actually occur, or the CHECK above never runs. seen_free_deck pins the
    // engine gate: SOME deck (of any engine, in either mode) has to be seen
    // above the cap, without which this case would also pass against an
    // implementation that clamped RANGE on every deck unconditionally and
    // killed the pitch lane everywhere. seen_bbd_step_free pins the mode gate
    // specifically: a BBD deck in STEP has to be seen above the cap, without
    // which this case would also pass against an implementation that dropped
    // the !_mode_now condition and clamped BBD decks in STEP too -- the
    // 772-odd non-BBD decks alone would keep seen_free_deck positive even
    // then.
    CAPTURE(seen_bbd_flow); CAPTURE(seen_free_deck); CAPTURE(seen_bbd_step_free);
    CHECK(seen_bbd_flow > 0);
    CHECK(seen_free_deck > 0);
    CHECK(seen_bbd_step_free > 0);
}

TEST_CASE("flow runtime: the BBD bend budget holds mid-blend too, not just settled") {
    // Companion to the settled-state case above, which ticks 200 times off
    // wake() and never presses NEW -- so it only pins the clamp once a
    // terrain has stopped moving. The guard's own comment in flow.cpp
    // (beside kBodyFiltFloor's) gives the reason this is a separate case:
    // a NEW blend interpolates P_RANGE_A/B between two terrains whose
    // engine assignments differ, so a deck can run as BBD for nearly the
    // whole ramp on a RANGE value drawn by a terrain that never put a BBD
    // there. An implementation that guarded on `!blending` instead of the
    // real per-deck engine-and-mode condition would still pass the settled
    // case above; nothing else in this file would catch it.
    //
    // Mirrors tests/test_flow_new.cpp's "the BODY FILT floor holds at every
    // tick of a blend": press NEW, tick through the ramp and a settled
    // tail, and gate on a counter that proves the window was genuinely
    // sampled (BBD-in-FLOW ticks while blending), so this cannot pass
    // vacuously against a change that never actually reaches that state.
    Instrument in; in.init(48000.f);
    int bbd_flow_blend_ticks = 0, blends_with_bbd_flow = 0;
    for (uint32_t k = 1; k <= 400; ++k) {
        Flow f = make(in, k * 2654435761u);
        for (int m = 0; m < MACRO_COUNT; ++m) f.set_macro(m, 0.5f);
        for (int i = 0; i < 50; ++i) f.tick();     // past the wake transient
        f.new_full();
        bool seen_here = false;
        for (int i = 0; i < 620; ++i) {            // 6.2 s: the ramp + a tail
            f.tick();
            if (f.blend_phase() >= 1.f) continue;  // settled: the other case
            // i == 0 is the one tick the guard's own comment (flow.cpp,
            // beside this clamp) names as accepted rather than a bug: on a
            // STEP -> FLOW press, P_MODE switches at blend phase 0 (the
            // press itself), but P_MODE is LAST in the parameter table, so
            // THIS tick's P_RANGE_* row is pushed before THIS tick's P_MODE
            // row updates _mode_now -- the clamp still reads last tick's
            // STEP and is skipped for exactly one control period. Measured:
            // every k in this loop that presses into a BBD-in-FLOW deck
            // hits it at i == 0 and nowhere else, so skipping i == 0 keeps
            // this case testing the guard's real invariant (holds from the
            // second tick of the blend onward) instead of re-asserting the
            // one tick the design doc already excuses.
            if (i == 0) continue;
            for (int d = 0; d < 2; ++d) {
                const int ep = d ? P_ENGINE_B : P_ENGINE_A;
                const int rp = d ? P_RANGE_B  : P_RANGE_A;
                if (int(f.param_now(ep) + .5f) != ENGINE_BBD) continue;
                if (f.param_now(P_MODE) > 0.5f) continue;   // STEP: no cap
                ++bbd_flow_blend_ticks; seen_here = true;
                CAPTURE(k); CAPTURE(d); CAPTURE(i);
                CHECK(f.param_now(rp) <= kBbdFlowRangeMax);
            }
        }
        if (seen_here) ++blends_with_bbd_flow;
    }
    CAPTURE(bbd_flow_blend_ticks); CAPTURE(blends_with_bbd_flow);
    // Non-vacuity floors, the same role as test_flow_new.cpp's
    // body_blend_ticks/blends_with_body: a deck really did run BBD-in-FLOW
    // *while blending* across a real spread of seeds, not just once.
    REQUIRE(bbd_flow_blend_ticks > 1000);
    REQUIRE(blends_with_bbd_flow >= 10);
}

TEST_CASE("flow runtime: the genre setting changes no sound by itself") {
    // The design's central safety claim: GENRE constrains the next NEW draw
    // and nothing else. Two identical instruments, one locked to DRONE, no
    // press: every pushed parameter must agree, tick for tick.
    spky::Instrument ia, ib;
    ia.init(48000.f);
    ib.init(48000.f);
    spky::flow::Flow fa, fb;
    fa.init(&ia, 100.f);
    fb.init(&ib, 100.f);
    spky::flow::TerrainState st;
    st.master = 0xC0FFEE;
    fa.wake(st);
    fb.wake(st);
    fb.set_genre(spky::flow::ARCH_DRONE);
    for (int t = 0; t < 400; ++t) {
        const float x = float(t) / 400.f;
        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m) {
            fa.set_macro(m, x);
            fb.set_macro(m, x);
        }
        fa.tick();
        fb.tick();
        for (int p = 0; p < spky::flow::P_COUNT; ++p)
            REQUIRE(fa.param_now(p) == fb.param_now(p));
    }
}

TEST_CASE("flow runtime: a genre-locked NEW press lands in that genre") {
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);
    spky::flow::TerrainState st;
    st.master = 0x101;
    fl.wake(st);
    fl.set_genre(spky::flow::ARCH_FRAGMENT);
    CHECK(fl.genre() == spky::flow::ARCH_FRAGMENT);
    for (int i = 0; i < 40; ++i) {
        REQUIRE(fl.new_full());
        CHECK(spky::flow::arch_of(fl.state().master) ==
              spky::flow::ARCH_FRAGMENT);
        for (int t = 0; t < 700; ++t) fl.tick();   // let the blend settle
    }
}

TEST_CASE("flow runtime: a scale override holds, and AUTO gives the terrain back") {
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);
    spky::flow::TerrainState st;
    st.master = 0x334;
    fl.wake(st);
    for (int t = 0; t < 50; ++t) fl.tick();
    const float terrain_scale = fl.param_now(spky::flow::P_SCALE);
    // The seed is picked so the terrain does NOT already sit on the overridden
    // scale -- otherwise every assertion below holds with no override at all
    // and the case cannot go red. (0x333, the seed this was first written
    // with, draws SCALE_MIN_PENT itself and passed against an empty stub.)
    REQUIRE(terrain_scale != float(spky::SCALE_MIN_PENT));

    fl.set_scale_override(spky::SCALE_MIN_PENT);
    for (int t = 0; t < 50; ++t) {
        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m)
            fl.set_macro(m, float(t) / 50.f);       // macros must not shake it
        fl.tick();
        REQUIRE(fl.param_now(spky::flow::P_SCALE) == float(spky::SCALE_MIN_PENT));
    }
    fl.set_scale_override(-1);
    fl.tick();
    CHECK(fl.param_now(spky::flow::P_SCALE) == terrain_scale);
}

TEST_CASE("flow runtime: an override released after NEW lands on the new scale") {
    // THE test for spec 3.3. quantize_hyst compares strictly at
    // kHysteresisFrac 0.5, so an un-forced ONE-STEP move never passes its
    // guard. If the override skipped quantize_hyst, _step_now would freeze
    // while the override was held, and a terrain one step away would be
    // unreachable forever after release. Search the seeds for exactly that
    // pairing rather than hoping for it.
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);

    bool tested = false;
    for (uint32_t m = 1; m < 400 && !tested; ++m) {
        spky::flow::TerrainState a;
        a.master = m;
        const int sa = int(spky::flow::generate(a).base[spky::flow::P_SCALE]);
        for (uint32_t n = 1; n < 400 && !tested; ++n) {
            spky::flow::TerrainState b;
            b.master = n;
            const int sb = int(spky::flow::generate(b).base[spky::flow::P_SCALE]);
            if (std::abs(sa - sb) != 1) continue;      // need a ONE-step move
            tested = true;

            fl.wake(a);
            for (int t = 0; t < 50; ++t) fl.tick();
            REQUIRE(fl.param_now(spky::flow::P_SCALE) == float(sa));

            fl.set_scale_override(spky::SCALE_WHOLE);
            for (int t = 0; t < 50; ++t) fl.tick();
            fl.wake(b);                                // stands in for a NEW press
            for (int t = 0; t < 800; ++t) fl.tick();
            REQUIRE(fl.param_now(spky::flow::P_SCALE) == float(spky::SCALE_WHOLE));

            fl.set_scale_override(-1);
            fl.tick();
            CHECK(fl.param_now(spky::flow::P_SCALE) == float(sb));
        }
    }
    REQUIRE(tested);      // the search must actually have found a pair
}

TEST_CASE("flow runtime: an override survives a blend and can be released inside one") {
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);
    spky::flow::TerrainState st;
    st.master = 0x55;
    fl.wake(st);
    for (int t = 0; t < 50; ++t) fl.tick();
    // Precondition, or the case passes against an empty stub: the override is
    // only observable while it differs from what the terrain itself draws.
    // Needed twice -- NEW moves to a second terrain mid-case, and that one has
    // to differ too. A taste.h retune that makes either draw KUMOI must fail
    // here loudly rather than turn the assertions below into tautologies.
    REQUIRE(int(spky::flow::terrain_of(fl).base[spky::flow::P_SCALE])
            != int(spky::SCALE_KUMOI));
    fl.set_scale_override(spky::SCALE_KUMOI);
    REQUIRE(fl.new_full());
    REQUIRE(int(spky::flow::terrain_of(fl).base[spky::flow::P_SCALE])
            != int(spky::SCALE_KUMOI));
    for (int t = 0; t < 300; ++t) {                    // mid-blend
        fl.tick();
        REQUIRE(fl.param_now(spky::flow::P_SCALE) == float(spky::SCALE_KUMOI));
    }
    fl.set_scale_override(-1);                         // release INSIDE the blend
    for (int t = 0; t < 400; ++t) fl.tick();
    CHECK(fl.param_now(spky::flow::P_SCALE) ==
          float(int(spky::flow::terrain_of(fl).base[spky::flow::P_SCALE])));
}

TEST_CASE("flow runtime: a root override replaces the terrain's root and stays "
          "in range") {
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);
    spky::flow::TerrainState st;
    st.master = 0x9;
    fl.wake(st);
    // Precondition: without it the case would pass against an empty stub if a
    // taste.h retune ever made this master draw root 7 by itself.
    REQUIRE(int(spky::flow::terrain_of(fl).base[spky::flow::P_ROOT]) != 7);
    fl.set_root_override(7);
    fl.tick();
    CHECK(fl.param_now(spky::flow::P_ROOT) == 7.f);
    // The fan-out to both parts is apply_param's job (flow_params.h: P_ROOT
    // calls set_root on PART_A and PART_B from one value) and is true by
    // construction, not observed here: Instrument::_parts is private with no
    // read-back path. Rather than add an accessor to the engine for a test's
    // convenience, this pins the ONE value the override publishes -- which is
    // the only thing this task can get wrong. Hence the case's name.
    //
    // Out of range must be clamped, not published: param_now is a public
    // observer and flow.cpp says it must never show an out-of-range value.
    fl.set_root_override(99);
    fl.tick();
    CHECK(fl.param_now(spky::flow::P_ROOT) <= spky::flow::kParams[spky::flow::P_ROOT].hi);
    CHECK(fl.param_now(spky::flow::P_ROOT) >= spky::flow::kParams[spky::flow::P_ROOT].lo);
}
