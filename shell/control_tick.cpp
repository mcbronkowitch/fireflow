#include "shell_panel_scan.h"

// Only the playing images run the law. Elsewhere this file is empty, so the
// law's static constructor does not land in images that never call it.
#if SHELL_PANEL_SCAN

#include "control_tick.h"
#include "boot_patch.h"
#include "panel_step.h"
#include "control/led_law.hpp"
#include "generated_boot_patch_noon.h"
#include "generated_panel_map.h"

namespace shell {

namespace {
control::ControlLaw g_law;
// Persistent, like VCV's per-deck SamplerPartState: the law's bbd_edge event
// turns excite_other_deck on, and apply_excite_events() turns it off again on
// the first tick the deck is off BBD (the panel has no checkbox to clear it).
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
#elif defined(SHELL_BOOT_PATCH) && SHELL_BOOT_PATCH == 3
    // Fault R4's all-noon patch (docs/hardware/reva-bring-up.md 10.1): the Rev
    // A pot frame the board played when its callback mean reached the
    // deadline, replayed through the panel's own row-to-knob path
    // (boot_patch.h, generated_boot_patch_noon.h). On the Rev A board the
    // pots at noon hold it: a pot writes its knob only once it has moved past
    // kPotTouch since boot (pot_row_step()).
    apply_boot_patch_rows(kRevaTable, kBootPatchNoonRows, kBootPatchNoonRowCount, knobs);
#elif defined(SHELL_BOOT_PATCH) && SHELL_BOOT_PATCH != 0
#error "SHELL_BOOT_PATCH must be 0 (init), 1 (W2), 2 (F2) or 3 (all-noon, fault R4)"
#endif
    g_law.on_restore();
    // The LED law's gamma table (kLedSteps - 1 powf calls) is a function-local
    // static built on first use. Built here, before StartAudio(), so the first
    // fill() inside the audio interrupt finds it ready.
    (void)spkyled::gamma_table(spkyled::kLedSteps);
}

control::Events control_tick(const float* knobs, spky::Instrument& inst, float measured_bpm)
{
    // The law takes it in 20..400 BPM and otherwise uses the TEMPO knob
    // (control_law.h); the clock law hands over 0 when the knob owns the tempo.
    g_opt.measured_bpm = measured_bpm;
    const control::Events ev = g_law.tick(knobs, g_opt, inst);
    apply_excite_events(ev, g_opt);
    return ev;
}

void led_tick(const spky::Instrument& inst, bool mod_latched, float dt, int* duty_out,
              bool clock_lamp, bool reset_lamp)
{
    spkyled::LedInputs in;
    in.mod_latched = mod_latched;
    in.clock_pulse = clock_lamp;
    in.reset_pulse = reset_lamp;
    spkyled::fill(inst, g_panel, dt, spkyled::kLedSteps, in, duty_out);
}

} // namespace shell

#endif // SHELL_PANEL_SCAN
