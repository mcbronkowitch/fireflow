#include "controls.h"

#include <cmath>

namespace shell {

const ControlEntry* find_control(const ControlTable& t, int group, int ch)
{
    for(int i = 0; i < t.count; ++i)
        if(t.entries[i].group == group && t.entries[i].ch == ch)
            return &t.entries[i];
    return nullptr;
}

float knob_from_pot(int param, float v)
{
    if(param < 0 || param >= ffctl::NUM_PARAMS) return 0.0f;
    // No clamp here: v arrives clamped from scan_value's span_normalize().
    const ffctl::ParamRange& r = ffctl::kParamRange[param];
    const float x = r.lo + v * (r.hi - r.lo);
    return r.snap ? std::round(x) : x;
}

} // namespace shell
