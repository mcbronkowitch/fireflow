#pragma once

namespace bench {

void srand(unsigned int seed);
int rand();

} // namespace bench

#if defined(__ARM_EABI__)
extern "C" void srand(unsigned int seed);
#endif
