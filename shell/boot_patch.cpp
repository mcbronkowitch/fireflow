#include "boot_patch.h"

#include "generated_boot_patch_noon.h"
#include "generated_panel_map.h"

namespace shell {

static_assert(kBootPatchNoonRowCount == kRevaTable.count,
              "generated_boot_patch_noon.h: one value per Rev A row (run gen_boot_patch_noon.py)");

int apply_boot_patch_rows(const ControlTable& t, const int* rows_x1000, int n, float* knobs)
{
    if(n != t.count) return -1;
    int written = 0;
    for(int r = 0; r < n; ++r)
    {
        const ControlEntry& e = t.entries[r];
        if(e.param < 0) continue;
        const int target = knob_target(e, knobs);
        knobs[target]    = knob_from_pot(target, static_cast<float>(rows_x1000[r]) * 0.001f);
        ++written;
    }
    return written;
}

} // namespace shell
