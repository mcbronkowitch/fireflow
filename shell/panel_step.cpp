// The playing image's per-block glue: see panel_step.h.
#include "panel_step.h"

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

void flux_channels(const ControlTable& t, const ChainProfile& p, int out[2])
{
    out[0] = out[1] = -1;
    for(int r = 0; r < t.count; ++r)
    {
        const ControlEntry& e = t.entries[r];
        const int d = e.param == ffctl::FLUX_A ? 0 : e.param == ffctl::FLUX_B ? 1 : -1;
        if(d < 0) continue;
        const int step = step_of(p, e.group, e.ch);
        if(step < 0) continue;
        for(int s = 0; s < p.sense_pins; ++s)
            if(group_at(p, step, s) == e.group && channel_at(p, step, s) == e.ch)
                out[d] = mux_channel(p, step, s);
    }
}

} // namespace shell
