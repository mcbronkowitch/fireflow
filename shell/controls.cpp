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

int knob_target(const ControlEntry& e, const float* knobs)
{
    if(e.alt < 0) return e.param;
    const int engine = e.param < ffctl::PART_STRIDE ? ffctl::ENGINE_A : ffctl::ENGINE_B;
    return static_cast<int>(std::round(knobs[engine])) == kEngineSlotBbd ? e.alt : e.param;
}

int pot_target(const ControlEntry& e, const float* knobs)
{
    const int target = knob_target(e, knobs);
    if(e.param < 0 || knobs[ffctl::MODBTN] < 0.5f) return target;
    for(const ffctl::ModTarget& m : ffctl::kModLayer)
        if(m.soundId == e.param)
            return target == e.param ? m.depthId : -1;
    return target;
}

bool apply_key_edges(uint8_t edges, const KeyMap& map, float* knobs)
{
    bool mod_changed = false;
    for(int i = 0; i < map.count && i < kMaxKeys; ++i)
    {
        if(!(edges & (1u << i))) continue;
        int id;
        switch(map.fn[i])
        {
            case KeyFn::RecA: id = ffctl::REC_A; break;
            case KeyFn::RecB: id = ffctl::REC_B; break;
            case KeyFn::Mod:  id = ffctl::MODBTN; mod_changed = true; break;
            default: continue;   // Shift: reserved, no function
        }
        knobs[id] = knobs[id] < 0.5f ? 1.0f : 0.0f;
    }
    return mod_changed;
}

} // namespace shell
