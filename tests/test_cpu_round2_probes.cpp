// CPU round 2, Phase A (docs/superpowers/specs/2026-10-30-cpu-engine-kernels-
// design.md section 5.1): the SAMPLER split. Built into spky_probe_tests, the
// one desktop target compiled with SPKY_PROBE_MARKS=1 and SPKY_PROBE_TOGGLES=1
// as the measurement image is; the default build has neither, so these cases
// could not fail there. The stamp clock is test_probe_marks.cpp's
// (spky::probe_clock); these cases count, they do not time.
// Also the toggle bits' plumbing for the class-1 candidates (bit 17 on), whose
// two paths can only both be compiled here.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <cstring>
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

TEST_CASE("V1 bit 17 reaches every SYNTH voice's oscillators on the next control tick") {
    spky::SynthEngine e;
    e.set_seed(99);
    e.init(48000.f);
    const float t[spky::LANE_COUNT] = { 0.5f, 1.f, 0.5f, 0.f, 1.f };
    e.set_targets(t, 0.5f);
    auto tick = [&] {
        for (int i = 0; i < 96; ++i) { float l = 0.f, r = 0.f; e.process(l, r); }
    };
    tick();
    for (int v = 0; v < spky::SynthEngine::kVoices; ++v)
        CHECK_FALSE(e.probe_voice(v).probe_morph_inv());
    e.set_probe_word(1u << spky::probe::kBitMorphInv);
    tick();
    for (int v = 0; v < spky::SynthEngine::kVoices; ++v)
        CHECK(e.probe_voice(v).probe_morph_inv());
    e.set_probe_word(~(1u << spky::probe::kBitMorphInv));   // every other bit
    tick();
    for (int v = 0; v < spky::SynthEngine::kVoices; ++v)
        CHECK_FALSE(e.probe_voice(v).probe_morph_inv());
}

TEST_CASE("V1 bit 17: MorphOsc's two paths agree bit for bit in the measurement build") {
    // The default build has the new path only (tests/test_cpu_round2_equiv.cpp
    // holds it against a copy of the old one); here both are compiled, and
    // the bit picks. Morph and frequency move on every tick.
    spky::MorphOsc a, b;
    a.init(48000.f);
    b.init(48000.f);
    a.set_probe_inv(false);
    b.set_probe_inv(true);
    uint32_t s = 2463534242u;
    auto next = [&s] {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float)(int32_t)s * (1.f / 2147483648.f);
    };
    int bad = 0;
    for (int i = 0; i < 96 * 1000; ++i) {
        if (i % 96 == 0) {
            const float m = 0.5f + 0.6f * next();
            const float f = 4000.f * (1.f + next());
            const float ct = 30.f * next();
            a.set_morph(m);  b.set_morph(m);
            a.set_detune_cents(ct); b.set_detune_cents(ct);
            a.set_freq(f);   b.set_freq(f);
        }
        const float x = a.process(), y = b.process();
        if (std::memcmp(&x, &y, 4) != 0) ++bad;
    }
    CHECK(bad == 0);
}
