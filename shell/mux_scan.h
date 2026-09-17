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
// Which board this image is for. Included HERE and not left to main.cpp:
// mux_scan.cpp sees only this header, and a profile that differs between
// two objects gives g_mux_values two sizes in one link.
#include "shell_coupon_probe.h"
#include "mux_plan.h"

namespace shell {

// Which board this image is built for. The switch header is generated at
// Makefile PARSE time; see write_shell_coupon_probe.py for why a bare -D
// is not enough.
#if SHELL_COUPON_PROBE
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

    // Public because the coupon bring-up probe drives the chain directly
    // instead of stepping the scan: it has to hold one address still while
    // the ADC is read, which step() deliberately never does.
    void write_chain(uint32_t word);

    // Clocks `word` out and the 165's parallel load back in, in the same
    // pass -- the two chains share clock and latch, so a separate read pass
    // would cost a second latch and re-load the buttons mid-flight.
    // Returns the return stream, first bit shifted out in bit 0.
    uint32_t read_chain(uint32_t word);

  private:
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
