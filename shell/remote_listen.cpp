// The SHELL_REMOTE listener: see remote_listen.h. Built with SHELL_REMOTE=1
// and, unchanged, as the play image's boot-only listener (SHELL_BOOTCMD); the
// callers choose what it does, so this file reads no switch header.
#include "remote_listen.h"

#include <cstdint>
#include <daisy_seed.h>        // libDaisy (System, UsbHandle) and the CMSIS core
#include "shell_git_hash.h"    // generated: SHELL_GIT_HASH (write_git_hash.py)

// The application's vector table (libDaisy core/startup_stm32h750xx.c),
// `void* g_pfnVectors[0xa6]` in section .isr_vector.
extern "C" void* g_pfnVectors[];

extern "C" {

// The C half of the fault entry.
[[noreturn]] void shell_remote_fault_reset()
{
    shell::remote_enter_bootloader();
}

// What NMI, HardFault, MemManage, BusFault and UsageFault vector to. The main
// stack pointer goes back to the top of DTCM (_estack, alt_sram.lds) before
// any C runs, so the C half never runs on whatever the faulting code left of
// the stack. Naked: no prologue may touch the old stack first. What this
// covers of a stack overflow is narrow: the core pushes its 8-word exception
// frame onto the old stack BEFORE this entry runs, so the entry is reached
// only when that push lands in writable memory. A stack pointer already
// outside it (nothing here guards the stack's end) faults the push itself,
// and the core locks up without ever getting here (spec section 8).
__attribute__((naked, noreturn)) void shell_remote_fault_entry()
{
    __asm volatile(
        "movw r0, #:lower16:_estack \n"
        "movt r0, #:upper16:_estack \n"
        "msr  msp, r0               \n"
        "isb                        \n"
        "b    shell_remote_fault_reset \n");
}

} // extern "C"

namespace shell {

namespace {

// The most commands one USB packet can complete. A full-speed bulk packet is
// at most 64 bytes, and the shortest valid line is three characters plus
// '\n' (RST, TOG and the rest are longer), so a packet holds 64 / 4 = 16
// whole lines -- or one line begun in the previous packet, whose '\n' costs
// a single byte here, plus 63 / 4 = 15 more: 16 either way. Sized for that,
// so a BOOT anywhere in a packet is seen, never dropped by
// LineAssembler::feed() for want of room (it drops what does not fit).
constexpr int      kMaxCmdsPerPacket = 16;
constexpr uint32_t kFsPacketBytes    = 64;

// The vector table: 16 core exceptions plus the STM32H750's 150 interrupt
// lines, WAKEUP_PIN_IRQn (149) the last. The same 0xa6 words as
// g_pfnVectors.
constexpr uint32_t kVectorWords = 16u + static_cast<uint32_t>(WAKEUP_PIN_IRQn) + 1u;
static_assert(kVectorWords == 0xa6u, "the startup file's g_pfnVectors has 0xa6 entries");
// VTOR needs the table aligned to its size rounded up to a power of two:
// 166 words = 664 bytes, so 1 KB.
static_assert(kVectorWords * 4u <= 1024u, "the RAM vector table outgrew its 1 KB alignment");

// In DTCM: zero-wait-state and outside the data cache, so the copy is visible
// to the next exception entry with no cache maintenance, and the table does
// not move any of the AXI SRAM objects whose addresses the CPU figures were
// measured with.
__attribute__((section(".dtcmram_bss"), aligned(1024))) uint32_t g_ram_vectors[kVectorWords];

LineAssembler g_assembler;   // the USB interrupt's alone
CmdRing       g_ring;        // USB interrupt -> audio callback
volatile bool g_ping = false;

// libDaisy's CDC receive callback: runs in the USB interrupt (NVIC priority
// 0). It may not wait on SysTick (priority 14), print, or spin.
void on_rx(uint8_t* buf, uint32_t* len)
{
    Command cmds[kMaxCmdsPerPacket];
    const uint32_t total = *len;
    // In packet-sized pieces, so the bound above holds even if a callback
    // were ever handed more than one packet.
    for(uint32_t off = 0; off < total; off += kFsPacketBytes)
    {
        const uint32_t n   = (total - off < kFsPacketBytes) ? total - off : kFsPacketBytes;
        const int      got = g_assembler.feed(buf + off, n, cmds, kMaxCmdsPerPacket);
        for(int i = 0; i < got; ++i)
        {
            if(cmds[i].kind == CmdKind::Boot)
                remote_enter_bootloader();   // here, not in the foreground
            else if(cmds[i].kind == CmdKind::Ping)
                g_ping = true;               // the foreground prints the pong
            else
                (void)g_ring.push(cmds[i]);  // full: dropped
        }
    }
}

} // namespace

void remote_listen_start()
{
    // UsbHandle has no state (libDaisy's logger relies on the same fact,
    // hid/logger_impl.h), so a temporary reaches the CDC port the log opened.
    daisy::UsbHandle{}.SetReceiveCallback(&on_rx, daisy::UsbHandle::FS_INTERNAL);
}

bool remote_poll(Command* c)
{
    return g_ring.pop(c);
}

bool remote_take_ping()
{
    if(!g_ping) return false;
    g_ping = false;
    return true;
}

const char* remote_build()
{
    return SHELL_GIT_HASH;
}

void remote_enter_bootloader()
{
    __disable_irq();
    // board_init() does this first thing; a fault before it (the vectors are
    // live from main()'s first statement) would otherwise leave the backup
    // SRAM write-protected, and the bootloader would not see the request.
    // Idempotent, and a register poll that ends at once -- not a timed wait.
    daisy::System::InitBackupSram();
    // What daisy::System::ResetToBootloader(DAISY_INFINITE_TIMEOUT) does
    // (lib/libDaisy/src/sys/system.cpp:306-358), written out here instead of
    // called. That function is one body for both bootloaders: its STM branch
    // waits HAL_Delay(10) (line 320), which never returns inside a priority-0
    // handler, and calls GPIO::Init, which this image runs from QSPI
    // (qspi_cold.ld) -- qspi_placement.py refuses the link once an interrupt
    // or fault handler can reach it, branch taken or not. Its DAISY branch is
    // these three steps; the one it skips here, "return if running from
    // internal flash" (GetProgramMemoryRegion() on SCB->VTOR), cannot apply
    // to a BOOT_SRAM image.
    daisy::boot_info.status = daisy::System::BootInfo::Type::INF_TIMEOUT;
    RCC->CIER = 0x00000000;
    // The request has to be in memory when the reset hits, and a reset does
    // not write a dirty D-cache line back (final review M1). Inline CMSIS
    // (set / way loop, then DSB / ISB): no QSPI code, no SysTick, and safe
    // with the cache still off (a fault before board_init()).
    SCB_CleanDCache();
    NVIC_SystemReset();
}

void remote_install_fault_vectors()
{
    const uint32_t* from = reinterpret_cast<const uint32_t*>(g_pfnVectors);
    for(uint32_t i = 0; i < kVectorWords; ++i) g_ram_vectors[i] = from[i];
    // A Thumb function's address carries bit 0 already.
    const uint32_t entry = reinterpret_cast<uint32_t>(&shell_remote_fault_entry);
    // NMI too (final review M8): libDaisy aliases NMI_Handler to its
    // Default_Handler (core/startup_stm32h750xx.c), which never returns.
    g_ram_vectors[16 + NonMaskableInt_IRQn]   = entry;
    g_ram_vectors[16 + HardFault_IRQn]        = entry;
    g_ram_vectors[16 + MemoryManagement_IRQn] = entry;
    g_ram_vectors[16 + BusFault_IRQn]         = entry;
    g_ram_vectors[16 + UsageFault_IRQn]       = entry;
    __DSB();
    SCB->VTOR = reinterpret_cast<uint32_t>(g_ram_vectors);
    __DSB();
    __ISB();
}

} // namespace shell
