#include "mux_plan.h"

namespace shell {

namespace {
constexpr uint8_t all_off(const ChainProfile& p)
{
    return static_cast<uint8_t>((1u << p.groups) - 1u);
}
}

int group_of_step(const ChainProfile& p, int step)
{
    if(step < 0 || step >= scan_steps(p)) return -1;
    int rest = step;
    for(int g = 0; g < p.groups; ++g)
    {
        if(rest < p.channels[g]) return g;
        rest -= p.channels[g];
    }
    return -1;
}

int step_of(const ChainProfile& p, int group, int ch)
{
    if(group < 0 || group >= p.groups) return -1;
    if(ch < 0 || ch >= p.channels[group]) return -1;
    int step = ch;
    for(int g = 0; g < group; ++g) step += p.channels[g];
    return step;
}

StepPattern step_pattern(const ChainProfile& p, int step)
{
    // A step that does not exist parks the scan with every enable off. An
    // out-of-range address would still select SOME channel and hand back a
    // foreign knob's voltage, which is worse than reading nothing.
    const int g = group_of_step(p, step);
    if(g < 0) return StepPattern{0, all_off(p)};

    int addr = step;
    for(int i = 0; i < g; ++i) addr -= p.channels[i];
    return StepPattern{static_cast<uint8_t>(addr),
                       static_cast<uint8_t>(all_off(p) & ~(1u << g))};
}

int mux_channel(const ChainProfile& p, int step, int sense)
{
    if(step < 0 || step >= scan_steps(p)) return -1;
    if(sense < 0 || sense >= p.sense_pins) return -1;
    return step * p.sense_pins + sense;
}

uint32_t chain_word(const ChainProfile& p, StepPattern s, uint32_t leds)
{
    const uint32_t led_mask = (1u << p.led_bits) - 1u;
    return (static_cast<uint32_t>(s.address & 0x0Fu) << p.addr_shift)
           | (static_cast<uint32_t>(s.enable_mask & all_off(p))
              << p.enable_shift)
           | ((leds & led_mask) << p.led_shift);
}

int button_bit(const ChainProfile& p) { return p.button_bit; }

} // namespace shell
