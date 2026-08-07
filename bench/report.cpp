#include "report.h"
#include <cstdio>
#include <cstdarg>
#include <daisy_seed.h>   // daisy::System::GetSysClkFreq(), CMSIS SCB/SCB_CCR_*_Msk

#include "bench_layout.h"
#include "bench_optimization.h"
#include "bench_transport.h"

#if defined(BENCH_TRANSPORT_USB)
#include "hid/logger.h"

// libDaisy declares these `extern const char*` in src/usbd/usbd_desc.c and
// never defines them -- the application owns its own USB identity. Nothing in
// this repository defined them before, because nothing here had ever brought
// up a USB device class; the shipping firmware logs over LOGGER_EXTERNAL.
// Without these two the USB branch fails at link, not at runtime, which is
// the good outcome.
extern "C" {
const char* USBD_MANUFACTURER_STRING = "FireFlow";
const char* USBD_PRODUCT_STRING_HS   = "FireFlow Bench";
}
#endif

#ifndef BENCH_LAYOUT
#define BENCH_LAYOUT "unknown"
#endif

#ifndef BENCH_OPTIMIZATION
#define BENCH_OPTIMIZATION "unknown"
#endif

namespace bench {
namespace {

#define DTCM_REPORT_BSS __attribute__((section(".dtcmram_bss")))
#define DTCM_REPORT_TEXT __attribute__((section(".dtcmram_text")))
#define DTCM_REPORT_RODATA __attribute__((section(".dtcmram_rodata")))
#define SRAM_REPORT_LAYOUT_GUARD \
    __attribute__((section(".sram_exec_layout_guard"), used))

#if defined(BENCH_TRANSPORT_USB)

using BenchLogger = daisy::Logger<daisy::LOGGER_INTERNAL>;

// USB-CDC instead of semihosting. Semihosting stopped the core outright --
// crude, but honest, and impossible to get subtly wrong. This line is
// interrupt-driven, so it may run BETWEEN workloads only, never inside a
// measured window. That was always the rule; here it stops being a courtesy
// and becomes a correctness condition.
inline void transport_write0(const char* s)
{
    BenchLogger::PrintLine("%s", s);
}

inline void open_line()
{
    // true: block until the host opens the port. Without it the firmware
    // runs ahead of anyone listening and the BENCH_BEGIN header is simply
    // gone -- a run that happened and is worthless anyway.
    BenchLogger::StartLog(true);
}

#else

// ARM semihosting SYS_WRITE0: r0 = op, r1 = pointer to a NUL-terminated
// string. The bkpt is the call. This is the whole transport.
constexpr int kSysWrite0 = 0x04;

inline void transport_write0(const char* s)
{
    register int         r0 asm("r0") = kSysWrite0;
    register const char* r1 asm("r1") = s;
    asm volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
}

// Nothing to open: openocd is already attached, or the first bkpt hangs.
inline void open_line() {}

#endif

// Reporting is outside measured windows. Preserve the proven 256-byte format
// capacity while using roomy DTCM rather than scarce AXI SRAM.
char DTCM_REPORT_BSS g_buf[256];

// Moving reporting into DTCM and replacing newlib's heap-backed rand() both
// shrink AXI text. This bench-only NOLOAD reservation keeps the accepted
// Task 8 measurement-object addresses and cache alignment; shipping code has
// no input section and pays nothing.
uint8_t SRAM_REPORT_LAYOUT_GUARD g_axi_layout_guard[0xcc8];

} // namespace

void transport_open()
{
    open_line();
}

void log_line(const char* s)
{
    transport_write0(s);
}

void logf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_buf, sizeof(g_buf), fmt, ap);
    va_end(ap);
    transport_write0(g_buf);
}

// Ceiling: g_buf[256] (declared above) is shared by every logf() call, and
// kBeginFormat below is the widest user of it. Its fixed fields --
// "BENCH_BEGIN,", githash, the clock (up to 10 digits), "96", the cache
// string, a 64-hex-char QSPI digest, a 24-hex-char UID, the layout, and the
// format's own commas/newline -- consume roughly 135 of those 256 bytes, leaving
// 128 for `families`. That is the same order of magnitude as, and
// independent of, families.cpp's own 128-byte families_csv() ceiling (see
// the comment there); either one truncating silently still leaves a
// syntactically valid BENCH_BEGIN line, so the failure surfaces downstream
// and unhelpfully -- as run.py rejecting a families mismatch -- rather than
// here. If a future family or a longer githash prefix makes this tight,
// widen g_buf (and DTCM has room: it's at 6.56% per bench/README.md).
void DTCM_REPORT_TEXT report_begin(
    const char* githash,
    const char* qspi_sha256,
    const char* families)
{
    // Measured, not asserted: this is exactly the failure mode that already
    // bit this project once (the plan claimed 480 MHz while hw.Init() was
    // actually delivering something else). Read the real clock and the real
    // cache-enable bits so the header line describes what is actually true.
    const uint32_t clk = daisy::System::GetSysClkFreq();
    const uint32_t ccr = SCB->CCR;
    const bool     ic  = (ccr & SCB_CCR_IC_Msk) != 0;
    const bool     dc  = (ccr & SCB_CCR_DC_Msk) != 0;
    const char* cache = (ic && dc) ? "dcache+icache"
                      : ic         ? "icache"
                      : dc         ? "dcache"
                                   : "none";
    const auto* uid = reinterpret_cast<const uint32_t*>(UID_BASE);
    static const char DTCM_REPORT_RODATA kBeginFormat[] =
        "BENCH_BEGIN,%s,%lu,96,%s,%s,%08lx%08lx%08lx,%s,%s,%s\n";
    logf(kBeginFormat,
         githash,
         (unsigned long)clk,
         cache,
         qspi_sha256,
         static_cast<unsigned long>(uid[0]),
         static_cast<unsigned long>(uid[1]),
         static_cast<unsigned long>(uid[2]),
         families,
         BENCH_LAYOUT,
         BENCH_OPTIMIZATION);
}

void report_end()
{
    log_line("BENCH_END\n");
}

} // namespace bench

namespace {
// Percent of the block budget in hundredths, printed as a fixed-point pair.
// Integer maths keeps the output exact and keeps float formatting (and its
// newlib bulk) out of the binary.
inline uint32_t pct_x100(uint32_t cyc)
{
    return static_cast<uint32_t>((static_cast<uint64_t>(cyc) * 10000ull)
                                 / bench::kBudgetCycles);
}
} // namespace

namespace bench {

void report_row(const Workload& w, const Result& r)
{
    if (r.timed_out) {
        logf("BENCH,%s,%s,TIMEOUT,%lu,,,%08lx\n",
             w.family, w.name,
             (unsigned long)r.max_cyc, (unsigned long)r.checksum);
        return;
    }
    const uint32_t pa = pct_x100(r.avg_cyc);
    const uint32_t pm = pct_x100(r.max_cyc);
    logf("BENCH,%s,%s,%lu,%lu,%lu.%02lu,%lu.%02lu,%08lx\n",
         w.family, w.name,
         (unsigned long)r.avg_cyc, (unsigned long)r.max_cyc,
         (unsigned long)(pa / 100), (unsigned long)(pa % 100),
         (unsigned long)(pm / 100), (unsigned long)(pm % 100),
         (unsigned long)r.checksum);
}

void report_anchor(const char* name, uint32_t avg_x100, uint32_t max_x100)
{
    logf("ANCHOR,%s,%lu.%02lu,%lu.%02lu\n",
         name,
         (unsigned long)(avg_x100 / 100), (unsigned long)(avg_x100 % 100),
         (unsigned long)(max_x100 / 100), (unsigned long)(max_x100 % 100));
}

} // namespace bench
