#pragma once

// The callback profile (CPU headroom round, 2026-10-18): where the cycles of
// one audio block go, and what the engine did in the blocks that ran hot.
// Built only with SPKY_PROBE_MARKS (make C_USER_FLAGS=-DSPKY_PROBE_MARKS=1,
// on a clean build/); without it every function here is an empty inline.
// The marks are engine/util/probe_mark.h's; this file is their host.
#include <cstdint>
#include "hw/board.h"

namespace shell {

#if defined(SPKY_PROBE_MARKS)
// Before StartAudio: measures what one mark costs and clears the counters.
void cb_profile_init();
// First thing in the callback: starts the block's segment counters.
void cb_profile_block_begin();
// Last thing in the callback, with the whole callback's cycles: books the
// block into the histogram, the per-segment mean and max, and the hot ring.
void cb_profile_block_end(uint32_t cb, bool warm);
// Foreground, once per report loop: prints new hot blocks; every 20th call
// also the mean, max and histogram lines.
void cb_profile_report(bench::Board& hw);
#else
inline void cb_profile_init() {}
inline void cb_profile_block_begin() {}
inline void cb_profile_block_end(uint32_t, bool) {}
inline void cb_profile_report(bench::Board&) {}
#endif

} // namespace shell
