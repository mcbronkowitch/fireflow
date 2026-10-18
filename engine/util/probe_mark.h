#pragma once

// Per-sample timestamps for the firmware's callback profile (the CPU headroom
// round, 2026-10-18). OFF unless the build defines SPKY_PROBE_MARKS: then
// Instrument::process stamps a host-owned counter (the shell points it at the
// DWT cycle counter) at the top of every sample into a LOCAL array and hands
// the array to the host once per block. Everywhere else -- the desktop, VCV,
// every other firmware image -- the macros expand to nothing, so the engine
// carries no hardware type and no cost.
//
// Why so little: the first two profile images (20c46513, 37830e2d) booked
// time to segments through global counters written inside the sample loop,
// and both overran on every block -- the global stores cost far more than
// their instruction count (the hot loop has to re-load engine state across
// them). A store into a stack array whose address never escapes until the
// loop is over does not have that effect.

#if defined(SPKY_PROBE_MARKS)
#include <cstdint>
namespace spky {
extern volatile uint32_t* probe_clock;          // defined by the host
// ts[0..n]: the counter at the top of each sample, ts[n] after the last.
void probe_block(const uint32_t* ts, int n);    // defined by the host
} // namespace spky
#define SPKY_PROBE_DECL          uint32_t _probe_ts[129]; \
                                 volatile uint32_t* const _probe_clk = ::spky::probe_clock
#define SPKY_PROBE_STAMP(i)      (_probe_ts[(i) < 128 ? (i) : 128] = *_probe_clk)
#define SPKY_PROBE_END(n)        ::spky::probe_block(_probe_ts, static_cast<int>((n) < 128 ? (n) : 128))
#else
#define SPKY_PROBE_DECL          static_assert(true, "")
#define SPKY_PROBE_STAMP(i)      ((void)0)
#define SPKY_PROBE_END(n)        ((void)0)
#endif
