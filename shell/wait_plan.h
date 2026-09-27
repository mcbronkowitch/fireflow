#pragma once

// The wait-sweep probe's grid, its arms and the one piece of arithmetic in it
// that can go silently wrong: turning a wait of up to 50 ms into DWT cycles.
// Data and pure arithmetic with no hardware type in it, for the same reason as
// xtalk_plan.h and tone_plan.h -- so the host test suite can hold it.
//
// It defines NO victim table: the victims are round one's five, used directly
// from xtalk_plan.h, exactly as round two uses them.
//
// Spec: ../docs/superpowers/specs/2026-09-27-coupon-wait-sweep-probe-design.md
#include <cstdint>

#include "settle_plan.h"
#include "xtalk_plan.h"

namespace shell {

// Which arm a case belongs to. Printed as arm=%d on SHELL_WAIT_CASE, so the
// numbering is part of the output format and may not be reordered. Spec
// section 3.
enum class WaitArm : uint8_t
{
    Wait    = 0,   // A: prime, idle W, measure -- codec stopped, working rung
    Discard = 1,   // B: prime, idle W, ONE discarded conversion, measure
    Long    = 2,   // L: as A, at the 387.5-cycle rung
    Codec   = 3,   // C: as A, codec running with the callback writing zeros,
                   //    on kWaitCodecGridUs only -- the bridge to round two
};
inline constexpr int kWaitArmCount = 4;

// The wait axis, in microseconds, from the END of the prime conversion to the
// START of the next one. Roughly three points per decade; round two's three
// cadences (200 us, 1 ms, 10 ms -- one period of 5 kHz, 1 kHz and 100 Hz) are
// ON the grid, so arm A meets them without interpolation. 0 is back to back
// and is every arm's own reference: shift(W) = mean(W) - mean(0).
//
// The top, 50 ms, is flagged in the spec: round two never waited longer than
// 10 ms, and if arm A has not saturated by 50 ms the next round extends the
// grid rather than this one stretching.
inline constexpr int kWaitPoints = 15;
inline constexpr uint32_t kWaitGridUs[kWaitPoints] = {
    0,    2,    5,    10,    20,    50,    100,  200,
    500,  1000, 2000, 5000, 10000, 20000, 50000,
};

// Arm C's grid: round two's three cadences and nothing else. Each must be a
// point of kWaitGridUs -- the host test pins that -- so arm C and arm A are
// compared at identical commanded waits.
inline constexpr int kWaitCodecPoints = 3;
inline constexpr uint32_t kWaitCodecGridUs[kWaitCodecPoints] = {200, 1000, 10000};

// Arm C's point k as an index into kWaitGridUs, or -1 if it is not on the
// grid. Constexpr so the host test can call it; the firmware never needs it.
constexpr int wait_codec_grid_index(int k)
{
    if(k < 0 || k >= kWaitCodecPoints) return -1;
    for(int i = 0; i < kWaitPoints; ++i)
        if(kWaitGridUs[i] == kWaitCodecGridUs[k]) return i;
    return -1;
}

inline constexpr int kWaitRepeats = kRepeats;

// The 387.5-cycle rung, index 6 of kSamplingLadderTenths -- the rung
// settle-measured.md section 7 measured landing on the divider's true value,
// and the one tone_plan.h's kToneWinRung names for the same reason.
inline constexpr int kWaitLongRung = 6;

// Microseconds to DWT core cycles at kCoreMhz, in 64-bit arithmetic.
//
// NOT ns_to_cycles(us * 1000). cycles.h's ns_to_cycles() computes ns * 480 in
// 32 bits, which wraps above 2^32 / 480 = 8 947 848 ns -- so every wait from
// 10 ms up would come back as a fraction of itself, silently, and the curve's
// whole top decade would be measured at the wrong W. The host test proves the
// wrap is real by routing 50 ms through ns_to_cycles() and watching it fail.
//
// The result fits 32 bits for every grid point: 50 000 us x 480 = 24 000 000.
// A DWT difference taken as uint32_t is exact up to 2^32 cycles, ~8.9 s.
//
// kWaitCoreMhz is cycles.h's kCoreMhz carried as a literal, because cycles.h
// includes daisy_seed.h and cannot be compiled on the host. wait_probe.cpp
// static_asserts the two equal, so the copy cannot drift in the firmware.
inline constexpr uint32_t kWaitCoreMhz = 480;
constexpr uint32_t us_to_cycles(uint32_t us)
{
    return static_cast<uint32_t>(static_cast<uint64_t>(us) * kWaitCoreMhz);
}

// The grid index walked at step k of n when the block sweeps ascending
// (sweep_dir 0) or descending (sweep_dir 1). Spec section 7: the direction
// alternates every block so that a drift across one sweep cannot pose as a
// slow tail -- settle-measured.md section 6's reason, reused.
constexpr int wait_grid_order(int k, int n, int sweep_dir)
{
    return sweep_dir == 0 ? k : (n - 1 - k);
}

// The spec section 6 ESTIMATE of one block's measuring time, in ms. Not a
// measurement -- SHELL_WAIT_HEALTH's block_ms is that -- but a bound the host
// test holds, so a grid edit that triples the block does not go unnoticed.
//
// Per-conversion costs are deliberately generous upper bounds, not the
// measured clock's figures: 5 us at a working rung (the worst working rung is
// 16.5 + 8.5 = 25 ADC cycles, 4.07 us at the measured 6.146 MHz, plus the
// start overhead) and 66 us at 387.5 cycles (396 ADC cycles, 64.4 us).
inline constexpr uint32_t kWaitConvUsWorking = 5;
inline constexpr uint32_t kWaitConvUsLong    = 66;
uint32_t wait_block_estimate_ms();

} // namespace shell
