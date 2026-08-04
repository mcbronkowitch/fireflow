#include "fx/reverb.h"
#include "util/onepole.h"
#include <cstdio>
#include <cmath>
using namespace spky;
static AmbientReverb rev;
static void run(float decay, float mixknob){
    const float sr=48000.f; rev.init(sr); rev.set_decay(decay);
    OnePole wet; wet.init(sr,0.010f);
    const float hp=1.57079632679489661923f;
    float wt = mixknob>=1.f?1.f:std::sin(mixknob*hp); wet.reset(wt);
    double ph=0;
    for(int i=0;i<144000;++i){ float s=0.5f*(float)sin(ph); ph+=2*3.14159265358979*220.0/sr;
        float wg=wet.process(wt); float wl,wr; rev.process(s*wg,s*wg,wl,wr); }
    wt=0.f;
    for(int i=0;i<40000;++i){ float wg=wet.process(wt); float wl=0,wr=0;
        rev.process(0.f,0.f,wl,wr);
        if(wg==0.f){ printf("DECAY %.2f MIX %.2f -> cut %5.1f ms after the knob, return level %.4f (%.1f dBFS)\n",
                            decay, mixknob, i/48.f, std::fabs(wl), 20*log10(std::fabs(wl)+1e-12)); return; } }
}
int main(){ run(0.55f,0.25f); run(0.75f,0.5f); run(0.85f,1.f); run(0.95f,1.f); }
