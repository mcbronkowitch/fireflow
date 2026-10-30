// CPU round 2, Phase A (docs/superpowers/specs/2026-10-30-cpu-engine-kernels-
// design.md section 5.1): the SAMPLER split. Built into spky_probe_tests, the
// one desktop target compiled with SPKY_PROBE_MARKS=1 and SPKY_PROBE_TOGGLES=1
// as the measurement image is; the default build has neither, so these cases
// could not fail there. The stamp clock is test_probe_marks.cpp's
// (spky::probe_clock); these cases count, they do not time.
// Also the toggle bits' plumbing for the class-1 candidates (bit 18 on), whose
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

TEST_CASE("V3 bit 18 reaches every SYNTH and WAVE voice on the next control tick") {
    auto run = [](auto& e) {
        e.set_seed(99);
        e.init(48000.f);
        const float t[spky::LANE_COUNT] = { 0.5f, 1.f, 0.5f, 0.f, 1.f };
        e.set_targets(t, 0.5f);
        auto tick = [&] {
            for (int i = 0; i < 96; ++i) { float l = 0.f, r = 0.f; e.process(l, r); }
        };
        tick();
        for (int v = 0; v < e.kVoices; ++v)
            CHECK_FALSE(e.probe_voice(v).probe_sin_wrapped());
        e.set_probe_word(1u << spky::probe::kBitSinWrapped);
        tick();
        for (int v = 0; v < e.kVoices; ++v)
            CHECK(e.probe_voice(v).probe_sin_wrapped());
        e.set_probe_word(~(1u << spky::probe::kBitSinWrapped));   // every other bit
        tick();
        for (int v = 0; v < e.kVoices; ++v)
            CHECK_FALSE(e.probe_voice(v).probe_sin_wrapped());
    };
    spky::SynthEngine s;
    run(s);
    spky::WaveEngine w;
    run(w);
}

namespace {

// Two engines of the same kind, one with bit 18 set, driven alike for 400
// control ticks with the sub at 1: morph in the sine segment (SYNTH), drift
// running (MOTION), a note re-struck every 100 ticks. Returns how many
// output samples differ in their bits, and how loud the run was.
template <class E>
int sinwrapped_mismatches(float timbre, float* peak) {
    E a, b;
    a.set_seed(7); b.set_seed(7);
    a.init(48000.f); b.init(48000.f);
    a.set_sub(1.f); b.set_sub(1.f);
    a.set_probe_word(0u);
    b.set_probe_word(1u << spky::probe::kBitSinWrapped);
    const float t[spky::LANE_COUNT] = { timbre, 0.9f, 0.5f, 0.6f, 1.f };
    a.set_targets(t, 0.5f); b.set_targets(t, 0.5f);
    int bad = 0;
    float pk = 0.f;
    for (int i = 0; i < 96 * 400; ++i) {
        if (i % (96 * 100) == 0) {
            const float pitch = 0.2f + 0.2f * float(i / (96 * 100));
            a.trigger(pitch); b.trigger(pitch);
        }
        float al = 0.f, ar = 0.f, bl = 0.f, br = 0.f;
        a.process(al, ar); b.process(bl, br);
        if (std::memcmp(&al, &bl, 4) != 0) ++bad;
        if (std::memcmp(&ar, &br, 4) != 0) ++bad;
        pk = std::fmax(pk, std::fabs(al));
    }
    *peak = pk;
    return bad;
}

} // namespace

TEST_CASE("V3 bit 18: voice output bit-identical on the sub-heavy setting") {
    // The measurement build has both paths; the default build has the new one
    // only (tests/test_cpu_round2_equiv.cpp holds fast_sin_wrapped against the
    // old arithmetic). Morph 0.2 and 0.0 sit in MorphOsc's sine segment, 0.8
    // outside it.
    const float timbres[] = { 0.f, 0.2f, 0.8f };
    for (float tm : timbres) {
        CAPTURE(tm);
        float pk = 0.f;
        CHECK(sinwrapped_mismatches<spky::SynthEngine>(tm, &pk) == 0);
        CHECK(pk > 0.01f);                                   // the run was not silent
        CHECK(sinwrapped_mismatches<spky::WaveEngine>(tm, &pk) == 0);
        CHECK(pk > 0.01f);
    }
}
