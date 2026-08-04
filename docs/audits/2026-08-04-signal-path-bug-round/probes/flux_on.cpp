#include "fx/flux.h"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace spky;
int main(){
    static std::vector<float> L(kTapeSamples,0.f), R(kTapeSamples,0.f);
    Flux f; f.init(48000.f, L.data(), R.data());
    f.set_mix(1.f); f.set_feedback(0.5f);
    f.set_on(true, true);
    double ph=0;
    for (int i=0;i<48000;++i){ float s=0.5f*(float)sin(ph); ph+=2*3.14159265358979*200.0/48000.0;
        float l=s,r=s; f.process(l,r); }
    f.set_on(false);
    for (int i=0;i<400;++i){ float l=0,r=0; f.process(l,r); }   // now idle, line frozen
    // ... 10 seconds of silence pass, FLUX untouched (process still called, early-returns)
    for (int i=0;i<480000;++i){ float l=0,r=0; f.process(l,r); }
    printf("FLUX is off; 10 s of silence have passed.\n");
    f.set_on(true);   // player turns FLUX back on, still feeding silence
    float mx=0.f; int at=-1;
    for (int i=0;i<2000;++i){ float l=0,r=0; f.process(l,r);
        if (std::fabs(l)>mx){mx=std::fabs(l);at=i;}
        if (i<6 || (i>=190 && i<196)) printf("  i=%4d wet_l=% .5f\n", i, l); }
    printf("\npeak of the resurrected tail in the first 2000 samples: %.5f at i=%d\n", mx, at);
}
