#include "fx/flux.h"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace spky;

int main() {
    static std::vector<float> L(kTapeSamples, 0.f), R(kTapeSamples, 0.f);
    Flux f;
    f.init(48000.f, L.data(), R.data());
    f.set_mix(1.f);            // full wet return
    f.set_feedback(0.5f);
    f.set_on(true, true);      // immediate on
    // feed a steady 200 Hz tone for 1 s so the tape line is full
    double ph = 0;
    for (int i = 0; i < 48000; ++i) {
        float s = 0.5f * (float)sin(ph); ph += 2*3.14159265358979*200.0/48000.0;
        float l = s, r = s;
        f.process(l, r);
    }
    // now switch off and feed SILENCE: the only thing left is the echo tail
    f.set_on(false);
    float prev = 0.f; float max_step = 0.f; int step_at = -1;
    for (int i = 0; i < 1000; ++i) {
        float l = 0.f, r = 0.f;
        f.process(l, r);
        if (i > 0) {
            float d = std::fabs(l - prev);
            if (d > max_step) { max_step = d; step_at = i; }
        }
        prev = l;
        if (i < 5 || (i > 185 && i < 200))
            printf("i=%3d  wet_l=% .5f\n", i, l);
    }
    printf("\nlargest sample-to-sample jump after FLUX off: %.5f at i=%d\n", max_step, step_at);
    return 0;
}
