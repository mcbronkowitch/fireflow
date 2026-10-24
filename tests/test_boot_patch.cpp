// The boot patch from a pot frame (fault R4): the rows become the knob vector
// the live panel would have written, order rule included, and the generated
// all-noon frame is the patch the record describes (docs/hardware/
// reva-bring-up.md 10.1).
#include <doctest/doctest.h>
#include <cmath>
#include "../shell/boot_patch.h"
#include "../shell/controls.h"
#include "../shell/generated_boot_patch_noon.h"
#include "../shell/generated_panel_map.h"
#include "control/params.hpp"

namespace {

void init_knobs(float* knobs)
{
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) knobs[i] = ffctl::kInitParamDefaults[i];
}

// A two-row table: the deck's ENGINE, then its ATTACK with STAGES as the BBD
// alternate -- the only order rule the firmware has (knob_target()).
constexpr shell::ControlEntry kTwoRows[] = {
    {0, 0, ffctl::ENGINE_A, 0},
    {0, 1, ffctl::ATTACK_A, 0, ffctl::STAGES_A},
};
constexpr shell::ControlTable kTwo{kTwoRows, 2};

} // namespace

TEST_CASE("boot patch: a row writes the knob its pot would have written") {
    float knobs[ffctl::NUM_PARAMS];
    init_knobs(knobs);
    // ENGINE_A spans 0..5 and snaps: 531 -> 2.655 -> 3. ATTACK_A spans 0..1.
    const int rows[] = {531, 250};
    CHECK(shell::apply_boot_patch_rows(kTwo, rows, 2, knobs) == 2);
    CHECK(knobs[ffctl::ENGINE_A] == 3.f);
    CHECK(knobs[ffctl::ATTACK_A] == doctest::Approx(0.25f));
    CHECK(knobs[ffctl::STAGES_A] == ffctl::kInitParamDefaults[ffctl::STAGES_A]);
}

TEST_CASE("boot patch: an ATTACK row lands on STAGES once its deck is on the BBD") {
    float knobs[ffctl::NUM_PARAMS];
    init_knobs(knobs);
    // 800 -> 4.0 -> slot 4, the BBD (shell::kEngineSlotBbd).
    const int rows[] = {800, 250};
    CHECK(shell::apply_boot_patch_rows(kTwo, rows, 2, knobs) == 2);
    CHECK(knobs[ffctl::ENGINE_A] == static_cast<float>(shell::kEngineSlotBbd));
    CHECK(knobs[ffctl::STAGES_A] == doctest::Approx(0.25f));
    CHECK(knobs[ffctl::ATTACK_A] == ffctl::kInitParamDefaults[ffctl::ATTACK_A]);
}

TEST_CASE("boot patch: a row count that is not the table's writes nothing") {
    float knobs[ffctl::NUM_PARAMS];
    init_knobs(knobs);
    const int rows[] = {800};
    CHECK(shell::apply_boot_patch_rows(kTwo, rows, 1, knobs) == -1);
    CHECK(knobs[ffctl::ENGINE_A] == ffctl::kInitParamDefaults[ffctl::ENGINE_A]);
}

TEST_CASE("boot patch: the all-noon frame is the Rev A patch the record describes") {
    float knobs[ffctl::NUM_PARAMS];
    init_knobs(knobs);
    REQUIRE(shell::kBootPatchNoonRowCount == shell::kRevaTable.count);
    CHECK(shell::apply_boot_patch_rows(shell::kRevaTable, shell::kBootPatchNoonRows,
                                       shell::kBootPatchNoonRowCount, knobs)
          == shell::kRevaTable.count);
    // Hand-derived from the frame: row 0 ENGINE_A 531 -> 2.655 -> 3; row 22
    // ENGINE_B 410 -> 2.05 -> 2 (WAVE). Neither is the BBD slot, so both
    // ATTACK rows (10 and 34) write ATTACK, and STAGES keeps the init value.
    CHECK(knobs[ffctl::ENGINE_A] == 3.f);
    CHECK(knobs[ffctl::ENGINE_B] == 2.f);
    CHECK(knobs[ffctl::ATTACK_A] == doctest::Approx(0.465f));   // row 10
    CHECK(knobs[ffctl::ATTACK_B] == doctest::Approx(0.505f));   // row 34
    CHECK(knobs[ffctl::STAGES_A] == ffctl::kInitParamDefaults[ffctl::STAGES_A]);
    CHECK(knobs[ffctl::STAGES_B] == ffctl::kInitParamDefaults[ffctl::STAGES_B]);
    // Every row's knob is its own value: nothing later in the order overwrote it.
    for(int r = 0; r < shell::kRevaTable.count; ++r)
    {
        const shell::ControlEntry& e = shell::kRevaTable.entries[r];
        const int target = shell::knob_target(e, knobs);
        CHECK(knobs[target]
              == shell::knob_from_pot(target, shell::kBootPatchNoonRows[r] * 0.001f));
    }
    // It is a different patch from the init patch, not a near miss of it.
    int changed = 0;
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i)
        if(knobs[i] != ffctl::kInitParamDefaults[i]) ++changed;
    CHECK(changed >= 60);
    // The MOD latch stays off: the rows wrote knobs, not depths.
    CHECK(knobs[ffctl::MODBTN] == ffctl::kInitParamDefaults[ffctl::MODBTN]);
}
