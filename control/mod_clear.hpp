#pragma once
// SHIFT + MOD: clear what the MOD latch added (Bastian, 2026-10-28). Shared by
// the firmware's key handler (shell/controls.cpp) and VCV's context menu
// (host/vcv/src/Fireflow.cpp), which has no SHIFT key.
//
// Every depth twin in kModLayer drops to 0 -- the factory extras (SUB on deck
// B, DETUNE, MORPH, REV_DIFF) included -- EXCEPT the per-deck texture depths
// TIMB/DPTH/FILT (MODK_TDEPTH). Those are how the super modulators reach the
// texture at all; they return to their init-patch positions, so the super
// modulators keep running exactly as after boot, even if they had been turned.
#include "params.hpp"

namespace ffctl {

inline float mod_clear_value(const ModTarget& t) {
    return t.kind == MODK_TDEPTH ? kInitParamDefaults[t.depthId] : 0.f;
}

inline void clear_mod_layer(float* knobs) {
    for (const ModTarget& t : kModLayer) knobs[t.depthId] = mod_clear_value(t);
}

} // namespace ffctl
