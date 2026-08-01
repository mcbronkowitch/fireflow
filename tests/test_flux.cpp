#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "fx/flux.h"
#include "mod/divisions.h"
using namespace spky;

static float s_buf[Flux::kMaxSamples];

// Peak-detect the first echo arrival. A single-sample impulse comes out of a
// band-limited BBD as a smear, so a burst plus a peak search is the honest
// way to ask "when did it arrive".
static int first_echo_index(Flux& f, int n) {
    float peak = 0.f;
    int at = -1;
    for (int i = 0; i < n; ++i) {
        float l = (i < 32) ? 1.f : 0.f;
        float r = l;
        f.process(l, r);
        if (i > 500 && std::fabs(l) > peak) { peak = std::fabs(l); at = i; }
    }
    return peak > 1e-3f ? at : -1;
}

TEST_CASE("flux: synced 1/4 at 120 BPM = 0.5 s echo") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);                   // slice 3 -> kDivisions[8] "1/4"
    f.set_feedback(0.f);
    f.set_mix(1.f);
    CHECK(f.delay_time() == doctest::Approx(0.5f).epsilon(0.001));
    const int idx = first_echo_index(f, 40000);
    REQUIRE(idx > 0);
    CHECK(idx > 23400);
    CHECK(idx < 24700);
}

TEST_CASE("flux: the clock law reaches the line") {
    // RATE is a tone control as much as a time control now: the ladder spans
    // 16x in time at a fixed tempo, which after the ceiling is roughly 8x in
    // brightness at 120 BPM -- 8 kHz at "1/32" down to 1.0 kHz at "1/2".
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_stages(0.8f);              // 8192, the Memory Man
    f.set_rate(0);                   // "1/2" -> 1.0 s @120
    // The 30 ms slew has to run before clock_hz() reflects the target.
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.stages() == 8192);
    CHECK(f.clock_hz() == doctest::Approx(4096.f).epsilon(0.02));
    f.set_rate(11);                  // "1/32" -> 0.0625 s @120 -> hits the ceiling
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.clock_hz() == doctest::Approx(bbd_tuning::kClockMaxHz).epsilon(0.001));
}

TEST_CASE("flux: the buffer no longer bounds the delay time") {
    // The t_max clamp is GONE. Delay time is bounded by how dark the user is
    // willing to go, not by a buffer length -- a "1/2" at 20 BPM is 6 s and
    // that is now a legal, very muddy setting.
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(20.f);
    f.set_rate(0);
    CHECK(f.delay_time() == doctest::Approx(6.f).epsilon(0.001));
    for (int i = 0; i < 48000; ++i) {
        float l = 0.2f, r = 0.2f;
        f.process(l, r);
        REQUIRE(std::isfinite(l));
    }
}

TEST_CASE("flux: STAGES is geometric, 512 to 16384, and 0.8 is the Memory Man") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    auto settled_stages = [&](float norm) {
        f.set_stages(norm);
        for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
        return f.stages();
    };
    CHECK(settled_stages(0.f) == Flux::kMinStages);
    CHECK(settled_stages(0.8f) == doctest::Approx(8192).epsilon(0.01));
    CHECK(settled_stages(1.f) == Flux::kMaxStages);
    CHECK(settled_stages(0.4f) == doctest::Approx(2048).epsilon(0.01));
}

TEST_CASE("flux: FXT_FLUX_TIME moves the clock -- the test that could not exist before") {
    // The 2026-07-17 spec retired this target with "modulating the delay time
    // makes no musical sense" -- true of a crossfade delay, false of a BBD,
    // where clock modulation IS the sound generation.
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_stages(0.8f);
    f.set_rate(3);                   // 0.5 s -> 8192 Hz
    f.set_time_mod(0.5f);            // neutral
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    const float neutral = f.clock_hz();
    CHECK(neutral == doctest::Approx(8192.f).epsilon(0.02));
    f.set_time_mod(0.75f);           // +1 octave
    { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.clock_hz() == doctest::Approx(neutral * 2.f).epsilon(0.02));
    f.set_time_mod(0.f);             // -2 octaves
    { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.clock_hz() == doctest::Approx(neutral * 0.25f).epsilon(0.02));
}

TEST_CASE("flux: the lane reaches the clock through the FAST path, not the 30 ms slew") {
    // Two smoothers, two jobs. Had modulation gone through the 30 ms path it
    // would have been a ~5 Hz low-pass and a 4 Hz vibrato would not have
    // survived. set_time_mod must therefore take effect within one sample.
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);
    f.set_time_mod(0.5f);
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    const float before = f.clock_hz();
    f.set_time_mod(0.75f);
    { float l = 0.f, r = 0.f; f.process(l, r); }   // exactly ONE sample later
    CHECK(f.clock_hz() > before * 1.9f);
}

TEST_CASE("flux: the ceiling holds when ladder and lane push together") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(200.f);
    f.set_stages(1.f);               // 16384
    f.set_rate(11);                  // "1/32"
    f.set_time_mod(1.f);             // x4 on top
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.clock_hz() == doctest::Approx(bbd_tuning::kClockMaxHz).epsilon(0.001));
}

TEST_CASE("flux: off is bit-exact dry") {
    Flux f;
    f.init(48000.f, s_buf);
    for (int i = 0; i < 2000; ++i) {
        const float s = std::sin(0.01f * i) * 0.4f;
        float l = s, r = s;
        f.process(l, r);
        CHECK(l == s);
        CHECK(r == s);
    }
}

TEST_CASE("flux: null buffers never engage") {
    Flux f;
    f.init(48000.f, nullptr);
    f.set_on(true, true);
    CHECK(!f.has_buffers());
    CHECK(!f.engaged());
    float l = 0.5f, r = 0.5f;
    f.process(l, r);
    CHECK(l == 0.5f);
}

TEST_CASE("flux: feedback produces decaying repeats") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(6);                   // 0.25 s
    f.set_feedback(0.45f);
    f.set_mix(1.f);
    // set_rate(6) differs from the boot default (3), so it runs the real 30
    // ms recompute_time(false) slew -- unlike the old tape-era version of
    // this test, which happened to pass rate 3, the SAME as boot, and so
    // triggered no slew at all. Measured: without this warm-up the first
    // echo lands ~1000 samples late (13014, not 12000) because the clock is
    // still climbing off the OLD 0.5 s rate while the burst's ticks
    // accumulate, and every later repeat inherits that same offset. Settle
    // it first, exactly like the sibling tests in this file already do.
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    std::vector<float> out(80000);
    for (int i = 0; i < (int)out.size(); ++i) {
        float l = (i < 32) ? 1.f : 0.f;
        float r = l;
        f.process(l, r);
        out[i] = l;
    }
    auto peak_around = [&](int c) {
        float p = 0.f;
        for (int i = c - 900; i < c + 900; ++i) p = std::max(p, std::fabs(out[i]));
        return p;
    };
    const float p1 = peak_around(12000);
    const float p2 = peak_around(24000);
    const float p3 = peak_around(36000);
    CHECK(p1 > 1e-3f);
    CHECK(p2 < p1);
    CHECK(p3 < p2);
}

TEST_CASE("flux: feedback at max blooms but stays bounded") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(6);
    f.set_feedback(1.f);             // -> 1.2 coefficient
    f.set_drive(0.5f);
    f.set_mix(1.f);
    float peak = 0.f;
    double late_sq = 0.0;
    int late_n = 0;
    for (int i = 0; i < 480000; ++i) {
        float l = (i < 32) ? 1.f : 0.f;
        float r = l;
        f.process(l, r);
        REQUIRE(std::isfinite(l));
        peak = std::max(peak, std::fabs(l));
        if (i >= 432000) { late_sq += (double)l * l; ++late_n; }
    }
    const float late_rms = static_cast<float>(std::sqrt(late_sq / late_n));
    INFO("peak=" << peak << " late_rms=" << late_rms);
    CHECK(peak > 0.2f);
    CHECK(peak < 12.f);
    CHECK(late_rms > 0.01f);
}

// Self-normalised decay ratio at a FIXED feedback knob: rms of the tail in
// [3.0,3.5]s over rms in [1.0,1.5]s. Normalising against the tail's own
// earlier level keeps the ~8 dB that DRIVE legitimately adds out of the
// number, so what is left is purely "how long does it ring".
static float decay_ratio(Flux& f) {
    const int burst_n = 32;
    const int a_at = 48000, b_at = 3 * 48000;
    const int win = 24000;
    const int total = b_at + win + 1000;
    double A = 0.0, B = 0.0;
    for (int i = 0; i < total; ++i) {
        float l = (i < burst_n) ? 0.8f * std::sin(0.2f * i) : 0.f;
        float r = l;
        f.process(l, r);
        const int t = i - burst_n;
        if (t >= a_at && t < a_at + win) A += (double)l * l;
        if (t >= b_at && t < b_at + win) B += (double)l * l;
    }
    const double a = std::sqrt(A / win), b = std::sqrt(B / win);
    return a > 1e-12 ? static_cast<float>(b / a) : 0.f;
}

TEST_CASE("flux: DRIVE does not move where FEEDBACK blooms") {
    // The saturator sits INSIDE the feedback loop, so its gain `g` multiplies
    // the loop gain: raising DRIVE by +12 dB used to quadruple it. Measured on
    // the coupled law, the feedback knob that produces a 15 s tail slid from
    // 0.57 at DRIVE 0 to 0.14 at DRIVE 1 -- above roughly a quarter DRIVE,
    // most of FEEDBACK's travel was runaway territory and the two controls
    // fought each other. Flux::set_feedback now divides the coefficient by the
    // same `g`, so a feedback setting means one thing at every DRIVE.
    //
    // This asserts the DECAY only. The level and dirt DRIVE adds are a
    // separate contract, covered in test_bbd.cpp -- and deliberately
    // untouched here: the fix divides the feedback coefficient, never
    // sat_out_, which is what the 2026-07-27 investigation had to repair.
    //
    // Measured at knob 0.40: coupled 0.027 -> 1.216 across DRIVE (a factor of
    // 46); decoupled 0.027 -> 0.053 (a factor of 2). The factor-of-4 bound
    // below therefore fails loudly on the coupled law and passes with room on
    // the decoupled one.
    auto ratio_at_drive = [](float drive) {
        Flux f;
        f.init(48000.f, s_buf);
        f.set_on(true, true);
        f.set_bpm(120.f);
        f.set_rate(6);
        f.set_mix(1.f);
        f.set_feedback(0.40f);
        f.set_drive(drive);      // pushed AFTER feedback: set_drive must re-apply it
        return decay_ratio(f);
    };
    const float clean = ratio_at_drive(0.f);
    const float dirty = ratio_at_drive(1.f);
    INFO("decay ratio: DRIVE 0 = " << clean << ", DRIVE 1 = " << dirty);
    REQUIRE(clean > 1e-4f);
    CHECK(dirty < 4.f * clean);
}

TEST_CASE("flux: init resets the DRIVE guard so a re-init's repeated push isn't swallowed") {
    // Reproduces Spotymod::reinit() -> Instrument::init() -> Flux::init(): a
    // sample-rate change rebuilds BbdEcho, and if Flux::init did not also
    // reset its unchanged-value guard, the next push of the SAME value the
    // user already had dialled in would be swallowed forever.
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_drive(0.7f);
    f.init(48000.f, s_buf);     // simulate the re-init
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(6);
    f.set_mix(1.f);
    f.set_feedback(0.9f);
    f.set_drive(0.7f);                      // SAME value as before the re-init
    // With a stale guard this push is dropped and BbdEcho keeps the drive
    // SetDrive(0.f) that Init() left behind -- measurably cleaner.
    float peak = 0.f;
    for (int i = 0; i < 120000; ++i) {
        float l = (i < 480) ? 0.8f * std::sin(0.2f * i) : 0.f;
        float r = l;
        f.process(l, r);
        peak = std::max(peak, std::fabs(l));
    }
    CHECK(peak > 0.05f);
    CHECK(f.drive_norm_for_test() == doctest::Approx(0.7f));
}

TEST_CASE("flux: init resets the STAGES guard the same way") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_stages(0.2f);
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_stages(0.2f);                     // SAME value again
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.stages() == doctest::Approx(1024).epsilon(0.01));
}

TEST_CASE("flux: init resets the LINK guard so a re-init's repeated push isn't swallowed") {
    // Reproduces Spotymod::reinit(): a sample-rate change, a reset, or a
    // fresh sampler add all call Flux::init() again without touching
    // params[]. If init left _link at whatever the knob last was while
    // resetting _drag/_thin to 0, the next pushParams(SAME value) would be
    // swallowed by set_link's n == _link guard and LINK would go silent
    // until the knob physically moved.
    auto setup = [](Flux& f) {
        f.init(48000.f, s_buf);
        f.set_on(true, true);
        f.set_bpm(120.f);
        f.set_rate(11);                  // "1/32" -> 0.0625 s -> 3000 samples/repeat
        f.set_stages(0.f);
        f.set_mix(1.f);
        f.set_feedback(0.f);
        RhythmView rv;
        rv.gap[0] = 12000; rv.gap[1] = 6000; rv.valid = true;   // n0=4, n1=2
        f.set_rhythm(rv);
    };
    Flux f;
    setup(f);
    f.set_link(-1.f);
    setup(f);                                        // simulates the re-init
    f.set_link(-1.f);                                // SAME value as before
    // With a stale _link guard this push is dropped and _thin stays at
    // init's reset value (0), so the gate would never leave 1 -- run to a
    // point that must be ducked if thinning actually re-engaged.
    auto run = [&f](int n) { for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); } };
    run(7500);
    CHECK(f.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));
}

TEST_CASE("flux slice: norm endpoints hit 1/2 and 1/32") {
    CHECK(kFluxRateCount == 12);
    CHECK(kFluxRateOffset == 5);
    CHECK(std::string(kDivisions[kFluxRateOffset + flux_division_index(0.f)].name) == "1/2");
    CHECK(std::string(kDivisions[kFluxRateOffset + flux_division_index(1.f)].name) == "1/32");
    CHECK(std::string(kDivisions[kFluxRateOffset + flux_division_index(3.f/11.f)].name) == "1/4");
}

static float s_buf2[Flux::kMaxSamples];

static RhythmView drag_view(int32_t g0, int32_t g1) {
    RhythmView rv;
    rv.gap[0] = g0;
    rv.gap[1] = g1;
    rv.valid  = true;
    return rv;
}

TEST_CASE("flux: LINK at 0 (centre) is bit-identical to a Flux that never heard a rhythm") {
    Flux plain, dragged;
    plain.init(48000.f, s_buf);
    dragged.init(48000.f, s_buf2);
    for (Flux* f : { &plain, &dragged }) {
        f->set_on(true, true);
        f->set_bpm(120.f);
        f->set_rate(3);
        f->set_stages(0.8f);
        f->set_mix(1.f);
        f->set_feedback(0.5f);
    }
    dragged.set_rhythm(drag_view(12000, 6000));
    dragged.set_link(0.f);

    for (int i = 0; i < 60000; ++i) {
        const float in = (i < 32) ? 1.f : 0.f;
        float al = in, ar = in, bl = in, br = in;
        plain.process(al, ar);
        dragged.process(bl, br);
        REQUIRE(al == bl);      // bit-identical, not Approx
        REQUIRE(ar == br);
    }
}

TEST_CASE("flux: DRAG at 1 alternates between the neighbour's two intervals") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);                       // ladder = 0.5 s, well away from both
    f.set_stages(0.8f);
    f.set_mix(1.f);
    f.set_feedback(0.f);
    f.set_rhythm(drag_view(12000, 6000));   // 0.25 s and 0.125 s
    f.set_link(1.f);

    CHECK(f.drag_time_s() == doctest::Approx(0.25f).epsilon(0.001));

    auto run = [&f](int n) { for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); } };

    // The repeat phase is never reset to 0 by a run() boundary, only by a flip --
    // so each leg's margin is measured from wherever the PREVIOUS leg left
    // phase, not from a fresh 0. Step 0 is 12000 samples: run(11990) leaves
    // phase at 11990 (10 short), and the first 10 calls of the next run(20)
    // reach the flip, leaving phase at 10 afterwards. Step 1 is 6000 samples,
    // so from that phase of 10 it takes 5990 more calls to reach the flip --
    // one sample short of run(5990) is run(5980), which is what leaves phase
    // at 5990 (still short) for the "not yet" check below; the following
    // run(20) then supplies the last 10 calls needed to flip back.
    run(11990);
    CHECK(f.drag_time_s() == doctest::Approx(0.25f).epsilon(0.001));   // not yet
    run(20);
    CHECK(f.drag_time_s() == doctest::Approx(0.125f).epsilon(0.001));  // flipped
    run(5980);
    CHECK(f.drag_time_s() == doctest::Approx(0.125f).epsilon(0.001));
    run(20);
    CHECK(f.drag_time_s() == doctest::Approx(0.25f).epsilon(0.001));   // and back
}

TEST_CASE("flux: DRAG interpolates geometrically") {
    // Pitch tracks the clock RATIO, so the interpolation is geometric --
    // the same reasoning behind the modulation lane's x1/4..x4 mapping.
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);                       // ladder = 0.5 s
    f.set_rhythm(drag_view(12000, 18000));   // 0.25 s and 0.375 s
    f.set_link(0.5f);
    // sqrt(0.5 * 0.25) == 0.353553
    CHECK(f.drag_time_s() == doctest::Approx(0.353553f).epsilon(0.001));
}

TEST_CASE("flux: DRAG reaches the clock") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);                       // ladder = 0.5 s -- must NOT be the value read
    f.set_mix(1.f);
    f.set_feedback(0.f);
    // 0.375 s and 0.5 s: the FIRST interval (0.375 s) is off the 0.5 s ladder,
    // so a DRAG-ignoring apply_drag (one that just kept _delay_time) would
    // read the ladder's 0.5 s and land on the wrong clock below -- unlike the
    // old (24000, 18000) pair, whose first interval WAS 0.5 s and so passed
    // even with DRAG doing nothing.
    f.set_rhythm(drag_view(18000, 24000));
    f.set_link(1.f);
    // The 30 ms slew has to run before clock_hz() reflects the target.
    for (int i = 0; i < 5000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    // stages stays the 8192 boot default (kBootStagesNorm == 0.8, set by init()).
    // f = stages / (2 * t) = 8192 / (2 * 0.375) = 10922.667 Hz
    CHECK(f.clock_hz() == doctest::Approx(10922.667f).epsilon(0.02));
}

TEST_CASE("flux: a step bends pitch by the ratio of the two intervals") {
    // The bend IS the clock ratio -- there is no crossfade in a BBD and there
    // must be none in the model. Asserting the clock ratio across a step is
    // asserting the pitch ratio (spec section 1.4).
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);
    f.set_stages(0.8f);
    f.set_mix(1.f);
    f.set_feedback(0.f);
    f.set_rhythm(drag_view(24000, 12000));   // 0.5 s and 0.25 s -- a 2:1 step
    f.set_link(1.f);

    auto run = [&f](int n) { for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); } };

    run(5000);                               // past the 30 ms slew, still step 0
    const float before = f.clock_hz();
    run(24000);                              // step 0 elapses, flip to 0.25 s
    run(5000);                               // let the slew settle on the new one
    const float after = f.clock_hz();
    CHECK(after / before == doctest::Approx(2.0f).epsilon(0.02));
}

TEST_CASE("flux: an invalid neighbour rhythm leaves DRAG inert at any setting") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);                       // ladder = 0.5 s
    // Make the rhythm VALID first and let DRAG actually engage and flip once --
    // otherwise set_rhythm's reset branch and the `active == _drag_active`
    // clause of its guard are never exercised, and the case only proves DRAG
    // is inert on a Flux that was never dragged in the first place.
    f.set_rhythm(drag_view(12000, 6000));   // 0.25 s and 0.125 s
    f.set_link(1.f);
    CHECK(f.drag_time_s() == doctest::Approx(0.25f).epsilon(0.001));  // off the ladder

    auto run = [&f](int n) { for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); } };
    run(12000);                          // step 0 (12000 samples) elapses -- flip
    CHECK(f.drag_time_s() == doctest::Approx(0.125f).epsilon(0.001));  // flipped

    RhythmView rv = drag_view(12000, 6000);
    rv.valid = false;
    f.set_rhythm(rv);
    CHECK(f.drag_time_s() == doctest::Approx(0.5f).epsilon(0.001));  // back on the ladder
}

TEST_CASE("flux: RATE still reaches the ladder at intermediate DRAG") {
    // Guards against an interpolation that accidentally saturates to the
    // neighbour's interval as soon as DRAG leaves zero.
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rhythm(drag_view(12000, 18000));
    f.set_link(0.5f);
    f.set_rate(3);                       // 0.5 s -> sqrt(0.5*0.25)  = 0.353553
    const float at_quarter = f.drag_time_s();
    f.set_rate(0);                       // 1.0 s -> sqrt(1.0*0.25)  = 0.5
    const float at_half = f.drag_time_s();
    CHECK(at_quarter == doctest::Approx(0.353553f).epsilon(0.001));
    CHECK(at_half    == doctest::Approx(0.5f).epsilon(0.001));
}

// A 1/32 rung at 120 BPM is 0.0625 s = 3000 samples per repeat, so a 12000
// sample gap is 4 repeats and a 6000 sample gap is 2. 512 stages keeps the
// clock (512/(2*0.0625) = 4096 Hz) well under the 32 kHz ceiling, so a
// clamped clock cannot be mistaken for a stable one.
static void thin_setup(Flux& f, float* buf, int32_t g0, int32_t g1) {
    f.init(48000.f, buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(11);                  // "1/32" -> 0.0625 s
    f.set_stages(0.f);               // 512
    f.set_mix(1.f);
    f.set_feedback(0.f);
    f.set_rhythm(drag_view(g0, g1));
}

TEST_CASE("flux: LINK's negative half never moves the clock") {
    // THE assertion this whole design exists for. Per sample, bit-equal, over
    // a full second -- long enough to cross many pattern boundaries.
    //
    // Fixture note: thin_setup's set_stages(0.f) moves STAGES from the boot
    // default (8192) down to 512, and that 30 ms slew -- unrelated to
    // thinning -- takes ~13000 samples to snap (measured). Every other
    // STAGES-changing case in this file (e.g. "the clock law reaches the
    // line") warms up 20000 samples before reading clock_hz() for exactly
    // this reason; the brief's original 5000-sample warm-up here left the
    // capture of hz0 mid-slew, so the first several thousand REQUIRE checks
    // below were failing on the ordinary STAGES settle, not on anything LINK
    // does. Widened to 20000 to match the file's own convention.
    Flux f;
    thin_setup(f, s_buf, 12000, 6000);
    f.set_link(-1.f);
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    const float hz0 = f.clock_hz();
    for (int i = 0; i < 48000; ++i) {
        float l = 0.f, r = 0.f;
        f.process(l, r);
        REQUIRE(f.clock_hz() == hz0);
    }
}

TEST_CASE("flux: LINK -1 sounds one repeat in n and ducks the rest") {
    Flux f;
    thin_setup(f, s_buf, 12000, 6000);   // n0 = 4, n1 = 2
    f.set_link(-1.f);
    auto run = [&f](int n) { for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); } };

    // 4500 lands mid-way into the first COUNTED repeat window (the first
    // boundary fires at 3000); every later probe steps one whole repeat, so
    // each reads a settled gate rather than the 3 ms ramp.
    run(4500);  CHECK(f.gate_for_test() == doctest::Approx(1.f).epsilon(0.02));
    run(3000);  CHECK(f.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));
    run(3000);  CHECK(f.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));
    run(3000);  CHECK(f.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));
    run(3000);  CHECK(f.gate_for_test() == doctest::Approx(1.f).epsilon(0.02));  // interval 1
    run(3000);  CHECK(f.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));
    run(3000);  CHECK(f.gate_for_test() == doctest::Approx(1.f).epsilon(0.02));  // back to 0
}

TEST_CASE("flux: LINK's depth ducks rather than mutes") {
    Flux f;
    thin_setup(f, s_buf, 12000, 6000);
    f.set_link(-0.5f);
    auto run = [&f](int n) { for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); } };
    run(4500);  CHECK(f.gate_for_test() == doctest::Approx(1.f).epsilon(0.02));
    run(3000);  CHECK(f.gate_for_test() == doctest::Approx(0.5f).epsilon(0.02));
}

TEST_CASE("flux: an even neighbour rhythm is preserved, not spread") {
    // The uniformity guard must NOT reach this path: n0 == n1 is a musical
    // result here (a quarter-note echo whose repeats keep 1/32 resolution),
    // where for DRAG it would be a failure. Going through derive_intervals
    // would give 4 and 3.
    Flux f;
    thin_setup(f, s_buf, 12000, 12000);
    f.set_link(-1.f);
    CHECK(f.thin_n_for_test(0) == 4);
    CHECK(f.thin_n_for_test(1) == 4);
}

TEST_CASE("flux: a gap far longer than the delay clamps rather than mutes") {
    Flux f;
    thin_setup(f, s_buf, 1000000, 6000);   // 333 repeats
    f.set_link(-1.f);
    CHECK(f.thin_n_for_test(0) == link_tuning::kMaxSkip);
    CHECK(f.thin_n_for_test(1) == 2);
}

TEST_CASE("flux: a gap shorter than one repeat means every repeat sounds") {
    Flux f;
    thin_setup(f, s_buf, 100, 6000);
    f.set_link(-1.f);
    CHECK(f.thin_n_for_test(0) == 1);
    CHECK(f.thin_n_for_test(1) == 2);   // 2 is not the idle default: non-vacuous
}

TEST_CASE("flux: no valid neighbour rhythm leaves every repeat sounding") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(11);
    f.set_mix(1.f);
    RhythmView rv = drag_view(12000, 6000);
    rv.valid = false;
    f.set_rhythm(rv);
    f.set_link(-1.f);
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.gate_for_test() == 1.f);
}

TEST_CASE("flux: a valid but DRAG-unusable rhythm still arms THIN's repeat scheduler") {
    // set_rhythm's early-return guard only tracks the DRAG intervals: a
    // 20-sample gap is legal on RhythmView (gaps clamp at 1, not at
    // drag_tuning::kMinGap == 32) but derive_intervals rejects it, so
    // derive_intervals(rv, iv) == {kNone, kNone} and, with LINK set BEFORE
    // any rhythm was ever seen (_drag_active starts false, _drag_iv starts
    // {0,0}), active == _drag_active and iv == _drag_iv -- the guard's
    // "nothing changed for DRAG" branch. That branch is also the thinning
    // half's ONLY route to the repeat scheduler (apply_drag reaches its
    // refresh). Order matters: LINK first, rhythm second, is what leaves the
    // scheduler unarmed if that branch does not also call apply_drag().
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(11);                  // "1/32" -> 0.0625 s -> 3000 samples/repeat
    f.set_stages(0.f);
    f.set_mix(1.f);
    f.set_feedback(0.f);
    f.set_link(-1.f);                        // LINK before any rhythm
    f.set_rhythm(drag_view(6000, 20));        // valid, DRAG-unusable: n0=2, n1=1
    auto run = [&f](int n) { for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); } };

    // On the repeat grid (verified against the fixed implementation and
    // cross-checked against the already-passing n0=4,n1=2 pattern case,
    // which has the same shape): the FIRST counted repeat of an interval
    // always sounds, so interval1's lone repeat (n1=1) merges with
    // interval0's own first repeat into one continuous dwell at 1 spanning
    // TWO repeat lengths, alternating with interval0's second repeat (a
    // single repeat length) ducked -- sound[0,6000), duck[6000,9000),
    // sound[9000,15000), duck[15000,18000), ... If the scheduler were
    // left unarmed (repeat period == 0), advance_gate() would instead fire
    // every SAMPLE (or, with the defensive process() guard alone, never
    // fire at all) -- either way the gate would never track this grid.
    run(1500);  CHECK(f.gate_for_test() == doctest::Approx(1.f).epsilon(0.02));  // mid first sound dwell
    run(6000);  CHECK(f.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));  // t=7500, mid duck dwell
    run(4500);  CHECK(f.gate_for_test() == doctest::Approx(1.f).epsilon(0.02));  // t=12000, mid second sound dwell
    run(4500);  CHECK(f.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));  // t=16500, mid second duck dwell
}

TEST_CASE("flux: THIN owns a repeat scheduler even when DRAG has no intervals") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_rate(10);                         // short, easy-to-observe repeat
    f.set_link(-1.f);
    f.set_rhythm(drag_view(6000, 20));      // valid for THIN, invalid for DRAG
    f.set_on(true, true);
    float l = 0.f, r = 0.f;
    bool ducked = false;
    for (int i = 0; i < 18000; ++i) {
        f.process(l, r);
        if (f.gate_for_test() < 0.1f) ducked = true;
    }
    CHECK(ducked);
}

TEST_CASE("flux: an oscillating DRAG-active flag does not restart THIN's repeat scheduler") {
    // set_rhythm's `if (!active) { _drag_i = 0; repeat phase = 0; }` used to
    // reset the repeat phase on every active-flag flip, including into thinning --
    // the one case where it must not (apply_drag, called on the very next
    // line, already owns that reset for the non-thinning case; this line's
    // only REACHABLE effect was to break the one case it should leave alone).
    // Reachable whenever the neighbour republishes a rhythm that flips
    // DRAG-usability without stopping THIN: a uniform pair, or (as here) a
    // gap that crosses drag_tuning::kMinGap.
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(11);                  // "1/32" -> 0.0625 s -> 3000 samples/repeat
    f.set_stages(0.f);
    f.set_mix(1.f);
    f.set_feedback(0.f);
    f.set_rhythm(drag_view(12000, 6000));   // DRAG-usable (active=true), n0=4, n1=2
    f.set_link(-1.f);
    auto run = [&f](int n) { for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); } };
    run(1000);                                // partway into the first repeat, no boundary yet

    // Republish a valid rhythm that flips active (20 < kMinGap == 32) without
    // touching _rhy_valid or _thin. update_thin_pattern() also re-derives
    // _thin_n from the NEW gaps: n0 becomes 1 (20 rounds below one repeat).
    f.set_rhythm(drag_view(20, 6000));

    // update_thin_pattern() re-derives _thin_n = {1, 2} from the republish
    // BEFORE either schedule's first boundary ever fires, so both schedules
    // use the new {1, 2}, and interval0's single repeat means the first
    // boundary in either schedule immediately flips to interval1 (n1 = 2) --
    // the first duck is therefore the THIRD boundary of the interval0+
    // interval1 cycle, not the second (verified with a scratch probe before
    // trusting these numbers, in the same spirit as the branch's earlier
    // fixture checks: b1 interval0 sound+flip, b2 interval1 sound, b3
    // interval1 duck+flip).
    //
    // With the fix, the elapsed 1000 samples carry over, so the schedule is
    // undisturbed: b1@3000, b2@6000, b3@9000 (first duck). With the bug,
    // this republish restarts the window -- the elapsed 1000 samples are
    // discarded, so the first boundary needs a full 3000 MORE (global
    // t=4000), and every later boundary inherits that same 1000-sample
    // offset: b1@4000, b2@7000, b3@10000 (first duck). The two schedules are
    // only 1000 samples apart here, and the 3 ms/144-sample ramp needs
    // ~565+ samples past a boundary before Approx(epsilon=0.02) reads it as
    // settled (measured), so the probe has to land solidly inside that
    // narrow window: 700 samples past the FIXED schedule's duck boundary
    // (well past the ramp) and 300 samples short of the BUGGY schedule's own
    // boundary (still untouched there).
    run(8700);                                // 1000 + 8700 = 9700 total
    CHECK(f.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));
}

TEST_CASE("flux: crossing LINK from a ducked THIN repeat straight to DRAG un-mutes the return") {
    // apply_drag's gate reset must be owned by `thinning`, not by which
    // branch is taken: the DRAG branch never touches the gate, so if the
    // reset lived only in the inert branch's !thinning arm, a knob move
    // that jumps straight from THIN to DRAG without a push landing exactly
    // on 0 -- routine on a bipolar knob driven at frame rate against a
    // param push every 16 samples -- would leave a stale duck target (and a
    // stale _gate) in force for as long as DRAG stayed engaged, because the
    // gate branch in process() never switches itself off while _gate != 1.
    //
    // The rhythm here (12000, 6000) is deliberately the ordinary case, not
    // a contrived one: it is usable for BOTH halves (n0=4/n1=2 for
    // thinning, and a legal DRAG interval pair), because a rhythm usable
    // for thinning is usually also usable for DRAG -- this is the state a
    // player actually reaches turning the knob.
    Flux f;
    thin_setup(f, s_buf, 12000, 6000);   // usable for both halves
    f.set_link(-1.f);
    auto run = [&f](int n) { for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); } };

    run(7500);                                       // lands mid a ducked repeat (see the pattern case above)
    CHECK(f.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));

    f.set_link(1.f);                                 // straight across, no stop at 0
    run(2000);                                        // well past the 3 ms / 144-sample gate ramp
    CHECK(f.gate_for_test() == 1.f);
}

TEST_CASE("flux: changing RATE re-derives the thinning pattern from the new ladder time") {
    // recompute_time's update_thin_pattern() call was previously uncovered:
    // thin_setup always calls set_rate BEFORE set_rhythm, so at
    // recompute_time time _rhy_gap was still {0,0} in every existing case,
    // and every thin_n_for_test assertion in the suite came from
    // set_rhythm's own call instead. "Vary RATE while thinning" is Task 3
    // step 3 and the spec's risk-2 lever, and it hits this exact line.
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(11);                        // "1/32" -> 0.0625 s -> 3000 samples/repeat
    f.set_stages(0.f);
    f.set_mix(1.f);
    f.set_feedback(0.f);
    f.set_rhythm(drag_view(12000, 6000));  // n0 = 12000/3000 = 4 on this rung
    CHECK(f.thin_n_for_test(0) == 4);

    f.set_rate(9);                          // "1/16" -> 0.125 s -> 6000 samples/repeat
    CHECK(f.thin_n_for_test(0) == 2);       // same raw gap, re-derived against the new repeat length
}

TEST_CASE("flux: changing BPM re-derives the thinning pattern from the new ladder time") {
    // BPM changes hit the same recompute_time -> update_thin_pattern line.
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(11);                        // "1/32" -> 0.0625 s -> 3000 samples/repeat
    f.set_stages(0.f);
    f.set_mix(1.f);
    f.set_feedback(0.f);
    f.set_rhythm(drag_view(12000, 6000));  // n0 = 12000/3000 = 4 at 120 BPM
    CHECK(f.thin_n_for_test(0) == 4);

    f.set_bpm(60.f);                         // half tempo -> repeat doubles to 6000 samples
    CHECK(f.thin_n_for_test(0) == 2);
}


TEST_CASE("flux: the FEEDBACK coefficient is norm * 1.2 / the DRIVE gain, in either push order") {
    // apply_feedback() divides bbd_drive_gain() back out so that a FEEDBACK
    // knob position means one thing at every DRIVE -- see the long comment on
    // its definition. The control-rate round caches that quotient in
    // set_drive instead of evaluating it per call, which is a REASSOCIATION
    // of the same arithmetic. The tolerance is chosen for exactly that: tight
    // enough that a changed LAW fails, loose enough that the reassociation
    // (~1e-7 relative) passes.
    //
    // Both push orders, because a host may send them either way round and
    // set_drive is the thing that has to re-derive the coefficient when DRIVE
    // moves.
    const float drives[] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };
    const float fbs[]    = { 0.f, 0.2f, 0.45f, 0.8f, 1.f };
    for (float d : drives) {
        for (float fb : fbs) {
            const float want = fb * 1.2f / bbd_drive_gain(d);
            Flux a;
            a.init(48000.f, s_buf);
            a.set_feedback(fb);
            a.set_drive(d);
            Flux b;
            b.init(48000.f, s_buf);
            b.set_drive(d);
            b.set_feedback(fb);
            INFO("drive=" << d << " feedback=" << fb);
            CHECK(a.feedback_coef_for_test() == doctest::Approx(want).epsilon(1e-6));
            CHECK(b.feedback_coef_for_test() == doctest::Approx(want).epsilon(1e-6));
        }
    }
}

TEST_CASE("flux: a repeated FEEDBACK push changes nothing, and DRIVE still re-derives") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_drive(0.6f);
    f.set_feedback(0.3f);
    const float once = f.feedback_coef_for_test();
    f.set_feedback(0.3f);
    f.set_feedback(0.3f);
    CHECK(f.feedback_coef_for_test() == doctest::Approx(once).epsilon(1e-6));
    // A DRIVE move re-derives the coefficient from the STORED knob position,
    // not from the coefficient currently in force, so an unchanged-value
    // guard on FEEDBACK must not make it sticky across a DRIVE change.
    f.set_drive(0.f);
    CHECK(f.feedback_coef_for_test()
          == doctest::Approx(0.3f * 1.2f / bbd_drive_gain(0.f)).epsilon(1e-6));

    // 0.45 -- the value init() itself pushes -- is a REACHABLE knob position,
    // unlike the -1 sentinels _drive_norm and _stages_norm use. Pushing it
    // straight after init is swallowed by the guard, and that is correct:
    // init() already put the state there, so there is no change to swallow.
    Flux g;
    g.init(48000.f, s_buf);
    g.set_feedback(0.45f);
    CHECK(g.feedback_coef_for_test()
          == doctest::Approx(0.45f * 1.2f / bbd_drive_gain(0.f)).epsilon(1e-6));
}

TEST_CASE("flux: init leaves the FEEDBACK coefficient at its boot value, even re-initialised over a hot DRIVE") {
    // A CHARACTERISATION, not a bug-catcher: it passes before and after the
    // control-rate round by construction. init() sets _drive_norm = -1 before
    // calling set_drive(0.f), so that call always passes its own guard and
    // always rewrites whatever DRIVE-derived state exists. This test exists to
    // fail LATER -- if someone reorders init(), or gives set_drive an early
    // return, a cached DRIVE factor would silently survive a re-init. See
    // section 4.1 of docs/superpowers/specs/2026-07-29-flux-control-rate-design.md.
    const float want = 0.45f * 1.2f / bbd_drive_gain(0.f);
    Flux f;
    f.init(48000.f, s_buf);
    CHECK(f.feedback_coef_for_test() == doctest::Approx(want).epsilon(1e-6));
    f.set_drive(1.f);                       // a hot DRIVE, then re-init over it
    f.init(48000.f, s_buf);      // reproduces Spotymod::reinit()
    CHECK(f.feedback_coef_for_test() == doctest::Approx(want).epsilon(1e-6));
}

TEST_CASE("flux: the FXT_FLUX_TIME guard lands the first push and swallows repeats") {
    // The clock is the only observable of _time_mult, so this asserts through
    // clock_hz(). Rate 3 is the boot "1/4" (flux.cpp), i.e. 0.5 s at 120 BPM,
    // and the boot stage count is 8192 -- so the base clock is
    // 8192 / (2 * 0.5) = 8192 Hz.
    //
    // The DEPTH is 0.75, not 1.0. tape_time_mult maps 0.5 -> x1 and 1.0 -> x4,
    // and x4 on this base lands at 32768 Hz, above kClockMaxHz (32000) -- the
    // ceiling would clamp it and the test would be asserting the clamp rather
    // than the guard. 0.75 is x2, landing at 16384 Hz: comfortably under the
    // ceiling and unmistakably different from neutral.
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);
    f.set_mix(1.f);
    f.set_feedback(0.f);

    auto run = [&f](int n) {
        for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
        return f.clock_hz();
    };

    const float neutral = run(48000);
    REQUIRE(neutral > 0.f);

    // First push after init lands, despite _time_mult already holding x1:
    // the sentinel is -1, which no clamped norm can equal.
    f.set_time_mod(0.75f);
    const float doubled = run(4800);
    CHECK(doubled == doctest::Approx(2.f * neutral).epsilon(0.02f));

    // A repeat is swallowed, and swallowing it changes nothing.
    f.set_time_mod(0.75f);
    f.set_time_mod(0.75f);
    CHECK(run(4800) == doctest::Approx(doubled).epsilon(0.02f));

    // And back down again -- the guard must not make the control sticky.
    f.set_time_mod(0.5f);
    CHECK(run(4800) == doctest::Approx(neutral).epsilon(0.02f));
}

TEST_CASE("flux: init resets the FXT_FLUX_TIME guard so a re-init's repeated push isn't swallowed") {
    // Reproduces Spotymod::reinit() -> Instrument::init() -> Flux::init() on
    // an instance that already had a TIME depth pushed. init() puts _time_mult
    // back to neutral; if it did not ALSO reset the guard, the next push of
    // the SAME value the user still has dialled in would be swallowed and the
    // clock would stay at neutral until the knob physically moved. Same trap
    // the _link, _drive_norm and _stages_norm resets each document -- and
    // unlike _fb_scale, nothing else rewrites _time_mod_norm afterwards, so
    // this reset is load-bearing rather than defensive.
    auto arm = [](Flux& f) {
        f.set_on(true, true);
        f.set_bpm(120.f);
        f.set_rate(3);                      // "1/4" -> 0.5 s -> 8192 Hz base
        f.set_mix(1.f);
        f.set_feedback(0.f);
    };
    auto settle = [](Flux& f) {
        for (int i = 0; i < 48000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
        return f.clock_hz();
    };

    Flux f;
    f.init(48000.f, s_buf);
    arm(f);
    f.set_time_mod(0.75f);                  // x2
    CHECK(settle(f) == doctest::Approx(16384.f).epsilon(0.02f));

    f.init(48000.f, s_buf);      // the re-init
    arm(f);
    f.set_time_mod(0.75f);                  // the SAME value the host still holds
    // With a stale guard this push is dropped, _time_mult stays at init()'s
    // neutral 1.0, and the clock reads 8192 instead of 16384.
    CHECK(settle(f) == doctest::Approx(16384.f).epsilon(0.02f));
}

TEST_CASE("flux: the single echo lands identically on both channels") {
    // The mono collapse's whole structural claim in one assertion: there is
    // one line, and its output is added to L and R unchanged. Asserted only
    // in the silent tail, where the dry signal is exactly 0 in both channels
    // and l and r therefore hold nothing but the echo -- so this is a bit-
    // exact comparison, not a tolerance (design spec 2026-07-29-flux-mono §3).
    //
    // A TOPOLOGY test, not a taste test. It asserts nothing about what the
    // echo sounds like, so the owner's listening pass cannot make it stale.
    Flux f;
    f.init(48000.f, s_buf);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);
    f.set_mix(1.f);
    f.set_feedback(0.5f);
    bool saw_echo = false;
    for (int i = 0; i < 96000; ++i) {
        const float s = (i < 480) ? 0.4f * std::sin(0.2f * i) : 0.f;
        float l = s, r = 0.f;                 // hard left: maximally asymmetric
        f.process(l, r);
        if (i >= 480) {
            REQUIRE(l == r);                  // dry is silent; only wet remains
            if (std::fabs(l) > 1e-4f) saw_echo = true;
        }
    }
    // Guard against the assertion above passing on a dead echo (0 == 0).
    CHECK(saw_echo);
}

TEST_CASE("flux: the echo is driven by the mono sum") {
    // (s,s) and (2s,0) have the SAME mono sum, so they must give the identical
    // echo -- that is what 0.5f * (l + r) means, stated as arithmetic. Both
    // sums are exactly s in float32, so this is not an approximation.
    //
    // (s,0) sums to half and must give a quieter echo. Deliberately NOT
    // asserted as exactly -6 dB: the line runs a saturator and a compander, so
    // its output is not a linear function of its input and any decibel figure
    // would be a claim this test cannot support. Direction and symmetry of
    // the sum are what is structural here -- NOT the 0.5 weight itself: the
    // left_double/both comparison below is w*(s+s) vs. w*(2s+0), the same
    // expression for every w, so it cannot discriminate 0.5 from any other
    // constant. That constant is pinned by the source comment in
    // Flux::process and by design spec 2026-07-29-flux-mono §3, not by this
    // test.
    auto tail_rms = [](float gl, float gr) {
        Flux f;
        f.init(48000.f, s_buf);
        f.set_on(true, true);
        f.set_bpm(120.f);
        f.set_rate(3);
        f.set_mix(1.f);
        f.set_feedback(0.5f);
        double acc = 0.0;
        int n = 0;
        for (int i = 0; i < 96000; ++i) {
            const float s = (i < 480) ? 0.4f * std::sin(0.2f * i) : 0.f;
            float l = s * gl, r = s * gr;
            f.process(l, r);
            if (i >= 24000) { acc += double(l) * double(l); ++n; }
        }
        return n ? std::sqrt(acc / n) : 0.0;
    };
    const double both        = tail_rms(1.f, 1.f);
    const double left_double = tail_rms(2.f, 0.f);
    const double left_only   = tail_rms(1.f, 0.f);
    INFO("both=" << both << " left_double=" << left_double << " left_only=" << left_only);
    REQUIRE(both > 1e-6);
    CHECK(left_double == doctest::Approx(both).epsilon(1e-9));   // same sum
    CHECK(left_only < 0.85 * both);                              // quieter
    CHECK(left_only > 0.20 * both);                              // not dead
}
