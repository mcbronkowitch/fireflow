#pragma once

// The hardware half of the panel scan. mux_plan.h holds everything that can
// be checked on the host; this file holds what only means something on a
// board: four bit-banged chain pins and four ADC reads.
//
// IT NEVER WAITS FOR THE MUX TO SETTLE. The address is clocked out at the end
// of one audio block and sampled at the start of the next, so the block period
// IS the settle window -- 2 ms at 96 samples / 48 kHz. A busy-wait settle
// inside the callback would be dead time charged to the audio budget for
// nothing. Whether 2 ms is actually enough is Phase-0 Task 6 step 5b's
// question and needs a mux on the table; this file is what gets priced first.
//
// One step per block means a full sweep of kScanSteps takes 32 blocks = 64 ms,
// i.e. ~15.6 Hz per channel with all eight chips populated, ~31 Hz with four.
// That is arithmetic, not a measurement, and it is the rate the CPU numbers
// belong to.
#include "hw/board.h"
#include "mux_plan.h"

namespace shell {

// Which board this image is built for. The switch header is generated at
// Makefile PARSE time; see write_shell_coupon_probe.py for why a bare -D
// is not enough. Task 3 of this plan adds that switch -- until then this
// resolves to the panel.
#if defined(SHELL_COUPON_PROBE) && SHELL_COUPON_PROBE
inline constexpr ChainProfile kActiveChain = kCouponChain;
#else
inline constexpr ChainProfile kActiveChain = kPanelChain;
#endif

class MuxScan
{
  public:
    void init();

    // Read the four sense pins for the step written last time, then clock out
    // the next step.
    void step(bench::Board& hw);

    uint32_t steps() const { return steps_; }

  private:
    void write_chain(uint32_t word);

    daisy::GPIO data_, clock_, latch_, sense_in_;
    uint32_t    leds_      = 0;
    int         next_step_ = 0;
    int         live_step_ = -1;
    uint32_t    steps_     = 0;
};

// Where the scan puts what it read. Volatile so a build that does not apply
// the values still performs the reads -- the probe images deliberately do NOT
// push these into the engine, because the operating point has to stay the one
// the baseline image runs or the audio comparison compares two instruments.
extern volatile float g_mux_values[mux_total(kActiveChain)];

} // namespace shell
