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
// others wait. Prints one SHELL_SD_S line.
void sd_stream_pass(bench::Board& hw, const volatile uint32_t& win);

} // namespace shell
