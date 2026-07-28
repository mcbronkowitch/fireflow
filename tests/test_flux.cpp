#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "fx/flux.h"
#include "mod/divisions.h"
using namespace spky;

static float s_buf_l[Flux::kMaxSamples];
static float s_buf_r[Flux::kMaxSamples];

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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, nullptr, nullptr);
    f.set_on(true, true);
    CHECK(!f.has_buffers());
    CHECK(!f.engaged());
    float l = 0.5f, r = 0.5f;
    f.process(l, r);
    CHECK(l == 0.5f);
}

TEST_CASE("flux: feedback produces decaying repeats") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
        f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_drive(0.7f);
    f.init(48000.f, s_buf_l, s_buf_r);     // simulate the re-init
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
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_stages(0.2f);
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_stages(0.2f);                     // SAME value again
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.stages() == doctest::Approx(1024).epsilon(0.01));
}

TEST_CASE("flux slice: norm endpoints hit 1/2 and 1/32") {
    CHECK(kFluxRateCount == 12);
    CHECK(kFluxRateOffset == 5);
    CHECK(std::string(kDivisions[kFluxRateOffset + flux_division_index(0.f)].name) == "1/2");
    CHECK(std::string(kDivisions[kFluxRateOffset + flux_division_index(1.f)].name) == "1/32");
    CHECK(std::string(kDivisions[kFluxRateOffset + flux_division_index(3.f/11.f)].name) == "1/4");
}

static float s_buf_l2[Flux::kMaxSamples];
static float s_buf_r2[Flux::kMaxSamples];

static RhythmView drag_view(int32_t g0, int32_t g1) {
    RhythmView rv;
    rv.gap[0] = g0;
    rv.gap[1] = g1;
    rv.valid  = true;
    return rv;
}

TEST_CASE("flux: DRAG at 0 is bit-identical to a Flux that never heard a rhythm") {
    Flux plain, dragged;
    plain.init(48000.f, s_buf_l, s_buf_r);
    dragged.init(48000.f, s_buf_l2, s_buf_r2);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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

    // _drag_phase is never reset to 0 by a run() boundary, only by a flip --
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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
    f.init(48000.f, s_buf_l, s_buf_r);
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

TEST_CASE("flux: LINK's negative half is inert until the thinning lands") {
    // Task 1 of the link plan wires the bipolar range and nothing else. This
    // case is REPLACED in Task 2 by the real thinning tests -- it exists so
    // the intermediate state is asserted rather than assumed.
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);                       // ladder = 0.5 s
    f.set_rhythm(drag_view(12000, 6000));
    f.set_link(-1.f);
    CHECK(f.drag_time_s() == doctest::Approx(0.5f).epsilon(0.001));
}
