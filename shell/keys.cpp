#include "keys.h"

namespace shell {

void key_update(KeyState& s, const KeyPad& pad, uint32_t ret)
{
    for(int i = 0; i < pad.count && i < kMaxKeys; ++i)
    {
        const uint8_t me   = static_cast<uint8_t>(1u << i);
        const bool    raw  = ((ret >> pad.bit[i]) & 1u) == 0u;
        const bool    cand = (s.candidate & me) != 0u;
        if(raw == cand)
        {
            if(s.same[i] < kKeyDebounce) ++s.same[i];
        }
        else
        {
            s.candidate = static_cast<uint8_t>(raw ? (s.candidate | me)
                                                   : (s.candidate & ~me));
            s.same[i]   = 1;
        }
        const bool now = (s.pressed & me) != 0u;
        if(s.same[i] >= kKeyDebounce && now != raw)
        {
            s.pressed = static_cast<uint8_t>(raw ? (s.pressed | me)
                                                 : (s.pressed & ~me));
            if(raw) ++s.presses[i];
        }
    }
}

} // namespace shell
