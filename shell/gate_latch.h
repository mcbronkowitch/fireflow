#pragma once

// CLOCK (Gate In 1, B10 = PG13) and RESET (Gate In 2, B9 = PG14) edges,
// latched by the EXTI edge detector and read once per audio block, with no
// interrupt (spec docs/superpowers/specs/2026-10-19-clock-and-reset-jacks-design.md
// section 6, "Edges on the board are latched in the EXTI pending bits").
//
// Why not GateIn::Trig(): it compares the pin level between calls, so a
// trigger shorter than the 2 ms block can fall between two reads, and a missed
// RESET is a wrong bar. Why not libDaisy's GPIO interrupt mode: its
// GPIOInterruptHandler keeps one pin per EXTI channel, and PG13 and PG14 share
// EXTI15_10 -- the second registration would overwrite the first. And no
// handler of our own: libDaisy already defines EXTI15_10_IRQHandler
// (lib/libDaisy/src/per/gpio.cpp).
//
// The edge is the falling one at the pin: Rev A's gate input inverts, as
// libDaisy's GateIn assumes (invert defaults to true), so a rising edge at the
// jack is a falling edge at the pin. P7 confirms the direction on the board.
//
// Rev A play image only (SHELL_PANEL_SCAN=1, SHELL_COUPON_PROBE=0): the
// coupon's B9/B10 float with no pull resistor, and random edges would set wild
// tempos and restart songs. shell/Makefile builds this file into that image
// alone, and shell/test_clock_wiring.py guards every call site.

namespace shell {

struct GateEdges
{
    bool clock;   // at least one CLOCK edge since the last take
    bool reset;   // at least one RESET edge since the last take
};

// Once at boot, after the board is up (DaisyPatchSM::Init() has made B9/B10
// inputs): routes EXTI lines 13 and 14 to port G and arms falling-edge
// detection on them. The lines' interrupt and event masks stay closed, so no
// IRQ is ever requested and the NVIC is not touched.
void gate_latch_init();

// Clears both pending bits. Call immediately before StartAudio(), so edges
// from boot (pins settling, the codec coming up) never arrive as a pulse.
void gate_latch_arm();

// Once per block: reads both pending bits and clears the ones it saw. Several
// edges on one line within a block read as one.
GateEdges gate_latch_take();

} // namespace shell
