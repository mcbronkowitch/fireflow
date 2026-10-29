#pragma once

// The remote CPU hunt's workloads on the board (spec
// docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md sections 5.2 and
// 5.3, plan Task 5): the pure half, host tested (tests/test_workload_run.cpp).
// panel_scan.cpp wires it under SHELL_REMOTE; nothing here touches hardware.
//
//   OverrunGuard        the self-limiter: N consecutive over-budget callbacks,
//                       or N consecutive late entries (back to back), trip
//                       it, and the callback then writes silence instead
//                       of calling process(), so a workload that saturates
//                       the callback cannot cut the USB command channel for
//                       good (USB and the audio DMA share NVIC priority 0, and
//                       the DMA wins). It re-arms itself after
//                       kGuardRearmBlocks silent blocks; LOAD, HOME, RST and a
//                       run's settle end clear it at once (Bastian,
//                       2026-10-29: a patch that overruns for real must still
//                       be counted, trips and all).
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
// ~930-960 k (docs/gotchas.md). 965 000 for 8 blocks in a row: 16 ms of
// sustained overrun, well past anything a single expensive tick produces.
// Raised from 950 000 on 2026-10-29: Bastian's patch peaks at 959-968 k on
// its own and tripped the old limit every ~20 s without saturating
// (docs/hardware/cpu-hunt-measured.md section 1). Back-to-back saturation
// is the period criterion's job (kGuardPeriodCycles).
inline constexpr uint32_t kGuardLimitCycles = 965000;
inline constexpr uint32_t kGuardBlocks      = 8;
// The back-to-back signature (final review I2): cb alone misses a callback
// that saturates with cb under kGuardLimitCycles, because 12-30 k of every
// block run outside cb's window (libDaisy's sample conversion, the interrupt's
// entry and exit, the code ahead of cb0, the measurement image's probe tail;
// docs/gotchas.md). The audio DMA interrupt comes every 960 000 cycles, so the
// period from one callback's entry to the next can stay above that only while
// every callback starts later than the one before: the next interrupt is
// already pending when the callback returns. A single late entry (another
// priority-0 interrupt in the way) is followed by a short period and makes no
// streak. 5 000 cycles of margin for that entry jitter: chosen, not measured
// (plan Task 7 Step 5b). A callback back to back by less than the margin per
// block is not caught (spec section 8).
inline constexpr uint32_t kGuardPeriodCycles = 965000;
// A tripped guard re-arms itself once it has been silent for this many
// consecutive blocks: 1 s at 96 samples / 48 kHz. Bastian's patch overruns for
// real, the LOAD's engine switch trips the guard once and the patch again at a
// fixed time later (docs/hardware/cpu-hunt-measured.md section 1); a guard
// that latches until RST / LOAD / HOME makes a run on it silent and counts
// nothing.
inline constexpr uint32_t kGuardRearmBlocks = 500;

// Which criterion tripped the guard; None while it is not tripped.
enum class GuardCause : uint8_t { None = 0, Cycles = 1, Period = 2 };

class OverrunGuard
{
public:
    // limit: a callback above it is over budget; period_limit: an entry-to-
    // entry period above it is late. Without a period_limit only cb counts.
    // rearm_blocks: silent blocks until a tripped guard re-arms itself.
    OverrunGuard(uint32_t limit, uint32_t n, uint32_t period_limit = 0xFFFFFFFFu,
                 uint32_t rearm_blocks = kGuardRearmBlocks)
        : limit_(limit), n_(n), period_limit_(period_limit), rearm_(rearm_blocks) {}
    // One callback: its cycles, and the cycles from the previous callback's
    // entry to this one's (0: unknown, no previous entry). Each criterion keeps
    // its own streak -- a block over it extends it, any other ends it -- and the
    // first to reach n trips the guard (cause()). A tripped guard ignores cb
    // and period (the silenced callback is cheap) and counts the blocks fed
    // while it is tripped: the rearm_blocks-th of them clears it again, by
    // itself, as clear() does (streaks restart from 0, cause() is None), and
    // that call returns false -- the next callback runs. Returns tripped().
    bool block(uint32_t cb, uint32_t period = 0);
    bool tripped() const { return tripped_; }
    GuardCause cause() const { return cause_; }
    void clear();
    // Both streaks and the silence count start again from 0; a tripped guard
    // stays tripped. For the measurement image's bit 31, which stops feeding
    // block(): without this, the streaks would freeze while the bit is set and
    // resume stale after it, and blocks that were not silent would count
    // towards the re-arm.
    void reset_streak();

private:
    uint32_t   limit_;
    uint32_t   n_;
    uint32_t   period_limit_;
    uint32_t   rearm_;
    uint32_t   streak_  = 0;
    uint32_t   pstreak_ = 0;
    uint32_t   silent_  = 0;
    bool       tripped_ = false;
    GuardCause cause_   = GuardCause::None;
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
    // The overrun guard tripped during the run: from that block on its callbacks
    // are silence, not the workload, until the guard re-arms (spec 5.2: a worst
    // case measured on silence does not count; the host splits the frames by
    // whether the guard was tripped in them). trips() counts the trips since
    // start(), tripped() says there was one. Without a run, nothing. Cleared
    // only by start() / stop().
    void on_guard_trip();
    // A TOG in block now_block (the measurement image): the run settles again,
    // so the toggle's switch-over is not counted -- nothing counts before
    // now_block + settle_blocks, or before the run's own settle end when that
    // is later (a TOG right after the LOAD keeps the LOAD's settle) -- and gen
    // is bumped, so no frame window holding blocks from before the TOG is the
    // same gen as one after it. The gesture clock (counted()) pauses: it
    // resumes from the value it had in now_block. void and tripped stay, they
    // are about the run, not its counting. Without a run, nothing.
    void resettle(uint32_t now_block, uint32_t settle_blocks);

    bool active() const { return active_; }
    // Settled: now_block is at least settle blocks past the start (wraps).
    bool counting(uint32_t now_block) const;
    // now_block is the first counted block (again, after a resettle()).
    bool settle_ends(uint32_t now_block) const;
    // Counted blocks before now_block (0 in the first counted one); across a
    // resettle() the count goes on from where it paused.
    uint32_t counted(uint32_t now_block) const;
    bool     void_run() const { return void_; }
    bool     tripped() const { return tripped_; }
    uint32_t trips() const { return trips_; }
    int      id() const { return active_ ? id_ : -1; }
    // Bumped by every start(): two readings with the same gen are one run.
    uint32_t gen() const { return gen_; }

private:
    bool     active_ = false;
    bool     void_   = false;
    bool     tripped_ = false;
    uint32_t trips_  = 0;
    int      id_     = -1;
    uint32_t start_  = 0;
    uint32_t settle_ = 0;
    uint32_t gen_    = 0;
    uint32_t offset_ = 0;   // counted() before the last resettle()
};

// A run's settle ends in block now_block (WorkloadRun::settle_ends(), after a
// TOG's re-settle too): a guard tripped by the load's own transient -- an
// engine switch costs a block of 1 001 608 cycles on Bastian's patch -- must not
// silence the counted blocks, so it is cleared there, exactly when counting
// starts. True when it was tripped and is not any more (the caller says so);
// false when nothing ended or nothing was tripped.
bool rearm_at_settle_end(OverrunGuard& g, const WorkloadRun& r, uint32_t now_block);

// The measurement image's TOG settle: the maxima reset kTogSettleBlocks after
// the block the TOG landed in, by block number, so the answer does not depend
// on whether remote_block() asks before or after it handles the command.
class TogSettle
{
public:
    explicit TogSettle(uint32_t blocks) : blocks_(blocks) {}
    // A TOG in block now_block; a second one before the first settled re-arms.
    void arm(uint32_t now_block);
    // True once, in block arm + blocks: the TOG's block and the blocks-1 after
    // it are the switch-over (wraps).
    bool ends(uint32_t now_block);

private:
    uint32_t blocks_;
    uint32_t at_    = 0;
    bool     armed_ = false;
};

// One block of the outside-window loop (panel_scan.cpp busy_window()): busy is
// the cycles from the jump that held its callback to the one that holds the
// next, cb the callback's measured window, tail the cycles the callback spends
// after that window (the measurement image's own block-end bookkeeping: guard,
// WIN, sections, cb_profile). out = busy - cb, with the tail in it; ext = out -
// tail, the time outside the callback without it (USB, SysTick, the audio
// interrupt's entry and exit). Both saturate at 0; neg says one of them did
// (busy under cb, or out under tail), so a block whose figures do not add up
// is counted (SHELL_OUT neg=) instead of hiding in a 0.
struct OutSplit
{
    uint32_t out, ext;
    bool     neg;
};
OutSplit out_split(uint32_t busy, uint32_t cb, uint32_t tail);

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
