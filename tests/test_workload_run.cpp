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

TEST_CASE("a guard trip marks the run until the next start") {
    WorkloadRun r;
    r.on_guard_trip();               // no run: nothing to mark
    CHECK_FALSE(r.tripped());
    r.start(4, 0, 500);
    CHECK_FALSE(r.tripped());
    r.on_guard_trip();               // in the settle or counted: the same
    CHECK(r.tripped());
    CHECK_FALSE(r.void_run());       // a separate flag from a pot take
    r.start(4, 900, 500);            // only a new start clears it
    CHECK_FALSE(r.tripped());
    r.on_guard_trip();
    r.stop();
    CHECK_FALSE(r.tripped());
}

// ---- the measurement image (Task 6 fix round 1) ------------------------------
TEST_CASE("overrun guard: reset_streak() restarts the count and keeps the latch") {
    OverrunGuard g(1000, 3);
    CHECK_FALSE(g.block(2000));
    CHECK_FALSE(g.block(2000));
    g.reset_streak();                 // bit 31 flipped: the frozen streak goes
    CHECK_FALSE(g.block(2000));
    CHECK_FALSE(g.block(2000));       // two fresh blocks, not four stale ones
    CHECK(g.block(2000));             // the third in a row
    g.reset_streak();
    CHECK(g.tripped());               // a latched guard stays latched
    CHECK(g.block(10));
}

TEST_CASE("TOG settle ends 200 blocks after the TOG's block, once") {
    shell::TogSettle s(200);
    CHECK_FALSE(s.ends(5));           // nothing armed
    s.arm(1000);                      // the TOG lands in block 1000
    // remote_block() asks in the TOG's own block too, after the command.
    CHECK_FALSE(s.ends(1000));
    bool early = false;
    for(uint32_t b = 1001; b < 1200; ++b) early = early || s.ends(b);
    CHECK_FALSE(early);               // 1000..1199 are the switch-over
    CHECK(s.ends(1200));
    CHECK_FALSE(s.ends(1201));        // once
    // A second TOG before the first settled re-arms it.
    s.arm(2000);
    CHECK_FALSE(s.ends(2000));
    for(uint32_t b = 2001; b < 2100; ++b) (void)s.ends(b);
    s.arm(2100);
    CHECK_FALSE(s.ends(2100));
    early = false;
    for(uint32_t b = 2101; b < 2300; ++b) early = early || s.ends(b);
    CHECK_FALSE(early);
    CHECK(s.ends(2300));
    // Across the block counter's wrap.
    s.arm(0xFFFFFFF0u);
    CHECK_FALSE(s.ends(0xFFFFFFF0u + 199u));
    CHECK(s.ends(0xFFFFFFF0u + 200u));
}

TEST_CASE("a TOG in the counted blocks re-settles the run under a new gen") {
    WorkloadRun r;
    r.start(3, 0, 500);
    const uint32_t g0 = r.gen();
    CHECK(r.counted(600) == 100u);
    r.resettle(600, 200);
    CHECK(r.gen() == g0 + 1u);
    CHECK(r.id() == 3);
    CHECK(r.active());
    CHECK_FALSE(r.counting(600));
    CHECK_FALSE(r.counting(799));
    CHECK(r.settle_ends(800));
    CHECK(r.counting(800));
    // The gesture clock resumes where it paused, not at 0.
    CHECK(r.counted(800) == 100u);
    CHECK(r.counted(803) == 103u);
}

TEST_CASE("a TOG inside a longer settle keeps it; a late one extends it") {
    WorkloadRun r;
    r.start(5, 100, 2500);            // a SAMPLER run: counts from 2600
    const uint32_t g0 = r.gen();
    r.resettle(101, 200);             // the TOG right behind the LOAD
    CHECK(r.gen() == g0 + 1u);
    CHECK_FALSE(r.counting(2599));
    CHECK(r.settle_ends(2600));
    CHECK(r.counted(2600) == 0u);
    r.start(5, 100, 500);             // counts from 600
    r.resettle(550, 200);             // late in the settle: from 750
    CHECK_FALSE(r.counting(600));
    CHECK_FALSE(r.settle_ends(600));
    CHECK_FALSE(r.counting(749));
    CHECK(r.settle_ends(750));
    CHECK(r.counted(750) == 0u);
    // Across the wrap: the LOAD's settle end (past 0) is later than the TOG's.
    r.start(6, 0xFFFFFF00u, 500);     // counts from 0xF4
    r.resettle(0xFFFFFF10u, 200);
    CHECK_FALSE(r.counting(0xF3u));
    CHECK(r.settle_ends(0xF4u));
}

TEST_CASE("a TOG keeps the run's void and trip, and needs a run") {
    WorkloadRun r;
    const uint32_t g0 = r.gen();
    r.resettle(10, 200);              // no run: nothing changes
    CHECK(r.gen() == g0);
    CHECK_FALSE(r.active());
    CHECK_FALSE(r.counting(1000));
    r.start(2, 0, 500);
    r.on_pot_take();
    r.on_guard_trip();
    r.resettle(700, 200);
    CHECK(r.void_run());
    CHECK(r.tripped());
    r.start(2, 1000, 500);            // a new start clears the paused count too
    CHECK(r.counted(1500) == 0u);
    CHECK_FALSE(r.void_run());
}

TEST_CASE("outside-window split: out keeps the probe tail, ext leaves it out") {
    const shell::OutSplit s = shell::out_split(1000000, 900000, 7000);
    CHECK(s.out == 100000u);
    CHECK(s.ext == 93000u);
    // Saturating: busy under cb (or under cb + tail) is 0, never a wrapped figure.
    CHECK(shell::out_split(800, 900, 50).out == 0u);
    CHECK(shell::out_split(800, 900, 50).ext == 0u);
    CHECK(shell::out_split(920, 900, 50).out == 20u);
    CHECK(shell::out_split(920, 900, 50).ext == 0u);
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
