#include "scan_check.h"

#include "shell_scan_check.h"
#include "shell_coupon_probe.h"

#if SHELL_SCAN_CHECK

#include <atomic>
#include <cstdint>

#include "mux_scan.h"
#include "scan_check_plan.h"
#include "shell_git_hash.h"

namespace shell {

static_assert(!kActiveChain.parallel_sense,
              "scan_check walks the sequential model; build it without SHELL_PANEL_SCAN");
static_assert(scan_steps(kActiveChain) == kCheckSteps,
              "SHELL_SCAN_CHECK walks the coupon's 24 steps; the Makefile "
              "requires SHELL_COUPON_PROBE=1");

namespace {

struct Acc
{
    uint32_t n;
    uint32_t sum;    // 64 * 65535 < 2^32
    uint16_t min;
    uint16_t max;
};

struct RunBuf
{
    Acc      acc[kArmCount][kCheckSteps];
    uint32_t ticks;
};

MuxScan g_scan;

// Double buffer: the callback fills g_buf[g_fill]; at the end of a run
// block it publishes that buffer (g_ready, then g_done_blk) and starts the
// other. The foreground prints the published one; it has a whole run block,
// ~9.46 s, before the callback comes back to it.
RunBuf       g_buf[2];
int          g_fill  = 0;
int          g_block = 0;
int          g_run   = 0;
volatile int g_ready    = -1;
volatile int g_done_blk = -1;

void reset(RunBuf& b)
{
    for(int a = 0; a < kArmCount; ++a)
        for(int s = 0; s < kCheckSteps; ++s)
            b.acc[a][s] = Acc{0u, 0u, 0xFFFFu, 0u};
    b.ticks = 0u;
}

// The raw word of the step's live pin. On the coupon each group is wired to
// exactly one sense pin (sense_of_group), so there is exactly one.
uint16_t live_raw(int step)
{
    const int g     = group_of_step(kActiveChain, step);
    const int sense = kActiveChain.sense_of_group[g];
    return g_mux_raw[mux_channel(kActiveChain, step, sense)];
}

void read_into(bench::Board& hw, RunBuf& b, ScanArm arm, int step)
{
    g_scan.read_step(hw, step);
    const uint16_t raw = live_raw(step);
    Acc&           a   = b.acc[static_cast<int>(arm)][step];
    ++a.n;
    a.sum += raw;
    if(raw < a.min) a.min = raw;
    if(raw > a.max) a.max = raw;
}

} // namespace

void scan_check_init()
{
    g_scan.init();
    g_scan.set_walk_leds(false);
    reset(g_buf[0]);
    reset(g_buf[1]);
}

void scan_check_tick(bench::Board& hw)
{
    RunBuf&         b = g_buf[g_fill];
    const CheckSlot s = check_slot(g_block);

    if(!s.read_after_select && s.read_step >= 0)
        read_into(hw, b, s.arm, s.read_step);
    if(s.select_step >= 0) g_scan.select(s.select_step);
    if(s.read_after_select && s.read_step >= 0)
        read_into(hw, b, s.arm, s.read_step);

    ++b.ticks;
    if(++g_block >= kRunBlocks)
    {
        g_block = 0;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        g_ready    = g_fill;
        g_done_blk = g_run++;
        g_fill     = 1 - g_fill;
        reset(g_buf[g_fill]);
    }
}

void run_scan_check_report(bench::Board& hw)
{
    hw.StartLog(false);
    int printed = -1;
    while(1)
    {
        const int blk = g_done_blk;
        if(blk == printed) continue;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const RunBuf& b = g_buf[g_ready];

        hw.PrintLine("SHELL_SCAN_CFG blk=%d block=%d sr=%d steps=%d git=%s",
                     blk, static_cast<int>(hw.AudioBlockSize()),
                     static_cast<int>(hw.AudioSampleRate()), kCheckSteps,
                     SHELL_GIT_HASH);
        for(int a = 0; a < kArmCount; ++a)
            for(int s = 0; s < kCheckSteps; ++s)
            {
                const Acc& acc = b.acc[a][s];
                hw.PrintLine(
                    "SHELL_SCAN_CH blk=%d arm=%c step=%d group=%d ch=%d n=%d "
                    "sum=%d min=%d max=%d",
                    blk, arm_letter(static_cast<ScanArm>(a)), s,
                    group_of_step(kActiveChain, s),
                    static_cast<int>(step_pattern(kActiveChain, s).address),
                    static_cast<int>(acc.n), static_cast<int>(acc.sum),
                    static_cast<int>(acc.min), static_cast<int>(acc.max));
            }
        hw.PrintLine("SHELL_SCAN_HEALTH blk=%d ticks=%d expected=%d", blk,
                     static_cast<int>(b.ticks), kRunBlocks);
        hw.PrintLine("SHELL_SCAN_END blk=%d", blk);
        printed = blk;
    }
}

} // namespace shell

#endif // SHELL_SCAN_CHECK
