// tests/test_flow_patch_bridge.cpp
//
// The Fireflow -> flow converter (spec 2026-08-11 §5). The report is the
// deliverable: what could NOT be carried matters more than what could.
#include "doctest/doctest.h"
#include "vcv/src/flow_patch_bridge.hpp"
#include "flow/taste.h"

using namespace spky::flow;
using namespace spkyvcv;

static bool has_note_for(const TransferReport& r, int param) {
    for (int i = 0; i < r.note_count; ++i) if (r.notes[i].param == param) return true;
    return false;
}

TEST_CASE("the converter sets only base-rule parameters") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    for (int p = 0; p < P_COUNT; ++p)
        if (r.overlay.has[p]) CHECK(is_base_rule(p));
}

TEST_CASE("every unreachable parameter is reported, every time") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    // P_ROOT has no Fireflow control at all. A converter that silently left it
    // at zero would look correct and lose a third of the tonality.
    CHECK_FALSE(r.overlay.has[P_ROOT]);
    CHECK(has_note_for(r, P_ROOT));
}

TEST_CASE("the engine renumber follows Fireflow's own mapping") {
    struct Case { float knob; int engine; };
    const Case cases[] = {
        { 0.f, ENGINE_SYNTH }, { 1.f, ENGINE_SAMPLER }, { 2.f, ENGINE_WAVE },
        { 3.f, ENGINE_BODY },  { 4.f, ENGINE_BBD },
    };
    for (const Case& c : cases) {
        FireflowPatch fp{};
        fp.p[kFfEngineA] = c.knob;
        fp.p[kFfEngineB] = 0.f;              // SYNTH: a valid carrier on B
        const TransferReport r = to_flow_base(fp);
        CAPTURE(c.knob);
        REQUIRE_FALSE(r.overlay_rejected);
        CHECK(int(r.overlay.v[P_ENGINE_A] + 0.5f) == c.engine);
    }
}

TEST_CASE("the test tone does not transfer") {
    FireflowPatch fp{};
    fp.p[kFfEngineA] = 1.f;                  // the Sampler position
    fp.test_tone[0]  = true;
    const TransferReport r = to_flow_base(fp);
    // TEST_TONE is in neither kCarrierEngine nor kTextureEngine -- taste.h says
    // the generator must never roll it, so the converter must never write it.
    CHECK(int(r.overlay.v[P_ENGINE_A] + 0.5f) == ENGINE_SAMPLER);
    CHECK(has_note_for(r, P_ENGINE_A));
}

TEST_CASE("a loud pair is rejected whole and said so") {
    FireflowPatch fp{};
    fp.p[kFfEngineA] = 1.f;                  // SAMPLER
    fp.p[kFfEngineB] = 4.f;                  // BBD
    const TransferReport r = to_flow_base(fp);
    CHECK(r.overlay_rejected);
    CHECK(r.note_count > 0);
    // Nothing may be carried from a rejected transfer -- see Task 2.
    for (int p = 0; p < P_COUNT; ++p) CHECK_FALSE(r.overlay.has[p]);
}

TEST_CASE("an out-of-range tempo is clamped AND reported") {
    FireflowPatch fp{};
    fp.p[kFfEngineB] = 0.f;
    fp.p[kFfTempo]   = 180.f;                // flow's ceiling is 140
    const TransferReport r = to_flow_base(fp);
    CHECK(r.overlay.v[P_TEMPO_BPM] == doctest::Approx(kParams[P_TEMPO_BPM].hi));
    CHECK(has_note_for(r, P_TEMPO_BPM));
}

TEST_CASE("a value the runtime veto will rewrite is reported before it is heard") {
    FireflowPatch fp{};
    fp.p[kFfEngineB] = 0.f;
    fp.p[kFfCompB]   = 0.85f;                // veto band is 0.40..0.60
    const TransferReport r = to_flow_base(fp);
    CHECK(r.overlay.has[P_COMP_B]);          // it transfers...
    CHECK(has_note_for(r, P_COMP_B));        // ...and the owner is told it will not be heard
}

TEST_CASE("the report is never silently truncated") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    CHECK(r.note_count <= kMaxNotes);
    CHECK(kMaxNotes >= P_COUNT);             // one note per param is always representable
}

TEST_CASE("format_report names every note") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    const std::string s = format_report(r);
    CHECK(s.find("ROOT") != std::string::npos);
    CHECK_FALSE(s.empty());
}
