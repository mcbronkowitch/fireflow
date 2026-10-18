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
    // The CPU headroom round's stress patches (spec 2026-10-18-cpu-headroom-
    // design.md section 4.6), built with C_USER_FLAGS=-DSHELL_BOOT_PATCH=N on a
    // clean build: the init patch with one deck's engine changed. ENGINE knob
    // values as control_law.h maps them: 2 = WAVE, 5 = FEED; the init patch is
    // A = FEED, B = WAVE. The coupon's pots stay unowned, so the variant holds.
#if defined(SHELL_BOOT_PATCH) && SHELL_BOOT_PATCH == 1
    knobs[ffctl::ENGINE_A] = 2.0f;   // W2: both decks WAVE
#elif defined(SHELL_BOOT_PATCH) && SHELL_BOOT_PATCH == 2
    knobs[ffctl::ENGINE_B] = 5.0f;   // F2: both decks FEED
#elif defined(SHELL_BOOT_PATCH) && SHELL_BOOT_PATCH != 0
#error "SHELL_BOOT_PATCH must be 0 (init), 1 (W2) or 2 (F2)"
#endif
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
