#pragma once

#include "workload.h"

namespace bench {

// Two transports, chosen at compile time by BENCH_TRANSPORT (bench/Makefile);
// report.cpp holds both and nothing else in the bench knows which is linked.
//
//   semihost -- output leaves over the SWD link the probe already owns (ARM
//               semihosting). openocd services the breakpoint and prints the
//               string on its own stdout. DELIBERATE CONSEQUENCE: this binary
//               requires an attached openocd; without one the first bkpt 0xAB
//               halts the core forever.
//   usb      -- output leaves over USB-CDC. No probe, no SWD pins, and the
//               board is reachable with nothing but the cable that powers it.
//
// Call transport_open() once before the first line, from main(). It blocks on
// the USB branch until the host opens the port, and does nothing on the
// semihosting branch.
void transport_open();
void log_line(const char* s);
void logf(const char* fmt, ...);

// Marker contract, parsed by run.py. Anything printed outside these markers
// is free-form and ignored by the parser.
void report_begin(const char* githash, const char* qspi_sha256,
                  const char* families);
void report_end();

void report_row(const Workload& w, const Result& r);

void report_anchor(const char* name, uint32_t avg_x100, uint32_t max_x100);

} // namespace bench
