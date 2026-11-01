#pragma once

// The reverb grain layer (SHELL_RGRAIN; SD use-case tests B1 and B2, spec
// section 5). THROWAWAY measurement code, not a feature: up to
// kRgrainGrains sampler grains (spky::Grain, unchanged) play a generated
// sample from SDRAM, and panel_scan.cpp hands their sum to
// Instrument::set_reverb_inject(), so the reverb hears them and the callback
// pays for them. B1 prices one grain per block (rgrain_n(), fixed N per
// frame); B2 grants grains per block from the previous block's cycles
// (rgrain_grant(), sd_schedule.h). Nobody judges the sound (spec section 3).
//
// Spec: ../docs/superpowers/specs/2026-10-31-sd-use-cases-tests-design.md
#include <cstdint>

namespace shell {

// One grain's price per 96-sample block, in DWT cycles: B2's cap grants
// (budget - base) / kRgrainCycles grains. 17 900 is the round-2 estimate (spec
// section 2, "One grain of the sampler"); B1's measured slope replaces it.
// read_rgrain.py reads this line for B2's verdict.
inline constexpr uint32_t kRgrainCycles = 17900;

// B2's budget: the grant fills the previous block's grain-free cost up to here.
inline constexpr uint32_t kRgrainBudget = 920000;

// The grains the layer holds, and the most either schedule asks for.
inline constexpr uint8_t kRgrainGrains = 8;

// The block buffers' length: the board's audio block (bench::board_init()
// sets 96). A board with a longer block runs without the layer.
inline constexpr uint32_t kRgrainFrames = 96;

// Before StartAudio(), after board_init() (SDRAM is up): fills the 2 s stereo
// sample in SDRAM -- an exponential sine sweep 100 Hz to 4 kHz plus white
// noise, peaking at -12 dBFS together -- and hands it to the grains'
// spky::SampleBuffer.
void rgrain_init();

// Audio-callback context. Writes the sum of the first n of the kRgrainGrains
// grains into l and r (frames samples each, n clamped to kRgrainGrains, zeros
// for n = 0). A grain that has ended is respawned in place: 100 ms long,
// attack and decay a quarter each, a random start in the sample, ratio 1,
// centred. The grains past n are not processed and keep their place.
void rgrain_block(uint8_t n, float* l, float* r, uint32_t frames);

} // namespace shell
