#include "fx/fx_util.h"
#include <cstdio>
using namespace spky;
static void ramp(float sr) {
    SoftSwitch s; s.init(sr);
    s.set_on(true);
    printf("--- sr=%g rise ---\n", sr);
    float prev = 0.f, worst = 0.f; int at = -1;
    for (int i = 0; i < 260; ++i) {
        float v = s.process();
        if (i && std::fabs(v - prev) > worst) { worst = std::fabs(v-prev); at = i; }
        prev = v;
        if (i < 3 || (i > 186 && i < 196)) printf("  i=%3d v=%.6f\n", i, v);
    }
    printf("  largest single-sample step during rise: %.6f at i=%d\n", worst, at);
}
int main(){ ramp(44100.f); ramp(48000.f); ramp(96000.f); }
