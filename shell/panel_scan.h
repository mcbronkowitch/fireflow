#pragma once

// The playing image (SHELL_PANEL_SCAN=1): the scan runs first in the audio
// callback, and every mapped channel it reads goes through the value path
// (scan_value.h) into the engine (controls.h). On the coupon, RV2/RV4/RV6
// drive RATE_A/DENSITY_A/FILT_A; on the panel profile the table is empty
// until part 2 and nothing is applied.
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
// section 5.
#include "hw/board.h"
#include "instrument.h"

namespace shell {

void panel_scan_init();

// Call FIRST in the audio callback, before process() (spec section 2).
void panel_scan_tick(bench::Board& hw, spky::Instrument& inst);

// Starts the log and prints SHELL_PLAY twice a second. Never returns.
[[noreturn]] void run_panel_scan_report(bench::Board& hw);

} // namespace shell
