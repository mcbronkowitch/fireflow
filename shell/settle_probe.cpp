#include "settle_probe.h"

#include "cycles.h"
#include "settle_plan.h"

namespace shell {

void run_settle_probe(bench::Board& hw)
{
    cycles_init();
    hw.StartLog(false);

    while(1)
    {
        // The configuration line comes FIRST and the end marker always
        // arrives, even on a pass that measured nothing. A reader that can
        // only recognise a complete block is the point: a truncated one must
        // be discarded rather than half-believed.
        //
        // sample_cycles=165 is ADC_SAMPLETIME_16CYCLES_5 expressed in tenths,
        // so it stays an integer for %d -- PrintLine() is the lightweight
        // printf and the existing probes stay on %d for that reason.
        hw.PrintLine("SHELL_SETTLE_CFG sample_cycles=165 adc_khz=12290 "
                     "repeats=%d grid_step_ns=%d grid_points=%d park_ns=%d",
                     kRepeats, kGridStepNs, kGridPoints,
                     static_cast<int>(kParkNs));
        hw.PrintLine("SHELL_SETTLE_END");
        hw.Delay(1000);
    }
}

} // namespace shell
