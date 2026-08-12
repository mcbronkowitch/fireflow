// tests/test_flow_mode.cpp
#include "doctest/doctest.h"
#include "flow/taste.h"
#include "flow/flow_params.h"
#include "flow/flow.h"
#include "flow/terrain.h"
#include "instrument.h"
using namespace spky::flow;

TEST_CASE("flow mode: nothing may be inserted before P_MODE") {
    // Base draws are keyed kStreamParamBase + param (terrain.cpp). If a
    // parameter is inserted BEFORE P_MODE, every parameter from there on gets
    // a different RNG stream and every existing terrain code resolves to a
    // different sound.
    //
    // This used to read `P_MODE == P_COUNT - 1` -- "P_MODE is the last
    // parameter". That was never the real invariant, only the cheapest way to
    // state it, and on 2026-08-12 P_PACE was APPENDED BEHIND P_MODE precisely
    // because appending is free: a later param re-seeds nothing earlier. So
    // the position is pinned by INDEX now, against the enum as it stood when
    // the terrain codes in circulation were drawn. Anything appended after
    // P_PACE is free too and needs no edit here; anything that moves P_MODE
    // down re-renders every terrain code and must go red.
    CHECK(P_MODE == 62);
    CHECK(P_PACE == P_MODE + 1);
    CHECK(P_PACE == P_COUNT - 1);      // nothing appended after it yet
    // The two facts flow.cpp's static_asserts turn into a compile error, kept
    // here as a readable statement of the ordering they enforce.
    CHECK(P_RANGE_A < P_MODE);
    CHECK(P_RANGE_B < P_MODE);
    CHECK(kParams[P_MODE].lo == 0.f);
    CHECK(kParams[P_MODE].hi == 1.f);
    CHECK(kParams[P_MODE].steps == 2);
    // P_PACE's own table row: continuous, 0..1, centre 0.5 == x1 under
    // pace_mult (mod/divisions.h).
    CHECK(kParams[P_PACE].lo == 0.f);
    CHECK(kParams[P_PACE].hi == 1.f);
    CHECK(kParams[P_PACE].steps == 0);
}

TEST_CASE("flow mode: archetype weights are probabilities, drone lowest") {
    for (int a = 0; a < ARCH_COUNT; ++a) {
        CAPTURE(a);
        CHECK(kModeW[a] >= 0.f);
        CHECK(kModeW[a] <= 1.f);
    }
    // A drone is the archetype that normally wants no step sequencer.
    for (int a = 0; a < ARCH_COUNT; ++a)
        if (a != ARCH_DRONE) CHECK(kModeW[ARCH_DRONE] < kModeW[a]);
}

TEST_CASE("flow mode: steps never run without a grid") {
    // The failure this guards: STEP mode on with SYNC off is a step sequencer
    // at a free-running rate, which is what Glow shipped with. No reachable
    // tick may show that combination.
    bool saw_step = false, saw_flow = false;
    for (uint32_t master = 1; master <= 200; ++master) {
        spky::Instrument inst;
        inst.init(48000.f);
        Flow f;
        f.init(&inst, 100.f);
        TerrainState st; st.master = master;
        f.wake(st);
        for (int i = 0; i < 50; ++i) f.tick();
        CAPTURE(master);
        CHECK(inst.step_on(spky::PART_A) == inst.synced(spky::PART_A));
        CHECK(inst.step_on(spky::PART_B) == inst.synced(spky::PART_B));
        if (inst.step_on(spky::PART_A) || inst.step_on(spky::PART_B)) saw_step = true;
        if (!inst.step_on(spky::PART_A) || !inst.step_on(spky::PART_B)) saw_flow = true;
    }
    // A guard that only ever sees false == false is vacuous -- it would also
    // pass an implementation that never turns STEP on at all. Prove some
    // master in this range actually reaches STEP mode.
    //
    // saw_flow is the twin of that, and specifically it is the SHIPPED BUG'S
    // twin: an implementation that drew STEP for every terrain would satisfy
    // saw_step AND the equality above (it would merely be synced), so without
    // this the case would pass the very thing P_MODE exists to end.
    CHECK(saw_step);
    CHECK(saw_flow);

    // The equality above only samples SETTLED state, after wake(). The claim
    // is "no reachable tick", and a blend is 600 more reachable ticks in which
    // the clocking flip and the step push could in principle be issued apart.
    // Walk a full kBlendS ramp across a mode-CHANGING press and assert on
    // every one of them.
    {
        TerrainState flow_t, step_t;
        bool have_flow = false, have_step = false;
        for (uint32_t master = 1; master < 500; ++master) {
            TerrainState s; s.master = master;
            if (generate(s).base[P_MODE] > 0.5f) {
                if (!have_step) { step_t = s; have_step = true; }
            } else if (!have_flow) { flow_t = s; have_flow = true; }
            if (have_flow && have_step) break;
        }
        REQUIRE(have_flow);
        REQUIRE(have_step);

        spky::Instrument inst;
        inst.init(48000.f);
        Flow f;
        f.init(&inst, 100.f);
        f.wake(flow_t);
        f.restore_undo(step_t, true);
        REQUIRE(f.undo());                     // blends flow_t -> step_t
        const int ticks = int(kBlendS * 100.f) + 20;   // a full ramp, and past it
        for (int i = 0; i < ticks; ++i) {
            f.tick();
            CAPTURE(i);
            CHECK(inst.step_on(spky::PART_A) == inst.synced(spky::PART_A));
            CHECK(inst.step_on(spky::PART_B) == inst.synced(spky::PART_B));
        }
        CHECK(inst.step_on(spky::PART_A));     // the press really moved the mode
    }
}

TEST_CASE("flow mode: the drawn step counts reach the instrument, per deck") {
    // push_mode_and_steps is now the ONLY path by which P_STEPS_A/B reach the
    // engine -- apply_param drops them on the floor (flow_params.h). Nothing
    // else in the suite observes a step count on the INSTRUMENT side of that
    // boundary: test_flow_new and test_flow_audio read param_now(P_STEPS_*),
    // which is the FLOW side. So swapping sa and sb in push_mode_and_steps, or
    // pushing a constant, would leave the whole suite green.
    bool saw_differing = false;
    for (uint32_t master = 1; master <= 200; ++master) {
        spky::Instrument inst;
        inst.init(48000.f);
        Flow f;
        f.init(&inst, 100.f);
        TerrainState st; st.master = master;
        f.wake(st);
        for (int i = 0; i < 50; ++i) f.tick();
        const int want_a = int(f.param_now(P_STEPS_A) + 0.5f);
        const int want_b = int(f.param_now(P_STEPS_B) + 0.5f);
        CAPTURE(master); CAPTURE(want_a); CAPTURE(want_b);
        CHECK(inst.deck_steps(spky::PART_A) == want_a);
        CHECK(inst.deck_steps(spky::PART_B) == want_b);
        if (want_a != want_b) saw_differing = true;
    }
    // Load-bearing: if every terrain in the range drew the SAME count on both
    // decks, the two CHECKs above would hold just as well against a push that
    // had sa and sb the wrong way round.
    CHECK(saw_differing);
}

TEST_CASE("flow mode: the draw follows kModeW per archetype") {
    int n[ARCH_COUNT] = {}, step[ARCH_COUNT] = {};
    for (uint32_t master = 1; master <= 4000; ++master) {
        TerrainState st; st.master = master;
        const Terrain t = generate(st);
        // The value is a clean discrete, never something in between.
        CHECK((t.base[P_MODE] == 0.f || t.base[P_MODE] == 1.f));
        n[t.arch]++;
        if (t.base[P_MODE] > 0.5f) step[t.arch]++;
    }
    int judged = 0;
    for (int a = 0; a < ARCH_COUNT; ++a) {
        if (n[a] < 100) continue;              // too few to judge
        ++judged;
        const float got = float(step[a]) / float(n[a]);
        CAPTURE(a); CAPTURE(got); CAPTURE(kModeW[a]);
        CHECK(got > kModeW[a] - 0.08f);
        CHECK(got < kModeW[a] + 0.08f);
    }
    // Without this the skip above is silent: an archetype that stopped being
    // drawn at all (a kArchWeight edit, a stage-0 regression) would take its
    // whole tolerance check out of the suite and the case would stay green.
    // Every archetype must have been sampled often enough to judge.
    CHECK(judged == ARCH_COUNT);
}

TEST_CASE("flow mode: P_MODE is scheduled with the texture deck, not the carrier") {
    // P_MODE belongs to no deck, so the deckless fall-through in
    // switch_phase_for would ride it with the CARRIER at kCarrierStaggerFrac.
    // It must not: set_sync is global, so the mode switch flips BOTH decks'
    // rate mapping at once, and riding the carrier would jump the texture
    // deck's clocking 1.5 s after that deck's own duck had already closed --
    // exactly what the stagger exists to prevent.
    spky::Instrument inst;
    inst.init(48000.f);
    Flow f;
    f.init(&inst, 100.f);
    bool saw_a_carries = false, saw_b_carries = false;
    for (uint32_t master = 1; master <= 40; ++master) {
        TerrainState st; st.master = master;
        f.wake(st);
        CAPTURE(master);
        CHECK(f.switch_phase_for_test(P_MODE) == 0.f);
        // The fall-through itself is still there for the params it IS for
        // (SCALE and ROOT ride the carrier so tonality lands with the lead
        // voice). Without this the case would also pass against an
        // implementation that had simply deleted the stagger.
        CHECK(f.switch_phase_for_test(P_SCALE) == kCarrierStaggerFrac);
        if (terrain_of(f).a_carries) saw_a_carries = true;
        else                         saw_b_carries = true;
    }
    // Both carrier roles occur in that range, so the P_MODE answer above is
    // pinned against a real _carrier_deck of 0 AND of 1.
    CHECK(saw_a_carries);
    CHECK(saw_b_carries);
}

TEST_CASE("flow mode: the clocking flip lands at the press, ducked on both decks") {
    // A mode change is a whole-terrain event, not a per-deck one: set_sync is
    // global, so it goes at phase 0 and BOTH decks are ducked there. The
    // stagger itself survives -- the carrier keeps its own duck at
    // kCarrierStaggerFrac for its engine switch, and simply gets a second one.
    spky::Instrument inst;
    inst.init(48000.f);
    Flow f;
    f.init(&inst, 100.f);

    // Two FLOW-mode terrains (for the same-mode contrast) and one STEP-mode one.
    TerrainState flow_a, flow_b, step_t;
    int n_flow = 0;
    bool have_step = false;
    for (uint32_t master = 1; master < 500; ++master) {
        TerrainState s; s.master = master;
        const bool step = generate(s).base[P_MODE] > 0.5f;
        if (step) { if (!have_step) { step_t = s; have_step = true; } }
        else if (n_flow == 0) { flow_a = s; ++n_flow; }
        else if (n_flow == 1) { flow_b = s; ++n_flow; }
        if (have_step && n_flow == 2) break;
    }
    REQUIRE(have_step);
    REQUIRE(n_flow == 2);

    // An instant (unblended) terrain change really moves the mode -- without
    // this the blended checks below could not tell a working schedule from a
    // P_MODE that never reaches the instrument at all.
    f.wake(flow_a);
    for (int i = 0; i < 20; ++i) f.tick();
    CHECK_FALSE(inst.step_on(spky::PART_A));
    f.wake(step_t);
    f.tick();
    CHECK(inst.step_on(spky::PART_A));

    // The blended path: the mode must be live within ONE tick of the press,
    // not a quarter of the ramp later.
    f.wake(flow_a);
    for (int i = 0; i < 20; ++i) f.tick();
    REQUIRE_FALSE(inst.step_on(spky::PART_A));
    f.restore_undo(step_t, true);
    const double press = f.now_s();
    REQUIRE(f.undo());                       // blends toward step_t
    f.tick();
    CHECK(f.blend_phase() < kCarrierStaggerFrac);
    CHECK(inst.step_on(spky::PART_A));
    CHECK(inst.synced(spky::PART_A));
    CHECK(inst.step_on(spky::PART_B));

    // The clocking flip hits both decks at the press, so both are ducked
    // THERE -- the texture deck by its only duck, the carrier by its second
    // one. The carrier's first duck stays at the stagger, where its own engine
    // switch still is. (Exact == is safe: begin_blend assigns _t and
    // _t + kCarrierStaggerFrac * kBlendS verbatim, with no arithmetic in
    // between, so these are the same doubles -- not a tolerance question.)
    const int carrier = terrain_of(f).a_carries ? 0 : 1;
    const int texture = 1 - carrier;
    CHECK(f.duck_t_for_test(texture, 0) == press);
    CHECK(f.duck_t_for_test(texture, 1) < 0.0);          // slot unused
    CHECK(f.duck_t_for_test(carrier, 1) == press);
    CHECK(f.duck_t_for_test(carrier, 0)
          == press + double(kCarrierStaggerFrac * kBlendS));

    // Contrast: a press that does NOT move the mode leaves the carrier's
    // second slot empty, so the extra duck above is the mode's doing and not
    // something every press now gets.
    f.wake(flow_a);
    for (int i = 0; i < 20; ++i) f.tick();
    f.restore_undo(flow_b, true);
    const double press2 = f.now_s();
    REQUIRE(f.undo());
    const int carrier2 = terrain_of(f).a_carries ? 0 : 1;
    CHECK(f.duck_t_for_test(carrier2, 1) < 0.0);
    CHECK(f.duck_t_for_test(1 - carrier2, 0) == press2);
    CHECK(f.duck_t_for_test(carrier2, 0)
          == press2 + double(kCarrierStaggerFrac * kBlendS));
}

TEST_CASE("flow mode: a mode-changing press ducks the carrier at BOTH its events") {
    // The defect this guards: giving P_MODE phase 0 without giving the carrier
    // deck a duck there. The carrier's only duck sits at kCarrierStaggerFrac,
    // 1.5 s away, where duck() computes u = 1.5 / 0.25 = 6 and returns the
    // send untouched -- so the global set_sync clocking flip would land in the
    // open. Collapsing the stagger instead is worse: it moves the carrier's
    // ENGINE change into the open, which is louder. Both events get a duck.
    spky::Instrument in;
    in.init(48000.f);
    Flow f;
    f.init(&in, 100.f);
    // Seed picked so the press MOVES the mode and the carrier changes engine:
    // without both, neither half of this case is observable.
    TerrainState s; s.master = 0xC0FFEE; f.wake(s);
    const int eng_p[2] = { P_ENGINE_A, P_ENGINE_B };
    const int mix_p[2] = { P_REVMIX_A, P_REVMIX_B };
    const float eng0[2] = { f.param_now(P_ENGINE_A), f.param_now(P_ENGINE_B) };
    const float mix0[2] = { f.param_now(P_REVMIX_A), f.param_now(P_REVMIX_B) };
    const bool  mode0   = f.param_now(P_MODE) > 0.5f;

    f.new_full();
    const Terrain& nt = terrain_of(f);
    const int carrier = nt.a_carries ? 0 : 1;
    REQUIRE((nt.base[P_MODE] > 0.5f) != mode0);          // the mode moves
    REQUIRE(nt.base[eng_p[carrier]] != eng0[carrier]);   // and so does ENGINE

    // t = 0.01 s: the clocking flip is already live (P_MODE switches at phase
    // 0) and the CARRIER's send is ducked for it, even though its own engine
    // has not switched yet.
    f.tick();
    CHECK(in.step_on(spky::PART_A) == (nt.base[P_MODE] > 0.5f));
    CHECK(f.param_now(eng_p[carrier]) == doctest::Approx(eng0[carrier]));
    CHECK(f.param_now(mix_p[carrier]) > mix0[carrier] + 1e-3f);

    // t = 1.00 s: between the two windows (press duck spans -0.25..0.25, the
    // stagger duck 1.25..1.75), so this sample is duck-free -- the baseline.
    for (int i = 0; i < 99; ++i) f.tick();
    const float carrier_mix_pre = f.param_now(mix_p[carrier]);

    // t = 1.52 s: the carrier's own engine switch, still staggered, still
    // under its own duck. The stagger is a by-ear decision and survives.
    for (int i = 0; i < 52; ++i) f.tick();
    CHECK(f.blend_phase() > kCarrierStaggerFrac);
    CHECK(f.param_now(eng_p[carrier]) == doctest::Approx(nt.base[eng_p[carrier]]));
    CHECK(f.param_now(mix_p[carrier]) > carrier_mix_pre + 1e-3f);
}
