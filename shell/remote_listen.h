#pragma once

// The remote command channel's board half (CPU hunt, 2026-10-29; spec
// docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md section 5.3).
// Built with SHELL_REMOTE=1, and with SHELL_BOOTCMD as the play image's
// boot-only listener (shell/Makefile). Rev A has no SWD pins, so
// software is the only way back to the bootloader that does not need a hand
// on BOOT and RESET; this file is that way, and it has to work in the states
// a measurement image gets into:
//
//   - the foreground starves: the callback leaves gaps between blocks, but
//     too few for the report loop to get through. BOOT is therefore acted on
//     inside the USB receive interrupt itself (on_rx() in remote_listen.cpp),
//     which still runs in those gaps.
//   - the callback runs back to back (it, and what runs around it, takes a
//     block or longer). The audio DMA and USB both sit at NVIC priority 0 and
//     the DMA has the lower exception number, so its interrupt is always the
//     pending one picked next and the USB interrupt never runs: no command,
//     BOOT included, arrives. Only the overrun guard (workload_run.h) gets
//     out of that state, by silencing the callback; with the guard off
//     (measurement image, bit 31) the board needs BOOT + RESET by hand.
//   - the image does not boot: a failed itcm_load() (another build's QSPI
//     image) or a fault (NMI, HardFault, MemManage, BusFault, UsageFault).
//     All enter the bootloader, instead of blinking for ever or spinning in
//     libDaisy's default handler. A mismatched pair runs the other build's
//     cold QSPI code in board_init() BEFORE itcm_load() checks the stamp;
//     that is recovered only if that code faults (spec section 8).
//
// The pure half (parsing, packet assembly, the ISR-to-callback ring) is
// remote_cmd.h, host-tested; the wiring is held by test_remote_wiring.py.
#include "remote_cmd.h"

namespace shell {

// Registers the USB receive callback. Call right after StartLog(): the log is
// what brings the CDC port up.
void remote_listen_start();

// The audio callback's once-per-block pop. False when nothing is queued.
// PING and BOOT never arrive here: the interrupt handles both.
bool remote_poll(Command* c);

// True once per PING received since the last call. The foreground report
// loop answers with `SHELL_REMOTE pong`; the interrupt never prints.
bool remote_take_ping();

// The build's git stamp (shell_git_hash.h), for the `SHELL_REMOTE on` line.
const char* remote_build();

// Resets into the Daisy bootloader with an infinite timeout, so the board
// waits for DFU: the DAISY_INFINITE_TIMEOUT path of
// daisy::System::ResetToBootloader(), written out (remote_listen.cpp says
// why). Safe from any context, a priority-0 interrupt and a fault handler
// included: it never waits on SysTick and never reaches QSPI code.
[[noreturn]] void remote_enter_bootloader();

// Copies the vector table into RAM, points NMI, HardFault, MemManage, BusFault
// and UsageFault at remote_enter_bootloader(), and moves SCB->VTOR there. The
// first statement of main(), so a fault in board_init() is covered too.
void remote_install_fault_vectors();

} // namespace shell
