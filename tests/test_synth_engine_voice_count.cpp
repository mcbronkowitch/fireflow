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
