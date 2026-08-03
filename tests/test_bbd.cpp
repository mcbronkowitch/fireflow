#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include "fx/bbd.h"
#include "fx/flux.h"
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
    // Two lines call this at init. Rebuilding the 3 kB of tables twice
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

static uint32_t fold_bbd_sample(uint32_t hash, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (hash ^ bits) * 16777619u;
}

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

TEST_CASE("bbd line: fixed-stage output stays bit-identical") {
    BbdLine line;
    line.Init(s_bbd_mem, 8192, 48000.f);
    line.SetStages(8192);                  // 4096 cells
    line.SetDither(4e-5f);
    line.Reset();                          // settle that requested length before audio
    line.SetClock(16384.f);

    uint32_t hash = 2166136261u;
    for (int i = 0; i < 48000; ++i) {
        const float x = 0.25f * std::sin(
            TWO_PI * 137.f * static_cast<float>(i) / 48000.f)
            + (i < 16 ? 0.5f : 0.f);
        hash = fold_bbd_sample(hash, line.Process(x));
    }
    CHECK(hash == 0x12156b08u);
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

// --- Compander --------------------------------------------------------------

TEST_CASE("compander: the 10 ms time constant is measurable") {
    // tau = 10 kOhm * 1 uF. A one-pole reaches 1 - 1/e = 63.2 % of a step in
    // exactly tau, and this is the only place that number is observable.
    Compander c;
    c.Init(48000.f);
    const float target = 0.5f;                       // |x| of a DC step
    const int tau_samples = static_cast<int>(bbd_tuning::kCompTauS * 48000.f);
    for (int i = 0; i < tau_samples; ++i) c.Compress(target);
    CHECK(c.env_comp() == doctest::Approx(0.632f * target).epsilon(0.05));
    for (int i = 0; i < 4 * tau_samples; ++i) c.Compress(target);
    CHECK(c.env_comp() == doctest::Approx(target).epsilon(0.05));
}

TEST_CASE("compander: compress then expand is unity in steady state") {
    // 2:1 followed by 1:2 must give back the level it was handed, or every
    // gain staging decision downstream is built on sand. Checked at three
    // levels spanning 30 dB.
    for (float amp : { 0.03f, 0.1f, 0.5f }) {
        Compander c;
        c.Init(48000.f);
        double in_sq = 0.0, out_sq = 0.0;
        for (int i = 0; i < 48000; ++i) {
            const float x = amp * std::sin(TWO_PI * 220.f * static_cast<float>(i) / 48000.f);
            const float y = c.Expand(c.Compress(x));
            if (i > 24000) { in_sq += (double)x * x; out_sq += (double)y * y; }
        }
        const float ratio = static_cast<float>(std::sqrt(out_sq / in_sq));
        INFO("amp=" << amp << " ratio=" << ratio);
        CHECK(ratio == doctest::Approx(1.f).epsilon(0.15));
    }
}

TEST_CASE("compander: 2:1 really is 2:1 on the way in") {
    // A 12 dB input change must come out as a 6 dB change from Compress
    // alone. This is what pulls the tails down harder than a linear delay
    // would -- the audible signature of the part.
    auto compressed_rms = [](float amp) {
        Compander c;
        c.Init(48000.f);
        double acc = 0.0;
        for (int i = 0; i < 48000; ++i) {
            const float x = amp * std::sin(TWO_PI * 220.f * static_cast<float>(i) / 48000.f);
            const float y = c.Compress(x);
            if (i > 24000) acc += (double)y * y;
        }
        return static_cast<float>(std::sqrt(acc / 24000.0));
    };
    const float lo = compressed_rms(0.05f);
    const float hi = compressed_rms(0.2f);           // +12 dB in
    const float db = 20.f * std::log10(hi / lo);
    INFO("delta_db=" << db);
    CHECK(db > 4.f);
    CHECK(db < 8.f);
}

TEST_CASE("compander: gain is bounded in both directions") {
    // Silence must not be amplified into the noise floor of the universe,
    // and a loud transient must not be expanded without limit. The bounds are
    // derived from kCompRef, not tasted -- see the constants.
    Compander c;
    c.Init(48000.f);
    for (int i = 0; i < 48000; ++i) CHECK(std::isfinite(c.Compress(0.f)));
    CHECK(std::fabs(c.Compress(1e-9f)) < 1e-6f);     // gain capped at 4
    Compander d;
    d.Init(48000.f);
    for (int i = 0; i < 48000; ++i) {
        const float y = d.Expand(3.f);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 16.f);
    }
}

// --- BbdEcho ----------------------------------------------------------------

static float s_echo_mem[8192];

TEST_CASE("bbd echo: feedback produces decaying repeats") {
    BbdEcho e;
    e.Init(48000.f, s_echo_mem, 8192);
    e.SetStages(8192);
    e.SetDrive(0.f);
    e.SetFeedback(0.5f);
    const float hz = bbd_clock_hz(0.25f, 8192);      // 250 ms
    std::vector<float> out(60000);
    for (int i = 0; i < 60000; ++i)
        out[i] = e.Process((i < 32) ? 1.f : 0.f, hz);
    auto peak_around = [&](int c) {
        float p = 0.f;
        for (int i = c - 900; i < c + 900; ++i) p = std::max(p, std::fabs(out[i]));
        return p;
    };
    const float p1 = peak_around(12000);
    const float p2 = peak_around(24000);
    const float p3 = peak_around(36000);
    INFO("p1=" << p1 << " p2=" << p2 << " p3=" << p3);
    CHECK(p1 > 1e-3f);
    CHECK(p2 < p1);
    CHECK(p3 < p2);
}

TEST_CASE("bbd echo: each repeat is darker than the last") {
    // The feedback path re-enters BEFORE the compander, so every repeat pays
    // the whole chain again and bandwidth shrinks multiplicatively. That is
    // the difference between this and a delay with a one-off filter on it.
    BbdEcho e;
    static float mem[8192];
    e.Init(48000.f, mem, 8192);
    e.SetStages(8192);
    e.SetDrive(0.f);
    e.SetFeedback(0.7f);
    const float hz = bbd_clock_hz(0.25f, 8192);
    std::vector<float> out(60000);
    for (int i = 0; i < 60000; ++i) {
        // A burst with real high-frequency content to lose. 4200 Hz, not the
        // plan's 1500 Hz: 1500 Hz sits below BOTH the fixed 3600 Hz filter
        // chain and the BBD's own f_clk/4 = 4096 Hz corner at this clock, so
        // a second pass has almost no brightness left to remove (measured
        // b1=0.038107, b2=0.038088 -- a 0.05% margin, too thin to catch a
        // regression). 4200 Hz clears both corners and measures a real
        // multiplicative loss (b1=0.209076, b2=0.067975 -- 67%).
        const float x = (i < 480)
            ? 0.5f * std::sin(TWO_PI * 4200.f * static_cast<float>(i) / 48000.f)
            : 0.f;
        out[i] = e.Process(x, hz);
    }
    // High-frequency energy per repeat, measured as first-difference energy
    // normalised by total energy -- a cheap brightness proxy that needs no FFT.
    auto brightness = [&](int c) {
        double hf = 0.0, tot = 0.0;
        for (int i = c - 700; i < c + 700; ++i) {
            const double d = out[i] - out[i - 1];
            hf += d * d;
            tot += (double)out[i] * out[i];
        }
        return tot > 0.0 ? hf / tot : 0.0;
    };
    const double b1 = brightness(12300);
    const double b2 = brightness(24300);
    INFO("b1=" << b1 << " b2=" << b2);
    CHECK(b2 < b1);
}

TEST_CASE("bbd echo: feedback at max blooms but stays bounded") {
    // FEEDBACK keeps its 1.2 over unity so self-oscillation stays reachable
    // -- documented behaviour of the original. The bound now comes from
    // saturation WITHIN the loop rather than a tanh on the read path.
    BbdEcho e;
    static float mem[8192];
    e.Init(48000.f, mem, 8192);
    e.SetStages(8192);
    e.SetDrive(0.5f);
    e.SetFeedback(1.2f);
    const float hz = bbd_clock_hz(0.25f, 8192);
    float peak = 0.f;
    double late_sq = 0.0;
    int late_n = 0;
    for (int i = 0; i < 480000; ++i) {               // 10 s
        const float y = e.Process((i < 32) ? 1.f : 0.f, hz);
        REQUIRE(std::isfinite(y));
        peak = std::max(peak, std::fabs(y));
        if (i >= 432000) { late_sq += (double)y * y; ++late_n; }
    }
    const float late_rms = static_cast<float>(std::sqrt(late_sq / late_n));
    INFO("peak=" << peak << " late_rms=" << late_rms);
    CHECK(peak > 0.2f);                              // it did bloom
    CHECK(peak < 12.f);                              // and it stayed bounded
    CHECK(late_rms > 0.01f);                         // and it sustained
}

TEST_CASE("bbd echo: feedback below unity decays to silence") {
    BbdEcho e;
    static float mem[8192];
    e.Init(48000.f, mem, 8192);
    e.SetStages(8192);
    e.SetDrive(0.f);
    e.SetFeedback(0.6f);
    const float hz = bbd_clock_hz(0.25f, 8192);
    float early = 0.f, late = 0.f;
    for (int i = 0; i < 480000; ++i) {
        const float y = e.Process((i < 32) ? 1.f : 0.f, hz);
        if (i > 11000 && i < 13000) early = std::max(early, std::fabs(y));
        if (i > 400000) late = std::max(late, std::fabs(y));
    }
    CHECK(early > 1e-3f);
    CHECK(late < early * 0.05f);
}

TEST_CASE("bbd echo: DRIVE dirties every pass, not just the input") {
    // This is what makes DRIVE not redundant with GRIT: GRIT runs before FLUX
    // and dirties the input once; DRIVE sits inside the loop.
    auto harmonic_growth = [](float drive) {
        BbdEcho e;
        static float mem[8192];
        e.Init(48000.f, mem, 8192);
        e.SetStages(8192);
        e.SetDrive(drive);
        e.SetFeedback(0.85f);
        const float hz = bbd_clock_hz(0.25f, 8192);
        // A pure 200 Hz tone for one delay's worth, then silence: what comes
        // back is the loop's own doing.
        std::vector<float> out(140000);
        for (int i = 0; i < 140000; ++i) {
            const float x = (i < 12000)
                ? 0.4f * std::sin(TWO_PI * 200.f * static_cast<float>(i) / 48000.f)
                : 0.f;
            out[i] = e.Process(x, hz);
        }
        // Compare the first repeat's waveform crest factor with the fourth's.
        // Saturation flattens peaks: crest falls as harmonics accumulate.
        auto crest = [&](int c) {
            float pk = 0.f;
            double sq = 0.0;
            for (int i = c; i < c + 6000; ++i) {
                pk = std::max(pk, std::fabs(out[i]));
                sq += (double)out[i] * out[i];
            }
            const float rms = static_cast<float>(std::sqrt(sq / 6000.0));
            return rms > 0.f ? pk / rms : 0.f;
        };
        return crest(15000) - crest(51000);          // repeat 1 vs repeat 4
    };
    const float clean = harmonic_growth(0.f);
    // 0.5, not the top of the knob: delta vs DRIVE is an inverted U -- the
    // compounding effect peaks near the middle of the range and collapses at
    // the top, where a single pass already saturates the burst flat and
    // leaves repeat 1 with no headroom left to compound over. Measured at
    // the shipped 0..+12 range: dirty=0.2464 vs clean=0.0240524 at drive
    // 0.5. Both numbers moved again from the -6..+24 range's values
    // (dirty=0.221906, clean=-0.00150919) and from the original pre-fix
    // values (dirty=0.0617, clean=0.0048) -- DRIVE 0 is unity gain now
    // rather than boosted or attenuated -- but `dirty > clean` has held
    // through every revision of this constant pair.
    const float dirty = harmonic_growth(0.5f);
    INFO("clean=" << clean << " dirty=" << dirty);
    CHECK(dirty > clean);
}

// --- retired: "DRIVE does not move the small-signal loop gain" ------------
// That contract was pinned by the OLD makeup-gain law, sat_out_ = kSatCeil/g,
// which held small-signal loop gain at unity by construction -- deliberate,
// and this was its test. The 2026-07-27 DRIVE investigation
// (.superpowers/sdd/2026-07-27-flux-bbd-delay/drive-investigation.md) traced
// the reason DRIVE read as inaudible back to that same law: at the dB range
// then in force (-6..+24) it shrank the saturator's MAXIMUM possible output
// by exactly that range's own 30 dB width across the knob (1.796 -> 0.057),
// which measured as a 14.0 dB peak-level drop in the actual echo return
// between DRIVE 0 and DRIVE 1 -- the one part of the knob's travel that does
// add real distortion also made the echo quieter at the same rate, so the
// two cues cancelled out perceptually. The owner's fix, after listening:
// sat_out_ is now the FIXED kSatCeil. The real MN3005's headroom does not
// recede as you drive it harder, and neither does this one anymore. That
// retires the unity-gain contract on purpose -- small-signal loop gain now
// equals `g` itself, so it is no longer DRIVE-independent -- and the three
// cases below pin what replaces it: the ceiling itself does not move, the
// loop gain now rises (monotonically, the thing that used to be forbidden),
// and the loop still cannot run away even at the new worst case (max DRIVE,
// max FEEDBACK).
//
// A follow-up measurement then moved kDriveLoDb/kDriveHiDb themselves from
// that first -6..+24 range to the shipped 0..+12 (see the constants' own
// comment in bbd.h and drive-fix-report.md's "DRIVE range follow-up"): the
// -6..+24 range pushed DRIVE 0's self-oscillation FEEDBACK threshold to
// ~1.71, past Flux's 1.2 ceiling, making self-oscillation unreachable at
// DRIVE 0 -- a regression this file did not have a test for, which is why it
// went unnoticed. The three cases below were re-measured against 0..+12 and
// hold with the same relations; a fourth case pins the constraint that broke
// (self-oscillation reachable at DRIVE 0, within FEEDBACK's 1.2 ceiling).

TEST_CASE("bbd echo: the saturator's output ceiling does not move with DRIVE") {
    // sat_out_ = kSatCeil, fixed, regardless of DRIVE. Drive the input hard
    // enough that fast_tanh clamps at every DRIVE setting -- even DRIVE 0's
    // smallest sat_in_ (kDriveLoDb=0 -> g=1 -> sat_in_ = 1/kSatCeil ~= 1.111)
    // needs an input over 3.646739/1.111 = 3.28 to reach the knee, so
    // amplitude 50 clamps deeply everywhere on the knob -- and feedback is
    // held at 0 so only the forward chain (no loop recirculation) is under
    // test. If the ceiling really is fixed, the peak that comes out of the
    // delay should land in the same place no matter where DRIVE sits.
    auto clamped_peak = [](float drive) {
        BbdEcho e;
        static float mem[8192];
        e.Init(48000.f, mem, 8192);
        e.SetStages(8192);
        e.SetFeedback(0.f);
        e.SetDrive(drive);
        const float hz = bbd_clock_hz(0.25f, 8192);
        float peak = 0.f;
        for (int i = 0; i < 20000; ++i) {
            const float x = (i < 4000) ? 50.f * std::sin(TWO_PI * 300.f * static_cast<float>(i) / 48000.f) : 0.f;
            const float y = e.Process(x, hz);
            if (i > 12000 && i < 16000) peak = std::max(peak, std::fabs(y));
        }
        return peak;
    };
    const float p0   = clamped_peak(0.f);
    const float p25  = clamped_peak(0.25f);
    const float p50  = clamped_peak(0.5f);
    const float p75  = clamped_peak(0.75f);
    const float p100 = clamped_peak(1.f);
    INFO("p0=" << p0 << " p25=" << p25 << " p50=" << p50 << " p75=" << p75 << " p100=" << p100);
    // Measured at the shipped 0..+12 range: 1.51143 / 1.51193 / 1.51202 /
    // 1.51202 / 1.51202 -- max deviation from DRIVE 0 is 0.04%. epsilon(0.02)
    // (2%) leaves ~50x that margin.
    REQUIRE(p0 > 0.5f);                  // sanity: this really did clamp hard
    CHECK(p25  == doctest::Approx(p0).epsilon(0.02));
    CHECK(p50  == doctest::Approx(p0).epsilon(0.02));
    CHECK(p75  == doctest::Approx(p0).epsilon(0.02));
    CHECK(p100 == doctest::Approx(p0).epsilon(0.02));
}

TEST_CASE("bbd echo: DRIVE now raises the small-signal loop gain, monotonically") {
    // The exact thing the retired test forbade. Reuses that test's own probe
    // (a quiet 0.01-amplitude impulse, feedback 0.7, the same late window) --
    // only the assertion direction changed. Small-signal loop gain is now
    // `g` itself: unity (kDriveLoDb=0 dB) at DRIVE 0 up to +12 dB
    // (kDriveHiDb) at DRIVE 1 -- comfortably below this test's own feedback
    // (0.7) and DRIVE 0's self-oscillation threshold (~0.84, see the
    // constraint test below), so the tail still climbs measurably from quiet
    // to loud as DRIVE rises, without DRIVE 0 itself blooming here. FEEDBACK
    // moving closer to self-oscillation as DRIVE increases IS the authentic,
    // accepted consequence of this change, not a bug. A future silent
    // reversion to the old inverse-gain law would flatten this back to
    // "b/a ~= 1" and this assertion would catch it.
    auto tail_at = [](float drive) {
        BbdEcho e;
        static float mem[8192];
        e.Init(48000.f, mem, 8192);
        e.SetStages(8192);
        e.SetDrive(drive);
        e.SetFeedback(0.7f);
        const float hz = bbd_clock_hz(0.25f, 8192);
        float p = 0.f;
        for (int i = 0; i < 100000; ++i) {
            const float y = e.Process((i < 32) ? 0.01f : 0.f, hz);
            if (i > 60000 && i < 64000) p = std::max(p, std::fabs(y));
        }
        return p;
    };
    const float a = tail_at(0.f);
    const float b = tail_at(0.5f);
    const float c = tail_at(1.f);
    // Measured at the shipped 0..+12 range: a=0.00247503, b=0.0763823,
    // c=1.4489. DRIVE 0's threshold (~0.84) sits above this feedback (0.7),
    // so `a` is still a genuine decaying tail; DRIVE 0.5's and DRIVE 1's
    // thresholds (~0.42, ~0.21) sit below it, so `b` and `c` are actually
    // partway into a growing/blooming loop by this window rather than a
    // simple quiet decay -- which is exactly "FEEDBACK moves closer to
    // self-oscillation as DRIVE rises" showing up directly, not a test
    // artifact.
    INFO("a=" << a << " b=" << b << " c=" << c);
    REQUIRE(a > 1e-6f);
    CHECK(b > a);
    CHECK(c > b);
}

TEST_CASE("bbd echo: max DRIVE and max FEEDBACK together still cannot run away") {
    // The new law raises loop gain with DRIVE, and FEEDBACK already reaches
    // 1.2 (documented behaviour of the original -- see "feedback at max
    // blooms but stays bounded"). Together these two knobs at their limits
    // are the actual worst case the fixed ceiling has to survive: fast_tanh
    // clamps hard at +-1 regardless of sat_in_/sat_out_, so Process() is
    // bounded by construction at sat_out_ = kSatCeil no matter how high DRIVE
    // pushes the small-signal gain. Bounded, finite, and still sustaining --
    // not silent, which would mean the compander or expander had collapsed
    // the bloom instead of the saturator doing its job.
    BbdEcho e;
    static float mem[8192];
    e.Init(48000.f, mem, 8192);
    e.SetStages(8192);
    e.SetDrive(1.f);
    e.SetFeedback(1.2f);
    const float hz = bbd_clock_hz(0.25f, 8192);
    float peak = 0.f;
    double late_sq = 0.0;
    int late_n = 0;
    for (int i = 0; i < 480000; ++i) {               // 10 s
        const float y = e.Process((i < 32) ? 1.f : 0.f, hz);
        REQUIRE(std::isfinite(y));
        peak = std::max(peak, std::fabs(y));
        if (i >= 432000) { late_sq += (double)y * y; ++late_n; }
    }
    const float late_rms = static_cast<float>(std::sqrt(late_sq / late_n));
    // Measured at the shipped 0..+12 range: peak=1.54485, late_rms=0.24392.
    INFO("peak=" << peak << " late_rms=" << late_rms);
    CHECK(peak > 0.2f);                              // it did bloom
    CHECK(peak < 12.f);                               // and it stayed bounded
    CHECK(late_rms > 0.01f);                          // and it sustained
}

TEST_CASE("bbd echo: self-oscillation is reachable at DRIVE 0, within FEEDBACK's 1.2 ceiling") {
    // The constraint that broke unnoticed when the fixed ceiling first
    // shipped at kDriveLoDb=-6/kDriveHiDb=24: the design's own rule is that
    // self-oscillation must stay reachable ("FEEDBACK keeps its 1.2 over
    // unity so self-oscillation stays reachable -- documented behaviour of
    // the original", see "feedback at max blooms but stays bounded"). At
    // -6..+24, DRIVE 0's small-signal loop gain was g(-6dB)=0.501, which
    // pushed the self-oscillation FEEDBACK threshold to ~1.71 -- past
    // Flux::set_feedback's own 1.2 maximum, so self-oscillation became
    // UNREACHABLE at DRIVE 0. Nothing in this file guarded that constraint,
    // which is why nobody noticed it breaking. At the shipped 0..+12 range,
    // DRIVE 0 is unity gain (g=1), and FEEDBACK 1.2 must bloom.
    BbdEcho e;
    static float mem[8192];
    e.Init(48000.f, mem, 8192);
    e.SetStages(8192);
    e.SetDrive(0.f);
    e.SetFeedback(1.2f);
    const float hz = bbd_clock_hz(0.25f, 8192);
    float early = 0.f, late = 0.f;
    for (int i = 0; i < 480000; ++i) {               // 10 s
        const float y = e.Process((i < 32) ? 1.f : 0.f, hz);
        REQUIRE(std::isfinite(y));
        if (i > 11000 && i < 13000) early = std::max(early, std::fabs(y));
        if (i > 400000) late = std::max(late, std::fabs(y));
    }
    // Measured: early=0.885514, late=1.4634 -- late is BIGGER than early
    // (a decaying echo would have late << early, as in "feedback below unity
    // decays to silence"), i.e. genuinely blooming, not just failing to
    // decay all the way.
    INFO("early=" << early << " late=" << late);
    CHECK(early > 1e-3f);
    CHECK(late > early * 0.05f);                     // the "sustains" criterion this file already uses
    CHECK(late > early);                             // stronger: it actually GREW, i.e. it oscillates
}

// --- Task 2: the part-engine hooks, neutral by default ----------------------

TEST_CASE("bbd: the new hooks are all neutral at their defaults") {
    static constexpr size_t kBbdCells = bbd_tuning::kMaxStages / 2;
    static float bufA[kBbdCells];
    static float bufB[kBbdCells];
    BbdEcho a, b;
    a.Init(48000.f, bufA, kBbdCells);
    b.Init(48000.f, bufB, kBbdCells);
    // b touches every new setter with its documented neutral value.
    b.SetLossCoef(bbd_tuning::kLossCoef);
    b.SetDither(0.f);
    b.SetFeedbackTilt(0.f, 4000.f);
    b.SetFeedbackDcBlock(false);
    a.SetFeedback(0.6f);  b.SetFeedback(0.6f);
    a.SetDrive(0.3f);     b.SetDrive(0.3f);
    a.SetStages(8192);    b.SetStages(8192);
    for (int i = 0; i < 48000; ++i) {
        const float x = std::sin(i * 0.01f) * (i < 4800 ? 1.f : 0.f);
        // Bit-identical, not approximately: a neutral default that only nearly
        // reproduces the old path is a behaviour change nobody stated.
        CHECK(a.Process(x, 6000.f) == b.Process(x, 6000.f));
    }
}

TEST_CASE("bbd: Reset clears the line, the compander and the feedback state") {
    static constexpr size_t kBbdCells = bbd_tuning::kMaxStages / 2;
    static float buf[kBbdCells];
    BbdEcho e;
    e.Init(48000.f, buf, kBbdCells);
    e.SetFeedback(0.8f);
    e.SetStages(2048);
    for (int i = 0; i < 24000; ++i) e.Process(std::sin(i * 0.05f), 6000.f);
    CHECK(std::fabs(e.FeedbackState()) > 1e-4f);   // charge is in flight
    e.Reset();
    CHECK(e.FeedbackState() == 0.f);
    // Nothing may come back out of a reset line for a full delay period.
    float peak = 0.f;
    for (int i = 0; i < 24000; ++i)
        peak = std::max(peak, std::fabs(e.Process(0.f, 6000.f)));
    CHECK(peak < 1e-6f);
}

TEST_CASE("bbd: dither makes a silent line audible and stays inaudible itself") {
    static constexpr size_t kBbdCells = bbd_tuning::kMaxStages / 2;
    static float buf[kBbdCells];
    BbdEcho e;
    e.Init(48000.f, buf, kBbdCells);
    e.SeedDither(0x1234u);
    e.SetDither(4e-5f);
    e.SetStages(4096);
    e.SetFeedback(0.f);
    float rms = 0.f;
    for (int i = 0; i < 48000; ++i) {
        const float y = e.Process(0.f, 6000.f);
        rms += y * y;
    }
    rms = std::sqrt(rms / 48000.f);
    CHECK(rms > 0.f);          // it is not silence
    CHECK(rms < 1e-3f);        // and it is below -60 dBFS
}

TEST_CASE("bbd: the loss coefficient moves the darkness") {
    static constexpr size_t kBbdCells = bbd_tuning::kMaxStages / 2;
    static float bufD[kBbdCells];
    static float bufB[kBbdCells];
    auto hi_energy = [](BbdEcho& e) {
        float s = 0.f;
        for (int i = 0; i < 48000; ++i) {
            const float x = std::sin(i * 0.5f);      // ~3.8 kHz at 48 k
            const float y = e.Process(x, 8000.f);
            if (i > 24000) s += y * y;
        }
        return s;
    };
    BbdEcho dark, bright;
    dark.Init(48000.f, bufD, kBbdCells);
    bright.Init(48000.f, bufB, kBbdCells);
    dark.SetStages(4096);   bright.SetStages(4096);
    dark.SetLossCoef(0.2f);
    bright.SetLossCoef(0.95f);
    CHECK(hi_energy(dark) < hi_energy(bright));
}

TEST_CASE("bbd: the feedback tilt brightens or darkens the repeats") {
    static constexpr size_t kBbdCells = bbd_tuning::kMaxStages / 2;
    static float bufN[kBbdCells];
    static float bufU[kBbdCells];
    auto tail_hi = [](BbdEcho& e) {
        float s = 0.f;
        for (int i = 0; i < 96000; ++i) {
            const float x = (i < 2400) ? std::sin(i * 0.4f) : 0.f;
            const float y = e.Process(x, 8000.f);
            if (i > 48000) s += y * y;   // long after the input stopped
        }
        return s;
    };
    BbdEcho flat, up;
    flat.Init(48000.f, bufN, kBbdCells);
    up.Init(48000.f, bufU, kBbdCells);
    flat.SetStages(4096);  up.SetStages(4096);
    flat.SetFeedback(0.7f); up.SetFeedback(0.7f);
    up.SetFeedbackTilt(0.8f, 2000.f);
    CHECK(tail_hi(up) > tail_hi(flat));
}

TEST_CASE("bbd: the feedback DC blocker stops a frozen loop drifting") {
    static constexpr size_t kBbdCells = bbd_tuning::kMaxStages / 2;
    static float buf[kBbdCells];
    BbdEcho e;
    e.Init(48000.f, buf, kBbdCells);
    e.SetStages(4096);
    e.SetFeedbackDcBlock(true);
    e.SetFeedback(0.999f);
    for (int i = 0; i < 4800; ++i) e.Process(0.5f, 8000.f);   // pump DC in
    // FEEDBACK 0.999 at DRIVE 0 is above this line's self-oscillation
    // threshold (measured ~0.84 elsewhere in this file), so the loop is a
    // sustained tone, not a decaying one -- exactly the "frozen loop" this
    // hook targets. The tone's ~256 ms round trip does not divide evenly into
    // a short window, so the very first second after the pump still carries
    // real windowing bias from the pump transient locking in; a 2 s settle
    // and a 30 s measurement window (both longer than the plan's 0 s / 10 s)
    // are what it takes to show the mean actually converging toward zero
    // rather than reading a leftover transient. Confirmed by hand: at 1 s/10 s
    // the mean is 0.0010245 (a false RED); by 2 s/30 s it is 0.0006805 and
    // still falling as the window grows, i.e. real convergence, not noise.
    for (int i = 0; i < 96000; ++i) e.Process(0.f, 8000.f);   // settle
    float mean = 0.f;
    const int n = 48000 * 30;
    for (int i = 0; i < n; ++i) mean += e.Process(0.f, 8000.f);
    mean /= n;
    CHECK(std::fabs(mean) < 1e-3f);
}
