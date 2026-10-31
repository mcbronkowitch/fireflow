#pragma once

// SD probe schedules, the pot hold and the grain grant (SD use-case tests,
// spec section 5). The probe images drive these once per window, block or
// frame; the logic is pure data, no hardware type, host tested
// (tests/test_sd_schedule.cpp).
//
// Spec: ../docs/superpowers/specs/2026-10-31-sd-use-cases-tests-design.md
#include <cstdint>

namespace shell {

// One burst phase of the SD transfer schedule.
struct BurstPhase
{
    bool     on;          // false: idle, no transfers
    uint16_t period_ms;   // one 32 KB read per period (83 ms ~ 395 KB/s, 41 ms ~ 799 KB/s)
    uint8_t  tail;        // blocks after a transfer whose pot readings are dropped
};

// Raw window w -> phase (w / 10) % 12. Even phases idle; odd phases 1,3,5 run
// at 83 ms with tail 0,1,2; 7,9,11 at 41 ms with tail 0,1,2.
BurstPhase burst_phase(uint32_t w);

// Pot-reading hold. Call once per block; true means this block's pot readings
// are dropped. A block that saw a transfer drops and sets left = tail (a
// transfer inside a countdown restarts it); otherwise a block with left > 0
// drops and counts down.
struct SdHold
{
    uint8_t left = 0;
    bool step(bool transfer_seen, uint8_t tail);
};

// B1's grain count N per 500 ms frame: phases of 20 frames, the sequence
// {0, 8, 2, 4, 4, 2, 8, 0} repeating.
uint8_t rgrain_n(uint32_t frame);

// B2's cap switch: phases of 60 frames, off first, then alternating.
bool rgrain_cap_on(uint32_t frame);

// Grains granted for the next block. Works from the previous block's cost
// without the layer (last_cb - last_granted * per_grain, clamped at 0), so the
// grant does not feed back on itself. 0 if last_silenced or the base already
// reaches the budget, else min(max, (budget - base) / per_grain).
uint8_t rgrain_grant(uint32_t last_cb, uint8_t last_granted, bool last_silenced,
                     uint32_t budget, uint32_t per_grain, uint8_t max);

} // namespace shell
