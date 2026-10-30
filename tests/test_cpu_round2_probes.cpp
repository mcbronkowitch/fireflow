// CPU round 2, Phase A (docs/superpowers/specs/2026-10-30-cpu-engine-kernels-
// design.md section 5.1): the SAMPLER split and the voice / grain ablation
// bits. Built into spky_probe_tests, the one desktop target compiled with
// SPKY_PROBE_MARKS=1 and SPKY_PROBE_TOGGLES=1 as the measurement image is;
// the default build has neither, so these cases could not fail there. The
// stamp clock is test_probe_marks.cpp's (spky::probe_clock); these cases
// count, they do not time.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include "sampler/sampler_engine.h"
#include "synth/synth_engine.h"
#include "util/probe_toggles.h"

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

std::vector<float> render(spky::IPartEngine& e, int n) {
    std::vector<float> out(2 * n);
    for (int i = 0; i < n; ++i) e.process(out[2 * i], out[2 * i + 1]);
    return out;
}

// A WAVE engine with all four voices struck (a four-note chord), so every
// voice stage runs.
void strike(spky::WaveEngine& e) {
    e.init(48000.f);
    const float chord[4] = { 0.2f, 0.35f, 0.5f, 0.7f };
    e.trigger_chord(chord, 4);
}

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

TEST_CASE("ablation bits change only their stage") {
    // WAVE, bit 12 (ABL_SVF): the voices run unfiltered, so the output moves.
    // Word 0 is bit-identical to an engine that never got a word.
    spky::WaveEngine never, zero, svf;
    strike(never);
    strike(zero);
    strike(svf);
    zero.set_probe_word(0u);
    svf.set_probe_word(1u << spky::probe::kBitAblSvf);
    CHECK(spky::probe::kBitAblSvf == 12);
    const auto a = render(never, 4800);
    const auto b = render(zero, 4800);
    const auto c = render(svf, 4800);
    CHECK(a == b);
    CHECK(a != c);

    // The other voice bits move a WAVE voice's output too.
    for (int bit : { spky::probe::kBitAblOscB, spky::probe::kBitAblSub,
                     spky::probe::kBitAblEnvPan }) {
        CAPTURE(bit);
        spky::WaveEngine x;
        strike(x);
        x.set_probe_word(1u << bit);
        CHECK(render(x, 4800) != a);
    }

    // The grain bits: word 0 equals no word; the window and the DTCM read
    // move the cloud. (The fold bit leaves a read inside the content as it
    // was: only a position outside it is clamped instead of folded.)
    SamplerRig sn, s0;
    s0.e.set_probe_word(0u);
    const auto sa = render(sn.e, 9600);
    CHECK(render(s0.e, 9600) == sa);
    for (int bit : { spky::probe::kBitAblGrainWin, spky::probe::kBitAblGrainSram }) {
        CAPTURE(bit);
        SamplerRig sx;
        sx.e.set_probe_word(1u << bit);
        CHECK(render(sx.e, 9600) != sa);
    }
    CHECK(spky::probe::kBitAblOscB == 10);
    CHECK(spky::probe::kBitAblGrainSram == 16);
}
