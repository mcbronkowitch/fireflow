#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdint>
#include <cstring>
#include "Effects/decimator.h"
#include "Utility/dsp.h"
#include "fx/grit.h"
using namespace spky;

static std::vector<float> sine(int n, float amp = 0.5f) {
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = amp * std::sin(6.2831853f * 220.f * i / 48000.f);
    return v;
}

TEST_CASE("grit: off leaves the signal bit-exact") {
    Grit g;
    g.init(48000.f);
    auto in = sine(2000);
    for (float s : in) {
        float l = s, r = s;
        g.process(l, r);
        CHECK(l == s);
        CHECK(r == s);
    }
    CHECK(!g.engaged());
}

TEST_CASE("grit: drive distorts, reduce decimates, and they differ") {
    auto in = sine(4800);
    auto run = [&](GritMode m) {
        Grit g;
        g.init(48000.f);
        g.set_mode(m);
        g.set_intensity(0.8f);
        g.set_mix(1.f);
        g.set_on(true, true);
        std::vector<float> out;
        out.reserve(in.size());
        for (float s : in) {
            float l = s, r = s;
            g.process(l, r);
            out.push_back(l);
        }
        return out;
    };
    auto drive = run(GritMode::Drive);
    auto reduce = run(GritMode::Reduce);
    int drive_diff = 0, mode_diff = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        if (std::fabs(drive[i] - in[i]) > 1e-4f) ++drive_diff;
        if (std::fabs(drive[i] - reduce[i]) > 1e-4f) ++mode_diff;
        CHECK(std::fabs(drive[i]) <= 1.5f);
        CHECK(std::fabs(reduce[i]) <= 1.5f);
    }
    CHECK(drive_diff > 1000);
    CHECK(mode_diff > 1000);
}

TEST_CASE("grit: mix 0 returns the dry signal") {
    Grit g;
    g.init(48000.f);
    g.set_intensity(0.9f);
    g.set_mix(0.f);
    g.set_on(true, true);
    auto in = sine(2000);
    for (float s : in) {
        float l = s, r = s;
        g.process(l, r);
        CHECK(l == doctest::Approx(s).epsilon(1e-6));
    }
}

TEST_CASE("grit: switching on mid-signal is click-free") {
    // A click-free ramp must not introduce any sample-to-sample jump larger
    // than the effect already produces in steady state. We run the SAME
    // stimulus and params twice: a baseline that is on from sample 0 (no
    // ramp), and a ramped run that switches on mid-buffer. Comparing their
    // max sample-to-sample deltas isolates the switch transient from the
    // effect's own inherent fuzz. A hard switch would jump ~0.3 (the dry/wet
    // difference), far above baseline; the 4 ms Hann ramp must not.
    auto in = sine(9600);

    Grit g_base;
    g_base.init(48000.f);
    g_base.set_intensity(0.7f);
    g_base.set_mix(1.f);
    g_base.set_on(true, true);   // immediate, on from sample 0 — no ramp
    float prev = 0.f, max_delta_baseline = 0.f;
    for (int i = 0; i < (int)in.size(); ++i) {
        float l = in[i], r = in[i];
        g_base.process(l, r);
        if (i > 0) max_delta_baseline = std::max(max_delta_baseline, std::fabs(l - prev));
        prev = l;
    }

    Grit g_ramp;
    g_ramp.init(48000.f);
    g_ramp.set_intensity(0.7f);
    g_ramp.set_mix(1.f);
    prev = 0.f;
    float max_delta_ramped = 0.f;
    for (int i = 0; i < (int)in.size(); ++i) {
        if (i == 4800) g_ramp.set_on(true);   // ramped, not immediate
        float l = in[i], r = in[i];
        g_ramp.process(l, r);
        if (i > 0) max_delta_ramped = std::max(max_delta_ramped, std::fabs(l - prev));
        prev = l;
    }

    CHECK(max_delta_ramped <= max_delta_baseline + 1e-4f);
}

TEST_CASE("GRIT Reduce: an input on one channel reaches the output") {
    // One shared Decimator latched on the R call at some intensities (0.55,
    // 0.8), so an L-only input never reached the wet path at all.
    auto in = sine(48000);   // 220 Hz at 0.5: only the level matters here
    auto rms_db = [](const std::vector<float>& v) {
        double acc = 0.0;
        for (size_t i = v.size() / 2; i < v.size(); ++i) acc += (double)v[i] * v[i];
        return 10.0 * std::log10(acc / (v.size() - v.size() / 2) + 1e-20);
    };
    for (float inten : {0.f, 0.3f, 0.55f, 0.8f, 1.f}) {
        INFO("intensity " << inten);
        double db[2][2];   // [fed side][output channel]
        for (int fed = 0; fed < 2; ++fed) {
            Grit g;
            g.init(48000.f);
            g.set_mode(GritMode::Reduce);
            g.set_intensity(inten);
            g.set_mix(1.f);
            g.set_on(true, true);
            std::vector<float> ol, orr;
            for (float s : in) {
                float l = fed == 0 ? s : 0.f, r = fed == 1 ? s : 0.f;
                g.process(l, r);
                ol.push_back(l);
                orr.push_back(r);
            }
            db[fed][0] = rms_db(ol);
            db[fed][1] = rms_db(orr);
        }
        CHECK(db[0][0] > -30.0);   // L-only: L output
        CHECK(db[1][1] > -30.0);   // R-only: R output
        CHECK(std::fabs(db[0][0] - db[1][1]) < 0.5);
    }
}

TEST_CASE("GRIT Reduce: per-channel state keeps the inherited crush rate") {
    // The shared Decimator counted once per channel call, so it latched
    // twice as often per frame as a per-channel one does: at intensity 0.55
    // the old R output changed 3201 times per second. A per-channel pair
    // must keep that rate, not halve it. A rising ramp under the sine makes
    // every latched value differ; a change event is the first changed
    // sample after a held one (the SampleRateReducer smears each step over
    // a sample or two).
    Grit g;
    g.init(48000.f);
    g.set_mode(GritMode::Reduce);
    g.set_intensity(0.55f);
    g.set_mix(1.f);
    g.set_on(true, true);
    int events = 0;
    float prev = 0.f, prev2 = 0.f;
    for (int n = 0; n < 48000; ++n) {
        float l = 0.5f * std::sin(6.2831853f * 220.f * n / 48000.f) + 1e-4f * n;
        float r = 0.f;
        g.process(l, r);
        if (n >= 2 && l != prev && prev == prev2) ++events;
        prev2 = prev;
        prev = l;
    }
    CHECK(events > 3201 * 0.9);
    CHECK(events < 3201 * 1.1);
}

// ---- GRIT_DECIM_STEP (probe toggle bit 0): the discarded Decimator step ----

namespace {
// xorshift32, mixed before use (small seeds start biased).
struct Noise {
    uint32_t s = 2463534242u;
    float next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float)(int32_t)s * (1.f / 2147483648.f);   // -1..1
    }
};
// What GritReduce::apply() feeds its decimators.
float crush_for(float intensity) {
    return daisysp::fmap(1.f - intensity, 0.5f, 0.7f, daisysp::Mapping::EXP);
}
uint32_t bits_of(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
} // namespace

TEST_CASE("GritDecimator: process() equals daisysp::Decimator::Process bit for bit") {
    for (float inten : {0.f, 0.3f, 0.55f, 0.8f, 1.f}) {
        for (float crush : {crush_for(inten), 0.f, 0.5f, 1.f}) {
            INFO("intensity " << inten << " crush " << crush);
            daisysp::Decimator ref;
            ref.Init();
            ref.SetBitsToCrush(16);
            ref.SetDownsampleFactor(inten);
            ref.SetBitcrushFactor(crush);
            GritDecimator mine;
            mine.init();
            mine.set_downsample_factor(inten);
            mine.set_bitcrush_factor(crush);
            Noise n;
            int mismatches = 0;
            for (int i = 0; i < 5000; ++i) {
                const float x = n.next();
                if (bits_of(ref.Process(x)) != bits_of(mine.process(x))) ++mismatches;
            }
            CHECK(mismatches == 0);
        }
    }
}

TEST_CASE("GritDecimator: step() + process() equals two daisysp Process() calls") {
    for (float inten : {0.f, 0.3f, 0.55f, 0.8f, 1.f}) {
        INFO("intensity " << inten);
        const float crush = crush_for(inten);
        daisysp::Decimator ref;
        ref.Init();
        ref.SetBitsToCrush(16);
        ref.SetDownsampleFactor(inten);
        ref.SetBitcrushFactor(crush);
        GritDecimator mine;
        mine.init();
        mine.set_downsample_factor(inten);
        mine.set_bitcrush_factor(crush);
        Noise n;
        int mismatches = 0;
        for (int i = 0; i < 5000; ++i) {
            const float x = n.next();
            ref.Process(x);
            const float want = ref.Process(x);
            mine.step(x);
            if (bits_of(want) != bits_of(mine.process(x))) ++mismatches;
        }
        CHECK(mismatches == 0);
    }
}

TEST_CASE("GritReduce: output equals the daisysp-built reference (two full Process() per frame)") {
    // The reference is GritReduce as it was before the discarded step went
    // state-only: two daisysp::Decimator per channel, each Process()-ed twice
    // per frame with the same input, the same SampleRateReducer pair and
    // XFade mix.
    struct Setting { float intensity, mix; };
    for (Setting st : {Setting{0.f, 1.f}, Setting{0.3f, 0.7f}, Setting{0.55f, 1.f},
                       Setting{0.8f, 0.5f}, Setting{1.f, 1.f}}) {
        INFO("intensity " << st.intensity << " mix " << st.mix);
        GritReduce g;
        g.init(48000.f);
        g.set_intensity(st.intensity);
        g.set_mix(st.mix);

        daisysp::Decimator dec[2];
        daisysp::SampleRateReducer red[2];
        XFade mix;
        for (int c = 0; c < 2; ++c) {
            dec[c].Init();
            dec[c].SetBitsToCrush(16);
            red[c].Init();
            red[c].SetFreq(0.6f);
            dec[c].SetDownsampleFactor(st.intensity);
            dec[c].SetBitcrushFactor(crush_for(st.intensity));
        }
        mix.SetStage(st.mix);

        Noise n;
        int mismatches = 0;
        double acc = 0.0;
        for (int i = 0; i < 6000; ++i) {
            float l = n.next(), r = n.next();
            float rl = l, rr = r;
            dec[0].Process(l);
            const float c0 = dec[0].Process(l);
            red[0].Process(c0);
            const float r0 = red[0].Process(c0);
            dec[1].Process(r);
            const float c1 = dec[1].Process(r);
            red[1].Process(c1);
            const float r1 = red[1].Process(c1);
            mix.Process(rl, rr, r0, r1, rl, rr);

            g.process(l, r);
            if (bits_of(l) != bits_of(rl)) ++mismatches;
            if (bits_of(r) != bits_of(rr)) ++mismatches;
            acc += (double)l * l + (double)r * r;
        }
        CHECK(mismatches == 0);
        // The render is not silence: a test on zeros proves nothing.
        CHECK(acc > 1.0);
    }
}
