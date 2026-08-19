// tests/test_voice_edge_broadcast.cpp
//
// The ledger of which engines EDGE actually reaches.
//
// An engine missing from a broadcast line is a knob that is silently dead on
// that engine -- this project's oldest recurring failure (part.h says so on
// the set_voice_* block, engine_iface.h says it again about process_in/
// consumes_input). One case per engine, each asserting BOTH halves of what
// the knob promises: that EDGE at a non-zero trim CHANGES that engine's
// output (REACH), and that EDGE at 0 leaves it bit-identical to a deck whose
// knob was never touched (NEUTRAL).
//
// WHAT THE NEUTRAL HALF DOES NOT PROVE (fix-round-1 finding, Task 7): on
// every engine but FEED, i0 (set_voice_edge never called) and i1
// (set_voice_edge(0.f)) both land on the identical stored _edge == 0 through
// the identical deterministic code path -- so bit equality between them
// proves the SETTER is side-effect-free at zero, not that neutral means "no
// filter runs". A set_edge(0.f) that ran a filter at its bottom rail instead
// of skipping it would pass this exact loop too, on every engine, because i0
// and i1 would then both run that filter identically (two instances on the
// same deterministic path land on the same bits either way). Where each
// engine's REAL bypass proof -- that process() actually SKIPS the filter
// rather than running it transparently -- actually lives:
//   SYNTH and WAVE: the stored-hash gates ctrl_identity and
//     wave_formant_sweep, which compare against pre-EDGE baselines and would
//     move the moment a filter ran at boot.
//   SAMPLER: the #ifdef SPKY_TESTING state assertions in
//     tests/test_sampler_engine.cpp ("sampler: EDGE at 0 is bit-identical to
//     no EDGE at all"), reading _hp_l's own {x1, y1} history.
//   BBD: the same idiom, in tests/test_bbd_engine.cpp ("bbd engine: EDGE at
//     0 provably skips the pre-emphasis filter").
//   BODY: not applicable in the same shape -- Exciter's corner filter ALWAYS
//     runs, so t == 0 means "coefficient unchanged", pinned in
//     tests/test_body_voice.cpp ("body: EDGE at 0 leaves the exciter's
//     corner exactly where RESO put it") -- against the coefficient RESO
//     alone had already computed, not a literal named constant, because
//     BODY's neutral corner moves with RESO/character rather than sitting at
//     one fixed value the way FEED's does.
//   FEED: also always-runs, and its case (test_feed_engine.cpp, "feed: EDGE
//     at 0 is exactly kDampFixedHz") IS pinned directly against a named
//     engine constant, feed_cfg::kDampFixedHz -- FEED's neutral corner is a
//     fixed Hz, not a RESO-derived one, so it has one to pin against.
//
// This file ends the DPTH/EDGE plan with SIX cases, one per engine. Task 3
// ships only FEED, because Task 3 is the spine: the other five engines carry
// one-line set_edge STUBS until Tasks 4-7 replace them, and a stub that moved
// audio would mean the stub is not a stub. Adding your engine's case is the
// last step of your task, not an optional extra.
#include <doctest/doctest.h>
#include "instrument.h"
#include "parts/bbd_engine.h"
#include <cmath>
#include <vector>

using namespace spky;

namespace {

void render(Instrument& inst, float* l, float* r, int n) {
    float in[64] = {};
    for (int i = 0; i < n; i += 64) inst.process(in, in, l + i, r + i, 64);
}

// BBD is input-consuming (IPartEngine::consumes_input) and renders silence
// on a plain render() above -- this overload drives it with real audio,
// block by block, from a caller-supplied buffer.
void render_with_input(Instrument& inst, const float* in, float* l, float* r, int n) {
    for (int i = 0; i < n; i += 64) inst.process(in + i, in + i, l + i, r + i, 64);
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
// Tasks 4-7 (all landed): FEED is a continuously running network and needs
// nothing beyond set_engine to make sound. The four voice engines needed a
// trigger_manual on all THREE instruments; BBD, voiceless and
// input-consuming, needed a fed input buffer instead (render_with_input()
// above) -- neither excitation shortcut reaches for the REACH threshold
// below, and both had to reach i0 as well, or the neutral half would compare
// two silences and pass vacuously.
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
    // BBD needs its own LINE memory the same way SAMPLER needs record memory:
    // FxMem::bbd's own pointers default null, and BbdEngine::process() checks
    // exactly that (`if (!_buf_ok) { outL = 0.f; outR = 0.f; return; }`,
    // bbd_engine.cpp) -- an instrument built with the plain no-argument
    // init() renders a permanently silent BBD deck regardless of what
    // reaches process_in(). Three independent line pairs, one per
    // instrument, same reasoning as SAMPLER's mem0/mem1/mem2.
    static float bbd0[2][BbdEngine::kCells], bbd1[2][BbdEngine::kCells],
                 bbd2[2][BbdEngine::kCells];
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
    } else if (eng == ENGINE_BBD) {
        FxMem fx0, fx1, fx2;
        fx0.bbd[PART_A][0] = bbd0[0]; fx0.bbd[PART_A][1] = bbd0[1];
        fx1.bbd[PART_A][0] = bbd1[0]; fx1.bbd[PART_A][1] = bbd1[1];
        fx2.bbd[PART_A][0] = bbd2[0]; fx2.bbd[PART_A][1] = bbd2[1];
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
    // BBD is voiceless and input-consuming (IPartEngine::consumes_input) --
    // a trigger_manual would do nothing (BbdEngine::trigger is a no-op) and
    // an unfed deck renders silence, which would make the NEUTRAL half
    // compare two silences and the REACH half compare two more. A steady
    // tone, same content on all three so the NEUTRAL half compares like
    // with like.
    std::vector<float> bbd_in;
    if (eng == ENGINE_BBD) {
        bbd_in.resize(24000);
        for (size_t i = 0; i < bbd_in.size(); ++i)
            bbd_in[i] = std::sin(6.2831853f * 220.f * float(i) / 48000.f) * 0.4f;
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
    // SAMPLER and BBD: every Part boots on ENGINE_SYNTH (Part::init) and
    // set_engine() above only ARMS the switch -- Part::process() crossfades
    // out the old engine and into the new one over two Hann ramps of 4 ms
    // each (SoftSwitch::init, fx_util.h), ~384 samples total. set_voice_edge
    // reaches SYNTH too, so for ~384 samples the still-live SYNTH drone
    // (auto-triggered by "lanes boot in FLOW", synth_engine.cpp's
    // _auto_pending) differs between i1 (EDGE 0) and i2 (EDGE 0.9),
    // measured on SAMPLER (Task 6): with the sampler's own set_edge() still
    // a stub, diff over [0, 24000) was 0.167939, entirely inside [0, 500)
    // (probed with a split-window CAPTURE, removed once this was
    // understood) -- a REACH pass with nothing behind it. Both engines are
    // voiceless and reach Part via the identical set_engine()/soft-switch
    // path, so BBD (Task 7) gets the same short warm-up render, discarded.
    // Checked, not just copied: with the warm-up removed BBD's own REACH
    // diff read 152.858 over [0, 24000), against 207.309 with it -- both
    // orders of magnitude past the 1e-3 floor below, unlike the sampler's
    // 0.167939 boot-tail artifact. BBD's own audio input dominates the
    // measurement quickly enough that the crossfade tail never had a
    // realistic chance to be what this case was actually measuring, but the
    // warm-up costs nothing and keeps both voiceless engines' cases built
    // the same way.
    if (eng == ENGINE_SAMPLER || eng == ENGINE_BBD) {
        constexpr int kWarmup = 1024;   // must stay a multiple of 64: render()
                                         // steps in fixed 64-sample blocks and
                                         // writes past a non-multiple length
        std::vector<float> wl(kWarmup), wr(kWarmup);
        if (eng == ENGINE_BBD) {
            render_with_input(i0, bbd_in.data(), wl.data(), wr.data(), kWarmup);
            render_with_input(i1, bbd_in.data(), wl.data(), wr.data(), kWarmup);
            render_with_input(i2, bbd_in.data(), wl.data(), wr.data(), kWarmup);
        } else {
            render(i0, wl.data(), wr.data(), kWarmup);
            render(i1, wl.data(), wr.data(), kWarmup);
            render(i2, wl.data(), wr.data(), kWarmup);
        }
    }
    if (eng == ENGINE_BBD) {
        render_with_input(i0, bbd_in.data(), e, f, 24000);
        render_with_input(i1, bbd_in.data(), a, b, 24000);
        render_with_input(i2, bbd_in.data(), c, d, 24000);
    } else {
        render(i0, e, f, 24000);
        render(i1, a, b, 24000);
        render(i2, c, d, 24000);
    }

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
TEST_CASE("edge: the trim reaches BBD")     { edge_case(ENGINE_BBD); }
