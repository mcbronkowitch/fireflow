// The control table: which (group, channel) drives which parameter, and how
// a 0..1 value is scaled into that parameter's range. Pure data logic, host
// tested: on the board a wrong row is only audible as "the knob does the
// wrong thing". Spec: docs/superpowers/specs/
// 2026-09-28-coupon-panel-scan-design.md sections 3 and 5.
#include <doctest/doctest.h>
#include "../shell/controls.h"
#include "../shell/pot_plan.h"
#include "instrument.h"

TEST_CASE("controls: the coupon table maps RV2, RV4 and RV6") {
    const shell::ControlTable& t = shell::kCouponTable;
    REQUIRE(t.count == 3);
    CHECK(t.entries[0].param == spky::P_RATE_A);
    CHECK(t.entries[1].param == spky::P_DENSITY_A);
    CHECK(t.entries[2].param == spky::P_FILT_A);
}

TEST_CASE("controls: the coupon table's channels are pot_plan.h's pots") {
    // One source of truth for where the pots sit: the pot round's table.
    const shell::ControlTable& t = shell::kCouponTable;
    REQUIRE(t.count == shell::kPotCount);
    for(int i = 0; i < t.count; ++i)
    {
        CHECK(t.entries[i].group == shell::kPots[i].group);
        CHECK(t.entries[i].ch == shell::kPots[i].channel);
    }
}

TEST_CASE("controls: the panel table is empty until part 2") {
    CHECK(shell::kPanelTable.count == 0);
    CHECK(shell::find_control(shell::kPanelTable, 0, 0) == nullptr);
}

TEST_CASE("controls: find_control answers only for mapped channels") {
    const shell::ControlTable& t = shell::kCouponTable;
    REQUIRE(shell::find_control(t, 0, 6) != nullptr);
    CHECK(shell::find_control(t, 0, 6)->param == spky::P_DENSITY_A);
    CHECK(shell::find_control(t, 0, 3) == nullptr);    // a rail tie
    CHECK(shell::find_control(t, 1, 6) == nullptr);    // group 1's divider
    CHECK(shell::find_control(t, 5, 0) == nullptr);
    CHECK(shell::find_control(t, -1, 2) == nullptr);
}

TEST_CASE("controls: values scale into the parameter's own range") {
    CHECK(shell::control_value(spky::P_RATE_A, 0.25f) == doctest::Approx(0.25f));
    CHECK(shell::control_value(spky::P_FILT_A, 0.0f) == doctest::Approx(-1.0f));
    CHECK(shell::control_value(spky::P_FILT_A, 0.5f) == doctest::Approx(0.0f));
    CHECK(shell::control_value(spky::P_FILT_A, 1.0f) == doctest::Approx(1.0f));
    CHECK(shell::control_value(-1, 0.5f) == doctest::Approx(0.0f));
    CHECK(shell::control_value(spky::P_COUNT, 0.5f) == doctest::Approx(0.0f));
}

TEST_CASE("controls: applying RV2's entry moves part A's rate and only it") {
    spky::Instrument inst;
    inst.init(48000.0f);
    const float b_before = inst.rate(spky::PART_B);
    const shell::ControlEntry* e = shell::find_control(shell::kCouponTable, 0, 2);
    REQUIRE(e != nullptr);
    shell::apply_control(*e, 0.75f, inst);
    CHECK(inst.rate(spky::PART_A) == doctest::Approx(0.75f));
    CHECK(inst.rate(spky::PART_B) == doctest::Approx(b_before));
}
