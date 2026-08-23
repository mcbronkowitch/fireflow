#include "mux_plan.h"

namespace shell {

namespace {
constexpr uint8_t kAllOff = static_cast<uint8_t>((1u << kMuxGroups) - 1u);
}

StepPattern step_pattern(int step)
{
    // A step that does not exist parks the scan with every enable off. An
    // out-of-range address would still select SOME channel and hand back a
    // foreign knob's voltage, which is worse than reading nothing.
    if(step < 0 || step >= kScanSteps) return StepPattern{0, kAllOff};

    const int group = step / kMuxChannels;
    const int addr  = step % kMuxChannels;
    return StepPattern{static_cast<uint8_t>(addr),
                       static_cast<uint8_t>(kAllOff & ~(1u << group))};
}

int mux_channel(int step, int sense)
{
    if(step < 0 || step >= kScanSteps) return -1;
    if(sense < 0 || sense >= kSensePins) return -1;
    return step * kSensePins + sense;
}

uint32_t chain_word(StepPattern p, uint32_t leds)
{
    return (static_cast<uint32_t>(p.address & 0x0Fu) << kAddrShift)
           | (static_cast<uint32_t>(p.enable_mask & kAllOff) << kEnableShift)
           | (leds << kLedShift);
}

} // namespace shell
