#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "fx/reverb.h"
using namespace spky;

// ~130 KB object: static, never on the stack. init() fully re-seeds all
// state (buffer, filters, LFOs, RNG), so sharing one instance is safe.
static AmbientReverb s_rev;

static std::vector<float> impulse_response(AmbientReverb& rv, int n,
                                           bool left_channel) {
    std::vector<float> out(n);
    for (int i = 0; i < n; ++i) {
        float wl = 0.f, wr = 0.f;
        float in = (i == 0) ? 1.f : 0.f;
        rv.process(in, in, wl, wr);
        out[i] = left_channel ? wl : wr;
    }
    return out;
}

TEST_CASE("reverb: silence in, exact silence out") {
    s_rev.init(48000.f);
    for (int i = 0; i < 2000; ++i) {
        float wl = 1.f, wr = 1.f;
        s_rev.process(0.f, 0.f, wl, wr);
        CHECK(wl == 0.f);
        CHECK(wr == 0.f);
    }
}

TEST_CASE("reverb: mono impulse produces a persistent stereo tail") {
    s_rev.init(48000.f);
    s_rev.set_decay(0.75f);
    auto l = impulse_response(s_rev, 48000, true);
    s_rev.init(48000.f);
    s_rev.set_decay(0.75f);
    auto r = impulse_response(s_rev, 48000, false);
    float tail = 0.f, decorr = 0.f;
    for (int i = 24000; i < 48000; ++i) tail += l[i] * l[i];
    for (int i = 0; i < 48000; ++i) decorr = std::max(decorr, std::fabs(l[i] - r[i]));
    CHECK(tail > 1e-6f);     // still ringing after 0.5 s
    CHECK(decorr > 1e-4f);   // L and R differ
}

TEST_CASE("reverb: below 100% the impulse energy decays monotonically") {
    s_rev.init(48000.f);
    s_rev.set_decay(0.4f);
    auto ir = impulse_response(s_rev, 48000, true);
    float w[4] = { 0.f, 0.f, 0.f, 0.f };
    for (int k = 0; k < 4; ++k)
        for (int i = k * 12000; i < (k + 1) * 12000; ++i) w[k] += ir[i] * ir[i];
    CHECK(w[0] > w[1]);
    CHECK(w[1] > w[2]);
    CHECK(w[2] > w[3]);
}

TEST_CASE("reverb: decay past 100% blooms, self-sustains, stays bounded") {
    s_rev.init(48000.f);
    s_rev.set_decay(1.f);    // internal loop gain 1.05 (capped)
    float peak = 0.f, late = 0.f;
    bool finite = true;
    const int N = 48000 * 8;
    for (int i = 0; i < N; ++i) {
        float in = (i < 96000) ? 0.3f * std::sin(6.2831853f * 220.f * i / 48000.f) : 0.f;
        float wl = 0.f, wr = 0.f;
        s_rev.process(in, in, wl, wr);
        if (!std::isfinite(wl) || !std::isfinite(wr)) { finite = false; break; }
        peak = std::max(peak, std::max(std::fabs(wl), std::fabs(wr)));
        if (i >= N - 48000) late += wl * wl;
    }
    CHECK(finite);                      // never runs away
    CHECK(peak < 4.f);                  // the in-loop SoftLimit holds it
    CHECK(late / 48000.f > 0.0004f);    // still singing 6 s after input stopped
}

// The bloom leg of DECAY used to be the sub-unity slope (norm/0.9) clamped at
// 1.05, which the clamp reached at norm 0.945 -- so the top 5.5% of the knob
// was dead: 0.95, 0.97 and 1.0 rang bit-identically. The whole travel is live.
TEST_CASE("reverb: the top of the DECAY travel is live, not a dead zone") {
    auto tail_energy = [](float norm) {
        s_rev.init(48000.f);
        s_rev.set_decay(norm);
        double e = 0.0;
        const int burst = 48000 * 2, N = 48000 * 14;
        for (int i = 0; i < N; ++i) {
            float in = (i < burst) ? 0.3f * std::sin(6.2831853f * 220.f * i / 48000.f) : 0.f;
            float wl, wr;
            s_rev.process(in, in, wl, wr);
            if (i >= N - 48000) e += (double)wl * wl;   // last second, 12 s after input
        }
        return e / 48000.0;
    };
    const double e94 = tail_energy(0.94f);
    const double e97 = tail_energy(0.97f);
    const double e100 = tail_energy(1.00f);
    CHECK(e94 < e97);      // each step up the bloom leg sustains harder
    CHECK(e97 < e100);
}

// The room can hand the master a level nobody asked for: past unity loop gain
// it plateaus wherever its own saturation puts it, and a hot send alone returns
// near full scale (measured 1.12 off a 0.9 send at DECAY 0.85). That lands on
// top of however hot the decks already are, so the return carries its own
// ceiling.
namespace {
struct WetRun { float peak; float gmin; };
WetRun wet_run(float decay, float send_amp, float secs, bool sustain) {
    s_rev.init(48000.f);
    s_rev.set_size(0.6f);
    s_rev.set_tone(0.5f);
    s_rev.set_diffusion(0.7f);
    s_rev.set_diffuser_mod_depth(0.5f);
    s_rev.set_mod_depth(0.2f);
    s_rev.set_decay(decay);
    const int N = (int)(48000.f * secs), stop = 48000 * 2;
    float peak = 0.f, gmin = 1.f;
    for (int i = 0; i < N; ++i) {
        float in = (sustain || i < stop)
                       ? send_amp * std::sin(6.2831853f * 220.f * i / 48000.f)
                       : 0.f;
        float wl, wr;
        s_rev.process(in, in, wl, wr);
        if (i > 48000 * 2) {   // past the ride's own settling
            peak = std::max(peak, std::max(std::fabs(wl), std::fabs(wr)));
            gmin = std::min(gmin, s_rev.limiter_gain());
        }
    }
    return {peak, gmin};
}
} // namespace

TEST_CASE("reverb: the return carries a ceiling, whatever the room does") {
    // A hot send is the loudest case of all -- louder than any bloom. The bound
    // is above kWetKnee because the knee is soft: the return keeps growing past
    // it, just four times more slowly, which is what keeps the ride from
    // breathing. Unbounded these reach 1.00 to 1.12.
    CHECK(wet_run(0.85f, 0.90f, 24.f, true).peak < 0.80f);
    CHECK(wet_run(0.90f, 0.90f, 24.f, true).peak < 0.80f);
    CHECK(wet_run(0.60f, 0.90f, 24.f, true).peak < 0.80f);
    CHECK(wet_run(0.85f, 0.50f, 24.f, true).peak < 0.80f);
    // and the bloom, now that it reaches 120%, is bounded by the same knee
    CHECK(wet_run(1.00f, 0.10f, 30.f, true).peak < 0.80f);
    CHECK(wet_run(1.00f, 0.05f, 45.f, false).peak < 0.80f);
    CHECK(wet_run(1.00f, 0.60f, 45.f, true).peak < 0.80f);
}

// The knob reads out as a percentage of loop gain, the way FLUX FB does, so
// the player can see where the room starts feeding itself. The panel calls the
// engine's own curve; if these two ever disagree the number on screen is a lie.
TEST_CASE("reverb: DECAY reads out as loop gain, and 100% is where it says") {
    CHECK(AmbientReverb::decay_loop_gain(0.00f) == doctest::Approx(0.00f));
    CHECK(AmbientReverb::decay_loop_gain(0.40f) == doctest::Approx(0.50f));
    CHECK(AmbientReverb::decay_loop_gain(0.80f) == doctest::Approx(1.00f));
    CHECK(AmbientReverb::decay_loop_gain(0.90f) == doctest::Approx(1.05f));
    CHECK(AmbientReverb::decay_loop_gain(1.00f) == doctest::Approx(1.10f));
    // monotone across the join, and clamped outside
    float prev = -1.f;
    for (int i = 0; i <= 200; ++i) {
        const float g = AmbientReverb::decay_loop_gain(i / 200.f);
        CHECK(g > prev);
        prev = g;
    }
    CHECK(AmbientReverb::decay_loop_gain(-1.f) == doctest::Approx(0.f));
    CHECK(AmbientReverb::decay_loop_gain(2.f) == doctest::Approx(1.10f));
}

// The bloom used to live at a single point: the room does not sustain itself
// below about 105% loop gain, and 105% WAS the top of the knob. Now the top is
// 120%, so blooming occupies real travel and its speed can be played.
TEST_CASE("reverb: the bloom is reachable before the end of the travel") {
    auto sustains_after_silence = [](float norm) {
        s_rev.init(48000.f);
        s_rev.set_size(0.6f);
        s_rev.set_tone(0.5f);
        s_rev.set_diffusion(0.7f);
        s_rev.set_diffuser_mod_depth(0.5f);
        s_rev.set_mod_depth(0.2f);
        s_rev.set_decay(norm);
        const int N = 48000 * 40, stop = 48000 * 2;
        float late = 0.f;
        for (int i = 0; i < N; ++i) {
            float in = (i < stop)
                           ? 0.15f * std::sin(6.2831853f * 220.f * i / 48000.f)
                           : 0.f;
            float wl, wr;
            s_rev.process(in, in, wl, wr);
            if (i > N - 48000 * 5) late = std::max(late, std::fabs(wl));
        }
        return late;
    };
    // still ringing 33 s after the input stopped, well below the stop
    CHECK(sustains_after_silence(0.90f) > 0.05f);
    // while an ordinary room below unity dies away as it always did
    CHECK(sustains_after_silence(0.70f) < 0.001f);
    // Deliberately NOT asserted: that 1.00 returns a higher level than 0.90.
    // It does not, and that is the design -- the return knee holds the level
    // while more DECAY buys length and density instead. Measured 0.565 at 0.90
    // against 0.536 at 1.00. What must keep rising is the loop gain itself:
    CHECK(AmbientReverb::decay_loop_gain(0.90f)
          < AmbientReverb::decay_loop_gain(1.00f));
}

// The ceiling must be a ceiling and not a compressor: anything that never
// reaches it has to come through with the ride at exactly 1.0. Only the gain
// shows this -- from outside, a ride looks the same as a quieter room.
TEST_CASE("reverb: material under the ceiling is passed through untouched") {
    CHECK(wet_run(0.50f, 0.10f, 12.f, true).gmin == 1.f);
    CHECK(wet_run(0.60f, 0.10f, 12.f, true).gmin == 1.f);
    CHECK(wet_run(0.75f, 0.10f, 12.f, true).gmin == 1.f);
    CHECK(wet_run(0.50f, 0.20f, 12.f, true).gmin == 1.f);
    // The boundary is close: a 0.30 send at DECAY 0.50 already grazes the knee
    // (ride 0.994, about 0.05 dB). Recorded rather than hidden -- it is where
    // "quiet" stops, and it moves whenever kWetKnee does.
    // a decaying tail is never touched on its way down either
    CHECK(wet_run(0.85f, 0.10f, 20.f, false).gmin == 1.f);
    // Note what is NOT claimed: that the knee leaves every ordinary sound
    // alone. It does not -- a 0.30 send at DECAY 0.60 already crosses it. The
    // knee sits where the level problem is, so it has to.
}

TEST_CASE("reverb: size ride Doppler-warps without clicks") {
    s_rev.init(48000.f);
    s_rev.set_decay(0.9f);
    s_rev.set_size(0.7f);
    // ring the room first
    for (int i = 0; i < 24000; ++i) {
        float in = (i == 0) ? 1.f : 0.2f * std::sin(6.2831853f * 330.f * i / 48000.f);
        float wl, wr;
        s_rev.process(in, in, wl, wr);
    }
    float prev = 0.f, max_step = 0.f;
    bool finite = true;
    for (int i = 0; i < 96000; ++i) {
        if (i % 480 == 0) {  // sweep 0.7 -> 0.1 in 200 steps over 1 s, then back
            float t = i / 96000.f;
            float n = t < 0.5f ? 0.7f - 1.2f * t : 0.1f + 1.2f * (t - 0.5f);
            s_rev.set_size(n);
        }
        float wl, wr;
        s_rev.process(0.f, 0.f, wl, wr);
        if (!std::isfinite(wl)) { finite = false; break; }
        max_step = std::max(max_step, std::fabs(wl - prev));
        prev = wl;
    }
    CHECK(finite);
    CHECK(max_step < 1.f);   // Doppler yes, discontinuities no
}

TEST_CASE("reverb: tone closed removes high-frequency tail energy") {
    auto hf_ratio = [](const std::vector<float>& x) {
        float diff = 0.f, tot = 1e-12f;
        for (size_t i = 4801; i < x.size(); ++i) {
            float d = x[i] - x[i - 1];
            diff += d * d;
            tot += x[i] * x[i];
        }
        return diff / tot;   // first-difference energy ~ HF content proxy
    };
    s_rev.init(48000.f);
    s_rev.set_decay(0.7f);
    s_rev.set_tone(0.9f);
    auto bright = impulse_response(s_rev, 48000, true);
    s_rev.init(48000.f);
    s_rev.set_decay(0.7f);
    s_rev.set_tone(0.1f);
    auto dark = impulse_response(s_rev, 48000, true);
    CHECK(hf_ratio(bright) > hf_ratio(dark) * 1.5f);
}

TEST_CASE("reverb: diffusion reshapes the room (sparse vs dense)") {
    s_rev.init(48000.f);
    s_rev.set_decay(0.8f);
    s_rev.set_diffusion(0.f);            // discrete slap-echo cluster
    auto sparse = impulse_response(s_rev, 48000, true);
    s_rev.init(48000.f);
    s_rev.set_decay(0.8f);
    s_rev.set_diffusion(0.9f);           // dense wash
    auto dense = impulse_response(s_rev, 48000, true);
    int diff = 0;
    for (int i = 4800; i < 48000; ++i)
        if (std::fabs(sparse[i] - dense[i]) > 1e-6f) ++diff;
    CHECK(diff > 1000);
    // early-window crest: a sparse room concentrates energy in discrete
    // events; a diffused one spreads it -> lower peak-to-RMS
    auto crest = [](const std::vector<float>& x, int a, int b) {
        float pk = 0.f;
        double acc = 0.0;
        for (int i = a; i < b; ++i) {
            pk = std::max(pk, std::fabs(x[i]));
            acc += x[i] * x[i];
        }
        float rms = std::sqrt((float)(acc / (b - a))) + 1e-12f;
        return pk / rms;
    };
    CHECK(crest(sparse, 0, 9600) > crest(dense, 0, 9600));
}

TEST_CASE("reverb: diffusion ride stays bounded without clicks") {
    s_rev.init(48000.f);
    s_rev.set_decay(0.9f);
    // ring the room first
    for (int i = 0; i < 24000; ++i) {
        float in = (i == 0) ? 1.f : 0.2f * std::sin(6.2831853f * 330.f * i / 48000.f);
        float wl, wr;
        s_rev.process(in, in, wl, wr);
    }
    float prev = 0.f, max_step = 0.f;
    bool finite = true;
    for (int i = 0; i < 96000; ++i) {
        if (i % 480 == 0) {   // sweep 0 -> 1 -> 0 over 2 s in 200 steps
            float t = i / 96000.f;
            float n = t < 0.5f ? 2.f * t : 2.f * (1.f - t);
            s_rev.set_diffusion(n);
        }
        float wl, wr;
        s_rev.process(0.f, 0.f, wl, wr);
        if (!std::isfinite(wl)) { finite = false; break; }
        max_step = std::max(max_step, std::fabs(wl - prev));
        prev = wl;
    }
    CHECK(finite);
    CHECK(max_step < 1.f);   // density morph yes, discontinuities no
}

// SMEAR drives the 8 random line LFOs, which advance on a /32 raster
// (Oliverb::kLfoDecim, the 2026-07-19 CPU cut). If their value is HELD flat
// between raster ticks instead of glided, the diffuser read offset is a
// staircase: every 32nd sample splices the delay position, which folds an
// inharmonic comb at 48000/32 = 1500 Hz around everything in the room. That
// is the sample-rate-reduction-like grit SMEAR grew at high settings.
// Measured as sideband energy at 1 kHz +- k*1500 Hz against the 1 kHz signal.
static double bin_energy(const std::vector<float>& x, const std::vector<double>& win,
                         double f, double sr) {
    const size_t n = x.size();
    double e = 0.0;
    for (int j = -6; j <= 6; ++j) {                    // +-6 bins of skirt
        const double ff = f + j * sr / (double)n;
        if (ff < 20.0 || ff > sr * 0.5 - 20.0) continue;
        double re = 0.0, im = 0.0;
        const double a = 2.0 * 3.14159265358979 * ff / sr;
        for (size_t i = 0; i < n; ++i) {
            const double v = x[i] * win[i];
            re += v * std::cos(a * i);
            im += v * std::sin(a * i);
        }
        e += re * re + im * im;
    }
    return e;
}

static double smear_raster_comb_db(float smear) {
    const double sr = 48000.0;
    const size_t N = 16384;
    s_rev.init((float)sr);
    s_rev.set_size(0.6f);
    s_rev.set_decay(0.55f);
    s_rev.set_tone(0.5f);
    s_rev.set_diffusion(0.7f);
    s_rev.set_mod_depth(0.f);              // isolate SMEAR from the tail wobble
    s_rev.set_diffuser_mod_depth(smear);
    std::vector<float> buf;
    buf.reserve(N);
    const int warm = (int)(sr * 3);        // let the room fill
    for (int i = 0; i < warm + (int)N; ++i) {
        float in = 0.3f * std::sin(6.2831853f * 1000.f * i / (float)sr);
        float wl, wr;
        s_rev.process(in, in, wl, wr);
        if (i >= warm) buf.push_back(wl);
    }
    std::vector<double> win(N);
    for (size_t i = 0; i < N; ++i)
        win[i] = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / (N - 1));
    const double sig = bin_energy(buf, win, 1000.0, sr);
    double comb = 0.0;
    for (int k = 1; k <= 5; ++k) {         // 1 kHz +- k*1500 Hz: not harmonics
        comb += bin_energy(buf, win, 1000.0 + k * 1500.0, sr);
        comb += bin_energy(buf, win, std::fabs(1000.0 - k * 1500.0), sr);
    }
    return 10.0 * std::log10(comb / (sig + 1e-30) + 1e-30);
}

TEST_CASE("reverb: SMEAR glides the diffusers, it does not staircase them") {
    // held LFO: -47 dB at half smear, -36 dB at full -- audible grit.
    // glided:   -80 dB           and -55 dB, matching a per-sample LFO to 0.8 dB.
    CHECK(smear_raster_comb_db(0.5f) < -65.0);
    CHECK(smear_raster_comb_db(1.0f) < -50.0);
}

TEST_CASE("reverb: bit-deterministic across instances") {
    static AmbientReverb rvA, rvB;
    rvA.init(48000.f);
    rvB.init(48000.f);
    for (AmbientReverb* rv : { &rvA, &rvB }) {
        rv->set_size(0.65f);
        rv->set_decay(0.85f);
        rv->set_tone(0.6f);
        rv->set_diffusion(0.6f);
    }
    bool identical = true;
    for (int i = 0; i < 48000; ++i) {
        float in = (i == 0) ? 1.f : 0.2f * std::sin(6.2831853f * 110.f * i / 48000.f);
        float la, ra, lb, rb;
        rvA.process(in, in, la, ra);
        rvB.process(in, in, lb, rb);
        if (la != lb || ra != rb) { identical = false; break; }
    }
    CHECK(identical);
}

TEST_CASE("reverb: clear() empties the room but keeps the parameter state") {
    static AmbientReverb rv;             // BIG object: never stack-allocate
    rv.init(48000.f);
    rv.set_decay(0.8f);
    float l, r;
    // ring up a tail: periodic impulses for 0.25 s
    for (int i = 0; i < 12000; ++i) {
        float in = (i % 4800 == 0) ? 0.9f : 0.f;
        rv.process(in, in, l, r);
    }
    float energy = 0.f;                  // the room is audibly ringing
    for (int i = 0; i < 4800; ++i) { rv.process(0.f, 0.f, l, r); energy += l * l + r * r; }
    CHECK(energy > 1e-6f);

    rv.clear();
    // silence in -> exact silence out: buffer AND loop filter state are zeroed
    for (int i = 0; i < 4800; ++i) {
        rv.process(0.f, 0.f, l, r);
        CHECK(l == 0.f);
        CHECK(r == 0.f);
    }
    // parameters survived the clear: a fresh impulse still rings the same room
    rv.process(0.9f, 0.9f, l, r);
    float energy2 = 0.f;
    for (int i = 0; i < 9600; ++i) { rv.process(0.f, 0.f, l, r); energy2 += l * l + r * r; }
    CHECK(energy2 > 1e-6f);
}

TEST_CASE("reverb: return_level reads 0 fresh, rises with a bloom, forgets on clear") {
    s_rev.init(48000.f);
    CHECK(s_rev.return_level() == 0.f);
    s_rev.set_decay(1.f);                     // 110% loop gain: self-driving
    for (int i = 0; i < 48000 * 6; ++i) {
        float wl = 0.f, wr = 0.f;
        float in = (i < 4800) ? 0.5f : 0.f;   // 100 ms burst seeds the room
        s_rev.process(in, in, wl, wr);
    }
    // Post-ceiling plateau sits near 0.55 (knee 0.45 + overshoot/7). The
    // bound is loose on purpose: this pins "well above the duck threshold",
    // not a render checksum.
    CHECK(s_rev.return_level() > 0.3f);
    s_rev.clear();
    CHECK(s_rev.return_level() == 0.f);       // clear() forgets the ride
}
