#pragma once

// The settle probe. Takes ADC1 away from libDaisy and drives it single-shot
// so that the aperture is a thing the firmware decides rather than a thing
// the DMA happens to do. Spec:
// ../docs/superpowers/specs/2026-09-17-coupon-settle-probe-design.md
#include "hw/board.h"

namespace shell {

// Never returns. Prints one block per pass on USB-CDC, forever.
void run_settle_probe(bench::Board& hw);

} // namespace shell
