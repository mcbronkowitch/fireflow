#include "fx/limiter.h"
#include <cstdio>
#include <cmath>
using namespace spky;
int main(){
    Limiter lim; lim.init(); lim.set_drive(0.f);
    // quiet program for a while, then a sudden loud onset (a stab landing on
    // top of a wash) -- exactly what the limiter's peak follower cannot see coming
    double ph=0; const double w=2*3.14159265358979*220.0/48000.0;
    for(int i=0;i<48000;++i){ float s=0.05f*(float)sin(ph); ph+=w; float l=s,r=s; lim.process(l,r); }
    printf("onset: input 1.4 peak sine after a -26 dBFS wash\n");
    float worst_err=0; int flat=0;
    for(int i=0;i<400;++i){
        float s=1.4f*(float)sin(ph); ph+=w; float l=s,r=s; lim.process(l,r);
        float err = std::fabs(l) - std::fabs(s);
        if (std::fabs(l) > 0.999f) ++flat;
        if (i<6 || (i%40==0)) printf("  i=%3d in=% .4f out=% .4f\n", i, s, l);
    }
    printf("  samples pinned at |out| > 0.999 in the first 400 (8.3 ms): %d\n", flat);
}
