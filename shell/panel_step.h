#pragma once

// The playing image's per-block glue, lifted out of panel_scan.cpp so it can
// be tested: one pot reading from its row to the knob vector, the law's events
// applied to the knob vector and the owners, the clock law's TEMPO release,
// and the FLUX and TEMPO rows' mux channels.
// No hardware type, host tested (tests/test_panel_step.cpp) -- the same
// arrangement as scan_value.h, controls.h and pot_owner.h. panel_scan.cpp
// (firmware only, under SHELL_PANEL_SCAN) calls these and nothing else does
// the work.
//
// Spec: ../docs/superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md
// sections 2, 3 and 5; ../docs/superpowers/specs/2026-10-19-clock-and-reset-jacks-design.md
// section 6.
#include <cstdint>
#include "controls.h"
#include "mux_plan.h"
#include "pot_owner.h"
#include "scan_value.h"

namespace control { struct Events; struct ClockOut; }

namespace shell {

// One reading of one mapped pot, in this order:
//   1. pot_target(): the id the pot writes right now (MOD-aware, ATTACK ->
//      STAGES on a BBD deck; -1 = nothing);
//   2. pot_own(): ownership; a changed target releases the pot first;
//   3. on Own::Took the filter forgets what it emitted, so this very reading
//      is emitted -- the parameter jumps to the pot's CURRENT position, not to
//      the reference the move was measured from;
//   4. pot_filter(): hysteresis and snap;
//   5. if the filter emitted and the pot owns a target, the value is written
//      into knobs[target] in VCV's units (knob_from_pot()).
// Returns whether the filter emitted; *v_out then holds the normalised value
// (every row is reported, only an owner writes).
bool pot_row_step(const ControlEntry& e, PotOwner& owner, PotFilter& filter,
                  uint16_t raw, const Span& span, float* knobs, float* v_out);

// The law's events (spec section 5). bbd_edge[p]: deck p's FLUX knob drops to
// 0, as VCV's does, and the FLUX pot (owners[flux_idx[p]], skipped when -1) is
// released, so FLUX holds 0 until that pot moves past kPotTouch again.
// rec_started is ignored: the hardware has no sample path to clear (spec
// section 3). excite_other_deck is control_tick()'s job, not this one's.
void apply_law_events(const control::Events& ev, float* knobs, PotOwner* owners,
                      const int flux_idx[2]);

// The mux channel (g_mux_raw / owner index) of the FLUX_A row in out[0] and
// the FLUX_B row in out[1], -1 where the table has none (the coupon). Found
// the way panel_scan_tick() walks a step -- group_at() / channel_at() per
// sense pin -- so it is the index that row's owner has.
void flux_channels(const ControlTable& t, const ChainProfile& p, int out[2]);

// The mux channel of the TEMPO row, found the same way; -1 where the table has
// none (the coupon).
int tempo_channel(const ControlTable& t, const ChainProfile& p);

// The clock law's events for the pots (spec 2026-10-19-clock-and-reset-jacks
// section 6, "Knob takeover"): on `lost` the TEMPO pot (owners[tempo_idx],
// skipped when -1) is released, so the pot's idle noise cannot count as a
// turn -- it must move past kPotTouch before the knob vector's TEMPO changes
// and the knob takes the tempo back. The same release a bbd_edge gives FLUX.
void apply_clock_events(const control::ClockOut& o, PotOwner* owners, int tempo_idx);

} // namespace shell
