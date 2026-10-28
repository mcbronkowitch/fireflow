// Panel-state persistence on the board: see persist_flash.h. Compiled in
// every image (shell/Makefile), empty wherever SHELL_PERSIST_ON is 0.
#include "persist_flash.h"

#if SHELL_PERSIST_ON

#include <cstring>
#include "persist_policy.h"
#include "sys/dma.h"   // dsy_dma_invalidate_cache_for_buffer

namespace shell {

namespace {

// libDaisy's EraseSector() sends its address unmasked as the 24-bit command
// address; Write() masks with 0x0FFFFFFF itself. Both get the chip offset,
// as libDaisy's own PersistentStorage passes it.
constexpr uint32_t kQspiBase = 0x90000000u;

uint32_t sector_addr(int s) { return s == 0 ? kPersistSector0 : kPersistSector1; }

const uint8_t* sector_ptr(int s)
{
    return reinterpret_cast<const uint8_t*>(sector_addr(s));
}

// The QSPI window is cacheable (no MPU region covers it, so the default
// write-through attributes apply), and the data cache can hold the sectors as
// they were before an erase or a program behind its back. Every read of the
// sectors goes through this first, as libDaisy's PersistentStorage does.
void invalidate(uint32_t addr, uint32_t n)
{
    dsy_dma_invalidate_cache_for_buffer(reinterpret_cast<uint8_t*>(addr), n);
}

SlotPick g_boot_pick{-1, 0, 0};
bool     g_boot_excite[2] = {false, false};

// Sticky: libDaisy failed to put the QSPI back into memory-mapped mode after
// an erase or a program (its SetMode() retries once and then reports
// E_SWITCHING_MODES). A read of the window would then fault the foreground,
// so from here on nothing reads it and no save is tried; the audio path
// needs no QSPI and keeps playing.
bool g_unmapped = false;

bool mapped(daisy::QSPIHandle& q)
{
    // Both: a failed SetMode() resets the mode field to MEMORY_MAPPED before
    // its retry, so the field alone says nothing; the status does.
    if(q.GetConfig().mode != daisy::QSPIHandle::Config::Mode::MEMORY_MAPPED
       || q.GetStatus() == daisy::QSPIHandle::Status::E_SWITCHING_MODES)
        g_unmapped = true;
    return !g_unmapped;
}

// libDaisy's Write() takes a non-const buffer; the record is copied here
// first. Static, not on the stack: 540 B in the foreground loop's frame for
// one call every few minutes is not worth the stack.
PersistRecord g_wbuf;

} // namespace

SlotPick persist_latest()
{
    if(g_unmapped) return SlotPick{-1, 0, 0};
    invalidate(kPersistSector0, 2 * kPersistSectorSize);
    return pick_latest(sector_ptr(0), sector_ptr(1), current_layout());
}

void persist_boot(float* knobs, bool excite_out[2])
{
    g_boot_pick = persist_latest();
    // Copied out: the flash bytes carry no alignment promise for the floats
    // (persist_record.cpp reads the slots the same way).
    PersistRecord        rec;
    const PersistRecord* found = nullptr;
    if(g_boot_pick.sector >= 0)
    {
        std::memcpy(&rec, sector_ptr(g_boot_pick.sector) + g_boot_pick.slot * kPersistStride,
                    sizeof rec);
        found = &rec;
    }
    // Init patch, the record over it, minus REC_A / REC_B / MODBTN (spec 4.3).
    boot_overlay(found, knobs, excite_out);
    g_boot_excite[0] = excite_out[0];
    g_boot_excite[1] = excite_out[1];
}

const SlotPick& persist_boot_pick() { return g_boot_pick; }
const bool*     persist_boot_excite() { return g_boot_excite; }

bool persist_write(daisy::QSPIHandle& q, const PersistRecord& r)
{
    // Where it goes is decided on the flash as it is now, not on what boot
    // saw: the scan is cheap next to an erase, and a slot torn by an earlier
    // failed save is then simply skipped (next_slot() takes blank slots only).
    if(!mapped(q)) return false;
    const SlotPick latest = persist_latest();
    const SlotNext next   = next_slot(sector_ptr(0), sector_ptr(1), latest);
    const uint32_t sec    = sector_addr(next.sector);
    const uint32_t addr   = sec + static_cast<uint32_t>(next.slot) * kPersistStride;

    bool ok = true;
    // The roll (spec 5.2): the sector being erased never holds the newest
    // record -- that one sits in the full sector next_slot() rolls away from
    // -- so a power cut from here on still leaves one valid record.
    if(next.erase_first)
        ok = q.EraseSector(sec - kQspiBase) == daisy::QSPIHandle::Result::OK;
    if(ok)
    {
        g_wbuf = r;
        // Write() ignores its per-page results and reports OK regardless; the
        // read-back below is the real verdict.
        ok = q.Write(addr - kQspiBase, sizeof g_wbuf, reinterpret_cast<uint8_t*>(&g_wbuf))
             == daisy::QSPIHandle::Result::OK;
    }

    // Memory-mapped again: both calls restore the mode before they return,
    // and this checks that they did before anything reads the window.
    if(!mapped(q)) return false;
    invalidate(sec, kPersistSectorSize);
    PersistRecord back;
    std::memcpy(&back, reinterpret_cast<const void*>(addr), sizeof back);
    return ok && std::memcmp(&back, &r, sizeof back) == 0 && record_valid(back, current_layout());
}

} // namespace shell

#endif // SHELL_PERSIST_ON
