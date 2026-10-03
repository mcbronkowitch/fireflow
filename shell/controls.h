#pragma once

// Mux channel -> engine parameter, per board, as data. No hardware type:
// tests/test_controls_map.cpp holds it on the host. A knob that does the
// wrong thing is only audible on a board and expensive to find; here it is
// one row.
//
// The coupon's table maps its three measured pots (pot_plan.h), Bastian's
// choice of 2026-09-28.
// Rev A's table is generated: shell/generated_panel_map.h, from
// hardware/reva/panel-map.json (spec 2026-10-02-rev-a-p6a-panel-scan-design.md).
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
#include "instrument.h"
#include "param_table.h"

namespace shell {

struct ControlEntry
{
    int group;       // mux group (chip) as in mux_plan.h; on Rev A the global mux 0..9
    int ch;          // channel on that chip
    int param;       // spky::ParamId, or -1: scanned and reported, sent nowhere
    int sense = -1;  // the sense pin the schematic wires the group to, -1 = not recorded
};

struct ControlTable
{
    const ControlEntry* entries;
    int                 count;
};

inline constexpr ControlEntry kCouponControls[] = {
    {0, 2, spky::P_RATE_A},      // RV2, 10 k, on the 4067
    {0, 6, spky::P_DENSITY_A},   // RV4, 20 k, on the 4067
    {1, 2, spky::P_FILT_A},      // RV6, 10 k, on the 4051
};

inline constexpr ControlTable kCouponTable{
    kCouponControls,
    static_cast<int>(sizeof(kCouponControls) / sizeof(kCouponControls[0]))};

// The entry for (group, ch), or nullptr. A channel that is not in the table
// changes nothing: a half-seated chip produces indices nobody planned, and
// guessing would hand a foreign knob's voltage to a parameter.
const ControlEntry* find_control(const ControlTable& t, int group, int ch);

// lo + v * (hi - lo) of the parameter's range in param_table.h, or 0 for a
// parameter that does not exist.
float control_value(int param, float v);

// Scales v into the entry's parameter range and routes it via apply_param().
// Returns false, and touches nothing, for an entry without a parameter.
bool apply_control(const ControlEntry& e, float v, spky::Instrument& inst);

} // namespace shell
