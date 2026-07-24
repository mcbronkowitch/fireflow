#include <cstdint>

#include "rand_shim.h"

#if defined(__ARM_EABI__)
#define DTCM_RAND_BSS __attribute__((section(".dtcmram_bss")))
#else
#define DTCM_RAND_BSS
#endif

namespace {

uint32_t DTCM_RAND_BSS g_rand_state;
bool DTCM_RAND_BSS g_rand_seeded;

} // namespace

void bench::srand(unsigned int seed)
{
    g_rand_state = static_cast<uint32_t>(seed);
    g_rand_seeded = true;
}

int bench::rand()
{
    // C specifies the unseeded stream as though srand(1) ran first.
    if(!g_rand_seeded)
    {
        g_rand_state = 1u;
        g_rand_seeded = true;
    }
    g_rand_state = g_rand_state * 1664525u + 1013904223u;
    return static_cast<int>(g_rand_state & 0x7fffffffu);
}

#if defined(__ARM_EABI__)
extern "C" void srand(unsigned int seed)
{
    bench::srand(seed);
}

extern "C" int rand(void)
{
    return bench::rand();
}
#endif
