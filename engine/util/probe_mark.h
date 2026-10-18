#pragma once

// Timing marks for the firmware's callback profile (the CPU headroom round,
// 2026-10-18). OFF unless the build defines SPKY_PROBE_MARKS: then every
// SPKY_MARK / SPKY_PROBE_EVENT below is a call into the host, which owns the
// clock (the shell reads the DWT cycle counter). Everywhere else -- the
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
namespace spky {
void probe_mark(int seg);     // defined by the host
void probe_event(int bit);    // defined by the host
} // namespace spky
#define SPKY_MARK(seg)        ::spky::probe_mark(seg)
#define SPKY_PROBE_EVENT(bit) ::spky::probe_event(bit)
#else
#define SPKY_MARK(seg)        ((void)0)
#define SPKY_PROBE_EVENT(bit) ((void)0)
#endif
