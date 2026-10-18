#include "shell_panel_scan.h"

// Only the playing images run the law. Elsewhere this file is empty, so the
// law's static constructor does not land in images that never call it.
#if SHELL_PANEL_SCAN

#include "control_tick.h"
#include "control/led_law.hpp"

namespace shell {

namespace {
control::ControlLaw g_law;
// Persistent, like VCV's per-deck SamplerPartState: the law's bbd_edge event
// turns excite_other_deck on and nothing on the hardware turns it off again.
control::Options    g_opt;
spkyled::Panel      g_panel;
} // namespace

void control_boot(float* knobs)
{
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) knobs[i] = ffctl::kInitParamDefaults[i];
    g_law.on_restore();
    // The LED law's gamma table (kLedSteps - 1 powf calls) is a function-local
    // static built on first use. Built here, before StartAudio(), so the first
    // fill() inside the audio interrupt finds it ready.
    (void)spkyled::gamma_table(spkyled::kLedSteps);
}

control::Events control_tick(const float* knobs, spky::Instrument& inst)
{
    const control::Events ev = g_law.tick(knobs, g_opt, inst);
    for(int p = 0; p < 2; ++p)
        if(ev.bbd_edge[p]) g_opt.deck[p].excite_other_deck = true;
    return ev;
}

void led_tick(const spky::Instrument& inst, bool mod_latched, float dt, int* duty_out)
{
    spkyled::LedInputs in;
    in.mod_latched = mod_latched;
    spkyled::fill(inst, g_panel, dt, spkyled::kLedSteps, in, duty_out);
}

} // namespace shell

#endif // SHELL_PANEL_SCAN
