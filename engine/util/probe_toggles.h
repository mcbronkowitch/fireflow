#pragma once

// Candidate toggles for the firmware's remote CPU hunt (spec
// docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md section 5.3,
// plan Task 6). OFF unless the build defines SPKY_PROBE_TOGGLES, which only
// shell/'s measurement image does (SHELL_REMOTE_MEASURE=1): there the shell's
// `TOG <bit> <0|1>` command sets a bit of one word, and a candidate saving in
// engine/ runs its new path only while its bit is set, so the same image
// measures both paths against an interleaved baseline.
//
// THE RULE (spec 5.3, docs/gotchas.md): the word is read ONCE per block, at
// the top of Instrument::process, into the Instrument's _probe_tog, and a
// candidate tests that member -- never toggles() itself, and never a global
// inside a per-sample loop: global stores in that loop once pushed every
// block over budget (probe_mark.h says the same). shell/test_remote_wiring.py
// holds that toggles() is called in instrument.cpp alone, before the loop.
//
// Everywhere else -- the desktop, VCV, every other firmware image --
// toggles() is a constexpr 0, Instrument::_probe_tog a static constexpr 0
// (instrument.h), and on() a constant false, so every candidate branch
// compiles away and those builds carry neither the member nor the read.
//
// The bits. Each candidate claims one bit here, with its name and file; a
// bit is never reused within the round.
//
//   bit  name                 file                       what it switches
//   ---  -------------------  -------------------------  -----------------
//   0    GRIT_DECIM_STEP      engine/fx/grit.cpp         RETIRED: adopted, the
//                                                        only path now (the
//                                                        discarded Decimator
//                                                        step does state only)
//   1-29 (free: candidates; bit 0 is never reused)
//   30   SHELL_LATE_REPEAT    shell/panel_scan.cpp       repeat the last LATE
//                                                        burn on every block
//   31   SHELL_GUARD_OFF      shell/panel_scan.cpp       the overrun guard is
//                                                        not consulted
//
// Bits 30 and 31 are the shell's, in the measurement image only: together
// they saturate the callback on purpose (the BOOT-under-load test, plan
// Task 7). The engine never reads them.

#include <cstdint>

namespace spky {
namespace probe {

// The shell's two bits (above).
inline constexpr int kBitShellLateRepeat = 30;
inline constexpr int kBitShellGuardOff   = 31;

#if defined(SPKY_PROBE_TOGGLES)
inline constexpr bool kTogglesOn = true;
// One writer, the shell's audio callback (the TOG command is handled there),
// and one reader per block, Instrument::process, in the same interrupt: no
// volatile needed, and none wanted.
inline uint32_t g_toggles = 0;
inline uint32_t toggles() { return g_toggles; }
inline void     set_toggles(uint32_t word) { g_toggles = word; }
#else
inline constexpr bool kTogglesOn = false;
constexpr uint32_t toggles() { return 0; }
inline void        set_toggles(uint32_t) {}
#endif

// Whether `bit` is set in a word latched from toggles(). A constant false
// without SPKY_PROBE_TOGGLES, so `if(probe::on(_probe_tog, k))` folds away.
constexpr bool on(uint32_t word, int bit)
{
    return kTogglesOn && ((word >> bit) & 1u) != 0u;
}

} // namespace probe
} // namespace spky
