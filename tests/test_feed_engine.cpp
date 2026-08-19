// FEED -- the coupled feedback-FM drone engine.
// Spec: docs/superpowers/specs/2026-08-18-feed-coupled-feedback-fm-design.md
//
// P is feed_cfg::kPairs and is a MEASURED number (spec section 8). Nothing in
// this file may assume its value: every loop runs to kPairs and every
// expectation is derived from the named constants in feed_config.h, never from
// their literals.
#include <doctest/doctest.h>
#include "parts/part.h"
#include "feed/feed_engine.h"
#include "part_engine_contract.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

using namespace spky;

namespace {

// A FeedEngine at a known state. set_seed() BEFORE init(), the SynthEngineT
// convention (synth_engine.h) -- init() consumes the seed to draw the SPREAD
// signature and the per-pair feedback offsets, so the reverse order measures a
// different object.
FeedEngine fresh_feed(uint32_t seed = 99u) {
    FeedEngine e;
    e.set_seed(seed);
    e.init(48000.f);
    e.set_cycle(1.f);
    return e;
}

// The five lane targets, in the order Part pushes them: SOURCE, SIZE, PITCH,
// MOTION, LEVEL (engine/mod/lane_id.h). Named for what FEED reads them as.
void feed_lanes(FeedEngine& e, float pitch, float bond = 0.f,
                float spread = 0.f, float depth = 0.5f, float level = 1.f) {
    const float t[LANE_COUNT] = { bond, spread, pitch, depth, level };
    e.set_targets(t, 0.5f);
}

std::vector<float> render_l(FeedEngine& e, int n) {
    std::vector<float> out(n);
    for (auto& s : out) { float l = 0.f, r = 0.f; e.process(l, r); s = l; }
    return out;
}

float peak_of(const std::vector<float>& b) {
    float p = 0.f;
    for (float v : b) p = std::max(p, std::fabs(v));
    return p;
}

// Run the engine long enough for every slope to land: kCtrlInterval samples is
// one control tick, and the glide closes over several of them.
void settle(FeedEngine& e, int ticks = 200) {
    float l = 0.f, r = 0.f;
    for (int i = 0; i < ticks * FeedEngine::kCtrlInterval; ++i) e.process(l, r);
}

// The instrument's pitch law, 110 * 8^p (synth_engine.cpp's file-static
// pitch_to_hz). Duplicated here on purpose: a gate that recomputed the
// expected frequency from the engine's own copy of the law would pass however
// wrong that copy was.
float pitch_to_hz_ref(float p) { return 110.f * std::pow(8.f, p); }

// Hann-windowed magnitude spectrum. The radix-2 transform
// tests/test_wt_osc.cpp's dft_energy uses, returning magnitude rather than
// energy -- copied rather than shared because that one is a static in its own
// file and lifting it into a header is a refactor this plan does not own.
std::vector<double> mag_spectrum(const std::vector<float>& x) {
    const int n = static_cast<int>(x.size());
    std::vector<std::complex<double>> bins(n);
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(6.28318530717958647692 * i / (n - 1));
        bins[i] = x[i] * w;
    }
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(bins[i], bins[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double angle = -6.28318530717958647692 / len;
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (int s = 0; s < n; s += len) {
            std::complex<double> tw(1.0, 0.0);
            for (int o = 0; o < len / 2; ++o) {
                const std::complex<double> e = bins[s + o];
                const std::complex<double> d = bins[s + o + len / 2] * tw;
                bins[s + o] = e + d;
                bins[s + o + len / 2] = e - d;
                tw *= step;
            }
        }
    }
    std::vector<double> m(n / 2 + 1);
    for (int b = 0; b <= n / 2; ++b) m[b] = std::abs(bins[b]);
    return m;
}

// Normalized magnitude flux: how far two spectra moved, as a fraction of the
// energy they hold between them.
double spectral_flux(const std::vector<double>& a, const std::vector<double>& b) {
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        num += std::fabs(a[i] - b[i]);
        den += a[i] + b[i];
    }
    return den > 0.0 ? num / den : 0.0;
}

// Spectral flatness: geometric mean over arithmetic mean of the magnitude
// spectrum. 0 = pure lines, 1 = white noise. The measure G10 is about, and it
// is a property of ONE window rather than of the difference between two --
// which is precisely why it is used there. See G10 for the reasoning.
double spectral_flatness(const std::vector<double>& m) {
    double log_sum = 0.0, sum = 0.0;
    int n = 0;
    for (size_t i = 1; i < m.size(); ++i) {
        const double v = m[i] + 1e-20;
        log_sum += std::log(v);
        sum += v;
        ++n;
    }
    return n > 0 ? std::exp(log_sum / n) / (sum / n) : 1.0;
}

// Estimated fundamental by YIN (de Cheveigne & Kawahara 2002), returning an
// aperiodicity alongside the frequency.
//
// docs/engine-map.md section 9 records why this and not autocorrelation: on
// THIS engine, argmax of the raw autocorrelation locks to a sub-multiple
// (measured -1200.97 ct and -1905.27 ct on BOND 0 rows) and a
// shortest-lag-above-0.9-max rule locks to a harmonic instead (+1904.44 ct).
// Both return a confident number for a pitch they have wrong by an octave,
// which is the only kind of measurement error that survives into a gate.
//
// The aperiodicity is not diagnostics. A coupled network driven past its
// stability point genuinely has no fundamental, and a gate that asserted a
// cent tolerance on one would be asserting something about noise.
struct F0 { float hz; float aperiodicity; };

F0 f0_yin(const std::vector<float>& x, float sr, float lo_hz, float hi_hz) {
    const int n = static_cast<int>(x.size());
    const int max_lag = static_cast<int>(sr / lo_hz) + 2;
    const int min_lag = static_cast<int>(sr / hi_hz);
    if (max_lag * 2 >= n || min_lag < 2) return { 0.f, 1.f };
    const int w = n - max_lag;
    std::vector<double> d(max_lag + 1, 0.0);
    for (int tau = 1; tau <= max_lag; ++tau) {
        double s = 0.0;
        for (int i = 0; i < w; ++i) {
            const double diff = static_cast<double>(x[i]) - x[i + tau];
            s += diff * diff;
        }
        d[tau] = s;
    }
    std::vector<double> dn(max_lag + 1, 1.0);
    double run = 0.0;
    for (int tau = 1; tau <= max_lag; ++tau) {
        run += d[tau];
        dn[tau] = run > 0.0 ? d[tau] * tau / run : 1.0;
    }
    constexpr double kThreshold = 0.15;
    int best = -1;
    for (int tau = min_lag; tau <= max_lag; ++tau) {
        if (dn[tau] < kThreshold) {
            while (tau + 1 <= max_lag && dn[tau + 1] < dn[tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0) {
        best = min_lag;
        for (int tau = min_lag; tau <= max_lag; ++tau)
            if (dn[tau] < dn[best]) best = tau;
    }
    double lag = best;
    if (best > min_lag && best < max_lag) {
        const double a = dn[best - 1], b = dn[best], c = dn[best + 1];
        const double den = a - 2.0 * b + c;
        if (den != 0.0) lag = best + 0.5 * (a - c) / den;
    }
    return { sr / static_cast<float>(lag), static_cast<float>(dn[best]) };
}

// A row whose aperiodicity reaches this has no fundamental to be right or
// wrong about. Calibrated on known signals (engine map section 9): every
// pitched test case sat below 0.0005, white noise at 0.9726.
constexpr float kPitchedMax = 0.30f;

// The window a FEED pitch measurement needs. A +-S cent cluster beats at
// 220 * (2^(S/600) - 1) Hz -- 0.764 Hz at S = 3 ct, a 1.31 s period -- and a
// window shorter than two beat periods reads a confident octave error
// (measured +1203 ct at aperiodicity 0.08 on a 16384-sample window, engine map
// section 9). 131072 samples is 2.73 s at 48 kHz.
constexpr int kF0Window = 131072;

}  // namespace

TEST_CASE("feed G1: the engine id is appended, never renumbered") {
    // A saved patch stores the id, so moving one silently reassigns every deck
    // that used it (engine_iface.h). This case is the census; the
    // static_assert in test_deck_bus.cpp is the build-time half.
    CHECK(ENGINE_TEST_TONE == 0);
    CHECK(ENGINE_SYNTH == 1);
    CHECK(ENGINE_SAMPLER == 2);
    CHECK(ENGINE_WAVE == 3);
    CHECK(ENGINE_BODY == 4);
    CHECK(ENGINE_BBD == 5);
    CHECK(ENGINE_FEED == 6);
    CHECK(ENGINE_COUNT == 7);
}

TEST_CASE("feed G2: FeedEngine satisfies the universal part-engine contract") {
    // Silence in stays bounded and finite forever; the process_in/
    // consumes_input pairing holds (FEED overrides neither, so the static
    // assert reads both as IPartEngine's); every no-op setter is safe in any
    // order. tests/part_engine_contract.h owns the reasoning.
    check_part_engine_contract<FeedEngine>([](FeedEngine& e) {
        e.set_seed(7u);
        e.init(48000.f);
    });
}

TEST_CASE("feed G3: a FEED deck is a note deck, and the switch completes") {
    Part p;
    p.init(48000.f, 5u);
    float l = 0.f, r = 0.f;
    p.set_engine(ENGINE_FEED);
    for (int i = 0; i < 500; ++i) p.process(l, r);   // 4 ms fade out + in
    REQUIRE(p.engine_id() == ENGINE_FEED);
    // Part derives the note-deck flag as "not SAMPLER and not BBD"
    // (part.cpp:43 and :460), so FEED gets the melodic phrase machinery for
    // free -- and that is exactly the kind of free behaviour that silently
    // stops being true when someone adds an engine to the exclusion list.
    CHECK(p.mod().pitch_lane_is_note_lane_for_test());
}

TEST_CASE("feed G3b: a FEED deck reports its envelope to the meter") {
    // Part::voice_env/active_voices return 0 for any engine they have no arm
    // for, so without one the VCV LED and Instrument's meter go dead on a FEED
    // deck. A coupled network is one sound, not n voices: slot 0 carries the
    // envelope and active_voices() is 1 while audible (spec section 6).
    Part p;
    p.init(48000.f, 5u);
    float l = 0.f, r = 0.f;
    p.set_engine(ENGINE_FEED);
    for (int i = 0; i < 500; ++i) p.process(l, r);
    REQUIRE(p.engine_id() == ENGINE_FEED);
    p.trigger_manual();
    float m = 0.f;
    for (int i = 0; i < 4800; ++i) { p.process(l, r); m = std::max(m, p.max_voice_env()); }
    CHECK(m > 0.1f);
}

TEST_CASE("feed G8: P is a measured decision, not a placeholder") {
    // feed_cfg::kPairs is set by the feed_pairs row on the Patch Submodule
    // (spec section 8). This gate is the only thing standing between a guessed
    // P and main; the bench task flips kPDecided when the result is in, and
    // names the run's docs/bench/ file in the commit message.
    CHECK(feed_cfg::kPDecided);
}

TEST_CASE("feed G9: the coupling enters the MODULATOR, not the carrier") {
    // Spec 3.1: at k > 0 pair i's modulator is driven by a signal at a
    // different fundamental, so the pair's spectrum stops being a function of
    // its own pitch alone. Carrier-side coupling was considered and rejected
    // because it reads as a mix. The difference is observable: modulator-side
    // coupling changes the SIDEBAND STRUCTURE around the carrier; a mix would
    // add a second carrier peak and leave the first one's sidebands alone.
    auto spectrum_at = [](float bond) {
        FeedEngine e = fresh_feed();
        e.set_resonance(0.f);                 // RATIO 1:1
        e.set_decay(1.f);                     // FLOOR up: a standing drone
        feed_lanes(e, 0.5f, bond, 0.3f, 1.f);
        e.set_flow(true);
        e.trigger(0.5f);
        settle(e);
        return mag_spectrum(render_l(e, 32768));
    };
    const std::vector<double> quiet = spectrum_at(0.f);
    const std::vector<double> bound = spectrum_at(0.6f);
    const double moved = spectral_flux(quiet, bound);
    CAPTURE(moved);
    CHECK(moved > 0.05);
}

TEST_CASE("feed G10: the inner life is the coupling -- two-sided") {
    // THE gate SWARM never had. All lanes static, FLOOR 1, nothing modulating:
    // the deck's spectrum must move by itself at mid BOND and must not at
    // BOND 0.
    //
    // The measure is SPECTRAL FLATNESS over a long window, not the
    // window-to-window magnitude flux the plan called for, and the reason is a
    // measurement rather than a preference.
    //
    // What the flux measure actually sees. At BOND 0 the pairs are independent
    // oscillators at FIXED frequencies, so nothing about the signal wanders --
    // but the SPREAD signature is a random draw, so the two closest pairs can
    // land arbitrarily close together (a tenth of a cent is 0.02 Hz at 311 Hz),
    // and a pair that close is unresolvable at any practical window length. Its
    // sum pulses at a beat period of tens of seconds, so consecutive windows
    // legitimately differ. Measured over five seeds: BOND 0 flux 0.031..0.210
    // against BOND 0.5 flux 0.124..0.231 -- overlapping ranges, and the
    // separation is set by which seed was drawn rather than by the coupling.
    // A gate built on it would pass or fail on the draw.
    //
    // What flatness sees instead. A fixed set of frequencies produces a LINE
    // spectrum however those lines beat against each other, because beating is
    // interference between constant partials and moves no partial anywhere.
    // Coupling drives each pair's modulator from another pair's output, so the
    // sidebands themselves wander and the lines BROADEN. Flatness is a
    // property of ONE window, so it cannot be fooled by which phase of a slow
    // beat the window landed on. Measured over the same five seeds:
    //
    //   seed    BOND 0     BOND 0.5    BOND 1.0
    //     99   0.000547   0.005369    0.125733
    //   4242   0.000826   0.004624    0.066854
    //    999   0.000436   0.003013    0.036366
    //      7   0.000933   0.004263    0.062244
    //  12345   0.000416   0.005348    0.078832
    //
    // -- monotone in BOND for every seed, with the worst separation still
    // 4.6x. The thresholds below are sized off that table: 0.002 is 2.1x the
    // worst BOND 0 reading, and the 3x ratio has 1.5x of margin against the
    // worst seed.
    //
    // RATIO must be an INTEGER here, and 1:1 is the one used. Flatness at
    // BOND 0 measures inharmonicity as well as wandering, and only an integer
    // modulator:carrier ratio puts the sidebands on the carrier's harmonics --
    // so only there is a still ring a line spectrum. Measured at BOND 0 across
    // the RATIO knob: 0.00064 at 1:1 and 0.024 at 4:1, against 0.084 at 2.5,
    // 0.187 at 6.8 and 0.288 at 11. The plan's knob position of 0.25 lands on
    // exactly 2.5 -- the midpoint between two integers, where the magnet has
    // no pull at all -- and the gate would have been measuring the ratio.
    auto flatness_at = [](float bond, int extra_seconds) {
        FeedEngine e = fresh_feed();
        e.set_decay(1.f);                      // FLOOR 1
        e.set_resonance(0.f);                  // RATIO 1:1, an integer
        feed_lanes(e, 0.5f, bond, 0.3f, 0.8f);
        e.set_flow(true);
        e.trigger(0.5f);
        for (int i = 0; i < 48000 * (5 + extra_seconds); ++i) {
            float l, r;
            e.process(l, r);
        }
        return spectral_flatness(mag_spectrum(render_l(e, 262144)));
    };
    const double still = flatness_at(0.f, 0);
    const double alive = flatness_at(0.5f, 0);
    const double harder = flatness_at(1.f, 0);
    CAPTURE(still);
    CAPTURE(alive);
    CAPTURE(harder);
    CAPTURE(feed_cfg::kSpreadKneeCt);
    // The inert half: uncoupled, the deck is a line spectrum.
    CHECK(still < 0.002);
    // The live half, both ways: coupled it is not, and more coupling is more.
    CHECK(alive > 3.0 * still);
    CHECK(harder > alive);
    // ...and the inner life does not stop. Measured 25 s later, the coupled
    // deck is still smeared and the uncoupled one is still a line spectrum --
    // which is the half the plan's "after 30 s" was reaching for.
    const double still_late = flatness_at(0.f, 25);
    const double alive_late = flatness_at(0.5f, 25);
    CAPTURE(still_late);
    CAPTURE(alive_late);
    CHECK(still_late < 0.002);
    CHECK(alive_late > 3.0 * still_late);
}

TEST_CASE("feed G11: bounded everywhere -- BOND x DEPTH x RATIO x pitch") {
    // Feedback FM's real failure mode, and the gate is cheap.
    for (float bond : { 0.f, 0.33f, 0.67f, 1.f })
    for (float depth : { 0.f, 0.5f, 1.f })
    for (float ratio : { 0.f, 0.5f, 1.f })
    for (float pitch : { 0.f, 0.5f, 1.f }) {
        CAPTURE(bond); CAPTURE(depth); CAPTURE(ratio); CAPTURE(pitch);
        FeedEngine e = fresh_feed();
        e.set_resonance(ratio);
        e.set_decay(1.f);
        e.set_sub(1.f);
        feed_lanes(e, pitch, bond, 1.f, depth);
        e.set_flow(true);
        e.trigger(pitch);
        for (int i = 0; i < 48000 * 2; ++i) {
            float l = 0.f, r = 0.f;
            e.process(l, r);
            REQUIRE(std::isfinite(l));
            REQUIRE(std::isfinite(r));
            REQUIRE(std::fabs(l) <= feed_cfg::kSatCeil + 1e-4f);
            REQUIRE(std::fabs(r) <= feed_cfg::kSatCeil + 1e-4f);
        }
    }
}

TEST_CASE("feed G12: high notes are attenuated") {
    // Spec 3.2.2. Without this the second stabilizer is written but not wired,
    // and the top of a chord escalates where it should stay clean. The claim is
    // about the EFFECTIVE feedback amount, so the gate reads it rather than
    // inferring it from audio.
    FeedEngine low = fresh_feed();
    feed_lanes(low, 0.05f);
    low.set_flow(true);
    settle(low, 8);
    FeedEngine high = fresh_feed();
    feed_lanes(high, 0.95f);
    high.set_flow(true);
    settle(high, 8);
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        CAPTURE(i);
        CHECK(high.pair_fb_amount_for_test(i) < low.pair_fb_amount_for_test(i));
    }
    // ...and it does not fall to zero, or BOND would be dead at the top.
    CHECK(high.pair_fb_amount_for_test(0) >=
          feed_cfg::kFbAttenMin * feed_cfg::kFbBaseCycles * (1.f - feed_cfg::kFbOffsetRange));
}

TEST_CASE("feed G13: SPREAD is symmetric about the played pitch -- two-sided") {
    // Spec 3.4 claims the perceived centre does not move with SPREAD. Until
    // this gate that is only a claim. The arithmetic half: the signature sums
    // to zero within every tone group, so the geometric mean of a group's
    // frequencies is exactly the tone.
    FeedEngine e = fresh_feed();
    feed_lanes(e, 0.5f, 0.f, 1.f);          // SPREAD at full
    e.set_flow(true);
    settle(e);
    double log_sum = 0.0;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        log_sum += std::log2(double(e.pair_hz_for_test(i)));
    const double geo_mean = std::exp2(log_sum / feed_cfg::kPairs);
    FeedEngine flat = fresh_feed();
    feed_lanes(flat, 0.5f, 0.f, 0.f);       // SPREAD at zero
    flat.set_flow(true);
    settle(flat);
    CAPTURE(geo_mean);
    CAPTURE(flat.pair_hz_for_test(0));
    CHECK(geo_mean == doctest::Approx(flat.pair_hz_for_test(0)).epsilon(1e-4));
    // The other side: SPREAD 0 really is zero spread, so the gate above is not
    // comparing two identical banks.
    for (int i = 1; i < feed_cfg::kPairs; ++i)
        CHECK(flat.pair_hz_for_test(i) == doctest::Approx(flat.pair_hz_for_test(0)));
    // ...and SPREAD 1 really spreads.
    float lo = 1e9f, hi = 0.f;
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        lo = std::min(lo, e.pair_hz_for_test(i));
        hi = std::max(hi, e.pair_hz_for_test(i));
    }
    const float span_ct = 1200.f * std::log2(hi / lo);
    CAPTURE(span_ct);
    CHECK(span_ct > 0.5f * feed_cfg::kSpreadMaxCt);
    CHECK(span_ct <= 2.05f * feed_cfg::kSpreadMaxCt);   // symmetric: +-max
}

TEST_CASE("feed G14: SPREAD's lower half stays in single-digit cents") {
    // Spec 3.4's frame: "beating audible, detune not". The upper half is
    // allowed to reach dense roughness; the lower half is not.
    FeedEngine e = fresh_feed();
    feed_lanes(e, 0.5f, 0.f, 0.5f);
    e.set_flow(true);
    settle(e);
    CAPTURE(e.spread_ct_for_test());
    CHECK(e.spread_ct_for_test() < 10.f);
    CHECK(e.spread_ct_for_test() > 0.f);     // and it is not simply off
}

TEST_CASE("feed G15: the pitch centre holds up to the BOND threshold") {
    // Spec 9.9, made falsifiable. Below kBondPitchThreshold, over SPREAD's
    // LOWER HALF, the estimated fundamental stays within kPitchCentreTolCt of
    // the played pitch. BEYOND the threshold the network may break and this
    // gate asserts nothing there -- but it does check that the region beyond
    // exists and was reached, so the test cannot pass by never leaving the
    // safe half.
    //
    // Two things here are the engine map's rather than this file's, and both
    // are departures from what the plan text assumed:
    //
    //  - The sweep runs SPREAD's lower half, not its whole travel. Above
    //    kSpreadKneeCt the cluster progressively stops having ONE fundamental
    //    on purpose (spec 3.4's dense roughness): measured 10 of 27 rows with
    //    no fundamental at all at kSpreadMaxCt. Asserting a cent tolerance
    //    there would be asserting a pitch the design dissolves.
    //  - The window is kF0Window, not a round number of seconds. A slowly
    //    beating cluster read through a short window gives a confident octave
    //    error; see kF0Window's comment.
    // The claim is about BOND, so the quantity is the DRIFT WITH BOND -- each
    // position measured against the same configuration at BOND 0. That is not
    // a weakening. An absolute bound at fixed SPREAD would be measuring
    // something else and failing on it: a detuned cluster's perceived centre
    // sits a fraction of its own half-spread away from the geometric mean of
    // its pairs, by up to 3.795 ct at kSpreadKneeCt on seed 99, for reasons
    // that have nothing to do with BOND -- the pairs are at fixed frequencies
    // and their exact positions are a random draw. G13 is what pins the
    // arithmetic centre; this gate pins what the knob does to it. The
    // SPREAD-0 rows carry the absolute claim, where there is no cluster to
    // confound it.
    const float played_hz = pitch_to_hz_ref(0.35f);
    int checked = 0;
    for (float spread : { 0.f, 0.25f, 0.5f }) {
        float base = 0.f;
        bool have_base = false;
        for (float bond = 0.f;
             bond <= feed_cfg::kBondPitchThreshold + 1e-4f; bond += 0.1f) {
            FeedEngine e = fresh_feed();
            e.set_decay(1.f);
            feed_lanes(e, 0.35f, bond, spread, 0.7f);
            e.set_flow(true);
            e.trigger(0.35f);
            settle(e, 60);
            const std::vector<float> x = render_l(e, kF0Window);
            const F0 f = f0_yin(x, 48000.f, played_hz * 0.5f, played_hz * 2.f);
            const float cents = 1200.f * std::log2(f.hz / played_hz);
            if (!have_base) { base = cents; have_base = true; }
            CAPTURE(bond); CAPTURE(spread); CAPTURE(f.hz); CAPTURE(cents);
            CAPTURE(base);
            CAPTURE(f.aperiodicity);
            // Inside the safe region the signal must HAVE a pitch, not merely
            // be near one. Without this half a network that dissolved into
            // noise would satisfy the tolerance by accident.
            REQUIRE(f.aperiodicity < kPitchedMax);
            // BOND does not move the centre.
            CHECK(std::fabs(cents - base) <= feed_cfg::kPitchCentreTolCt);
            // And with no cluster in the way, the centre IS the played pitch.
            if (spread == 0.f)
                CHECK(std::fabs(cents) <= feed_cfg::kPitchCentreTolCt);
            ++checked;
        }
    }
    REQUIRE(checked > 6);          // the loop actually ran
    CHECK(feed_cfg::kBondPitchThreshold < 1.f);   // there IS a region beyond
}

TEST_CASE("feed G16: FLOOR rides the top quarter of FALL -- two-sided") {
    // The fold that frees the RES slot for RATIO (the plan's control map).
    // Both halves asserted, because a fold that is always on and a fold that
    // is never on both pass a one-sided gate.
    FeedEngine e = fresh_feed();
    e.set_decay(feed_cfg::kFloorFoldStart * 0.5f);
    CHECK(e.floor_for_test() == 0.f);
    e.set_decay(feed_cfg::kFloorFoldStart);
    CHECK(e.floor_for_test() == doctest::Approx(0.f));
    e.set_decay(1.f);
    CHECK(e.floor_for_test() == doctest::Approx(1.f));
    // ...and it is monotone in between, so the knob has no step in it.
    float prev = -1.f;
    for (float n = feed_cfg::kFloorFoldStart; n <= 1.0001f; n += 0.02f) {
        e.set_decay(n);
        CAPTURE(n);
        REQUIRE(e.floor_for_test() >= prev);
        prev = e.floor_for_test();
    }
}

TEST_CASE("feed G17: FLOOR 1 is a standing drone, FLOOR 0 blooms and dies") {
    auto tail_after = [](float dec, float seconds) {
        FeedEngine e = fresh_feed();
        e.set_decay(dec);
        e.set_attack(0.2f);
        feed_lanes(e, 0.5f, 0.3f, 0.3f, 0.8f);
        e.set_flow(false);                    // STEP: no minimum floor
        e.trigger(0.5f);
        for (int i = 0; i < int(48000 * seconds); ++i) { float l, r; e.process(l, r); }
        return peak_of(render_l(e, 4800));
    };
    CHECK(tail_after(1.f, 8.f) > 0.02f);      // endless
    CHECK(tail_after(0.3f, 8.f) < 1e-4f);     // gone
}

TEST_CASE("feed G17b: RISE and FALL are the knobs, on the SynthEngineT law") {
    // A gap in the planned set, found by running it: G17 moves the DECAY knob
    // across the FLOOR fold and asserts what the FLOOR half does, so it passes
    // unchanged against an engine whose FALL TIME is a compiled-in constant --
    // which is exactly what the engine held while this gate was written. RISE
    // has no gate at all in the planned set.
    //
    // Both are measured as note length (the 5 %-of-own-peak idiom) and as time
    // to peak, in STEP so nothing sustains, and both are compared as RATIOS
    // between two knob positions rather than against an absolute -- the law is
    // ratio = 0.002 * 250^n for RISE and 0.1 * 80^n for FALL, so the knob's
    // meaning is a ratio and an absolute expectation would be pinning the
    // cycle length instead.
    auto shape = [](float rise_n, float fall_n) {
        FeedEngine e = fresh_feed();
        e.set_attack(rise_n);
        e.set_decay(fall_n);
        feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
        e.set_flow(false);
        e.trigger(0.5f);
        const std::vector<float> x = render_l(e, 48000 * 6);
        const float pk = peak_of(x);
        int to_peak = 0, len = 0;
        for (size_t i = 0; i < x.size(); ++i) {
            if (std::fabs(x[i]) >= pk * 0.999f && to_peak == 0)
                to_peak = static_cast<int>(i);
            if (std::fabs(x[i]) > 0.05f * pk) len = static_cast<int>(i);
        }
        return std::pair<int, int>(to_peak, len);
    };
    // FALL: both positions below kFloorFoldStart, so FLOOR is 0 on both and
    // what moves is the tail alone.
    const auto shortf = shape(0.f, 0.1f);
    const auto longf  = shape(0.f, 0.6f);
    CAPTURE(shortf.second); CAPTURE(longf.second);
    CHECK(longf.second > 2 * shortf.second);
    // RISE, at a fixed FALL.
    const auto fast = shape(0.05f, 0.6f);
    const auto slow = shape(0.8f, 0.6f);
    CAPTURE(fast.first); CAPTURE(slow.first);
    CHECK(slow.first > 4 * fast.first);
    // ...and the cycle scales both, which is what makes them ratios.
    FeedEngine e = fresh_feed();
    e.set_cycle(4.f);
    e.set_attack(0.f);
    e.set_decay(0.6f);
    feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
    e.set_flow(false);
    e.trigger(0.5f);
    const std::vector<float> x = render_l(e, 48000 * 20);
    const float pk = peak_of(x);
    int len4 = 0;
    for (size_t i = 0; i < x.size(); ++i)
        if (std::fabs(x[i]) > 0.05f * pk) len4 = static_cast<int>(i);
    CAPTURE(len4);
    CHECK(len4 > 2 * longf.second);
}

TEST_CASE("feed G18: the index rides the envelope, not just the level") {
    // Spec 2.5: bright and rough on the attack, darker and calmer in the tail.
    // If the index were constant, the attack and the tail would have the same
    // spectral shape at different gains -- so the gate normalises the two
    // windows and compares their SHAPE.
    //
    // LEVEL is held well below kSatCeil, and that is what makes this gate
    // about the index at all. At LEVEL 1 the attack window clips into the tanh
    // and the tail window does not, which moves the centroid by itself:
    // measured, the gate passed unchanged with the envelope removed from
    // set_index entirely. It was measuring the ceiling.
    FeedEngine e = fresh_feed();
    e.set_attack(0.6f);
    e.set_decay(0.5f);
    e.set_resonance(0.4f);
    feed_lanes(e, 0.4f, 0.2f, 0.2f, 1.f, /*level=*/0.25f);
    e.set_flow(false);
    e.trigger(0.4f);
    const std::vector<float> attack = render_l(e, 8192);
    for (int i = 0; i < 24000; ++i) { float l, r; e.process(l, r); }
    const std::vector<float> tail = render_l(e, 8192);
    auto centroid = [](const std::vector<float>& x) {
        const std::vector<double> m = mag_spectrum(x);
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < m.size(); ++i) { num += i * m[i]; den += m[i]; }
        return den > 0.0 ? num / den : 0.0;
    };
    const double c_attack = centroid(attack);
    const double c_tail = centroid(tail);
    CAPTURE(c_attack);
    CAPTURE(c_tail);
    CHECK(c_tail < 0.8 * c_attack);
}

TEST_CASE("feed G19: FLOW keeps a minimum floor at FLOOR 0") {
    // The drone promise. SWARM's rule, kept (spec section 5).
    FeedEngine e = fresh_feed();
    e.set_decay(0.f);                          // FLOOR 0
    feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
    e.set_flow(true);
    e.trigger(0.5f);
    for (int i = 0; i < 48000 * 10; ++i) { float l, r; e.process(l, r); }
    CHECK(peak_of(render_l(e, 4800)) > 0.01f);
    // The other side: in STEP the same knob really does decay to nothing, so
    // the floor is a FLOW rule and not a leak.
    FeedEngine s = fresh_feed();
    s.set_decay(0.f);
    feed_lanes(s, 0.5f, 0.2f, 0.2f, 0.8f);
    s.set_flow(false);
    s.trigger(0.5f);
    for (int i = 0; i < 48000 * 10; ++i) { float l, r; s.process(l, r); }
    CHECK(peak_of(render_l(s, 4800)) < 1e-4f);
}

TEST_CASE("feed G20: CHOKE decays the drone out and stops re-arming") {
    FeedEngine e = fresh_feed();
    e.set_decay(1.f);
    feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
    e.set_flow(true);
    e.trigger(0.5f);
    settle(e, 40);
    const float before = peak_of(render_l(e, 4800));
    REQUIRE(before > 0.02f);
    e.set_hold(true);
    // Monotone decay -- a level that dips and returns is an auto-retrigger
    // that did not stop. Sampled on the ENVELOPE rather than on the audio
    // peak: the audio is a drone of detuned pairs beating against each other,
    // so its peak over a 2400-sample window rises and falls by a percent or
    // two under a perfectly monotone envelope (measured +1.2 % at window 6),
    // and a gate on that would be measuring the beat. The envelope is what
    // must not re-arm, and it is exactly what voice_env reports to the meter.
    // The windows are a second each, not a tenth: at DEC 1 the FALL ratio is
    // 0.1 * 80^1 = 8 cycles, so a 60 dB release takes 8 s and twelve tenths of
    // a second only reaches 39 % of the starting level.
    float prev = e.voice_env(0);
    REQUIRE(prev > 0.f);
    for (int w = 0; w < 12; ++w) {
        for (int i = 0; i < 48000; ++i) { float l, r; e.process(l, r); }
        const float now = e.voice_env(0);
        CAPTURE(w); CAPTURE(now); CAPTURE(prev);
        REQUIRE(now <= prev);
        prev = now;
    }
    // ...and the audio really did go with it.
    CHECK(peak_of(render_l(e, 4800)) < 0.1f * before);
    // Release re-arms.
    e.set_hold(false);
    settle(e, 40);
    CHECK(peak_of(render_l(e, 4800)) > 0.5f * before);
}

TEST_CASE("feed G21: the accent spends itself twice -- and DEC gates the second") {
    // Spec section 5, the SYNTH/WAVE/BODY shape. Two halves, and the DEC gate
    // is what makes the ring half's inert case reachable (vacuous shape 4).
    //
    // Ring time is measured as the index of the last sample above 5 % of the
    // note's OWN peak -- tests/test_step_accent.cpp's note_len_samples idiom.
    // A ratio of two window peaks (which the plan asked for) is degenerate at
    // DEC 0, where both windows are already silent and the inert half reads
    // 0 == 0: true, and true whatever the accent does.
    //
    // LEVEL is held well below the ceiling on purpose. At LEVEL 1 the full
    // note peaks at 0.523 against a kSatCeil of 0.55, so its peak is
    // tanh-compressed and the weak note's is not -- and "5 % of own peak" then
    // lands at different points on two notes whose envelopes are identical.
    // Measured: the DEC-0 half read 3085 against 3313 samples, a 7 % gap that
    // was the ceiling and not the accent.
    struct Note { float peak; int len; };
    auto note = [](float accent, float dec) {
        FeedEngine e = fresh_feed();
        e.set_attack(0.f);
        e.set_decay(dec);
        feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f, /*level=*/0.35f);
        e.set_flow(false);
        e.set_accent(accent);
        e.trigger(0.5f);
        const std::vector<float> x = render_l(e, 48000 * 3);
        const float pk = peak_of(x);
        int len = 0;
        for (size_t i = 0; i < x.size(); ++i)
            if (std::fabs(x[i]) > 0.05f * pk) len = static_cast<int>(i);
        return Note{ pk, len };
    };
    const Note full = note(0.f, 0.5f);
    const Note weak = note(1.f, 0.5f);
    CAPTURE(full.peak); CAPTURE(weak.peak);
    CAPTURE(full.len);  CAPTURE(weak.len);
    // Hit height: the accent scales the strike, and not past its own floor.
    CHECK(weak.peak < full.peak);
    CHECK(weak.peak > feed_cfg::kAccentVelFloor * 0.8f * full.peak);
    // Ring time: shorter at accent 1 with DEC up...
    CHECK(weak.len < full.len);
    // ...and untouched at DEC 0, which is the half that has to be reachable.
    const Note full0 = note(0.f, 0.f);
    const Note weak0 = note(1.f, 0.f);
    CAPTURE(full0.len); CAPTURE(weak0.len);
    REQUIRE(full0.len > 0);            // there IS a note to compare
    CHECK(weak0.len == doctest::Approx(full0.len).epsilon(0.02));
}

TEST_CASE("feed G22: a retrigger is click-free") {
    // Env::trigger rises from the CURRENT level, so a re-hit on a sounding
    // drone must not step.
    //
    // FLOOR is deliberately MID, not 1. At FLOOR 1 the envelope is already
    // pinned at its ceiling when the retrigger lands, so trigger() has nothing
    // to change and "click-free" is true of every possible implementation --
    // measured, a trigger that reset the level to zero left the FLOOR-1
    // formulation green.
    FeedEngine e = fresh_feed();
    e.set_decay(0.8f);                       // FLOOR ~0.2, a sustaining drone
    e.set_attack(0.4f);
    feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
    e.set_flow(true);
    e.trigger(0.5f);
    settle(e, 400);                          // ...settled onto the floor

    const std::vector<float> quiet = render_l(e, 24000);
    float worst_running = 0.f;
    for (size_t i = 1; i < quiet.size(); ++i)
        worst_running = std::max(worst_running, std::fabs(quiet[i] - quiet[i - 1]));

    // Read the level at the trigger, not before the window above it: the
    // envelope is still settling onto the floor and is legitimately lower here
    // than it was 24000 samples ago.
    const float env_before = e.voice_env(0);
    REQUIRE(env_before > 0.05f);
    REQUIRE(env_before < 0.9f);              // there IS room to rise
    e.trigger(0.5f);
    // The envelope is the discriminating observable: the bank's amplitude
    // glide smooths the AUDIO whatever the envelope does, so an audio-only
    // bound cannot tell a rise-from-current-level from a restart-from-zero.
    // voice_env is not glided.
    float env_floor = 1e9f;
    std::vector<float> hit;
    for (int i = 0; i < 4800; ++i) {
        float l = 0.f, r = 0.f;
        e.process(l, r);
        hit.push_back(l);
        env_floor = std::min(env_floor, e.voice_env(0));
    }
    CAPTURE(env_before);
    CAPTURE(env_floor);
    // Rises FROM the current level: it never dips below where it started.
    CHECK(env_floor >= env_before - 1e-4f);

    // ...and end to end, the audio does not step either. The bound is against
    // the same signal's own worst derivative while running, so the threshold
    // is measured rather than invented.
    float worst_hit = 0.f;
    for (size_t i = 1; i < hit.size(); ++i)
        worst_hit = std::max(worst_hit, std::fabs(hit[i] - hit[i - 1]));
    CAPTURE(worst_running);
    CAPTURE(worst_hit);
    CHECK(worst_hit < 3.f * worst_running);
}

TEST_CASE("feed G23: trigger_chord fires ONE envelope hit, not n") {
    // IPartEngine's default implementation loops trigger(). A four-note chord
    // would be four hits inside one sample -- four Env::trigger calls, each
    // restarting the attack, so the audible result is one hit at the wrong
    // shape and three wasted.
    //
    // The envelope's SHAPE is the observable, and it is read directly rather
    // than inferred from the audio's peak index: the ring is a drone of
    // detuned pairs, so where its waveform happens to peak is a beat artifact.
    // voice_env is the envelope itself.
    auto rise_profile = [](const float* p, int n) {
        FeedEngine e = fresh_feed();
        e.set_attack(0.5f);
        e.set_decay(0.4f);
        feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
        e.set_flow(false);
        e.trigger_chord(p, n);
        std::vector<float> env;
        for (int i = 0; i < 12000; ++i) {
            float l, r;
            e.process(l, r);
            env.push_back(e.voice_env(0));
        }
        return env;
    };
    const float one[1] = { 0.4f };
    const float four[4] = { 0.4f, 0.45f, 0.5f, 0.55f };
    const std::vector<float> a = rise_profile(one, 1);
    const std::vector<float> b = rise_profile(four, 4);
    REQUIRE(a.size() == b.size());
    // One hit rises once, and a four-note chord must rise identically.
    float worst = 0.f;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, std::fabs(a[i] - b[i]));
    CAPTURE(worst);
    CHECK(worst < 1e-6f);
    // And the envelope actually did something, so the comparison is not
    // between two flat lines.
    CHECK(peak_of(a) > 0.5f);

    // The half that actually catches a missing override, and the envelope
    // above is NOT it. Env::trigger() only sets the stage to Attack -- it does
    // not reset the level -- so calling it n times in one sample is idempotent
    // and the interface default's n hits are envelope-identical to one. What
    // the default really does is call trigger(p[i]) per note, and FEED's
    // trigger() sets _chord[0] and _chord_n = 1 each time: the chord collapses
    // to its LAST note and the deck voices one tone instead of the cap.
    // Measured: with trigger_chord replaced by the default's loop, the
    // envelope comparison above stayed green.
    FeedEngine g = fresh_feed();
    feed_lanes(g, 0.5f, 0.2f, 0.f, 0.8f);      // SPREAD 0: pitches are tones
    g.set_flow(true);
    g.trigger_chord(four, 4);
    settle(g, 60);
    const int cap = feed_cfg::kPairs / feed_cfg::kPairsPerTone;
    CAPTURE(g.voiced_tones_for_test());
    CHECK(g.voiced_tones_for_test() == (4 < cap ? 4 : cap));
    // ...and the tones really are the chord's, sorted, not the last note n
    // times: pair 0 sits on the lowest.
    CHECK(g.pair_hz_for_test(0) ==
          doctest::Approx(pitch_to_hz_ref(four[0])).epsilon(0.001));
}

TEST_CASE("feed G24: a chord change is a glissando, not a retrigger") {
    // set_chord must re-voice the network live, with no envelope hit at all --
    // COLOR is a modulation destination and it moves every control tick.
    FeedEngine e = fresh_feed();
    e.set_decay(1.f);
    feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
    e.set_flow(true);
    const float one[1] = { 0.4f };
    e.set_chord(one, 1);
    e.trigger_chord(one, 1);
    settle(e, 60);
    const float env_before = e.voice_env(0);
    REQUIRE(env_before > 0.1f);
    const float four[4] = { 0.4f, 0.5f, 0.6f, 0.7f };
    e.set_chord(four, 4);
    // The envelope must not jump: a retrigger would push it back toward 1.
    for (int i = 0; i < 96; ++i) { float l, r; e.process(l, r); }
    CHECK(e.voice_env(0) == doctest::Approx(env_before).epsilon(0.02));
    // ...but the pitches must move.
    settle(e, 60);
    bool moved = false;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        if (std::fabs(e.pair_hz_for_test(i) - pitch_to_hz_ref(0.4f)) >
            pitch_to_hz_ref(0.4f) * 0.02f) moved = true;
    CHECK(moved);
}

TEST_CASE("feed G25: pairs on the root hold still, and the tone cap binds") {
    // Nearest-neighbour allocation, spec section 5: pairs on common tones hold
    // still, only moving ones glide. The strided grouping (pair i -> tone
    // i % voiced) is what delivers it: growing the chord leaves pair 0 on the
    // root.
    FeedEngine e = fresh_feed();
    feed_lanes(e, 0.5f, 0.f, 0.f);         // SPREAD 0: pitches are the tones
    e.set_flow(true);
    const float one[1] = { 0.4f };
    e.set_chord(one, 1);
    settle(e, 60);
    const float root_hz = e.pair_hz_for_test(0);
    // The root really is the tone, not whatever the lane happened to hold.
    CHECK(root_hz == doctest::Approx(pitch_to_hz_ref(0.4f)).epsilon(0.001));
    const float four[4] = { 0.4f, 0.5f, 0.6f, 0.7f };
    e.set_chord(four, 4);
    settle(e, 60);
    CHECK(e.pair_hz_for_test(0) == doctest::Approx(root_hz).epsilon(0.001));

    // The cap: at most kPairs / kPairsPerTone tones are voiced, so every
    // voiced tone keeps a group SPREAD can reach (plan open point 4).
    const int cap = feed_cfg::kPairs / feed_cfg::kPairsPerTone;
    CHECK(e.voiced_tones_for_test() == (4 < cap ? 4 : cap));
    e.set_chord(one, 1);
    settle(e, 60);
    CHECK(e.voiced_tones_for_test() == 1);

    // Tones are stored SORTED, which is what makes i % voiced a
    // nearest-neighbour map rather than an arbitrary one: the same chord
    // pushed in a different order must voice the same frequencies.
    const float shuffled[4] = { 0.6f, 0.4f, 0.7f, 0.5f };
    e.set_chord(four, 4);
    settle(e, 60);
    std::vector<float> in_order;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        in_order.push_back(e.pair_hz_for_test(i));
    e.set_chord(shuffled, 4);
    settle(e, 60);
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        CAPTURE(i);
        CHECK(e.pair_hz_for_test(i) == doctest::Approx(in_order[i]).epsilon(0.001));
    }
}

TEST_CASE("feed G26: RATIO's lower half gravitates to the integers") {
    // Spec section 4. A continuous knob stands BETWEEN the integers almost
    // everywhere, and near-integer ratios read as chorus -- motion from the
    // wrong source. Plan open point 3 chose a monotone warp over zones with
    // hysteresis, and monotonicity is asserted rather than claimed.
    //
    // "Gravitates" is measured AGAINST THE LINEAR MAP, computed here over the
    // same knob positions, rather than against a fixed fraction. The plan
    // asked for "more than half the travel within 2 % of an integer", but that
    // is a statement about kRatioMagnetExp -- at its by-ear 3.0 the true
    // figure is 34 %, and the gate would have been asserting a by-ear literal
    // through the back door (memory fireflow-vacuous-test-gates, shape 3). The
    // ratio between the two maps is the claim that survives a retuning.
    FeedEngine e = fresh_feed();
    int near_magnet = 0, near_linear = 0, samples = 0;
    float prev = -1.f;
    for (float n = 0.f; n <= 0.5f + 1e-6f; n += 0.002f) {
        e.set_resonance(n);
        const float r = e.ratio_for_test();
        // The same knob position under a plain linear map to the same span.
        const float lin = 1.f + (feed_cfg::kRatioMagnetTop - 1.f) * (n * 2.f);
        CAPTURE(n); CAPTURE(r); CAPTURE(lin);
        REQUIRE(r >= prev);                       // monotone: no zone flip
        prev = r;
        REQUIRE(r >= 1.f - 1e-4f);
        REQUIRE(r <= feed_cfg::kRatioMagnetTop + 1e-4f);
        if (std::fabs(r - std::round(r)) < 0.02f) ++near_magnet;
        if (std::fabs(lin - std::round(lin)) < 0.02f) ++near_linear;
        ++samples;
    }
    CAPTURE(near_magnet);
    CAPTURE(near_linear);
    CAPTURE(samples);
    REQUIRE(near_linear > 0);                     // the baseline is reachable
    CHECK(near_magnet > 3 * near_linear);
    // The endpoints are exact, or the lower half does not actually reach 1:1
    // and 4:1.
    e.set_resonance(0.f);
    CHECK(e.ratio_for_test() == doctest::Approx(1.f));
    e.set_resonance(0.5f);
    CHECK(e.ratio_for_test() == doctest::Approx(feed_cfg::kRatioMagnetTop));
}

TEST_CASE("feed G27: RATIO's upper half runs continuously into the irrational") {
    FeedEngine e = fresh_feed();
    int off_integer = 0, samples = 0;
    float prev = feed_cfg::kRatioMagnetTop - 1e-4f;
    for (float n = 0.5f; n <= 1.f + 1e-6f; n += 0.002f) {
        e.set_resonance(n);
        const float r = e.ratio_for_test();
        CAPTURE(n); CAPTURE(r);
        REQUIRE(r >= prev);
        prev = r;
        if (std::fabs(r - std::round(r)) > 0.1f) ++off_integer;
        ++samples;
    }
    CAPTURE(off_integer);
    CAPTURE(samples);
    CHECK(off_integer > samples / 2);           // no magnet up here
    e.set_resonance(1.f);
    CHECK(e.ratio_for_test() == doctest::Approx(feed_cfg::kRatioMax));
}

TEST_CASE("feed G28: DAMP is honestly a filter, and its centre is neutral") {
    // FILT is bipolar: left sweeps the feedback path's cutoff DOWN (dark and
    // tame -- the loop loses the highs that feed escalation), right sweeps it
    // up toward effectively open (bright and wild). The centre detent is the
    // by-ear neutral cutoff.
    auto centroid_at = [](float t) {
        FeedEngine e = fresh_feed();
        e.set_filt(t);
        e.set_decay(1.f);
        e.set_resonance(0.3f);
        feed_lanes(e, 0.35f, 0.5f, 0.3f, 0.9f);
        e.set_flow(true);
        e.trigger(0.35f);
        settle(e, 60);
        const std::vector<double> m = mag_spectrum(render_l(e, 32768));
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < m.size(); ++i) { num += i * m[i]; den += m[i]; }
        return den > 0.0 ? num / den : 0.0;
    };
    const double dark = centroid_at(-1.f);
    const double mid = centroid_at(0.f);
    const double bright = centroid_at(1.f);
    CAPTURE(dark); CAPTURE(mid); CAPTURE(bright);
    CHECK(dark < mid);
    CHECK(mid < bright);
}

TEST_CASE("feed G29: SUB is a sub, and DEPTH 0.5 is a good sound") {
    // Two claims in one case because they share a setup, and both are about
    // the deck being usable rather than merely finite.
    //
    // SUB: energy an octave below the root appears when the knob is up and is
    // absent when it is down.
    auto sub_energy = [](float sub_n) {
        FeedEngine e = fresh_feed();
        e.set_sub(sub_n);
        e.set_decay(1.f);
        e.set_resonance(0.f);
        feed_lanes(e, 0.5f, 0.f, 0.f, 0.f);     // DEPTH 0: a bare carrier
        e.set_flow(true);
        e.trigger(0.5f);
        settle(e, 60);
        const std::vector<double> m = mag_spectrum(render_l(e, 32768));
        const double bin_hz = 48000.0 / 32768.0;
        const int half = int(0.5 * pitch_to_hz_ref(0.5f) / bin_hz);
        double band = 0.0;
        for (int i = half - 2; i <= half + 2; ++i) band += m[i];
        return band;
    };
    CHECK(sub_energy(1.f) > 20.0 * sub_energy(0.f));

    // DEPTH at kDepthBase must be a SOUND, not a dead sine. DEPTH is the one
    // FEED control with no knob of its own (the plan's control map), so spec
    // section 4's defensive requirement is what stands in for one -- and this
    // is what makes it falsifiable.
    FeedEngine e = fresh_feed();
    e.set_decay(1.f);
    e.set_resonance(0.3f);
    feed_lanes(e, 0.4f, 0.4f, 0.3f, feed_cfg::kDepthBase);
    e.set_flow(true);
    e.trigger(0.4f);
    settle(e, 60);
    const std::vector<double> m = mag_spectrum(render_l(e, 32768));
    const double bin_hz = 48000.0 / 32768.0;
    const int f0_bin = int(pitch_to_hz_ref(0.4f) / bin_hz);
    double fundamental = 0.0, above = 0.0;
    for (int i = f0_bin - 3; i <= f0_bin + 3; ++i) fundamental += m[i];
    for (size_t i = size_t(f0_bin) + 4; i < m.size(); ++i) above += m[i];
    CAPTURE(fundamental);
    CAPTURE(above);
    // A bare sine puts essentially nothing above the fundamental. "A good
    // sound" is not testable; "has a spectrum" is, and it is the half that
    // would actually have caught a dead default.
    CHECK(above > 0.1 * fundamental);
}

TEST_CASE("feed G30: same seed and same knobs give bit-identical audio") {
    auto run = [](uint32_t seed) {
        FeedEngine e = fresh_feed(seed);
        e.set_resonance(0.35f);
        e.set_decay(0.9f);
        e.set_filt(0.3f);
        e.set_sub(0.5f);
        feed_lanes(e, 0.45f, 0.55f, 0.6f, 0.8f);
        e.set_flow(true);
        e.trigger(0.45f);
        return render_l(e, 48000 * 2);
    };
    const std::vector<float> a = run(4242u);
    const std::vector<float> b = run(4242u);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);
    // Different seed, different individual -- otherwise the seed is dead state
    // and G31 below could not tell anything.
    const std::vector<float> c = run(999u);
    bool differs = false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != c[i]) differs = true;
    CHECK(differs);
}

TEST_CASE("feed G31: NEW redraws the individual, and only NEW does") {
    FeedEngine e = fresh_feed(4242u);
    feed_lanes(e, 0.5f, 0.4f, 1.f, 0.8f);      // SPREAD full: the signature shows
    e.set_flow(true);
    settle(e, 60);
    std::vector<float> before;
    std::vector<float> fb_before;
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        before.push_back(e.pair_hz_for_test(i));
        fb_before.push_back(e.pair_fb_amount_for_test(i));
    }

    // Everything short of NEW leaves the individual alone: re-pushing the same
    // lanes, moving the chord and coming back, triggering again.
    feed_lanes(e, 0.5f, 0.4f, 1.f, 0.8f);
    const float chord[2] = { 0.5f, 0.6f };
    e.set_chord(chord, 2);
    settle(e, 60);
    const float one[1] = { 0.5f };
    e.set_chord(one, 1);
    e.trigger_chord(one, 1);
    settle(e, 60);
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        CAPTURE(i);
        CHECK(e.pair_hz_for_test(i) == doctest::Approx(before[i]).epsilon(0.001));
        CHECK(e.pair_fb_amount_for_test(i) ==
              doctest::Approx(fb_before[i]).epsilon(0.001));
    }

    e.reseed(777u);
    settle(e, 60);
    int hz_moved = 0, fb_moved = 0;
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        if (std::fabs(e.pair_hz_for_test(i) - before[i]) > before[i] * 0.0005f) ++hz_moved;
        if (std::fabs(e.pair_fb_amount_for_test(i) - fb_before[i]) >
            fb_before[i] * 0.005f) ++fb_moved;
    }
    CAPTURE(hz_moved); CAPTURE(fb_moved);
    // Both halves of the individual redrawn -- the detune signature AND the
    // per-pair feedback offsets (spec 3.4). A reseed that moved only the
    // frequencies would leave the cliff an edge rather than a gradient.
    CHECK(hz_moved > 0);
    CHECK(fb_moved > 0);
}

TEST_CASE("feed G32: NEW on a FEED deck reaches the ring") {
    // Instrument::new_phrase reached only mod() until this task, so the redraw
    // had no route in at all.
    Part p;
    p.init(48000.f, 5u);
    float l = 0.f, r = 0.f;
    p.set_engine(ENGINE_FEED);
    for (int i = 0; i < 500; ++i) p.process(l, r);
    REQUIRE(p.engine_id() == ENGINE_FEED);
    p.set_target_base(LANE_SIZE, 1.f);          // SPREAD full
    for (int i = 0; i < 96 * 80; ++i) p.process(l, r);
    std::vector<float> before;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        before.push_back(p.feed().pair_hz_for_test(i));
    p.new_phrase();
    for (int i = 0; i < 96 * 80; ++i) p.process(l, r);
    int moved = 0;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        if (std::fabs(p.feed().pair_hz_for_test(i) - before[i]) > before[i] * 0.0005f)
            ++moved;
    CHECK(moved > 0);
    // NEW is deterministic AND progressive: a fresh Part pressed NEW three
    // times always lands on the same third individual, which is what makes
    // this gate and any future render reproducible.
    auto third_press = [] {
        Part q;
        q.init(48000.f, 5u);
        float a = 0.f, b = 0.f;
        q.set_engine(ENGINE_FEED);
        for (int i = 0; i < 500; ++i) q.process(a, b);
        q.set_target_base(LANE_SIZE, 1.f);
        for (int k = 0; k < 3; ++k) q.new_phrase();
        for (int i = 0; i < 96 * 80; ++i) q.process(a, b);
        std::vector<float> hz;
        for (int i = 0; i < feed_cfg::kPairs; ++i)
            hz.push_back(q.feed().pair_hz_for_test(i));
        return hz;
    };
    const std::vector<float> x = third_press();
    const std::vector<float> y = third_press();
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        CAPTURE(i);
        CHECK(x[i] == doctest::Approx(y[i]).epsilon(1e-6));
    }
    // ...and the third press is not the first: the counter advances.
    bool progressed = false;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        if (std::fabs(x[i] - before[i]) > before[i] * 0.0005f) progressed = true;
    CHECK(progressed);
}
