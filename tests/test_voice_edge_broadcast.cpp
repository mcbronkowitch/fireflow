// tests/test_voice_edge_broadcast.cpp
//
// The ledger of which engines EDGE actually reaches.
//
// An engine missing from a broadcast line is a knob that is silently dead on
// that engine -- this project's oldest recurring failure (part.h says so on
// the set_voice_* block, engine_iface.h says it again about process_in/
// consumes_input). One case per engine, each asserting that EDGE at a
// non-zero trim CHANGES that engine's output, and that EDGE at 0 does not.
//
// This file ends the DPTH/EDGE plan with SIX cases, one per engine. Task 3
// ships only FEED, because Task 3 is the spine: the other five engines carry
// one-line set_edge STUBS until Tasks 4-7 replace them, and a stub that moved
// audio would mean the stub is not a stub. Adding your engine's case is the
// last step of your task, not an optional extra.
#include <doctest/doctest.h>
#include "instrument.h"
#include <cmath>

using namespace spky;

namespace {

void render(Instrument& inst, float* l, float* r, int n) {
    float in[64] = {};
    for (int i = 0; i < n; i += 64) inst.process(in, in, l + i, r + i, 64);
}

// Two identical instruments, one at EDGE 0 and one at EDGE 0.9, rendered in
// lockstep. Everything else is the boot state.
//
// EXTENDING THIS (Tasks 4-7): FEED is a continuously running network and
// needs nothing beyond set_engine to make sound. The four voice engines and
// the input-consuming BBD do not -- they render silence until something
// excites them. When you add your engine's case, teach this helper to drive
// it (a trigger_manual on both instruments, a fed input buffer for the BBD)
// rather than reaching for the threshold below.
void edge_case(EngineId eng) {
    static float a[24000], b[24000], c[24000], d[24000];
    Instrument i1, i2;
    i1.init(48000.f); i2.init(48000.f);      // engine only, no FX chain
    i1.set_engine(PART_A, eng); i2.set_engine(PART_A, eng);
    i1.set_voice_edge(PART_A, 0.f);          // neutral
    i2.set_voice_edge(PART_A, 0.9f);         // trimmed
    render(i1, a, b, 24000);
    render(i2, c, d, 24000);
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
