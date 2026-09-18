#pragma once

// The codec-tone probe. Round one (the crosstalk probe) pits digital edges
// against a settled mux channel; this one pits the AUDIO OUTPUT against it --
// a tone on AUDIO_OUT_L/R, which leave the submodule on B2/B1
// (netlist.py:106) and run through the analog domain to the unpopulated
// J_AUDIO and to TP_AUDIO_L, while a divider channel is read.
//
// It is NOT the 2026-08-23 artifact question. That series is the scan
// coupling INTO audio; this is audio coupling into the scan. The two share a
// board and nothing else.
//
// Spec: ../docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md
#include "hw/board.h"

namespace shell {

// Never returns. Drives a sine on AUDIO_OUT_L/R through a phase accumulator
// clocked from the audio callback's own block boundaries (spec section 5),
// while ADC1 reads one of round one's five victims through the divider under
// test. Each block: the ADC clock and latency calibration (round one's
// passes, unchanged), then per victim the two silent levels -- codec
// stopped and codec running with the callback writing zeros, which is the
// first measurement anywhere of the I2S/SAI-DMA floor with the SAI actually
// running -- then G5's span and address verdict. Task 4 adds the frequency x
// level phase grid on top of this; Task 5 adds the two window cases. See
// tone_probe.cpp for the gates (G2, G4, G5, G7; G8 is computed by
// read_tone.py, not here) and for what each printed line carries.
void run_tone_probe(bench::Board& hw);

} // namespace shell
