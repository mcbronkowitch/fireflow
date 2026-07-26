#pragma once

#include "synth_engine_contract.h"

// The VoiceT half of what used to be one contract (spec 2026-07-26
// body-resonator, Task 8). Everything here was in synth_engine_contract.h
// until BODY arrived, and every assertion is UNCHANGED -- same engine setup,
// same seeds, same sample counts, same bounds. It moved rather than
// generalised because generalising it would have meant weakening it.
//
// What makes a claim live here rather than in the shared contract:
//
// 1. It reads voice_env() as an ENVELOPE. SynthEngineT::voice_env returns
//    V::env_value(), which for VoiceT is an AD envelope with a defined 0.7
//    sustain plateau and a defined zero. For BodyVoice it is an energy
//    follower -- it sits wherever the resonator's energy currently is, and
//    "0.7 and holds" is not a statement about it at all.
// 2. It scales a note's length to the master cycle. VoiceT's attack and decay
//    are cycle-scaled envelope times. BODY's ring time comes from damping,
//    which DECAY sets; how tempo-locked it feels is a listening decision
//    (Task 12), not a bound a contract can carry.
// 3. It requires an idle voice to output a hard zero. True of an envelope
//    multiplying an oscillator; false of a resonator, which is still ringing
//    below the -72 dB floor at which BodyVoice::active() reports inactive.
// 4. It requires a SECOND voice to exist. "A new fire promotes a different
//    voice" cannot be false or true at kVoices == 1; there is no different
//    voice, and 0 != 0 fails by arithmetic.
//
// SYNTH and WAVE run all of it, so nothing they assert today got any weaker.

// The steal retriggers the stolen voice from its CURRENT envelope level, so
// the level goes UP -- the click-free retrigger of Voice::trigger.
//
// This runs fill_and_steal<EngineT>() a SECOND time; the shared contract's
// own call built and threw away its own engine. Same setup code, run twice,
// and the two runs land on the same numbers only because the engine is
// deterministic -- see the note on fill_and_steal in
// synth_engine_contract.h for why that is a tested property and not an
// assumption.
// (Category 1: on a resonator the follower LAGS a strike instead of jumping,
// so it is still falling one sample after the steal even when the strike
// landed. voice_env is the wrong instrument to ask a resonator that with.)
template <class EngineT>
void voicet_steal_retriggers_from_level() {
    using namespace spky_contract;

    const StealProbe p = fill_and_steal<EngineT>();
    CHECK(p.env0_after_steal > p.env0_at_steal);
}

// Attack and decay are a fixed fraction of the master cycle: a note's whole
// life scales with tempo, and it ends in exact silence. (Categories 2 and 3.)
template <class EngineT>
void voicet_cycle_scaled_ad_envelope() {
    using namespace spky_contract;

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

// The sustaining voice decays to the 0.7 plateau and holds there; a new fire
// promotes a DIFFERENT voice and demotes the old one all the way to zero.
// (Categories 1 and 4.)
template <class EngineT>
void voicet_flow_plateau_and_handover() {
    using namespace spky_contract;

    EngineT e;
    fresh(e);
    feed(e, 0.25f);
    e.set_flow(true);
    render_l(e, 48000);
    render_l(e, 48000 * 3);
    CHECK(e.voice_env(e.sustain_voice()) == doctest::Approx(0.7f).epsilon(0.03));

    const int old_voice = e.sustain_voice();
    e.trigger(0.25f);
    CHECK(e.sustain_voice() != old_voice);
    render_l(e, 48000 * 8);
    CHECK(e.voice_env(old_voice) == 0.f);
    CHECK(e.voice_env(e.sustain_voice()) == doctest::Approx(0.7f).epsilon(0.03));

    // Every voice of a full chord surface sits on the same plateau.
    EngineT surface;
    surface.set_seed(3u);
    surface.init(48000.f);
    const float chord[4] = { 0.3f, 0.36f, 0.42f, 0.5f };
    surface.set_flow(true);
    run_surface(surface, chord, 4, 48000);
    for (int v = 0; v < EngineT::kVoices; ++v)
        CHECK(surface.voice_env(v) == doctest::Approx(0.7f).epsilon(0.05));
}
