#pragma once

// The scan-check image's run block, as data (spec 2026-09-28-coupon-panel-
// scan-design.md section 4). One run block is three arms, 4729 audio blocks,
// ~9.46 s at 2 ms:
//
//   S  the shipping pattern. Block 0 selects step 0; every later block reads
//      the step selected one block earlier, then selects the next. 64 full
//      sweeps. The last block selects nothing.
//   P  parked. Per step: block 0 selects it, blocks 5..68 read it -- 64
//      reads, each at least four blocks after the address settled.
//   0  lag zero. Each block selects a step and reads it in the same
//      callback, so the read comes from the previous address. The control
//      that proves a clean S could have been dirty.
//
// No hardware type; tests/test_scan_check_plan.cpp holds it.
#include <cstdint>

namespace shell {

enum class ScanArm : uint8_t
{
    S    = 0,
    P    = 1,
    Zero = 2,
};

inline constexpr int kArmCount      = 3;
inline constexpr int kCheckSteps    = 24;   // the coupon: 16 + 8
inline constexpr int kCheckReads    = 64;
inline constexpr int kParkHold      = 69;
inline constexpr int kParkFirstRead = 5;
inline constexpr int kArmSBlocks    = 1 + kCheckSteps * kCheckReads;
inline constexpr int kArmPBlocks    = kCheckSteps * kParkHold;
inline constexpr int kArmZeroBlocks = kCheckSteps * kCheckReads;
inline constexpr int kRunBlocks     = kArmSBlocks + kArmPBlocks + kArmZeroBlocks;

static_assert(kParkHold - kParkFirstRead == kCheckReads,
              "arm P must read each step as often as the other arms");

struct CheckSlot
{
    ScanArm arm;
    int     select_step;        // step to select this block, -1 = none
    int     read_step;          // step to read this block, -1 = none
    bool    read_after_select;  // true only in arm 0
};

// The slot for block `block` of a run block; out of range selects and reads
// nothing.
CheckSlot check_slot(int block);

char arm_letter(ScanArm a);

} // namespace shell
