#pragma once

// The ITCM loader (itcm_load.cpp, itcm_hot.ld). Built with SHELL_ITCM=1
// (shell/Makefile, the default; a switch header, so flipping it rebuilds).
// At SHELL_ITCM=0 it is an inline that copies nothing and the hot set stays
// in SRAM: the comparison image of the ITCM measurement (spec
// 2026-10-18-cpu-headroom-design.md section 4).

#include "shell_itcm.h"   // generated: SHELL_ITCM 0 or 1 (write_shell_itcm.py)

namespace shell {

// Copies the ITCM code out of QSPI. false: the QSPI image is not this build's
// (its link stamp differs), and nothing in ITCM may be run.
#if SHELL_ITCM
bool itcm_load();
#else
inline bool itcm_load() { return true; }
#endif

} // namespace shell
