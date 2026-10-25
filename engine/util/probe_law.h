#pragma once

// The control law's TIDE split for the firmware (fault R4, 2026-10-25). OFF
// unless the build defines SPKY_PROBE_LAW: then the TIDE path stamps a
// host-owned counter (the shell points it at the DWT cycle counter) around
// its parts and sums the cycles into probe_law, which the shell clears
// before each control tick and latches with the block that sets cyc_max.
// Everywhere else the macros expand to nothing.
//
// Independent of SPKY_PROBE_MARKS (probe_mark.h): that one stamps the sample
// loop and changes Part::process's signature; this one only touches setters
// that run on the control tick, a few dozen times per block, so global
// stores are fine here, as for ProbeSwap.
//
// What is summed, over every call inside one control tick (both decks):
//   tide   SuperModulator::set_tide, early-out included
//   steps  SuperModulator::_apply_steps (inside tide, or inside set_step)
//   reslot ModLane::set_step's live re-slot branch (inside steps)
//   sinc   ModLane::set_step's closing _update_inc, the slew rebuild with it
//   rate   SuperModulator::_apply_rate (inside tide, set_step or a rate push)
//   rinc   ModLane::set_rate_hz's _update_inc (inside rate)
// with the call counts n_tide, n_reslot, n_sinc and n_rinc.

#if defined(SPKY_PROBE_LAW)
#include <cstdint>
namespace spky {
struct ProbeLaw {
    uint32_t tide, steps, reslot, sinc, rate, rinc;
    uint32_t n_tide, n_reslot, n_sinc, n_rinc;
};
extern ProbeLaw probe_law;                     // defined in super_modulator.cpp
extern volatile uint32_t* probe_law_clock;     // ditto; the host points it
} // namespace spky
#define SPKY_PROBE_LAW_BEGIN(tag)    const uint32_t _probe_law_##tag = *::spky::probe_law_clock
#define SPKY_PROBE_LAW_END(tag, f)   (::spky::probe_law.f += *::spky::probe_law_clock - _probe_law_##tag)
#define SPKY_PROBE_LAW_COUNT(f)      (::spky::probe_law.f = ::spky::probe_law.f + 1u)
#else
#define SPKY_PROBE_LAW_BEGIN(tag)    static_assert(true, "")
#define SPKY_PROBE_LAW_END(tag, f)   ((void)0)
#define SPKY_PROBE_LAW_COUNT(f)      ((void)0)
#endif
