// SD probe schedules, the pot hold and the grain grant. Pure data logic, no
// hardware type.
// Spec: docs/superpowers/specs/2026-10-31-sd-use-cases-tests-design.md
// section 5.
#include <doctest/doctest.h>
#include "../shell/sd_schedule.h"

TEST_CASE("burst_phase: idle windows, then the 83 ms and 41 ms phases") {
    for (uint32_t w = 0; w < 10; ++w) CHECK_FALSE(shell::burst_phase(w).on);

    auto p = shell::burst_phase(10);
    CHECK(p.on);
    CHECK(p.period_ms == 83);
    CHECK(p.tail == 0);

    p = shell::burst_phase(30);
    CHECK(p.on);
    CHECK(p.period_ms == 83);
    CHECK(p.tail == 1);

    p = shell::burst_phase(50);
    CHECK(p.on);
    CHECK(p.period_ms == 83);
    CHECK(p.tail == 2);

    p = shell::burst_phase(70);
    CHECK(p.on);
    CHECK(p.period_ms == 41);
    CHECK(p.tail == 0);

    p = shell::burst_phase(110);
    CHECK(p.on);
    CHECK(p.period_ms == 41);
    CHECK(p.tail == 2);

    CHECK_FALSE(shell::burst_phase(20).on);     // even phase
    CHECK_FALSE(shell::burst_phase(120).on);    // wraps to phase 0
    CHECK(shell::burst_phase(130).on);          // and the cycle repeats
    CHECK(shell::burst_phase(130).period_ms == 83);
}

TEST_CASE("SdHold: a transfer drops the block and the tail after it") {
    shell::SdHold h;
    CHECK(h.step(true, 2));      // block 0: the transfer
    CHECK(h.step(false, 2));     // block 1
    CHECK(h.step(false, 2));     // block 2
    CHECK_FALSE(h.step(false, 2));   // block 3: back to normal
    CHECK_FALSE(h.step(false, 2));
}

TEST_CASE("SdHold: tail 0 drops only the transfer block") {
    shell::SdHold h;
    CHECK(h.step(true, 0));
    CHECK_FALSE(h.step(false, 0));
    CHECK_FALSE(h.step(false, 0));
}

TEST_CASE("SdHold: a transfer during the countdown restarts it") {
    shell::SdHold h;
    CHECK(h.step(true, 2));      // left = 2
    CHECK(h.step(false, 2));     // left = 1
    CHECK(h.step(true, 2));      // restart: left = 2
    CHECK(h.step(false, 2));
    CHECK(h.step(false, 2));
    CHECK_FALSE(h.step(false, 2));
}

TEST_CASE("rgrain_n: the 20-frame sequence repeats") {
    CHECK(shell::rgrain_n(0) == 0);
    CHECK(shell::rgrain_n(19) == 0);
    CHECK(shell::rgrain_n(20) == 8);
    CHECK(shell::rgrain_n(40) == 2);
    CHECK(shell::rgrain_n(60) == 4);
    CHECK(shell::rgrain_n(80) == 4);
    CHECK(shell::rgrain_n(100) == 2);
    CHECK(shell::rgrain_n(120) == 8);
    CHECK(shell::rgrain_n(159) == 0);
    CHECK(shell::rgrain_n(160) == 0);       // wraps
    CHECK(shell::rgrain_n(180) == 8);
}

TEST_CASE("rgrain_cap_on: 60-frame phases, off first") {
    CHECK_FALSE(shell::rgrain_cap_on(0));
    CHECK_FALSE(shell::rgrain_cap_on(59));
    CHECK(shell::rgrain_cap_on(60));
    CHECK(shell::rgrain_cap_on(119));
    CHECK_FALSE(shell::rgrain_cap_on(120));
    CHECK(shell::rgrain_cap_on(180));
}

TEST_CASE("rgrain_grant: budget minus the cost without the layer") {
    constexpr uint32_t budget = 920000, per = 18000;
    CHECK(shell::rgrain_grant(900000, 0, false, budget, per, 8) == 1);
    // base 828 000 -> 92 000 / 18 000 = 5
    CHECK(shell::rgrain_grant(900000, 4, false, budget, per, 8) == 5);
    CHECK(shell::rgrain_grant(700000, 0, false, budget, per, 8) == 8);   // capped
    CHECK(shell::rgrain_grant(930000, 0, false, budget, per, 8) == 0);
    // 8 * 18 000 > 50 000: the base clamps at 0 instead of wrapping
    CHECK(shell::rgrain_grant(50000, 8, false, budget, per, 8) == 8);
    CHECK(shell::rgrain_grant(500000, 0, true, budget, per, 8) == 0);    // silenced
    CHECK(shell::rgrain_grant(920000, 0, false, budget, per, 8) == 0);   // base == budget
}
