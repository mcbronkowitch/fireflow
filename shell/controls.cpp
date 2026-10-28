#include "controls.h"

#include <cmath>

#include "control/mod_clear.hpp"

namespace shell {

namespace {

// kModLayer inverted once, at compile time: depth[param] is the depthId of
// the first kModLayer row whose soundId is param, -1 for none. pot_target()
// runs for every pot reading inside the audio interrupt; with MOD latched it
// used to walk all kModLayer rows per reading.
struct DepthTable
{
    int16_t depth[ffctl::NUM_PARAMS];
};

constexpr DepthTable make_depth_table()
{
    DepthTable t{};
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) t.depth[i] = -1;
    for(const ffctl::ModTarget& m : ffctl::kModLayer)
        if(m.soundId >= 0 && m.soundId < ffctl::NUM_PARAMS && t.depth[m.soundId] < 0)
            t.depth[m.soundId] = static_cast<int16_t>(m.depthId);
    return t;
}

static_assert(ffctl::NUM_PARAMS <= 32767, "DepthTable stores ids as int16_t");
constexpr DepthTable kDepth = make_depth_table();

} // namespace

int mod_depth_of(int param)
{
    if(param < 0 || param >= ffctl::NUM_PARAMS) return -1;
    return kDepth.depth[param];
}

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
    const int depth = mod_depth_of(e.param);
    if(depth < 0) return target;
    return target == e.param ? depth : -1;
}

KeyResult apply_key_edges(uint8_t edges, uint8_t held, const KeyMap& map, float* knobs)
{
    bool shift = false;
    for(int i = 0; i < map.count && i < kMaxKeys; ++i)
        if(map.fn[i] == KeyFn::Shift && (held & (1u << i))) shift = true;

    KeyResult r;
    for(int i = 0; i < map.count && i < kMaxKeys; ++i)
    {
        if(!(edges & (1u << i))) continue;
        int id;
        switch(map.fn[i])
        {
            case KeyFn::RecA: id = ffctl::REC_A; break;
            case KeyFn::RecB: id = ffctl::REC_B; break;
            case KeyFn::Mod:
                if(shift)
                {
                    ffctl::clear_mod_layer(knobs);
                    r.mod_cleared = true;
                    continue;
                }
                id = ffctl::MODBTN; r.mod_changed = true; break;
            default: continue;   // Shift: a modifier only
        }
        knobs[id] = knobs[id] < 0.5f ? 1.0f : 0.0f;
    }
    return r;
}

} // namespace shell
