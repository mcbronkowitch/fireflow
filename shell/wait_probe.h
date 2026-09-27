#pragma once

// The wait-sweep probe (round three). Round two found that a settled 5150 ohm
// channel reads 852 counts low when its conversions are 10 ms apart, and
// proved it is the interval between conversions and not the tone -- but its
// wait was a tone period, so it could produce three waits and no more. This
// probe makes the wait the axis: prime a conversion, idle a commanded W,
// convert again, for W from back to back to 50 ms.
//
// Spec: ../docs/superpowers/specs/2026-09-27-coupon-wait-sweep-probe-design.md
#include "hw/board.h"

namespace shell {

// Never returns. Each block: the ADC clock and latency calibration (round
// one's passes, unchanged), arms A (wait), B (one discard before the read)
// and L (387.5-cycle rung) over five victims with the codec stopped, arm C
// (the bridge to round two) with the codec running and the callback writing
// zeros, then G5's span and address pass. See wait_probe.cpp for the gates
// (G2, G4, G5, G7; G9 is computed by read_wait.py, not here).
void run_wait_probe(bench::Board& hw);

} // namespace shell
