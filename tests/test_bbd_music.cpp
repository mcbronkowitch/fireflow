#include <doctest/doctest.h>
#include "parts/bbd_music.h"
#include <cmath>

using namespace spky;
using namespace spky::bbd_music;

TEST_CASE("bbd_music: the ladder has eleven rungs, fastest first") {
    CHECK(kDivCount == 11);
    CHECK(kDivs[0] == doctest::Approx(1.f / 32.f));
    CHECK(kDivs[10] == doctest::Approx(1.f));
    for (int i = 1; i < kDivCount; ++i) CHECK(kDivs[i] > kDivs[i - 1]);
}

TEST_CASE("bbd_music: the ladder snaps, and holds through one rung of overlap") {
    DivLadder L;
    L.reset(5);
    // Inside the held rung's own half-width: no move.
    CHECK(L.process(5.f / 10.f) == 5);
    CHECK(L.process(5.4f / 10.f) == 5);
    // Past the NEXT rung's centre: move, and only by one.
    CHECK(L.process(6.0f / 10.f) == 6);
    CHECK(L.process(5.6f / 10.f) == 6);   // sticky coming back
    CHECK(L.process(5.0f / 10.f) == 5);
    CHECK(L.process(0.f) == 0);
    CHECK(L.process(1.f) == 10);
}

TEST_CASE("bbd_music: a lane dithering on a boundary does not chatter") {
    DivLadder L;
    L.reset(4);
    int changes = 0, prev = 4;
    for (int i = 0; i < 400; ++i) {
        // A boundary-hugging wobble of +-2% of the lane, which a bare
        // nearest-rung round would flip on nearly every sample.
        const float lane = 0.45f + 0.02f * std::sin(i * 1.7f);
        const int r = L.process(lane);
        if (r != prev) { ++changes; prev = r; }
    }
    CHECK(changes == 0);
}

TEST_CASE("bbd_music: the reachable window matches the spec's table") {
    // T >= 256 ms: the full 32x, five octaves.
    const Window a = window(0.256f);
    CHECK(a.f_lo == doctest::Approx(1000.f));
    CHECK(a.f_hi == doctest::Approx(32000.f));
    CHECK(!a.time_clamped);
    CHECK(!a.scale_truncated);
    // Long T stays at 32x -- the long end is self-normalising, because f_lo is
    // DEFINED as kMinStages/(2T), so the stage count at f_lo is 512 for every T.
    const Window l = window(2.0f);
    CHECK(l.f_hi / l.f_lo == doctest::Approx(32.f));
    // 125 ms: 15.6x.
    const Window b = window(0.125f);
    CHECK(b.f_lo == doctest::Approx(2048.f));
    CHECK(b.f_hi / b.f_lo == doctest::Approx(15.625f));
    // 50 ms: 6.25x, under three octaves -- STEP cannot reach the scale's top.
    const Window c = window(0.050f);
    CHECK(c.f_hi / c.f_lo == doctest::Approx(6.25f));
    CHECK(c.scale_truncated);
}

TEST_CASE("bbd_music: T below the floor is clamped, and says so") {
    // A free master lane at 30 Hz gives a 33 ms cycle; div 1/32 asks for 1 ms.
    const Window w = window(0.00104f);
    CHECK(w.time_clamped);
    CHECK(w.t_eff == doctest::Approx(kMinDelayS));
    CHECK(kMinDelayS == doctest::Approx(0.008f));
    // At the floor the window has collapsed: the lane is dead, and honestly so.
    CHECK(w.f_hi / w.f_lo == doctest::Approx(1.f));
}

TEST_CASE("bbd_music: the stage count holds the delay on the grid") {
    const Window w = window(0.25f);
    for (float lane = 0.f; lane <= 1.0001f; lane += 0.05f) {
        const float f = clock_flow(w, lane);
        const int st = stages_for(w, f);
        CHECK(st >= bbd_tuning::kMinStages);
        CHECK(st <= bbd_tuning::kMaxStages);
        // delay = stages / (2 f_clk) == T, to within the rounding of one stage.
        const float delay = st / (2.f * f);
        CHECK(delay == doctest::Approx(w.t_eff).epsilon(0.002));
    }
}

TEST_CASE("bbd_music: the lane spans its full travel at every division") {
    // No dead zone at the top: whatever the division, lane 1 lands exactly on
    // f_hi and lane 0 exactly on f_lo. The alternative -- a fixed frequency
    // range that clamps -- puts a silently-moving dead zone in the master lane.
    const float times[] = { 0.020f, 0.050f, 0.125f, 0.25f, 0.5f, 1.0f, 4.0f };
    for (float t : times) {
        const Window w = window(t);
        CHECK(clock_flow(w, 0.f) == doctest::Approx(w.f_lo));
        CHECK(clock_flow(w, 1.f) == doctest::Approx(w.f_hi));
        CHECK(clock_flow(w, 0.5f)
              == doctest::Approx(std::sqrt(w.f_lo * w.f_hi)));   // geometric
    }
}

TEST_CASE("bbd_music: STEP re-derives semitones against the quantizer's span") {
    // Quantizer::SPAN_SEMIS is 36, not 60: process() returns note/36. Mapping
    // that straight onto a five-octave clock span would make one quantizer step
    // 1.667 semitones of clock ratio -- a grid that is not a scale.
    const Window w = window(0.5f);            // full 32x available
    CHECK(clock_step(w, 0.f) == doctest::Approx(w.f_lo));
    CHECK(clock_step(w, 12.f / 36.f) == doctest::Approx(w.f_lo * 2.f));
    CHECK(clock_step(w, 24.f / 36.f) == doctest::Approx(w.f_lo * 4.f));
    CHECK(clock_step(w, 1.f) == doctest::Approx(w.f_lo * 8.f));
    // And it never leaves the reachable window.
    const Window n = window(0.050f);          // only 6.25x
    CHECK(n.scale_truncated);
    CHECK(clock_step(n, 1.f) == doctest::Approx(n.f_hi));
}
