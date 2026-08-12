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

TEST_CASE("the veto rewrite is called out only when the value is out of band") {
    // The case above does NOT exercise the rewrite branch: LVL 0.85 converts to
    // 0.528, which is INSIDE 0.40..0.60, so nothing is rewritten and the note
    // says something else. Both ends of the LVL travel land outside the band,
    // and that is where the wording has to change -- otherwise the branch that
    // names the loudest loss in this file has no gate at all.
    struct Case { float lvl; bool rewritten; };
    const Case cases[] = {
        { 0.00f, true  },   // comp 0.00, below the band -> forced up to 0.40
        { 0.60f, true  },   // the split itself: still 0.00
        { 0.85f, false },   // comp 0.528, in band
        { 1.00f, true  },   // comp 0.700, above the band -> forced down to 0.60
    };
    for (const Case& c : cases) {
        FireflowPatch fp{};
        fp.p[kFfEngineB] = 0.f;
        fp.p[kFfCompB]   = c.lvl;
        const TransferReport r = to_flow_base(fp);
        const std::string s = format_report(r);
        CAPTURE(c.lvl);
        CHECK(has_note_for(r, P_COMP_B));
        CHECK((s.find("REWRITTEN AT RUNTIME") != std::string::npos) == c.rewritten);
    }
}

TEST_CASE("the four transforming conversions store what Fireflow pushed") {
    // These four are the ones the map singles out as "the exact shape of the
    // four conversion changes that silently moved the factory sound": each has
    // a knob whose value is NOT the engine's value. A converter that copied the
    // knob would still look plausible in every other test in this file.
    FireflowPatch fp{};
    fp.p[kFfEngineB] = 0.f;
    fp.p[kFfCompB]   = 0.85f;   // LVL, split at 0.6 then a 0.6-power curve
    fp.p[kFfCouple]  = 0.75f;   // GRID half of the zone split, rescaled to 0.5
    fp.p[kFfChoke]   = -2.f;    // the -2..+2 switch, halved onto flow's -1..1
    fp.p[kFfModA]    = 0.42f;   // the knob printed MOD -- it is DEPTH
    fp.p[kFfModB]    = 0.17f;
    fp.p[kFfMelodyA] = 0.99f;   // the neighbouring knob, to catch an index slip
    const TransferReport r = to_flow_base(fp);
    REQUIRE_FALSE(r.overlay_rejected);

    // 0.7 * pow((0.85 - 0.6) / 0.4, 0.6), computed off the map's formula.
    // NOT 0.85: copying the knob here is the trap this case exists for.
    CHECK(r.overlay.v[P_COMP_B] == doctest::Approx(0.52799043f));
    // (0.75 - 0.5) / 0.5, the rescaled half-zone -- not the 0.75 knob position.
    CHECK(r.overlay.v[P_COUPLE] == doctest::Approx(0.5f));
    // -2 * 0.5, the five snapped states landing on flow's -1..1.
    CHECK(r.overlay.v[P_CHOKE] == doctest::Approx(-1.f));
    CHECK(r.overlay.v[P_DEPTH_A] == doctest::Approx(0.42f));
    CHECK(r.overlay.v[P_DEPTH_B] == doctest::Approx(0.17f));
}

TEST_CASE("the report is never silently truncated") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    CHECK(r.note_count <= kMaxNotes);
    CHECK(kMaxNotes >= P_COUNT);             // one note per param is always representable
}

TEST_CASE("an overfull report says so instead of losing the tail") {
    // The two checks above cannot fail: the push guards itself, and kMaxNotes
    // is P_COUNT + 8 by definition. The BEHAVIOUR they are meant to stand for
    // -- what happens when the array does fill -- is this. No converter input
    // can reach it today (a busy patch emits about a dozen notes), so the sink
    // is driven directly.
    TransferReport r;
    spkyvcv::detail::NoteSink sink(r);
    for (int i = 0; i < kMaxNotes + 5; ++i) sink.note(i, "filler");
    REQUIRE(r.note_count == kMaxNotes);
    const TransferNote before_finish = r.notes[kMaxNotes - 1];
    REQUIRE(before_finish.param == kMaxNotes - 1);   // a real note occupies it

    sink.finish();
    // The last slot is spent on the truth about the overflow, displacing the
    // last note that happened to fit. Truncating in silence is what this costs.
    CHECK(r.notes[kMaxNotes - 1].param == kNoteGeneral);
    CHECK(std::string(r.notes[kMaxNotes - 1].reason).find("REPORT FULL") !=
          std::string::npos);
    CHECK(r.notes[kMaxNotes - 1].param != before_finish.param);
}

TEST_CASE("format_report names every note") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    const std::string s = format_report(r);
    CHECK(s.find("ROOT") != std::string::npos);
    CHECK_FALSE(s.empty());
    // ...and every OTHER note too: naming only the first one would pass the
    // line above while dropping the rest of the losses on the floor.
    REQUIRE(r.note_count > 1);
    for (int i = 0; i < r.note_count; ++i) {
        CAPTURE(i);
        const int p = r.notes[i].param;
        const std::string label = (p >= 0 && p < P_COUNT)
            ? std::string(kParams[p].name).substr(2)   // kParams keeps the P_ prefix
            : std::string("(patch)");
        CHECK(s.find(label) != std::string::npos);
        CHECK(s.find(r.notes[i].reason) != std::string::npos);
    }
}
