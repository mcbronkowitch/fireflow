#pragma once

// The test coupon's bring-up scan (hardware/coupon/). Walks every channel,
// holds it long enough that the free-running ADC certainly reports it,
// judges it against coupon_expect.h and prints the lot over USB-CDC.
//
// It is NOT a settle measurement: libDaisy's ADC scans twelve channels at
// OVS_32 and has no defined time relation to the address write, which is
// exactly why the settle probe owns ADC1 itself
// (docs/superpowers/specs/2026-09-17-coupon-settle-probe-design.md section 3).
// This probe answers the other question -- whether the board is wired the
// way the netlist says -- and that one has no clock in it.
#include "hw/board.h"

namespace shell {

// Runs the scan once, then repeats the report forever. Never returns.
void run_coupon_bringup(bench::Board& hw);

} // namespace shell
