// The control table: which (group, channel) drives which parameter, and how
// a 0..1 value becomes a knob in that parameter's VCV units. Pure data logic,
// host tested: on the board a wrong row is only audible as "the knob does the
// wrong thing". Spec: docs/superpowers/specs/
// 2026-09-28-coupon-panel-scan-design.md sections 3 and 5, and
// 2026-10-09-rev-a-p6b1-shared-control-law-design.md section 4.2.
#include <doctest/doctest.h>
#include <set>
#include <utility>
#include "../shell/controls.h"
#include "../shell/generated_panel_map.h"
#include "../shell/mux_plan.h"
#include "../shell/pot_plan.h"
#include "instrument.h"
#include "control/control_law.h"

TEST_CASE("controls: the coupon table maps RV2, RV4 and RV6") {
    const shell::ControlTable& t = shell::kCouponTable;
    REQUIRE(t.count == 3);
    CHECK(t.entries[0].param == ffctl::RATE_A);
    CHECK(t.entries[1].param == ffctl::DENSITY_A);
    CHECK(t.entries[2].param == ffctl::FILT_A);
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

TEST_CASE("controls: find_control answers only for mapped channels") {
    const shell::ControlTable& t = shell::kCouponTable;
    REQUIRE(shell::find_control(t, 0, 6) != nullptr);
    CHECK(shell::find_control(t, 0, 6)->param == ffctl::DENSITY_A);
    CHECK(shell::find_control(t, 0, 3) == nullptr);    // a rail tie
    CHECK(shell::find_control(t, 1, 6) == nullptr);    // group 1's divider
    CHECK(shell::find_control(t, 5, 0) == nullptr);
    CHECK(shell::find_control(t, -1, 2) == nullptr);
}

TEST_CASE("controls: a pot becomes a knob in parameter units") {
    CHECK(shell::knob_from_pot(ffctl::RATE_A, 0.25f) == doctest::Approx(0.25f));
    CHECK(shell::knob_from_pot(ffctl::FILT_A, 0.0f) == doctest::Approx(-1.0f));
    CHECK(shell::knob_from_pot(ffctl::FILT_A, 1.0f) == doctest::Approx(1.0f));
    CHECK(shell::knob_from_pot(-1, 0.5f) == 0.0f);
}

TEST_CASE("controls: snapping pots reach both stops") {
    CHECK(shell::knob_from_pot(ffctl::ENGINE_A, 1.0f) == 5.f);
    CHECK(shell::knob_from_pot(ffctl::STEPS_B, 1.0f) == 16.f);
    CHECK(shell::knob_from_pot(ffctl::FLUXRATE_A, 1.0f) == 11.f);
    CHECK(shell::knob_from_pot(ffctl::SCALE, 1.0f) == 12.f);
    CHECK(shell::knob_from_pot(ffctl::SONG_B, 1.0f) == 13.f);
    CHECK(shell::knob_from_pot(ffctl::ENGINE_A, 0.0f) == 0.f);
    CHECK(shell::knob_from_pot(ffctl::ENGINE_A, 0.55f) == 3.f);   // 2.75 rounds up
}

TEST_CASE("controls: an entry records its sense pin, -1 when unrecorded") {
    CHECK(shell::kCouponControls[0].sense == -1);
    // A local, not a braced temporary inside CHECK(): the preprocessor
    // splits macro arguments on the commas inside braces.
    const shell::ControlEntry e{3, 4, ffctl::RATE_B, 1};
    CHECK(e.sense == 1);
}

TEST_CASE("controls: the Rev A table has one row per pot") {
    CHECK(shell::kRevaTable.count == 73);
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

TEST_CASE("controls: only the three reserved Rev A pots send nothing") {
    int none = 0;
    for (int i = 0; i < shell::kRevaTable.count; ++i)
        none += shell::kRevaTable.entries[i].param < 0;
    CHECK(shell::kRevaTable.count == 73);
    CHECK(none == 3);
}

TEST_CASE("controls: the first tick after on_restore with init knobs fires nothing") {
    spky::Instrument inst; inst.init(48000.f);
    control::ControlLaw law; law.on_restore();
    float k[ffctl::NUM_PARAMS];
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) k[i] = ffctl::kInitParamDefaults[i];
    const control::Events ev = law.tick(k, control::Options{}, inst);
    CHECK_FALSE(ev.bbd_edge[0]); CHECK_FALSE(ev.bbd_edge[1]);
    CHECK_FALSE(ev.rec_started[0]); CHECK_FALSE(ev.rec_started[1]);
    // The first tick alone cannot fire: the engines it asks for land blocks
    // later (an engine switch completes inside process()), and only then
    // could the BBD edge or a REC start show. So the firmware's boot is run
    // on: process, tick, as the audio callback does, until the switch is long
    // done. With a BBD deck in the init patch this goes red on block 2
    // (measured 2026-10-09).
    float il[96] = {}, ir[96] = {}, ol[96], orr[96];
    int fired = 0;
    for (int b = 0; b < 32; ++b) {
        inst.process(il, ir, ol, orr, 96);
        const control::Events e = law.tick(k, control::Options{}, inst);
        fired += e.bbd_edge[0] + e.bbd_edge[1] + e.rec_started[0] + e.rec_started[1];
    }
    CHECK(fired == 0);
}

namespace {
// The Instrument with its two one-shot actions counted: the law is a template
// on the instrument, and these members hide the base's for it.
struct ActionSpy : spky::Instrument {
    int phrases = 0;
    int settles = 0;
    void new_phrase(int p) { ++phrases; spky::Instrument::new_phrase(p); }
    void settle()          { ++settles; spky::Instrument::settle(); }
};
} // namespace

TEST_CASE("controls: the first tick after on_restore with init knobs re-rolls and settles nothing") {
    // The firmware boots exactly this way (spec 2026-10-09-rev-a-p6b1 4.1):
    // a restored SONG rung and DRIFT position are a baseline, not a turn.
    ActionSpy inst; inst.init(48000.f);
    control::ControlLawT<ActionSpy> law; law.on_restore();
    float k[ffctl::NUM_PARAMS];
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) k[i] = ffctl::kInitParamDefaults[i];
    law.tick(k, control::Options{}, inst);
    law.tick(k, control::Options{}, inst);
    CHECK(inst.phrases == 0);
    CHECK(inst.settles == 0);
}

TEST_CASE("controls: on_restore over a live law makes the restored knobs a baseline") {
    // The init patch puts DRIFT and SONG at their left stops, so a fresh law
    // cannot show what on_restore() is for. Here the law has run at another
    // state first; the restore then lands on a new SONG rung and DRIFT in its
    // settle zone. Without on_restore() both would fire. (The BBD edge is not
    // checked here: an engine switch lands blocks after the tick that asks
    // for it, so a restore onto the BBD over a live non-BBD deck fires the
    // edge with or without on_restore() -- measured 2026-10-09. The firmware
    // boots a fresh Instrument onto ENGINE 5 and 2, neither of them the BBD.)
    ActionSpy inst; inst.init(48000.f);
    control::ControlLawT<ActionSpy> law;
    float k[ffctl::NUM_PARAMS];
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) k[i] = ffctl::kInitParamDefaults[i];
    k[ffctl::DRIFT] = 1.f;
    law.tick(k, control::Options{}, inst);
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) k[i] = ffctl::kInitParamDefaults[i];
    k[ffctl::SONG_A] = 5.f;
    inst.phrases = 0;
    inst.settles = 0;
    law.on_restore();
    law.tick(k, control::Options{}, inst);
    CHECK(inst.phrases == 0);
    CHECK(inst.settles == 0);
}
