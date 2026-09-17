#include "mux_scan.h"

namespace shell {

volatile float g_mux_values[mux_total(kActiveChain)] = {};

namespace {

// The production pins, not spare ones picked for a test: io-budget §3 spends
// B7, B8, D1 and D10 on the 595/165 chain and every remaining GPIO on the
// 4-bit SD slot. Measuring on other pins would price a different port.
constexpr daisy::Pin kData  = daisy::patch_sm::DaisyPatchSM::B7;
constexpr daisy::Pin kClock = daisy::patch_sm::DaisyPatchSM::B8;
constexpr daisy::Pin kLatch = daisy::patch_sm::DaisyPatchSM::D1;
constexpr daisy::Pin kIn    = daisy::patch_sm::DaisyPatchSM::D10;

// libDaisy's default, and it stays the default. Edge rate is an EMI
// parameter and this round measures whether the burst moves the block-rate
// tone -- so it moves one thing at a time. A faster setting is a separate
// measurement, not a footnote to this one.
constexpr daisy::GPIO::Speed kSpeed = daisy::GPIO::Speed::LOW;

// The sense pins are the RAW ADC inputs A2/A3/D8/D9, not the conditioned CV
// pins. Until 2026-09-17 this read CV_1 + s, which cost nothing in the CPU
// measurement it was written for and would have made every coupon reading
// meaningless. The number lives in mux_plan.h so the host can assert it;
// this is where it gets checked against libDaisy.
static_assert(daisy::patch_sm::ADC_9 == kSenseAdcBase,
              "libDaisy's patch_sm channel enum moved under kSenseAdcBase");

} // namespace

void MuxScan::init()
{
    data_.Init(kData, daisy::GPIO::Mode::OUTPUT, daisy::GPIO::Pull::NOPULL,
               kSpeed);
    clock_.Init(kClock, daisy::GPIO::Mode::OUTPUT, daisy::GPIO::Pull::NOPULL,
                kSpeed);
    latch_.Init(kLatch, daisy::GPIO::Mode::OUTPUT, daisy::GPIO::Pull::NOPULL,
                kSpeed);
    // The 165 button chain's return line. Nothing drives it on a bare
    // submodule, hence the pull-up: a floating input is a current source and
    // a noise source, and this round is about noise.
    sense_in_.Init(kIn, daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
}

void MuxScan::write_chain(uint32_t word)
{
    // MSB first, no delay between edges: at 480 MHz two register writes are
    // some nanoseconds apart, which a 74HC595 at 3V3 may or may not accept.
    // If a real chain needs a delay, THE COST MEASURED HERE IS OPTIMISTIC --
    // that belongs in the write-up, not in a comment nobody reads.
    for(int i = kActiveChain.chain_bits - 1; i >= 0; --i)
    {
        data_.Write(((word >> i) & 1u) != 0u);
        clock_.Write(true);
        clock_.Write(false);
    }
    latch_.Write(true);
    latch_.Write(false);
}

void MuxScan::step(bench::Board& hw)
{
    if(live_step_ >= 0)
    {
        for(int s = 0; s < kActiveChain.sense_pins; ++s)
        {
            const int ch = mux_channel(kActiveChain, live_step_, s);
            if(ch >= 0)
                g_mux_values[ch] = hw.GetAdcValue(kSenseAdcBase + s);
        }
    }

    // The LED field changes every step, as it does in production. A constant
    // word would let the compiler hoist the loop's work out of the hot path
    // and price a scan nobody will ship.
    leds_ = (leds_ + 1u) & ((1u << kActiveChain.led_bits) - 1u);

    const StepPattern p = step_pattern(kActiveChain, next_step_);
    write_chain(chain_word(kActiveChain, p, leds_));

    live_step_ = next_step_;
    next_step_ = (next_step_ + 1) % scan_steps(kActiveChain);
    ++steps_;
}

} // namespace shell
