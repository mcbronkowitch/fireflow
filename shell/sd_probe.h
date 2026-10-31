#pragma once

// The SD card bring-up (spec docs/superpowers/specs/2026-10-31-sd-bring-up-design.md,
// docs/hardware/reva-bring-up.md section 11). Built only with SHELL_SD=1
// (shell/Makefile), which also needs the listener and the raw readout; the
// three calls below sit in run_panel_scan_report() under #if SHELL_SD.
//
// J_SD (Yamaichi PJS008U-3000-0) is on SDMMC1, 4-bit, the module's D2-D7.
// Nothing here touches QSPI: the persistence record is the listener image's
// to leave alone (persist_write() is not linked there).
#include <cstdint>

#include "hw/board.h"

namespace shell {

// Once, from the report loop with audio running and the listener up, so a
// card that hangs still leaves BOOT working. Mounts the card, verifies a test
// file left by an earlier boot, writes the file again (8 MiB), reads it back
// and verifies every word; leaves the file open for sd_stream_pass().
void sd_bringup_run();

// The four SHELL_SD lines: on pass 0 and every 10th pass after it.
void sd_report(bench::Board& hw, uint32_t pass);

// In place of the report loop's Delay(500). `win` is the raw readout's window
// counter (g_raw_win): windows w with (w / 10) odd stream the test file in
// 32 KB reads, verified, until the 500 ms are up or the phase ends; the
// others wait. Prints one SHELL_SD_S line. With SHELL_SD_PROBE=burst (shell/
// Makefile) the pass follows sd_schedule.h's burst phases instead; see below.
void sd_stream_pass(bench::Board& hw, const volatile uint32_t& win);

// ---- The pot hold (SHELL_SD_PROBE=burst, spec 2026-10-31-sd-use-cases-tests-
// design.md section 5, A1). The foreground marks each transfer, the audio
// callback decides once per block whether this block's pot readings are
// dropped, so the readings that stay can be measured against the noise floor
// (fault R5: the card makes every pot noisy while it transfers).
//
// Set by the foreground right before every f_read / f_write, and again right
// after it returns; cleared by the callback when it takes it.
extern volatile bool g_sd_seen;

// The current phase's tail: blocks after a transfer whose readings are also
// dropped. 0 outside the burst mode and in an idle phase (no transfer starts
// there, and a countdown that began earlier finishes on the tail it was
// started with -- SdHold keeps it).
uint8_t sd_hold_tail();

// The callback's side, once per block, before the step's readings are looked
// at: true means drop this block's pot readings. Counts the dropped blocks and
// the longest run of them for the pass line. Always false when nothing sets
// g_sd_seen (every mode but the burst), so the other modes read every block.
bool sd_hold_block();

} // namespace shell
