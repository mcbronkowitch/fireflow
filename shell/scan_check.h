#pragma once

// The scan-check image (SHELL_SCAN_CHECK=1, coupon only): the engine plays
// at the shell's fixed operating point, and the scan runs at the start of
// the audio callback in three arms (scan_check_plan.h). Nothing it reads
// reaches the engine. The foreground prints each finished run block.
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
// section 4.
#include "hw/board.h"

namespace shell {

void scan_check_init();

// Call FIRST in the audio callback, before process(): arm S's read and the
// next select have to be exactly one block apart (spec section 2).
void scan_check_tick(bench::Board& hw);

// Starts the log and prints every finished run block. Never returns.
[[noreturn]] void run_scan_check_report(bench::Board& hw);

} // namespace shell
