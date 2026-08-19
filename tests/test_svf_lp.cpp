#include <doctest/doctest.h>
#include <cmath>
#include "Filters/svf.h"
#include "util/svf_lp.h"

using namespace spky;

// SvfLp exists only because daisysp::Svf computed four outputs and a drive term
// the engine never reads (see util/svf_lp.h for the measurement that motivated
// it). Its whole licence to exist is that it is the SAME filter -- so the
// invariant worth pinning is not "close enough", it is bit equality of Low()
// against the vendored original, over the call pattern the engine actually
// makes: a cutoff pushed at every control tick, a resonance pushed at every
// control tick that almost never moves, and drive nailed to zero.
//
// This is a desktop test and desktop builds do not use -ffast-math. On the
// firmware (-ffast-math -funroll-loops) dropping the `- drive_*band^3` term
// lets the compiler contract the band update differently, and the two do drift
// -- measured at 2.1e-6 absolute worst case over 1.9 M samples, i.e. about
// -113 dBFS. That is a rounding difference, not a behavioural one, and the
// engine does not claim byte-identity across toolchains. The equality below is
// what pins the ALGEBRA; the firmware figure is recorded here so nobody
// rediscovers it as a bug.
TEST_CASE("svf_lp: Low() is bit-identical to daisysp::Svf with drive 0") {
    daisysp::Svf ref;
    SvfLp        lp;
    ref.Init(48000.f);
    lp.Init(48000.f);
    ref.SetFreq(2000.f); lp.SetFreq(2000.f);
    ref.SetRes(0.15f);   lp.SetRes(0.15f);
    ref.SetDrive(0.f);

    unsigned s = 7u;
    long long differing = 0;
    for (int blk = 0; blk < 2000; ++blk) {
        // control tick: cutoff sweeps the whole musical range, resonance steps
        // occasionally (the case SvfLp's change guard has to get right)
        const float c = 60.f + 13940.f * (0.5f + 0.5f * std::sin(blk * 0.01f));
        ref.SetFreq(c); lp.SetFreq(c);
        const float r = 0.15f + (blk % 97 == 0 ? 0.5f : 0.f);
        ref.SetRes(r); lp.SetRes(r);
        for (int i = 0; i < 96; ++i) {
            s = s * 1664525u + 1013904223u;
            const float x = (float)(s >> 8) / 8388608.f - 1.f;
            ref.Process(x);
            lp.Process(x);
            if (ref.Low() != lp.Low()) ++differing;
        }
    }
    CHECK(differing == 0);
}

// The change guards must not turn into "the first value wins". A fresh filter
// has to accept its first SetFreq/SetRes even when the argument happens to
// equal the sentinel-adjacent defaults, and a repeated push must be a no-op
// rather than a re-tune -- both are covered by the equality above only because
// it pushes repeats; this pins the fresh-instance half directly.
TEST_CASE("svf_lp: a fresh filter takes its first SetFreq/SetRes") {
    SvfLp a, b;
    a.Init(48000.f);
    b.Init(48000.f);
    a.SetFreq(400.f); a.SetRes(0.6f);
    // b gets the same values, but pushed twice -- the guard must make the
    // second push change nothing at all.
    b.SetFreq(400.f); b.SetRes(0.6f);
    b.SetFreq(400.f); b.SetRes(0.6f);
    for (int i = 0; i < 512; ++i) {
        const float x = std::sin(6.2831853f * 220.f * i / 48000.f);
        a.Process(x);
        b.Process(x);
        REQUIRE(a.Low() == b.Low());
    }
    CHECK(a.Low() != 0.f);   // guard against both being silently dead
}

// A two-pole fed exact zeros decays geometrically: it approaches zero without
// reaching it and settles in the subnormal range, where every subsequent
// Process pays the denormal penalty for as long as the input stays silent.
// FEED found this the hard way -- after its own amplitude glide was fixed to
// arrive at zero, the filter inherited the whole tax and an idle deck still
// cost 1.17x a sounding one.
//
// FlushDenormals is opt-in at the caller's control rate, so the equality
// against daisysp::Svf above is untouched: that test drives full-scale noise
// and never comes near the threshold.
TEST_CASE("svf_lp: FlushDenormals ends a silent tail, and only a silent one") {
    SvfLp lp;
    lp.Init(48000.f);
    lp.SetFreq(2000.f);
    lp.SetRes(0.f);
    for (int i = 0; i < 64; ++i) lp.Process(i == 0 ? 1.f : 0.f);   // excite

    // Silence, with the flush pushed at a control rate. The state has to reach
    // EXACT zero -- "very small" is the defect, not the fix.
    long long subnormal_out = 0;
    for (int blk = 0; blk < 4000; ++blk) {
        lp.FlushDenormals();
        for (int i = 0; i < 96; ++i) {
            lp.Process(0.f);
            if (std::fpclassify(lp.Low()) == FP_SUBNORMAL) ++subnormal_out;
        }
    }
    CAPTURE(subnormal_out);
    CHECK(subnormal_out == 0);
    CHECK(lp.Low() == 0.f);

    // The half that stops the guard from being "zero the filter whenever the
    // input is quiet". At a low cutoff the ring outlives the excitation by a
    // long way: 60 Hz is a 16.7 ms period, so 5 ms in it is near its peak and
    // must survive any number of flush calls.
    //
    // What this catches is an UNCONDITIONAL flush -- verified red, peak 0. It
    // does NOT catch dropping the _band term from the condition: at 1e-15 that
    // mutation stays green, because a live ring's zero crossings never land in
    // a window that narrow. Said here so the next reader does not take this
    // block for a gate on _band; svf_lp.h carries the same note.
    SvfLp ring;
    ring.Init(48000.f);
    ring.SetFreq(60.f);
    ring.SetRes(0.9f);
    for (int i = 0; i < 8; ++i) ring.Process(i == 0 ? 1.f : 0.f);
    float peak = 0.f;
    for (int i = 0; i < 240; ++i) {          // 5 ms
        ring.FlushDenormals();
        ring.Process(0.f);
        peak = fmaxf(peak, fabsf(ring.Low()));
    }
    CAPTURE(peak);
    CHECK(peak > 1e-4f);
}
