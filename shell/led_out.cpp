// The lamp word: see led_out.h.
#include "led_out.h"

#include "control/params.hpp"

namespace shell {

uint32_t led_word(const int* duty, int steps, uint32_t frame, const int8_t* led_bit)
{
    // The period in blocks; steps 2 (or less) is plain on/off.
    const uint32_t m = steps > 1 ? static_cast<uint32_t>(steps - 1) : 1u;
    const uint32_t f = frame % m;
    uint32_t       w = 0u;
    for(int i = 0; i < ffctl::NUM_LIGHTS; ++i)
    {
        const int b = led_bit[i];
        if(b < 0) continue;
        const int d = duty[i];
        if(d <= 0) continue;
        const uint32_t ud = d >= static_cast<int>(m) ? m : static_cast<uint32_t>(d);
        // Bresenham: the d on-blocks of each period land as evenly as integer
        // blocks allow, never bunched at its start.
        if((f + 1u) * ud / m > f * ud / m) w |= 1u << b;
    }
    return w;
}

} // namespace shell
