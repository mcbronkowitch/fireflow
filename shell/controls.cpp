#include "controls.h"

namespace shell {

const ControlEntry* find_control(const ControlTable& t, int group, int ch)
{
    for(int i = 0; i < t.count; ++i)
        if(t.entries[i].group == group && t.entries[i].ch == ch)
            return &t.entries[i];
    return nullptr;
}

float control_value(int param, float v)
{
    if(param < 0 || param >= spky::P_COUNT) return 0.0f;
    const spky::ParamInfo& pi = spky::kParams[param];
    return pi.lo + v * (pi.hi - pi.lo);
}

bool apply_control(const ControlEntry& e, float v, spky::Instrument& inst)
{
    if(e.param < 0 || e.param >= spky::P_COUNT) return false;
    // No clamp here: v arrives clamped from scan_value's span_normalize(),
    // and apply_param() clamps to the table range once more.
    spky::apply_param(inst, e.param, control_value(e.param, v));
    return true;
}

} // namespace shell
