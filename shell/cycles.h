#pragma once

// The Cortex-M7 DWT cycle counter. Free-running at the core clock (480 MHz),
// so one tick is 2.08 ns and a delay grid in nanoseconds is a plain integer
// compare.
//
// COPIED from bench/cycles.h rather than included across trees:
// shell/README.md is explicit that exactly one file is shared with bench/
// (src/hw/board.h) and that the separation is deliberate. The origin is named
// here so the trap below is not re-learned from scratch.
//
// THE TRAP: the M7 gates the DWT registers behind a lock and LAR must be
// unlocked before CYCCNT counts at all. The M3/M4 examples on the web omit
// this, and without it the counter reads zero forever -- which looks like an
// instrument that is infinitely fast.
#include <cstdint>
#include <daisy_seed.h>   // pulls in the CMSIS core headers for CoreDebug/DWT

namespace shell {

inline void cycles_init()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR   = 0xC5ACCE55;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

inline uint32_t cycles_now()
{
    return DWT->CYCCNT;
}

// 480 MHz core clock. Integer arithmetic on purpose: this runs inside the
// timed path and a float divide there would be measuring the measurement.
inline constexpr uint32_t kCoreMhz = 480;

constexpr uint32_t ns_to_cycles(uint32_t ns)
{
    return ns * kCoreMhz / 1000u;
}

constexpr uint32_t cycles_to_ns(uint32_t cyc)
{
    return cyc * 1000u / kCoreMhz;
}

} // namespace shell
