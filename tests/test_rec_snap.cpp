// The REC snap rule (spec 2026-11-07-sampler-records-other-deck-on-the-bar
// §4.2, plan Task 5).
#include <doctest/doctest.h>
#include <initializer_list>
#include "sampler/rec_snap.h"
using namespace spky;

namespace {

// A press `beats_after` beats after a bar line at frame 1 000 000 (negative:
// before the NEXT line), at `bpm` with PACE x1.
SnapIn press(float bpm, double beats_after, bool start, uint32_t delay = 0) {
    const float fpb = 48000.f * 60.f / bpm;
    SnapIn in{};
    in.frames_per_beat = fpb;
    in.start = start;
    in.delay = delay;
    in.last_bar = 1000000;
    const double b = beats_after >= 0.0 ? beats_after : 4.0 + beats_after;   // position in the bar
    in.beats = 40.0 + b;                                                       // bar 10
    in.now = in.last_bar + uint64_t(b * double(fpb));
    return in;
}

} // namespace

TEST_CASE("rec snap: the rule table at 120 BPM and 40 BPM") {
    for (float bpm : {120.f, 40.f}) {
        INFO("bpm ", bpm);
        const uint32_t D = uint32_t(48000.f * 60.f / bpm);   // the writer one beat behind
        // late: 0.3 beat after the line -> the line
        SnapOut o = rec_snap(press(bpm, 0.3, true));
        CHECK(o.kind == SnapKind::Past);
        CHECK(o.target == 1000000);
        o = rec_snap(press(bpm, 0.3, false, D));
        CHECK(o.kind == SnapKind::Past);
        CHECK(o.target == 1000000);
        // early: 0.3 beat before the next line -> held
        CHECK(rec_snap(press(bpm, -0.3, true)).kind == SnapKind::HeldNext);
        CHECK(rec_snap(press(bpm, -0.3, false, D)).kind == SnapKind::HeldNext);
        // elsewhere: mid-bar -> now
        const SnapIn mid = press(bpm, 2.0, true);
        o = rec_snap(mid);
        CHECK(o.kind == SnapKind::Now);
        CHECK(o.target == mid.now);
    }
}

TEST_CASE("rec snap: no snap at 39 BPM and at 120 BPM PACE x1/4") {
    for (float bpm : {39.f, 30.f}) {                 // 30 = 120 x 1/4
        INFO("bpm ", bpm);
        CHECK_FALSE(rec_snap_enabled_at(48000.f * 60.f / bpm));
        for (double at : {0.3, -0.3}) {
            const SnapIn in = press(bpm, at, true);
            const SnapOut o = rec_snap(in);
            CHECK(o.kind == SnapKind::Now);
            CHECK(o.target == in.now);
        }
    }
    CHECK(rec_snap_enabled_at(72000.f));             // 40 BPM: still snaps
}

TEST_CASE("rec snap: a stop's late window is clamped to the writer's delay") {
    // 0.3 beat late at 120 BPM is 7 200 frames; a writer only 5 000 behind
    // cannot reach the line, so the stop acts now.
    SnapIn in = press(120.f, 0.3, false, 5000);
    CHECK(rec_snap(in).kind == SnapKind::Now);
    in = press(120.f, 0.3, false, 8000);
    CHECK(rec_snap(in).kind == SnapKind::Past);
    // A stop with a live writer (D = 0) never snaps back.
    in = press(120.f, 0.01, false, 0);
    CHECK(rec_snap(in).kind == SnapKind::Now);
}

TEST_CASE("rec snap: the window edges") {
    const float fpb = 24000.f;                       // 120 BPM
    SnapIn in{};
    in.frames_per_beat = fpb;
    in.start = true;
    in.last_bar = 500000;
    // Exactly one beat after the line: no longer late, and not early -> now.
    in.now = in.last_bar + 24000;
    in.beats = 41.0;
    CHECK(rec_snap(in).kind == SnapKind::Now);
    // One frame less: late.
    in.now = in.last_bar + 23999;
    in.beats = 40.99996;
    CHECK(rec_snap(in).kind == SnapKind::Past);
    // Exactly one beat before the next line: early.
    in.now = in.last_bar + 72000;
    in.beats = 43.0;
    CHECK(rec_snap(in).kind == SnapKind::HeldNext);
    // A hair more than one beat before it: now.
    in.beats = 42.999;
    in.now = in.last_bar + 71976;
    CHECK(rec_snap(in).kind == SnapKind::Now);
}
