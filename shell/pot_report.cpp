#include "pot_report.h"

#include "pot_plan.h"
#include "shell_git_hash.h"

namespace shell {

void print_pot_lines(bench::Board& hw)
{
    // BYTE BUDGET: the ID line runs ~65 bytes at its widest, the CFG line
    // ~40 plus the hash, both far inside libDaisy's 125-byte payload
    // (logger.h:29). shell/pot_round.py parses both field sets exactly.
    hw.PrintLine("SHELL_POT_CFG round=1 pots=%d git=%s", kPotCount, SHELL_GIT_HASH);
    for(int i = 0; i < kPotCount; ++i)
    {
        const Pot& p = kPots[i];
        hw.PrintLine("SHELL_POT_ID idx=%d name=%s group=%d ch=%d r_track=%d "
                     "hi=%d lo=%d",
                     i, p.name, p.group, p.channel,
                     static_cast<int>(p.r_track_ohm), p.hi_ch, p.lo_ch);
    }
}

} // namespace shell
