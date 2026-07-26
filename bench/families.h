#pragma once

#include "workload.h"

namespace bench {

// One workload family: a name, its row table, and how many rows it has.
//
// This registry is the ONLY place the set of families is written down inside
// the firmware. Before it existed, main.cpp carried seven hand-written loops
// and runner.cpp carried two parallel arrays plus a literal 7 -- two copies of
// the same list, which is exactly what a compile-time family switch must not
// have.
//
// Registry order IS execution order, and must not depend on link order
// (workload.h's contract). Keep `sampler` last: its rows overwrite the SDRAM
// arena earlier families load into -- see main.cpp's note.
struct Family {
    const char*     name;
    const Workload* rows;
    int             count;
};

extern const Family kFamilies[];
extern const int    kFamilyCount;

// Space-separated family names in registry order, e.g. "system voice".
// Emitted in the BENCH_BEGIN header so the host can prove the image it
// measured is the image it asked for.
const char* families_csv();

} // namespace bench
