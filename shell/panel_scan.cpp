#include "panel_scan.h"

#include "shell_panel_scan.h"
#include "shell_coupon_probe.h"
#include "shell_keys_nc.h"
#include "shell_raw_spread.h"
#include "shell_remote.h"
#include "shell_bootcmd.h"
#include "shell_remote_measure.h"
#include "shell_sd.h"

#if SHELL_PANEL_SCAN

#include <atomic>
#include <cstdio>

#include "control/led_law.hpp"
#include "cb_profile.h"
#include "control_tick.h"
#include "controls.h"
#include "coupon_expect.h"
#include "cycles.h"
#include "keys.h"
#include "led_out.h"
#include "mux_scan.h"
#include "panel_step.h"
#include "pot_owner.h"
#include "scan_value.h"
#include "util/probe_law.h"
#include "persist_flash.h"
#if SHELL_PERSIST_ON
#include <cmath>
#include <cstring>
#include "boot_take.h"
#include "persist_policy.h"
#endif
#if SHELL_REMOTE
#include <cstring>
#include "generated_workloads.h"
#include "remote_listen.h"
#include "workload_run.h"
// The run line's tog= field: a constexpr 0 outside the measurement image.
#include "util/probe_toggles.h"
#endif
#if SHELL_BOOTCMD
#if SHELL_REMOTE
#error "SHELL_BOOTCMD is the play image's boot-only listener; with SHELL_REMOTE the Makefile writes 0"
#endif
// The boot-only listener (shell/Makefile, SHELL_BOOTCMD): BOOT and PING only,
// both handled in the USB interrupt; nothing here ever pops the command ring.
#include "remote_listen.h"
#endif
#if SHELL_REMOTE_MEASURE
#include <algorithm>
#endif
#if SHELL_SD
#include "sd_probe.h"
#endif
#if !SHELL_COUPON_PROBE
#include "control/clock_law.hpp"
#include "gate_latch.h"
#include "generated_panel_map.h"
#endif

namespace shell {

namespace {

#if SHELL_COUPON_PROBE
constexpr ControlTable  kTable  = kCouponTable;
constexpr KeyPad        kKeys   = kCouponKeys;
constexpr KeyMap        kKeyMap = kCouponKeyMap;
constexpr const int8_t* kLedBit = kCouponLedBit;
#else
constexpr ControlTable  kTable  = kRevaTable;
// SHELL_KEYS_NC=1: Rev A as assembled reads its keys closed at rest
// (reva-bring-up.md 6.1). A workaround; the finding stays in the fault list.
#if SHELL_KEYS_NC
constexpr KeyPad        kKeys   = closed_at_rest(kRevaKeys);
#else
constexpr KeyPad        kKeys   = kRevaKeys;
#endif
constexpr KeyMap        kKeyMap = kRevaKeyMap;
constexpr const int8_t* kLedBit = kRevaLedBit;
#endif

constexpr int kSteps    = scan_steps(kActiveChain);
constexpr int kChannels = mux_total(kActiveChain);

MuxScan   g_scan;
PotFilter g_filter[kChannels];
KeyState  g_keys;

// Pot ownership (spec 2026-10-18-rev-a-p6b2 section 2), one per mux channel
// like g_filter. All released at boot, so the init patch plays.
PotOwner g_owner[kChannels];
// The debounced key mask of the previous block: a key acts on its press edge.
uint8_t  g_last_pressed = 0;
bool     g_mod_cleared  = false;   // SHIFT + MOD since the last led_tick()
// The mux channel of the FLUX_A / FLUX_B row, -1 when the board has none (the
// coupon): a bbd_edge event releases that pot (spec section 5).
int      g_flux_idx[2] = {-1, -1};
#if !SHELL_COUPON_PROBE
// CLOCK and RESET (spec 2026-10-19-clock-and-reset-jacks): the shared clock
// law, fed once per block from the EXTI pending bits (gate_latch.h). Rev A
// only: the coupon's gate pins float. g_tempo_idx is the TEMPO row's mux
// channel, whose pot is released on clock loss (apply_clock_events()).
control::ClockLaw g_clock;
int               g_tempo_idx = -1;
#endif

// Last emitted value per channel, -1 = never emitted. Read by the
// foreground for SHELL_PLAY only.
volatile float g_value[kChannels];

// The span the value path uses. It starts invalid -- nothing reaches the
// engine before a sweep has measured one -- and an invalid sweep keeps the
// last valid span (spec 2026-10-02-rev-a-p6a-panel-scan-design.md 3.5).
Span g_span{0, 0, false};
#if SHELL_COUPON_PROBE
// The coupon's ties, in kCouponChain's step order: coupon_span() reads that
// order, and this image scans in kCouponPlayChain's (spec section 3.2).
constexpr int kCouponSteps = scan_steps(kCouponChain);
uint16_t      g_step_raw[kCouponSteps];
#else
uint16_t g_cal_zero = 0;
uint16_t g_cal_rail = 0;
#endif
volatile uint32_t g_sweeps = 0;

#if SHELL_RAW_SPREAD
// The raw-count readout (fault R3, reva-bring-up.md 5.3/5.5/5.7): per mux
// channel the min and max of the raw word over a window of whole sweeps, so
// every channel is read the same number of times in it. 20 sweeps is one
// second at the measured 50 ms per sweep (5.9). The callback keeps the open
// window in g_raw_lo/hi and copies it to the published pair at the window's
// end, then counts g_raw_win up; the foreground reads the pair between two
// equal g_raw_win readings (a torn copy is retried, never printed).
constexpr uint32_t kRawWindowSweeps = 20;
uint16_t           g_raw_lo[kChannels];
uint16_t           g_raw_hi[kChannels];
volatile uint16_t  g_raw_pub_lo[kChannels];
volatile uint16_t  g_raw_pub_hi[kChannels];
volatile uint32_t  g_raw_win        = 0;
uint32_t           g_raw_win_sweeps = 0;
#endif

// The knob vector the shared control law reads (spec 2026-10-09-rev-a-p6b1
// section 4.2): every parameter in VCV's units, booted from the init patch,
// depths included. A pot writes it only while it owns its target (pot_owner.h);
// a key toggles its latch, SHIFT + MOD clears the depth twins
// (apply_key_edges()). The law itself lives in
// control_tick.cpp.
float             g_knobs[ffctl::NUM_PARAMS];
volatile uint32_t g_law_cycles_max = 0;
volatile uint32_t g_law_cycles_last = 0;
// The LED law plus the lamp word, counted on their own (spec 2026-10-18-rev-a-
// p6b2 section 6: the LED law's cost alone and together with the law).
volatile uint32_t g_led_cycles_max = 0;
volatile uint32_t g_led_cycles_last = 0;
// The whole audio callback, panel_scan_tick() through inst.process(): the scan
// and pot loop, the law, the lamps, and the engine with its per-sample
// observer followers and FLUX phase, none of which the two windows above see.
volatile uint32_t g_cb_cycles_max = 0;
volatile uint32_t g_cb_cycles_last = 0;
#if defined(SPKY_PROBE_LAW)
// The TIDE split of the block that set g_law_cycles_max (util/probe_law.h,
// fault R4): the engine sums into spky::probe_law during one control tick,
// and a new maximum copies it here. g_law_split_n counts the copies; the
// foreground reads between two equal readings.
spky::ProbeLaw    g_law_split = {};
volatile uint32_t g_law_split_n = 0;
#endif
// All three worst cases ignore the first second (500 blocks): a cold-cache
// first tick or an interrupt during boot would otherwise pin them for good.
constexpr uint32_t kLawWarmupBlocks = 500;
uint32_t           g_law_blocks = 0;
// The instrument the callback passes to panel_scan_tick(), for the callback
// profile (cb_profile.h) at the block end.
const spky::Instrument* g_inst = nullptr;

// The LED law's duties and the block counter the lamp word's duty pattern
// runs on. The block period comes from the board (AudioBlockSize /
// AudioSampleRate), read on the first tick, and so is the block's sample
// count the clock law advances by.
int      g_duty[ffctl::NUM_LIGHTS];
uint32_t g_led_frame     = 0;
float    g_block_dt      = 0.0f;
uint32_t g_block_samples = 0;

#if SHELL_PERSIST_ON
// Panel-state persistence (spec 2026-10-28-panel-state-persistence-design.md).
//
// The boot take (section 4.4 steps 3-5): until it is done, every reading with
// a valid span is collected instead of going through pot_row_step(); once
// every row has one, they are written ENGINE rows first (boot_take_apply()),
// every pot is released, and the law is re-armed as for a restore.
BootTake g_take = {};
// The fallback: the walk reaches every row once per sweep (host test "one
// sweep of kRevaChain reaches every kRevaTable row"), so a complete take is
// done at the end of the first sweep with a valid span. Should a row ever go
// unread, the take is applied anyway after kTakeSweeps sweeps that ended with
// a valid span, with the rows seen so far -- a panel whose pots never write
// again would be the alternative. g_take_partial is the seen-row count of
// such a take for the foreground's one line, 0 when the take was complete.
constexpr uint32_t kTakeSweeps     = 3;
uint32_t           g_take_sweeps   = 0;
volatile int       g_take_partial  = 0;

// After the take has written: every pot lets go -- from here on a pot writes
// only once moved past kPotTouch, as before -- and the law is re-armed as for
// a restore, so an ENGINE the take changed fires no bbd_edge (FLUX stays at
// its pot). The record's excite_other_deck survives only on a deck that is on
// BBD after the take (spec 4.3).
// Excite during the boot: the law's bbd_part follows the RUNNING engine, and
// a restored BBD deck runs another engine until the fade-and-swap lands
// (roughly the first 100 ms, from reading the code, not measured). Without a
// hold, apply_excite_events() clears the restored bit on the first tick;
// control_set_excite() therefore holds it across the switch
// (apply_excite_hold(), panel_step.h), both at boot and here, where the take
// may have finished before the engine landed.
// Which decks' ENGINE knob is on BBD, read before the take writes: a deck
// the take turns onto BBD gets excite as a live turn would (excite_after_take()).
void engines_on_bbd(const float* knobs, bool out[2])
{
    out[0] = static_cast<int>(std::round(knobs[ffctl::ENGINE_A])) == kEngineSlotBbd;
    out[1] = static_cast<int>(std::round(knobs[ffctl::ENGINE_B])) == kEngineSlotBbd;
}

void finish_take(const bool was_bbd[2])
{
    for(PotOwner& o : g_owner) pot_release(o);
    control_rearm_after_take();
    bool excite[2];
    excite_after_take(persist_boot_excite(), was_bbd, g_knobs, excite);
    control_set_excite(excite);
}
// The snapshot the foreground saves from (section 5.1), published once per
// sweep after the boot take, in the g_raw_win / g_law_split_n manner but as a
// sequence lock: g_pub_seq is odd while the callback writes, and the
// foreground copies between two equal, even readings. The callback cannot be
// interrupted by the foreground, so its own write never sees a torn copy; a
// foreground copy the callback cut into is retried. 0 = nothing published yet.
float             g_pub_knobs[ffctl::NUM_PARAMS];
bool              g_pub_excite[2] = {false, false};
volatile uint32_t g_pub_seq       = 0;
#endif

#ifndef SHELL_CB_WINDOW
#define SHELL_CB_WINDOW 0
#endif
// panel_scan_block_end()'s per-save window (stress image) or per-frame window
// (SHELL_CB_WINDOW, C_USER_FLAGS, and every SHELL_REMOTE image: the remote
// runner's frame_max is this window's worst block).
#define SHELL_WIN_ON (SHELL_PERSIST_STRESS_ON || SHELL_CB_WINDOW || SHELL_REMOTE)
#if SHELL_WIN_ON
volatile uint32_t g_win_gap_max    = 0;
volatile uint32_t g_win_cb_max     = 0;
volatile uint32_t g_win_over       = 0;
volatile uint32_t g_win_blocks     = 0;
volatile bool     g_win_reset      = false;
uint32_t          g_win_prev_entry = 0;
// A block above this counts in g_win_over: the low end of the measured
// deadline band (docs/gotchas.md, ~930-960 k).
constexpr uint32_t kWinOverCycles  = 930000;
#endif

#if SHELL_REMOTE
// The remote CPU hunt's workloads (spec 2026-10-29-cpu-hunt-remote-design.md
// 5.2/5.3, plan Task 5). Everything here runs in the audio callback, after
// the pot loop and before the control tick, so a LOAD reaches the law in the
// block it arrives; the foreground only prints. The pure half -- the guard,
// the run's settle and void, the load's vector, a gesture step -- is
// workload_run.h, host tested.
static_assert(kWorkloadRowCount == kTable.count,
              "generated_workloads.h: one value per Rev A row (run gen_workloads.py)");

// The self-limiter (spec 5.3): tripped in panel_scan_block_end() by
// kGuardBlocks consecutive callbacks over kGuardLimitCycles, or by as many
// consecutive entry-to-entry periods over kGuardPeriodCycles (back to back
// with cb under the limit; workload_run.h). main.cpp's callback then writes
// silence instead of calling process(), and the measurement image's LATE
// burn stops (panel_scan_late()), so the foreground and USB get the CPU back.
// It re-arms itself after kGuardRearmBlocks silent blocks, and a run's settle
// end clears it (Bastian, 2026-10-29: a patch that overruns for real must be
// counted, trips and all); LOAD, HOME and RST clear it at once.
// g_guard_blk: the block it last tripped in; g_guard_by: which criterion
// (GuardCause); g_guard_flag mirrors tripped() for the foreground.
// The foreground prints from counters, so a trip or a re-arm between two passes
// is never lost: g_guard_trips / g_guard_rearms count them (never reset),
// g_guard_rearm_blk / g_guard_rearm_settle say the last re-arm's block and
// whether the settle end did it (else the silence), and g_guard_silent counts
// the blocks the guard was tripped in (the trip's own block and every silenced
// one), from which each frame's tripped= is the difference between two reads.
OverrunGuard      g_guard(kGuardLimitCycles, kGuardBlocks, kGuardPeriodCycles);
volatile uint32_t g_guard_blk  = 0;
volatile GuardCause g_guard_by = GuardCause::None;
volatile bool     g_guard_flag = false;
volatile uint32_t g_guard_trips        = 0;
volatile uint32_t g_guard_rearms       = 0;
volatile uint32_t g_guard_rearm_blk    = 0;
volatile bool     g_guard_rearm_settle = false;
volatile uint32_t g_guard_silent       = 0;

// LOAD, HOME and RST: the flag too, before their ack is queued -- otherwise
// the foreground can pop the ack ahead of this block's panel_scan_block_end()
// and print a stale `guard tripped` behind the new run's load line, which the
// host reads as that run tripping.
void clear_guard()
{
    g_guard.clear();
    g_guard_flag = false;
}

// The block clock the runs count on: every block that reaches the law.
uint32_t    g_rblock = 0;

// The guard re-armed by itself, in block g_rblock: by the settle end or by its
// own silence. The block and the cause go in ahead of the count.
void note_guard_rearm(bool by_settle)
{
    g_guard_rearm_blk    = g_rblock;
    g_guard_rearm_settle = by_settle;
    g_guard_rearms       = g_guard_rearms + 1u;
}

WorkloadRun g_run;
int         g_run_gest = -1;       // the gesture id of this run, -1 for a LOAD
bool        g_run_rec[2] = {false, false};   // REC latched by the material
// The running gesture: its steps copied out of QSPI at GEST, so the measured
// blocks never read QSPI; the next step to apply; the gesture's length.
GestureStep g_gest_steps[kGestureMaxSteps];
int         g_gest_n      = 0;
int         g_gest_next   = 0;
uint32_t    g_gest_blocks = 0;
bool        g_gest_done   = false;
// The LOAD's knob vector, built here and not on the interrupt's stack (1 KB).
float g_load_knobs[ffctl::NUM_PARAMS];
// HOME: the panel as it played when the first LOAD / GEST arrived -- the
// booted patch plus whatever the hands had done -- and its excite bits.
float g_home_knobs[ffctl::NUM_PARAMS];
bool  g_home_excite[2] = {false, false};
bool  g_home_valid     = false;
// The run as the foreground reads it (publish_run(), read_run()), refreshed
// every block and when the guard trips. A sequence lock like g_pub_seq's:
// g_rs_seq is odd while the callback writes, and the foreground copies the
// set between two equal, even readings. The callback cannot be interrupted
// by the foreground, so a copy it cut into is simply retried.
volatile uint32_t g_rs_seq      = 0;
volatile int      g_rs_id       = -1;
volatile int      g_rs_gest     = -1;
volatile uint32_t g_rs_gen      = 0;
volatile bool     g_rs_counting = false;
volatile bool     g_rs_void     = false;
volatile uint32_t g_rs_trips    = 0;
// The toggle word process() latches (util/probe_toggles.h; 0 outside the
// measurement image), for the run line: the host voids a run whose counted
// frames had bit 30 or 31 set (tools/run_stats.py).
volatile uint32_t g_rs_tog      = 0;

void publish_run()
{
    g_rs_seq = g_rs_seq + 1u;   // odd: writing
    std::atomic_signal_fence(std::memory_order_seq_cst);
    g_rs_id       = g_run.id();
    g_rs_gest     = g_run.active() ? g_run_gest : -1;
    g_rs_gen      = g_run.gen();
    g_rs_counting = g_run.counting(g_rblock);
    g_rs_void     = g_run.void_run();
    g_rs_trips    = g_run.trips();
    g_rs_tog      = spky::probe::toggles();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    g_rs_seq = g_rs_seq + 1u;   // even: done
}

struct RunSnap
{
    uint32_t gen, tog, trips;
    int      id, gest;
    bool     counting, void_run;
};

// Foreground only.
RunSnap read_run()
{
    RunSnap  s;
    uint32_t q;
    do
    {
        q = g_rs_seq;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        s.gen      = g_rs_gen;
        s.tog      = g_rs_tog;
        s.id       = g_rs_id;
        s.gest     = g_rs_gest;
        s.counting = g_rs_counting;
        s.void_run = g_rs_void;
        s.trips    = g_rs_trips;
        std::atomic_signal_fence(std::memory_order_seq_cst);
    } while((q & 1u) != 0 || q != g_rs_seq);
    return s;
}

// What the callback did, for the foreground to print (the callback never
// prints): CmdRing as a callback -> foreground queue, the command kind and its
// id, `b` one of these. A full queue drops the line, never the command.
// A started LOAD / GEST carries its run's gen instead: b = kAckStarted + gen,
// so the host can tell this run's frames from an earlier run of the same id.
// Printed gens are the low 16 bits (kGenPrintMask), on the load line and on
// the run line alike, which keeps the load line inside the log buffer; 65 536
// loads in one capture would be needed for two runs to share one.
// The measurement image's TOG ack carries the new toggle word in `a` and
// kAckTogBase + (bit << 1 | value) in `b` (kAckTogBase + 0..63, above every
// started code, kAckStarted + a 16-bit gen). Its settle, kTogSettleBlocks
// later, carries the word and kAckTogSettledBase + the run's printed gen
// then (a TOG re-settles a running workload under a new gen, which the host
// follows from that line): 2^21 + 0..65535, above both.
enum AckCode : int32_t { kAckOk = 0, kAckBadId = 1, kAckUnsupported = 2, kAckGestDone = 3,
                         kAckTake = 4, kAckStarted = 16,
                         kAckTogBase = 1 << 20, kAckTogSettledBase = 1 << 21 };
constexpr int32_t kAckTogCodes = 64;   // kAckTogBase + 0..63
constexpr uint32_t kGenPrintMask = 0xFFFFu;
CmdRing g_acks;

void ack(CmdKind k, int32_t a, int32_t b)
{
    const Command c{k, a, b};
    (void)g_acks.push(c);
}

void ack_started(CmdKind k, int32_t a)
{
    ack(k, a, kAckStarted + static_cast<int32_t>(g_run.gen() & kGenPrintMask));
}

#if SHELL_REMOTE_MEASURE
// The measurement image (SHELL_REMOTE_MEASURE=1; spec 5.3-5.5, plan Task 6).
//
// The callback's sections, per block, in DWT cycles: the scan with the keys
// and the pot loop (scan), the control law (law, = SHELL_PLAY_LAW's cyc),
// the LED law and the lamp word (led, = its led), the boot gate after
// process() (gate), the boot take and the per-sweep save snapshot (persist),
// and cb_profile's split of process() itself: deck A, deck B, the mix
// (a / b / mix, cb_profile_split()). What the eight leave out of the whole
// callback is the command handling and a LOAD (remote_block()), the clock
// law, a sweep's span, a LATE burn and the bookkeeping: worst minus the sum.
enum { SEC_SCAN = 0, SEC_LAW, SEC_LED, SEC_GATE, SEC_PERSIST, SEC_A, SEC_B, SEC_MIX, SEC_COUNT };
uint32_t g_sec_cur[SEC_COUNT];
// Over the frame's window (reset with SHELL_PLAY_WIN's, g_win_reset): the
// worst block's cycles and its own sections -- the breakdown that adds up --
// and each section's maximum, which come from different blocks. A sequence
// lock as g_rs_seq's: odd while the callback writes.
uint32_t          g_sec_worst_cb = 0;
uint32_t          g_sec_worst[SEC_COUNT];
uint32_t          g_sec_max[SEC_COUNT];
volatile uint32_t g_sec_seq = 0;
// Callbacks ended (panel_scan_block_end()), for the outside-window loop, and
// the last one's tail: its cycles after the measured window (dc), which
// SHELL_PLAY_LAW's cb leaves out and SHELL_OUT's out takes in.
volatile uint32_t g_blk_done  = 0;
volatile uint32_t g_tail_last = 0;
// LATE k: k * 1000 cycles burnt in panel_scan_late(), after the tick and
// before process(), so they land inside the measured callback. g_late_last
// is what bit 30 repeats on every block; g_late_blk the block the last LATE
// burnt in, for its ack line.
uint32_t          g_late_next = 0;
uint32_t          g_late_last = 0;
volatile uint32_t g_late_blk  = 0;
// TOG: the maxima reset kTogSettleBlocks after the TOG's block -- the TOG's
// block and the 199 after it are the switch-over -- so a stateful candidate's
// switch-over is not its measurement (spec 5.3). A running workload settles
// again for as long (WorkloadRun::resettle()), under a new gen.
constexpr uint32_t kTogSettleBlocks = 200;
TogSettle          g_tog_settle(kTogSettleBlocks);

// Bit 31 of the toggle word (util/probe_toggles.h): the overrun guard is not
// consulted. Read per block, outside every loop.
bool guard_off()
{
    return spky::probe::on(spky::probe::toggles(), spky::probe::kBitShellGuardOff);
}

void sec_reset()
{
    g_sec_seq = g_sec_seq + 1u;   // odd: writing
    std::atomic_signal_fence(std::memory_order_seq_cst);
    g_sec_worst_cb = 0;
    for(int s = 0; s < SEC_COUNT; ++s) g_sec_worst[s] = g_sec_max[s] = 0;
    std::atomic_signal_fence(std::memory_order_seq_cst);
    g_sec_seq = g_sec_seq + 1u;   // even: done
}

// The block end: process()'s split (zeros when the guard silenced it), then
// the frame's worst block and maxima.
void sec_block(uint32_t cb, bool processed)
{
    uint32_t a = 0, b = 0, mix = 0;
    if(processed) cb_profile_split(&a, &b, &mix);
    g_sec_cur[SEC_A]   = a;
    g_sec_cur[SEC_B]   = b;
    g_sec_cur[SEC_MIX] = mix;
    g_sec_seq = g_sec_seq + 1u;
    std::atomic_signal_fence(std::memory_order_seq_cst);
    if(cb > g_sec_worst_cb)
    {
        g_sec_worst_cb = cb;
        for(int s = 0; s < SEC_COUNT; ++s) g_sec_worst[s] = g_sec_cur[s];
    }
    for(int s = 0; s < SEC_COUNT; ++s)
        if(g_sec_cur[s] > g_sec_max[s]) g_sec_max[s] = g_sec_cur[s];
    std::atomic_signal_fence(std::memory_order_seq_cst);
    g_sec_seq = g_sec_seq + 1u;
    // The shell's sections are this block's only: a tick that returned
    // early (the scan's first block) leaves zeros, not the last block's.
    for(int s = 0; s < SEC_COUNT; ++s) g_sec_cur[s] = 0;
}

struct SecSnap
{
    uint32_t worst_cb;
    uint32_t worst[SEC_COUNT], max[SEC_COUNT];
};

// Foreground only.
SecSnap read_sec()
{
    SecSnap  s;
    uint32_t q;
    do
    {
        q = g_sec_seq;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        s.worst_cb = g_sec_worst_cb;
        for(int k = 0; k < SEC_COUNT; ++k)
        {
            s.worst[k] = g_sec_worst[k];
            s.max[k]   = g_sec_max[k];
        }
        std::atomic_signal_fence(std::memory_order_seq_cst);
    } while((q & 1u) != 0 || q != g_sec_seq);
    return s;
}

// The time outside the callback (spec 5.4): the report loop's 500 ms, spent
// reading DWT instead of in hw.Delay(500). Every jump of more than
// kOutJumpCycles between two reads is an interrupt; a block's busy time is
// the sum of the jumps from the one that held its callback up to the one
// that holds the next. busy minus that block's cb is `out`: everything that
// ran outside the measured window (dc) in it -- including this image's own
// probe tail, the block end after dc's stamp (guard, WIN window, sections,
// cb_profile's per-block accumulation; g_tail_last). out minus the tail is
// `ext` (out_split(), workload_run.h): what ran outside the callback without
// the probe -- USB, SysTick, the audio interrupt's entry and exit, and the
// callback's own code ahead of cb0 in main.cpp. A jump that held more than
// one callback (back to back: the foreground starved) cannot be split, so it
// and the block after it are not counted. `neg` counts the blocks whose
// split saturated (busy under cb, or out under the tail: out_split()), whose
// out / ext then read 0 instead of a figure that adds up.
constexpr uint32_t kOutWindowCycles = 500u * 1000u * kCoreMhz;   // 500 ms, as Delay(500)
static_assert(kOutWindowCycles == 240000000u, "500 ms at 480 MHz");
constexpr uint32_t kOutJumpCycles   = 64;
constexpr int      kOutMaxBlocks    = 512;   // 250 blocks in 500 ms, with room

struct OutStats
{
    uint32_t busy_max, busy_p50, out_max, tail_max, tail_p50, ext_max;
    int      n, neg;
};

// The foreground's alone: per counted block, its busy cycles and its tail.
uint32_t g_out_busy[kOutMaxBlocks];
uint32_t g_out_tail[kOutMaxBlocks];

OutStats busy_window()
{
    OutStats       o       = {0, 0, 0, 0, 0, 0, 0, 0};
    const uint32_t t_start = cycles_now();
    uint32_t       prev    = t_start;
    uint32_t       blk     = g_blk_done;
    uint32_t       acc     = 0;       // the open block's busy cycles so far
    uint32_t       cb      = 0;       // the open block's own callback
    uint32_t       tail    = 0;       // and its tail
    bool           open    = false;   // a countable block is open
    for(;;)
    {
        const uint32_t now = cycles_now();
        const uint32_t d   = now - prev;
        prev               = now;
        if(now - t_start >= kOutWindowCycles) break;
        const uint32_t b = g_blk_done;
        if(b != blk)
        {
            // The callback ran inside this jump: the open block ends here.
            if(open && o.n < kOutMaxBlocks)
            {
                g_out_busy[o.n] = acc;
                g_out_tail[o.n] = tail;
                ++o.n;
                const OutSplit s = out_split(acc, cb, tail);
                if(s.neg) ++o.neg;
                if(acc > o.busy_max) o.busy_max = acc;
                if(s.out > o.out_max) o.out_max = s.out;
                if(tail > o.tail_max) o.tail_max = tail;
                if(s.ext > o.ext_max) o.ext_max = s.ext;
            }
            open = b - blk == 1u;
            blk  = b;
            acc  = d;
            cb   = g_cb_cycles_last;
            tail = g_tail_last;
        }
        else if(d > kOutJumpCycles)
            acc += d;
    }
    if(o.n > 0)
    {
        std::nth_element(g_out_busy, g_out_busy + o.n / 2, g_out_busy + o.n);
        o.busy_p50 = g_out_busy[o.n / 2];
        std::nth_element(g_out_tail, g_out_tail + o.n / 2, g_out_tail + o.n);
        o.tail_p50 = g_out_tail[o.n / 2];
    }
    return o;
}
#endif

// The maxima the frame lines report, so each run's numbers are its own:
// SHELL_PLAY_LAW's cyc/led/cb maxima and the WIN window (reset by
// panel_scan_block_end() on its next call). The warm-up rule still holds:
// nothing is recorded in the first kLawWarmupBlocks after boot.
void reset_maxima()
{
    g_law_cycles_max = 0;
    g_led_cycles_max = 0;
    g_cb_cycles_max  = 0;
    g_win_reset      = true;
}

// A workload as the sweep loaded it (host/sweep/main.cpp render_once()): the
// init patch with the rows over it and REC latched for its SAMPLER decks
// (workload_knobs()), the law re-armed, every pot let go, the excite bits set
// and held across the engine switch (control_set_excite(), apply_excite_hold()).
// The run starts settling; the maxima reset now and again when it counts.
void start_workload(const WorkloadDef& w, int id, int gest)
{
    if(!g_home_valid)
    {
        for(int i = 0; i < ffctl::NUM_PARAMS; ++i) g_home_knobs[i] = g_knobs[i];
        control_get_excite(g_home_excite);
        g_home_valid = true;
    }
    (void)workload_knobs(w, kTable, g_load_knobs);   // row count: static_assert above
    panel_scan_load_knobs(g_load_knobs);
    const bool excite[2] = {w.excite[0] != 0, w.excite[1] != 0};
    control_set_excite(excite);
    g_run_rec[0] = w.rec[0] != 0;
    g_run_rec[1] = w.rec[1] != 0;
    g_run.start(id, g_rblock, w.settle_blocks);
    g_run_gest = gest;
    clear_guard();
    reset_maxima();
}

void remote_command(const Command& c)
{
    switch(c.kind)
    {
        case CmdKind::Load:
        {
            // The parser bounds nothing above 0; the table does.
            const WorkloadDef* w = workload_by_id(c.a);
            if(w == nullptr) { ack(c.kind, c.a, kAckBadId); break; }
            g_gest_n = 0;
            start_workload(*w, c.a, -1);
            ack_started(c.kind, c.a);
            break;
        }
        case CmdKind::Gesture:
        {
            // A gesture plays on its base workload (r4_noon, with its
            // material), loaded here; step 0 lands in the first counted block.
            int                n  = 0;
            const GestureStep* st = gesture_by_id(c.a, &n);
            const GestureDef*  d  = gesture_def(c.a);
            if(st == nullptr || d == nullptr || n > kGestureMaxSteps
               || workload_by_id(d->base) == nullptr)
            {
                ack(c.kind, c.a, kAckBadId);
                break;
            }
            std::memcpy(g_gest_steps, st, sizeof(GestureStep) * static_cast<size_t>(n));
            g_gest_n      = n;
            g_gest_next   = 0;
            g_gest_blocks = d->blocks;
            g_gest_done   = false;
            start_workload(*workload_by_id(d->base), d->base, c.a);
            ack_started(c.kind, c.a);
            break;
        }
        case CmdKind::Home:
            // Back to the panel as it was before the first LOAD; no run.
            if(g_home_valid)
            {
                panel_scan_load_knobs(g_home_knobs);
                control_set_excite(g_home_excite);
            }
            g_run.stop();
            g_run_gest = -1;
            g_gest_n   = 0;
            clear_guard();
            reset_maxima();
            ack(c.kind, g_home_valid ? 1 : 0, kAckOk);
            break;
        case CmdKind::ResetMax:
            clear_guard();
            reset_maxima();
            ack(c.kind, 0, kAckOk);
            break;
#if SHELL_REMOTE_MEASURE
        case CmdKind::Toggle:
        {
            // The parser bounds the bit to 0..31 and the value to 0 / 1.
            const uint32_t bit  = 1u << static_cast<uint32_t>(c.a);
            const uint32_t old  = spky::probe::toggles();
            const uint32_t word = c.b != 0 ? (old | bit) : (old & ~bit);
            spky::probe::set_toggles(word);   // process() latches it this block
            // Bit 31 flipped: the guard's streak starts again, so it neither
            // resumes stale (bit cleared) nor carries blocks it never saw.
            if(((old ^ word) >> spky::probe::kBitShellGuardOff) & 1u) g_guard.reset_streak();
            g_tog_settle.arm(g_rblock);
            g_run.resettle(g_rblock, kTogSettleBlocks);   // no run: nothing
            ack(c.kind, static_cast<int32_t>(word), kAckTogBase + (c.a << 1 | (c.b != 0 ? 1 : 0)));
            break;
        }
        case CmdKind::Late:
            // Burnt in this callback, by panel_scan_late() ahead of process();
            // the parser bounds k to 1..2000.
            g_late_next = static_cast<uint32_t>(c.a) * 1000u;
            g_late_last = g_late_next;
            g_late_blk  = g_rblock;
            ack(c.kind, c.a, kAckOk);
            break;
#else
        case CmdKind::Toggle:
        case CmdKind::Late:
            // The measurement image's (SHELL_REMOTE_MEASURE); said, not
            // silently eaten.
            ack(c.kind, c.a, kAckUnsupported);
            break;
#endif
        default:
            break;   // PING and BOOT never get here (remote_listen.cpp)
    }
}

// Once per block, before the control tick: at most one command, the settle's
// end, the gesture's steps due by this block, and the run for the foreground.
void remote_block()
{
    const uint32_t now = ++g_rblock;
    Command        c;
#if SHELL_PERSIST_ON
    // Not before the boot take: it would write the pot positions over a load.
    if(g_take.done && remote_poll(&c)) remote_command(c);
#else
    if(remote_poll(&c)) remote_command(c);
#endif
#if SHELL_REMOTE_MEASURE
    // A TOG's switch-over has settled: the maxima start again from here, and
    // the line names the gen the run (if any) now counts under.
    if(g_tog_settle.ends(now))
    {
        reset_maxima();
        ack(CmdKind::Toggle, static_cast<int32_t>(spky::probe::toggles()),
            kAckTogSettledBase + static_cast<int32_t>(g_run.gen() & kGenPrintMask));
    }
#endif
    if(g_run.settle_ends(now))
    {
        // The sweep releases REC at the settle's end, so the counted blocks
        // play back what was recorded; and the maxima start from here.
        if(g_run_rec[0]) g_knobs[ffctl::REC_A] = 0.0f;
        if(g_run_rec[1]) g_knobs[ffctl::REC_B] = 0.0f;
        reset_maxima();
        // A guard tripped by the load's own transient (or a TOG's) does not
        // silence the counted blocks: it is cleared exactly as counting starts.
        if(rearm_at_settle_end(g_guard, g_run, now))
        {
            g_guard_flag = false;
            note_guard_rearm(true);
        }
    }
    if(g_gest_n > 0 && g_run.counting(now))
    {
        const uint32_t off = g_run.counted(now);
        while(g_gest_next < g_gest_n && g_gest_steps[g_gest_next].block <= off)
        {
            // Through the pot / key path; SHIFT + MOD lets every pot go and
            // starts the MOD lamp's receipt blink, as the live keys do.
            if(apply_gesture_step(g_gest_steps[g_gest_next], kTable, kKeyMap, g_knobs))
            {
                for(PotOwner& o : g_owner) pot_release(o);
                g_mod_cleared = true;
            }
            ++g_gest_next;
        }
        if(!g_gest_done && g_gest_next >= g_gest_n && off + 1u >= g_gest_blocks)
        {
            g_gest_done = true;
            ack(CmdKind::Gesture, g_run_gest, kAckGestDone);
        }
    }
    publish_run();
}
#endif

} // namespace

void panel_scan_init(bench::Board& hw)
{
    g_scan.init();
    g_scan.set_walk_leds(false);
    g_scan.set_read_keys(true, keys_released(kKeys));
    for(int c = 0; c < kChannels; ++c) g_value[c] = -1.0f;
#if SHELL_RAW_SPREAD
    for(int c = 0; c < kChannels; ++c)
    {
        g_raw_lo[c] = 0xFFFF;
        g_raw_hi[c] = 0;
    }
#endif
    // The FLUX rows' channels, found the way panel_scan_tick() walks a step
    // (group_at / channel_at per sense pin), so the index is the one its
    // g_owner[] entry has (flux_channels(), host tested against kRevaTable).
    flux_channels(kTable, kActiveChain, g_flux_idx);
#if !SHELL_COUPON_PROBE
    // The TEMPO row's channel, the same way (tempo_channel(), host tested).
    g_tempo_idx = tempo_channel(kTable, kActiveChain);
    // Boot is the clock law's discontinuity: no period history, the knob owns
    // the tempo (spec section 6). Then the EXTI edge detector on Gate In 1/2;
    // main() clears its pending bits right before StartAudio().
    g_clock.init(hw.AudioSampleRate());
    gate_latch_init();
#else
    (void)hw;
#endif
    // The firmware's boot is a patch restore of the init patch (spec 4.1), or
    // with SHELL_PERSIST_ON of the init patch plus the newest saved record
    // (spec 2026-10-28 section 4.4).
    control_boot(g_knobs);
    // The DWT counter the law's cycle report reads; libDaisy never starts it,
    // and without this it reads zero forever (cycles.h).
    cycles_init();
#if defined(SPKY_PROBE_LAW)
    spky::probe_law_clock = &DWT->CYCCNT;
#endif
    cb_profile_init();
}

void panel_scan_tick(bench::Board& hw, spky::Instrument& inst)
{
    g_inst = &inst;
#if SHELL_REMOTE_MEASURE
    const uint32_t sec0 = cycles_now();   // the scan section starts here
#endif
    const int step = g_scan.step(hw);
    // The return stream of the latch step() just clocked: the 165 loads on
    // the same edge, so the keys are read once per block.
    key_update(g_keys, kKeys, g_scan.last_return());
    // A key acts on its debounced press, never on release or hold (spec
    // 2026-10-18-rev-a-p6b2 section 3). MOD needs no release call here: every
    // pot whose target changes with the latch is released by pot_own() on its
    // next reading, because pot_target() hands it a different id. SHIFT + MOD
    // does: an owned depth pot would write its position straight back over the
    // cleared value, so every pot lets go until it is moved again.
    const uint8_t edges = static_cast<uint8_t>(g_keys.pressed & ~g_last_pressed);
    g_last_pressed      = g_keys.pressed;
    if(apply_key_edges(edges, g_keys.pressed, kKeyMap, g_knobs).mod_cleared)
    {
        for(PotOwner& o : g_owner) pot_release(o);
        g_mod_cleared = true;   // held for led_tick(): a step < 0 returns first
    }
    if(step < 0) return;

    for(int s = 0; s < kActiveChain.sense_pins; ++s)
    {
        // A pin with no live channel this step floats; group_at() says so.
        const int g = group_at(kActiveChain, step, s);
        if(g < 0) continue;
        const int      ch  = channel_at(kActiveChain, step, s);
        const int      idx = mux_channel(kActiveChain, step, s);
        const uint16_t raw = g_mux_raw[idx];
#if SHELL_RAW_SPREAD
        if(raw < g_raw_lo[idx]) g_raw_lo[idx] = raw;
        if(raw > g_raw_hi[idx]) g_raw_hi[idx] = raw;
#endif
#if SHELL_COUPON_PROBE
        g_step_raw[step_of(kCouponChain, g, ch)] = raw;
#else
        if(g == kRevaCalZero.group && ch == kRevaCalZero.ch) g_cal_zero = raw;
        if(g == kRevaCalRail.group && ch == kRevaCalRail.ch) g_cal_rail = raw;
#endif
        const ControlEntry* e = find_control(kTable, g, ch);
        if(e == nullptr) continue;
#if SHELL_PERSIST_ON
        // The boot take collects (spec 2026-10-28 section 4.4 step 3): the
        // first reading per row with a valid span is kept, not filtered and
        // not owned; it is written below once every row has one. Before the
        // span is valid the row goes through pot_row_step() as always, which
        // writes nothing then.
        if(!g_take.done && g_span.valid)
        {
            boot_take_collect(g_take, static_cast<int>(e - kTable.entries), raw);
            continue;
        }
#endif
        // Target (MOD-aware, ATTACK -> STAGES on the BBD), ownership (spec
        // 2026-10-18-rev-a-p6b2 section 2), the jump to the pot's current
        // position on the move that takes it, the filter, and the write by
        // an owner only: pot_row_step(), host tested.
        float v;
#if SHELL_REMOTE
        // A pot that takes its parameter over during a run voids it (spec
        // 5.2, "Pot drift"), and every take is logged with its row.
        const bool owned_before = g_owner[idx].owned;
#endif
        if(pot_row_step(*e, g_owner[idx], g_filter[idx], raw, g_span, g_knobs, &v))
            g_value[idx] = v;               // every row is reported
#if SHELL_REMOTE
        if(!owned_before && g_owner[idx].owned)
        {
            g_run.on_pot_take();
            ack(CmdKind::None, static_cast<int32_t>(e - kTable.entries), kAckTake);
        }
#endif
    }
#if SHELL_REMOTE_MEASURE
    const uint32_t sec1 = cycles_now();
    g_sec_cur[SEC_SCAN] = sec1 - sec0;
#endif

#if SHELL_PERSIST_ON
    // The boot take writes (spec 2026-10-28 section 4.4 steps 3-5), once,
    // before this block's control tick sees the vector: ENGINE rows first, so
    // an ATTACK row lands on ATTACK or STAGES by the engine the panel shows;
    // then finish_take(). A take that cannot apply (no valid span) stays open.
    if(!g_take.done && boot_take_ready(g_take, kTable))
    {
        bool was_bbd[2];
        engines_on_bbd(g_knobs, was_bbd);
        boot_take_apply(g_take, kTable, g_span, g_knobs);
        if(g_take.done) finish_take(was_bbd);
    }
#endif
#if SHELL_REMOTE_MEASURE
    g_sec_cur[SEC_PERSIST] = cycles_now() - sec1;   // the sweep's snapshot adds below
#endif

#if SHELL_REMOTE
    // The remote commands and the running workload, once per block, after
    // the pots and before the law sees the vector (see remote_block()).
    remote_block();
#endif

    // The block period and size come from the board (AudioBlockSize /
    // AudioSampleRate), read on the first tick.
    if(g_block_dt <= 0.0f)
    {
        g_block_samples = static_cast<uint32_t>(hw.AudioBlockSize());
        g_block_dt      = static_cast<float>(g_block_samples) / hw.AudioSampleRate();
    }

#if SHELL_COUPON_PROBE
    // The coupon reads no gate pin (spec 2026-10-19-clock-and-reset-jacks
    // section 6): the knob sets the tempo and the jack lamps stay dark.
    const float measured_bpm = 0.0f;
    const bool  clock_lamp   = false;
    const bool  reset_lamp   = false;
#else
    // CLOCK and RESET (spec 2026-10-19-clock-and-reset-jacks section 6): this
    // block's latched edges into the shared clock law, stamped at the block's
    // end; its pulse and restart go to the engine before the control law
    // runs, as VCV does per sample. Pulse first: a reset in the same block
    // counts as arriving before its pulse, so that pulse is the 1. The pulse
    // that starts a clock whose first edge came with a RESET is counted, not
    // rounded (section 10).
    const GateEdges         gates = gate_latch_take();
    const control::ClockOut clk   = g_clock.step(g_block_samples, gates.clock, gates.reset,
                                                 g_knobs[ffctl::TEMPO], inst.beat_phase());
    if(clk.pulse) clk.counted ? inst.clock_pulse_counted() : inst.clock_pulse();
    if(clk.restart) (void)inst.restart(clk.restart_age);
    const float measured_bpm = clk.measured_bpm;
    const bool  clock_lamp   = clk.clock_lamp;
    const bool  reset_lamp   = clk.reset_lamp;
#endif

    // Once per block, the whole vector (spec 2026-10-09 section 4.2).
    const bool     warm = g_law_blocks >= kLawWarmupBlocks;
    if(!warm) ++g_law_blocks;
#if defined(SPKY_PROBE_LAW)
    spky::probe_law = {};
#endif
    const uint32_t c0 = cycles_now();
    const control::Events ev = control_tick(g_knobs, inst, measured_bpm);
    const uint32_t dc = cycles_now() - c0;
    g_law_cycles_last = dc;
#if SHELL_REMOTE_MEASURE
    g_sec_cur[SEC_LAW] = dc;
#endif
    if(warm && dc > g_law_cycles_max)
    {
        g_law_cycles_max = dc;
#if defined(SPKY_PROBE_LAW)
        g_law_split   = spky::probe_law;
        g_law_split_n = g_law_split_n + 1u;
#endif
    }

    // The law's events (spec 2026-10-18-rev-a-p6b2 section 5). On a genuine
    // entry into the BBD, FLUX drops to 0 as VCV's knob does, and the FLUX pot
    // is released: FLUX stays at 0 until that pot is moved. control_tick()
    // has already turned the deck's excite_other_deck on (on BBD) or off
    // (leaving BBD). rec_started is
    // ignored: the hardware has no sample path to clear (spec section 3).
    apply_law_events(ev, g_knobs, g_owner, g_flux_idx);
#if !SHELL_COUPON_PROBE
    // On clock loss the TEMPO pot is released, so its idle noise cannot count
    // as a turn: the knob takes the tempo back only once the pot moves past
    // kPotTouch (spec 2026-10-19-clock-and-reset-jacks section 6).
    apply_clock_events(clk, g_owner, g_tempo_idx);
#endif

    // The lamps (spec section 4): the LED law, then its duties as this
    // block's lamp word. set_leds() only stores the word; it reaches the 595s
    // with the NEXT step's latch, never on its own (P2 section 4), and this
    // is the one place it changes, once per block.
    const uint32_t c1 = cycles_now();
    led_tick(inst, g_knobs[ffctl::MODBTN] > 0.5f, g_block_dt, g_duty, clock_lamp, reset_lamp,
             g_mod_cleared);
    g_mod_cleared = false;
    g_scan.set_leds(led_word(g_duty, spkyled::kLedSteps, g_led_frame, kLedBit));
    const uint32_t dl = cycles_now() - c1;
    g_led_frame       = g_led_frame + 1u;
    g_led_cycles_last = dl;
    if(warm && dl > g_led_cycles_max) g_led_cycles_max = dl;
#if SHELL_REMOTE_MEASURE
    g_sec_cur[SEC_LED] = dl;
#endif

    if(step == kSteps - 1)
    {
#if SHELL_COUPON_PROBE
        const Span sp = coupon_span(g_step_raw, kCouponSteps);
#else
        const Span sp = panel_span(g_cal_zero, g_cal_rail);
#endif
        if(sp.valid) g_span = sp;
        g_sweeps = g_sweeps + 1u;
#if SHELL_PERSIST_ON
#if SHELL_REMOTE_MEASURE
        const uint32_t sec2 = cycles_now();
#endif
        // The partial-take fallback (see kTakeSweeps): this sweep ended with
        // a valid span and the take is still open. The first such sweep is
        // the one whose end made the span valid and collected nothing, so
        // the fallback fires after kTakeSweeps sweeps that did collect.
        if(!g_take.done && g_span.valid && ++g_take_sweeps > kTakeSweeps)
        {
            int seen = 0;
            for(int r = 0; r < kTable.count && r < kMaxRows; ++r) seen += g_take.seen[r] ? 1 : 0;
            bool was_bbd[2];
            engines_on_bbd(g_knobs, was_bbd);
            boot_take_apply(g_take, kTable, g_span, g_knobs);   // writes the seen rows only
            if(g_take.done)
            {
                finish_take(was_bbd);
                g_take_partial = seen > 0 ? seen : -1;   // -1: none seen, still reported
            }
        }
        // The save snapshot (spec 2026-10-28 section 5.1), once per sweep and
        // only after the boot take: the foreground seeds its comparison with
        // the first one, so the take itself never counts as a change. The
        // fences keep the compiler from moving the copy across the counter.
        if(g_take.done)
        {
            g_pub_seq = g_pub_seq + 1u;   // odd: writing
            std::atomic_signal_fence(std::memory_order_seq_cst);
            for(int i = 0; i < ffctl::NUM_PARAMS; ++i) g_pub_knobs[i] = g_knobs[i];
            control_get_excite(g_pub_excite);
            std::atomic_signal_fence(std::memory_order_seq_cst);
            g_pub_seq = g_pub_seq + 1u;   // even: done
        }
#if SHELL_REMOTE_MEASURE
        g_sec_cur[SEC_PERSIST] += cycles_now() - sec2;
#endif
#endif
#if SHELL_RAW_SPREAD
        if(++g_raw_win_sweeps >= kRawWindowSweeps)
        {
            for(int c = 0; c < kChannels; ++c)
            {
                g_raw_pub_lo[c] = g_raw_lo[c];
                g_raw_pub_hi[c] = g_raw_hi[c];
                g_raw_lo[c]     = 0xFFFF;
                g_raw_hi[c]     = 0;
            }
            g_raw_win_sweeps = 0;
            g_raw_win        = g_raw_win + 1u;
        }
#endif
    }
}

void panel_scan_gate(float* l, float* r, size_t n)
{
#if SHELL_PERSIST_ON
    // The boot mute: silent until the take has written, then the fade-in
    // (boot_gate_apply(), host tested). Same context as panel_scan_tick().
    static BootGate gate;
#if SHELL_REMOTE_MEASURE
    const uint32_t g0 = cycles_now();
#endif
    boot_gate_apply(gate, g_take.done, l, r, n);
#if SHELL_REMOTE_MEASURE
    g_sec_cur[SEC_GATE] = cycles_now() - g0;
#endif
#else
    (void)l; (void)r; (void)n;   // without a take the init patch boots unmuted
#endif
}

void panel_scan_block_end(uint32_t c0)
{
    const uint32_t dc = cycles_now() - c0;
    g_cb_cycles_last  = dc;
    if(g_law_blocks >= kLawWarmupBlocks && dc > g_cb_cycles_max) g_cb_cycles_max = dc;
#if SHELL_REMOTE
    // The overrun guard (workload_run.h) on this callback's cycles: integer
    // compares only, nothing printed here (the foreground says it).
    // A trip is counted on the running workload (trips=) and marks the frames
    // it silences (tripped=): until the guard re-arms its callbacks are silence
    // (spec 5.2), and the host keeps those frames apart from the others.
#if SHELL_REMOTE_MEASURE
    // Toggle bit 31 (util/probe_toggles.h): the guard is not consulted, so a
    // LATE repeated by bit 30 saturates the callback on purpose (plan Task 7).
    // Its streak is not fed meanwhile; the TOG that flips the bit restarts it
    // (OverrunGuard::reset_streak()), so it never resumes stale.
    // Whether process() ran this block: the callback asked the same question
    // before it, and nothing between changed the answer.
    const bool off       = guard_off();
    const bool processed = !panel_scan_guard_tripped();
#else
    constexpr bool off = false;
#endif
    // The entry-to-entry period, the back-to-back signature (workload_run.h,
    // kGuardPeriodCycles): read here, before the WIN window below overwrites
    // g_win_prev_entry with this callback's entry. 0 before the first block.
    const uint32_t period = g_win_prev_entry != 0u ? c0 - g_win_prev_entry : 0u;
    const bool was_tripped = g_guard.tripped();
    if(!off)
    {
        const bool now_tripped = g_guard.block(dc, period);
        if(now_tripped && !was_tripped)
        {
            g_guard_blk = g_rblock;
            g_guard_by  = g_guard.cause();
            g_guard_trips = g_guard_trips + 1u;
            g_run.on_guard_trip();
            publish_run();
        }
        else if(was_tripped && !now_tripped)
            note_guard_rearm(false);   // 500 silent blocks: it resumes by itself
        // The trip's own block and every silenced one: the frame's tripped=.
        if(now_tripped || was_tripped) g_guard_silent = g_guard_silent + 1u;
    }
    g_guard_flag = g_guard.tripped() && !off;
#endif
#if SHELL_WIN_ON
    // The probe image's per-save window (plan Task 6 probe 1): the worst
    // callback and the worst interval between two callback entries since the
    // foreground's last save line. A callback held off by the QSPI shows in
    // the interval even when its own cycles do not. The foreground asks for
    // the reset; the callback does it, so the counters have one writer.
    // SHELL_CB_WINDOW resets it once per printed frame instead.
    if(g_win_reset)
    {
        g_win_gap_max = 0;
        g_win_cb_max  = 0;
        g_win_over    = 0;
        g_win_blocks  = 0;
        g_win_reset   = false;
#if SHELL_REMOTE_MEASURE
        sec_reset();   // SHELL_SEC / SHELL_SECMAX share the window
#endif
    }
    if(g_win_prev_entry != 0 && c0 - g_win_prev_entry > g_win_gap_max)
        g_win_gap_max = c0 - g_win_prev_entry;
    g_win_prev_entry = c0;
    if(dc > g_win_cb_max) g_win_cb_max = dc;
    if(dc > kWinOverCycles) g_win_over = g_win_over + 1u;
    g_win_blocks = g_win_blocks + 1u;
#endif
#if SHELL_REMOTE_MEASURE
    sec_block(dc, processed);
#endif
    if(g_inst != nullptr)
        cb_profile_block_end(dc, g_law_cycles_last, g_led_cycles_last,
                             g_law_blocks >= kLawWarmupBlocks, *g_inst);
#if SHELL_REMOTE_MEASURE
    // The tail: everything this callback ran after dc's stamp -- the guard,
    // the WIN window, the sections and cb_profile's per-block accumulation
    // above -- published with the block count, as g_cb_cycles_last is, for
    // busy_window(). The last statements of the callback: only these two
    // stores and the interrupt's exit come after the stamp.
    g_tail_last = cycles_now() - c0 - dc;
    g_blk_done  = g_blk_done + 1u;
#endif
}

#if SHELL_REMOTE
void panel_scan_load_knobs(const float* knobs)
{
    if(knobs != g_knobs)
        for(int i = 0; i < ffctl::NUM_PARAMS; ++i) g_knobs[i] = knobs[i];
    // pot_release(): the next valid reading is the new reference, so a pot
    // must move kPotTouch from where it stands now before it writes again.
    for(PotOwner& o : g_owner) pot_release(o);
    control_rearm_after_take();
}

bool panel_scan_guard_tripped()
{
#if SHELL_REMOTE_MEASURE
    // Bit 31: a guard tripped before the bit was set stays latched but is not
    // obeyed; with the bit cleared again it silences until it re-arms (500
    // silent blocks, as block() counts them) or LOAD, HOME or RST clear it.
    if(guard_off()) return false;
#endif
    return g_guard.tripped();
}
#endif

#if SHELL_REMOTE_MEASURE
void panel_scan_late()
{
    uint32_t n  = g_late_next;
    g_late_next = 0;
    // Not while the guard holds the callback silent (final review C1): with
    // bit 30 the burn would otherwise go on after the trip, keep the callback
    // back to back and starve USB, so neither RST nor TOG nor BOOT could
    // arrive. A LATE that lands while tripped is dropped, not deferred. With
    // bit 31 panel_scan_guard_tripped() is false and the burn runs on purpose.
    if(panel_scan_guard_tripped()) return;
    // Bit 30: the last LATE again, on every block.
    if(n == 0 && spky::probe::on(spky::probe::toggles(), spky::probe::kBitShellLateRepeat))
        n = g_late_last;
    if(n == 0) return;
    const uint32_t t0 = cycles_now();
    while(cycles_now() - t0 < n) {}
}
#endif

void run_panel_scan_report(bench::Board& hw)
{
    hw.StartLog(false);
#if SHELL_REMOTE
    // Right after the log: StartLog() is what brings the CDC port up.
    remote_listen_start();
    // The `SHELL_REMOTE on` line as the log opens and every kRemoteEvery
    // passes (5 s), for the same reason as SHELL_PERSIST src below: nothing
    // waits for the host, and a line printed once is gone before it attaches.
    constexpr uint32_t kRemoteEvery = 10;
    uint32_t           remote_pass  = 0;
    // The previous frame's `before` run reading, for the counting=1 rule.
    RunSnap remote_prev = {0, 0, 0, -1, -1, false, false};
    // g_guard_silent at the previous frame: the difference is "the guard was
    // tripped at some block of this frame's window" (the run line's tripped=).
    uint32_t guard_silent_prev   = 0;
    // The trips and re-arms already said (foreground): a counter that moved
    // since is a line owed, so nothing is lost between two passes.
    uint32_t guard_trips_said    = 0;
    uint32_t guard_rearms_said   = 0;
#endif
#if SHELL_BOOTCMD
    // Right after the log, as the full listener: StartLog() brings the CDC port up.
    remote_listen_start();
    // The `SHELL_REMOTE on build= cmds=boot` line, at the full listener's
    // cadence: tools/remote_run.py's --expect-build reads the build from it.
    constexpr uint32_t kBootcmdEvery = 10;
    uint32_t           bootcmd_pass  = 0;
#endif
#if SHELL_SD
    // The card test, once, here: audio runs and the listener is up, so a card
    // that hangs still leaves BOOT working (sd_probe.h). Its lines go out
    // with the passes below.
    sd_bringup_run();
    uint32_t sd_pass = 0;
#endif
#if SHELL_REMOTE_MEASURE
    // The last busy window's figures, printed with the next frame.
    OutStats out_stats = {0, 0, 0, 0, 0, 0, 0, 0};
#endif
#if SHELL_PERSIST_ON
    // Where boot's hidden state came from (spec 2026-10-28 section 8): the
    // newest valid record and its seq, or the init patch (seq=0). As the log
    // opens and again every kPersistSrcEvery passes (5 s): StartLog(false)
    // does not wait for the host, so a terminal attached later would never
    // see a line printed only once.
    constexpr uint32_t kPersistSrcEvery = 10;
    uint32_t           persist_pass     = 0;
    bool               persist_partial_printed = false;
    // The save (spec section 5): two vectors, this pass's snapshot and the
    // previous pass's, so `changed` is "differs from half a second ago"
    // (persist_differs() ignores REC_A, REC_B and MODBTN, which are never
    // restored). Seeded from the first snapshot, which the callback publishes
    // only after the boot take, so the take never counts as a change.
    // Static, not on this never-returning frame: 1 KB.
    static float persist_cur[ffctl::NUM_PARAMS];
    static float persist_prev[ffctl::NUM_PARAMS];
    bool         persist_cur_excite[2] = {false, false};
    bool         persist_seeded        = false;
    SavePolicy   persist_policy;
    // Passes whose snapshot differed from the previous pass's, since boot:
    // printed as `chg=` beside the src line. It is the save trigger's raw
    // input, which the rate-limited save lines hide (plan Task 6 probe 3,
    // the dead-band question of spec 5.3).
    uint32_t persist_chg = 0;
    // The excite / engine line, latched about a second after the first pass
    // (plan Task 6 probe 5): by then a restored BBD deck has landed. Printed
    // then and again beside every src line -- once only, it went out before
    // the host had the port open (probe 4, 2026-10-28).
    const uint32_t persist_t_first = daisy::System::GetNow();
    bool           persist_boot_printed = false;
    int            persist_boot_v[5]    = {0, 0, -1, -1, 0};   // ex0 ex1 eng0 eng1 take
#if SHELL_PERSIST_STRESS_ON
    constexpr uint32_t kStressEveryMs     = 2000;   // quiet 2 s, floor 0: every 2 s
    uint32_t           persist_stress_last = persist_t_first;
#endif
#endif
    while(1)
    {
        // The span is copied field by field from the ISR's variable; a torn
        // read is possible and only cosmetic, this line is for a human. The
        // fence stops the compiler from hoisting the reads out of the loop:
        // g_span is not volatile, and nothing here tells it the ISR writes it.
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const int zero  = g_span.zero;
        const int rail  = g_span.rail;
        const int valid = g_span.valid ? 1 : 0;
        const int keys  = g_keys.pressed;
#if SHELL_COUPON_PROBE
        int v[3];
        for(int i = 0; i < 3; ++i)
        {
            const ControlEntry& e = kCouponControls[i];
            const int step = step_of(kActiveChain, e.group, e.ch);
            const int idx  = mux_channel(kActiveChain, step,
                                         kActiveChain.sense_of_group[e.group]);
            v[i] = static_cast<int>(g_value[idx] * 1000.0f);
        }
        hw.PrintLine("SHELL_PLAY rv2=%d rv4=%d rv6=%d zero=%d rail=%d "
                     "valid=%d sweeps=%d",
                     v[0], v[1], v[2], zero, rail, valid,
                     static_cast<int>(g_sweeps));
        // A second line, so the first keeps part 1's format and each stays
        // inside libDaisy's 128-byte log buffer (LOGGER_BUFFER).
        hw.PrintLine("SHELL_PLAY_IO keys=%d presses=%d adc11=%d adc12=%d", keys,
                     static_cast<int>(g_keys.presses[0]),
                     static_cast<int>(hw.adc.Get(daisy::patch_sm::ADC_11)),
                     static_cast<int>(hw.adc.Get(daisy::patch_sm::ADC_12)));
#else
#if SHELL_RAW_SPREAD
        // Once per window, before the frame: `SHELL_PLAY_R w=<window> r=<first
        // row>` and up to six `lo:hi` raw words per line, in the V lines' row
        // order. Worst case "SHELL_PLAY_R w=4294967295 r=72" plus six
        // " 65535:65535" is 102 bytes, inside the 128-byte log buffer.
        static uint32_t raw_printed = 0;
        static uint16_t raw_lo[kChannels];
        static uint16_t raw_hi[kChannels];
        uint32_t        w;
        do
        {
            w = g_raw_win;
            for(int c = 0; c < kChannels; ++c)
            {
                raw_lo[c] = g_raw_pub_lo[c];
                raw_hi[c] = g_raw_pub_hi[c];
            }
        } while(w != g_raw_win);
        if(w != 0 && w != raw_printed)
        {
            for(int r0 = 0; r0 < kTable.count; r0 += 6)
            {
                const int n = kTable.count - r0 < 6 ? kTable.count - r0 : 6;
                char      line[112];
                int       len = snprintf(line, sizeof line, "SHELL_PLAY_R w=%u r=%d",
                                         static_cast<unsigned>(w), r0);
                for(int i = 0; i < n; ++i)
                {
                    const ControlEntry& e    = kTable.entries[r0 + i];
                    const int           step = step_of(kActiveChain, e.group, e.ch);
                    const int           idx  = mux_channel(kActiveChain, step, e.sense);
                    len += snprintf(line + len, sizeof line - len, " %u:%u",
                                    static_cast<unsigned>(raw_lo[idx]),
                                    static_cast<unsigned>(raw_hi[idx]));
                }
                hw.PrintLine("%s", line);
            }
            // The two calibration channels in the same window (fault R3): low
            // source impedance on SENSE_0's own mux, so their spread tells the
            // pin's floor from the pots' share of it.
            const int zi = mux_channel(kActiveChain,
                                       step_of(kActiveChain, kRevaCalZero.group, kRevaCalZero.ch),
                                       kActiveChain.sense_of_group[kRevaCalZero.group]);
            const int ri = mux_channel(kActiveChain,
                                       step_of(kActiveChain, kRevaCalRail.group, kRevaCalRail.ch),
                                       kActiveChain.sense_of_group[kRevaCalRail.group]);
            hw.PrintLine("SHELL_PLAY_RC w=%u zero=%u:%u rail=%u:%u",
                         static_cast<unsigned>(w),
                         static_cast<unsigned>(raw_lo[zi]), static_cast<unsigned>(raw_hi[zi]),
                         static_cast<unsigned>(raw_lo[ri]), static_cast<unsigned>(raw_hi[ri]));
            raw_printed = w;
        }
#endif
        // Up to ten values per line, in generated_panel_map.h's row order:
        // seventy in one line would overrun libDaisy's 128-byte log buffer.
        // The last line carries the remainder (73 rows: seven of ten, one of
        // three). Worst case "SHELL_PLAY_V r=70" plus ten " -1000" is 77 bytes.
        for(int r0 = 0; r0 < kTable.count; r0 += 10)
        {
            const int n = kTable.count - r0 < 10 ? kTable.count - r0 : 10;
            char      line[96];
            int       len = snprintf(line, sizeof line, "SHELL_PLAY_V r=%d", r0);
            for(int i = 0; i < n; ++i)
            {
                const ControlEntry& e    = kTable.entries[r0 + i];
                const int           step = step_of(kActiveChain, e.group, e.ch);
                const int           idx  = mux_channel(kActiveChain, step, e.sense);
                len += snprintf(line + len, sizeof line - len, " %d",
                                static_cast<int>(g_value[idx] * 1000.0f));
            }
            hw.PrintLine("%s", line);
        }
        hw.PrintLine("SHELL_PLAY zero=%d rail=%d valid=%d sweeps=%d keys=%d "
                     "presses=%d,%d,%d,%d",
                     zero, rail, valid, static_cast<int>(g_sweeps), keys,
                     static_cast<int>(g_keys.presses[0]),
                     static_cast<int>(g_keys.presses[1]),
                     static_cast<int>(g_keys.presses[2]),
                     static_cast<int>(g_keys.presses[3]));
#endif
        // The shared control law's cost per block, in DWT cycles: the last
        // tick and the worst after the first second, 0 until then (spec
        // 2026-10-09-rev-a-p6b1 section 6:
        // above 9600 cycles, one point, it moves to every second block).
        // led_max is the LED law plus the lamp word, the worst after the
        // first second, counted apart from the law (spec 2026-10-18-rev-a-
        // p6b2 section 6). cb_max is the whole callback, scan through
        // process(), under the same rule. Six counters of at most seven
        // digits (a 2 ms block is 960 000 cycles) keep the line at 111 bytes
        // or less, inside libDaisy's 128-byte log buffer.
#if SHELL_REMOTE
        // The run this frame belongs to, printed right before the frame's LAW /
        // WIN lines (tools/run_stats.py reads it that way), with its gen (the
        // load line carries the same gen, so the host tells this run from an
        // earlier run of the same id). The frame's numbers are copied between
        // two run readings: `before` ahead of the copy and the WIN reset,
        // `after` behind them. counting=1 only when the run was settled at the
        // previous frame's `before` -- ahead of that frame's WIN reset, so
        // ahead of this window's first block -- and is the same gen, still
        // settled at this frame's `after`, behind its last block: the window
        // then holds counted blocks of that run only. A LOAD anywhere inside
        // it changes gen, and so does a TOG during a run (it re-settles it).
        // A guard trip does not stop the count (Bastian, 2026-10-29): tripped=
        // says the guard was tripped at some block of this window (silence in
        // part of it), trips= how often it has tripped since the run's ack.
        // tog= is the toggle word at `after` (0 outside the measurement
        // image); in a counted frame no TOG fell inside the window, so it held
        // throughout. At most 95 bytes (a 3-digit id and gest, a 5-digit gen,
        // a 5-digit trips).
        {
            const RunSnap  before = read_run();
            // Read with the WIN window's numbers, so tripped= covers the same
            // blocks as win= (the callback resets that window a block later).
            const uint32_t guard_silent = g_guard_silent;
            const uint32_t law_last = g_law_cycles_last, law_max = g_law_cycles_max;
            const uint32_t led_last = g_led_cycles_last, led_max = g_led_cycles_max;
            const uint32_t cb_last = g_cb_cycles_last, cb_max = g_cb_cycles_max;
#if !SHELL_PERSIST_STRESS_ON
            const uint32_t win = g_win_cb_max, gap = g_win_gap_max;
            const uint32_t over = g_win_over, n = g_win_blocks;
#if SHELL_REMOTE_MEASURE
            const SecSnap sec = read_sec();   // the same window, reset with it
#endif
            g_win_reset = true;
#endif
            const RunSnap after   = read_run();
            const bool    counted = remote_prev.counting && remote_prev.gen == after.gen
                                    && after.counting;
            remote_prev = before;
            // The guard was tripped at some block of this window (the trip's
            // own block, or one it silenced): the host keeps such a frame
            // apart from the others, it no longer stops counting.
            const bool     tripped      = guard_silent != guard_silent_prev;
            guard_silent_prev = guard_silent;
            hw.PrintLine("SHELL_REMOTE run id=%d gen=%u counting=%d void=%d tripped=%d trips=%u gest=%d "
                         "tog=%08x",
                         after.id, static_cast<unsigned>(after.gen & kGenPrintMask),
                         counted ? 1 : 0,
                         after.void_run ? 1 : 0, tripped ? 1 : 0,
                         static_cast<unsigned>(after.trips), after.gest,
                         static_cast<unsigned>(after.tog));
            hw.PrintLine("SHELL_PLAY_LAW cyc_last=%d cyc_max=%d led_last=%d led_max=%d "
                         "cb_last=%d cb_max=%d",
                         static_cast<int>(law_last), static_cast<int>(law_max),
                         static_cast<int>(led_last), static_cast<int>(led_max),
                         static_cast<int>(cb_last), static_cast<int>(cb_max));
#if !SHELL_PERSIST_STRESS_ON
            hw.PrintLine("SHELL_PLAY_WIN win=%u gap=%u over=%u n=%u",
                         static_cast<unsigned>(win), static_cast<unsigned>(gap),
                         static_cast<unsigned>(over), static_cast<unsigned>(n));
#if SHELL_REMOTE_MEASURE
            // The window's worst block and its own sections (they add up to
            // worst less the unsectioned rest, see SEC_COUNT), then each
            // section's maximum over the window, from whichever block. With
            // seven-digit figures at most 121 bytes, inside the 128-byte log
            // buffer.
            if(sec.worst_cb != 0)
            {
                hw.PrintLine("SHELL_SEC worst=%u scan=%u law=%u led=%u gate=%u persist=%u a=%u b=%u mix=%u",
                             static_cast<unsigned>(sec.worst_cb),
                             static_cast<unsigned>(sec.worst[SEC_SCAN]),
                             static_cast<unsigned>(sec.worst[SEC_LAW]),
                             static_cast<unsigned>(sec.worst[SEC_LED]),
                             static_cast<unsigned>(sec.worst[SEC_GATE]),
                             static_cast<unsigned>(sec.worst[SEC_PERSIST]),
                             static_cast<unsigned>(sec.worst[SEC_A]),
                             static_cast<unsigned>(sec.worst[SEC_B]),
                             static_cast<unsigned>(sec.worst[SEC_MIX]));
                hw.PrintLine("SHELL_SECMAX scan=%u law=%u led=%u gate=%u persist=%u a=%u b=%u mix=%u",
                             static_cast<unsigned>(sec.max[SEC_SCAN]),
                             static_cast<unsigned>(sec.max[SEC_LAW]),
                             static_cast<unsigned>(sec.max[SEC_LED]),
                             static_cast<unsigned>(sec.max[SEC_GATE]),
                             static_cast<unsigned>(sec.max[SEC_PERSIST]),
                             static_cast<unsigned>(sec.max[SEC_A]),
                             static_cast<unsigned>(sec.max[SEC_B]),
                             static_cast<unsigned>(sec.max[SEC_MIX]));
            }
            // The busy window ahead of this frame (busy_window()): its blocks
            // lie inside this frame's WIN window. Cycles per block; out takes
            // in the probe tail, ext leaves it out; neg the blocks whose split
            // saturated. With seven-digit figures and neg <= 512 at most 117
            // bytes.
            if(out_stats.n > 0)
                hw.PrintLine("SHELL_OUT busy_max=%u busy_p50=%u out_max=%u tail_max=%u "
                             "tail_p50=%u ext_max=%u neg=%d",
                             static_cast<unsigned>(out_stats.busy_max),
                             static_cast<unsigned>(out_stats.busy_p50),
                             static_cast<unsigned>(out_stats.out_max),
                             static_cast<unsigned>(out_stats.tail_max),
                             static_cast<unsigned>(out_stats.tail_p50),
                             static_cast<unsigned>(out_stats.ext_max),
                             out_stats.neg);
#endif
#endif
        }
#else
        hw.PrintLine("SHELL_PLAY_LAW cyc_last=%d cyc_max=%d led_last=%d led_max=%d "
                     "cb_last=%d cb_max=%d",
                     static_cast<int>(g_law_cycles_last),
                     static_cast<int>(g_law_cycles_max),
                     static_cast<int>(g_led_cycles_last),
                     static_cast<int>(g_led_cycles_max),
                     static_cast<int>(g_cb_cycles_last),
                     static_cast<int>(g_cb_cycles_max));
#if SHELL_CB_WINDOW && !SHELL_PERSIST_STRESS_ON
        // Since the previous frame: the worst callback, the worst interval
        // between two callback entries (a late block shows here even when its
        // own cycles stay under the deadline), the blocks above
        // kWinOverCycles, and how many blocks the window held.
        {
            const uint32_t win = g_win_cb_max, gap = g_win_gap_max;
            const uint32_t over = g_win_over, n = g_win_blocks;
            g_win_reset = true;
            hw.PrintLine("SHELL_PLAY_WIN win=%u gap=%u over=%u n=%u",
                         static_cast<unsigned>(win), static_cast<unsigned>(gap),
                         static_cast<unsigned>(over), static_cast<unsigned>(n));
        }
#endif
#endif // SHELL_REMOTE
#if defined(SPKY_PROBE_LAW)
        // The cyc_max block's TIDE split, in cycles and in calls (two lines,
        // each inside the 128-byte log buffer): the law around it is cyc_max.
        spky::ProbeLaw t;
        uint32_t       tn;
        do
        {
            tn = g_law_split_n;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            t = g_law_split;
            std::atomic_signal_fence(std::memory_order_seq_cst);
        } while(tn != g_law_split_n);
        hw.PrintLine("SHELL_PLAY_TIDE n=%d tide=%d steps=%d reslot=%d sinc=%d rate=%d rinc=%d",
                     static_cast<int>(tn), static_cast<int>(t.tide), static_cast<int>(t.steps),
                     static_cast<int>(t.reslot), static_cast<int>(t.sinc),
                     static_cast<int>(t.rate), static_cast<int>(t.rinc));
        hw.PrintLine("SHELL_PLAY_TIDEN tide=%d reslot=%d sinc=%d rinc=%d",
                     static_cast<int>(t.n_tide), static_cast<int>(t.n_reslot),
                     static_cast<int>(t.n_sinc), static_cast<int>(t.n_rinc));
#endif
        cb_profile_report(hw);
#if SHELL_BOOTCMD
        // cmds=boot: this image answers BOOT and PING only (the line shape
        // up to the build is the full listener's, which remote_lib.py matches).
        if(bootcmd_pass++ % kBootcmdEvery == 0)
            hw.PrintLine("SHELL_REMOTE on build=%s cmds=boot", remote_build());
        if(remote_take_ping()) hw.PrintLine("SHELL_REMOTE pong");
#endif
#if SHELL_REMOTE
        if(remote_pass++ % kRemoteEvery == 0)
        {
#if SHELL_REMOTE_MEASURE
            // The measurement image says so, and which toggle word it plays.
            hw.PrintLine("SHELL_REMOTE on build=%s measure=1 tog=%08x", remote_build(),
                         static_cast<unsigned>(spky::probe::toggles()));
#else
            hw.PrintLine("SHELL_REMOTE on build=%s", remote_build());
#endif
        }
        // PING is answered here, at most one pass (500 ms) late: the USB
        // interrupt only raises the flag, it never prints.
        if(remote_take_ping()) hw.PrintLine("SHELL_REMOTE pong");
        // What the callback did since the last pass. The tables are read here
        // from QSPI (memory-mapped; the save that would unmap it is
        // suppressed below). Every line stays inside the 128-byte log buffer:
        // the longest, a load line with a 31-character name and a 5-digit
        // gen, is 122 bytes (today's longest name has 28).
        Command a;
        while(g_acks.pop(&a))
        {
#if SHELL_REMOTE_MEASURE
            // TOG: the bit, its value and the word process() now latches;
            // settled: the maxima reset kTogSettleBlocks later, and the gen
            // the run counts under from here (the host follows it). LATE: k
            // and the block it burnt in (the last LATE's, read here).
            if(a.kind == CmdKind::Toggle && a.b >= kAckTogBase && a.b < kAckTogBase + kAckTogCodes)
                hw.PrintLine("SHELL_REMOTE tog bit=%d v=%d word=%08x",
                             static_cast<int>((a.b - kAckTogBase) >> 1),
                             static_cast<int>((a.b - kAckTogBase) & 1),
                             static_cast<unsigned>(a.a));
            else if(a.kind == CmdKind::Toggle && a.b >= kAckTogSettledBase)
                hw.PrintLine("SHELL_REMOTE tog settled word=%08x gen=%u rst",
                             static_cast<unsigned>(a.a),
                             static_cast<unsigned>(a.b - kAckTogSettledBase));
            else if(a.kind == CmdKind::Late && a.b == kAckOk)
                hw.PrintLine("SHELL_REMOTE late k=%d blk=%u", static_cast<int>(a.a),
                             static_cast<unsigned>(g_late_blk));
            else
#endif
            if(a.b == kAckTake)
                hw.PrintLine("SHELL_REMOTE take row=%d", static_cast<int>(a.a));
            else if(a.b == kAckBadId)
                hw.PrintLine("SHELL_REMOTE error cmd=%s id=%d range=0..%d",
                             a.kind == CmdKind::Load ? "LOAD" : "GEST", static_cast<int>(a.a),
                             (a.kind == CmdKind::Load ? kWorkloadCount : kGestureCount) - 1);
            else if(a.b == kAckUnsupported)
                hw.PrintLine("SHELL_REMOTE error cmd=%s unsupported (measurement image only)",
                             a.kind == CmdKind::Toggle ? "TOG" : "LATE");
            else if(a.b == kAckGestDone)
                hw.PrintLine("SHELL_REMOTE gest done id=%d", static_cast<int>(a.a));
            else if(a.kind == CmdKind::Home)
                hw.PrintLine("SHELL_REMOTE home restored=%d", static_cast<int>(a.a));
            else if(a.kind == CmdKind::ResetMax)
                hw.PrintLine("SHELL_REMOTE rst");
            else if(a.b >= kAckStarted
                    && (a.kind == CmdKind::Load || a.kind == CmdKind::Gesture))
            {
                // gen= is the run's (tools/run_stats.py keys on it); gest= on
                // the load line ties a gesture's base load to its gesture.
                const unsigned gen  = static_cast<unsigned>(a.b - kAckStarted);
                const int      gest = a.kind == CmdKind::Gesture ? static_cast<int>(a.a) : -1;
                int            id   = a.a;
                if(a.kind == CmdKind::Gesture)
                {
                    const GestureDef* g = gesture_def(a.a);
                    hw.PrintLine("SHELL_REMOTE gest id=%d gen=%u name=%s base=%d steps=%d blocks=%u",
                                 static_cast<int>(a.a), gen, g->name, static_cast<int>(g->base),
                                 static_cast<int>(g->n), static_cast<unsigned>(g->blocks));
                    id = g->base;
                }
                // input= is what the sweep fed the audio input; the board
                // feeds nothing and plays whatever is patched to its input.
                const WorkloadDef* w = workload_by_id(id);
                hw.PrintLine("SHELL_REMOTE load id=%d gen=%u gest=%d name=%s input=%s settle=%u "
                             "excite=%d,%d rec=%d,%d",
                             id, gen, gest, w->name,
                             w->input == WorkloadInput::Noise ? "noise" : "silence",
                             static_cast<unsigned>(w->settle_blocks), w->excite[0], w->excite[1],
                             w->rec[0], w->rec[1]);
            }
        }
        // While the guard holds the callback silent: every pass, so a host
        // that attaches late still sees why the board is quiet; and once for
        // every trip a pass has not said yet (a settle end can clear a guard
        // within a pass). by= is the criterion: cb (the callback's cycles) or
        // period (back to back).
        const uint32_t guard_trips = g_guard_trips;
        if(g_guard_flag || guard_trips != guard_trips_said)
            hw.PrintLine("SHELL_REMOTE guard tripped blk=%u by=%s", static_cast<unsigned>(g_guard_blk),
                         g_guard_by == GuardCause::Period ? "period" : "cb");
        guard_trips_said = guard_trips;
        // The guard resumed by itself: after 500 silent blocks (by=silence) or
        // at its run's settle end (by=settle). Once per re-arm.
        const uint32_t guard_rearms = g_guard_rearms;
        if(guard_rearms != guard_rearms_said)
            hw.PrintLine("SHELL_REMOTE guard rearm blk=%u by=%s",
                         static_cast<unsigned>(g_guard_rearm_blk),
                         g_guard_rearm_settle ? "settle" : "silence");
        guard_rearms_said = guard_rearms;
#endif
#if SHELL_PERSIST_ON
        if(persist_pass++ % kPersistSrcEvery == 0)
        {
            const SlotPick& b = persist_boot_pick();
            hw.PrintLine("SHELL_PERSIST src=%s seq=%u", b.sector >= 0 ? "flash" : "init",
                         static_cast<unsigned>(b.sector >= 0 ? b.seq : 0u));
            hw.PrintLine("SHELL_PERSIST chg=%u", static_cast<unsigned>(persist_chg));
            if(persist_boot_printed)
                hw.PrintLine("SHELL_PERSIST boot ex=%d,%d eng=%d,%d take=%d", persist_boot_v[0],
                             persist_boot_v[1], persist_boot_v[2], persist_boot_v[3],
                             persist_boot_v[4]);
        }
        // Once, about a second after the first pass: each deck's excite bit
        // and running engine, and whether the boot take is done. Read without
        // a handshake from the callback's state: bools and an engine id, a
        // torn read is impossible and a stale one costs nothing here.
        if(!persist_boot_printed && daisy::System::GetNow() - persist_t_first >= 1000u)
        {
            bool ex[2];
            control_get_excite(ex);
            persist_boot_v[0] = ex[0] ? 1 : 0;
            persist_boot_v[1] = ex[1] ? 1 : 0;
            persist_boot_v[2] = g_inst ? static_cast<int>(g_inst->engine_id(0)) : -1;
            persist_boot_v[3] = g_inst ? static_cast<int>(g_inst->engine_id(1)) : -1;
            persist_boot_v[4] = g_take.done ? 1 : 0;
            hw.PrintLine("SHELL_PERSIST boot ex=%d,%d eng=%d,%d take=%d", persist_boot_v[0],
                         persist_boot_v[1], persist_boot_v[2], persist_boot_v[3],
                         persist_boot_v[4]);
            persist_boot_printed = true;
        }
        // A take the fallback applied with rows missing (kTakeSweeps): once.
        // A complete take prints nothing.
        const int partial = g_take_partial;
        if(partial != 0 && !persist_partial_printed)
        {
            hw.PrintLine("SHELL_PERSIST take=partial seen=%d/%d", partial < 0 ? 0 : partial,
                         kTable.count);
            persist_partial_printed = true;
        }
        // The snapshot, between two equal, even sequence counts; a copy the
        // callback cut into is retried (the callback always finishes its own
        // write before this loop runs again, so the retry ends).
        uint32_t pub;
        do
        {
            pub = g_pub_seq;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            for(int i = 0; i < ffctl::NUM_PARAMS; ++i) persist_cur[i] = g_pub_knobs[i];
            persist_cur_excite[0] = g_pub_excite[0];
            persist_cur_excite[1] = g_pub_excite[1];
            std::atomic_signal_fence(std::memory_order_seq_cst);
        } while((pub & 1u) != 0 || pub != g_pub_seq);
        if(pub != 0 && !persist_seeded)
        {
            for(int i = 0; i < ffctl::NUM_PARAMS; ++i) persist_prev[i] = persist_cur[i];
            persist_seeded = true;
        }
        else if(pub != 0)
        {
            const bool changed = persist_differs(persist_cur, persist_prev);
            for(int i = 0; i < ffctl::NUM_PARAMS; ++i) persist_prev[i] = persist_cur[i];
            if(changed) ++persist_chg;
            const uint32_t now = daisy::System::GetNow();
#if SHELL_PERSIST_STRESS_ON
            // The probe image: a save every 2 s, changed or not.
            const bool due = now - persist_stress_last >= kStressEveryMs;
            if(due) persist_stress_last = now;
#else
            // 15 s unchanged and at most one save per 5 min (save_due()).
            const bool due = save_due(persist_policy, changed, now);
#endif
#if SHELL_REMOTE
            // Saves are suppressed in every SHELL_REMOTE image (spec 5.3): a
            // loaded workload must not replace Bastian's saved patch, and the
            // erase would take the QSPI out of memory-mapped mode under the
            // workload tables a LOAD reads (qspi_cold.ld, .qspi_rodata). The
            // policy is told the save happened, so one line per due save.
            // Nothing calls persist_write() here, so it is not even linked.
            (void)persist_cur_excite;
            if(due)
            {
                hw.PrintLine("SHELL_PERSIST save suppressed");
                save_done(persist_policy, now);
            }
#else
            if(due)
            {
                // Here and only here, in the foreground: the QSPI erase and
                // program never run in the audio interrupt (spec 5.2).
                static PersistRecord rec;
                std::memset(&rec, 0, sizeof rec);
                rec.layout = current_layout();
                rec.seq    = persist_latest().seq + 1u;   // 1 for the first record
                for(int i = 0; i < ffctl::NUM_PARAMS; ++i) rec.knobs[i] = persist_cur[i];
                rec.excite[0] = persist_cur_excite[0] ? 1 : 0;
                rec.excite[1] = persist_cur_excite[1] ? 1 : 0;
                record_seal(rec);
                // DWT cycles, not System::GetUs(): GetUs() wraps every ~21 s
                // and printed nonsense for a save across the wrap (probes 1
                // and 3). CYCCNT wraps at ~8.9 s, far above a ~70 ms save.
                const uint32_t c0 = cycles_now();
                const bool     ok = persist_write(hw.qspi, rec);
                const uint32_t us = (cycles_now() - c0) / kCoreMhz;
                // A failed save keeps the vector dirty: the same state is
                // tried again once the 5-min floor is up, without burning a
                // slot every half second meanwhile (save_failed()).
                if(ok) save_done(persist_policy, daisy::System::GetNow());
                else   save_failed(persist_policy, daisy::System::GetNow());
#if SHELL_PERSIST_STRESS_ON
                // Plus the callback since the previous save line: cb_max as
                // SHELL_PLAY_LAW's (since the first second), win = the worst
                // callback in this window, gap = the worst entry-to-entry
                // interval in it (2 ms block = 960 000 cycles). Worst case
                // about 110 B, inside the 128-byte log buffer.
                const uint32_t win = g_win_cb_max;
                const uint32_t gap = g_win_gap_max;
                g_win_reset        = true;
                hw.PrintLine("SHELL_PERSIST save seq=%u us=%u ok=%d cb_max=%u win=%u gap=%u",
                             static_cast<unsigned>(rec.seq), static_cast<unsigned>(us),
                             ok ? 1 : 0, static_cast<unsigned>(g_cb_cycles_max),
                             static_cast<unsigned>(win), static_cast<unsigned>(gap));
#else
                hw.PrintLine("SHELL_PERSIST save seq=%u us=%u ok=%d",
                             static_cast<unsigned>(rec.seq), static_cast<unsigned>(us),
                             ok ? 1 : 0);
#endif
            }
#endif // SHELL_REMOTE
        }
#endif
#if SHELL_REMOTE_MEASURE
        // The same 500 ms as Delay(500), spent counting what interrupts take
        // (busy_window()). What the foreground does while idle moves the
        // block-rate artifact (shell/Makefile, SHELL_IDLE_FILL: 7.5 dB on
        // 8 August, docs/bench/2026-08-23-978cbaf-artifact-triage.md), so
        // this image's recordings are not compared by level against plain
        // images.
        out_stats = busy_window();
#elif SHELL_SD
        // The same 500 ms, reading the card in every other ten raw windows
        // (row 11.2: the pots' spread streaming against idle).
        sd_report(hw, sd_pass++);
        sd_stream_pass(hw, g_raw_win);
#else
        hw.Delay(500);
#endif
    }
}

} // namespace shell

#endif // SHELL_PANEL_SCAN
