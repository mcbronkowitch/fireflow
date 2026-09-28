#include "panel_scan.h"

#include "shell_panel_scan.h"
#include "shell_coupon_probe.h"

#if SHELL_PANEL_SCAN

#include <atomic>

#include "controls.h"
#include "coupon_expect.h"
#include "mux_scan.h"
#include "scan_value.h"

namespace shell {

namespace {

#if SHELL_COUPON_PROBE
constexpr ControlTable kTable = kCouponTable;
#else
constexpr ControlTable kTable = kPanelTable;
#endif

constexpr int kSteps    = scan_steps(kActiveChain);
constexpr int kChannels = mux_total(kActiveChain);

MuxScan   g_scan;
PotFilter g_filter[kChannels];

// Last emitted value per channel, -1 = never emitted. Read by the
// foreground for SHELL_PLAY only.
volatile float g_value[kChannels];

// The span the value path uses. On the coupon it starts invalid and is
// replaced after every full sweep whose ties give a valid one; an invalid
// sweep leaves it in place. On the panel it is kPanelSpan until part 2.
#if SHELL_COUPON_PROBE
Span     g_span{0, 0, false};
uint16_t g_step_raw[kSteps];   // the live pin's raw word per step
#else
Span g_span = kPanelSpan;
#endif
volatile uint32_t g_sweeps = 0;

} // namespace

void panel_scan_init()
{
    g_scan.init();
    g_scan.set_walk_leds(false);
    for(int c = 0; c < kChannels; ++c) g_value[c] = -1.0f;
}

void panel_scan_tick(bench::Board& hw, spky::Instrument& inst)
{
    const int step = g_scan.step(hw);
    if(step < 0) return;

    const int g  = group_of_step(kActiveChain, step);
    const int ch = static_cast<int>(step_pattern(kActiveChain, step).address);
    for(int s = 0; s < kActiveChain.sense_pins; ++s)
    {
        if(!sense_live(kActiveChain, step, s)) continue;
        const int      idx = mux_channel(kActiveChain, step, s);
        const uint16_t raw = g_mux_raw[idx];
#if SHELL_COUPON_PROBE
        g_step_raw[step] = raw;
#endif
        // On the panel profile every group sits on all four pins, so (g, ch)
        // names four channels; part 2's table will key on the sense pin too.
        // With an empty table this lookup never matches.
        const ControlEntry* e = find_control(kTable, g, ch);
        if(e == nullptr) continue;
        float v;
        if(pot_filter(g_filter[idx], raw, g_span, kPotHysteresis, &v))
        {
            apply_control(*e, v, inst);
            g_value[idx] = v;
        }
    }

    if(step == kSteps - 1)
    {
#if SHELL_COUPON_PROBE
        const Span sp = coupon_span(g_step_raw, kSteps);
        if(sp.valid) g_span = sp;
#endif
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
#else
        hw.PrintLine("SHELL_PLAY zero=%d rail=%d valid=%d sweeps=%d", zero,
                     rail, valid, static_cast<int>(g_sweeps));
#endif
        hw.Delay(500);
    }
}

} // namespace shell

#endif // SHELL_PANEL_SCAN
