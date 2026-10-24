#pragma once

// A boot patch from a pot frame (fault R4, 2026-10-24): the knob vector a
// SHELL_PLAY_V frame describes, applied at control_boot() so the engine plays
// that patch from the first block with nobody touching the panel. Pure data
// logic, host tested (tests/test_boot_patch.cpp); the firmware calls it from
// control_tick.cpp behind SHELL_BOOT_PATCH.
#include "controls.h"

namespace shell {

// knobs[] <- the rows, in row order: row r's value (x1000 of the normalised
// pot value, as SHELL_PLAY_V prints it) goes through knob_from_pot() into the
// knob knob_target() names for that row -- the same two calls pot_row_step()
// makes for an owned pot, so the result is the vector the live panel wrote
// at those positions. Row order matters for the same reason it does live:
// an ATTACK row lands on STAGES only once its deck's ENGINE row has put the
// deck on the BBD. A row with no parameter is skipped.
// Returns the rows written, or -1 (knobs untouched) when n != t.count.
int apply_boot_patch_rows(const ControlTable& t, const int* rows_x1000, int n, float* knobs);

} // namespace shell
