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
    // Measured (not exact equality, unlike the kMinStages case above): the 30
    // ms one-pole runs in float32, and at this magnitude (16384) its ULP
    // (~0.002) versus the tiny per-sample increment near convergence makes it
    // stall about 0.7 stages short of the exact target -- settles at 16383,
    // not 16384. The endpoint still reaches the top of the range; only exact
    // last-bit convergence doesn't survive float32 at this scale.
    CHECK(settled_stages(1.f) == doctest::Approx(Flux::kMaxStages).epsilon(0.001));
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
