#include "doctest/doctest.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "synth/wt_osc.h"

using namespace spky;

static int crossings(WtOsc& o, int n) {
    int count = 0;
    float prev = o.process();
    for (int i = 1; i < n; ++i) {
        const float cur = o.process();
        if (prev <= 0.f && cur > 0.f) ++count;
        prev = cur;
    }
    return count;
}

TEST_CASE("wt_osc: matches MorphOsc frequency and cents contract") {
    WtOsc o;
    o.init(48000.f);
    o.set_freq(220.f);
    CHECK(crossings(o, 48000) == doctest::Approx(220).epsilon(0.01));
    o.reset_phase();
    o.set_detune_cents(1200.f);
    CHECK(crossings(o, 48000) == doctest::Approx(440).epsilon(0.01));
}

TEST_CASE("wt_osc: detuned frequency cannot exceed the Nyquist safety cap") {
    WtOsc capped;
    capped.init(48000.f);
    capped.set_freq(21600.f);

    WtOsc detuned;
    detuned.init(48000.f);
    detuned.set_freq(21600.f);
    detuned.set_detune_cents(2400.f);

    for (int i = 0; i < 2; ++i) {
        CHECK(detuned.process() == capped.process());
    }
}

TEST_CASE("wt_osc: position clamps and reaches its target in 96 samples") {
    WtOsc o;
    o.init(48000.f);
    o.set_morph(-1.f);
    CHECK(o.target_position() == 0.f);
    o.set_morph(2.f);
    CHECK(o.target_position() == 15.f);
    for (int i = 0; i < 95; ++i) o.process();
    CHECK(o.position() < 15.f);
    o.process();
    CHECK(o.position() == doctest::Approx(15.f).epsilon(1e-6));
}

TEST_CASE("wt_osc: adjacent-frame sweep adds no control-boundary discontinuity") {
    WtOsc o;
    o.init(48000.f);
    o.set_freq(110.f);
    float max_boundary_residual = 0.f;
    for (int block = 0; block <= 150; ++block) {
        // The held oscillator advances the same rich waveform phase but does not
        // receive the new position target.  Their first-sample residual therefore
        // isolates the control-boundary scan change from legitimate waveform slope.
        WtOsc held = o;
        o.set_morph(block / 150.f);
        max_boundary_residual = std::max(max_boundary_residual,
            std::fabs(o.process() - held.process()));
        for (int i = 1; i < WtOsc::kRampSamples; ++i) o.process();
    }

    // Each update advances a 0.1-frame target by 1/96 of its distance.  The
    // 0.01 bound leaves ample room above that bounded scan residual while still
    // rejecting an unsmoothed 0.1-frame target jump.
    CHECK(max_boundary_residual < 0.01f);
}

TEST_CASE("wt_osc: mip changes crossfade for one control block") {
    WtOsc o;
    o.init(48000.f);
    o.set_freq(90.f);
    const int before = o.mip_level();
    o.set_freq(190.f);
    CHECK(o.mip_level() > before);
    CHECK(o.mip_crossfading());
    for (int i = 0; i < 95; ++i) o.process();
    CHECK(o.mip_crossfading());
    o.process();
    CHECK_FALSE(o.mip_crossfading());
}

TEST_CASE("wt_osc: retargeting an active mip fade preserves the audible blend") {
    float max_retarget_residual = 0.f;
    for (int phase_step = 0; phase_step < 32; ++phase_step) {
        WtOsc retargeted;
        retargeted.init(48000.f);
        retargeted.set_freq(90.f);
        for (int i = 0; i < WtOsc::kRampSamples; ++i) retargeted.process();
        retargeted.set_morph(13.f / 15.f);
        for (int i = 0; i < WtOsc::kRampSamples; ++i) retargeted.process();

        retargeted.set_freq(190.f);
        for (int i = 0; i < WtOsc::kRampSamples / 2; ++i) retargeted.process();
        WtOsc uninterrupted = retargeted;

        retargeted.set_freq(90.f);
        CHECK(retargeted.mip_crossfading());
        const float output_phase = (phase_step + 0.5f) / 32.f;
        retargeted.reset_phase(output_phase - 90.f / 48000.f);
        uninterrupted.reset_phase(output_phase - 190.f / 48000.f);
        max_retarget_residual = std::max(max_retarget_residual,
            std::fabs(retargeted.process() - uninterrupted.process()));
        for (int i = 0; i < WtOsc::kRampSamples - 1; ++i) retargeted.process();
        CHECK_FALSE(retargeted.mip_crossfading());
    }
    CHECK(max_retarget_residual < 0.01f);
}

static float read_frame_mip_at_phase(int frame, int mip, float phase) {
    const int length = wt::kMipLength[mip];
    const float index = phase * length;
    const int index0 = static_cast<int>(index);
    const int index1 = index0 + 1 < length ? index0 + 1 : 0;
    const float blend = index - index0;
    const int16_t* const samples = wt::table(frame, mip);
    return (samples[index0] + (samples[index1] - samples[index0]) * blend) / 32112.f;
}

TEST_CASE("wt_osc: multiple active retargets preserve the weighted mip source") {
    constexpr float sample_rate = 48000.f;
    constexpr float output_phase = 0.375f;
    WtOsc o;
    o.init(sample_rate);
    o.set_freq(90.f);  // mip 0
    for (int i = 0; i < WtOsc::kRampSamples; ++i) o.process();
    o.set_morph(13.f / 15.f);
    for (int i = 0; i < WtOsc::kRampSamples; ++i) o.process();

    o.set_freq(190.f); // mip 2
    for (int i = 0; i < 48; ++i) o.process();
    o.set_freq(110.f); // mip 1; source becomes 0.5 * mip 0 + 0.5 * mip 2
    for (int i = 0; i < 24; ++i) o.process();
    o.set_freq(90.f);  // mip 0; source becomes 0.375 * mip 0 + 0.375 * mip 2 + 0.25 * mip 1
    CHECK(o.mip_crossfading());

    o.reset_phase(output_phase - 90.f / sample_rate);
    const float source = 0.375f * read_frame_mip_at_phase(13, 0, output_phase)
        + 0.375f * read_frame_mip_at_phase(13, 2, output_phase)
        + 0.25f * read_frame_mip_at_phase(13, 1, output_phase);
    const float expected = source * (95.f / 96.f)
        + read_frame_mip_at_phase(13, 0, output_phase) * (1.f / 96.f);
    CHECK(o.process() == doctest::Approx(expected).epsilon(1e-6));
    for (int i = 0; i < WtOsc::kRampSamples - 1; ++i) o.process();
    CHECK_FALSE(o.mip_crossfading());
}

TEST_CASE("wt_osc: mip selection follows octave-floor boundaries") {
    WtOsc o;
    o.init(48000.f);
    o.set_freq(90.f);
    CHECK(o.mip_level() == 0);
    o.set_freq(110.f);
    CHECK(o.mip_level() == 1);
    o.set_freq(220.f);
    CHECK(o.mip_level() == 2);
}

TEST_CASE("wt_osc: same calls produce bit-identical output") {
    auto run = [] {
        WtOsc o;
        o.init(48000.f);
        std::vector<float> out;
        for (int b = 0; b < 64; ++b) {
            o.set_freq(55.f * std::pow(2.f, b / 16.f));
            o.set_morph((b % 17) / 16.f);
            for (int i = 0; i < 96; ++i) out.push_back(o.process());
        }
        return out;
    };
    CHECK(run() == run());
}

static std::vector<double> dft_energy(const std::vector<float>& samples) {
    const int n = static_cast<int>(samples.size());
    std::vector<double> energy(n / 2 + 1);
    for (int bin = 0; bin <= n / 2; ++bin) {
        double real = 0.0;
        double imag = 0.0;
        for (int i = 0; i < n; ++i) {
            const double phase = 6.28318530717958647692 * bin * i / n;
            const double window = 0.5 - 0.5 * std::cos(6.28318530717958647692 * i / (n - 1));
            const double sample = samples[i] * window;
            real += sample * std::cos(phase);
            imag -= sample * std::sin(phase);
        }
        energy[bin] = real * real + imag * imag;
    }
    return energy;
}

static double other_bin_energy_db(const std::vector<float>& samples, float freq,
                                  float sample_rate, int mip) {
    const std::vector<double> energy = dft_energy(samples);
    double wanted = 0.0;
    double unwanted = 0.0;
    const int max_retained_partial = wt::kMipLength[mip] / 4;
    for (int bin = 0; bin < static_cast<int>(energy.size()); ++bin) {
        bool harmonic = false;
        for (int h = 1; h <= max_retained_partial && h * freq < sample_rate * 0.5f; ++h) {
            const float harmonic_bin = h * freq * samples.size() / sample_rate;
            if (std::fabs(bin - harmonic_bin) <= 1.f) {
                harmonic = true;
                break;
            }
        }
        if (harmonic) wanted += energy[bin];
        else unwanted += energy[bin];
    }
    return 10.0 * std::log10(unwanted / wanted);
}

TEST_CASE("wt_osc: retained-partial mask detects a folded high harmonic") {
    constexpr float sample_rate = 56320.f;
    constexpr float freq = 3520.f;
    constexpr int sample_count = 2048;
    std::vector<float> samples;
    samples.reserve(sample_count);
    for (int i = 0; i < sample_count; ++i) {
        const float phase = 6.28318530717958647692f * freq * i / sample_rate;
        samples.push_back(std::sin(phase) + 0.1f * std::sin(9.f * phase));
    }

    // The ninth partial folds onto the seventh harmonic bin.  Mip 6 retains
    // only four source partials, so this must count as out-of-band energy.
    CHECK(other_bin_energy_db(samples, freq, sample_rate, 6) > -36.0);
}

TEST_CASE("wt_osc: band-limited mips keep alias energy 36 dB below harmonics") {
    constexpr float sample_rate = 56320.f; // Each test pitch is a DFT bin and advances its selected mip by one sample.
    constexpr int sample_count = 2048;
    for (float freq : {110.f, 220.f, 440.f, 880.f, 1760.f, 3520.f}) {
        WtOsc o;
        o.init(sample_rate);
        o.set_morph(13.f / 15.f);
        o.set_freq(freq);
        for (int i = 0; i < WtOsc::kRampSamples; ++i) o.process();

        std::vector<float> samples;
        samples.reserve(sample_count);
        for (int i = 0; i < sample_count; ++i) samples.push_back(o.process());

        CHECK(other_bin_energy_db(samples, freq, sample_rate, o.mip_level()) <= -36.0);
    }
}
