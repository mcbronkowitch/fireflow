#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "doctest/doctest.h"
#include "synth/synth_engine.h"

namespace spky_contract {

using namespace spky;

template <class EngineT>
void feed(EngineT& e, float pitch, float timbre = 0.f, float filter = 1.f,
          float motion = 0.f, float level = 1.f) {
    float t[LANE_COUNT] = { timbre, filter, pitch, motion, level };
    e.set_targets(t, 0.5f);
}

template <class EngineT>
void fresh(EngineT& e, uint32_t seed = 99) {
    e.set_seed(seed);
    e.init(48000.f);
    e.set_sub(0.f);
    e.set_detune(0.f);
    e.set_cycle(1.f);
    feed(e, 0.5f);
}

template <class EngineT>
std::vector<float> render_l(EngineT& e, int n) {
    std::vector<float> out(n);
    for (auto& s : out) {
        float l = 0.f, r = 0.f;
        e.process(l, r);
        s = l;
    }
    return out;
}

template <class EngineT>
void run_surface(EngineT& e, const float* chord, int n_chord,
                 int samples, float* max_step = nullptr) {
    float prev = 0.f;
    for (int i = 0; i < samples; ++i) {
        e.set_chord(chord, n_chord);
        float l = 0.f, r = 0.f;
        e.process(l, r);
        if (max_step && i > 0 && std::fabs(l - prev) > *max_step)
            *max_step = std::fabs(l - prev);
        prev = l;
    }
}

} // namespace spky_contract

template <class EngineT>
void contract_round_robin_and_steal() {
    using namespace spky_contract;

    EngineT e;
    fresh(e);
    e.set_cycle(4.f);
    for (int v = 0; v < 4; ++v) {
        e.trigger(0.2f + 0.15f * v);
        render_l(e, 960);
    }
    CHECK(e.active_voices() == 4);
    for (int v = 0; v < 4; ++v) CHECK(e.voice_env(v) > 0.f);

    float env0_at_steal = 0.f;
    float prev = 0.f, max_delta = 0.f;
    for (int i = 0; i < 9600; ++i) {
        if (i == 4800) {
            env0_at_steal = e.voice_env(0);
            e.trigger(0.8f);
        }
        float l = 0.f, r = 0.f;
        e.process(l, r);
        if (i > 0) max_delta = std::max(max_delta, std::fabs(l - prev));
        prev = l;
    }
    CHECK(e.active_voices() == 4);
    CHECK(e.voice_env(0) > env0_at_steal);
    CHECK(max_delta < 0.2f);

    auto silence_time = [](float cycle_s) {
        EngineT voice;
        fresh(voice);
        voice.set_cycle(cycle_s);
        voice.trigger(0.5f);
        int n = 0;
        while (n < 48000 * 30) {
            float l = 0.f, r = 0.f;
            voice.process(l, r);
            ++n;
            if (voice.active_voices() == 0) break;
        }
        return n;
    };
    const int fast = silence_time(0.5f);
    const int slow = silence_time(2.0f);
    CHECK(fast > static_cast<int>(0.75f * 48000));
    CHECK(fast < static_cast<int>(1.4f * 48000));
    CHECK(slow > static_cast<int>(3.0f * 48000));
    CHECK(slow < static_cast<int>(5.6f * 48000));
    CHECK(slow > fast * 3);

    EngineT extreme;
    fresh(extreme);
    extreme.set_cycle(0.02f);
    extreme.trigger(0.5f);
    int to_peak = 0;
    while (extreme.voice_env(0) < 1.f && to_peak < 4800) {
        float l = 0.f, r = 0.f;
        extreme.process(l, r);
        ++to_peak;
    }
    CHECK(to_peak >= 80);
    CHECK(to_peak <= 200);
    int n = to_peak;
    while (extreme.active_voices() > 0 && n < 48000) {
        float l = 0.f, r = 0.f;
        extreme.process(l, r);
        ++n;
    }
    CHECK(n - to_peak > static_cast<int>(0.05f * 48000));
    CHECK(n - to_peak < static_cast<int>(0.05f * 48000 * 2.0f));
    float l = 0.f, r = 0.f;
    extreme.process(l, r);
    CHECK(l == 0.f);
    CHECK(r == 0.f);
}

template <class EngineT>
void contract_flow_drone_and_surface() {
    using namespace spky_contract;

    EngineT e;
    fresh(e);
    feed(e, 0.25f);
    e.set_flow(true);
    render_l(e, 48000);
    CHECK(e.active_voices() >= 1);
    CHECK(e.sustain_voice() >= 0);
    render_l(e, 48000 * 3);
    CHECK(e.voice_env(e.sustain_voice()) == doctest::Approx(0.7f).epsilon(0.03));

    const int old_voice = e.sustain_voice();
    e.trigger(0.25f);
    CHECK(e.sustain_voice() != old_voice);
    render_l(e, 48000 * 8);
    CHECK(e.voice_env(old_voice) == 0.f);
    CHECK(e.voice_env(e.sustain_voice()) == doctest::Approx(0.7f).epsilon(0.03));

    EngineT entry;
    fresh(entry);
    render_l(entry, 4800);
    CHECK(entry.active_voices() == 0);
    entry.set_flow(true);
    render_l(entry, 4800);
    CHECK(entry.active_voices() >= 1);
    CHECK(entry.sustain_voice() >= 0);

    EngineT mono;
    fresh(mono);
    mono.set_flow(true);
    feed(mono, 0.5f, 0.f, 1.f, 0.f);
    float max_diff = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l = 0.f, r = 0.f;
        mono.process(l, r);
        max_diff = std::max(max_diff, std::fabs(l - r));
    }
    CHECK(max_diff == 0.f);

    EngineT stereo;
    fresh(stereo);
    stereo.set_flow(true);
    feed(stereo, 0.5f, 0.f, 1.f, 1.f);
    float suml = 0.f, sumr = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l = 0.f, r = 0.f;
        stereo.process(l, r);
        suml += l * l;
        sumr += r * r;
    }
    CHECK(std::fabs(suml - sumr) / (suml + sumr + 1e-9f) > 0.2f);

    EngineT surface;
    surface.set_seed(3u);
    surface.init(48000.f);
    const float chord[4] = { 0.3f, 0.36f, 0.42f, 0.5f };
    surface.set_flow(true);
    run_surface(surface, chord, 4, 48000);
    CHECK(surface.sustain_count() == 4);
    for (int v = 0; v < EngineT::kVoices; ++v)
        CHECK(surface.voice_env(v) == doctest::Approx(0.7f).epsilon(0.05));

    const float next[3] = { 0.35f, 0.41f, 0.47f };
    surface.trigger_chord(next, 3);
    run_surface(surface, next, 3, 48000);
    CHECK(surface.sustain_count() == 3);
}

template <class EngineT>
void contract_chord_surface_and_hold() {
    using namespace spky_contract;

    EngineT a, b;
    a.set_seed(42u); b.set_seed(42u);
    a.init(48000.f); b.init(48000.f);
    a.set_flow(false); b.set_flow(false);
    a.trigger(0.4f);
    const float p = 0.4f;
    b.trigger_chord(&p, 1);
    for (int i = 0; i < 9600; ++i) {
        float la = 0.f, ra = 0.f, lb = 0.f, rb = 0.f;
        a.process(la, ra); b.process(lb, rb);
        CHECK(la == lb);
        CHECK(ra == rb);
    }

    EngineT stab;
    stab.set_seed(7u);
    stab.init(48000.f);
    stab.set_flow(false);
    const float four[4] = { 0.3f, 0.36f, 0.42f, 0.5f };
    stab.trigger_chord(four, 4);
    CHECK(stab.active_voices() >= 1);
    const int window = static_cast<int>(EngineT::kStabSpreadS * 48000.f) + 2;
    for (int i = 0; i < window; ++i) { float l, r; stab.process(l, r); }
    CHECK(stab.active_voices() == 4);

    EngineT bloom;
    bloom.set_seed(3u);
    bloom.init(48000.f);
    const float one[1] = { 0.4f };
    const float three[3] = { 0.4f, 0.33f, 0.48f };
    bloom.set_flow(true);
    run_surface(bloom, one, 1, 24000);
    CHECK(bloom.sustain_count() == 1);
    float step = 0.f;
    run_surface(bloom, three, 3, 24000, &step);
    CHECK(bloom.sustain_count() == 3);
    CHECK(step < 0.3f);
    step = 0.f;
    run_surface(bloom, one, 1, 48000, &step);
    CHECK(bloom.sustain_count() == 1);
    CHECK(step < 0.3f);

    EngineT hold;
    hold.set_seed(3u);
    hold.init(48000.f);
    const float chord[3] = { 0.3f, 0.36f, 0.42f };
    hold.set_flow(true);
    run_surface(hold, chord, 3, 48000);
    CHECK(hold.sustain_count() == 3);
    hold.set_hold(true);
    run_surface(hold, chord, 3, 4800);
    CHECK(hold.sustain_count() == 0);
    hold.set_hold(false);
    run_surface(hold, chord, 3, 48000);
    CHECK(hold.sustain_count() == 3);

    EngineT entering;
    entering.set_seed(3u);
    entering.init(48000.f);
    entering.set_flow(false);
    run_surface(entering, chord, 3, 480);
    CHECK(entering.sustain_count() == 0);
    entering.set_flow(true);
    run_surface(entering, chord, 3, 48000);
    CHECK(entering.sustain_count() == 3);

    EngineT rebloom;
    rebloom.set_seed(5u);
    rebloom.init(48000.f);
    rebloom.set_decay(0.8f);
    const float surface_four[4] = { 0.3f, 0.36f, 0.42f, 0.5f };
    const float surface_three[3] = { 0.3f, 0.36f, 0.42f };
    rebloom.set_flow(true);
    run_surface(rebloom, surface_four, 4, 48000);
    CHECK(rebloom.sustain_count() == 4);
    run_surface(rebloom, surface_three, 3, 480);
    CHECK(rebloom.sustain_count() == 3);

    int min_sustain = rebloom.sustain_count();
    int reached_four_at = -1;
    for (int i = 0; i < 480; ++i) {
        rebloom.set_chord(surface_four, 4);
        float l = 0.f, r = 0.f;
        rebloom.process(l, r);
        const int sc = rebloom.sustain_count();
        if (sc < min_sustain) min_sustain = sc;
        if (reached_four_at < 0 && sc == 4) reached_four_at = i;
    }
    CHECK(min_sustain >= 3);
    REQUIRE(reached_four_at >= 0);
    CHECK(reached_four_at < 300);
}

template <class EngineT>
void contract_deterministic_seed() {
    auto run = [] {
        EngineT e;
        e.set_seed(1234);
        e.init(48000.f);
        e.set_cycle(0.8f);
        e.set_flow(true);
        std::vector<float> out;
        out.reserve(96000);
        for (int i = 0; i < 48000; ++i) {
            float t[LANE_COUNT] = { 0.4f, 0.7f, 0.5f, 0.8f, 0.9f };
            e.set_targets(t, 0.5f);
            if (i == 10000 || i == 20000) e.trigger(0.3f);
            float l = 0.f, r = 0.f;
            e.process(l, r);
            out.push_back(l);
            out.push_back(r);
        }
        return out;
    };
    CHECK(run() == run());

    EngineT a, b;
    a.set_seed(9u); b.set_seed(9u);
    a.init(48000.f); b.init(48000.f);
    a.set_flow(false); b.set_flow(false);
    const float chord[3] = { 0.3f, 0.38f, 0.47f };
    a.trigger_chord(chord, 3);
    b.trigger_chord(chord, 3);
    for (int i = 0; i < 9600; ++i) {
        float la = 0.f, ra = 0.f, lb = 0.f, rb = 0.f;
        a.process(la, ra); b.process(lb, rb);
        CHECK(la == lb);
        CHECK(ra == rb);
    }
}

template <class EngineT>
void contract_detune_is_independent_of_source() {
    using namespace spky_contract;

    EngineT e;
    e.init(48000.f);
    CHECK(e.detune_spread_ct() == doctest::Approx(18.f));
    CHECK(e.applied_detune_ct() == doctest::Approx(18.f));

    e.set_detune(6.f / EngineT::kDetuneCeilCt);
    CHECK(e.detune_spread_ct() == doctest::Approx(6.f));
    CHECK(e.applied_detune_ct() == doctest::Approx(18.f));

    feed(e, 0.5f, 0.f);
    render_l(e, EngineT::kCtrlInterval + 1);
    CHECK(e.applied_detune_ct() == doctest::Approx(6.f));

    feed(e, 0.5f, 1.f);
    render_l(e, EngineT::kCtrlInterval + 1);
    CHECK(e.applied_detune_ct() == doctest::Approx(6.f));
    CHECK(e.detune_spread_ct() == doctest::Approx(6.f));
}
