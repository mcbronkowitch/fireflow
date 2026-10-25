// The wavetable bank in D2 SRAM (spec 2026-10-18-cpu-headroom-design.md
// section 4.1). The bank ships in QSPI (.qspiflash_data, shell-qspi.bin) and
// stays there as the source; at boot this copies it into the cacheable part of
// D2 SRAM and points the engine at the copy, so WAVE's table reads hit RAM
// instead of memory-mapped QSPI. Measured on the coupon, init patch, 2026-10-18
// (spec section 2.3): deck B 432 k -> 408 k cycles a block on average, the
// worst block 1 011 k -> 955 k.
#include <cstring>
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
} // namespace

namespace shell {

void wt_bank_to_d2()
{
    std::memcpy(g_bank_d2, spky::wt::kBankSamples, sizeof g_bank_d2);
    spky::wt::set_bank(g_bank_d2);
}

} // namespace shell
