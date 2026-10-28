#include "persist_policy.h"
#include <cmath>
#include <cstring>
#include "controls.h"

namespace shell {

namespace {
bool never_restored(int id)
{
    return id == ffctl::REC_A || id == ffctl::REC_B || id == ffctl::MODBTN;
}
}

bool persist_differs(const float* a, const float* b)
{
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i)
        if(!never_restored(i) && std::memcmp(&a[i], &b[i], sizeof(float)) != 0) return true;
    return false;
}

bool save_due(SavePolicy& s, bool changed, uint32_t now_ms)
{
    if(changed) { s.last_change_ms = now_ms; s.dirty = true; }
    if(!s.dirty) return false;
    if(now_ms - s.last_change_ms < kSaveQuietMs) return false;
    return !s.saved_once || now_ms - s.last_save_ms >= kSaveFloorMs;
}

void save_done(SavePolicy& s, uint32_t now_ms)
{
    s.last_save_ms = now_ms;
    s.saved_once   = true;
    s.dirty        = false;
}

void save_failed(SavePolicy& s, uint32_t now_ms)
{
    save_done(s, now_ms);
    s.dirty = true;
}

void boot_overlay(const PersistRecord* rec, float* knobs, bool excite_out[2])
{
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i)
        knobs[i] = (rec && !never_restored(i)) ? rec->knobs[i] : ffctl::kInitParamDefaults[i];
    excite_out[0] = rec && rec->excite[0];
    excite_out[1] = rec && rec->excite[1];
}

void excite_after_take(const bool rec_excite[2], const bool was_bbd[2], const float* knobs,
                       bool excite_out[2])
{
    const int eng[2] = {ffctl::ENGINE_A, ffctl::ENGINE_B};
    for(int p = 0; p < 2; ++p)
    {
        const bool on_bbd = static_cast<int>(std::round(knobs[eng[p]])) == kEngineSlotBbd;
        excite_out[p] = on_bbd && (was_bbd[p] ? rec_excite[p] : true);
    }
}

} // namespace shell
