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
// their instruction count (why is not measured; docs/gotchas.md). A store
// into a stack array whose address never escapes until the loop is over
// measured cheap (cbc0545c).

#if defined(SPKY_PROBE_MARKS)
#include <cstdint>
namespace spky {
extern volatile uint32_t* probe_clock;          // defined by the host
// Six stamps per deck call, written into the stack array the caller passes
// in. Part::process: [0] after the modulation and control half (just before
// the engine runs), [1] after the engine (just before the FX chain).
// PartFx::process, handed the same array: [2] after the target smoothers,
// [3] after GRIT, [4] after FLUX, [5] after the tap, the FX mix and COMP
// (just before the send law). Deck end = the caller's d1 / d2 stamp.
using ProbePartSlots = uint32_t[6];
// Where the Part::process overloads that take no slots write their stamps
// (tests, the bench's single-part workloads): nobody reads it. Defined by
// the host.
extern ProbePartSlots probe_part_scratch;
// ts[0..n]: the counter at the top of each sample, ts[n] after the last;
// d1[i] / d2[i]: after the priority / the yielding deck's process() of
// sample i; p1[i] / p2[i]: the priority / the yielding deck's two inner
// stamps of sample i (fault R4, 2026-10-24: the deck split into mod, engine
// and FX). pri is the priority deck (PART_A or PART_B) of the block's last
// sample.
void probe_block(const uint32_t* ts, const uint32_t* d1, const uint32_t* d2,
                 const ProbePartSlots* p1, const ProbePartSlots* p2,
                 int n, int pri);                // defined by the host
} // namespace spky
#define SPKY_PROBE_DECL          uint32_t _probe_ts[129], _probe_d1[128], _probe_d2[128]; \
                                 ::spky::ProbePartSlots _probe_p1[128], _probe_p2[128]; \
                                 int _probe_pri = 0; \
                                 volatile uint32_t* const _probe_clk = ::spky::probe_clock
#define SPKY_PROBE_IDX(i)        ((i) < 128 ? (i) : 127)
#define SPKY_PROBE_STAMP(i)      (_probe_ts[(i) < 128 ? (i) : 128] = *_probe_clk)
#define SPKY_PROBE_DECK1(i, p)   (_probe_d1[SPKY_PROBE_IDX(i)] = *_probe_clk, _probe_pri = (p))
#define SPKY_PROBE_DECK2(i)      (_probe_d2[SPKY_PROBE_IDX(i)] = *_probe_clk)
#define SPKY_PROBE_END(n)        ::spky::probe_block(_probe_ts, _probe_d1, _probe_d2, \
                                     _probe_p1, _probe_p2, \
                                     static_cast<int>((n) < 128 ? (n) : 128), _probe_pri)
// Part::process's side: the clock and the slots travel as trailing parameters
// of the always-inlined call, so after inlining they are the caller's locals.
#define SPKY_PROBE_PART_PARAMS   , volatile uint32_t* const _probe_clk, uint32_t* const _probe_slot
#define SPKY_PROBE_PART_ARGS(s)  , _probe_clk, (s)
#define SPKY_PROBE_PART_ARGS1(i) SPKY_PROBE_PART_ARGS(_probe_p1[SPKY_PROBE_IDX(i)])
#define SPKY_PROBE_PART_ARGS2(i) SPKY_PROBE_PART_ARGS(_probe_p2[SPKY_PROBE_IDX(i)])
#define SPKY_PROBE_PART_SCRATCH  , ::spky::probe_clock, ::spky::probe_part_scratch
#define SPKY_PROBE_PART_FWD      , _probe_clk, _probe_slot
#define SPKY_PROBE_PART_MARK(k)  (_probe_slot[k] = *_probe_clk)
namespace spky {
// The engine swap's own cost (fault R2, 2026-10-24): Part::_engine_swap stamps
// the clock around the whole swap and around BbdEngine::reset() inside it, and
// the host prints the figures. A swap runs once per pot turn, so the global
// stores the sample loop cannot afford are fine here.
struct ProbeSwap {
    uint32_t n;            // swaps so far
    int      eng;          // the engine swapped in last
    uint32_t total, bbd;   // the last swap: whole swap / BbdEngine::reset() in it
    uint32_t total_max, bbd_max;
};
extern ProbeSwap probe_swap;                    // defined in part.cpp
} // namespace spky
#else
#define SPKY_PROBE_DECL          static_assert(true, "")
#define SPKY_PROBE_STAMP(i)      ((void)0)
#define SPKY_PROBE_DECK1(i, p)   ((void)0)
#define SPKY_PROBE_DECK2(i)      ((void)0)
#define SPKY_PROBE_END(n)        ((void)0)
#define SPKY_PROBE_PART_PARAMS
#define SPKY_PROBE_PART_ARGS1(i)
#define SPKY_PROBE_PART_ARGS2(i)
#define SPKY_PROBE_PART_SCRATCH
#define SPKY_PROBE_PART_FWD
#define SPKY_PROBE_PART_MARK(k)  ((void)0)
#endif
