#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "doctest/doctest.h"
#include "synth/synth_engine.h"

// The SHARED part-engine contract: what SynthEngineT<V> promises for EVERY V.
//
// Everything in this file is a claim about the MACHINE -- allocation, steal,
// the FLOW drone promise, CHOKE, chord-surface bookkeeping, determinism,
// DETUNE independence -- and holds whatever voice the template is given and
// however many voices that voice asks for. SYNTH, WAVE and BODY all run it.
//
// What is NOT here lives in synth_voicet_contract.h: the 0.7 sustain plateau,
// the cycle-scaled attack/decay timing, the hard zero at the end of a note,
// and the retrigger-from-current-level steal. Those are true of VoiceT's AD
// envelope, not of SynthEngineT, and only SYNTH and WAVE run them. The split
// was made in Task 8 (spec 2026-07-26 body-resonator) when BodyVoice arrived:
// its env_value() is an ENERGY FOLLOWER, not an envelope, and it has one
// voice, not four -- three of the five contract functions failed against it
// on 30 assertions without a single BodyEngine defect among them. Nothing was
// weakened to get there: every assertion below evaluates to exactly what it
// evaluated to before for a four-voice engine, and every assertion that could
// not be generalised without weakening was MOVED, not softened.
//
// Rule for anything added here later: if the claim mentions a number that is
// really "4" or a shape that is really "an AD envelope", it belongs in the
// VoiceT contract instead.

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

// How much of an n-note chord an engine can actually sustain. This is the
// engine's own rule, not a test convenience: set_chord clamps to kMaxChord,
// and trigger_chord and _adjust_surface both clamp to kVoices
// (synth_engine.cpp). At kVoices >= kMaxChord it is the identity, so every
// use of it below reads exactly as the literal it replaced for SYNTH and
// WAVE.
template <class EngineT>
constexpr int cap(int n) {
    return n < EngineT::kVoices ? n : EngineT::kVoices;
}

// Fill every voice, then steal one, measuring everything either contract
// wants to know.
//
// This is shared SETUP CODE, not a shared measurement. Both
// contract_round_robin_and_steal (machine claims) and
// voicet_steal_retriggers_from_level (envelope claim) call it, and each call
// constructs its own EngineT and runs the whole thing again. The two runs
// agree because the engine is deterministic -- which is not an assumption
// here, it is contract_deterministic_seed, asserted for every engine that
// runs this suite. Nothing memoises the result, and nothing would notice if
// an engine acquired run-to-run state: contract_deterministic_seed would go
// red first, and that is the intended order.
//
// The reason to factor it out is therefore that the two contracts drive the
// engine through the SAME steps rather than through two copies of them that
// can be edited apart -- not that they observe the same object.
struct StealProbe {
    int   active_after_fill  = -1;
    std::vector<float> env_after_fill;   // kVoices entries
    int   active_after_steal = -1;
    float max_delta          = 0.f;
    float env0_at_steal      = 0.f;
    float env0_after_steal   = 0.f;
};

template <class EngineT>
StealProbe fill_and_steal() {
    StealProbe p;
    EngineT e;
    fresh(e);
    e.set_cycle(4.f);
    for (int v = 0; v < EngineT::kVoices; ++v) {
        e.trigger(0.2f + 0.15f * v);
        render_l(e, 960);
    }
    p.active_after_fill = e.active_voices();
    for (int v = 0; v < EngineT::kVoices; ++v)
        p.env_after_fill.push_back(e.voice_env(v));

    float prev = 0.f;
    for (int i = 0; i < 9600; ++i) {
        if (i == 4800) {
            p.env0_at_steal = e.voice_env(0);
            e.trigger(0.8f);
        }
        float l = 0.f, r = 0.f;
        e.process(l, r);
        if (i > 0) p.max_delta = std::max(p.max_delta, std::fabs(l - prev));
        prev = l;
    }
    p.active_after_steal = e.active_voices();
    p.env0_after_steal = e.voice_env(0);
    return p;
}

} // namespace spky_contract

// Allocation: round-robin fills every voice the engine has, and a trigger
// with none free steals rather than dropping the note or growing the pool.
// The steal is click-free.
template <class EngineT>
void contract_round_robin_and_steal() {
    using namespace spky_contract;

    const StealProbe p = fill_and_steal<EngineT>();
    CHECK(p.active_after_fill == EngineT::kVoices);
    for (float env : p.env_after_fill) CHECK(env > 0.f);
    CHECK(p.active_after_steal == EngineT::kVoices);
    CHECK(p.max_delta < 0.2f);
}

template <class EngineT>
void contract_flow_drone_and_surface() {
    using namespace spky_contract;

    // The drone promise: in FLOW there is always exactly one voice marked as
    // the sustaining one, and it survives a retrigger.
    EngineT e;
    fresh(e);
    feed(e, 0.25f);
    e.set_flow(true);
    render_l(e, 48000);
    CHECK(e.active_voices() >= 1);
    CHECK(e.sustain_voice() >= 0);
    render_l(e, 48000 * 3);
    CHECK(e.sustain_voice() >= 0);

    e.trigger(0.25f);
    render_l(e, 48000 * 8);
    CHECK(e.sustain_voice() >= 0);
    CHECK(e.active_voices() >= 1);

    // Entering FLOW with nothing sounding auto-triggers the drone.
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
    CHECK(surface.sustain_count() == cap<EngineT>(4));

    const float next[3] = { 0.35f, 0.41f, 0.47f };
    surface.trigger_chord(next, 3);
    run_surface(surface, next, 3, 48000);
    CHECK(surface.sustain_count() == cap<EngineT>(3));
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
    CHECK(stab.active_voices() == cap<EngineT>(4));

    EngineT bloom;
    bloom.set_seed(3u);
    bloom.init(48000.f);
    const float one[1] = { 0.4f };
    const float three[3] = { 0.4f, 0.33f, 0.48f };
    bloom.set_flow(true);
    run_surface(bloom, one, 1, 24000);
    CHECK(bloom.sustain_count() == cap<EngineT>(1));
    float step = 0.f;
    run_surface(bloom, three, 3, 24000, &step);
    CHECK(bloom.sustain_count() == cap<EngineT>(3));
    CHECK(step < 0.3f);
    step = 0.f;
    run_surface(bloom, one, 1, 48000, &step);
    CHECK(bloom.sustain_count() == cap<EngineT>(1));
    CHECK(step < 0.3f);

    EngineT hold;
    hold.set_seed(3u);
    hold.init(48000.f);
    const float chord[3] = { 0.3f, 0.36f, 0.42f };
    hold.set_flow(true);
    run_surface(hold, chord, 3, 48000);
    CHECK(hold.sustain_count() == cap<EngineT>(3));
    hold.set_hold(true);
    run_surface(hold, chord, 3, 4800);
    CHECK(hold.sustain_count() == 0);
    hold.set_hold(false);
    run_surface(hold, chord, 3, 48000);
    CHECK(hold.sustain_count() == cap<EngineT>(3));

    EngineT entering;
    entering.set_seed(3u);
    entering.init(48000.f);
    entering.set_flow(false);
    run_surface(entering, chord, 3, 480);
    CHECK(entering.sustain_count() == 0);
    entering.set_flow(true);
    run_surface(entering, chord, 3, 48000);
    CHECK(entering.sustain_count() == cap<EngineT>(3));

    EngineT rebloom;
    rebloom.set_seed(5u);
    rebloom.init(48000.f);
    rebloom.set_decay(0.8f);
    const float surface_four[4] = { 0.3f, 0.36f, 0.42f, 0.5f };
    const float surface_three[3] = { 0.3f, 0.36f, 0.42f };
    rebloom.set_flow(true);
    run_surface(rebloom, surface_four, 4, 48000);
    CHECK(rebloom.sustain_count() == cap<EngineT>(4));
    run_surface(rebloom, surface_three, 3, 480);
    CHECK(rebloom.sustain_count() == cap<EngineT>(3));

    int min_sustain = rebloom.sustain_count();
    int reached_full_at = -1;
    for (int i = 0; i < 480; ++i) {
        rebloom.set_chord(surface_four, 4);
        float l = 0.f, r = 0.f;
        rebloom.process(l, r);
        const int sc = rebloom.sustain_count();
        if (sc < min_sustain) min_sustain = sc;
        if (reached_full_at < 0 && sc == cap<EngineT>(4)) reached_full_at = i;
    }
    CHECK(min_sustain >= cap<EngineT>(3));
    REQUIRE(reached_full_at >= 0);
    CHECK(reached_full_at < 300);
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
