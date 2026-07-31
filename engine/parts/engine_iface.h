#pragma once
#include "mod/lane_id.h"

namespace spky {

// Selectable part engines. ENGINE_SYNTH is the boot default from M2 on;
// the test tone stays selectable (tests, A/B reference). M5 adds the
// sampler and WAVE -- appended, so no persisted id changes meaning. BODY
// (spec 2026-07-26 body-resonator) is appended for the same reason: ids go
// in milestone order and are never renumbered, because patches persist them.
enum EngineId {
    ENGINE_TEST_TONE = 0,
    ENGINE_SYNTH = 1,
    ENGINE_SAMPLER = 2,
    ENGINE_WAVE = 3,
    ENGINE_BODY = 4,
    // The bucket-brigade delay (spec 2026-07-31 bbd-part-engine). Voiceless
    // and input-consuming, so it is the second engine after the sampler to
    // override the process_in/consumes_input pair.
    ENGINE_BBD = 5,
    // Sentinel, not a selectable engine -- keep it last so it always equals
    // the count. Exists so a `static_assert(ENGINE_COUNT == N, ...)` next to
    // any hand-written "every engine" list (e.g. tests/test_deck_bus.cpp's
    // bit-identity sweep) fails to build the day a new engine is added
    // instead of silently continuing to cover only the old set.
    ENGINE_COUNT
};

// A part's sound engine. Consumes the 5 normalized target values; produces
// stereo audio. TestToneEngine (M1), SynthEngine (M2) and SamplerEngine
// (M5) implement the same interface behind the same Part.
class IPartEngine {
public:
    virtual ~IPartEngine() = default;
    virtual void init(float sample_rate) = 0;
    virtual void set_targets(const float* targets /*[LANE_COUNT]*/, float tune) = 0;
    virtual void trigger(float pitch_norm) = 0;
    virtual void process(float& outL, float& outR) = 0;

    // M2 additions - default no-ops so engines that don't care (test tone,
    // M5 sampler) ignore them. Part forwards both: cycle on change (not per
    // sample), flow on STEP/FLOW switches.
    virtual void set_cycle(float /*seconds*/) {}   // master-lane cycle length
    virtual void set_flow(bool /*flow*/) {}        // true = FLOW, false = STEP

    // CHOKE hold (spec 2026-07-16 choke-priority rev. 2): while held, a FLOW
    // engine releases its sustaining drone (decays out, click-free) and stops
    // auto-retriggering; releasing the hold re-arms it. Default no-op.
    virtual void set_hold(bool /*on*/) {}

    // Chord layer (spec 2026-07-17 chord-layer). trigger_chord fires one
    // chord; the default keeps single-note engines working unchanged.
    // set_chord feeds the CURRENT chord surface targets so a FLOW engine can
    // track root + COLOR live; engines without a surface ignore it. Part
    // pushes it from _control_tick() (engine/parts/part.cpp), i.e. once per
    // Part::kCtrlInterval samples plus once on each step fire -- NOT every
    // sample, which this comment claimed until 2026-07-26 and which two
    // later comments in synth_engine were written on top of.
    virtual void trigger_chord(const float* pitches_norm, int n) {
        for (int i = 0; i < n; ++i) trigger(pitches_norm[i]);
    }
    virtual void set_chord(const float* /*pitches_norm*/, int /*n*/) {}

    // M5 additions -- default no-ops, so the synth and the test tone are
    // untouched by them (the neutrality gate proves this).
    //
    // process_in: per-sample input feed, called by Part BEFORE process().
    // Only the sampler implements it -- it records and monitors from here.
    virtual void process_in(float /*inL*/, float /*inR*/) {}
    //
    // consumes_input: does this engine override process_in? Answered once per
    // engine swap and never per sample -- Part caches the answer (part.h) so
    // its per-sample path can skip an indirect call whose target is the empty
    // body above. Skipping it is observationally identical for every engine
    // that does not override process_in, which today is all of them but the
    // sampler.
    //
    // The two default together and MUST be overridden together. Nothing
    // enforces that: an engine cannot be asked at runtime whether it overrode
    // a virtual, so an engine that implements process_in and forgets this one
    // keeps returning false and its input feed goes silently missing -- no
    // crash, no assert, just an engine that never hears the input. The pairing
    // is a convention held by this comment and by the override sitting next to
    // process_in in every implementer (sampler_engine.h).
    virtual bool consumes_input() const { return false; }
    //
    // set_gate: Part's composed gate signal (the manual 5 ms pulse OR'd with
    // the groove's note sustain -- exactly what Part::gate() computes), so a
    // cloud can sound for the composed note duration in STEP. Forwarded on
    // EDGES, not per sample (the set_cycle idiom). The synth ignores it: it
    // has its own envelope.
    virtual void set_gate(bool /*on*/) {}

    // Excitation bus (spec 2026-07-26 body-resonator §6, Task 9): the part's
    // own FLUX tape tap, one control block late, pushed once per control
    // tick alongside set_chord() (Part::_control_tick(), part.cpp). Default
    // no-op, same shape as set_chord/set_gate/set_cycle above -- so
    // TestToneEngine and SamplerEngine ignore it for free, and only
    // SynthEngineT<BodyVoice> (synth/synth_engine.h) does anything with it.
    // Chosen over a concrete `if (_engine_id == ENGINE_BODY) _body.set_
    // excitation(...)` at the call site: that alternative is provably
    // cheaper for the other three engines, but it would need its own
    // re-sync reasoning for a deck that switches away from BODY and back
    // (does the stale value in a re-activated _body matter?). Going through
    // `_engine` sidesteps the question entirely -- whichever engine is
    // active is exactly the one that gets pushed, identical to how
    // set_chord already behaves across an engine switch -- at a cost
    // (one virtual call per part per control tick) that is immeasurable
    // next to the audio-rate budget this file's other comments are about.
    virtual void set_excitation(float /*x*/) {}

    // Stereo width, pushed once per control tick by Part::_control_tick from
    // the SAME effective COLOR the chord layer receives. Default no-op, the
    // set_excitation idiom: whichever engine is active is exactly the one that
    // gets pushed, and an engine switched away from and back to needs no
    // re-sync reasoning. One virtual call per part per control tick.
    virtual void set_width(float /*n*/) {}
};

} // namespace spky
