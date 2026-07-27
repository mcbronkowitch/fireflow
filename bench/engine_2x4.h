#pragma once
#include "workload.h"   // kSampleRate, kBlock

namespace bench {

// Matched two-engine, four-voice A/B pattern (Task 8, bench/workloads_
// system.cpp's synth_2x4 / wave_2x4). Hoisted out to a shared header (Task
// 13) so bench/workloads_body.cpp can reuse it for BodyEngine without
// copying it: setup_engine_2x4 and proc_engine_2x4 only call set_seed, init,
// set_decay, set_cycle, set_flow, trigger and active_voices, every one of
// which SynthEngineT<V> provides regardless of V, so BodyEngine (kVoices ==
// 1) instantiates them exactly as SynthEngine and WaveEngine (kVoices == 4)
// already do -- unchanged, a pure move from workloads_system.cpp.
constexpr float kEngine2x4Pitches[] = { 0.25f, 0.35f, 0.45f, 0.55f };

template <class EngineT>
void setup_engine_2x4(EngineT& a, EngineT& b)
{
    a.set_seed(3u);
    b.set_seed(4u);
    a.init(kSampleRate);
    b.init(kSampleRate);
    a.set_decay(1.f);
    b.set_decay(1.f);
    a.set_cycle(2.f);
    b.set_cycle(2.f);
    a.set_flow(false);
    b.set_flow(false);
    for (float pitch : kEngine2x4Pitches) {
        a.trigger(pitch);
        b.trigger(pitch);
    }
}

template <class EngineT>
float proc_engine_2x4(EngineT& a, EngineT& b)
{
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float a_l, a_r, b_l, b_r;
        a.process(a_l, a_r);
        b.process(b_l, b_r);
        acc += a_l + a_r + b_l + b_r;
    }
    acc += static_cast<float>(a.active_voices());
    acc += static_cast<float>(b.active_voices());
    return acc;
}

} // namespace bench
