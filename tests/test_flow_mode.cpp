// tests/test_flow_mode.cpp
#include "doctest/doctest.h"
#include "flow/taste.h"
#include "flow/flow_params.h"
#include "flow/flow.h"
#include "flow/terrain.h"
#include "instrument.h"
using namespace spky::flow;

TEST_CASE("flow mode: P_MODE is the last parameter") {
    // Base draws are keyed kStreamParamBase + param (terrain.cpp). If P_MODE
    // is not last, every parameter after it gets a different RNG stream and
    // every existing terrain code resolves to a different sound.
    CHECK(P_MODE == P_COUNT - 1);
    CHECK(kParams[P_MODE].lo == 0.f);
    CHECK(kParams[P_MODE].hi == 1.f);
    CHECK(kParams[P_MODE].steps == 2);
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
    bool saw_step = false;
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
    }
    // A guard that only ever sees false == false is vacuous -- it would also
    // pass an implementation that never turns STEP on at all. Prove some
    // master in this range actually reaches STEP mode.
    CHECK(saw_step);
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
    for (int a = 0; a < ARCH_COUNT; ++a) {
        if (n[a] < 100) continue;              // too few to judge
        const float got = float(step[a]) / float(n[a]);
        CAPTURE(a); CAPTURE(got); CAPTURE(kModeW[a]);
        CHECK(got > kModeW[a] - 0.08f);
        CHECK(got < kModeW[a] + 0.08f);
    }
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

TEST_CASE("flow mode: a mode change ducks both decks at the press") {
    // A mode change is a whole-terrain event, not a per-deck one: it goes at
    // phase 0 and BOTH ducks open there, so neither deck's clocking flips in
    // the open.
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
    // Both ducks are centred on the press instant, so the switch happens
    // inside a wash on both decks.
    CHECK(f.duck_t_for_test(0) == press);
    CHECK(f.duck_t_for_test(1) == press);

    // Contrast: a press that does NOT move the mode keeps the stagger, so the
    // collapse above is the mode's doing and not a deleted stagger.
    f.wake(flow_a);
    for (int i = 0; i < 20; ++i) f.tick();
    f.restore_undo(flow_b, true);
    const double press2 = f.now_s();
    REQUIRE(f.undo());
    CHECK(f.duck_t_for_test(0) != f.duck_t_for_test(1));
    CHECK((f.duck_t_for_test(0) == press2 || f.duck_t_for_test(1) == press2));
}

