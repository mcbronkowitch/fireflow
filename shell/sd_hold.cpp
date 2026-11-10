#include "sd_hold.h"

#include <atomic>

#include "sd_schedule.h"

// The take image's pot hold (sd_hold.h). Linked with SHELL_TAKES=1 alone; the
// SD probe image has its own definitions in sd_probe.cpp.

namespace shell {

volatile bool g_sd_seen = false;

namespace {

// Set for the whole card call. g_sd_seen alone would not cover a call longer
// than one block: the callback clears it at the first block boundary inside
// the call (sd_probe.cpp, g_sd_busy, has the same reason).
volatile bool     g_sd_busy = false;
SdHold            g_hold;               // callback only
volatile uint32_t g_held    = 0;        // written by the callback alone

// A1's smallest passing tail (spec 2026-10-31-sd-use-cases-tests-design.md
// section 5, A1): only the blocks a transfer touched are dropped.
constexpr uint8_t kTakeHoldTail = 0;

} // namespace

SdMark::SdMark()
{
    g_sd_seen = true;
    g_sd_busy = true;
    // The card call must not move above the marks.
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

SdMark::~SdMark()
{
    std::atomic_signal_fence(std::memory_order_seq_cst);
    g_sd_seen = true;
    g_sd_busy = false;
}

bool sd_hold_block()
{
    const bool seen = g_sd_seen || g_sd_busy;
    g_sd_seen       = false;
    const bool drop = g_hold.step(seen, kTakeHoldTail);
    if(drop) g_held = g_held + 1u;
    return drop;
}

uint32_t sd_held_blocks() { return g_held; }

} // namespace shell
