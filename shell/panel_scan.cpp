#include "panel_scan.h"

#include "shell_panel_scan.h"
#include "shell_coupon_probe.h"

#if SHELL_PANEL_SCAN

#include <atomic>
#include <cstdio>

#include "control/led_law.hpp"
#include "control_tick.h"
#include "controls.h"
#include "coupon_expect.h"
#include "cycles.h"
#include "keys.h"
#include "led_out.h"
#include "mux_scan.h"
#include "pot_owner.h"
#include "scan_value.h"
#if !SHELL_COUPON_PROBE
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
constexpr KeyPad        kKeys   = kRevaKeys;
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
// The mux channel of the FLUX_A / FLUX_B row, -1 when the board has none (the
// coupon): a bbd_edge event releases that pot (spec section 5).
int      g_flux_idx[2] = {-1, -1};

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

// The knob vector the shared control law reads (spec 2026-10-09-rev-a-p6b1
// section 4.2): every parameter in VCV's units, booted from the init patch,
// depths included. A pot writes it only while it owns its target (pot_owner.h);
// a key toggles its latch (apply_key_edges()). The law itself lives in
// control_tick.cpp.
float             g_knobs[ffctl::NUM_PARAMS];
volatile uint32_t g_law_cycles_max = 0;
volatile uint32_t g_law_cycles_last = 0;
// The LED law plus the lamp word, counted on their own (spec 2026-10-18-rev-a-
// p6b2 section 6: the LED law's cost alone and together with the law).
volatile uint32_t g_led_cycles_max = 0;
volatile uint32_t g_led_cycles_last = 0;
// Both worst cases ignore the first second (500 blocks): a cold-cache first
// tick, the LED law's one-time gamma table, or an interrupt during boot would
// otherwise pin them for good.
constexpr uint32_t kLawWarmupBlocks = 500;
uint32_t           g_law_blocks = 0;

// The LED law's duties and the block counter the lamp word's duty pattern
// runs on. The block period comes from the board (AudioBlockSize /
// AudioSampleRate), read on the first tick.
int      g_duty[ffctl::NUM_LIGHTS];
uint32_t g_led_frame = 0;
float    g_block_dt  = 0.0f;

} // namespace

void panel_scan_init()
{
    g_scan.init();
    g_scan.set_walk_leds(false);
    g_scan.set_read_keys(true);
    for(int c = 0; c < kChannels; ++c) g_value[c] = -1.0f;
    // The FLUX rows' channels, found the way panel_scan_tick() walks a step
    // (group_at / channel_at per sense pin), so the index is the one its
    // g_owner[] entry has.
    for(int r = 0; r < kTable.count; ++r)
    {
        const ControlEntry& e = kTable.entries[r];
        const int p = e.param == ffctl::FLUX_A ? 0 : e.param == ffctl::FLUX_B ? 1 : -1;
        if(p < 0) continue;
        const int step = step_of(kActiveChain, e.group, e.ch);
        for(int s = 0; s < kActiveChain.sense_pins; ++s)
            if(group_at(kActiveChain, step, s) == e.group
               && channel_at(kActiveChain, step, s) == e.ch)
                g_flux_idx[p] = mux_channel(kActiveChain, step, s);
    }
    // The firmware's boot is a patch restore of the init patch (spec 4.1).
    control_boot(g_knobs);
    // The DWT counter the law's cycle report reads; libDaisy never starts it,
    // and without this it reads zero forever (cycles.h).
    cycles_init();
}

void panel_scan_tick(bench::Board& hw, spky::Instrument& inst)
{
    const int step = g_scan.step(hw);
    // The return stream of the latch step() just clocked: the 165 loads on
    // the same edge, so the keys are read once per block.
    key_update(g_keys, kKeys, g_scan.last_return());
    // A key acts on its debounced press, never on release or hold (spec
    // 2026-10-18-rev-a-p6b2 section 3). MOD needs no release call here: every
    // pot whose target changes with the latch is released by pot_own() on its
    // next reading, because pot_target() hands it a different id.
    const uint8_t edges = static_cast<uint8_t>(g_keys.pressed & ~g_last_pressed);
    g_last_pressed      = g_keys.pressed;
    (void)apply_key_edges(edges, kKeyMap, g_knobs);
    if(step < 0) return;

    for(int s = 0; s < kActiveChain.sense_pins; ++s)
    {
        // A pin with no live channel this step floats; group_at() says so.
        const int g = group_at(kActiveChain, step, s);
        if(g < 0) continue;
        const int      ch  = channel_at(kActiveChain, step, s);
        const int      idx = mux_channel(kActiveChain, step, s);
        const uint16_t raw = g_mux_raw[idx];
#if SHELL_COUPON_PROBE
        g_step_raw[step_of(kCouponChain, g, ch)] = raw;
#else
        if(g == kRevaCalZero.group && ch == kRevaCalZero.ch) g_cal_zero = raw;
        if(g == kRevaCalRail.group && ch == kRevaCalRail.ch) g_cal_rail = raw;
#endif
        const ControlEntry* e = find_control(kTable, g, ch);
        if(e == nullptr) continue;
        // The id this pot writes right now: MOD-aware, and ATTACK writes
        // STAGES while its deck is on the BBD (pot_target, knob_target). -1
        // for a reserved row, or ATTACK-on-BBD under the latch: nothing.
        const int target = pot_target(*e, g_knobs);
        // A pot takes its target only by being moved (spec 2026-10-18-rev-a-
        // p6b2 section 2); a changed target releases it inside pot_own().
        const Own own = pot_own(g_owner[idx], raw, target, g_span.valid, kPotTouch);
        // The jump: on the move that takes the target, the filter emits this
        // reading, the pot's current position, whatever it emitted before.
        if(own == Own::Took) g_filter[idx].emitted = false;
        float v;
        if(pot_filter(g_filter[idx], raw, g_span, kPotHysteresis, &v))
        {
            g_value[idx] = v;               // every row is reported ...
            if(own != Own::Idle && target >= 0)
                g_knobs[target] = knob_from_pot(target, v);   // ... an owner writes
        }
    }

    // Once per block, the whole vector (spec 2026-10-09 section 4.2).
    const bool     warm = g_law_blocks >= kLawWarmupBlocks;
    if(!warm) ++g_law_blocks;
    const uint32_t c0 = cycles_now();
    const control::Events ev = control_tick(g_knobs, inst);
    const uint32_t dc = cycles_now() - c0;
    g_law_cycles_last = dc;
    if(warm && dc > g_law_cycles_max) g_law_cycles_max = dc;

    // The law's events (spec 2026-10-18-rev-a-p6b2 section 5). On a genuine
    // entry into the BBD, FLUX drops to 0 as VCV's knob does, and the FLUX pot
    // is released: FLUX stays at 0 until that pot is moved. control_tick()
    // has already turned the deck's excite_other_deck on. rec_started is
    // ignored: the hardware has no sample path to clear (spec section 3).
    for(int p = 0; p < 2; ++p)
    {
        if(!ev.bbd_edge[p]) continue;
        g_knobs[p ? ffctl::FLUX_B : ffctl::FLUX_A] = 0.0f;
        if(g_flux_idx[p] >= 0) pot_release(g_owner[g_flux_idx[p]]);
    }

    // The lamps (spec section 4): the LED law, then its duties as this
    // block's lamp word. set_leds() only stores the word; it reaches the 595s
    // with the NEXT step's latch, never on its own (P2 section 4), and this
    // is the one place it changes, once per block.
    if(g_block_dt <= 0.0f)
        g_block_dt = static_cast<float>(hw.AudioBlockSize()) / hw.AudioSampleRate();
    const uint32_t c1 = cycles_now();
    led_tick(inst, g_knobs[ffctl::MODBTN] > 0.5f, g_block_dt, g_duty);
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
    }
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
        // p6b2 section 6).
        hw.PrintLine("SHELL_PLAY_LAW cyc_last=%d cyc_max=%d led_last=%d led_max=%d",
                     static_cast<int>(g_law_cycles_last),
                     static_cast<int>(g_law_cycles_max),
                     static_cast<int>(g_led_cycles_last),
                     static_cast<int>(g_led_cycles_max));
        hw.Delay(500);
    }
}

} // namespace shell

#endif // SHELL_PANEL_SCAN
