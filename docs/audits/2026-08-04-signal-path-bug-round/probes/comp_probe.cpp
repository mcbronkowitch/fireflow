#include "fx/comp.h"
#include <initializer_list>
#include <cstdio>
#include <cmath>
using namespace spky;
static float run(float amt, float amp){
    Comp c; c.init(48000.f); c.set_amount(amt);
    double ph=0; float pk=0;
    for(int i=0;i<48000;++i){ float s=amp*(float)sin(ph); ph+=2*3.14159265358979*220.0/48000.0;
        float l=s,r=s; c.process(l,r); if(i>24000 && std::fabs(l)>pk) pk=std::fabs(l); }
    return pk;
}
int main(){
    float amp=0.8f;
    printf("input peak %.3f\n", amp);
    for (float a : {0.f, 0.001f, 0.01f, 0.05f, 0.2f, 0.5f, 1.f})
        printf("  COMP %.3f -> out peak %.4f  (%+.2f dB vs input)\n",
               a, run(a,amp), 20*log10(run(a,amp)/amp));
    // and the snap: knob rides to exactly 0 while a hot tone plays
    Comp c; c.init(48000.f); c.set_amount(0.5f);
    double ph=0; float prev=0;
    for(int i=0;i<48000;++i){ float s=amp*(float)sin(ph); ph+=2*3.14159265358979*220.0/48000.0;
        float l=s,r=s; c.process(l,r); }
    c.set_amount(0.f);
    float worst=0; int at=-1; float before=0, after=0;
    for(int i=0;i<2000;++i){ float s=amp*(float)sin(ph); ph+=2*3.14159265358979*220.0/48000.0;
        float l=s,r=s; float dry=l; c.process(l,r);
        float g = l/ (dry==0?1e-9f:dry);
        if(i){ float d=std::fabs(g-prev); if(d>worst){worst=d;at=i;before=prev;after=g;} }
        prev=g; }
    printf("\nCOMP 0.5 -> 0 on a hot tone: largest one-sample GAIN jump %.4f at i=%d (%.4f -> %.4f, %+.1f dB)\n",
           worst, at, before, after, 20*log10(after/before));
}
