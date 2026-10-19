// CLOCK / RESET edges in the EXTI pending bits: see gate_latch.h.
//
// Register sequence (STM32H750, RM0433: SYSCFG and EXTI chapters). The CMSIS
// names are the single-core view of the EXTI: EXTI->IMR1 / EMR1 / PR1 at
// offsets 0x80 / 0x84 / 0x88 are RM0433's EXTI_CPUIMR1 / CPUEMR1 / CPUPR1.
#include "gate_latch.h"

#include <cstdint>

#include "daisy_patch_sm.h"
#include "stm32h7xx_hal.h"

namespace shell {

namespace {

using daisy::patch_sm::DaisyPatchSM;

// Gate In 1 is CLOCK, Gate In 2 is RESET (P2 pin map, spec section 3). EXTI
// line n serves pin n of whichever port SYSCFG_EXTICRx selects; the code
// below selects port G for exactly these two lines.
static_assert(DaisyPatchSM::B10.port == daisy::PORTG && DaisyPatchSM::B10.pin == 13,
              "CLOCK (Gate In 1) must be PG13");
static_assert(DaisyPatchSM::B9.port == daisy::PORTG && DaisyPatchSM::B9.pin == 14,
              "RESET (Gate In 2) must be PG14");

constexpr uint32_t kClockLine = 1u << DaisyPatchSM::B10.pin;
constexpr uint32_t kResetLine = 1u << DaisyPatchSM::B9.pin;
constexpr uint32_t kLines     = kClockLine | kResetLine;

} // namespace

void gate_latch_init()
{
    // SYSCFG_EXTICR4 holds the port selection of EXTI lines 12..15, four bits
    // each: line 13 in [7:4], line 14 in [11:8]. Port G is code 6. The
    // register needs the SYSCFG clock (RCC_APB4ENR.SYSCFGEN); the EXTI itself
    // has no clock enable.
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    uint32_t cr = SYSCFG->EXTICR[3];
    cr &= ~(SYSCFG_EXTICR4_EXTI13 | SYSCFG_EXTICR4_EXTI14);
    cr |= SYSCFG_EXTICR4_EXTI13_PG | SYSCFG_EXTICR4_EXTI14_PG;
    SYSCFG->EXTICR[3] = cr;

    // Falling edge only (EXTI_FTSR1), no rising edge (EXTI_RTSR1).
    EXTI->RTSR1 &= ~kLines;
    EXTI->FTSR1 |= kLines;

    // No interrupt and no event: CPUIMR1 and CPUEMR1 masked for these lines,
    // written here so the state depends neither on reset values nor on
    // whoever ran before. The NVIC's EXTI15_10 line is
    // left as it is: nothing in this firmware enables it, and with the line
    // masked no request reaches it.
    EXTI->IMR1 &= ~kLines;
    EXTI->EMR1 &= ~kLines;

    // CPUPR1 is write-1-to-clear.
    EXTI->PR1 = kLines;
}

void gate_latch_arm()
{
    EXTI->PR1 = kLines;
}

GateEdges gate_latch_take()
{
    // Clear only what was read: an edge on the other line between the read
    // and the write stays pending for the next block.
    const uint32_t pr = EXTI->PR1 & kLines;
    if(pr != 0u) EXTI->PR1 = pr;
    return GateEdges{(pr & kClockLine) != 0u, (pr & kResetLine) != 0u};
}

} // namespace shell
