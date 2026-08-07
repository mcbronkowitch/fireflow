#pragma once

// The bench knows two boards. The Seed carries the entire measurement history
// in docs/bench/; the Patch Submodule is the M6 target. Same STM32H750, same
// SDRAM and QSPI -- but until the first run on the submodule is through, that
// is an expectation and not a measurement. Which is why the board goes into
// every line of the report.
//
// This lives under src/hw/ and not under bench/ on purpose: the shipping
// firmware needs the same board init, and firmware pointing at the measuring
// tool is exactly the separation bench/README.md draws. A copy in both trees
// would be worse -- the boot_info trap below is not something to maintain
// twice.

#if defined(BENCH_BOARD_PATCH_SM)
#include "daisy_patch_sm.h"
#else
#include "daisy_seed.h"
#endif

namespace bench {

#if defined(BENCH_BOARD_PATCH_SM)
using Board = daisy::patch_sm::DaisyPatchSM;
inline const char* board_name() { return "patch_sm"; }
#else
using Board = daisy::DaisySeed;
inline const char* board_name() { return "seed"; }
#endif

// BOOT_SRAM loads jump straight into the app's reset vector via the debug
// probe (openocd/spotykach-sram.cfg), bypassing the Daisy bootloader entirely.
// DaisySeed::Init() infers "already chained from an old (<v6.0) bootloader
// that brought SDRAM up itself" from a stale/blank .backup_sram boot_info
// marker, and on that inference SKIPS both ConfigureClocks()/ConfigureMpu()
// (System::Config::skip_clocks) AND sdram_handle.Init() outright -- the same
// "bootloader normally does this" gap as the FPU CPACR fix already carried in
// openocd/spotykach-sram.cfg, just for SDRAM instead of the FPU. Stamping
// boot_info to a recognized real-bootloader version before Init() defeats that
// inference so both steps run for real. Confirmed on hardware 2026-07-18:
// without this, the first workload to touch SDRAM (fx_grit's Flux::init(),
// which memsets its injected echo buffer) hits a HardFault -- SCB->CFSR
// IMPRECISERR, traced via arm-none-eabi-addr2line into libDaisy's
// HardFault_Handler.
//
// The USB/DFU path enters through the real bootloader and would not need the
// stamp, but it costs nothing there and the two transports must not diverge in
// what they initialize -- a board that boots differently per transport cannot
// be compared across them.
//
// The submodule is the same story: daisy_patch_sm.cpp:236 checks
// GetBootloaderVersion() == LT_v6_0 for syscfg.skip_clocks, and :245 hangs
// sdram.Init() on the same condition.
//
// The clock is 480 MHz on both boards: DaisySeed::Init(true) boosts by
// argument, DaisyPatchSM::Init() calls syscfg.Boost() unconditionally
// (daisy_patch_sm.cpp:230), with no argument and no way out. Without that
// equality every comparison against the Seed history would be void.
inline void board_init(Board& hw)
{
    daisy::System::InitBackupSram();
    daisy::boot_info.version = daisy::System::BootInfo::Version::v6_1;

#if defined(BENCH_BOARD_PATCH_SM)
    hw.Init();                  // boosts to 480 MHz on its own
#else
    hw.Init(true);              // 480 MHz boost, caches on, SDRAM up
#endif
    hw.SetAudioBlockSize(96);
    hw.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
}

} // namespace bench
