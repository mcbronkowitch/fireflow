// tests/test_voice_edge_broadcast.cpp
//
// The ledger of which engines EDGE actually reaches.
//
// An engine missing from a broadcast line is a knob that is silently dead on
// that engine -- this project's oldest recurring failure (part.h says so on
// the set_voice_* block, engine_iface.h says it again about process_in/
// consumes_input). One case per engine, each asserting BOTH halves of what
// the knob promises: that EDGE at a non-zero trim CHANGES that engine's
// output, and that EDGE at 0 leaves it bit-identical to a deck whose knob was
// never touched.
//
// This file ends the DPTH/EDGE plan with SIX cases, one per engine. Task 3
// ships only FEED, because Task 3 is the spine: the other five engines carry
// one-line set_edge STUBS until Tasks 4-7 replace them, and a stub that moved
// audio would mean the stub is not a stub. Adding your engine's case is the
// last step of your task, not an optional extra.
#include <doctest/doctest.h>
#include "instrument.h"
#include <cmath>
#include <vector>

using namespace spky;

namespace {

void render(Instrument& inst, float* l, float* r, int n) {
    float in[64] = {};
    for (int i = 0; i < n; i += 64) inst.process(in, in, l + i, r + i, 64);
}

// THREE identical instruments, rendered in lockstep and differing only in
// what EDGE was told:
//
//   i0  never has set_voice_edge called on it at all -- the deck as it was
//       before this knob existed.
//   i1  is pushed the neutral, 0.
//   i2  is pushed a real trim, 0.9.
//
// i1 against i0 is the NEUTRAL half and it is asserted BIT FOR BIT: a knob
// whose centre is "this engine, unchanged" is the property the whole design
// rests on (spec 4.2), and it is the half that will NOT hold by accident on
// the three linear engines -- engine/util/onepole_hp.h's own warning is that
// its bottom rail is a bypass on paper and not in float32. i2 against i1 is
// the REACH half.
//
// EXTENDING THIS (Tasks 4-7): FEED is a continuously running network and
// needs nothing beyond set_engine to make sound. The four voice engines and
// the input-consuming BBD do not -- they render silence until something
// excites them. When you add your engine's case, teach this helper to drive
// it (a trigger_manual on all THREE instruments, a fed input buffer for the
// BBD) rather than reaching for the threshold below. Whatever you add has to
// reach i0 as well, or the neutral half compares two silences and passes
// vacuously.
void edge_case(EngineId eng) {
    static float a[24000], b[24000], c[24000], d[24000], e[24000], f[24000];
    // SAMPLER needs its own record memory. Instrument::init(sample_rate)
    // alone forwards an empty FxMem{}, which leaves every sampler_buf
    // pointer null -- SamplerEngine::_buf then reports !valid() and
    // load_sample() is a silent no-op (FxMem::sampler_buf's own comment:
    // "nullptr -> that part's sampler runs silent"). Passing a real FxMem
    // here does NOT turn on reverb/echo/BBD -- those pointers stay null and
    // their subsystems no-op exactly as they do under the plain
    // no-argument init(), so this is still "no FX chain" in every way that
    // matters to this test, just with a working sampler buffer. Three
    // independent buffers, one per instrument, so i0/i1/i2 cannot leak
    // record state into each other.
    std::vector<SampleBuffer::Frame> mem0, mem1, mem2;
    Instrument i0, i1, i2;
    if (eng == ENGINE_SAMPLER) {
        constexpr size_t kFrames = 48000;   // 1 s @ 48 kHz, plenty for 24000 samples
        mem0.assign(kFrames, SampleBuffer::Frame{});
        mem1.assign(kFrames, SampleBuffer::Frame{});
        mem2.assign(kFrames, SampleBuffer::Frame{});
        FxMem fx0, fx1, fx2;
        fx0.sampler_buf[PART_A] = mem0.data(); fx0.sampler_frames = kFrames;
        fx1.sampler_buf[PART_A] = mem1.data(); fx1.sampler_frames = kFrames;
        fx2.sampler_buf[PART_A] = mem2.data(); fx2.sampler_frames = kFrames;
        i0.init(48000.f, fx0);
        i1.init(48000.f, fx1);
        i2.init(48000.f, fx2);
    } else {
        i0.init(48000.f); i1.init(48000.f); i2.init(48000.f);  // no FX chain
    }
    i0.set_engine(PART_A, eng);
    i1.set_engine(PART_A, eng);
    i2.set_engine(PART_A, eng);
    // BODY, SYNTH and WAVE (and every future voice engine added here) render
    // silence until something excites them -- unlike FEED, an always-on
    // seeded network. RESO is fixed inside zone 0/1 (< 0.67) for BODY: at
    // RESO >= 0.67 EDGE is legitimately inert there (spec 4.6, the
    // sputter/ping zone's filter is not in the signal path at all), and a
    // case driven there would measure that blind spot instead of the wiring.
    if (eng == ENGINE_BODY) {
        i0.set_voice_resonance(PART_A, 0.2f);
        i1.set_voice_resonance(PART_A, 0.2f);
        i2.set_voice_resonance(PART_A, 0.2f);
    }
    // SAMPLER needs MATERIAL, not a note -- a trigger into an empty buffer
    // is still silence. A part boots in FLOW (Part::init, "lanes boot in
    // FLOW -> drone"), so once content is loaded the cloud spawns on its
    // own; no set_flow() call needed here. Same content on all three so the
    // NEUTRAL half compares like with like.
    if (eng == ENGINE_SAMPLER) {
        std::vector<float> l(4800), r(4800);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = std::sin(6.2831853f * 441.f * float(i) / 48000.f);
        r = l;
        i0.load_sample(PART_A, l.data(), r.data(), l.size());
        i1.load_sample(PART_A, l.data(), r.data(), l.size());
        i2.load_sample(PART_A, l.data(), r.data(), l.size());
    }
    /* i0: the knob is never touched */
    i1.set_voice_edge(PART_A, 0.f);          // neutral
    i2.set_voice_edge(PART_A, 0.9f);         // trimmed
    const bool needs_excitation = eng == ENGINE_BODY || eng == ENGINE_SYNTH || eng == ENGINE_WAVE;
    if (needs_excitation) {
        i0.trigger_manual(PART_A);
        i1.trigger_manual(PART_A);
        i2.trigger_manual(PART_A);
    }
    // SAMPLER only: every Part boots on ENGINE_SYNTH (Part::init) and
    // set_engine() above only ARMS the switch -- Part::process() crossfades
    // out the old engine and into the new one over two Hann ramps of 4 ms
    // each (SoftSwitch::init, fx_util.h), ~384 samples total. set_voice_edge
    // reaches SYNTH too, so for ~384 samples the still-live SYNTH drone
    // (auto-triggered by "lanes boot in FLOW", synth_engine.cpp's
    // _auto_pending) differs between i1 (EDGE 0) and i2 (EDGE 0.9),
    // measured: with the sampler's own set_edge() still a stub, diff over
    // [0, 24000) was 0.167939, entirely inside [0, 500) (probed with a
    // split-window CAPTURE, removed once this was understood) -- a REACH
    // pass with nothing behind it. A short warm-up render, discarded, lets
    // the crossfade finish so the measurement below is the SAMPLER's own
    // output, not the boot engine's tail.
    if (eng == ENGINE_SAMPLER) {
        constexpr int kWarmup = 1024;   // must stay a multiple of 64: render()
                                         // steps in fixed 64-sample blocks and
                                         // writes past a non-multiple length
        std::vector<float> wl(kWarmup), wr(kWarmup);
        render(i0, wl.data(), wr.data(), kWarmup);
        render(i1, wl.data(), wr.data(), kWarmup);
        render(i2, wl.data(), wr.data(), kWarmup);
    }
    render(i0, e, f, 24000);
    render(i1, a, b, 24000);
    render(i2, c, d, 24000);

    // 1. NEUTRAL: pushing 0 must be indistinguishable from never pushing.
    //    Exact ==, on both channels, because "unchanged" is a bit claim. An
    //    engine that cannot hold this needs an explicit bypass at t == 0, not
    //    a tolerance here -- see the plan's note and onepole_hp.h.
    int mismatch = 0;
    double worst = 0.0;
    for (int n = 0; n < 24000; ++n) {
        if (a[n] != e[n] || b[n] != f[n]) ++mismatch;
        worst = std::fmax(worst, std::fabs(double(a[n]) - double(e[n])));
        worst = std::fmax(worst, std::fabs(double(b[n]) - double(f[n])));
    }
    CAPTURE(worst);
    CHECK(mismatch == 0);                     // EDGE 0 is this engine, unchanged

    // 2. REACH: the trim must actually arrive.
    double diff = 0.0;
    for (int n = 0; n < 24000; ++n) diff += std::fabs(a[n] - c[n]);
    CAPTURE(diff);
    // NEVER lower this threshold to make a case pass. It is not a tolerance
    // on a near-miss: the two renders are either bit-identical (diff exactly
    // 0.0, which is what an unexcited engine and an unwired knob both look
    // like) or they differ by orders of magnitude more than this. A case
    // failing here means the engine is not being driven or the knob does not
    // reach it -- both are the bug this file exists to report, and both are
    // fixed above, not here.
    CHECK(diff > 1e-3);                       // the knob reaches this engine
}

}  // namespace

TEST_CASE("edge: the trim reaches FEED")    { edge_case(ENGINE_FEED); }
TEST_CASE("edge: the trim reaches BODY")    { edge_case(ENGINE_BODY); }
TEST_CASE("edge: the trim reaches SYNTH")   { edge_case(ENGINE_SYNTH); }
TEST_CASE("edge: the trim reaches WAVE")    { edge_case(ENGINE_WAVE); }
TEST_CASE("edge: the trim reaches SAMPLER") { edge_case(ENGINE_SAMPLER); }
