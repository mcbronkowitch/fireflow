// Task 7 review (spec 2026-07-26-body-resonator-engine): VoiceCountProbe
// (test_synth_engine_voice_count.cpp) proved SynthEngineT<V> compiles at a
// voice count other than 4 with a STUB voice. BodyVoice is the first REAL
// voice run through the template at kEngineVoices == 1, so it is the one
// that would trip anything in SynthEngineT still quietly assuming 4 (a loop
// bound, an array literal, a hardcoded index) that a fake voice's no-op
// methods could never exercise.
//
// SynthEngineBodyVoiceProof (engine/synth/synth_engine.h) is a compile/link/
// run proof only -- NOT the production BODY part engine. Task 8 owns that
// alias, plus wiring COLOR to chord quality (spec amendment, out of scope
// here: at one voice per deck the chord layer has no slots, so COLOR moves
// to material stretch instead of BodyVoice).
#include "doctest/doctest.h"
#include "synth/synth_engine.h"

using namespace spky;

TEST_CASE("SynthEngineT<BodyVoice> compiles, links and runs at kVoices == 1") {
    SynthEngineBodyVoiceProof eng;
    eng.set_seed(7);
    eng.init(48000.f);
    static_assert(SynthEngineBodyVoiceProof::kVoices == 1,
                  "kVoices must come from BodyVoice::kEngineVoices, not a fixed 4");

    float targets[LANE_COUNT] = {0.5f, 0.5f, 0.5f, 0.5f, 0.8f};
    eng.set_targets(targets, 1.f);
    eng.trigger(0.5f);

    float energy = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l = 0.f, r = 0.f;
        eng.process(l, r);
        energy += l * l + r * r;
    }
    CHECK(energy > 0.f);              // the voice actually made sound
    CHECK(eng.active_voices() <= 1);  // never more than kVoices == 1
}
