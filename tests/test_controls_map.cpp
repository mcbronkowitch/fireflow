// The control table: which (group, channel) drives which parameter, and how
// a 0..1 value is scaled into that parameter's range. Pure data logic, host
// tested: on the board a wrong row is only audible as "the knob does the
// wrong thing". Spec: docs/superpowers/specs/
// 2026-09-28-coupon-panel-scan-design.md sections 3 and 5.
#include <doctest/doctest.h>
#include <set>
#include <utility>
#include "../shell/controls.h"
#include "../shell/generated_panel_map.h"
#include "../shell/mux_plan.h"
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

TEST_CASE("controls: an entry without a parameter is refused, not applied") {
    // Rev A's table carries rows that are scanned and reported but sent
    // nowhere (spec section 2.3). apply_param() would index kParams[-1].
    spky::Instrument inst;
    inst.init(48000.0f);
    const float a = inst.rate(spky::PART_A);
    CHECK_FALSE(shell::apply_control(shell::ControlEntry{0, 0, -1}, 0.9f, inst));
    CHECK_FALSE(shell::apply_control(shell::ControlEntry{0, 0, spky::P_COUNT}, 0.9f, inst));
    CHECK(inst.rate(spky::PART_A) == doctest::Approx(a));
    CHECK(shell::apply_control(shell::ControlEntry{0, 2, spky::P_RATE_A}, 0.9f, inst));
    CHECK(inst.rate(spky::PART_A) == doctest::Approx(0.9f));
}

TEST_CASE("controls: an entry records its sense pin, -1 when unrecorded") {
    CHECK(shell::kCouponControls[0].sense == -1);
    // A local, not a braced temporary inside CHECK(): the preprocessor
    // splits macro arguments on the commas inside braces.
    const shell::ControlEntry e{3, 4, spky::P_RATE_B, 1};
    CHECK(e.sense == 1);
}

TEST_CASE("controls: the Rev A table has one row per pot") {
    CHECK(shell::kRevaTable.count == 70);
}

TEST_CASE("controls: every Rev A row is a channel the scan reads, on its own sense pin") {
    const shell::ChainProfile& p = shell::kRevaChain;
    for(int i = 0; i < shell::kRevaTable.count; ++i)
    {
        const shell::ControlEntry& e = shell::kRevaTable.entries[i];
        CAPTURE(i);
        REQUIRE(e.group >= 0);
        REQUIRE(e.group < p.groups);
        CHECK(e.sense == p.sense_of_group[e.group]);
        const int s = shell::step_of(p, e.group, e.ch);
        REQUIRE(s >= 0);
        CHECK(shell::group_at(p, s, e.sense) == e.group);
        CHECK(shell::channel_at(p, s, e.sense) == e.ch);
    }
}

TEST_CASE("controls: no two Rev A rows share an input, and none is a calibration channel") {
    // Review Focus 5.
    std::set<std::pair<int, int>> seen;
    for(int i = 0; i < shell::kRevaTable.count; ++i)
        seen.insert({shell::kRevaTable.entries[i].group, shell::kRevaTable.entries[i].ch});
    CHECK(static_cast<int>(seen.size()) == shell::kRevaTable.count);
    CHECK(shell::find_control(shell::kRevaTable, shell::kRevaCalZero.group,
                              shell::kRevaCalZero.ch) == nullptr);
    CHECK(shell::find_control(shell::kRevaTable, shell::kRevaCalRail.group,
                              shell::kRevaCalRail.ch) == nullptr);
}

TEST_CASE("controls: the Rev A table sends exactly the 35 safe parameters (spec 2.3)") {
    using namespace spky;
    const std::set<int> expected = {
        P_RATE_A, P_RATE_B, P_SHAPE_A, P_SHAPE_B, P_SMOOTH_A, P_SMOOTH_B,
        P_RANGE_A, P_RANGE_B, P_TUNE_A, P_TUNE_B, P_DECAY_A, P_DECAY_B,
        P_FILT_A, P_FILT_B, P_COLOR_A, P_COLOR_B, P_LINK_A, P_LINK_B,
        P_PAN_A, P_PAN_B, P_REVMIX_A, P_REVMIX_B, P_DEPTH_A, P_DEPTH_B,
        P_MORPH, P_TIDE, P_CHOKE, P_PULL, P_SHUFFLE, P_REV_SIZE, P_REV_DECAY,
        P_REV_TONE, P_REV_DIFF, P_PACE, P_SCALE};
    REQUIRE(expected.size() == 35);
    std::multiset<int> got;
    for(int i = 0; i < shell::kRevaTable.count; ++i)
        if(shell::kRevaTable.entries[i].param >= 0)
            got.insert(shell::kRevaTable.entries[i].param);
    CHECK(got.size() == 35);
    CHECK(std::set<int>(got.begin(), got.end()) == expected);
}
