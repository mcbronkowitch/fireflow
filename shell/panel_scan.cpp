#include "panel_scan.h"

#include "shell_panel_scan.h"
#include "shell_coupon_probe.h"

#if SHELL_PANEL_SCAN

#include <atomic>
#include <cstdio>

#include "control_tick.h"
#include "controls.h"
#include "coupon_expect.h"
#include "cycles.h"
#include "keys.h"
#include "mux_scan.h"
#include "scan_value.h"
#if !SHELL_COUPON_PROBE
#include "generated_panel_map.h"
#endif

namespace shell {

namespace {

#if SHELL_COUPON_PROBE
constexpr ControlTable kTable = kCouponTable;
constexpr KeyPad       kKeys  = kCouponKeys;
#else
constexpr ControlTable kTable = kRevaTable;
constexpr KeyPad       kKeys  = kRevaKeys;
#endif

constexpr int kSteps    = scan_steps(kActiveChain);
constexpr int kChannels = mux_total(kActiveChain);

MuxScan   g_scan;
PotFilter g_filter[kChannels];
KeyState  g_keys;

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

// P6b-2 Task 1 probe, removed in Task 7. Two questions for the coupon's LED
// and pots (spec 2026-10-18-rev-a-p6b2-mod-keys-leds-design.md sections 2, 4.3):
// how finely may LED_1 be dimmed before it visibly flickers, and how far does
// an untouched pot's raw word wander while the LED switches.
struct DimPreset
{
    int n; // steps; the pattern repeats every n-1 blocks
    int d; // on-blocks per period, 0..n-1
};
// Duty 1 of N for N = 4, 6, 8, 16, then full on, then off. The last two use
// N = 16 only so that the report line has a value to print.
constexpr DimPreset kDimPresets[] = {{4, 1}, {6, 1}, {8, 1}, {16, 1}, {16, 15}, {16, 0}};
constexpr int       kDimCount     = static_cast<int>(sizeof(kDimPresets) / sizeof(kDimPresets[0]));
constexpr int       kProbePots    = 3;  // RV2, RV4, RV6: kCouponControls order
constexpr uint32_t  kSpreadBlocks = 500; // one second at 500 blocks/s

volatile int      g_dim_preset = 0;
uint16_t          g_last_presses = 0;
uint32_t          g_dim_frame = 0;
int               g_pot_step[kProbePots];       // index into g_step_raw
uint16_t          g_win_min[kProbePots];
uint16_t          g_win_max[kProbePots];
uint32_t          g_win_blocks = 0;
volatile uint16_t g_spread[kProbePots];         // last full window
volatile uint32_t g_spread_seq = 0;             // bumps when g_spread is new

void spread_window_reset()
{
    for(int i = 0; i < kProbePots; ++i)
    {
        g_win_min[i] = 0xFFFFu;
        g_win_max[i] = 0u;
    }
    g_win_blocks = 0;
}
#else
uint16_t g_cal_zero = 0;
uint16_t g_cal_rail = 0;
#endif
volatile uint32_t g_sweeps = 0;

// The knob vector the shared control law reads (spec 2026-10-09-rev-a-p6b1
// section 4.2): every parameter in VCV's units, booted from the init patch --
// depths included, which P6b-1 does not let the panel edit yet. The law
// itself lives in control_tick.cpp.
float             g_knobs[ffctl::NUM_PARAMS];
volatile uint32_t g_law_cycles_max = 0;
volatile uint32_t g_law_cycles_last = 0;
// The worst case ignores the first second (500 blocks): a cold-cache first
// tick or an interrupt during boot would otherwise pin it for good.
constexpr uint32_t kLawWarmupBlocks = 500;
uint32_t           g_law_blocks = 0;

} // namespace

void panel_scan_init()
{
    g_scan.init();
    g_scan.set_walk_leds(false);
    g_scan.set_read_keys(true);
    for(int c = 0; c < kChannels; ++c) g_value[c] = -1.0f;
#if SHELL_COUPON_PROBE
    // P6b-2 Task 1 probe, removed in Task 7.
    for(int i = 0; i < kProbePots; ++i)
        g_pot_step[i] = step_of(kCouponChain, kCouponControls[i].group,
                                kCouponControls[i].ch);
    spread_window_reset();
#endif
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
#if SHELL_COUPON_PROBE
    // P6b-2 Task 1 probe, removed in Task 7: each key press advances the dim
    // preset, and LED_1 follows the preset's duty pattern. This replaces the
    // earlier "key held, LED_1 lit" gesture (spec 2026-10-02 section 3.7) in
    // this image; the key path is still exercised by the press counter. The
    // word still reaches the 595s with the NEXT step's latch, never on its
    // own, and changes only here, once per block.
    if(g_keys.presses[0] != g_last_presses)
    {
        g_last_presses = g_keys.presses[0];
        g_dim_preset   = (g_dim_preset + 1) % kDimCount;
    }
    {
        // Task 7's spread rule: on in block f iff
        // floor((f+1)*d/(n-1)) > floor(f*d/(n-1)).
        const DimPreset p = kDimPresets[g_dim_preset];
        const uint32_t  m = static_cast<uint32_t>(p.n - 1);
        const uint32_t  f = g_dim_frame % m;
        const uint32_t  d = static_cast<uint32_t>(p.d);
        g_dim_frame       = g_dim_frame + 1u;
        g_scan.set_leds(((f + 1u) * d) / m > (f * d) / m ? 1u : 0u);
    }
    // One window = one second of blocks; the foreground only prints it. A pot
    // that saw no sample in the window reports 0xFFFF (never a real spread).
    if(++g_win_blocks >= kSpreadBlocks)
    {
        for(int i = 0; i < kProbePots; ++i)
            g_spread[i] = g_win_max[i] >= g_win_min[i]
                              ? static_cast<uint16_t>(g_win_max[i] - g_win_min[i])
                              : 0xFFFFu;
        g_spread_seq = g_spread_seq + 1u;
        spread_window_reset();
    }
#endif
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
        const int craw_step = step_of(kCouponChain, g, ch);
        g_step_raw[craw_step] = raw;
        // P6b-2 Task 1 probe, removed in Task 7: min and max of each pot's
        // raw word over the current one-second window.
        for(int i = 0; i < kProbePots; ++i)
        {
            if(craw_step != g_pot_step[i]) continue;
            if(raw < g_win_min[i]) g_win_min[i] = raw;
            if(raw > g_win_max[i]) g_win_max[i] = raw;
        }
#else
        if(g == kRevaCalZero.group && ch == kRevaCalZero.ch) g_cal_zero = raw;
        if(g == kRevaCalRail.group && ch == kRevaCalRail.ch) g_cal_rail = raw;
#endif
        const ControlEntry* e = find_control(kTable, g, ch);
        if(e == nullptr) continue;
        float v;
        if(pot_filter(g_filter[idx], raw, g_span, kPotHysteresis, &v))
        {
            g_value[idx] = v;               // reserved rows are reported too
            if(e->param < 0) continue;      // ... and send nothing
            // ATTACK writes STAGES while its deck is on the BBD (knob_target).
            const int id = knob_target(*e, g_knobs);
            g_knobs[id] = knob_from_pot(id, v);
        }
    }

    // Once per block, the whole vector (spec section 4.2). Events are not
    // applied: a physical FLUX pot cannot be turned back (spec section 8).
    const uint32_t c0 = cycles_now();
    control_tick(g_knobs, inst);
    const uint32_t dc = cycles_now() - c0;
    g_law_cycles_last = dc;
    if(g_law_blocks < kLawWarmupBlocks)
        ++g_law_blocks;
    else if(dc > g_law_cycles_max)
        g_law_cycles_max = dc;

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
#if SHELL_COUPON_PROBE
    int      last_dim        = -1;
    uint32_t last_spread_seq = 0;
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
        const int      dim        = g_dim_preset;
        const uint32_t spread_seq = g_spread_seq;
        const int      spread[kProbePots] = {g_spread[0], g_spread[1], g_spread[2]};
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
        // P6b-2 Task 1 probe, removed in Task 7. SHELL_DIM prints on every
        // preset change (and once at boot); SHELL_SPREAD once per finished
        // one-second window. The loop wakes twice a second, so a change that
        // is undone inside one wake-up would go unprinted.
        if(dim != last_dim)
        {
            last_dim = dim;
            hw.PrintLine("SHELL_DIM n=%d d=%d", kDimPresets[dim].n,
                         kDimPresets[dim].d);
        }
        if(spread_seq != last_spread_seq)
        {
            last_spread_seq = spread_seq;
            hw.PrintLine("SHELL_SPREAD rv2=%d rv4=%d rv6=%d", spread[0],
                         spread[1], spread[2]);
        }
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
        hw.PrintLine("SHELL_PLAY_LAW cyc_last=%d cyc_max=%d",
                     static_cast<int>(g_law_cycles_last),
                     static_cast<int>(g_law_cycles_max));
        hw.Delay(500);
    }
}

} // namespace shell

#endif // SHELL_PANEL_SCAN
