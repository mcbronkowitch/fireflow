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
    auto flatness_at = [](float bond, int extra_seconds) {
        FeedEngine e = fresh_feed();
        e.set_decay(1.f);                      // FLOOR 1
        e.set_resonance(0.25f);
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
