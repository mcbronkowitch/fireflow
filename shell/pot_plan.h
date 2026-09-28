#pragma once

// The three pots round four measures at mid travel. Data only, no hardware
// type, so the host suite holds it (tests/test_pot_plan.cpp) -- the same
// reason as settle_plan.h and xtalk_plan.h.
//
// Every number here is DERIVED: the channels from
// hardware/coupon/scripts/design.py, the values from what is soldered on the
// board. R_track is NOMINAL and stays nominal -- the pot ends hang on
// A+3V3/AGND together with three dividers and R_BTN, so the track cannot be
// metered in circuit (spec section 3).
//
// shell/pot_round.py carries the host-side copy; test_pot_round.py parses
// this file and fails if the two drift apart. Keep one pot per line in the
// {"name", group, channel, hi, lo, r_track} shape that parser expects.
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
#include <cstdint>

namespace shell {

struct Pot
{
    const char* name;
    int         group;        // 0 = the 4067 on ADC_9, 1 = the 4051 on ADC_10
    int         channel;      // the wiper's mux channel
    int         hi_ch;        // neighbour tied to A+3V3 through 0 ohm
    int         lo_ch;        // neighbour tied to AGND through 0 ohm
    uint32_t    r_track_ohm;  // nominal track resistance
};

// The switch on-resistance every source-impedance table on this board adds.
inline constexpr uint32_t kPotRonOhm = 150;

// A linear pot at 50/50 is two R/2 halves in parallel -- R/4 -- the peak of
// its wiper's source impedance.
constexpr uint32_t pot_r_src_mid(uint32_t r_track_ohm)
{
    return r_track_ohm / 4u + kPotRonOhm;
}

inline constexpr int kPotCount = 3;
inline constexpr Pot kPots[kPotCount] = {
    {"RV2", 0, 2, 1, 3, 10000},
    {"RV4", 0, 6, 5, 7, 20000},
    {"RV6", 1, 2, 1, 3, 10000},
};

} // namespace shell
