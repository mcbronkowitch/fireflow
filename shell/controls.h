#pragma once

// Mux channel -> control-law parameter, per board, as data. No hardware type:
// tests/test_controls_map.cpp holds it on the host. A knob that does the
// wrong thing is only audible on a board and expensive to find; here it is
// one row.
//
// The coupon's table maps its three measured pots (pot_plan.h), Bastian's
// choice of 2026-09-28.
// Rev A's table is generated: shell/generated_panel_map.h, from
// hardware/reva/panel-map.json (spec 2026-10-02-rev-a-p6a-panel-scan-design.md).
//
// The ids are the shared control law's (control/params.hpp, numerically
// VCV's): the firmware keeps a knob vector in VCV's units and the law turns
// it into engine calls, exactly as FireflowHW does (spec
// 2026-10-09-rev-a-p6b1-shared-control-law-design.md section 4.2).
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
#include <cstdint>
#include "control/params.hpp"
#include "keys.h"

namespace shell {

struct ControlEntry
{
    int group;       // mux group (chip) as in mux_plan.h; on Rev A the global mux 0..9
    int ch;          // channel on that chip
    int param;       // ffctl::ParamId, or -1: scanned and reported, sent nowhere
    int sense = -1;  // the sense pin the schematic wires the group to, -1 = not recorded
    // The id FireflowHW draws at the same position instead of `param` while
    // the deck's ENGINE knob sits on the BBD (ATTACK -> STAGES), -1 = none.
    // A per-deck id: its deck is `param`'s (see knob_target()).
    int alt = -1;
};

struct ControlTable
{
    const ControlEntry* entries;
    int                 count;
};

inline constexpr ControlEntry kCouponControls[] = {
    {0, 2, ffctl::RATE_A},      // RV2, 10 k, on the 4067
    {0, 6, ffctl::DENSITY_A},   // RV4, 20 k, on the 4067
    {1, 2, ffctl::FILT_A},      // RV6, 10 k, on the 4051
};

inline constexpr ControlTable kCouponTable{
    kCouponControls,
    static_cast<int>(sizeof(kCouponControls) / sizeof(kCouponControls[0]))};

// True when every row's ids index the knob vector: param and alt each -1 or a
// ffctl::ParamId, and an alt only on a sending row of a deck's own block
// (ids below 2 * PART_STRIDE), which knob_target() reads the deck from.
constexpr bool entries_valid(const ControlEntry* e, int n)
{
    for(int i = 0; i < n; ++i)
    {
        if(e[i].param < -1 || e[i].param >= ffctl::NUM_PARAMS) return false;
        if(e[i].alt < -1 || e[i].alt >= ffctl::NUM_PARAMS) return false;
        if(e[i].alt >= 0 && (e[i].param < 0 || e[i].param >= 2 * ffctl::PART_STRIDE))
            return false;
    }
    return true;
}
static_assert(entries_valid(kCouponControls, kCouponTable.count),
              "kCouponControls: an id outside the knob vector");

// The entry for (group, ch), or nullptr. A channel that is not in the table
// changes nothing: a half-seated chip produces indices nobody planned, and
// guessing would hand a foreign knob's voltage to a parameter.
const ControlEntry* find_control(const ControlTable& t, int group, int ch);

// lo + v * (hi - lo) of the parameter's range in control/params.hpp, rounded
// for a parameter Rack snaps (ENGINE, STEPS, FLUXRATE, SCALE, SONG ...), or 0
// for no parameter. This is the only thing the firmware does to a pot before
// the shared control law sees it (spec 2026-10-09-rev-a-p6b1 section 4.2).
float knob_from_pot(int param, float v);

// The ENGINE knob's BBD position: FireflowHW's isBbdSelected() (host/vcv/src/
// Fireflow.cpp) is `round(ENGINE) == 4`, and the law maps 4 to ENGINE_BBD.
// tests/test_controls_map.cpp holds this against the law, slot by slot.
inline constexpr int kEngineSlotBbd = 4;

// The id a pot writes right now: `alt` while the row's deck has its ENGINE
// knob on the BBD (FireflowHW's ctlVisible() shows STAGES there, ATTACK on
// every other engine), else `param`. The id not chosen keeps its last value,
// as two separate Rack params do.
int knob_target(const ControlEntry& e, const float* knobs);

// The depthId of the first kModLayer row whose soundId is `param`, -1 when
// there is none or `param` is outside the knob vector. A lookup in a table
// built at compile time, not a walk over kModLayer: pot_target() asks it for
// every pot reading while MOD is latched, inside the audio interrupt.
int mod_depth_of(int param);

// The id a pot writes right now (spec 2026-10-18-rev-a-p6b2 section 3). MOD not
// latched (knobs[MODBTN] < 0.5): knob_target(). Latched: a pot whose row `param`
// has a kModLayer row writes that row's depthId, but only while knob_target()
// is still `param` -- ATTACK on a BBD deck targets STAGES, which has no depth
// twin, so it writes nothing (-1), as VCV hides both halves of that knob under
// the latch. A pot with no kModLayer row keeps knob_target(). Reserved rows
// (param -1) stay -1.
int pot_target(const ControlEntry& e, const float* knobs);

// Debounced press edges, one bit per key in KeyMap order (bit i = key i, the
// order of KeyState.pressed); the caller passes `pressed & ~last_pressed`. A set
// bit toggles REC_A / REC_B / MODBTN between 0 and 1; Shift and bits past
// map.count do nothing. Returns true if MODBTN changed.
bool apply_key_edges(uint8_t edges, const KeyMap& map, float* knobs);

// LightId -> bit in MuxScan's LED field, -1 when the lamp is not on the plate.
// Rev A: kRevaLedBit in generated_panel_map.h (panel-map.json "leds" order).
// The coupon has one lamp, LED_1 = bit 0 of its LED field, and it shows MODBTN_L.
inline constexpr int8_t kCouponLedBit[ffctl::NUM_LIGHTS] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,   // GATE_A_L .. FTIME_B_L
    -1, -1, -1, -1, -1,                       // FLOW_A_L .. RST_L
    0,                                        // MODBTN_L
    -1,                                       // SHIFTBTN_L
};
static_assert(sizeof(kCouponLedBit) / sizeof(kCouponLedBit[0]) == ffctl::NUM_LIGHTS &&
                  kCouponLedBit[ffctl::MODBTN_L] == 0,
              "kCouponLedBit: one entry per LightId, MODBTN_L on bit 0");

} // namespace shell
