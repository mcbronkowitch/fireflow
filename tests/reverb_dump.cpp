// Renders 6 s of the reverb (a 0.5 s noise burst into a room at decay 0.7,
// size 0.6, TONE 0.5, then the tail) and writes the wet left / right samples as
// raw floats to the file named on the command line. Built four ways by CMake:
//  - reverb_dump_dispatch: SPKY_TESTING, set_half_rate(true) after init() --
//    the variant the desktop tests (test_cpu_hunt_equiv.cpp) exercise;
//  - reverb_dump_only: SPKY_REVERB_HALF_ONLY, the variant SHELL_REVERB_HALF=1
//    compiles into the firmware (no setter, init() puts the room in the
//    half-rate tank).
//  - reverb_dump_dispatch_full / reverb_dump_plain (no REVERB_DUMP_SET_HALF): the
//    dispatch variant left at full rate, and the plain build with none of the
//    defines that VCV, the render host and SHELL_REVERB_HALF=0 compile.
// The ctest `reverb_half_variants` runs the first two and `reverb_full_rate_variants`
// the last two, each requiring the two files to be
// identical, so what the tests check is the code that ships.
#include <cstdint>
#include <cstdio>
#include <cmath>
#include "fx/reverb.h"

using namespace spky;

static AmbientReverb g_rev;

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    g_rev.init(48000.f);
    g_rev.set_decay(0.7f);
    g_rev.set_size(0.6f);
    g_rev.set_tone(0.5f);
#if defined(REVERB_DUMP_SET_HALF)
    g_rev.set_half_rate(true);
#endif
    FILE* f = std::fopen(argv[1], "wb");
    if (!f) return 3;
    uint32_t s = 2463534242u;
    double energy = 0.0;
    for (int i = 0; i < 48000 * 6; ++i) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        const float x = i < 24000 ? 0.3f * ((float)(int32_t)s / 2147483648.f) : 0.f;
        float l, r;
        g_rev.process(x, x, l, r);
        const float lr[2] = { l, r };
        std::fwrite(lr, sizeof(float), 2, f);
        energy += (double)l * l + (double)r * r;
    }
    std::fclose(f);
    return energy > 1e-3 && std::isfinite(energy) ? 0 : 4;   // a silent room is a failure
}
