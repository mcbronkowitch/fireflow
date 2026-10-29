#pragma once

// The remote CPU hunt's workloads on the board (spec
// docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md sections 5.2 and
// 5.3, plan Task 5): the pure half, host tested (tests/test_workload_run.cpp).
// panel_scan.cpp wires it under SHELL_REMOTE; nothing here touches hardware.
//
//   OverrunGuard        the self-limiter: N consecutive over-budget callbacks
//                       latch it, and the callback then writes silence instead
//                       of calling process() until LOAD, HOME or RST clears it,
//                       so a workload that saturates the callback cannot cut
//                       the USB command channel for good (USB and the audio
//                       DMA share NVIC priority 0, and the DMA wins).
//   WorkloadRun         one loaded workload: its settle (no block counts until
//                       the engine swap has landed), and void once a pot took
//                       its parameter over during the run (spec 5.2, "Pot
//                       drift").
//   workload_knobs()    the knob vector a LOAD writes, as the desktop sweep
//                       built it (host/sweep/main.cpp render_once()).
//   apply_gesture_step() one gesture step through the live pot / key path.
#include <cstdint>
#include "controls.h"
#include "generated_workloads.h"
#include "keys.h"

namespace shell {

// A 2 ms block is 960 000 cycles at 480 MHz; the measured deadline band is
// ~930-960 k (docs/gotchas.md). 950 000 for 8 blocks in a row: 16 ms of
// sustained overrun, well past anything a single expensive tick produces.
inline constexpr uint32_t kGuardLimitCycles = 950000;
inline constexpr uint32_t kGuardBlocks      = 8;

class OverrunGuard
{
public:
    OverrunGuard(uint32_t limit, uint32_t n) : limit_(limit), n_(n) {}
    // One callback's cycles. A block above the limit extends the streak, any
    // other ends it; the n-th in a row trips the guard, which then stays
    // tripped (the silenced callback is cheap) until clear(). Returns tripped().
    bool block(uint32_t cb);
    bool tripped() const { return tripped_; }
    void clear();

private:
    uint32_t limit_;
    uint32_t n_;
    uint32_t streak_  = 0;
    bool     tripped_ = false;
};

class WorkloadRun
{
public:
    // A new run of workload `id`, loaded in block now_block; the first
    // settle_blocks blocks (the workload's settle_blocks: 500, or 2500 with a
    // SAMPLER deck recording) do not count. Clears void.
    void start(int id, uint32_t now_block, uint32_t settle_blocks);
    // No run (HOME): nothing counts, nothing voids.
    void stop();
    // A pot took its parameter over: the run is void. Without a run, nothing.
    void on_pot_take();

    bool active() const { return active_; }
    // Settled: now_block is at least settle blocks past the start (wraps).
    bool counting(uint32_t now_block) const;
    // now_block is the first counted block.
    bool settle_ends(uint32_t now_block) const;
    // Counted blocks before now_block (0 in the first counted one).
    uint32_t counted(uint32_t now_block) const;
    bool     void_run() const { return void_; }
    int      id() const { return active_ ? id_ : -1; }
    // Bumped by every start(): two readings with the same gen are one run.
    uint32_t gen() const { return gen_; }

private:
    bool     active_ = false;
    bool     void_   = false;
    int      id_     = -1;
    uint32_t start_  = 0;
    uint32_t settle_ = 0;
    uint32_t gen_    = 0;
};

// knobs[0..ffctl::NUM_PARAMS) <- the init patch, the workload's rows through
// apply_boot_patch_rows() (knob_target() / knob_from_pot(), in row order), and
// REC_A / REC_B latched on where w.rec says so. What the sweep loaded, minus
// the fresh Instrument::init() it ran per vector. False (knobs untouched) when
// the table's row count is not the workload's.
bool workload_knobs(const WorkloadDef& w, const ControlTable& t, float* knobs);

// One gesture step, the way the panel would have made it: a pot row through
// pot_target() (MOD-aware: with MOD latched the row writes its depth twin) and
// knob_from_pot(); kGestureRowMod as a MOD press and kGestureRowShiftMod as a
// MOD press with SHIFT held, through apply_key_edges() with the map's bits. A
// row outside the table does nothing. True when the step cleared the MOD layer:
// the caller then releases every pot, as the live SHIFT + MOD does.
bool apply_gesture_step(const GestureStep& s, const ControlTable& t, const KeyMap& map,
                        float* knobs);

} // namespace shell
