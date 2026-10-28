// Panel-state persistence: the save policy (quiet period, rate floor) and the
// boot overlay (init patch + record minus the spec 4.3 overrides).
// Spec: docs/superpowers/specs/2026-10-28-panel-state-persistence-design.md
#include <doctest/doctest.h>
#include <cstring>
#include "../shell/persist_policy.h"
#include "../shell/controls.h"

using namespace shell;

namespace {
// "change at t" = one call with changed=true at t.
bool at(SavePolicy& s, bool changed, uint32_t t) { return save_due(s, changed, t); }
}

TEST_CASE("policy: MODBTN/REC-only change never saves") {
    float a[ffctl::NUM_PARAMS], b[ffctl::NUM_PARAMS];
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) a[i] = b[i] = 0.25f;
    CHECK_FALSE(persist_differs(a, b));
    b[ffctl::REC_A] = 1.f; b[ffctl::REC_B] = 1.f; b[ffctl::MODBTN] = 1.f;
    CHECK_FALSE(persist_differs(a, b));
    b[ffctl::DEPTH_A] = 0.5f;
    CHECK(persist_differs(a, b));
}

TEST_CASE("policy: quiet 15 s then save") {
    SavePolicy s;
    CHECK_FALSE(at(s, true, 0));
    CHECK_FALSE(at(s, false, 14999));
    CHECK(at(s, false, 15000));
}

TEST_CASE("policy: 5 min floor") {
    SavePolicy s;
    save_done(s, 20000);
    CHECK_FALSE(at(s, true, 30000));
    CHECK_FALSE(at(s, false, 45000));
    CHECK_FALSE(at(s, false, 319999));
    CHECK(at(s, false, 320000));
}

TEST_CASE("policy: repeated quiet periods save once") {
    SavePolicy s;
    at(s, true, 0);
    CHECK(at(s, false, 15000));
    save_done(s, 15000);
    for(uint32_t t = 15500; t < 15000 + 3600000u; t += 500)
        CHECK_FALSE(at(s, false, t));
}

TEST_CASE("policy: change during quiet restarts the 15 s") {
    SavePolicy s;
    at(s, true, 0);
    at(s, true, 10000);
    CHECK_FALSE(at(s, false, 15000));
    CHECK_FALSE(at(s, false, 24999));
    CHECK(at(s, false, 25000));
}

TEST_CASE("policy: millis wrap") {
    SavePolicy s;
    at(s, true, 0xFFFFF000u);
    CHECK_FALSE(at(s, false, 0x00002A97u));
    CHECK(at(s, false, 0x00002A98u));
}

TEST_CASE("overlay: no record is the init patch") {
    float k[ffctl::NUM_PARAMS];
    bool ex[2] = {true, true};
    boot_overlay(nullptr, k, ex);
    CHECK(std::memcmp(k, ffctl::kInitParamDefaults, sizeof k) == 0);
    CHECK_FALSE(ex[0]);
    CHECK_FALSE(ex[1]);
}

TEST_CASE("overlay: overrides") {
    PersistRecord r{};
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) r.knobs[i] = 0.123f;
    r.knobs[ffctl::REC_A] = 1.f; r.knobs[ffctl::REC_B] = 1.f; r.knobs[ffctl::MODBTN] = 1.f;
    r.knobs[ffctl::MODD_DEPTH_A] = 0.7f;
    r.excite[0] = 1;
    float k[ffctl::NUM_PARAMS];
    bool ex[2];
    boot_overlay(&r, k, ex);
    CHECK(k[ffctl::REC_A] == ffctl::kInitParamDefaults[ffctl::REC_A]);
    CHECK(k[ffctl::REC_B] == ffctl::kInitParamDefaults[ffctl::REC_B]);
    CHECK(k[ffctl::MODBTN] == ffctl::kInitParamDefaults[ffctl::MODBTN]);
    CHECK(k[ffctl::MODD_DEPTH_A] == 0.7f);
    CHECK(k[ffctl::DEPTH_A] == 0.123f);
    CHECK(ex[0]);
    CHECK_FALSE(ex[1]);
}

TEST_CASE("overlay: excite only on BBD") {
    float k[ffctl::NUM_PARAMS] = {};
    k[ffctl::ENGINE_A] = static_cast<float>(kEngineSlotBbd);
    k[ffctl::ENGINE_B] = static_cast<float>(kEngineSlotBbd - 1);
    const bool rec[2] = {true, true};
    bool ex[2];
    excite_after_take(rec, k, ex);
    CHECK(ex[0]);
    CHECK_FALSE(ex[1]);
}
