#pragma once

// Sampler takes on the SD card, the firmware half (SHELL_TAKES=1; spec
// docs/superpowers/specs/2026-11-06-sampler-takes-on-sd-design.md). The
// decisions live in the host-tested units: the file layout in take_file.h,
// when and how fast to save in take_policy.h. This file does the card work
// they ask for and hands loaded takes to the engine.
//
// Two sides, never crossing:
// - the foreground (run_panel_scan_report()) mounts, loads, saves and prints;
//   it never calls a mutating engine method -- a load goes through the
//   mailbox, which the callback applies at the top of a block;
// - the audio callback (panel_scan_tick()) applies the mailbox, publishes
//   each deck's state (take_policy.h's DeckView) and counts blocks.
//
// Every card call is marked for the pot hold (sd_hold.h, SdMark).
#include <cstdint>

#include "hw/board.h"
#include "instrument.h"

namespace shell {

// ---- Foreground ----

// Once, from the report loop, after StartAudio(), after the boot-only
// listener is up and after the boot take is done: mounts the card (bus clock
// stopped between transfers), creates /FIREFLOW/, its four slot directories
// and any missing slot file, then loads both decks. Blocks while it runs (up
// to kTakeLoadDeadlineBlocks per deck, longer only if a card call hangs on
// libDaisy's 30 s wait); the board plays meanwhile.
void take_card_boot(bench::Board& hw);

// In place of the report loop's Delay(500): runs the save policy and its card
// work until `ms` have passed, counted in audio blocks. Before
// take_card_boot() it only waits.
void take_service_for(bench::Board& hw, uint32_t ms);

// The latched SHELL_TAKE lines, on pass 0 and every 10th pass after it (5 s).
void take_report(bench::Board& hw, uint32_t pass);

// ---- Audio callback, once per block, at the top of panel_scan_tick() ----

// Applies a pending mailbox request (begin_load, restore, abort_load), then
// publishes both decks' DeckView and counts the block.
void take_block(spky::Instrument& inst);

} // namespace shell
