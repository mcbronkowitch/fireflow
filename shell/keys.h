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
//
// closed_at_rest flips the polarity: a released key reads low, a pressed key
// high. That is Rev A as assembled (docs/hardware/reva-bring-up.md 6.1 and
// the fault list: the keys sit on their normally closed contacts), reached
// through SHELL_KEYS_NC=1. The design is active low, and so is every table.
struct KeyPad
{
    int  count;
    int  bit[kMaxKeys];
    bool closed_at_rest = false;
};

inline constexpr KeyPad closed_at_rest(KeyPad pad)
{
    pad.closed_at_rest = true;
    return pad;
}

// The return word that reads every key of `pad` released -- what
// MuxScan::last_return() must hold before the first read, or the boot counts
// one phantom press per key.
inline constexpr uint32_t keys_released(const KeyPad& pad)
{
    return pad.closed_at_rest ? 0u : 0xFFFFFFFFu;
}

// The coupon: SW1 on D0 (hardware/coupon/scripts/netlist.py), so bit 7 --
// what kCouponChain.button_bit has always said.
inline constexpr KeyPad kCouponKeys{1, {7}};

// What a key does on its debounced press (spec 2026-10-18-rev-a-p6b2 section 3).
// Shift is read and counted like the others but has no function yet.
enum class KeyFn : int8_t { RecA, RecB, Mod, Shift };

// Key i of a KeyPad is KeyMap.fn[i]; same index as KeyState.pressed's bit.
struct KeyMap
{
    int   count;
    KeyFn fn[kMaxKeys];
};

// The coupon has one key and it is MOD (spec section 7, "the key toggles MOD").
inline constexpr KeyMap kCouponKeyMap{1, {KeyFn::Mod}};

struct KeyState
{
    uint8_t  pressed           = 0;   // bit i = key i, debounced
    uint8_t  candidate         = 0;   // bit i = what key i read last
    uint8_t  same[kMaxKeys]    = {};  // equal reads of the candidate in a row
    uint16_t presses[kMaxKeys] = {};  // released -> pressed transitions
};

// One read of the 165's return stream. Keys are active low: a pressed key
// pulls its input to GND against the pull-up -- unless pad.closed_at_rest.
void key_update(KeyState& s, const KeyPad& pad, uint32_t ret);

} // namespace shell
