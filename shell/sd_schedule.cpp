#include "sd_schedule.h"

namespace shell {

BurstPhase burst_phase(uint32_t w)
{
    const uint32_t phase = (w / 10) % 12;
    if (phase % 2 == 0) return BurstPhase{false, 0, 0};
    // phases 1,3,5 -> 83 ms; 7,9,11 -> 41 ms; tail 0,1,2 within each half
    const uint16_t period = (phase < 6) ? 83 : 41;
    const uint8_t  tail   = static_cast<uint8_t>((phase % 6) / 2);
    return BurstPhase{true, period, tail};
}

bool SdHold::step(bool transfer_seen, uint8_t tail)
{
    if (transfer_seen) {
        left = tail;
        return true;
    }
    if (left > 0) {
        --left;
        return true;
    }
    return false;
}

uint8_t rgrain_n(uint32_t frame)
{
    static constexpr uint8_t kSeq[8] = {0, 8, 2, 4, 4, 2, 8, 0};
    return kSeq[(frame / 20) % 8];
}

bool rgrain_cap_on(uint32_t frame)
{
    return (frame / 60) % 2 == 1;
}

uint8_t rgrain_grant(uint32_t last_cb, uint8_t last_granted, bool last_silenced,
                     uint32_t budget, uint32_t per_grain, uint8_t max)
{
    // uint64_t: last_granted * per_grain must not wrap before the clamp
    const uint64_t layer = static_cast<uint64_t>(last_granted) * per_grain;
    const uint32_t base  = (last_cb > layer) ? static_cast<uint32_t>(last_cb - layer) : 0u;
    if (last_silenced || base >= budget || per_grain == 0) return 0;
    const uint32_t n = (budget - base) / per_grain;
    return static_cast<uint8_t>(n < max ? n : max);
}

} // namespace shell
