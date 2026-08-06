// tests/test_flow_mode.cpp
#include "doctest/doctest.h"
#include "flow/taste.h"
#include "flow/flow_params.h"
#include "flow/flow.h"
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
    }
}
