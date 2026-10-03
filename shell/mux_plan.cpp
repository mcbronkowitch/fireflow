#include "mux_plan.h"

namespace shell {

namespace {

constexpr uint16_t all_off(const ChainProfile& p)
{
    return static_cast<uint16_t>((1u << p.groups) - 1u);
}

// Parallel: the step group g's channel 0 is read on -- the channel count of
// every earlier group on the same sense pin.
int parallel_start(const ChainProfile& p, int g)
{
    int start = 0;
    for(int i = 0; i < g; ++i)
        if(p.sense_of_group[i] == p.sense_of_group[g]) start += p.channels[i];
    return start;
}

} // namespace

int group_of_step(const ChainProfile& p, int step)
{
    if(p.parallel_sense) return -1;
    if(step < 0 || step >= scan_steps(p)) return -1;
    int rest = step;
    for(int g = 0; g < p.groups; ++g)
    {
        if(rest < p.channels[g]) return g;
        rest -= p.channels[g];
    }
    return -1;
}

int group_at(const ChainProfile& p, int step, int sense)
{
    if(step < 0 || step >= scan_steps(p)) return -1;
    if(sense < 0 || sense >= p.sense_pins) return -1;
    if(!p.parallel_sense)
    {
        const int g     = group_of_step(p, step);
        const int wired = p.sense_of_group[g];
        return (wired < 0 || wired == sense) ? g : -1;
    }
    int rest = step;
    for(int g = 0; g < p.groups; ++g)
    {
        if(p.sense_of_group[g] != sense) continue;
        if(rest < p.channels[g]) return g;
        rest -= p.channels[g];
    }
    return -1;
}

int channel_at(const ChainProfile& p, int step, int sense)
{
    const int g = group_at(p, step, sense);
    if(g < 0) return -1;
    if(p.parallel_sense) return step - parallel_start(p, g);
    int ch = step;
    for(int i = 0; i < g; ++i) ch -= p.channels[i];
    return ch;
}

int step_of(const ChainProfile& p, int group, int ch)
{
    if(group < 0 || group >= p.groups) return -1;
    if(ch < 0 || ch >= p.channels[group]) return -1;
    if(p.parallel_sense) return parallel_start(p, group) + ch;
    int step = ch;
    for(int g = 0; g < group; ++g) step += p.channels[g];
    return step;
}

StepPattern step_pattern(const ChainProfile& p, int step)
{
    // A step that does not exist parks the scan with every enable off. An
    // out-of-range ADDRESS would still select some channel and hand back a
    // foreign knob's voltage, which is worse than reading nothing.
    if(step < 0 || step >= scan_steps(p)) return StepPattern{0, all_off(p)};

    if(!p.parallel_sense)
    {
        const int g    = group_of_step(p, step);
        int       addr = step;
        for(int i = 0; i < g; ++i) addr -= p.channels[i];
        return StepPattern{static_cast<uint8_t>(addr),
                           static_cast<uint16_t>(all_off(p) & ~(1u << g))};
    }

    uint16_t mask  = all_off(p);
    int      addr  = 0;
    bool     first = true;
    for(int s = 0; s < p.sense_pins; ++s)
    {
        const int g = group_at(p, step, s);
        if(g < 0) continue;
        mask = static_cast<uint16_t>(mask & ~(1u << g));
        if(first)
        {
            addr  = channel_at(p, step, s);
            first = false;
        }
    }
    return StepPattern{static_cast<uint8_t>(addr), mask};
}

int mux_channel(const ChainProfile& p, int step, int sense)
{
    if(step < 0 || step >= scan_steps(p)) return -1;
    if(sense < 0 || sense >= p.sense_pins) return -1;
    return step * p.sense_pins + sense;
}

bool sense_live(const ChainProfile& p, int step, int sense)
{
    return group_at(p, step, sense) >= 0;
}

uint64_t chain_word(const ChainProfile& p, StepPattern s, uint32_t leds)
{
    const uint64_t addr_mask = (uint64_t{1} << p.addr_bits) - 1u;
    const uint64_t led_mask  = (uint64_t{1} << p.led_bits) - 1u;
    return ((uint64_t{s.address} & addr_mask) << p.addr_shift)
           | (uint64_t{static_cast<uint16_t>(s.enable_mask & all_off(p))}
              << p.enable_shift)
           | ((uint64_t{leds} & led_mask) << p.led_shift);
}

int button_bit(const ChainProfile& p) { return p.button_bit; }

} // namespace shell
