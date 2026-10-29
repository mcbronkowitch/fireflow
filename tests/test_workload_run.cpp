// The remote CPU hunt's board-side workload logic (plan 2026-10-29-cpu-hunt-
// remote Task 5, spec section 5.2/5.3): the overrun guard, the run's settle
// and void rules, the load's knob vector, and a gesture step through the pot
// and key paths. Pure logic; panel_scan.cpp wires it under SHELL_REMOTE.
#include <doctest/doctest.h>
#include <string>
#include "../shell/boot_patch.h"
#include "../shell/controls.h"
#include "../shell/generated_boot_patch_noon.h"
#include "../shell/generated_panel_map.h"
#include "../shell/generated_workloads.h"
#include "../shell/workload_run.h"
#include "control/params.hpp"

using shell::OverrunGuard;
using shell::WorkloadRun;

TEST_CASE("overrun guard trips on the 8th consecutive block") {
    OverrunGuard g(shell::kGuardLimitCycles, shell::kGuardBlocks);
    CHECK(shell::kGuardLimitCycles == 950000u);
    CHECK(shell::kGuardBlocks == 8u);
    for(int i = 0; i < 7; ++i) CHECK_FALSE(g.block(951000));
    CHECK_FALSE(g.tripped());
    CHECK_FALSE(g.block(900000));   // one block under the limit resets the streak
    for(int i = 0; i < 7; ++i) CHECK_FALSE(g.block(951000));
    CHECK(g.block(951000));          // the 8th in a row
    CHECK(g.tripped());
    // Latched: cheap blocks (the silenced callback) do not untrip it.
    CHECK(g.block(1000));
    CHECK(g.tripped());
    g.clear();
    CHECK_FALSE(g.tripped());
    // Exactly the limit is not over it.
    for(int i = 0; i < 20; ++i) CHECK_FALSE(g.block(950000));
}

TEST_CASE("workload run settles 500 blocks") {
    WorkloadRun r;
    CHECK_FALSE(r.active());
    CHECK_FALSE(r.counting(0));
    const uint32_t start = 1234;
    r.start(7, start, 500);
    CHECK(r.active());
    CHECK(r.id() == 7);
    CHECK_FALSE(r.counting(start));
    CHECK_FALSE(r.counting(start + 499));
    CHECK(r.counting(start + 500));
    CHECK(r.counting(start + 100000));
    CHECK_FALSE(r.settle_ends(start + 499));
    CHECK(r.settle_ends(start + 500));
    CHECK_FALSE(r.settle_ends(start + 501));
    CHECK(r.counted(start + 500) == 0u);
    CHECK(r.counted(start + 503) == 3u);
}

TEST_CASE("workload run settles 2500 blocks with a sampler deck") {
    WorkloadRun r;
    const uint32_t start = 10;
    r.start(3, start, 2500);
    CHECK_FALSE(r.counting(start + 500));
    CHECK_FALSE(r.counting(start + 2499));
    CHECK(r.counting(start + 2500));
    CHECK(r.settle_ends(start + 2500));
}

TEST_CASE("workload run counts across the block counter's wrap") {
    WorkloadRun r;
    const uint32_t start = 0xFFFFFF00u;
    r.start(1, start, 500);
    CHECK_FALSE(r.counting(start + 499u));   // wraps past 0
    CHECK(r.counting(start + 500u));
}

TEST_CASE("pot take voids the run") {
    WorkloadRun r;
    r.on_pot_take();                 // no run: nothing to void
    CHECK_FALSE(r.void_run());
    r.start(2, 0, 500);
    CHECK_FALSE(r.void_run());
    r.on_pot_take();
    CHECK(r.void_run());
    const uint32_t g = r.gen();
    r.start(2, 600, 500);            // a new start clears it
    CHECK_FALSE(r.void_run());
    CHECK(r.gen() != g);             // and is a different run
    r.stop();
    CHECK_FALSE(r.active());
    CHECK_FALSE(r.counting(100000));
    CHECK(r.id() == -1);
}

TEST_CASE("workload knobs: init patch, the rows through the panel path, REC latched") {
    // r4_noon: the hand entry, identical to the all-noon boot patch.
    const shell::WorkloadDef* w = nullptr;
    for(int i = 0; i < shell::kWorkloadCount; ++i)
        if(std::string(shell::workload_by_id(i)->name) == "r4_noon") w = shell::workload_by_id(i);
    REQUIRE(w != nullptr);
    float got[ffctl::NUM_PARAMS], want[ffctl::NUM_PARAMS];
    REQUIRE(shell::workload_knobs(*w, shell::kRevaTable, got));
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) want[i] = ffctl::kInitParamDefaults[i];
    shell::apply_boot_patch_rows(shell::kRevaTable, shell::kBootPatchNoonRows,
                                 shell::kRevaTable.count, want);
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) CHECK(got[i] == want[i]);

    // A sampler workload latches REC on its sampler deck(s), nothing else.
    const shell::WorkloadDef* s = nullptr;
    for(int i = 0; i < shell::kWorkloadCount && s == nullptr; ++i)
        if(shell::workload_by_id(i)->rec[0] && !shell::workload_by_id(i)->rec[1])
            s = shell::workload_by_id(i);
    REQUIRE(s != nullptr);
    REQUIRE(shell::workload_knobs(*s, shell::kRevaTable, got));
    CHECK(got[ffctl::REC_A] == 1.0f);
    CHECK(got[ffctl::REC_B] == 0.0f);
    CHECK(got[ffctl::MODBTN] == 0.0f);

    // The table must match the vector.
    const shell::ControlTable shorter{shell::kRevaTable.entries, shell::kRevaTable.count - 1};
    CHECK_FALSE(shell::workload_knobs(*w, shorter, got));
}

TEST_CASE("workload table: ids in range, out of range is null") {
    CHECK(shell::kWorkloadCount == 145);
    CHECK(shell::kWorkloadRowCount == shell::kRevaTable.count);
    CHECK(shell::workload_by_id(-1) == nullptr);
    CHECK(shell::workload_by_id(shell::kWorkloadCount) == nullptr);
    CHECK(shell::workload_by_id(0) != nullptr);
    int n = -5;
    CHECK(shell::gesture_by_id(-1, &n) == nullptr);
    CHECK(shell::gesture_by_id(shell::kGestureCount, &n) == nullptr);
    const shell::GestureStep* st = shell::gesture_by_id(0, &n);
    REQUIRE(st != nullptr);
    CHECK(n > 0);
    CHECK(n <= shell::kGestureMaxSteps);
    // Every gesture's base is a workload, and its steps are in block order.
    for(int g = 0; g < shell::kGestureCount; ++g)
    {
        const shell::GestureDef* d = shell::gesture_def(g);
        REQUIRE(d != nullptr);
        CHECK(shell::workload_by_id(d->base) != nullptr);
        const shell::GestureStep* s = shell::gesture_by_id(g, &n);
        for(int i = 1; i < n; ++i) CHECK(s[i - 1].block <= s[i].block);
        CHECK(s[n - 1].block < d->blocks);
    }
}

TEST_CASE("gesture step: a pot row goes through the MOD-aware pot path") {
    float knobs[ffctl::NUM_PARAMS];
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) knobs[i] = ffctl::kInitParamDefaults[i];
    // Row 3 is RATE_A (workloads.json `rows`).
    REQUIRE(shell::kRevaTable.entries[3].param == ffctl::RATE_A);
    const shell::GestureStep up{0, 3, 750};
    CHECK_FALSE(shell::apply_gesture_step(up, shell::kRevaTable, shell::kRevaKeyMap, knobs));
    CHECK(knobs[ffctl::RATE_A] == doctest::Approx(shell::knob_from_pot(ffctl::RATE_A, 0.75f)));

    // MOD press (row -1) latches MOD; the same pot row then writes the depth twin.
    const shell::GestureStep mod{4, shell::kGestureRowMod, 0};
    CHECK_FALSE(shell::apply_gesture_step(mod, shell::kRevaTable, shell::kRevaKeyMap, knobs));
    CHECK(knobs[ffctl::MODBTN] == 1.0f);
    const float rate_before = knobs[ffctl::RATE_A];
    const int   depth       = shell::mod_depth_of(ffctl::RATE_A);
    REQUIRE(depth >= 0);
    const shell::GestureStep twin{8, 3, 1000};
    CHECK_FALSE(shell::apply_gesture_step(twin, shell::kRevaTable, shell::kRevaKeyMap, knobs));
    CHECK(knobs[ffctl::RATE_A] == rate_before);
    CHECK(knobs[depth] == doctest::Approx(shell::knob_from_pot(depth, 1.0f)));

    // SHIFT + MOD (row -2) clears the layer and says so; MOD stays latched.
    const shell::GestureStep clr{12, shell::kGestureRowShiftMod, 0};
    CHECK(shell::apply_gesture_step(clr, shell::kRevaTable, shell::kRevaKeyMap, knobs));
    CHECK(knobs[depth] == 0.0f);
    CHECK(knobs[ffctl::MODBTN] == 1.0f);

    // A row outside the table does nothing.
    const shell::GestureStep bad{16, 99, 500};
    float before[ffctl::NUM_PARAMS];
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) before[i] = knobs[i];
    CHECK_FALSE(shell::apply_gesture_step(bad, shell::kRevaTable, shell::kRevaKeyMap, knobs));
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) CHECK(knobs[i] == before[i]);
}
