#pragma once

// The shared control law (control/control_law.h) as the firmware runs it:
// one ControlLaw instance over a knob vector in VCV's units, ticked once per
// audio block. Its own translation unit so that it alone builds with -Os
// (shell/Makefile): at -O3 the law costs about 14 KB of SRAM_EXEC and the
// Rev A image keeps 2.7 KB free, under spec 2026-10-09-rev-a-p6b1 section 5's
// 8 KB floor.
//
// Spec: ../docs/superpowers/specs/2026-10-09-rev-a-p6b1-shared-control-law-design.md
// sections 4.2 and 5.
#include "instrument.h"

namespace shell {

// Boot: knobs[0..ffctl::NUM_PARAMS) become the init patch
// (ffctl::kInitParamDefaults) -- depths included, which P6b-1 does not let the
// panel edit yet -- and the law is re-armed as for a patch restore, so the
// init patch's SONG rung and DRIFT position are a baseline, not a turn.
void control_boot(float* knobs);

// Once per block, the whole vector (spec section 4.2). Events are not
// applied: a physical FLUX pot cannot be turned back (spec section 8).
void control_tick(const float* knobs, spky::Instrument& inst);

} // namespace shell
