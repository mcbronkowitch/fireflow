// The ITCM loader (spec docs/superpowers/specs/2026-10-18-cpu-headroom-design.md
// section 4.2): copies .itcm_hot (itcm_hot.ld) out of QSPI into ITCM. Called
// once, at the top of main(), right after board_init() -- the QSPI is
// memory-mapped from then on, and nothing that runs before this call may reach
// an ITCM function (shell/itcm_placement.py, rule c). Stays in SRAM itself.
#include "itcm_load.h"

#if SHELL_ITCM

#include <cstdint>
#include <daisy_seed.h>          // CMSIS: __DSB, __ISB
#include "shell_link_stamp.h"    // generated: a fresh SHELL_LINK_STAMP every make run

extern "C" {
// Linker symbols (itcm_hot.ld): addresses, not objects. Both ends are
// ALIGN(32), so the section is a whole number of words.
extern uint32_t __itcm_hot_start__[];
extern uint32_t __itcm_hot_end__[];
extern uint32_t __itcm_hot_load__[];
}

namespace {
// This build's stamp, inside .itcm_hot: it reaches ITCM from shell-qspi.bin,
// while the constant it is compared with below sits in this function's code,
// in shell-sram.bin. Two builds' images flashed as a pair disagree here.
__attribute__((section(".itcm_hot_stamp"), used))
const uint32_t g_itcm_stamp = SHELL_LINK_STAMP;
} // namespace

namespace shell {

bool itcm_load()
{
    // Word by word, not memcpy: newlib-nano's memcpy is a byte loop, and
    // every byte write into ECC-protected ITCM is a read-modify-write of a
    // word that still holds power-up contents.
    const uint32_t* src = __itcm_hot_load__;
    for(uint32_t* dst = __itcm_hot_start__; dst < __itcm_hot_end__; ++dst, ++src)
        *dst = *src;
    // Data writes; the next instruction fetch from ITCM must see them. ITCM
    // has no cache, so the barriers are all it takes.
    __DSB();
    __ISB();
    // Read back through volatile: otherwise the compiler folds the comparison
    // against its own initialiser to true.
    return *static_cast<const volatile uint32_t*>(&g_itcm_stamp) == SHELL_LINK_STAMP;
}

} // namespace shell

#endif // SHELL_ITCM
