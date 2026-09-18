#include "xtalk_probe.h"

#include "cycles.h"
#include "shell_git_hash.h"
// SHELL_XTALK_RV4 is read below and lives in the generated switch
// header beside SHELL_XTALK_PROBE, so this translation unit needs it
// directly -- main.cpp's include of it does not reach here. The
// Makefile carries the matching edge on xtalk_probe.o.
#include "shell_xtalk_probe.h"
#include "xtalk_plan.h"

namespace shell {

void run_xtalk_probe(bench::Board& hw)
{
    cycles_init();
    hw.StartLog(false);

    // Read, not assumed: the CPU probe's own comment in main.cpp records the
    // day a block size was inferred from phase durations (48) and was in
    // fact 96. Both go into scan_settle_ns(), and both are printed so the
    // derivation is checkable from the capture alone.
    const int block_size = static_cast<int>(hw.AudioBlockSize());
    const int sr_hz      = static_cast<int>(hw.AudioSampleRate());

    while(1)
    {
        // The configuration line comes FIRST and the end marker always
        // arrives, even on a pass that measured nothing. A reader that can
        // only recognise a complete block is the point: a truncated one must
        // be discarded rather than half-believed.
        //
        // BYTE BUDGET. libDaisy's log buffer is 128 bytes
        // (lib/libDaisy/src/hid/logger.h:29). MEASURED on the board
        // 2026-09-18: this line is 115 characters, 117 with CRLF. Do not add
        // a field to it; SHELL_XTALK_GATES has room.
        //
        // The 11 bytes of margin are not all spare. DERIVED, not measured:
        // Task 5's real adc_khz replaces "-1" with 4 digits and costs 2 of
        // them (117 -> 119) -- and 4 digits is itself an assumption about a
        // clock nobody has printed yet. A 5-digit reading costs a third
        // byte. Whoever measures it re-counts this line instead of trusting
        // this comment.
        //
        // AND THE LIMIT IS NOT PER LINE, which is the part that misleads.
        // logger.cpp:78-87: when impl_.Transmit() returns false because the
        // host is not draining, tx_ptr_ is deliberately NOT reset and the
        // next PrintLine's vsnprintf appends into the same 128-byte buffer.
        // The "$$" at tx_buff_[126..127] is therefore an ACCUMULATION
        // overflow across lines, not a single line that grew too long. A
        // whole block is 197 bytes, so a stalled host truncates and fuses
        // lines ANYWHERE in it; shortening this line does not buy immunity,
        // it only moves where the damage lands. That is why the reader has
        // to drop an incomplete block rather than repair one.
        //
        // adc_khz is -1 because THIS image measures nothing: Task 5 brings
        // the ADC clock measurement in. A -1 here is an empty block by
        // design, not an ADC that failed to calibrate.
        hw.PrintLine("SHELL_XTALK_CFG adc_khz=%d repeats=%d grid_ns=%d "
                     "points=%d park_ns=%d scan_settle_ns=%d rv4=%d git=%s",
                     -1, kRepeats, kGridStepNs, kGridPoints,
                     static_cast<int>(kParkNs),
                     static_cast<int>(scan_settle_ns(block_size, sr_hz)),
                     SHELL_XTALK_RV4, SHELL_GIT_HASH);
        // block_size and sr are the inputs to the field above. They ride on
        // their own line rather than in CFG because CFG has no room left,
        // and a derived duration whose inputs are not in the capture cannot
        // be checked by anyone reading it later.
        hw.PrintLine("SHELL_XTALK_RATE block_size=%d sr_hz=%d cases=%d victims=%d",
                     block_size, sr_hz, kXtalkCases, kXtalkVictims);
        hw.PrintLine("SHELL_XTALK_END");
        hw.Delay(1000);
    }
}

} // namespace shell
