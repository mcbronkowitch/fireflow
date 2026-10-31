// CPU round 2, Phase A (docs/superpowers/specs/2026-10-30-cpu-engine-kernels-
// design.md section 5.1): the SAMPLER split. Built into spky_probe_tests, the
// one desktop target compiled with SPKY_PROBE_MARKS=1 and SPKY_PROBE_TOGGLES=1
// as the measurement image is; the default build has neither, so these cases
// could not fail there. The stamp clock is test_probe_marks.cpp's
// (spky::probe_clock); these cases count, they do not time.
// Also the toggle bits' plumbing for the round's candidates (bits 19 and 22
// on), whose two paths can only both be compiled here.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "sampler/sampler_engine.h"
#include "synth/synth_engine.h"

namespace {

constexpr size_t kFrames = 48000 * 2;

// A sampler deck with 0.5 s of a 441 Hz sine as content, the lanes at the
// sampler tests' defaults (SOURCE 0, SIZE 0.5, PITCH 0.5, MOTION 0, LEVEL 1).
struct SamplerRig {
    std::vector<spky::SampleBuffer::Frame> mem = std::vector<spky::SampleBuffer::Frame>(kFrames);
    spky::SamplerEngine e;
    explicit SamplerRig(uint32_t seed = 4242) {
        std::vector<float> l(24000), r(24000);
        for (size_t i = 0; i < l.size(); ++i) {
            l[i] = std::sin(6.2831853f * 441.f * float(i) / 48000.f);
            r[i] = l[i];
        }
        e.set_seed(seed);
        e.set_memory(mem.data(), kFrames);
        e.init(48000.f);
        e.set_cycle(1.f);
        e.load_sample(l.data(), r.data(), l.size());
        const float t[spky::LANE_COUNT] = { 0.f, 0.5f, 0.5f, 0.f, 1.f };
        e.set_targets(t, 0.5f);
        e.set_flow(true);
    }
};

} // namespace

TEST_CASE("sampler probe counts grain-samples") {
    SamplerRig g;
    uint64_t want = 0;
    const uint32_t before = g.e.probe_eng().gsamp;
    for (int i = 0; i < 9600; ++i) {
        float l = 0.f, r = 0.f;
        g.e.process(l, r);
        want += static_cast<uint64_t>(g.e.active_grains());
    }
    // The cloud stood: grains were active, so the equality below is not 0 == 0.
    CHECK(want > 9600u);
    CHECK(static_cast<uint64_t>(g.e.probe_eng().gsamp - before) == want);
}

namespace {

// A SYNTH or WAVE deck at the lanes' noon with full LEVEL, one control tick at
// a time.
template <class E>
struct DeckRig {
    E e;
    explicit DeckRig(uint32_t seed = 99) {
        e.set_seed(seed);
        e.init(48000.f);
        const float t[spky::LANE_COUNT] = { 0.5f, 1.f, 0.5f, 0.f, 1.f };
        e.set_targets(t, 0.5f);
    }
    double tick(std::vector<float>* out = nullptr) {
        double sum = 0.0;
        for (int i = 0; i < 96; ++i) {
            float l = 0.f, r = 0.f;
            e.process(l, r);
            sum += double(l) * l + double(r) * r;
            if (out) { out->push_back(l); out->push_back(r); }
        }
        return sum;
    }
};

template <class E>
void check_svf1_reaches_voices() {
    DeckRig<E> g;
    g.tick();
    for (int v = 0; v < E::kVoices; ++v)
        CHECK_FALSE(g.e.probe_voice(v).svf_single_pass());
    g.e.set_probe_word(1u << spky::probe::kBitLeanSvf1);
    g.tick();
    for (int v = 0; v < E::kVoices; ++v)
        CHECK(g.e.probe_voice(v).svf_single_pass());
    g.e.set_probe_word(~(1u << spky::probe::kBitLeanSvf1));   // every other bit
    g.tick();
    for (int v = 0; v < E::kVoices; ++v)
        CHECK_FALSE(g.e.probe_voice(v).svf_single_pass());
}

} // namespace

TEST_CASE("V6 bit 19 reaches every SYNTH and WAVE voice's filter on the next control tick") {
    check_svf1_reaches_voices<spky::SynthEngine>();
    check_svf1_reaches_voices<spky::WaveEngine>();
}

namespace {

template <class E>
void check_exp2_reaches_voices() {
    DeckRig<E> g;
    g.tick();
    for (int v = 0; v < E::kVoices; ++v)
        CHECK_FALSE(g.e.probe_voice(v).fast_exp2_on());
    g.e.set_probe_word(1u << spky::probe::kBitLeanExp2);
    g.tick();
    for (int v = 0; v < E::kVoices; ++v)
        CHECK(g.e.probe_voice(v).fast_exp2_on());
    g.e.set_probe_word(~(1u << spky::probe::kBitLeanExp2));   // every other bit
    g.tick();
    for (int v = 0; v < E::kVoices; ++v)
        CHECK_FALSE(g.e.probe_voice(v).fast_exp2_on());
}

// A FLOW drone at a DETUNE of `detune` with MOTION at full (the per-voice drift
// LFO running, so the oscillators' ratio is recomputed every tick), 1 s,
// with or without bit 25: the left channel, and its rising zero crossings.
template <class E>
std::vector<float> render_detuned(float detune, uint32_t word) {
    DeckRig<E> g;
    g.e.set_probe_word(word);
    g.e.set_flow(true);
    g.e.set_detune(detune);
    const float tg[spky::LANE_COUNT] = { 0.5f, 1.f, 0.5f, 1.f, 1.f };
    g.e.set_targets(tg, 0.5f);
    std::vector<float> both, left;
    for (int t = 0; t < 500; ++t) g.tick(&both);
    for (size_t i = 0; i < both.size(); i += 2) left.push_back(both[i]);
    return left;
}

int rising_crossings(const std::vector<float>& x) {
    int n = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] <= 0.f && x[i] > 0.f) ++n;
    return n;
}

template <class E>
void check_exp2_pitch_stable(const char* name) {
    for (float detune : { 0.f, 0.3f, 0.7f, 1.f }) {
        const std::vector<float> off = render_detuned<E>(detune, 0u);
        const std::vector<float> on  = render_detuned<E>(detune, 1u << spky::probe::kBitLeanExp2);
        REQUIRE(off.size() == on.size());
        double sig = 0.0, worst = 0.0;
        bool finite = true;
        for (size_t i = 0; i < off.size(); ++i) {
            sig += double(on[i]) * on[i];
            worst = std::fmax(worst, std::fabs(double(on[i]) - off[i]));
            finite = finite && std::isfinite(on[i]);
        }
        const int n_off = rising_crossings(off), n_on = rising_crossings(on);
        if (std::getenv("SPKY_V5_PRINT"))
            std::printf("V5 %s detune %.1f: crossings off %d on %d, worst sample diff %.3g\n",
                        name, double(detune), n_off, n_on, worst);
        CAPTURE(name);
        CAPTURE(detune);
        CHECK(finite);
        CHECK(sig > 1.0);          // the deck sounded
        CHECK(n_off > 100);        // and was a pitched tone, so the count is not 0 == 0
        if (detune > 0.f) CHECK(worst > 0.0);   // the bit reached the ratio (at 0 both round alike)
        CHECK(n_on == n_off);      // the same pitch, crossing for crossing
        CHECK(worst < 1e-2);       // a rounding-sized change of the ratio, nothing audible
    }
}

} // namespace

TEST_CASE("V5 bit 25 reaches every SYNTH and WAVE voice's oscillators on the next control tick") {
    check_exp2_reaches_voices<spky::SynthEngine>();
    check_exp2_reaches_voices<spky::WaveEngine>();
}

TEST_CASE("V5 bit 25: SYNTH and WAVE decks keep their pitch at several detunes, drift running") {
    check_exp2_pitch_stable<spky::SynthEngine>("SYNTH");
    check_exp2_pitch_stable<spky::WaveEngine>("WAVE");
}

TEST_CASE("G2 bit 22 reaches the grains, and the deck plays either way") {
    // The same SAMPLER cloud (same seed, so the same spawns) three times: no
    // bit, bit 22, and a bit that is not 22. Only bit 22 changes the window's
    // arithmetic (a different last bit here and there), and only by rounding.
    auto render = [](uint32_t word) {
        SamplerRig g;
        g.e.set_probe_word(word);
        std::vector<float> out;
        for (int i = 0; i < 19200; ++i) {
            float l = 0.f, r = 0.f;
            g.e.process(l, r);
            out.push_back(l);
            out.push_back(r);
        }
        return out;
    };
    const std::vector<float> off   = render(0u);
    const std::vector<float> on    = render(1u << spky::probe::kBitLeanGrainWin);
    const std::vector<float> other = render(1u << spky::probe::kBitLeanSvf1);
    REQUIRE(off.size() == on.size());
    double sig = 0.0, worst = 0.0, worst_other = 0.0;
    bool finite = true;
    for (size_t i = 0; i < off.size(); ++i) {
        sig += double(on[i]) * on[i];
        worst = std::fmax(worst, std::fabs(double(on[i]) - off[i]));
        worst_other = std::fmax(worst_other, std::fabs(double(other[i]) - off[i]));
        finite = finite && std::isfinite(on[i]);
    }
    CHECK(finite);
    CHECK(sig > 1.0);            // the deck sounded
    CHECK(worst > 0.0);          // the bit reached the window
    CHECK(worst < 1e-5);         // and changed it by rounding only
    CHECK(worst_other == 0.0);   // no other bit does
}

TEST_CASE("V6 bit 19: a SYNTH deck through the one-pass filter stays within rounding of the two-pass one") {
    // A FLOW drone at resonance 0.9, its FILTER lane swept over the whole
    // range and back once per 2 s; one deck with bit 19 set from the first
    // tick, one without. The one-pass step is the two-pass filter's two steps
    // folded into one, on the same state (util/svf_lp.h), so what remains is
    // float rounding -- also under the sweep. The bound is loose against that
    // and tight against any real change of the response.
    DeckRig<spky::SynthEngine> two, one;
    for (auto* g : { &two, &one }) {
        g->e.set_flow(true);
        g->e.set_resonance(0.9f);
    }
    one.e.set_probe_word(1u << spky::probe::kBitLeanSvf1);
    std::vector<float> a, b;
    for (int t = 0; t < 1000; ++t) {
        const float filt = 0.5f + 0.5f * std::sin(6.2831853f * t / 1000.f);
        const float tg[spky::LANE_COUNT] = { 0.5f, filt, 0.5f, 0.f, 1.f };
        two.e.set_targets(tg, 0.5f);
        one.e.set_targets(tg, 0.5f);
        two.tick(&a);
        one.tick(&b);
    }
    REQUIRE(a.size() == b.size());
    double sig = 0.0, diff = 0.0;
    bool finite = true;
    for (size_t i = 0; i < a.size(); ++i) {
        sig += double(a[i]) * a[i];
        diff += (double(a[i]) - b[i]) * (double(a[i]) - b[i]);
        finite = finite && std::isfinite(b[i]);
    }
    CHECK(finite);
    CHECK(sig > 1.0);        // the deck sounded
    CHECK(diff > 0.0);       // the bit switched the arithmetic
    CAPTURE(sig);
    CAPTURE(diff);
    CHECK(diff < 1e-8 * sig);   // below -80 dB
}
