#include "doctest/doctest.h"

#include "synth/synth_engine.h"
#include "synth_engine_contract.h"

using namespace spky;

TEST_CASE("wave engine satisfies the shared part-engine contract") {
    contract_round_robin_and_steal<WaveEngine>();
    contract_flow_drone_and_surface<WaveEngine>();
    contract_chord_surface_and_hold<WaveEngine>();
    contract_deterministic_seed<WaveEngine>();
}

TEST_CASE("wave engine TIMBRE scans the bank instead of analog shapes") {
    WaveEngine dark;
    WaveEngine vocal;
    dark.init(48000.f);
    vocal.init(48000.f);
    float td[LANE_COUNT] = {0.f, 1.f, 0.45f, 0.f, 1.f};
    float tv[LANE_COUNT] = {0.35f, 1.f, 0.45f, 0.f, 1.f};
    dark.set_targets(td, 0.5f);
    vocal.set_targets(tv, 0.5f);
    dark.trigger(0.35f);
    vocal.trigger(0.35f);
    bool differs = false;
    for (int i = 0; i < 4096; ++i) {
        float dl, dr, vl, vr;
        dark.process(dl, dr);
        vocal.process(vl, vr);
        if (dl != vl || dr != vr) differs = true;
    }
    CHECK(differs);
}
