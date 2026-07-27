#include <doctest/doctest.h>
#include <cmath>
#include "fx/bbd.h"
using namespace spky;

TEST_CASE("bbd_clock_hz: f_clk = stages / (2 * t_d)") {
    // 8192 stages at 250 ms -> 16384 Hz. The DMM's own numbers: 8192 stages
    // at 550 ms -> 7447 Hz, against the EH-7850 factory calibration document's
    // measured clock period of 120-140 us (7143-8333 Hz) at maximum delay.
    CHECK(bbd_clock_hz(0.25f, 8192) == doctest::Approx(16384.f));
    CHECK(bbd_clock_hz(0.550f, 8192) == doctest::Approx(7447.27f).epsilon(0.001));
    CHECK(bbd_clock_hz(1.0f, 4096) == doctest::Approx(2048.f));
}

TEST_CASE("bbd_clock_hz: the 32 kHz ceiling is hard") {
    // At 8192 stages the ceiling engages below 128 ms -- exactly where the
    // fixed post-BBD filter chain dominates anyway (spec "The clock law").
    CHECK(bbd_clock_hz(0.128f, 8192) == doctest::Approx(32000.f));
    CHECK(bbd_clock_hz(0.001f, 16384) == bbd_tuning::kClockMaxHz);
    CHECK(bbd_clock_hz(1e-9f, 16384) == bbd_tuning::kClockMaxHz);
}

TEST_CASE("bbd_clock_hz: there is no floor, and no way to return garbage") {
    // The mud at the long end is the point -- a 10 s delay at 512 stages runs
    // the line at 25.6 Hz and that is allowed. What is NOT allowed is a
    // non-finite or negative clock reaching BbdLine.
    CHECK(bbd_clock_hz(10.f, 512) == doctest::Approx(25.6f));
    CHECK(bbd_clock_hz(0.f, 8192) == bbd_tuning::kClockMaxHz);
    CHECK(bbd_clock_hz(-1.f, 8192) == bbd_tuning::kClockMaxHz);
    CHECK(std::isfinite(bbd_clock_hz(NAN, 8192)));
}

TEST_CASE("bbd_tuning: the ceiling buys twice the bandwidth the chain needs") {
    // 32 kHz / 4 = 8 kHz of BBD bandwidth against a fixed filter chain at
    // ~3.6 kHz: inaudible by construction, and it bounds the worst case at
    // 2 * 32000 / 48000 = 1.33 ticks per sample.
    CHECK(bbd_tuning::kClockMaxHz * 0.25f > 2.f * bbd_tuning::kFilterHz);
    CHECK(2.f * bbd_tuning::kClockMaxHz / 48000.f < 1.5f);
}

// --- the fixed filter chain -------------------------------------------------
// The analog spec is H(s) = sum_m R[m] / (s - P[m]). These tests pin that it
// really is a 3rd-order Butterworth at kFilterHz, because every claim the
// model makes downstream ("the filters do not move", "long delays are dark
// because of the CLOCK, not the filters") rests on this chain being the one
// the DMM has and staying where it is put.

static float analog_mag_db(bool out_kind, float hz) {
    Cf poles[bbd_tuning::kFiltOrder], res[bbd_tuning::kFiltOrder];
    bbd_analog_spec(out_kind, poles, res);
    const Cf s{ 0.f, TWO_PI * hz };
    Cf h{ 0.f, 0.f };
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m)
        h = cf_add(h, cf_div(res[m], Cf{ s.re - poles[m].re, s.im - poles[m].im }));
    return 20.f * std::log10(std::sqrt(h.re * h.re + h.im * h.im));
}

TEST_CASE("bbd filter: DC gain is exactly unity") {
    // H(0) = sum(-R/P) == 1. If this drifts, every level in the chain drifts
    // with it and the compander's reference stops meaning anything.
    CHECK(analog_mag_db(false, 0.f) == doctest::Approx(0.f).epsilon(0.001));
    CHECK(analog_mag_db(true,  0.f) == doctest::Approx(0.f).epsilon(0.001));
}

TEST_CASE("bbd filter: -3 dB at kFilterHz, -18 dB/oct above it") {
    CHECK(analog_mag_db(false, bbd_tuning::kFilterHz)
          == doctest::Approx(-3.0103f).epsilon(0.01));
    // Butterworth order 3: one octave up is -18 dB, two octaves -36 dB
    // (asymptotically). Generous windows -- the shape is what is pinned.
    const float oct1 = analog_mag_db(false, 2.f * bbd_tuning::kFilterHz);
    const float oct2 = analog_mag_db(false, 4.f * bbd_tuning::kFilterHz);
    CHECK(oct1 < -16.f);
    CHECK(oct1 > -22.f);
    CHECK(oct2 - oct1 < -16.f);
    CHECK(oct2 - oct1 > -20.f);
}

TEST_CASE("bbd filter: every pole is in the left half plane") {
    // A pole with Re >= 0 makes P[m] = exp(ts*p) leave the unit disc and the
    // parallel branches diverge -- silently, over seconds. This is the guard
    // that a hand-edited kFilterHz or kFiltOrder cannot get past.
    for (bool out_kind : { false, true }) {
        Cf poles[bbd_tuning::kFiltOrder], res[bbd_tuning::kFiltOrder];
        bbd_analog_spec(out_kind, poles, res);
        for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
            CHECK(poles[m].re < 0.f);
            CHECK(std::isfinite(res[m].re));
            CHECK(std::isfinite(res[m].im));
        }
    }
}

TEST_CASE("bbd filter: the discretised poles sit strictly inside the unit disc") {
    const BbdFilterCoef& fin = bbd_filter_in(48000.f);
    const BbdFilterCoef& fout = bbd_filter_out(48000.f);
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
        const float rin = std::sqrt(fin.P[m].re * fin.P[m].re + fin.P[m].im * fin.P[m].im);
        const float rout = std::sqrt(fout.P[m].re * fout.P[m].re + fout.P[m].im * fout.P[m].im);
        CHECK(rin < 1.f);
        CHECK(rout < 1.f);
        CHECK(rin > 0.f);
    }
    // H is the output filter's DC feed-through term, sum(-R/P) == H(0) == 1.
    CHECK(fout.H == doctest::Approx(1.f).epsilon(0.001));
}

TEST_CASE("bbd filter: interpolate_g matches the table at both endpoints") {
    // d == 0 and d == 1 must hit rows 0 and N-1 exactly, not one row short.
    // An off-by-one here shows up as a faint, clock-rate-dependent whine that
    // is very hard to attribute later.
    const BbdFilterCoef& f = bbd_filter_in(48000.f);
    Cf g[bbd_tuning::kFiltOrder];
    f.interpolate_g(0.f, g);
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
        CHECK(g[m].re == doctest::Approx(f.G[0][m].re));
        CHECK(g[m].im == doctest::Approx(f.G[0][m].im));
    }
    f.interpolate_g(1.f, g);
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
        CHECK(g[m].re == doctest::Approx(f.G[bbd_tuning::kInterpSteps - 1][m].re));
        CHECK(g[m].im == doctest::Approx(f.G[bbd_tuning::kInterpSteps - 1][m].im));
    }
    // And it must not read past the table for d slightly out of range.
    f.interpolate_g(1.0001f, g);
    CHECK(std::isfinite(g[0].re));
}

TEST_CASE("bbd filter: the table is built once per sample rate") {
    // Four lines call this at init. Rebuilding the 3 kB of tables four times
    // would be harmless but silly; rebuilding them from a DIFFERENT sample
    // rate and handing the stale result to the other lines would not be.
    const BbdFilterCoef& a = bbd_filter_in(48000.f);
    const BbdFilterCoef& b = bbd_filter_in(48000.f);
    CHECK(&a == &b);
    const BbdFilterCoef& c = bbd_filter_in(44100.f);
    CHECK(&c == &a);                       // same storage, rebuilt in place
    CHECK(c.P[0].re != doctest::Approx(0.f));
    // Put it back so later cases in this file see 48 kHz tables.
    bbd_filter_in(48000.f);
    bbd_filter_out(48000.f);
}

// --- BbdLine ----------------------------------------------------------------
// The core knows nothing about music: no BPM, no divisions, no feedback. That
// is what makes it testable against physics rather than against itself.

static float s_bbd_mem[8192];

// Peak-detect the arrival of a short burst, so a single-sample impulse's
// filtered smear does not decide the answer.
static int first_arrival(BbdLine& line, int n, int burst_len = 16) {
    float peak = 0.f;
    int peak_at = -1;
    for (int i = 0; i < n; ++i) {
        const float x = (i < burst_len) ? 1.f : 0.f;
        const float y = line.Process(x);
        if (i > burst_len * 4 && std::fabs(y) > peak) { peak = std::fabs(y); peak_at = i; }
    }
    return peak > 1e-3f ? peak_at : -1;
}

// RMS of the line's output for a steady sine, after settling.
static float line_rms(BbdLine& line, float hz, float sr, int settle, int measure) {
    double acc = 0.0;
    for (int i = 0; i < settle + measure; ++i) {
        const float x = std::sin(TWO_PI * hz * static_cast<float>(i) / sr);
        const float y = line.Process(x);
        if (i >= settle) acc += static_cast<double>(y) * y;
    }
    return static_cast<float>(std::sqrt(acc / measure));
}

TEST_CASE("bbd line: the arrival lands where stages/(2*f_clk) says it does") {
    BbdLine line;
    line.Init(s_bbd_mem, 8192, 48000.f);
    line.SetStages(8192);
    // Driven directly, not through bbd_clock_hz: this case is about the LINE,
    // and the ladder's ceiling has its own tests.
    line.SetClock(16384.f);                    // 8192 stages -> 250 ms
    const int idx = first_arrival(line, 20000);
    REQUIRE(idx > 0);
    CHECK(idx > 11700);                        // 12000 samples = 250 ms @48k
    CHECK(idx < 12400);
}

TEST_CASE("bbd line: halving the clock doubles the delay") {
    BbdLine a, b;
    static float mem_a[8192], mem_b[8192];
    a.Init(mem_a, 8192, 48000.f);
    b.Init(mem_b, 8192, 48000.f);
    a.SetStages(8192);
    b.SetStages(8192);
    a.SetClock(16384.f);                       // 250 ms
    b.SetClock(8192.f);                        // 500 ms
    const int ia = first_arrival(a, 40000);
    const int ib = first_arrival(b, 40000);
    REQUIRE(ia > 0);
    REQUIRE(ib > 0);
    CHECK(static_cast<float>(ib) / static_cast<float>(ia)
          == doctest::Approx(2.f).epsilon(0.05));
}

TEST_CASE("bbd line: bandwidth follows the clock, not the filters") {
    // THE claim of the design: long delays are dark because of the CLOCK.
    // Two clocks an octave apart, both far below the fixed 3.6 kHz chain, and
    // a probe tone that sits above the lower one's corner and below the
    // higher one's. f_clk/4 is 2 kHz and 1 kHz respectively; the probe is at
    // 1.4 kHz. A model whose bandwidth did NOT track the clock would give the
    // same level twice.
    static float mem_hi[8192], mem_lo[8192];
    BbdLine hi, lo;
    hi.Init(mem_hi, 8192, 48000.f);
    lo.Init(mem_lo, 8192, 48000.f);
    hi.SetStages(2048);                        // short line: settles fast
    lo.SetStages(2048);
    hi.SetClock(8000.f);                       // corner ~2000 Hz
    lo.SetClock(4000.f);                       // corner ~1000 Hz
    const float ref_hi = line_rms(hi, 100.f, 48000.f, 24000, 24000);
    const float ref_lo = line_rms(lo, 100.f, 48000.f, 24000, 24000);
    hi.Reset(); lo.Reset();
    const float p_hi = line_rms(hi, 1400.f, 48000.f, 24000, 24000);
    const float p_lo = line_rms(lo, 1400.f, 48000.f, 24000, 24000);
    REQUIRE(ref_hi > 1e-3f);
    REQUIRE(ref_lo > 1e-3f);
    const float rel_hi = p_hi / ref_hi;        // 1.4 kHz vs 100 Hz, fast clock
    const float rel_lo = p_lo / ref_lo;        // ... and slow clock
    INFO("rel_hi=" << rel_hi << " rel_lo=" << rel_lo);
    CHECK(rel_lo < rel_hi * 0.8f);             // the slow clock IS darker
}

TEST_CASE("bbd line: changing the clock bends the pitch of what is stored") {
    // EHX documents this as a feature. There is no crossfade in the physical
    // device and there must be none here: the stored charge packets simply
    // come out faster.
    static float mem[8192];
    BbdLine line;
    line.Init(mem, 8192, 48000.f);
    line.SetStages(4096);
    line.SetClock(8192.f);                     // 4096/(2*8192) = 250 ms
    // Fill the line with a 400 Hz tone for well over one delay's worth.
    for (int i = 0; i < 20000; ++i)
        line.Process(std::sin(TWO_PI * 400.f * static_cast<float>(i) / 48000.f));
    // Now double the clock and read the SAME material back at 2x.
    line.SetClock(16384.f);
    int crossings = 0;
    float prev = 0.f;
    const int window = 4000;                   // 83 ms of readback
    for (int i = 0; i < window; ++i) {
        const float y = line.Process(0.f);     // silence in: only stored charge
        if (i > 200 && prev <= 0.f && y > 0.f) ++crossings;
        prev = y;
    }
    // 800 Hz over 79 ms is ~63 positive-going crossings; 400 Hz would be ~32.
    INFO("crossings=" << crossings);
    CHECK(crossings > 45);
    CHECK(crossings < 85);
}

TEST_CASE("bbd line: SPIKE -- 819 Hz clock (5 s at 8192 stages) stays stable") {
    // Spec risk 1, faced first. 0.034 ticks per audio sample: most samples
    // produce NO tick at all and the parallel branches coast on their own
    // pole advance. If the partial-fraction decomposition is going to fall
    // apart anywhere, it is here.
    static float mem[8192];
    BbdLine line;
    line.Init(mem, 8192, 48000.f);
    line.SetStages(8192);
    line.SetClock(bbd_clock_hz(5.f, 8192));    // 819.2 Hz
    double energy = 0.0;
    float peak = 0.f;
    for (int i = 0; i < 480000; ++i) {         // 10 s: two full delays
        const float x = 0.5f * std::sin(TWO_PI * 100.f * static_cast<float>(i) / 48000.f);
        const float y = line.Process(x);
        REQUIRE(std::isfinite(y));
        if (std::fabs(y) > peak) peak = std::fabs(y);
        if (i > 288000) energy += static_cast<double>(y) * y;   // after 6 s
    }
    INFO("peak=" << peak);
    CHECK(peak < 8.f);                         // bounded, not exploding
    CHECK(energy > 1e-3);                      // and not silent either:
    // a 100 Hz tone sits below the 205 Hz corner this clock implies, so it
    // must survive attenuated but audible. Silence here means the model has
    // collapsed at low clock rates and the design needs the spec's fallback.
}

TEST_CASE("bbd line: a stage change mid-run drifts, it does not explode") {
    // STAGES is slewed through the 30 ms path (spec "Modulation"): swapping
    // the chip is not what a physical part does, but the artefact it produces
    // -- a drift in time and pitch -- is exactly the class this device
    // already makes. What it must never produce is a NaN or a read past the
    // buffer.
    static float mem[8192];
    BbdLine line;
    line.Init(mem, 8192, 48000.f);
    line.SetStages(8192);
    line.SetClock(8192.f);
    for (int i = 0; i < 96000; ++i) {
        if (i % 64 == 0) {
            const int n = 512 + (i / 64) % 15872;   // sweeps 512 .. 16383
            line.SetStages(n);
        }
        const float y = line.Process(0.3f * std::sin(0.05f * static_cast<float>(i)));
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 20.f);
    }
    CHECK(line.cells() >= bbd_tuning::kMinStages / 2);
    CHECK(line.cells() <= 8192);
}

TEST_CASE("bbd line: a zero clock holds instead of crashing") {
    // No floor on the clock means f_clk can be pushed arbitrarily low by a
    // very slow tempo. Zero ticks per sample must be a hold, not a divide by
    // zero -- note the 1/fclk inside the tick loop.
    static float mem[8192];
    BbdLine line;
    line.Init(mem, 8192, 48000.f);
    line.SetStages(1024);
    line.SetClock(0.f);
    for (int i = 0; i < 1000; ++i) CHECK(std::isfinite(line.Process(0.5f)));
    line.SetClock(-1.f);
    for (int i = 0; i < 1000; ++i) CHECK(std::isfinite(line.Process(0.5f)));
}

TEST_CASE("bbd line: a degenerate Init holds instead of writing through null") {
    // Init(nullptr, 0, sr) leaves cells_ at its floor of 1 -- nonzero -- so a
    // tick-loop guard that only checks cells_ would still dereference mem_.
    // No tick may run while the line has no usable memory.
    BbdLine line;
    line.Init(nullptr, 0, 48000.f);
    line.SetClock(8192.f);                     // well above zero: many ticks
    for (int i = 0; i < 10000; ++i)
        CHECK(std::isfinite(line.Process(0.5f)));
}
