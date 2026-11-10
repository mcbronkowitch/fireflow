#pragma once

// The pot hold for card traffic (fault R5: while the card transfers, every
// pot reads noisy; without C_SD2 the cure is to drop the readings of every
// block a transfer touched). Spec 2026-10-31-sd-use-cases-tests-design.md
// section 5, A1, measured there with a tail of 0; spec
// 2026-11-06-sampler-takes-on-sd-design.md section 4.6 for the take image.
//
// Two definitions, one interface: the SD probe image (SHELL_SD=1) defines
// these in sd_probe.cpp, with its per-mode tail and its pass counters; the
// take image (SHELL_TAKES=1) in sd_hold.cpp, with a tail of 0. The Makefile
// never links both (SHELL_TAKES=1 refuses SHELL_SD=1).
#include <cstdint>

namespace shell {

// Set by the foreground right before every card transfer, and again right
// after it returns; cleared by the callback when it takes it.
extern volatile bool g_sd_seen;

// The callback's side, once per block, before the step's readings are looked
// at: true means drop this block's pot readings.
bool sd_hold_block();

// Take image only (sd_hold.cpp). Marks one card call for the hold: busy for
// the whole call, seen again when it ends, so a call longer than one block
// and the block it ends in both drop their readings.
struct SdMark
{
    SdMark();
    ~SdMark();
    SdMark(const SdMark&)            = delete;
    SdMark& operator=(const SdMark&) = delete;
};

// Take image only: the blocks the hold has dropped since boot. The callback
// writes it, the foreground reads it (a save's held= is the difference).
uint32_t sd_held_blocks();

} // namespace shell
