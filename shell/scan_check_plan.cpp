#include "scan_check_plan.h"

namespace shell {

CheckSlot check_slot(int block)
{
    if(block < 0 || block >= kRunBlocks) return {ScanArm::S, -1, -1, false};

    if(block < kArmSBlocks)
    {
        if(block == 0) return {ScanArm::S, 0, -1, false};
        const int read   = (block - 1) % kCheckSteps;
        const int select = (block < kArmSBlocks - 1) ? block % kCheckSteps : -1;
        return {ScanArm::S, select, read, false};
    }

    int q = block - kArmSBlocks;
    if(q < kArmPBlocks)
    {
        const int step = q / kParkHold;
        const int k    = q % kParkHold;
        if(k == 0) return {ScanArm::P, step, -1, false};
        return {ScanArm::P, -1, (k >= kParkFirstRead) ? step : -1, false};
    }

    q -= kArmPBlocks;
    const int step = q % kCheckSteps;
    return {ScanArm::Zero, step, step, true};
}

char arm_letter(ScanArm a)
{
    switch(a)
    {
        case ScanArm::S: return 'S';
        case ScanArm::P: return 'P';
        case ScanArm::Zero: return '0';
    }
    return '?';
}

} // namespace shell
