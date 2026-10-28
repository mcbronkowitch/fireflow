#include "panel_scan.h"

#include "shell_panel_scan.h"
#include "shell_coupon_probe.h"
#include "shell_keys_nc.h"
#include "shell_raw_spread.h"

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
    // The firmware's boot is a patch restore of the init patch (spec 4.1).
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
        // Target (MOD-aware, ATTACK -> STAGES on the BBD), ownership (spec
        // 2026-10-18-rev-a-p6b2 section 2), the jump to the pot's current
        // position on the move that takes it, the filter, and the write by
        // an owner only: pot_row_step(), host tested.
        float v;
        if(pot_row_step(*e, g_owner[idx], g_filter[idx], raw, g_span, g_knobs, &v))
            g_value[idx] = v;               // every row is reported
    }

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

    if(step == kSteps - 1)
    {
#if SHELL_COUPON_PROBE
        const Span sp = coupon_span(g_step_raw, kCouponSteps);
#else
        const Span sp = panel_span(g_cal_zero, g_cal_rail);
#endif
        if(sp.valid) g_span = sp;
        g_sweeps = g_sweeps + 1u;
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

void panel_scan_block_end(uint32_t c0)
{
    const uint32_t dc = cycles_now() - c0;
    g_cb_cycles_last  = dc;
    if(g_law_blocks >= kLawWarmupBlocks && dc > g_cb_cycles_max) g_cb_cycles_max = dc;
    if(g_inst != nullptr)
        cb_profile_block_end(dc, g_law_cycles_last, g_led_cycles_last,
                             g_law_blocks >= kLawWarmupBlocks, *g_inst);
}

void run_panel_scan_report(bench::Board& hw)
{
    hw.StartLog(false);
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
        hw.PrintLine("SHELL_PLAY_LAW cyc_last=%d cyc_max=%d led_last=%d led_max=%d "
                     "cb_last=%d cb_max=%d",
                     static_cast<int>(g_law_cycles_last),
                     static_cast<int>(g_law_cycles_max),
                     static_cast<int>(g_led_cycles_last),
                     static_cast<int>(g_led_cycles_max),
                     static_cast<int>(g_cb_cycles_last),
                     static_cast<int>(g_cb_cycles_max));
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
        hw.Delay(500);
    }
}

} // namespace shell

#endif // SHELL_PANEL_SCAN
