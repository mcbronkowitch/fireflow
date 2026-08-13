// tests/test_glow_ui.cpp
#include <doctest/doctest.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include "vcv/src/generated_flow_panel.hpp"
#include "vcv/src/glow_ui.hpp"
#include "flow/taste.h"
#include "flow/terrain_code.h"

using namespace spky;
using namespace spky::flow;
using namespace spkyvcv;

static std::string glowSource() {
    for (const char* prefix : {"", "../"}) {
        std::ifstream input(std::string(prefix) + "host/vcv/src/Glow.cpp");
        if (input) {
            std::ostringstream text;
            text << input.rdbuf();
            return text.str();
        }
    }
    return {};
}

TEST_CASE("glow panel: physical P00-P11 map in order to PAD_1-PAD_12") {
    using namespace spkyvcv::glow;
    static constexpr const char* expectedNames[] = {
        "Touch electrode P00", "Touch electrode P01",
        "Touch electrode P02", "Touch electrode P03",
        "Touch electrode P04", "Touch electrode P05",
        "Touch electrode P06", "Touch electrode P07",
        "Touch electrode P08", "Touch electrode P09",
        "Touch electrode P10", "Touch electrode P11",
    };

    for (int i = 0; i < 12; ++i) {
        const PanelCtl& control = kParamCtls[PAD_1 + i];
        INFO("physical pad " << i);
        CHECK(control.id == PAD_1 + i);
        CHECK(control.kind == WK_PAD);
        CHECK(control.mm.x == doctest::Approx(kPadShapes[i].centre.x));
        CHECK(control.mm.y == doctest::Approx(kPadShapes[i].centre.y));
        const std::string_view tooltip(control.tip);
        CHECK(tooltip.substr(0, tooltip.find(" --")) == expectedNames[i]);
    }
}

TEST_CASE("glow panel: Rack widget installs the layered panel and custom "
          "three-position toggles") {
    const std::string source = glowSource();
    REQUIRE_FALSE(source.empty());
    CHECK(source.find("box.size = Vec(16 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT)")
          != std::string::npos);
    CHECK(source.find("setPanel(new GlowHardwarePanel())") != std::string::npos);
    CHECK(source.find("createPanel(asset::plugin(pluginInstance, \"res/Glow.svg\"))")
          == std::string::npos);
    CHECK(source.find("createParamCentered<GlowToggle>") != std::string::npos);
    CHECK(source.find("createParamCentered<CKSSThree>") == std::string::npos);
    CHECK(source.find("configSwitch(c.id, 0.f, 2.f, 0.f") != std::string::npos);
    CHECK(spkyvcv::glow::kParamCtls[spkyvcv::glow::SW_L].kind ==
          spkyvcv::glow::WK_SWITCH);
    CHECK(spkyvcv::glow::kParamCtls[spkyvcv::glow::SW_R].kind ==
          spkyvcv::glow::WK_SWITCH);
}

TEST_CASE("glow: pad visual states have one unambiguous precedence") {
    CHECK(pad_visual_state(false, false, false) == PadVisualState::IDLE);
    CHECK(pad_visual_state(true, false, false) == PadVisualState::LIVE);
    CHECK(pad_visual_state(true, true, false) == PadVisualState::EXCURSION);
    CHECK(pad_visual_state(true, false, true) == PadVisualState::REFUSED);
    CHECK(pad_visual_state(true, true, true) == PadVisualState::REFUSED);
    CHECK(pad_visual_state(false, true, true) == PadVisualState::IDLE);
}

TEST_CASE("glow: the house code is a decodable terrain code") {
    TerrainState st;
    CHECK(decode_code(kHouseCode, st));
}

TEST_CASE("glow: a saved payload restores the terrain and the undo slot") {
    // The lock is deliberately NOT part of this payload -- Glow.cpp's
    // controlTick pushes the LOCK switch's answer into Flow every tick, so a
    // saved lock could only survive one control period. See glow_ui.hpp's note
    // on GlowSave.
    Instrument inst;
    inst.init(48000.f);
    Flow fl;
    fl.init(&inst, 100.f);

    TerrainState house;
    REQUIRE(decode_code(kHouseCode, house));
    fl.wake(house);
    REQUIRE(fl.new_full());                  // fills the undo slot

    const GlowSave s = glow_capture(fl);
    CHECK(s.have_undo);
    CHECK(std::strlen(s.code) == size_t(kTerrainCodeLen));

    Instrument inst2;
    inst2.init(48000.f);
    Flow fl2;
    fl2.init(&inst2, 100.f);
    // Locked BEFORE the restore, and still locked after: a restore must not
    // write the lock at all. Glow.cpp's controlTick is that state's only
    // writer, and it decides from the switch. If a saved lock ever comes back
    // into GlowSave, this is the assertion that says so.
    fl2.set_lock(true);
    CHECK(glow_restore(fl2, s));
    CHECK(fl2.locked());
    CHECK(fl2.state().master == fl.state().master);
    for (int m = 0; m < MACRO_COUNT; ++m)
        CHECK(fl2.state().reroll[m] == fl.state().reroll[m]);
    CHECK(fl2.can_undo());
    CHECK(fl2.undo_state().master == fl.undo_state().master);
    // A restore is bookkeeping, not a gesture: no blend may be in flight.
    CHECK(fl2.blend_phase() == doctest::Approx(1.f));
}

TEST_CASE("glow: a malformed saved code changes nothing") {
    Instrument inst;
    inst.init(48000.f);
    Flow fl;
    fl.init(&inst, 100.f);
    TerrainState house;
    REQUIRE(decode_code(kHouseCode, house));
    fl.wake(house);

    GlowSave bad;
    std::snprintf(bad.code, sizeof bad.code, "%s", "F1-NOTHEX00-000000000000");
    // The undo half of the payload is deliberately VALID. A malformed current
    // code has to abort the whole restore, not just its own half -- otherwise
    // a corrupt patch loads with an undo slot pointing somewhere the player
    // never was. wake() cleared the slot above, so any undo here came from
    // this call.
    encode_code(house, bad.undo, int(sizeof bad.undo));
    bad.have_undo = true;
    CHECK_FALSE(glow_restore(fl, bad));
    CHECK(fl.state().master == house.master);
    CHECK_FALSE(fl.can_undo());
}

TEST_CASE("glow: a refuse flash is active only within its window after mark") {
    // The module's own refusal signal: Flow declines an op (locked generator,
    // empty undo slot, a pad whose place does not decode) by returning false,
    // and only the module knows it happened. This is that signal's headless
    // coverage.
    RefuseFlash rf;
    const double t = 12.5;                 // plausible mid-session timestamp
    CHECK_FALSE(rf.active(t));             // fresh: not "just refused"
    CHECK_FALSE(rf.active(0.0));

    rf.mark(t);
    CHECK(rf.active(t));
    CHECK(rf.active(t + spky::flow::kRefuseFlashS - 1e-6));
    CHECK_FALSE(rf.active(t + spky::flow::kRefuseFlashS));
    CHECK_FALSE(rf.active(t + spky::flow::kRefuseFlashS + 1.0));
}

TEST_CASE("glow: the scale knob travels from least to most friction") {
    // A permutation check alone would test the table, not the feature. The
    // monotonicity check is what catches a kScaleW retune that reorders the
    // groups and silently leaves the knob travel no longer running calm to
    // sharp.
    bool seen[spky::SCALE_LIST_COUNT] = {};
    for (int i = 0; i < spky::SCALE_LIST_COUNT; ++i) {
        const int s = spkyvcv::kScaleKnobOrder[i];
        REQUIRE(s >= 0);
        REQUIRE(s < spky::SCALE_LIST_COUNT);
        CHECK(!seen[s]);
        seen[s] = true;
    }
    for (int i = 1; i < spky::SCALE_LIST_COUNT; ++i)
        CHECK(spky::flow::kScaleW[spkyvcv::kScaleKnobOrder[i]] <=
              spky::flow::kScaleW[spkyvcv::kScaleKnobOrder[i - 1]]);
}

TEST_CASE("glow: a saved scale outside the list reads as the boot default") {
    // The successor to the old scale_of_knob test. There is no scale knob on
    // the Touch 2 surface -- the switch gates the menu's value -- so what is
    // left to guard is the saved value: paramsFromJson and a hand-edited patch
    // both reach menuScale with anything at all, and Flow::set_scale_override
    // indexes SCALE_MASKS with it.
    for (int s = 0; s < spky::SCALE_LIST_COUNT; ++s)
        CHECK(spkyvcv::clamp_menu_scale(s) == s);
    CHECK(spkyvcv::clamp_menu_scale(-1) == spky::SCALE_AEOLIAN);
    CHECK(spkyvcv::clamp_menu_scale(spky::SCALE_LIST_COUNT) == spky::SCALE_AEOLIAN);
    CHECK(spkyvcv::clamp_menu_scale(9999) == spky::SCALE_AEOLIAN);
}

TEST_CASE("glow: a saved genre outside the archetypes reads as ANY") {
    // An unmatchable genre is not an out-of-bounds read -- draw_new filters
    // with arch_of and never matches -- it is worse to diagnose: every draw
    // returns the same default terrain and the generator looks dead.
    for (int a = 0; a < spky::flow::ARCH_COUNT; ++a)
        CHECK(spkyvcv::clamp_genre(a) == a);
    CHECK(spkyvcv::clamp_genre(spky::flow::ARCH_ANY) == spky::flow::ARCH_ANY);
    CHECK(spkyvcv::clamp_genre(spky::flow::ARCH_COUNT) == spky::flow::ARCH_ANY);
    CHECK(spkyvcv::clamp_genre(-7) == spky::flow::ARCH_ANY);
}

TEST_CASE("glow: a saved root override outside 0..11 reads as AUTO") {
    // Spec 5 asks for the root override's JSON round-trip under test, and the
    // non-obvious half of it is the validation, not the jansson call: Rack's
    // Param::setValue does not clamp and paramsFromJson writes straight
    // through, so a hand-edited patch reaches this with anything at all.
    // Glow.cpp keeps only the json_is_integer type check and hands the number
    // here, which is why this is testable without rack.hpp.
    for (int r = 0; r <= 11; ++r) CHECK(spkyvcv::clamp_root_override(r) == r);
    CHECK(spkyvcv::clamp_root_override(12) == -1);
    CHECK(spkyvcv::clamp_root_override(99) == -1);
    CHECK(spkyvcv::clamp_root_override(-1) == -1);      // the AUTO sentinel
    CHECK(spkyvcv::clamp_root_override(-7) == -1);
}
