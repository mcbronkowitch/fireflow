#pragma once

// The crosstalk probe. Holds one multiplexer channel still, fires one
// controlled digital event on the same board at a chosen time before the
// ADC's aperture, and reports how far the reading moves.
//
// It is NOT a settle-time measurement -- that instrument exists and its
// results stand (docs/hardware/settle-measured.md). It reuses that probe's
// ADC primitives, its measured clock, its gates G2 and G4 and its grid, and
// it inherits their limits.
//
// Spec: ../docs/superpowers/specs/2026-09-18-coupon-crosstalk-probe-design.md
#include "hw/board.h"

namespace shell {

// Never returns. Prints one block per pass on USB-CDC, forever.
void run_xtalk_probe(bench::Board& hw);

} // namespace shell
