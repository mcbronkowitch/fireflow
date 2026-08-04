#include <doctest/doctest.h>
#include "fx/fx_util.h"
#include <algorithm>
using namespace spky;

TEST_CASE("xfade: stage 0 is exactly lhs, stage 1 is exactly rhs") {
    XFade x;
    float o0, o1;
    x.SetStage(0.f);
    x.Process(0.25f, -0.5f, 0.9f, 0.9f, o0, o1);
    CHECK(o0 == 0.25f);
    CHECK(o1 == -0.5f);
    x.SetStage(1.f);
    x.Process(0.25f, -0.5f, 0.9f, 0.7f, o0, o1);
    CHECK(o0 == 0.9f);
    CHECK(o1 == 0.7f);
}

TEST_CASE("softswitch: rises to 1 within ~4 ms, falls back to idle") {
    SoftSwitch s;
    s.init(48000.f);
    CHECK(s.process() == 0.f);
    CHECK(s.is_idle());
    s.set_on(true);
    for (int i = 0; i < 300; ++i) s.process();   // 4 ms = 192 samples
    CHECK(s.process() == 1.f);
    CHECK(!s.is_idle());
    s.set_on(false);
    for (int i = 0; i < 300; ++i) s.process();
    CHECK(s.process() == 0.f);
    CHECK(s.is_idle());
}

TEST_CASE("softswitch: immediate flag jumps straight to hold") {
    SoftSwitch s;
    s.init(48000.f);
    s.set_on(true, true);
    CHECK(s.process() == 1.f);
}

static float max_rise_step(float sr) {
    SoftSwitch s;
    s.init(sr);
    s.process();                     // settle in idle
    s.set_on(true);
    float prev = 0.f, worst = 0.f;
    const int n = static_cast<int>(0.02f * sr);   // 20 ms >> the 4 ms ramp
    for (int i = 0; i < n; ++i) {
        const float v = s.process();
        worst = std::max(worst, v - prev);
        prev = v;
    }
    CHECK(prev == 1.f);              // the ramp must actually arrive at hold
    return worst;
}

TEST_CASE("softswitch: the 4 ms ramp is a ramp at 44.1/48/96 kHz") {
    CHECK(max_rise_step(48000.f) < 0.02f);
    CHECK(max_rise_step(96000.f) < 0.02f);   // today: 0.508 — ramp dies at 0.492, then snaps
    CHECK(max_rise_step(44100.f) < 0.02f);   // today: OOB table read, then a >0.27 snap
}
