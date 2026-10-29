#pragma once

// The callback profile (CPU headroom round, 2026-10-18): what one audio block
// costs, where inside process() the expensive sample sits, and what the
// modulation lanes did in that block. Built only with SPKY_PROBE_MARKS
// (make C_USER_FLAGS=-DSPKY_PROBE_MARKS=1, on a clean build/); without it
// every function here is an empty inline. The per-sample stamps are
// engine/util/probe_mark.h's; this file is their host.
#include <cstdint>
#include "hw/board.h"
#include "instrument.h"

namespace shell {

#if defined(SPKY_PROBE_MARKS)
// Before StartAudio: clears the counters.
void cb_profile_init();
// Last thing in the callback: the whole callback's cycles, the law's and the
// LED law's cycles of this block, and the instrument, whose lanes it reads.
void cb_profile_block_end(uint32_t cb, uint32_t law, uint32_t led, bool warm,
                          const spky::Instrument& inst);
// Foreground, once per report loop: prints new ring entries; every 20th call
// also the summary and histogram lines.
void cb_profile_report(bench::Board& hw);
// Audio callback, after process(): this block's process() split as the
// stamps left it -- deck A, deck B and the mix (CBP_REC2's a / b / mix; the
// three sum to proc). The measurement image's SHELL_SEC carries them beside
// the shell's own sections (panel_scan.cpp, SHELL_REMOTE_MEASURE).
void cb_profile_split(uint32_t* a, uint32_t* b, uint32_t* mix);
#else
inline void cb_profile_init() {}
inline void cb_profile_block_end(uint32_t, uint32_t, uint32_t, bool,
                                 const spky::Instrument&) {}
inline void cb_profile_report(bench::Board&) {}
inline void cb_profile_split(uint32_t* a, uint32_t* b, uint32_t* mix) { *a = *b = *mix = 0; }
#endif

} // namespace shell
