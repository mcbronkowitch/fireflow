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
// One step per block means a full sweep of scan_steps(kPanelChain) takes
// 32 blocks = 64 ms, i.e. ~15.6 Hz per channel with all eight chips
// populated, ~31 Hz with four.
// That is arithmetic, not a measurement, and it is the rate the CPU numbers
// belong to.
#include "hw/board.h"
// Which board this image is for. Included HERE and not left to main.cpp:
// mux_scan.cpp sees only this header, and a profile that differs between
// two objects gives g_mux_raw two sizes in one link.
#include "shell_coupon_probe.h"
#include "shell_panel_scan.h"
#include "mux_plan.h"
#if SHELL_PANEL_SCAN && !SHELL_COUPON_PROBE
#include "generated_panel_map.h"
#endif
#include "cycles.h"

namespace shell {

// Which board and which step model this image runs (spec
// 2026-10-02-rev-a-p6a-panel-scan-design.md section 3.2):
//   coupon probes      -- kCouponChain, sequential, exactly as measured
//   coupon play image  -- kCouponPlayChain: the coupon's wiring, Rev A's model
//   Rev A play image   -- kRevaChain, generated from the pin map
//   anything else      -- kPanelChain, the profile SHELL_MUX_PROBE priced
#if SHELL_COUPON_PROBE && SHELL_PANEL_SCAN
inline constexpr ChainProfile kActiveChain = kCouponPlayChain;
#elif SHELL_COUPON_PROBE
inline constexpr ChainProfile kActiveChain = kCouponChain;
#elif SHELL_PANEL_SCAN
inline constexpr ChainProfile kActiveChain = kRevaChain;
#else
inline constexpr ChainProfile kActiveChain = kPanelChain;
#endif

class MuxScan
{
  public:
    void init();

    // Read the step selected last time, then select the next one. Returns
    // the step whose values were just stored in g_mux_raw, or -1 on the
    // first call (nothing was selected yet).
    //
    // The read is the RAW DMA word, hw.adc.Get(). Until 2026-09-28 this read
    // hw.GetAdcValue(), which returns libDaisy's AnalogControl::Value() -- a
    // filtered value that only moves when ProcessAnalogControls() runs, and
    // nothing in shell/ calls it. The CPU run of 2026-08-23 priced those
    // reads and was not affected. Read from the source, not measured: every
    // value they stored was 0. The slew filter behind AnalogControl must
    // never sit behind a mux in any case: it would average across channel
    // changes. SHELL_MUX_PROBE images now run this step() too (raw reads,
    // live pins only), so rebuilding one does not reproduce the 2026-08-23
    // instrument exactly.
    // (spec 2026-09-28-coupon-panel-scan-design.md section 2)
    int step(bench::Board& hw);

    // The two halves of step(), for the scan-check image, whose arms park,
    // re-order and repeat them.
    void select(int step);
    void read_step(bench::Board& hw, int step);

    int      live_step() const { return live_step_; }
    uint32_t steps() const { return steps_; }

    // step() walks the LED field by default, as the 2026-08-23 CPU probe
    // needs: a constant word would let the compiler hoist the loop's work
    // and price a scan nobody ships. The panel-scan images turn it off and
    // keep the coupon's LEDs dark, so both images put the same digital load
    // beside the analog read.
    void set_walk_leds(bool on) { walk_leds_ = on; }

    // Public because the coupon bring-up probe drives the chain directly
    // instead of stepping the scan: it has to hold one address still while
    // the ADC is read, which step() deliberately never does.
    void write_chain(uint64_t word);

    // Like write_chain(), but returns the DWT cycle count taken immediately
    // after the 595s' RCLK rising edge.
    //
    // t = 0 IS THAT EDGE, not the start of the bit-bang. write_chain() clocks
    // chain_bits bits (16 on the coupon) before the address reaches the mux
    // at all, so timing from the
    // call would fold the bit-bang into every settle time and make the fast
    // channels look slow by a constant nobody measured.
    uint32_t write_chain_timed(uint64_t word);

    // Like write_chain_timed(), but the RCLK pulse is left out entirely:
    // chain_bits bits are clocked and nothing is latched. Returns the DWT cycle
    // count taken immediately after the last SRCLK falling edge.
    //
    // WHAT THIS IS FOR. The 595's outputs follow its STORAGE register, which
    // moves on RCLK only -- so a shift with no latch is digital supply and
    // ground activity with no change on any mux control line. That is the
    // crosstalk probe's row 9, and it separates "the chain's traffic" from
    // "the bits the chain carries". Read from the 74HC595 datasheet and
    // UNMEASURED on this board; row 9 against row 2 is the check.
    //
    // The 165 is clocked too -- it shares CP -- which is what the shipping
    // scan does on every step anyway, so this is not a quieter event than
    // production, it is the production event minus the latch.
    uint32_t shift_chain_timed(uint64_t word);

    // Clocks `word` out and the 165's parallel load back in, in the same
    // pass -- the two chains share clock and latch, so a separate read pass
    // would cost a second latch and re-load the buttons mid-flight.
    // Returns the return stream, first bit shifted out in bit 0.
    // Only the 165's eight stages are returned: with DS on GND everything after reads 0.
    uint32_t read_chain(uint64_t word);

    // The LED field the next select() latches (spec section 3.3). It never
    // reaches the 595s on its own: LED bits change only in the latch that
    // carries the mux address (P2 section 4).
    void set_leds(uint32_t leds) { leds_ = leds; }

    // The play images read the keys in the same pass as every select()
    // (spec section 3.4); the probes keep the write-only pass they were
    // measured with.
    void     set_read_keys(bool on) { read_keys_ = on; }
    uint32_t last_return() const { return last_return_; }

  private:
    daisy::GPIO data_, clock_, latch_, sense_in_;
    uint32_t    leds_      = 0;
    int         next_step_ = 0;
    int         live_step_ = -1;
    uint32_t    steps_     = 0;
    bool        walk_leds_ = true;
    bool        read_keys_   = false;
    uint32_t    last_return_ = 0xFFFFFFFFu;   // every key released
};

// Where the scan puts what it read: the raw 16-bit DMA word per channel,
// indexed by mux_channel(). Only live (step, sense) pairs are ever written.
// Volatile so a build that does not use the values still performs the reads
// -- the CPU probe images deliberately do NOT push them into the engine.
extern volatile uint16_t g_mux_raw[mux_total(kActiveChain)];

} // namespace shell
