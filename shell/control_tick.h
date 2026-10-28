#pragma once

// The shared control law (control/control_law.h) as the firmware runs it:
// one ControlLaw instance over a knob vector in VCV's units, ticked once per
// audio block, and the shared LED law (control/led_law.hpp) after it. Its own
// translation unit so that it alone builds with -O2 (shell/Makefile): at -O3
// the law costs about 14 KB of SRAM_EXEC and the Rev A image keeps 2.7 KB
// free, under spec 2026-10-09-rev-a-p6b1 section 5's 8 KB floor.
//
// Spec: ../docs/superpowers/specs/2026-10-09-rev-a-p6b1-shared-control-law-design.md
// sections 4.2 and 5; ../docs/superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md
// sections 4 and 5.
#include "instrument.h"
#include "control/control_law.h"

namespace shell {

// Boot: knobs[0..ffctl::NUM_PARAMS) become the init patch
// (ffctl::kInitParamDefaults), depths included, and the law is re-armed as for
// a patch restore, so the init patch's SONG rung and DRIFT position are a
// baseline, not a turn. It also builds the LED law's gamma table, so that
// one-time build does not happen inside the audio interrupt.
void control_boot(float* knobs);

// Once per block, the whole vector (spec 2026-10-09 section 4.2). Returns the
// law's events for the caller to apply to the knob vector (P6b-2 spec section
// 5). This function itself runs apply_excite_events() (panel_step.h) on the
// persistent control::Options it passes the law: deck p's excite_other_deck is
// on while the deck is on BBD and off again when it leaves; it takes effect
// from the next tick on, as in VCV.
// measured_bpm is control::ClockOut::measured_bpm from this block's clock law
// step (control/clock_law.hpp), 0 = the TEMPO knob sets the tempo; the coupon,
// which reads no gate pin, passes 0.
control::Events control_tick(const float* knobs, spky::Instrument& inst, float measured_bpm);

// Once per block, after control_tick(): the LED law, NUM_LIGHTS duties of
// 0..spkyled::kLedSteps-1 into duty_out. SHIFT is never latched. clock_lamp /
// reset_lamp are the clock law's lamp flags for this block (a CLOCK / RESET
// edge at the jack); the coupon passes false. mod_cleared: SHIFT + MOD cleared
// the layer since the last call, which starts the MOD lamp's receipt blink.
void led_tick(const spky::Instrument& inst, bool mod_latched, float dt, int* duty_out,
              bool clock_lamp, bool reset_lamp, bool mod_cleared);

} // namespace shell
