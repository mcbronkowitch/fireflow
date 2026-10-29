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
#include <cstddef>
#include <cstdint>
#include "hw/board.h"
#include "instrument.h"

namespace shell {

// Before StartAudio(). On Rev A it also arms the CLOCK / RESET edge latch
// (gate_latch.h) and resets the clock law; main() then calls
// gate_latch_arm() as the last thing before StartAudio().
void panel_scan_init(bench::Board& hw);

// Call FIRST in the audio callback, before process() (spec section 2).
void panel_scan_tick(bench::Board& hw, spky::Instrument& inst);

// Call right after process(): mutes the output until the boot take is done,
// then fades it in (boot_gate_apply(), boot_take.h). Inert without persistence.
void panel_scan_gate(float* l, float* r, size_t n);

// Call LAST in the audio callback, after process(), with the DWT count read
// before panel_scan_tick(): records the whole callback's cycles for
// SHELL_PLAY_LAW's cb_last / cb_max (same warm-up rule as cyc_max).
void panel_scan_block_end(uint32_t c0);

// SHELL_REMOTE only (plan 2026-10-29-cpu-hunt-remote Task 5); audio-callback
// context, like panel_scan_tick():
// - panel_scan_load_knobs(): the knob vector becomes `knobs`, every pot lets
//   go (a pot takes over again only once it moves kPotTouch from its reading
//   after the load), and the law is re-armed as for a restore
//   (control_rearm_after_take()), so the load fires no bbd_edge.
// - panel_scan_guard_tripped(): the overrun guard (workload_run.h) has
//   tripped; the callback then writes silence instead of calling process().
void panel_scan_load_knobs(const float* knobs);
bool panel_scan_guard_tripped();

// SHELL_REMOTE_MEASURE only (plan Task 6), audio-callback context: call
// after panel_scan_tick() and before process(). Burns the cycles a LATE
// command asked for in this callback (k * 1000), and with toggle bit 30 the
// last LATE's on every block, so the late block lands inside the measured
// callback. Nothing to do otherwise.
void panel_scan_late();

// Starts the log and prints SHELL_PLAY twice a second. Never returns.
[[noreturn]] void run_panel_scan_report(bench::Board& hw);

} // namespace shell
