#include "coupon_scan.h"

#include "coupon_expect.h"
#include "mux_plan.h"
#include "mux_scan.h"

namespace shell {

namespace {

// Nine full DMA rounds. libDaisy converts at 12.29 MHz (adc.cpp:229) with
// SPEED_8CYCLES_5 sampling plus 8.5 cycles of 16-bit conversion = 17 ADC
// cycles = 1.38 us, at OVS_32 over twelve channels = 531 us per round. Five
// milliseconds is not a round number picked for comfort. That is arithmetic,
// not a measurement -- same as mux_scan.h's sweep-rate comment.
constexpr uint32_t kHoldMs = 5;

constexpr int kSteps = scan_steps(kCouponChain);

uint16_t g_raw[kSteps] = {};

} // namespace

void run_coupon_bringup(bench::Board& hw)
{
    MuxScan chain;
    chain.init();
    hw.StartLog(false);

    // Every round re-scans. The first draft scanned once before this loop and
    // then reprinted the same array forever, which reads as a working probe --
    // the values are per-channel correct, they simply never change again. It
    // cost a bring-up session: two readings taken at opposite pot stops came
    // back bit-identical, and the board was blamed before the firmware was.
    // A one-shot probe can only ever describe the instant it booted, so it
    // cannot answer anything you turn, press or touch. Rescanning also makes
    // ADC noise visible between rounds, which a frozen array hides.
    while(1)
    {
        for(int s = 0; s < kSteps; ++s)
        {
            // All LEDs dark during the scan. A walking pattern would be
            // prettier and would also put a changing digital load beside the
            // analog read -- that is the noise question, not this probe's.
            chain.write_chain(
                chain_word(kCouponChain, step_pattern(kCouponChain, s), 0u));
            hw.Delay(kHoldMs);

            const int g     = group_of_step(kCouponChain, s);
            const int sense = kCouponChain.sense_of_group[g];
            g_raw[s]        = static_cast<uint16_t>(hw.adc.Get(
                static_cast<uint8_t>(kCouponChain.sense_adc_base + sense)));
        }

        // Park with both muxes disabled, and take the 165's stream on the same
        // pass -- the two chains share clock and latch, so a separate read
        // would cost a second latch and re-load the buttons mid-flight.
        const uint32_t parked
            = chain_word(kCouponChain, step_pattern(kCouponChain, -1), 0u);
        const uint32_t ret     = chain.read_chain(parked);
        const int      bb      = button_bit(kCouponChain);
        const int pressed = (bb < 0) ? -1 : (((ret >> bb) & 1u) == 0u ? 1 : 0);

        int failures = 0;
        for(int s = 0; s < kSteps; ++s)
            if(!coupon_verdict(coupon_expect(s), g_raw[s])) ++failures;

        hw.PrintLine("COUPON_BEGIN steps=%d hold_ms=%d fails=%d button=%d "
                     "ret=%d",
                     kSteps, static_cast<int>(kHoldMs), failures, pressed,
                     static_cast<int>(ret));
        for(int s = 0; s < kSteps; ++s)
        {
            const int    g = group_of_step(kCouponChain, s);
            const Expect e = coupon_expect(s);
            hw.PrintLine("COUPON_CH step=%d group=%d addr=%d sense=%d raw=%d "
                         "expect=%d pass=%d",
                         s, g,
                         static_cast<int>(step_pattern(kCouponChain, s).address),
                         kCouponChain.sense_of_group[g],
                         static_cast<int>(g_raw[s]), static_cast<int>(e),
                         coupon_verdict(e, g_raw[s]) ? 1 : 0);
        }
        hw.PrintLine("COUPON_END");
        hw.Delay(1000);
    }
}

} // namespace shell
