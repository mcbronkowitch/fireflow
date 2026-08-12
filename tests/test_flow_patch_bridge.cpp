// tests/test_flow_patch_bridge.cpp
//
// The Fireflow -> flow converter (spec 2026-08-11 §5). The report is the
// deliverable: what could NOT be carried matters more than what could.
#include "doctest/doctest.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>
#include "vcv/src/flow_patch_bridge.hpp"
#include "vcv/src/glow_ui.hpp"
#include "vcv/src/touch_pads.hpp"
#include "flow/taste.h"

using namespace spky::flow;
using namespace spkyvcv;

static bool has_note_for(const TransferReport& r, int param) {
    for (int i = 0; i < r.note_count; ++i) if (r.notes[i].param == param) return true;
    return false;
}

// Does THIS parameter's note carry `tag`? Searching format_report()'s whole
// text for a tag was enough while each tag occurred once; it is not any more.
// P_TEMPO_BPM now carries an unconditional "REWRITTEN AT RUNTIME" note of its
// own (Glow's TEMPO fader overwrites the carried tempo every control tick), so
// a whole-report search for that tag answers "yes" on every patch and the
// COMP_B branch below would have no gate left at all.
static bool note_for_has(const TransferReport& r, int param, const char* tag) {
    for (int i = 0; i < r.note_count; ++i)
        if (r.notes[i].param == param && r.notes[i].reason &&
            std::string(r.notes[i].reason).find(tag) != std::string::npos)
            return true;
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
    // TEMPO is a 0..1 KNOB, not a BPM: Fireflow computes bpm = 40 + knob * 200
    // (Fireflow.cpp:919), so the knob's travel is 40..240 against flow's
    // 50..140 and BOTH ends run off the range. Writing a BPM into p[kFfTempo]
    // here would still pass -- and would be the exact units confusion this
    // file's whole thesis is written against.
    FireflowPatch top{};
    top.p[kFfEngineB] = 0.f;
    top.p[kFfTempo]   = 1.f;                 // knob fully up = 240 BPM
    const TransferReport rt = to_flow_base(top);
    CHECK(rt.overlay.v[P_TEMPO_BPM] == doctest::Approx(kParams[P_TEMPO_BPM].hi));
    CHECK(has_note_for(rt, P_TEMPO_BPM));

    FireflowPatch bottom{};
    bottom.p[kFfEngineB] = 0.f;
    bottom.p[kFfTempo]   = 0.f;              // knob fully down = 40 BPM
    const TransferReport rb = to_flow_base(bottom);
    CHECK(rb.overlay.v[P_TEMPO_BPM] == doctest::Approx(kParams[P_TEMPO_BPM].lo));
    CHECK(has_note_for(rb, P_TEMPO_BPM));
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
        CAPTURE(c.lvl);
        CHECK(has_note_for(r, P_COMP_B));
        CHECK(note_for_has(r, P_COMP_B, "REWRITTEN AT RUNTIME") == c.rewritten);
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

TEST_CASE("bridge: a patch carries its own pace and its deck-A compression") {
    FireflowPatch fp{};
    fp.p[kFfEngineB] = 0.f;    // SYNTH: a carrier, so nothing is rejected
    fp.p[kFfPace]    = 0.25f;
    fp.p[kFfCompA]   = 0.55f;  // below the 0.6 split -> comp 0, outside the band
    const TransferReport r = to_flow_base(fp);
    REQUIRE_FALSE(r.overlay_rejected);

    CHECK(r.overlay.has[P_PACE]);
    CHECK(r.overlay.v[P_PACE] == doctest::Approx(0.25f));

    CHECK(r.overlay.has[P_COMP_A]);
    // COMP_A is veto-confined to 0.40-0.60, so a carried value the runtime
    // moves must be tagged, exactly as COMP_B's row already is.
    CHECK(note_for_has(r, P_COMP_A, "REWRITTEN AT RUNTIME"));
}

TEST_CASE("bridge: GRIT carries the magnitude Fireflow pushed, and says what is lost") {
    // These two are the same trap as the four-conversion case above, plus a
    // loss the formula itself cannot carry at all: the sign.
    FireflowPatch fp{};
    fp.p[kFfEngineB] = 0.f;
    fp.p[kFfGritA]   = 0.5f;    // positive: Drive, and the magnitude transfers
    fp.p[kFfGritB]   = -0.5f;   // negative: Reduce, and only the sign is lost
    const TransferReport r = to_flow_base(fp);
    REQUIRE_FALSE(r.overlay_rejected);

    // (0.5 - 0.03) / (1 - 0.03), Fireflow.cpp's own dead-zone formula
    // (kGritDead = 0.03). NOT 0.5: copying the knob is the trap every
    // formula row in this file exists against.
    CHECK(r.overlay.v[P_GRIT_A] == doctest::Approx(0.48453608f));
    // Same magnitude, sign discarded -- GRIT_B's knob is negative.
    CHECK(r.overlay.v[P_GRIT_B] == doctest::Approx(0.48453608f));

    CHECK(note_for_has(r, P_GRIT_A, "inaudible under Glow"));
    CHECK(note_for_has(r, P_GRIT_B, "inaudible under Glow"));
    CHECK_FALSE(note_for_has(r, P_GRIT_A, "NOT CARRIED"));   // A's sign was positive
    CHECK(note_for_has(r, P_GRIT_B, "NOT CARRIED"));         // B's sign is lost
}

TEST_CASE("bridge: a plain patch still names the deck BALANCE it cannot carry") {
    // The fact the old, single "P_COMP_A is story-owned" note used to carry
    // alongside its own loss -- now that COMP_A has a destination and a note
    // of its own, this must not have quietly vanished with it.
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    REQUIRE_FALSE(r.overlay_rejected);
    CHECK(note_for_has(r, kNoteGeneral, "BALANCE"));
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

// ---------------------------------------------------------------------------
// What the menu shows, what the clipboard carries, what a paste decides (Task 8)
// ---------------------------------------------------------------------------
//
// The two menu items themselves are Rack widget code and no doctest compiles
// them. Everything about them that is NOT a Rack question lives in
// flow_patch_bridge.hpp so that it can be gated here: the line wrapping, the
// choice of which notes a menu is short enough to show, the clipboard string
// that has to carry a report and still paste, and the paste decision itself.

static std::vector<std::string> words_of(const std::string& s) {
    std::vector<std::string> w;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        const std::size_t start = i;
        while (i < s.size() && s[i] != ' ' && s[i] != '\n') ++i;
        if (i > start) w.push_back(s.substr(start, i - start));
        while (i < s.size() && s[i] == '\n') ++i;
    }
    return w;
}

static std::string join_lines(const std::vector<std::string>& lines) {
    std::string s;
    for (const std::string& l : lines) { s += l; s += "\n"; }
    return s;
}

TEST_CASE("wrapping carries every word of the report") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    const std::string text = format_report(r);
    const std::vector<std::string> lines = spkyvcv::detail::wrap_lines(text, 64);

    std::vector<std::string> joined;
    for (const std::string& l : lines)
        for (const std::string& w : words_of(l)) joined.push_back(w);
    // Nothing dropped, nothing reordered, nothing cut in half. A wrapper that
    // truncates the long notes would look tidy in the menu and would be lying
    // about the transfer, which is the one thing this whole file exists against.
    CHECK(joined == words_of(text));
}

TEST_CASE("wrapping leaves no empty lines") {
    // format_report terminates EVERY line with '\n', so the obvious split
    // leaves a trailing empty piece -- and an empty MenuLabel at the bottom of
    // the menu, which draws as a blank row nobody can explain.
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    const std::vector<std::string> lines =
        spkyvcv::detail::wrap_lines(format_report(r), 64);
    REQUIRE_FALSE(lines.empty());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        CAPTURE(i);
        CHECK_FALSE(lines[i].empty());
        CHECK(lines[i].find_first_not_of(' ') != std::string::npos);
    }
}

TEST_CASE("wrapping respects the column width it was given") {
    // Rack's MenuLabel::step() widens the menu to fit its text, so ONE 300-char
    // note would drag the context menu off the screen. A line may only exceed
    // the limit when it holds a SINGLE word -- a word that did not fit gets to
    // overflow rather than be cut (see the word test above). Note that such a
    // word need not itself be longer than `columns`: it also overflows when the
    // continuation indent plus the word is what does not fit.
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    const std::string text = format_report(r);
    for (int columns : { 40, 64, 100 }) {
        CAPTURE(columns);
        const std::vector<std::string> lines =
            spkyvcv::detail::wrap_lines(text, columns);
        for (const std::string& l : lines) {
            if (int(l.size()) <= columns) continue;
            CAPTURE(l);
            CHECK(words_of(l).size() == 1u);
        }
    }
    // ...and the wrapping really happens: at 40 columns the report cannot
    // possibly be as few lines as at 100.
    CHECK(spkyvcv::detail::wrap_lines(text, 40).size() >
          spkyvcv::detail::wrap_lines(text, 100).size());
}

TEST_CASE("the wrapper clamps a silly width, and the menu's default is 64") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    const std::string text = format_report(r);
    // Below the clamp every width behaves as 24. A column of single words is
    // not a report, so the wrapper refuses to go narrower rather than obeying.
    CHECK(spkyvcv::detail::wrap_lines(text, 24) ==
          spkyvcv::detail::wrap_lines(text, 23));
    CHECK(spkyvcv::detail::wrap_lines(text, 24) ==
          spkyvcv::detail::wrap_lines(text, 1));
    CHECK(spkyvcv::detail::wrap_lines(text, 24) ==
          spkyvcv::detail::wrap_lines(text, -5));
    // The default argument, which is the only width Fireflow.cpp ever passes:
    // nothing else in this suite would notice if it changed.
    CHECK(report_summary_lines(r) == report_summary_lines(r, 64));
}

// --- the summary: which notes are short enough to put in a menu -------------

TEST_CASE("the menu summary shows the heavy notes and counts the rest") {
    FireflowPatch fp{};
    fp.p[kFfStepsA] = 4.f;   // deck A stepped while COUPLE is free -> LOSSY
    const TransferReport r = to_flow_base(fp);

    int severe = 0;
    for (int i = 0; i < r.note_count; ++i)
        if (severe_tag(r.notes[i].reason)) ++severe;
    REQUIRE(severe > 0);
    REQUIRE(severe < r.note_count);   // ...so it really is a SELECTION

    // Short enough to sit in a context menu at all, which is the entire point
    // of having a summary: the full report runs to about a hundred rows.
    const std::vector<std::string> shown = report_summary_lines(r, 64);
    CHECK(shown.size() <= 16u);
    CHECK(shown.size() <
          spkyvcv::detail::wrap_lines(format_report(r), 64).size());

    // Read unwrapped, so these substring checks cannot be defeated by a line
    // break landing in the middle of one.
    const std::string flat = join_lines(report_summary_lines(r, 200));
    for (int i = 0; i < r.note_count; ++i) {
        const char* tag = severe_tag(r.notes[i].reason);
        if (!tag) continue;
        CAPTURE(i);
        CHECK(flat.find(spkyvcv::detail::note_label(r.notes[i].param) + ": " + tag)
              != std::string::npos);
    }
    // The count is the safety net: a tag renamed in to_flow_base without being
    // renamed in kSevereTags cannot hide a loss, because the note it stops
    // matching moves into this number instead of vanishing.
    char expect[64];
    std::snprintf(expect, sizeof expect, "%d of %d notes shown",
                  severe, r.note_count);
    CHECK(flat.find(expect) != std::string::npos);
    CHECK(flat.find("clipboard") != std::string::npos);
}

TEST_CASE("the summary names the losses by hand, not by its own rule") {
    // The test above computes what it expects THROUGH severe_tag, so it agrees
    // with kSevereTags however that table is edited -- deleting an entry keeps
    // it green while the menu quietly stops mentioning that loss. These are the
    // notes a plain patch really has, spelled out, so the table cannot shrink
    // without something going red.
    FireflowPatch plain{};
    const std::string flat = join_lines(report_summary_lines(to_flow_base(plain), 200));
    CHECK(flat.find("ROOT: UNREACHABLE") != std::string::npos);
    CHECK(flat.find("TEMPO_BPM: CLAMPED") != std::string::npos);
    CHECK(flat.find("COMP_B: REWRITTEN AT RUNTIME") != std::string::npos);
    // ...and the structural notes stay OUT of the menu: they say flow has no
    // slot for a control and never had, which is a fact about the two
    // instruments rather than about the patch in hand.
    CHECK(flat.find("NO DESTINATION") == std::string::npos);
    CHECK(flat.find("NOT TRANSFERABLE") == std::string::npos);

    FireflowPatch mixed{};
    mixed.p[kFfStepsA] = 4.f;
    const std::string mflat =
        join_lines(report_summary_lines(to_flow_base(mixed), 200));
    CHECK(mflat.find("MODE: LOSSY") != std::string::npos);

    // The last two tags in kSevereTags, and the two no case above reaches. Both
    // were deletable from the table with the whole suite still green: the
    // "rejected says so in its first menu line" case below reads lines[0],
    // which comes from report_head and not from the tag table, and REPORT FULL
    // has no input that produces it at all. Both notes are kNoteGeneral, so the
    // label is "(patch)".
    FireflowPatch loud{};
    loud.p[kFfEngineA] = 1.f;   // SAMPLER on both decks: no carrier anywhere
    loud.p[kFfEngineB] = 1.f;
    const std::string lflat =
        join_lines(report_summary_lines(to_flow_base(loud), 200));
    CHECK(lflat.find("(patch): REJECTED WHOLE") != std::string::npos);

    TransferReport full;        // REPORT FULL: only an overflowing sink writes it
    spkyvcv::detail::NoteSink sink(full);
    for (int i = 0; i < kMaxNotes + 5; ++i) sink.note(i, "filler");
    sink.finish();
    const std::string fflat = join_lines(report_summary_lines(full, 200));
    CHECK(fflat.find("(patch): REPORT FULL") != std::string::npos);
}

TEST_CASE("every severity tag is one the converter actually writes") {
    // kSevereTags is a table of string prefixes matched against prose written
    // somewhere else in the same header. Renaming a tag at the write site and
    // not here would quietly empty the menu summary out; this is what notices.
    std::vector<std::string> reasons;
    const auto collect = [&reasons](const TransferReport& rep) {
        for (int i = 0; i < rep.note_count; ++i)
            if (rep.notes[i].reason) reasons.push_back(rep.notes[i].reason);
    };

    FireflowPatch plain{};
    collect(to_flow_base(plain));   // UNREACHABLE, CLAMPED, REWRITTEN AT RUNTIME

    FireflowPatch mixed{};
    mixed.p[kFfStepsA] = 4.f;
    collect(to_flow_base(mixed));   // LOSSY

    FireflowPatch loud{};
    loud.p[kFfEngineA] = 1.f;
    loud.p[kFfEngineB] = 1.f;
    collect(to_flow_base(loud));    // REJECTED WHOLE

    TransferReport full;            // REPORT FULL: no input can reach it
    spkyvcv::detail::NoteSink sink(full);
    for (int i = 0; i < kMaxNotes + 5; ++i) sink.note(i, "filler");
    sink.finish();
    collect(full);

    for (int t = 0; t < kSevereTagCount; ++t) {
        CAPTURE(kSevereTags[t]);
        bool found = false;
        for (const std::string& why : reasons)
            if (why.rfind(kSevereTags[t], 0) == 0) { found = true; break; }
        CHECK(found);
    }
}

TEST_CASE("a rejected transfer says so in its first menu line") {
    // The one report a player most needs to see before pasting.
    FireflowPatch fp{};
    fp.p[kFfEngineA] = 1.f;   // SAMPLER
    fp.p[kFfEngineB] = 1.f;   // SAMPLER -- no carrier on either deck
    const TransferReport r = to_flow_base(fp);
    REQUIRE(r.overlay_rejected);
    const std::vector<std::string> lines = report_summary_lines(r, 64);
    REQUIRE_FALSE(lines.empty());
    CHECK(lines[0].find("REJECTED") != std::string::npos);
}

// --- the clipboard: an overlay and a report in one string -------------------

TEST_CASE("the complete clipboard string pastes back") {
    // THE gate for carrying the report in the clipboard at all. Round-tripping
    // encode_base's half would prove nothing -- the question is whether the
    // string a user actually copies, comment block and all, still decodes.
    FireflowPatch fp{};
    fp.p[kFfTuneA] = 0.25f;
    fp.p[kFfEngineA] = 0.f;   // SYNTH: a carrier, so nothing is rejected
    const TransferReport r = to_flow_base(fp);
    REQUIRE_FALSE(r.overlay_rejected);

    const std::string clip = clipboard_base(r);
    CHECK(clip.find('#') != std::string::npos);            // it has both halves
    CHECK(clip.find("UNREACHABLE") != std::string::npos);

    spky::flow::BaseOverlay out;
    REQUIRE(decode_base(clip.c_str(), out));
    for (int p = 0; p < P_COUNT; ++p) {
        CAPTURE(p);
        CHECK(out.has[p] == r.overlay.has[p]);
        if (out.has[p]) CHECK(out.v[p] == doctest::Approx(r.overlay.v[p]));
    }
    // ...and through the decision that actually consumes it on the Glow side.
    spky::flow::BaseOverlay pad;
    bool has = false;
    REQUIRE(base_for_pad(clip.c_str(), pad, has));
    CHECK(has);
}

TEST_CASE("the clipboard carries every note, not just the heavy ones") {
    // The summary is a selection; this is where the notes it left out have to
    // still be, or moving the report out of the menu lost them.
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    const std::string clip = clipboard_base(r);
    REQUIRE(r.note_count > 1);
    for (int i = 0; i < r.note_count; ++i) {
        CAPTURE(i);
        CHECK(clip.find(r.notes[i].reason) != std::string::npos);
    }
}

TEST_CASE("a rejected transfer's clipboard string pastes as an empty base") {
    // Its overlay half is the empty string, so the whole payload is nothing but
    // comments -- the one case where the '#' skipping has to carry the entire
    // parse rather than just a tail.
    FireflowPatch fp{};
    fp.p[kFfEngineA] = 1.f;
    fp.p[kFfEngineB] = 1.f;
    const TransferReport r = to_flow_base(fp);
    REQUIRE(r.overlay_rejected);
    const std::string clip = clipboard_base(r);
    CHECK(clip.find("REJECTED") != std::string::npos);

    spky::flow::BaseOverlay out;
    bool has = true;
    REQUIRE(base_for_pad(clip.c_str(), out, has));
    CHECK_FALSE(has);        // the pad is cleared, not left claiming a patch
}

TEST_CASE("a comment does not make a malformed base decode") {
    // The extension may not become a loophole: '#' skipping must not let a
    // broken token through by swallowing the evidence.
    spky::flow::BaseOverlay out;
    CHECK_FALSE(decode_base("P_TUNE_A0.5;\n# a note: with; punctuation\n", out));
    CHECK_FALSE(decode_base("# only a comment\nP_TUNE_A:zzz;\n", out));
    // ...while the same string with the token intact does decode.
    CHECK(decode_base("# a note: with; punctuation\nP_TUNE_A:0.5;\n", out));
    CHECK(out.has[P_TUNE_A]);
}

// --- the paste decision -----------------------------------------------------

TEST_CASE("base_for_pad: no clipboard at all leaves the pad alone") {
    spky::flow::BaseOverlay out;
    bool has = true;
    CHECK_FALSE(base_for_pad(nullptr, out, has));
}

TEST_CASE("base_for_pad: a terrain code leaves the pad alone") {
    // The likeliest wrong clipboard by far: the same menu offers "Copy terrain
    // code" a few rows up, and both are strings a player pastes onto pads.
    spky::flow::BaseOverlay out;
    bool has = true;
    CHECK_FALSE(base_for_pad(kHouseCode, out, has));
    CHECK_FALSE(base_for_pad("not a base at all", out, has));
}

TEST_CASE("base_for_pad: the empty base is accepted, and clears the pad") {
    // The subtle one, and the reason this function exists rather than an
    // inline decode. "" decodes CLEANLY -- it means "no hand-authored patch" --
    // so the honest answer is accept-and-clear. Recording it as has = true
    // would hand wakePad a pointer to an overlay with nothing in it.
    spky::flow::BaseOverlay out;
    bool has = true;
    CHECK(base_for_pad("", out, has));
    CHECK_FALSE(has);
    for (int p = 0; p < P_COUNT; ++p) CHECK_FALSE(out.has[p]);
}

TEST_CASE("base_for_pad: a real base is accepted whole") {
    spky::flow::BaseOverlay ov;
    ov.v[P_TUNE_A] = 0.25f;  ov.has[P_TUNE_A] = true;
    ov.v[P_RES_B]  = 0.5f;   ov.has[P_RES_B]  = true;
    REQUIRE(is_base_rule(P_TUNE_A));
    REQUIRE(is_base_rule(P_RES_B));

    spky::flow::BaseOverlay out;
    bool has = false;
    REQUIRE(base_for_pad(encode_base(ov).c_str(), out, has));
    CHECK(has);
    CHECK(out.has[P_TUNE_A]);
    CHECK(out.v[P_TUNE_A] == doctest::Approx(0.25f));
    CHECK(out.has[P_RES_B]);
    CHECK(out.v[P_RES_B] == doctest::Approx(0.5f));
}

// ---------------------------------------------------------------------------
// The one textual encoding, and the place that carries it (Task 7)
// ---------------------------------------------------------------------------

TEST_CASE("Place stays trivially copyable after growing") {
    // Glow.cpp memcpys the whole Place array to the audio thread as one staged
    // handover (UiOp::SET_PLACES). A heap-owning member would put a malloc
    // there, for a patch somebody pasted.
    CHECK(std::is_trivially_copyable<spkyvcv::Place>::value);
}

TEST_CASE("the pool row carries the base and stays one line per place") {
    spkyvcv::Place places[2] = {};
    std::snprintf(places[0].code, sizeof places[0].code, "F1-00000020-000000000000");
    spkyvcv::set_label(places[0].name, spkyvcv::kNameCap, "opener");
    places[0].has_base = true;
    places[0].base.v[spky::flow::P_TUNE_A] = 0.25f;
    places[0].base.has[spky::flow::P_TUNE_A] = true;

    const std::string tsv = spkyvcv::export_pool_tsv(places, 2);
    // Header plus one row per place, and not one newline more: a base written
    // as its own line would make the file unreadable by the §10.3 generator.
    CHECK(std::count(tsv.begin(), tsv.end(), '\n') == 3);
    CHECK(tsv.find("opener") != std::string::npos);
    // ...and the base is actually IN the row. Without this the case above
    // passes on an export that dropped the column entirely, which is the one
    // failure it is named for.
    CHECK(tsv.find("base\n") != std::string::npos);          // the header column
    CHECK(tsv.find(encode_base(places[0].base)) != std::string::npos);
    // The second place has none, and an absent base is an EMPTY field rather
    // than a zero patch: its row ends on the column separator.
    CHECK(tsv.find("\t2\t\t\t\n") != std::string::npos);      // pad, name, note, base
}

TEST_CASE("a place with no base is distinguishable from one with a zero base") {
    spkyvcv::Place p{};
    CHECK_FALSE(p.has_base);
    // A default Place must not claim to carry a patch: wakePad passes
    // nullptr for it, and Flow::wake(s, nullptr) plays the drawn terrain.
}

TEST_CASE("an overlay survives the text round trip for every base-rule param") {
    spky::flow::BaseOverlay in;
    for (int i = 0; i < kBaseRuleCount; ++i) {
        const int p = kBaseRules[i].param;
        in.v[p]   = kParams[p].lo + 0.25f * (kParams[p].hi - kParams[p].lo);
        in.has[p] = true;
    }
    spky::flow::BaseOverlay out;
    REQUIRE(decode_base(encode_base(in).c_str(), out));
    for (int p = 0; p < P_COUNT; ++p) {
        CAPTURE(p);
        CHECK(out.has[p] == in.has[p]);
        if (in.has[p]) CHECK(out.v[p] == doctest::Approx(in.v[p]));
    }
}

TEST_CASE("a malformed base string is rejected, not half-read") {
    spky::flow::BaseOverlay in;
    in.v[P_TUNE_A] = 0.5f; in.has[P_TUNE_A] = true;
    in.v[P_RES_B]  = 0.3f; in.has[P_RES_B]  = true;
    std::string s = encode_base(in);
    s.erase(s.find(';'), 1);                 // splice two pairs into one token
    spky::flow::BaseOverlay out;
    CHECK_FALSE(decode_base(s.c_str(), out));
    for (int p = 0; p < P_COUNT; ++p) CHECK_FALSE(out.has[p]);
}

TEST_CASE("an empty base string decodes to no overlay, not a zero one") {
    spky::flow::BaseOverlay out;
    out.has[P_TUNE_A] = true;                // pre-dirty it
    CHECK(decode_base("", out));             // empty is VALID: a place with no patch
    for (int p = 0; p < P_COUNT; ++p) CHECK_FALSE(out.has[p]);
}

TEST_CASE("a story-owned parameter is rejected, not silently dropped") {
    // decode_base is the entry point for hand-edited pool.tsv rows, clipboard
    // text and older patches -- text nobody's converter wrote. generate()
    // honours base rules and nothing else, so an entry for a story-owned
    // parameter is a string that claims to carry more than it can. Rejecting
    // it is the same all-or-nothing rule the partial-parse case gets, and it
    // asks taste.h (is_base_rule) rather than a transcribed list, exactly as
    // set_base does on the way in -- one authority for both directions.
    int story = -1;
    for (int p = 0; p < P_COUNT && story < 0; ++p) if (!is_base_rule(p)) story = p;
    REQUIRE(story >= 0);                     // 25 of 63 are story-owned
    CAPTURE(std::string(kParams[story].name));   // a const char* prints as a pointer
    const std::string s = std::string(kParams[story].name) + ":0.5;";
    spky::flow::BaseOverlay out;
    CHECK_FALSE(decode_base(s.c_str(), out));
    for (int p = 0; p < P_COUNT; ++p) CHECK_FALSE(out.has[p]);

    // ...and the rejection is about the PARTITION, not about the syntax: the
    // same string shape built on a base rule decodes.
    int base = -1;
    for (int p = 0; p < P_COUNT && base < 0; ++p) if (is_base_rule(p)) base = p;
    REQUIRE(base >= 0);
    CAPTURE(std::string(kParams[base].name));
    const std::string ok = std::string(kParams[base].name) + ":0.5;";
    spky::flow::BaseOverlay out2;
    CHECK(decode_base(ok.c_str(), out2));
    CHECK(out2.has[base]);
}

// ---------------------------------------------------------------------------
// The live place's base, which is NOT in the twelve
// ---------------------------------------------------------------------------
//
// Place::base covers the pads. The place actually PLAYING does not live in
// that array -- Flow holds it -- and neither does the undo slot's, so both
// travel in GlowSave. Without these two cases a reload restores every pad's
// base and loses the one being heard.

TEST_CASE("the live base and the slot's survive a capture and a restore") {
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);

    TerrainState house;
    REQUIRE(decode_code(kHouseCode, house));
    spky::flow::BaseOverlay ov;
    ov.v[P_TUNE_A] = 0.25f; ov.has[P_TUNE_A] = true;
    fl.wake(house, &ov);
    REQUIRE(fl.new_full());                  // fills the undo slot

    const GlowSave s = glow_capture(fl);
    REQUIRE(s.have_base);
    CHECK(s.base.has[P_TUNE_A]);
    CHECK(s.base.v[P_TUNE_A] == doctest::Approx(0.25f));
    // Nothing is asserted about s.have_undo_base here on purpose. glow_capture
    // assigns it from have_base two lines above its own return, so a check
    // would restate the assignment and read as coverage it is not. The slot's
    // overlay is gated by the case below, which is the only one that can see
    // it: restore_undo writes _undo_overlay, and Flow exposes that through no
    // accessor -- only an undo() can bring it back out.

    spky::Instrument inst2;
    inst2.init(48000.f);
    spky::flow::Flow fl2;
    fl2.init(&inst2, 100.f);
    REQUIRE(glow_restore(fl2, s));
    REQUIRE(fl2.overlay() != nullptr);
    CHECK(fl2.overlay()->has[P_TUNE_A]);
    CHECK(fl2.overlay()->v[P_TUNE_A] == doctest::Approx(0.25f));
    // The restored terrain is the one that was saved, base and all: an overlay
    // that arrived after the wake would render a different terrain from the
    // one the player heard.
    CHECK(fl2.state().master == fl.state().master);
}

TEST_CASE("a payload with no base restores NO overlay, not an empty one") {
    // The other half of "has_base false is not an all-zero patch", one level
    // up from Place: a patch saved before this round has no base key, and a
    // Flow that is already carrying one must be left playing its terrain as
    // drawn rather than under a zeroed patch.
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);
    TerrainState house;
    REQUIRE(decode_code(kHouseCode, house));
    spky::flow::BaseOverlay ov;
    ov.v[P_TUNE_A] = 0.25f; ov.has[P_TUNE_A] = true;
    fl.wake(house, &ov);
    REQUIRE(fl.overlay() != nullptr);

    GlowSave bare;                           // as an older patch decodes
    encode_code(house, bare.code, int(sizeof bare.code));
    REQUIRE_FALSE(bare.have_base);
    REQUIRE(glow_restore(fl, bare));
    CHECK(fl.overlay() == nullptr);
}

TEST_CASE("the slot's own base is restored, not the live one") {
    // The discriminating gate for glow_restore's THIRD argument to
    // restore_undo. Every other case here passes the same overlay on both
    // sides -- glow_capture cannot produce a divergent pair -- so dropping
    // that argument leaves them all green while the undo slot silently
    // inherits whatever wake() just set. A saved patch CAN hold two different
    // strings, so the pair is hand-built here.
    //
    // restore_undo writes _undo_overlay and nothing else, and Flow exposes it
    // through no accessor. undo() is what brings it back out: it is legal
    // after the restore (_woken from wake, _have_undo from restore_undo), and
    // it swaps the slot's pair into the live one.
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);

    TerrainState house;
    REQUIRE(decode_code(kHouseCode, house));

    GlowSave s;
    encode_code(house, s.code, int(sizeof s.code));
    TerrainState other = house;
    ++other.reroll[0];                       // a different place for the slot
    encode_code(other, s.undo, int(sizeof s.undo));
    s.have_undo = true;
    s.base.v[P_TUNE_A] = 0.25f;  s.base.has[P_TUNE_A] = true;       // A
    s.have_base = true;
    s.undo_base.v[P_TUNE_B] = 0.75f; s.undo_base.has[P_TUNE_B] = true;  // B
    s.have_undo_base = true;

    REQUIRE(glow_restore(fl, s));
    REQUIRE(fl.overlay() != nullptr);
    REQUIRE(fl.overlay()->has[P_TUNE_A]);    // the live one is A
    REQUIRE_FALSE(fl.overlay()->has[P_TUNE_B]);

    REQUIRE(fl.can_undo());
    REQUIRE(fl.undo());
    REQUIRE(fl.overlay() != nullptr);
    // ...and undoing lands on B, the pair the SLOT was saved with. Without the
    // third argument the slot would have inherited A from the wake above and
    // this is where the wrong base shows up -- one gesture after the reload,
    // which is exactly how Task 3's bug presented.
    CHECK(fl.overlay()->has[P_TUNE_B]);
    CHECK(fl.overlay()->v[P_TUNE_B] == doctest::Approx(0.75f));
    CHECK_FALSE(fl.overlay()->has[P_TUNE_A]);
}

TEST_CASE("the encoder cannot emit a string its own decoder refuses") {
    // Symmetry with the is_base_rule guard in decode_into. Unreachable today:
    // every overlay that reaches encode_base came from to_flow_base or from a
    // decode, and both already filter. It exists so the "one encoding"
    // contract cannot acquire a producer the consumer rejects.
    int story = -1;
    for (int p = 0; p < P_COUNT && story < 0; ++p) if (!is_base_rule(p)) story = p;
    REQUIRE(story >= 0);
    CAPTURE(std::string(kParams[story].name));

    spky::flow::BaseOverlay ov;
    ov.v[story] = 0.5f;  ov.has[story] = true;
    int base = -1;
    for (int p = 0; p < P_COUNT && base < 0; ++p) if (is_base_rule(p)) base = p;
    REQUIRE(base >= 0);
    ov.v[base] = 0.5f;   ov.has[base] = true;

    const std::string s = encode_base(ov);
    CHECK(s.find(kParams[base].name) != std::string::npos);   // the base rule travels
    CHECK(s.find(kParams[story].name) == std::string::npos);  // the story one does not
    // ...and the whole point: the result decodes.
    spky::flow::BaseOverlay out;
    CHECK(decode_base(s.c_str(), out));
}

TEST_CASE("a divergent pair survives capture as well as restore") {
    // The other half of the case above, and the one that closes the loop.
    // restore_undo is the ONE setter that assigns _undo_overlay independently
    // of _overlay (flow.cpp:242-246), and glow_restore uses exactly it -- so a
    // live Flow really can hold a divergent pair, and a capture that derives
    // the slot's overlay from the live one collapses that pair on the next
    // save. It would then survive precisely one load, which is the shape of
    // bug that looks fixed until somebody saves twice.
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);

    TerrainState house;
    REQUIRE(decode_code(kHouseCode, house));
    TerrainState other = house;
    ++other.reroll[0];

    spky::flow::BaseOverlay a, b;
    a.v[P_TUNE_A] = 0.25f; a.has[P_TUNE_A] = true;
    b.v[P_TUNE_B] = 0.75f; b.has[P_TUNE_B] = true;
    fl.wake(house, &a);
    fl.restore_undo(other, true, &b);        // the divergent state, as a load leaves it
    REQUIRE(fl.overlay() != nullptr);
    REQUIRE(fl.overlay()->has[P_TUNE_A]);
    REQUIRE(fl.undo_overlay() != nullptr);
    REQUIRE(fl.undo_overlay()->has[P_TUNE_B]);

    const GlowSave s = glow_capture(fl);
    CHECK(s.have_base);
    CHECK(s.base.has[P_TUNE_A]);
    CHECK(s.have_undo_base);
    // The slot's own base, not a second copy of the live one.
    CHECK(s.undo_base.has[P_TUNE_B]);
    CHECK_FALSE(s.undo_base.has[P_TUNE_A]);

    spky::Instrument inst2;
    inst2.init(48000.f);
    spky::flow::Flow fl2;
    fl2.init(&inst2, 100.f);
    REQUIRE(glow_restore(fl2, s));
    REQUIRE(fl2.undo_overlay() != nullptr);
    CHECK(fl2.undo_overlay()->has[P_TUNE_B]);
    CHECK(fl2.undo_overlay()->v[P_TUNE_B] == doctest::Approx(0.75f));
    // ...and the gesture agrees with the accessor: undoing lands on B.
    REQUIRE(fl2.undo());
    REQUIRE(fl2.overlay() != nullptr);
    CHECK(fl2.overlay()->has[P_TUNE_B]);
    CHECK_FALSE(fl2.overlay()->has[P_TUNE_A]);
}
