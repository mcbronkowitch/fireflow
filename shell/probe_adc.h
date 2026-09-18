#pragma once

// The ADC primitives both coupon probes drive ADC1 with. Extracted verbatim
// from settle_probe.cpp, which reached this shape over ten hardware fix
// rounds -- every comment that came with a fix came with it, because each one
// records a failure that cost a board session to find.
//
// Why a shared file rather than a second mode in settle_probe.cpp: that file
// is 1028 lines and its knee search, its tail reference and its gates G1 and
// G3 are the settle question's statistics, every one of which is the wrong
// question for a crosstalk measurement. Crosstalk spec section 3.
//
// Board-only. Never compiled on the host: it holds HAL types, which is
// exactly why settle_plan.h and xtalk_plan.h hold none.
#include <cstdint>

#include "hw/board.h"

namespace shell {
namespace probe_adc {

// sample_now() writes this into *span_cycles when the EOC poll hit its bound.
// It is not a duration and must never be averaged into one.
inline constexpr uint32_t kTimeoutSentinel = 0xFFFFFFFFu;

// The ADC1 channel number behind each of the coupon's two sense pins.
// group 0 = the 4067 on ADC_9, group 1 = the 4051 on ADC_10.
uint32_t channel_of_group(int group);

// The HAL ADC_SAMPLETIME_* constant at index `rung` of settle_plan.h's
// kSamplingLadderTenths. Out of range clamps to the longest rung, matching
// sample_time_index_for()'s own out-of-range answer.
uint32_t sample_time_for_rung(int rung);

// Takes ADC1 away from libDaisy and configures it single-shot, 16 bit, no
// oversampling, no DMA. Calibrates ONCE. Must run before anything else here.
void init(bench::Board& hw);

// Runs the first conversion to completion so no later timed sample pays the
// one-time enable cost. Must run AFTER a select(). Returns false on timeout:
// a probe that cannot start must say it cannot start.
bool warm_up();

void select_time(uint32_t channel, uint32_t sampling_time);
void select(uint32_t channel);   // the working rung

// One conversion, interrupt-masked across the timed window, EOC poll bounded.
// Writes the start-to-EOC span in DWT cycles if asked.
uint16_t sample_now(uint32_t* span_cycles);

// Mean value / mean span over `repeats` conversions on whatever channel and
// sampling time is currently selected. Timed-out repeats are excluded from
// the span reducer and counted; -1 comes back when every repeat timed out.
int32_t mean_of_repeats(int repeats);
int32_t mean_span_of_repeats(int repeats);

// Lifetime count of timed-out conversions, and the three HAL statuses this
// file would otherwise throw away. None of them is a gate; they are printed
// so a run whose ADC was never set up does not pass as plausible integers.
uint32_t timeouts();
bool     init_ok();
bool     cal_ok();
bool     cfg_ok();

// This boot's measured ADC clock and everything derived from it. The clock is
// MEASURED every boot and never assumed: settle-budget.md section 1 derived
// 12.29 MHz from PLL3's dividers, the board runs at 6.146 MHz -- exactly half
// -- and that wrong constant cost three fix rounds chasing a latency that was
// never in software.
struct Clock
{
    int32_t span_short_cyc;          // mean span at the working sampling rung
    int32_t span_long_cyc;           // mean span at the 387.5-cycle rung
    int32_t working_conversion_ns;   // 25 ADC cycles at the measured clock
    int32_t measured_adc_khz;
    double  core_cyc_per_adc_cyc;
    double  pre_adstart_overhead_core_cyc;
    bool    ok;                      // false -> every derived field reads -1
};

// Two spans on the SAME parked channel, identical in every way except the
// configured sampling time, so every fixed cost cancels in the difference and
// only the extra 371 ADC cycles remain. Leaves the working sampling time
// selected, which every later pass needs.
Clock measure_clock(int repeats, uint32_t channel);

// pre-ADSTART overhead plus that rung's sampling window, in ns, or -1 when
// the clock pass came back invalid.
//
// The sampling window and NOT the conversion cycles: the sample-and-hold is
// acquired at the END of the window, and the conversion cycles that follow
// only digitise what is already captured, so they do not delay the instant
// that needs the node to have settled.
int32_t offset_ns_for_rung(const Clock& c, int rung);

} // namespace probe_adc
} // namespace shell
