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

// Never returns. Alternates two DC rungs -- exact silence and 0 dBFS --
// holding each 15 seconds for a handheld meter, and prints one block per
// pass on USB-CDC, forever. The bench instrument that took spec §9's
// readings: -10.8 mV at silence, -8.66 V at 0 dBFS, both tracking the
// callback. See tone_probe.cpp for what those two rungs separated.
void run_tone_probe(bench::Board& hw);

} // namespace shell
