#pragma once

// The boot take: at power-up "the pots count" -- every pot's position is read
// once and written to its current target, ENGINE rows first, so a deck's
// ATTACK vs STAGES follows the engine the panel actually shows. No hardware
// type, host tested (tests/test_boot_take.cpp). The firmware (panel_scan.cpp)
// collects a reading per row during the first sweeps, applies once, then
// releases the owners; that wiring is not this unit's.
//
// Spec: ../docs/superpowers/specs/2026-10-28-panel-state-persistence-design.md
// section 4.4.
#include <cstddef>
#include <cstdint>
#include "controls.h"
#include "scan_value.h"

namespace shell {

// Row capacity: Rev A's table has 73 rows (kRevaTable.count, asserted in
// boot_take.cpp).
inline constexpr int kMaxRows = 80;

struct BootTake
{
    uint16_t raw[kMaxRows]  = {};
    bool     seen[kMaxRows] = {};
    bool     done = false;
};

// One reading of row r (index into t.entries) with a valid span; ignored once
// done or for a row outside 0..kMaxRows.
void boot_take_collect(BootTake& b, int row, uint16_t raw);

// True once every row of t has been seen.
bool boot_take_ready(const BootTake& b, const ControlTable& t);

// Writes every row once: first all rows whose param is ENGINE_A / ENGINE_B,
// then the rest through knob_target() (so ATTACK vs STAGES follows the freshly
// written ENGINE). MODBTN is treated as 0: a twin-bearing pot writes its sound
// parameter, never its depth. The value is what a fresh PotFilter emits for
// the raw word (span and snap regions, no hysteresis history). Only rows seen
// by boot_take_collect() are written; rows with param -1 are skipped. Sets
// done -- except with an invalid span or a table larger than kMaxRows, which
// write nothing and leave done false.
void boot_take_apply(BootTake& b, const ControlTable& t, const Span& span, float* knobs);

// The boot mute (probe 4, docs/hardware/persist-measured.md): until the take
// is done the engine plays the record's knob values, not the pots' -- on the
// board about 30 ms of a saved FILT A with the pot already turned shut. The
// audio callback runs every block's output through the gate after process():
// silence while the take is open, then a linear fade-in over
// kBootFadeSamples (20 ms at 48 kHz) so a sustained patch does not click on,
// then pass-through for good.
inline constexpr int kBootFadeSamples = 960;

struct BootGate
{
    int faded = 0;   // fade samples played; kBootFadeSamples = open for good
};

void boot_gate_apply(BootGate& g, bool take_done, float* l, float* r, size_t n);

} // namespace shell
