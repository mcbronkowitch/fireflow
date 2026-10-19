#pragma once

// The playing image (SHELL_PANEL_SCAN=1): the scan runs first in the audio
// callback, and every mapped channel it reads goes through the value path
// (scan_value.h) into a knob vector in VCV's units (controls.h), which the
// shared control law (control/control_law.h) turns into engine calls once
// per block -- the same law FireflowHW runs. A pot writes its knob only once
// it is moved (pot_owner.h), the keys toggle their latches, and the shared LED
// law lights the lamps through the LightId -> chain-bit table (spec
// 2026-10-18-rev-a-p6b2-mod-keys-leds-design.md).
// On the coupon, RV2/RV4/RV6 drive RATE_A/DENSITY_A/FILT_A over Rev A's step
// model (kCouponPlayChain); on Rev A, every pot of the generated table
// drives its FireflowHW twin's parameter (spec
// 2026-10-09-rev-a-p6b1-shared-control-law-design.md section 4.2) -- the
// three that were reserved, ROOT_A, ROOT_B and REV_MOD, too since spec
// 2026-10-18-root-and-wobl-knobs-design.md.
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
// section 5.
#include <cstdint>
#include "hw/board.h"
#include "instrument.h"

namespace shell {

void panel_scan_init();

// Call FIRST in the audio callback, before process() (spec section 2).
void panel_scan_tick(bench::Board& hw, spky::Instrument& inst);

// Call LAST in the audio callback, after process(), with the DWT count read
// before panel_scan_tick(): records the whole callback's cycles for
// SHELL_PLAY_LAW's cb_last / cb_max (same warm-up rule as cyc_max).
void panel_scan_block_end(uint32_t c0);

// Starts the log and prints SHELL_PLAY twice a second. Never returns.
[[noreturn]] void run_panel_scan_report(bench::Board& hw);

} // namespace shell
