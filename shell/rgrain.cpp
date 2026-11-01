#include "rgrain.h"

#include <cmath>
#include <cstddef>

#include "hw/board.h"   // daisy_core.h, and with it DSY_SDRAM_BSS
#include "sampler/grain.h"
#include "sampler/sample_buffer.h"

namespace shell {

namespace {

constexpr float  kRate      = 48000.f;
constexpr size_t kFrames    = 2u * 48000u;   // 2 s
constexpr int    kGrainLen  = 4800;          // 100 ms
constexpr int    kGrainEdge = kGrainLen / 4; // attack and decay, a quarter each
constexpr double kSweepLo   = 100.0;
constexpr double kSweepHi   = 4000.0;
// Sweep and noise peak at 0.25 together, -12 dBFS.
constexpr float  kSweepAmp  = 0.20f;
constexpr float  kNoiseAmp  = 0.05f;

// The sample in SDRAM, where the sampler's own recordings live (sdram_mem.cpp):
// 768 KB, NOLOAD, so rgrain_init() writes every frame before anything reads it.
spky::SampleBuffer::Frame DSY_SDRAM_BSS g_frames[kFrames];
// The buffer object and the grains are plain globals in AXI SRAM, like the
// sampler's: only the frames go to SDRAM (sdram_mem.cpp says why a constructor
// must not run there).
spky::SampleBuffer g_buf;
spky::Grain        g_grains[kRgrainGrains];

uint32_t g_rng = 0;

inline uint32_t xorshift32(uint32_t& x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

// [0, 1), from the top 24 bits.
inline float unit(uint32_t& x)
{
    return static_cast<float>(xorshift32(x) >> 8) * (1.f / 16777216.f);
}

// A random start that leaves the whole grain inside the sample, so no grain
// reads across the seam between the sweep's end and its start.
void spawn(spky::Grain& g)
{
    constexpr float kSpan = static_cast<float>(kFrames - kGrainLen);
    g.spawn(unit(g_rng) * kSpan, 1.f, 0.f, kGrainLen, kGrainEdge, kGrainEdge, false);
}

} // namespace

void rgrain_init()
{
    // A well-mixed seed and a few draws first: a small seed biases a xorshift's
    // cold start (memory: probe seeds must be mixed). Fixed, so every boot
    // writes the same sample and spawns the same starts.
    g_rng = 0x9E3779B9u;
    for(int i = 0; i < 16; ++i)
        xorshift32(g_rng);

    // The exponential sweep, one frequency step per frame; double for the
    // step, whose float rounding would compound over 96 000 frames.
    const double step  = std::pow(kSweepHi / kSweepLo, 1.0 / static_cast<double>(kFrames));
    double       f     = kSweepLo;
    float        phase = 0.f;
    constexpr float kTwoPi = 6.28318530718f;
    for(size_t i = 0; i < kFrames; ++i)
    {
        const float s = kSweepAmp * std::sin(phase);
        g_frames[i].l = s + kNoiseAmp * (2.f * unit(g_rng) - 1.f);
        g_frames[i].r = s + kNoiseAmp * (2.f * unit(g_rng) - 1.f);
        phase += kTwoPi * static_cast<float>(f / kRate);
        if(phase >= kTwoPi) phase -= kTwoPi;
        f *= step;
    }
    g_buf.init(g_frames, kFrames, kRate);
    g_buf.set_rec_size(kFrames);

    // Staggered by an eighth of a grain each, so the grains do not all end and
    // respawn in the same block.
    for(int k = 0; k < kRgrainGrains; ++k)
    {
        spawn(g_grains[k]);
        float l, r;
        for(int i = 0; i < k * kGrainLen / kRgrainGrains; ++i)
            g_grains[k].process(g_buf, l, r);
    }
}

void rgrain_block(uint8_t n, float* l, float* r, uint32_t frames)
{
    if(n > kRgrainGrains) n = kRgrainGrains;
    // Sample by sample over the grains, the order SamplerEngine::process runs
    // its cloud in.
    for(uint32_t i = 0; i < frames; ++i)
    {
        float sl = 0.f, sr = 0.f;
        for(uint8_t k = 0; k < n; ++k)
        {
            spky::Grain& g = g_grains[k];
            if(!g.active()) spawn(g);
            float gl, gr;
            g.process(g_buf, gl, gr);
            sl += gl;
            sr += gr;
        }
        l[i] = sl;
        r[i] = sr;
    }
}

} // namespace shell
