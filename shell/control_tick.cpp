#include "control_tick.h"

#include "shell_panel_scan.h"

// Only the playing images run the law. Elsewhere this file is empty, so the
// law's static constructor does not land in images that never call it.
#if SHELL_PANEL_SCAN

#include "control/control_law.h"

namespace shell {

namespace {
control::ControlLaw g_law;
} // namespace

void control_boot(float* knobs)
{
    for(int i = 0; i < ffctl::NUM_PARAMS; ++i) knobs[i] = ffctl::kInitParamDefaults[i];
    g_law.on_restore();
}

void control_tick(const float* knobs, spky::Instrument& inst)
{
    (void)g_law.tick(knobs, control::Options{}, inst);
}

} // namespace shell

#endif // SHELL_PANEL_SCAN
