#pragma once

// Panel-state persistence on the board (spec 2026-10-28): the two QSPI
// sectors the records live in, the boot read and the foreground save. The
// record format, the slot scan and the save policy are the host-tested units
// (persist_record.h, persist_policy.h); this is the part with a QSPIHandle.
//
// THE SWITCH. SHELL_PERSIST (generated header, shell/Makefile) is only half of
// it. Persistence is effective in the Rev A play image alone: not on the
// coupon (spec section 2, non-goals), not in the images that do not play,
// and not under a SHELL_BOOT_PATCH stress patch, which depends on unowned
// pots holding it (spec section 4.2). SHELL_BOOT_PATCH arrives through
// C_USER_FLAGS, so only the preprocessor can see it, which is why the fold
// happens here and not in make. Where SHELL_PERSIST_ON is 0 every caller
// compiles today's code: no boot take, no publish, no save, the init patch.
//
// WHY THE SAVE IS SAFE TO RUN WITH AUDIO ON. libDaisy's EraseSector() and
// Write() take the QSPI out of memory-mapped mode for their duration (spec
// section 3). Nothing hot executes from QSPI or reads it (qspi_placement.py,
// wt_bank_d2.cpp), persist_write() itself is kept in SRAM by the same guard,
// and it is only ever called from the foreground loop, never from the audio
// interrupt. Whether a libDaisy interrupt still touches QSPI is spec section
// 6 probe 1, on the board.
//
// Spec: ../docs/superpowers/specs/2026-10-28-panel-state-persistence-design.md
// sections 4.2, 4.4 and 5.2.
#include <cstdint>
#include "shell_persist.h"
#include "shell_panel_scan.h"
#include "shell_coupon_probe.h"
#include "persist_record.h"

#if SHELL_PERSIST && SHELL_PANEL_SCAN && !SHELL_COUPON_PROBE \
    && !(defined(SHELL_BOOT_PATCH) && SHELL_BOOT_PATCH != 0)
#define SHELL_PERSIST_ON 1
#else
#define SHELL_PERSIST_ON 0
#endif

namespace shell {

inline constexpr bool kPersistOn = SHELL_PERSIST_ON;

// The top two 4 KiB sectors of the 8 MiB chip; QSPI_COLD ends in front of
// them (qspi_cold.ld), and both placement guards refuse anything placed there.
inline constexpr uint32_t kPersistSector0 = 0x907FE000u, kPersistSector1 = 0x907FF000u;
static_assert(kPersistSector1 == kPersistSector0 + kPersistSectorSize, "adjacent sectors");
static_assert(kPersistSector1 + kPersistSectorSize == 0x90800000u, "the chip's last two sectors");

} // namespace shell

#if SHELL_PERSIST_ON
#include "hw/board.h"   // daisy::QSPIHandle, through the board's header

namespace shell {

// The newest valid record across both sectors, read memory-mapped (the data
// cache over the sectors is invalidated first). Foreground or boot only: never
// while an erase or a write is running, which can only be persist_write()'s
// own, and that does not call it mid-operation.
SlotPick persist_latest();

// Boot (spec 4.4 step 1): before StartAudio(), with the QSPI memory-mapped
// (board_init() has returned). Fills knobs from the init patch, overlays the
// newest valid record minus the spec 4.3 overrides (boot_overlay()), and
// hands back the record's excite_other_deck bits (false without a record).
// What it found stays readable: persist_boot_pick() for the boot line,
// persist_boot_excite() for excite_after_take() once the boot take is done.
void persist_boot(float* knobs, bool excite_out[2]);
const SlotPick& persist_boot_pick();
const bool*     persist_boot_excite();   // two entries

// One save (spec 5.2), foreground only: the slot after the newest record
// (next_slot()), the other sector erased first when the active one is full,
// the record programmed, then read back memory-mapped and compared. True only
// when the slot now holds exactly r and r is valid. r must be sealed.
bool persist_write(daisy::QSPIHandle& q, const PersistRecord& r);

} // namespace shell
#endif // SHELL_PERSIST_ON
