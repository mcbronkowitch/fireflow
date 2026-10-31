// CPU round 2, candidate G4 (class 2, spec 2026-10-30-cpu-engine-kernels-
// design.md section 6): the sampler's record buffer holds int16 stereo frames
// (4 B) instead of float ones (8 B), scaled 1/32 768 on read and saturated on
// write, in the same arena and at the same length in frames. Built into
// spky_lean_tests only, the desktop target compiled with
// SPKY_LEAN_SAMPLER_I16=1 as a SHELL_LEAN_SAMPLER_I16=1 firmware is; the
// default build stores floats and has none of the calls below.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include "sampler/sample_buffer.h"
#include "sampler/sampler_config.h"
#include "sampler/sampler_engine.h"

using namespace spky;

namespace {

constexpr float kLsb = 1.f / 32768.f;
constexpr float kTop = 32767.f / 32768.f;   // the largest stored value

// Deterministic noise in [-1, 1).
struct Lcg {
    uint32_t s = 0x2545F491u;
    float next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(s)) * (1.f / 2147483648.f);
    }
};

float clamp_store(float x) { return x > kTop ? kTop : (x < -1.f ? -1.f : x); }

} // namespace

TEST_CASE("G4: round trip within 1 LSB") {
    constexpr size_t kCap = 4096;
    std::vector<SampleBuffer::Frame> mem(kCap);
    SampleBuffer b;
    b.init(mem.data(), kCap, 48000.f);

    // Every value of [-1, 1] on a fine grid, and noise: stored and read back
    // through the converter, each within one step of what went in.
    float worst = 0.f;
    for (int k = -40000; k <= 40000; ++k) {
        const float x = static_cast<float>(k) / 40000.f;
        b.set_frame(7, x, -x);
        float l = 9.f, r = 9.f;
        b.frame_at(7, l, r);
        worst = std::fmax(worst, std::fabs(l - x));
        worst = std::fmax(worst, std::fabs(r + x));
    }
    CHECK(worst <= kLsb);

    // Out of range saturates at the int16 ends, non-finite stores silence.
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float outs[] = { 1.f, 1.5f, 7.f, 1e6f, -1.f, -1.5f, -7.f, -1e6f, inf, -inf };
    for (float x : outs) {
        b.set_frame(9, x, -x);
        float l = 9.f, r = 9.f;
        b.frame_at(9, l, r);
        CAPTURE(x);
        CHECK(l == clamp_store(x));
        CHECK(r == clamp_store(-x));
    }
    b.set_frame(9, nan, nan);
    float nl = 9.f, nr = 9.f;
    b.frame_at(9, nl, nr);
    CHECK(nl == 0.f);
    CHECK(nr == 0.f);

    // A take through write(): once the fade-in is through, the content is
    // the input within one step (fresh recording: nothing to feed back).
    SampleBuffer w;
    std::vector<SampleBuffer::Frame> wmem(kCap);
    w.init(wmem.data(), kCap, 48000.f);
    Lcg nz;
    std::vector<float> in_l(3000), in_r(3000);
    w.set_recording(true);
    for (size_t i = 0; i < in_l.size(); ++i) {
        in_l[i] = 0.9f * nz.next();
        in_r[i] = 0.9f * nz.next();
        w.write(in_l[i], in_r[i]);
    }
    REQUIRE(w.rec_size() == in_l.size());
    float worst_w = 0.f;
    for (size_t i = sampler_cfg::kRecordFade; i < in_l.size(); ++i) {
        float l = 9.f, r = 9.f;
        w.frame_at(i, l, r);
        worst_w = std::fmax(worst_w, std::fabs(l - in_l[i]));
        worst_w = std::fmax(worst_w, std::fabs(r - in_r[i]));
    }
    CHECK(worst_w <= kLsb);

    // read_linear between stored frames: within one step of the float
    // interpolation of the values that went in.
    SampleBuffer g;
    std::vector<SampleBuffer::Frame> gmem(kCap);
    g.init(gmem.data(), kCap, 48000.f);
    std::vector<float> src(kCap);
    for (size_t i = 0; i < kCap; ++i) {
        src[i] = 0.95f * nz.next();
        g.set_frame(i, src[i], -src[i]);
    }
    g.set_rec_size(kCap);
    float worst_i = 0.f;
    for (float frame = 0.f; frame < float(kCap - 1); frame += 0.173f) {
        const size_t i0 = static_cast<size_t>(frame);
        const float  fr = frame - static_cast<float>(i0);
        const float  want = src[i0] + fr * (src[i0 + 1] - src[i0]);
        float l = 9.f, r = 9.f;
        g.read_linear(frame, l, r);
        worst_i = std::fmax(worst_i, std::fabs(l - want));
        worst_i = std::fmax(worst_i, std::fabs(r + want));
    }
    CHECK(worst_i <= kLsb);
}

TEST_CASE("G4: overdub at feedback 1 stays bounded") {
    // The by-ear rule stays as it is: the overdub saturates its read-back
    // above this coefficient, and G4 does not move it.
    CHECK(sampler_cfg::kFbSatKnee == 0.90f);

    constexpr size_t kCap = 48000;
    std::vector<SampleBuffer::Frame> mem(kCap);
    SampleBuffer b;
    b.init(mem.data(), kCap, 48000.f);

    // A 0.5 s take of a 0.5 sine, stopped: the fade-out cuts the loop.
    const float w = 6.2831853f * 220.f / 48000.f;
    b.set_recording(true);
    for (int i = 0; i < 24000; ++i) b.write(0.5f * std::sin(w * i), 0.5f * std::cos(w * i));
    b.set_recording(false);
    for (int i = 0; i < 400; ++i) b.write(0.f, 0.f);
    REQUIRE_FALSE(b.is_recording());
    const size_t loop = b.rec_size();
    REQUIRE(loop > 20000);

    // Feedback at unity: above the knee, -6 dB + t (kFbMaxDb + 6 dB) = 0 dB.
    const float t = 6.f / (sampler_cfg::kFbMaxDb + 6.f);
    b.set_feedback(sampler_cfg::kFbKnee + t * (1.f - sampler_cfg::kFbKnee));
    CHECK(b.feedback() == doctest::Approx(1.f).epsilon(1e-4));
    CHECK(b.feedback() > sampler_cfg::kFbSatKnee);

    // 60 s of overdub with the same sine on top, the loop read back every
    // 2 s: always finite, still sounding, and held at the int16 ends, never
    // wrapped round them: a wrap is a jump of
    // about 2 between neighbours. The overdub leaves steps of its own -- at
    // the write head (the frames behind it have one pass more), and at the
    // tone's zero crossings, where x = tanh(x) + in converges slowly at
    // unity feedback and a frame keeps the sign of its history (0.57 seen
    // here) -- so the bound sits at 1, between the two.
    b.set_recording(true);
    REQUIRE(b.is_overdubbing());
    bool finite = true;
    float peak = 0.f, last_peak = 0.f, jump = 0.f;
    size_t jump_at = 0;
    int jump_s = 0;
    for (int s = 0; s < 60 * 48000; ++s) {
        b.write(0.5f * std::sin(w * s), 0.5f * std::cos(w * s));
        if ((s + 1) % (2 * 48000) == 0) {
            last_peak = 0.f;
            float pl = 0.f, pr = 0.f;
            for (size_t i = 0; i < loop; ++i) {
                float l = 0.f, r = 0.f;
                b.frame_at(i, l, r);
                finite = finite && std::isfinite(l) && std::isfinite(r);
                last_peak = std::fmax(last_peak, std::fmax(std::fabs(l), std::fabs(r)));
                const float d = std::fmax(std::fabs(l - pl), std::fabs(r - pr));
                if (i > 0 && i != b.write_head() && d > jump) { jump = d; jump_at = i; jump_s = s; }
                pl = l;
                pr = r;
            }
            peak = std::fmax(peak, last_peak);
        }
    }
    CAPTURE(peak);
    CAPTURE(last_peak);
    CAPTURE(jump);
    CAPTURE(jump_at);
    CAPTURE(jump_s);
    CHECK(b.rec_size() == loop);
    CHECK(finite);
    CHECK(peak <= 1.f);
    CHECK(last_peak > 0.5f);
    CHECK(jump < 1.f);
}

TEST_CASE("G4: load and save keep the length") {
    constexpr size_t kCap = 48000;
    std::vector<SampleBuffer::Frame> mem(kCap);
    SamplerEngine e;
    e.set_seed(7);
    e.set_memory(mem.data(), kCap);
    e.init(48000.f);

    Lcg nz;
    const size_t lens[] = { 1, 3, 1000, 30001, kCap, kCap + 5000 };
    for (size_t n : lens) {
        CAPTURE(n);
        std::vector<float> l(n), r(n);
        for (size_t i = 0; i < n; ++i) { l[i] = 0.99f * nz.next(); r[i] = 0.99f * nz.next(); }
        // Stereo, then mono (r normals to l).
        for (int mono = 0; mono < 2; ++mono) {
            e.load_sample(l.data(), mono ? nullptr : r.data(), n);
            const size_t want = n > kCap ? kCap : n;
            CHECK(e.rec_size() == want);
            // The save path reads the content back through the converter:
            // rec_size() frames, each the loaded one within one step.
            float worst = 0.f;
            size_t saved = 0;
            for (size_t i = 0; i < e.rec_size(); ++i) {
                float sl = 9.f, sr = 9.f;
                e.sample_frame(i, sl, sr);
                worst = std::fmax(worst, std::fabs(sl - l[i]));
                worst = std::fmax(worst, std::fabs(sr - (mono ? l[i] : r[i])));
                ++saved;
            }
            CHECK(saved == want);
            CHECK(worst <= kLsb);
        }
    }

    // A shorter load after a full one, then a clear.
    std::vector<float> one(500, 0.25f);
    e.load_sample(one.data(), one.data(), one.size());
    CHECK(e.rec_size() == 500);
    e.clear();
    CHECK(e.rec_size() == 0);
}
