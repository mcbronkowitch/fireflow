#pragma once

// Round four's two announcement lines: SHELL_POT_CFG, then one SHELL_POT_ID
// per pot. Shared by the settle and the wait probe so the two images cannot
// describe the same pots differently. Each probe prints them DIRECTLY AFTER
// its own _CFG line -- both readers open a block at that line and drop
// anything before it (spec 2026-09-28-coupon-pot-round-design.md section 6).
#include "hw/board.h"

namespace shell {

void print_pot_lines(bench::Board& hw);

} // namespace shell
