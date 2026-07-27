// Fix round 1 (spec 2026-07-26 body-resonator, Task 5 review): SynthEngineT's
// deliverable is that it accepts a voice type with a DIFFERENT voice count,
// not just VoiceT<OscT> at the fixed value 4. Nothing else in the suite
// instantiates it below kVoices == 4, so a regression there (e.g. a data
// member initializer that only happens to compile at exactly 4 elements) is
// invisible until BODY's real voice (Task 7) trips it.
//
// spky::SynthEngineVoiceCountProof (engine/synth/synth_engine.h) is
// SynthEngineT<detail::VoiceCountProbe>, a minimal stand-in voice with
// kEngineVoices == 1, explicitly instantiated in synth_engine.cpp the same
// way SYNTH and WAVE are. This file is the runtime half of that proof: it
// does not need to make sound, only to compile, link and run one trigger.
#include <doctest/doctest.h>

#include "synth/synth_engine.h"

using namespace spky;

TEST_CASE("SynthEngineT compiles and runs at a voice count other than 4") {
    static_assert(SynthEngineVoiceCountProof::kVoices == 1,
                  "kVoices must come from V::kEngineVoices, not a fixed 4");

    SynthEngineVoiceCountProof e;
    e.set_seed(1);
    e.init(48000.f);
    e.trigger(0.5f);
    for (int i = 0; i < 96; ++i) {
        float l = 0.f, r = 0.f;
        e.process(l, r);
    }
    CHECK(e.active_voices() == 1);
}

// Second half of the same proof, and the reason it is worth having a probe
// voice that COUNTS: SynthEngineT::_adjust_surface blooms the FLOW chord
// surface one voice per control tick until the surface holds _chord_n notes.
// At kVoices == 4 that terminates, because _chord_n is clamped to
// kMaxChord == 4 in set_chord(). At kVoices == 1 it never terminated: with a
// chord of 2+ notes the engine wanted a slot it had no voice for, so every
// control tick stole its only voice for the next missing slot and retriggered
// it -- measured 501 triggers per second of audio at 48 kHz (one per
// kCtrlInterval), against 1 for the same surface with a single note.
//
// _do_trigger's own steal comment ("unreachable during a bloom, since bloom
// implies m < _chord_n <= kVoices") is the invariant that broke: it holds
// only while kVoices >= kMaxChord. On BODY (kVoices == 1) the audible result
// is a resonator re-struck at 500 Hz instead of ringing.
//
// The fix clamps the wanted surface size to kVoices. Nothing changes at
// kVoices == 4 (min(_chord_n, 4) == _chord_n for every _chord_n set_chord
// admits), which is what the ctrl_identity render gate re-checks byte for
// byte.
TEST_CASE("the FLOW surface never blooms past the voices the engine has") {
    auto triggers_over_one_second = [](int n_chord) {
        detail::VoiceCountProbe::trig_count = 0;
        SynthEngineVoiceCountProof e;
        e.set_seed(3u);
        e.init(48000.f);
        const float chord[4] = { 0.3f, 0.36f, 0.42f, 0.5f };
        e.set_flow(true);
        for (int i = 0; i < 48000; ++i) {
            e.set_chord(chord, n_chord);
            float l = 0.f, r = 0.f;
            e.process(l, r);
        }
        CHECK(e.sustain_count() == 1);   // one voice, so one sustaining voice
        return detail::VoiceCountProbe::trig_count;
    };

    // The drone promise fires once on entering FLOW. After that the surface is
    // as full as a one-voice engine can make it, whatever the chord asks for,
    // so nothing may retrigger.
    CHECK(triggers_over_one_second(1) == 1);
    CHECK(triggers_over_one_second(2) == 1);
    CHECK(triggers_over_one_second(4) == 1);
}
