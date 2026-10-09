#pragma once

// The playing image (SHELL_PANEL_SCAN=1): the scan runs first in the audio
// callback, and every mapped channel it reads goes through the value path
// (scan_value.h) into a knob vector in VCV's units (controls.h), which the
// shared control law (control/control_law.h) turns into engine calls once
// per block -- the same law FireflowHW runs.
// On the coupon, RV2/RV4/RV6 drive RATE_A/DENSITY_A/FILT_A over Rev A's step
// model (kCouponPlayChain); on Rev A, every pot of the generated table but
// the three reserved ones drives its FireflowHW twin's parameter (spec
// 2026-10-09-rev-a-p6b1-shared-control-law-design.md section 4.2).
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
