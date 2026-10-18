#pragma once

// Timing marks for the firmware's callback profile (the CPU headroom round,
// 2026-10-18). OFF unless the build defines SPKY_PROBE_MARKS (1 or 2): then
// every SPKY_MARK reads a host-owned counter (the shell points it at the DWT
// cycle counter) and books the interval. Everywhere else -- the
// desktop, VCV, every other firmware image -- both expand to nothing, so the
// engine carries no hardware type and no cost.
//
// A mark names the segment the time FROM NOW ON belongs to; the host books
// the interval since the previous mark to the previous segment.

namespace spky {

enum ProbeSeg {
    PSEG_IDLE = 0,    // outside the profiled callback
    PSEG_SCAN,        // shell: mux step, keys, pot rows
    PSEG_LAW,         // shell: control_tick()
    PSEG_LED,         // shell: law events, LED law, lamp word, span
    PSEG_INST,        // Instrument::process outside the parts, reverb, limiter
    PSEG_CENTER,      // Instrument's control-rate branch (Center::update ...)
    PSEG_A_MOD,       // deck A: SuperModulator::process
    PSEG_A_TICK,      // deck A: fade, engine swap, master cycle, control tick
    PSEG_A_TRIG,      // deck A: trigger and gate edge
    PSEG_A_ENG,       // deck A: the part engine
    PSEG_A_FX,        // deck A: the deck FX chain (FLUX ...)
    PSEG_B_MOD,
    PSEG_B_TICK,
    PSEG_B_TRIG,
    PSEG_B_ENG,
    PSEG_B_FX,
    PSEG_REVERB,      // the shared reverb
    PSEG_LIMIT,       // the master limiter
    PSEG_COUNT
};
// Deck B's segments sit kProbeDeckStride above deck A's.
inline constexpr int kProbeDeckStride = PSEG_B_MOD - PSEG_A_MOD;

// Event bits, OR-ed per block by the host: lane l of deck d wrapped (bit
// d*10 + l) or fired (d*10 + 5 + l); a trigger (20 + d); a control tick off
// the raster, on a fire (22 + d); an engine swap (24 + d).
inline constexpr int kProbeEvTrigger  = 20;
inline constexpr int kProbeEvFireTick = 22;
inline constexpr int kProbeEvSwap     = 24;

} // namespace spky

#if defined(SPKY_PROBE_MARKS)
// Inline on purpose: a call per mark cost so much at ~1500 marks a block that
// the first profile image (20c46513) overran every block. The clock and the
// counters belong to the host (shell/cb_profile.cpp); the engine only sees a
// pointer to a free-running counter.
#include <cstdint>
namespace spky {
extern volatile uint32_t* probe_clock;
extern uint32_t           probe_acc[PSEG_COUNT];
extern int                probe_cur;
extern uint32_t           probe_last;
extern uint32_t           probe_ev;
inline void probe_mark(int seg) {
    const uint32_t t = *probe_clock;
    probe_acc[probe_cur] += t - probe_last;
    probe_last = t;
    probe_cur  = seg;
}
} // namespace spky
#define SPKY_MARK(seg)        ::spky::probe_mark(seg)
#define SPKY_PROBE_EVENT(bit) (::spky::probe_ev |= 1u << (bit))
// Level 2 also splits each deck into MOD / TICK / TRIG / ENG / FX. At level 1
// the whole deck is booked to its MOD segment.
#if SPKY_PROBE_MARKS >= 2
#define SPKY_MARK2(seg)       ::spky::probe_mark(seg)
#else
#define SPKY_MARK2(seg)       ((void)0)
#endif
#else
#define SPKY_MARK(seg)        ((void)0)
#define SPKY_MARK2(seg)       ((void)0)
#define SPKY_PROBE_EVENT(bit) ((void)0)
#endif
