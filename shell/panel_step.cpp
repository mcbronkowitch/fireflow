// The playing image's per-block glue: see panel_step.h.
#include "panel_step.h"

#include "control/clock_law.hpp"
#include "control/control_law.h"

namespace shell {

bool pot_row_step(const ControlEntry& e, PotOwner& owner, PotFilter& filter,
                  uint16_t raw, const Span& span, float* knobs, float* v_out)
{
    const int target = pot_target(e, knobs);
    const Own own    = pot_own(owner, raw, target, span.valid, kPotTouch);
    // The jump: on the move that takes the target, the filter emits this
    // reading, the pot's current position, whatever it emitted before.
    if(own == Own::Took) filter.emitted = false;
    if(!pot_filter(filter, raw, span, kPotHysteresis, v_out)) return false;
    if(own != Own::Idle && target >= 0) knobs[target] = knob_from_pot(target, *v_out);
    return true;
}

void apply_law_events(const control::Events& ev, float* knobs, PotOwner* owners,
                      const int flux_idx[2])
{
    for(int p = 0; p < 2; ++p)
    {
        if(!ev.bbd_edge[p]) continue;
        knobs[p ? ffctl::FLUX_B : ffctl::FLUX_A] = 0.0f;
        if(flux_idx[p] >= 0) pot_release(owners[flux_idx[p]]);
    }
}

namespace {

// The mux channel of the row that sends `param`, -1 if none: found the way
// panel_scan_tick() walks a step (group_at / channel_at per sense pin).
int param_channel(const ControlTable& t, const ChainProfile& p, int param)
{
    int out = -1;
    for(int r = 0; r < t.count; ++r)
    {
        const ControlEntry& e = t.entries[r];
        if(e.param != param) continue;
        const int step = step_of(p, e.group, e.ch);
        if(step < 0) continue;
        for(int s = 0; s < p.sense_pins; ++s)
            if(group_at(p, step, s) == e.group && channel_at(p, step, s) == e.ch)
                out = mux_channel(p, step, s);
    }
    return out;
}

} // namespace

void flux_channels(const ControlTable& t, const ChainProfile& p, int out[2])
{
    out[0] = param_channel(t, p, ffctl::FLUX_A);
    out[1] = param_channel(t, p, ffctl::FLUX_B);
}

int tempo_channel(const ControlTable& t, const ChainProfile& p)
{
    return param_channel(t, p, ffctl::TEMPO);
}

void apply_clock_events(const control::ClockOut& o, PotOwner* owners, int tempo_idx)
{
    if(o.lost && tempo_idx >= 0) pot_release(owners[tempo_idx]);
}

} // namespace shell
