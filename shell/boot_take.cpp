// The boot take: see boot_take.h.
#include "boot_take.h"

#include "generated_panel_map.h"

namespace shell {

static_assert(kMaxRows >= kRevaTable.count, "BootTake: kMaxRows below Rev A's row count");

void boot_take_collect(BootTake& b, int row, uint16_t raw)
{
    if(b.done || row < 0 || row >= kMaxRows) return;
    b.raw[row]  = raw;
    b.seen[row] = true;
}

bool boot_take_ready(const BootTake& b, const ControlTable& t)
{
    if(t.count > kMaxRows) return false;
    for(int r = 0; r < t.count; ++r)
        if(!b.seen[r]) return false;
    return true;
}

namespace {

void write_row(const BootTake& b, const ControlTable& t, int r, const Span& span, float* knobs)
{
    const ControlEntry& e = t.entries[r];
    if(e.param < 0 || !b.seen[r]) return;
    PotFilter fresh{};
    float v = 0.0f;
    // The row's own band, as pot_row_step() uses it: the snap regions match.
    if(!pot_filter(fresh, b.raw[r], span, pot_hysteresis(e.sense), &v)) return;   // invalid span
    // knob_target(), not pot_target(): MODBTN counts as 0 at boot.
    const int target = knob_target(e, knobs);
    if(target >= 0) knobs[target] = knob_from_pot(target, v);
}

bool is_engine(int param) { return param == ffctl::ENGINE_A || param == ffctl::ENGINE_B; }

} // namespace

void boot_take_apply(BootTake& b, const ControlTable& t, const Span& span, float* knobs)
{
    if(b.done || !span.valid || t.count > kMaxRows) return;
    for(int r = 0; r < t.count; ++r)
        if(is_engine(t.entries[r].param)) write_row(b, t, r, span, knobs);
    for(int r = 0; r < t.count; ++r)
        if(!is_engine(t.entries[r].param)) write_row(b, t, r, span, knobs);
    b.done = true;
}

void boot_gate_apply(BootGate& g, bool take_done, float* l, float* r, size_t n)
{
    if(g.faded >= kBootFadeSamples) return;
    if(!take_done)
    {
        for(size_t i = 0; i < n; ++i) l[i] = r[i] = 0.0f;
        return;
    }
    for(size_t i = 0; i < n; ++i)
    {
        const float k = g.faded < kBootFadeSamples
                            ? static_cast<float>(g.faded) / static_cast<float>(kBootFadeSamples)
                            : 1.0f;
        l[i] *= k;
        r[i] *= k;
        if(g.faded < kBootFadeSamples) ++g.faded;
    }
}

} // namespace shell
