#include "wait_plan.h"

namespace shell {

// Spec section 6. Arms A and B at the working rung (B takes one conversion
// more per repeat), arm L at the long rung, arm C at the working rung on its
// three points only -- each summed over five victims and kWaitRepeats
// repeats. Integer microseconds throughout, divided once at the end.
uint32_t wait_block_estimate_ms()
{
    uint64_t grid_us = 0;
    for(int i = 0; i < kWaitPoints; ++i) grid_us += kWaitGridUs[i];
    uint64_t codec_us = 0;
    for(int i = 0; i < kWaitCodecPoints; ++i) codec_us += kWaitCodecGridUs[i];

    const uint64_t per_victim_us
        = kWaitRepeats
          * ((grid_us + kWaitPoints * 2u * kWaitConvUsWorking)          // A
             + (grid_us + kWaitPoints * 3u * kWaitConvUsWorking)        // B
             + (grid_us + kWaitPoints * 2u * kWaitConvUsLong)           // L
             + (codec_us + kWaitCodecPoints * 2u * kWaitConvUsWorking)); // C

    return static_cast<uint32_t>(per_victim_us * kXtalkVictims / 1000u);
}

} // namespace shell
