// Probe (CPU headroom round, 2026-10-18): the wavetable bank copied out of
// QSPI into D2 SRAM at boot, so WAVE's table reads hit cacheable RAM instead
// of memory-mapped QSPI. Built only with SPKY_WT_BANK_RAM; the question is
// how much of deck B's cost is the bank's location.
#if defined(SPKY_WT_BANK_RAM)

#include <cstring>
#include "synth/wt_bank.h"

namespace spky::wt {
const int16_t* g_bank = kBankSamples;
} // namespace spky::wt

namespace {
// .d2_bss lands in RAM_D2CACHE (alt_sram.lds), the cacheable part of D2 SRAM
// behind libDaisy's 32 KB non-cacheable DMA window.
int16_t __attribute__((section(".d2_bss"))) g_bank_ram[spky::wt::kTotalSamples];
} // namespace

namespace shell {
void wt_bank_to_ram()
{
    std::memcpy(g_bank_ram, spky::wt::kBankSamples, sizeof g_bank_ram);
    spky::wt::g_bank = g_bank_ram;
}
} // namespace shell

#endif // SPKY_WT_BANK_RAM
