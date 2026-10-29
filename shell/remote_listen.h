#pragma once

// The remote command channel's board half (CPU hunt, 2026-10-29; spec
// docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md section 5.3).
// Built only with SHELL_REMOTE=1 (shell/Makefile). Rev A has no SWD pins, so
// software is the only way back to the bootloader that does not need a hand
// on BOOT and RESET; this file is that way, and it has to work in the states
// a measurement image gets into:
//
//   - the audio callback runs back to back. The audio DMA and USB both sit at
//     NVIC priority 0 and the DMA wins, so the foreground starves and never
//     sees a command. BOOT is therefore acted on inside the USB receive
//     interrupt itself (on_rx() in remote_listen.cpp).
//   - the image does not boot: a failed itcm_load() (another build's QSPI
//     image) or a hard fault. Both enter the bootloader, instead of blinking
//     for ever or hitting libDaisy's HardFault_Handler, which executes BKPT
//     and locks up without a debugger.
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

// Copies the vector table into RAM, points HardFault, MemManage, BusFault and
// UsageFault at remote_enter_bootloader(), and moves SCB->VTOR there. The
// first statement of main(), so a fault in board_init() is covered too.
void remote_install_fault_vectors();

} // namespace shell
