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
    if(e.param < 0) return;
    PotFilter fresh{};
    float v = 0.0f;
    if(!pot_filter(fresh, b.raw[r], span, kPotHysteresis, &v)) return;   // invalid span
    // knob_target(), not pot_target(): MODBTN counts as 0 at boot.
    const int target = knob_target(e, knobs);
    if(target >= 0) knobs[target] = knob_from_pot(target, v);
}

bool is_engine(int param) { return param == ffctl::ENGINE_A || param == ffctl::ENGINE_B; }

} // namespace

void boot_take_apply(BootTake& b, const ControlTable& t, const Span& span, float* knobs)
{
    if(b.done) return;
    for(int r = 0; r < t.count; ++r)
        if(is_engine(t.entries[r].param)) write_row(b, t, r, span, knobs);
    for(int r = 0; r < t.count; ++r)
        if(!is_engine(t.entries[r].param)) write_row(b, t, r, span, knobs);
    b.done = true;
}

} // namespace shell
