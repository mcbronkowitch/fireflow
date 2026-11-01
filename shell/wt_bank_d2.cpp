// The wavetable bank in D2 SRAM (spec 2026-10-18-cpu-headroom-design.md
// section 4.1). The bank ships in QSPI (.qspiflash_data, shell-qspi.bin) and
// stays there as the source; at boot this copies it into the cacheable part of
// D2 SRAM and points the engine at the copy, so WAVE's table reads hit RAM
// instead of memory-mapped QSPI. Measured on the coupon, init patch, 2026-10-18
// (spec section 2.3): deck B 432 k -> 408 k cycles a block on average, the
// worst block 1 011 k -> 955 k.
#include <cstring>
#include "shell_sd_probe.h"
#include "synth/wt_bank.h"

namespace {
// .d2_bss lands in RAM_D2CACHE (alt_sram.lds): D2 SRAM behind libDaisy's
// 32 KB non-cacheable DMA window at 0x30000000. NOLOAD, so the bootloader's
// copy and the image size do not grow.
//
// Fault R4's test (C_USER_FLAGS=-DSHELL_WT_DTCM=1, clean build): the bank in
// DTCM instead -- zero-wait and uncached, so a pitch change cannot cost a
// D-cache refill there. 65 KB of DTCM's 128 KB; the stack, which grows down
// from its top (_estack), keeps the rest.
#if defined(SHELL_WT_DTCM) && SHELL_WT_DTCM
int16_t __attribute__((section(".dtcmram_bss"))) g_bank_d2[spky::wt::kTotalSamples];
#else
int16_t __attribute__((section(".d2_bss"))) g_bank_d2[spky::wt::kTotalSamples];
#endif
#if SHELL_SD_PROBE_BANK
// The SD probe's bank reload (spec 2026-10-31-sd-use-cases-tests-design.md
// section 5, D1; shell/sd_probe.cpp): a second slot the card loads into while
// the engine reads the other, swapped at the top of a block. In this image
// alone (write_shell_sd_probe.py deletes this object when the mode changes).
int16_t __attribute__((section(".d2_bss"))) g_bank_d2b[spky::wt::kTotalSamples];
#endif
} // namespace

namespace shell {

void wt_bank_to_d2()
{
    std::memcpy(g_bank_d2, spky::wt::kBankSamples, sizeof g_bank_d2);
    spky::wt::set_bank(g_bank_d2);
}

// Slot 0 is the boot copy above; slot 1 exists with SHELL_SD_PROBE=bank only
// (nullptr otherwise).
int16_t* wt_bank_slot(int i)
{
    if(i == 0)
        return g_bank_d2;
#if SHELL_SD_PROBE_BANK
    if(i == 1)
        return g_bank_d2b;
#endif
    return nullptr;
}

} // namespace shell
