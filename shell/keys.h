#pragma once

// The panel's keys, read through the 74HC165 in the same pass as every scan
// step (spec 2026-10-02-rev-a-p6a-panel-scan-design.md section 3.4). No
// hardware type: tests/test_keys.cpp holds the debounce on the host.
#include <cstdint>

namespace shell {

inline constexpr int kMaxKeys = 4;

// A key changes state after this many equal reads in a row. One read per
// 2 ms block, so 6 ms.
inline constexpr int kKeyDebounce = 3;

// Where each key sits in the 165's return stream, counted from the first bit
// shifted out (MuxScan::read_chain). The first bit is D7, so a key on Dn is
// bit 7 - n.
struct KeyPad
{
    int count;
    int bit[kMaxKeys];
};

// The coupon: SW1 on D0 (hardware/coupon/scripts/netlist.py), so bit 7 --
// what kCouponChain.button_bit has always said.
inline constexpr KeyPad kCouponKeys{1, {7}};

struct KeyState
{
    uint8_t  pressed           = 0;   // bit i = key i, debounced
    uint8_t  candidate         = 0;   // bit i = what key i read last
    uint8_t  same[kMaxKeys]    = {};  // equal reads of the candidate in a row
    uint16_t presses[kMaxKeys] = {};  // released -> pressed transitions
};

// One read of the 165's return stream. Keys are active low: a pressed key
// pulls its input to GND against the pull-up.
void key_update(KeyState& s, const KeyPad& pad, uint32_t ret);

} // namespace shell
