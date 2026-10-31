// CPU round 2, Phase A (docs/superpowers/specs/2026-10-30-cpu-engine-kernels-
// design.md section 5.1): the SAMPLER split. Built into spky_probe_tests, the
// one desktop target compiled with SPKY_PROBE_MARKS=1 and SPKY_PROBE_TOGGLES=1
// as the measurement image is; the default build has neither, so these cases
// could not fail there. The stamp clock is test_probe_marks.cpp's
// (spky::probe_clock); these cases count, they do not time.
// Also the toggle bits' plumbing for the round's candidates (bit 19 on, bit
// 21 on), whose two paths can only both be compiled here.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
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

namespace {

constexpr float kG4Lsb = 1.f / 32768.f;

float g4_stored(float x) {
    const float top = 32767.f / 32768.f;
    return x > top ? top : (x < -1.f ? -1.f : x);
}

// Every frame of the arena through the buffer's own reader: finite, and the
// content within one int16 step of `want` (after saturation), the rest zero.
void g4_check(const spky::SampleBuffer& b, const std::vector<float>& want_l,
              const std::vector<float>& want_r, bool saturated) {
    bool finite = true;
    float worst = 0.f, tail = 0.f;
    for (size_t i = 0; i < b.capacity(); ++i) {
        float l = 9.f, r = 9.f;
        b.frame_at(i, l, r);
        finite = finite && std::isfinite(l) && std::isfinite(r);
        if (i < want_l.size()) {
            const float wl = saturated ? g4_stored(want_l[i]) : want_l[i];
            const float wr = saturated ? g4_stored(want_r[i]) : want_r[i];
            worst = std::fmax(worst, std::fmax(std::fabs(l - wl), std::fabs(r - wr)));
        } else {
            tail = std::fmax(tail, std::fmax(std::fabs(l), std::fabs(r)));
        }
    }
    CHECK(finite);
    CHECK(worst <= kG4Lsb);
    CHECK(tail == 0.f);
}

} // namespace

TEST_CASE("G4 bit 21: the toggle flip converts the buffer both ways at the same length") {
    // An arena as the board's SDRAM comes up: not zeroed (NaN here), with
    // content over the first 3 000 frames, some of it past full scale.
    constexpr size_t kCap = 8192, kN = 3000;
    const uint32_t bit = 1u << spky::probe::kBitLeanSamplerI16;
    std::vector<spky::SampleBuffer::Frame> mem(
        kCap, spky::SampleBuffer::Frame{ std::numeric_limits<float>::quiet_NaN(),
                                         std::numeric_limits<float>::quiet_NaN() });
    spky::SampleBuffer b;
    b.init(mem.data(), kCap, 48000.f);
    REQUIRE_FALSE(b.storage_i16());
    std::vector<float> l(kN), r(kN);
    for (size_t i = 0; i < kN; ++i) {
        l[i] = 1.3f * std::sin(0.0137f * float(i));
        r[i] = 0.7f * std::cos(0.0291f * float(i));
        b.set_frame(i, l[i], r[i]);
    }
    b.set_rec_size(kN);

    // On: int16, the length kept, the content within a step, saturated.
    b.set_probe_word(bit);
    CHECK(b.storage_i16());
    CHECK(b.rec_size() == kN);
    CHECK(b.capacity() == kCap);
    g4_check(b, l, r, true);
    // The same word again (every block hands it down) converts nothing.
    b.set_probe_word(bit | (1u << spky::probe::kBitLeanSvf1));
    CHECK(b.storage_i16());
    g4_check(b, l, r, true);

    // Off: float again, the same length and content; the arena holds plain
    // floats once more, so the float reader and the arena agree bit for bit.
    b.set_probe_word(1u << spky::probe::kBitLeanSvf1);
    CHECK_FALSE(b.storage_i16());
    CHECK(b.rec_size() == kN);
    CHECK(b.capacity() == kCap);
    g4_check(b, l, r, true);
    for (size_t i = 0; i < kCap; i += 97) {
        float fl = 9.f, fr = 9.f;
        b.frame_at(i, fl, fr);
        CHECK(fl == mem[i].l);
        CHECK(fr == mem[i].r);
    }

    // And on again from the float side, after a round trip.
    b.set_probe_word(bit);
    CHECK(b.storage_i16());
    g4_check(b, l, r, true);
}

TEST_CASE("G4 bit 21 reaches the sampler's buffer, and the deck plays either way") {
    SamplerRig g;
    const uint32_t bit = 1u << spky::probe::kBitLeanSamplerI16;
    auto play = [&g]() {
        bool finite = true;
        double sum = 0.0;
        for (int i = 0; i < 9600; ++i) {
            float l = 0.f, r = 0.f;
            g.e.process(l, r);
            finite = finite && std::isfinite(l) && std::isfinite(r);
            sum += double(l) * l + double(r) * r;
        }
        CHECK(finite);
        CHECK(sum > 1.0);
    };
    play();
    const size_t n = g.e.rec_size();
    REQUIRE(n == 24000);
    g.e.set_probe_word(bit);
    // The arena's first bytes are int16 pairs now: frame 100 of the sine.
    int16_t q[2];
    std::memcpy(q, reinterpret_cast<const unsigned char*>(g.mem.data()) + 4 * 100, sizeof q);
    const float s100 = std::sin(6.2831853f * 441.f * 100.f / 48000.f);
    CHECK(std::fabs(float(q[0]) / 32768.f - s100) <= kG4Lsb);
    CHECK(g.e.rec_size() == n);
    play();
    g.e.set_probe_word(0u);
    CHECK(g.e.rec_size() == n);
    CHECK(std::fabs(g.mem[100].l - s100) <= kG4Lsb);
    play();
}
