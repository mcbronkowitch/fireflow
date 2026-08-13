// tests/test_touch_pads.cpp
#include <doctest/doctest.h>
#include <cstring>
#include <string>
#include "vcv/src/generated_flow_panel.hpp"
#include "vcv/src/glow_panel.hpp"
#include "vcv/src/pad_geometry.hpp"
#include "vcv/src/touch_pads.hpp"
#include "flow/taste.h"
#include "flow/terrain_code.h"

using namespace spky;
using namespace spky::flow;
using namespace spkyvcv;

TEST_CASE("pads: every generated centre is inside its exact physical "
          "electrode and retains the 10+2 zone split") {
    using namespace spkyvcv::glow;
    const auto& bindings = spkyvcv::glow_panel::padBindings();
    REQUIRE(bindings.size() == 12);
    for (int i = 0; i < int(bindings.size()); ++i) {
        REQUIRE(bindings[i].shape != nullptr);
        const PadShape& shape = *bindings[i].shape;
        INFO(shape.id);
        CHECK(spkyvcv::pad_geometry::pointInClosedCatmullRom(
            shape.points, shape.pointCount, shape.centre));
        CHECK(shape.zone == (i < 10 ? PadZone::LowerTouch
                                    : PadZone::UpperRear));
    }
}

// A helper that builds the 12-bool "which pads are down" vector.
static void press(bool* d, int pad) {
    for (int i = 0; i < kPadCount; ++i) d[i] = (i == pad);
}
static void none(bool* d) {
    for (int i = 0; i < kPadCount; ++i) d[i] = false;
}

TEST_CASE("pads: a press wakes immediately, with no latency") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 3);
    const PadEvent e = g.update(d, 0.0);
    CHECK(e.action == PadAction::WAKE);
    CHECK(e.pad == 3);
    CHECK(g.live == 3);
    CHECK(g.excursion == false);
}

TEST_CASE("pads: holding past the threshold rerolls once, not repeatedly") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 5);
    CHECK(g.update(d, 0.0).action == PadAction::WAKE);
    // Still short of the threshold.
    CHECK(g.update(d, kPadHoldS * 0.5).action == PadAction::NONE);
    // Crossing it fires exactly one reroll.
    const PadEvent r = g.update(d, kPadHoldS + 0.01);
    CHECK(r.action == PadAction::REROLL);
    CHECK(r.pad == 5);
    // Holding on does NOT fire again.
    CHECK(g.update(d, kPadHoldS + 1.0).action == PadAction::NONE);
    CHECK(g.update(d, kPadHoldS + 2.0).action == PadAction::NONE);
}

TEST_CASE("pads: releasing without reaching the threshold rerolls nothing") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 1);
    CHECK(g.update(d, 0.0).action == PadAction::WAKE);
    none(d);
    CHECK(g.update(d, 0.1).action == PadAction::NONE);
    // Time passing after the release must not arm anything.
    CHECK(g.update(d, 5.0).action == PadAction::NONE);
}

TEST_CASE("pads: tapping the same pad again is a plain wake -- the excursion "
          "needs no special case") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 7);
    g.update(d, 0.0);
    g.update(d, kPadHoldS + 0.01);
    g.excursion = true;                    // the module sets this on success
    none(d);
    g.update(d, 1.0);
    press(d, 7);
    const PadEvent e = g.update(d, 2.0);
    CHECK(e.action == PadAction::WAKE);
    CHECK(e.pad == 7);
    CHECK(g.excursion == false);           // the wake clears it
}

TEST_CASE("pads: a second pad pressed while one is held is ignored") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 2);
    CHECK(g.update(d, 0.0).action == PadAction::WAKE);
    d[9] = true;                           // both down now
    CHECK(g.update(d, 0.1).action == PadAction::NONE);
    CHECK(g.live == 2);
}

TEST_CASE("pads: a pad already down at the first update does not fire -- "
          "a restored patch must not wake or reroll itself") {
    PadGesture g;
    bool d[kPadCount];
    press(d, 4);
    // reset() models the post-load state: params were forced to 0, and the
    // gesture must treat whatever it sees next as the baseline, not an edge.
    g.reset();
    g.prime(d);
    CHECK(g.update(d, 0.0).action == PadAction::NONE);
    CHECK(g.update(d, kPadHoldS + 1.0).action == PadAction::NONE);
}

TEST_CASE("faders: TEMPO spans P_TEMPO_BPM's declared range") {
    CHECK(fader_tempo_bpm(0.f) == doctest::Approx(50.f));
    CHECK(fader_tempo_bpm(1.f) == doctest::Approx(140.f));
    CHECK(fader_tempo_bpm(0.5f) == doctest::Approx(95.f));
}

TEST_CASE("faders: MASTER is unity at the top and silent at the bottom") {
    CHECK(fader_master_gain(1.f) == doctest::Approx(1.f));
    CHECK(fader_master_gain(0.f) == doctest::Approx(0.f));
}

TEST_CASE("switches: LOCK uses the end positions, centre reads as off") {
    CHECK(lock_switch(0) == false);
    CHECK(lock_switch(1) == false);
    CHECK(lock_switch(2) == true);
    CHECK(lock_switch(-1) == false);       // a corrupt patch must not lock
    CHECK(lock_switch(99) == false);
}

TEST_CASE("switches: SCALE gates the menu's values and never invents one") {
    const int S = SCALE_DORIAN, R = 5;
    CHECK(scale_switch(0, S, R).scale_ovr == -1);
    CHECK(scale_switch(0, S, R).root_ovr == -1);
    CHECK(scale_switch(1, S, R).scale_ovr == S);
    CHECK(scale_switch(1, S, R).root_ovr == -1);
    CHECK(scale_switch(2, S, R).scale_ovr == S);
    CHECK(scale_switch(2, S, R).root_ovr == R);
    // Out of range is AUTO -- the same rule glow_ui.hpp's clamp_* helpers
    // apply. (scale_of_knob, which used to carry it, is gone: the Touch 2
    // surface has no scale knob.)
    CHECK(scale_switch(7, S, R).scale_ovr == -1);
    CHECK(scale_switch(-3, S, R).root_ovr == -1);
}

TEST_CASE("labels: TSV-hostile characters are stripped, not escaped") {
    CHECK(sanitize_label("a\tb", 32) == "ab");
    CHECK(sanitize_label("a\nb\r\nc", 32) == "abc");
    CHECK(sanitize_label("abcdef", 3) == "abc");
    CHECK(sanitize_label("", 32).empty());
}

TEST_CASE("labels: set_label fills a Place's fixed buffer without overrunning "
          "it") {
    // Place stopped holding std::string so the twelve can be copied onto the
    // audio thread without allocating (see touch_pads.hpp). The cost of that
    // is a buffer, and this is the guard on it: the byte at dst[cap] is the
    // terminator and must always be written, whatever came in.
    char buf[8];                       // cap 7 plus the terminator
    std::memset(buf, 'Z', sizeof buf);
    set_label(buf, 7, "a\tb\nc");
    CHECK(std::string(buf) == "abc");

    std::memset(buf, 'Z', sizeof buf);
    set_label(buf, 7, "abcdefghijkl");
    CHECK(std::string(buf) == "abcdefg");
    CHECK(buf[7] == '\0');

    std::memset(buf, 'Z', sizeof buf);
    set_label(buf, 7, "");
    CHECK(buf[0] == '\0');

    // The two caps really are the two field widths -- a name that fits by the
    // cap must fit by sizeof, or the memcpy above writes past the array.
    Place p;
    CHECK(sizeof p.name == kNameCap + 1);
    CHECK(sizeof p.note == kNoteCap + 1);
    set_label(p.note, kNoteCap, std::string(kNoteCap + 40, 'x'));
    CHECK(std::strlen(p.note) == kNoteCap);
}

TEST_CASE("export: the header row and column order match pool.tsv") {
    Place p[kPadCount];
    const std::string tsv = export_pool_tsv(p, kPadCount);
    // `base` is the eighth column since 2026-08-11 §6: the hand-authored
    // overlay, in the one textual encoding, as a COLUMN and not a line.
    const std::string hdr = "code\tarch\tdate\tfp\tpad\tname\tnote\tbase\n";
    CHECK(tsv.compare(0, hdr.size(), hdr) == 0);
}

TEST_CASE("export: every pad emits a row, and the empty interior columns "
          "still emit their tabs") {
    Place p[kPadCount];
    for (int i = 0; i < kPadCount; ++i)
        std::snprintf(p[i].code, sizeof p[i].code, "%s", kHouseCode);
    set_label(p[0].name, kNameCap, "First light");
    set_label(p[0].note, kNoteCap, "It carries at 0.2");

    const std::string tsv = export_pool_tsv(p, kPadCount);
    int lines = 0;
    for (char c : tsv) if (c == '\n') ++lines;
    CHECK(lines == kPadCount + 1);          // header plus twelve rows

    const std::size_t rowStart = tsv.find('\n') + 1;
    const std::size_t rowEnd = tsv.find('\n', rowStart);
    const std::string row = tsv.substr(rowStart, rowEnd - rowStart);
    int tabs = 0;
    for (char c : row) if (c == '\t') ++tabs;
    CHECK(tabs == 7);                       // eight columns
    CHECK(row.find(kHouseCode) == 0);
    CHECK(row.find("\t\t\t1\t") != std::string::npos);   // date, fp empty; pad 1
    CHECK(row.find("First light") != std::string::npos);
    CHECK(row.find("It carries at 0.2") != std::string::npos);
    // No base on this place, so the last column is empty and the row still
    // ends where it did -- the tab is emitted, the value is not, and nothing
    // spills onto a line of its own.
    CHECK(row.back() == '\t');
}

TEST_CASE("export: the arch column is spelled with the enum's short name") {
    CHECK(std::strcmp(arch_name(ARCH_DRONE), "DRONE") == 0);
    CHECK(std::strcmp(arch_name(ARCH_PULSE), "PULSE") == 0);
    CHECK(std::strcmp(arch_name(ARCH_ARP), "ARP") == 0);
    CHECK(std::strcmp(arch_name(ARCH_FRAGMENT), "FRAGMENT") == 0);
    CHECK(std::strcmp(arch_name(-1), "") == 0);
    CHECK(std::strcmp(arch_name(ARCH_COUNT), "") == 0);
}

TEST_CASE("export: every archetype the engine has is spelled, not just the "
          "four that were there when this was written") {
    // arch_name() has no coupling to ARCH_COUNT -- it is a switch with a
    // default, so a fifth archetype falls through it and export_pool_tsv emits
    // an empty arch column for one twelfth of the pool, silently. The four
    // CHECKs above cannot see that (they name the four that exist), and
    // arch_name(ARCH_COUNT) == "" cannot either: "out of range" and "in range
    // but unnamed" are the same answer there. kMacroNames in Glow.cpp is sized
    // by MACRO_COUNT and gets this coupling from the compiler; arch_name only
    // gets it here.
    for (int a = 0; a < ARCH_COUNT; ++a) {
        INFO("archetype ", a);
        CHECK(std::strcmp(arch_name(a), "") != 0);
    }
}

TEST_CASE("export: an undecodable code leaves arch empty rather than "
          "dropping the row") {
    Place p[1];
    std::snprintf(p[0].code, sizeof p[0].code, "%s", "not-a-code");
    const std::string tsv = export_pool_tsv(p, 1);
    int lines = 0;
    for (char c : tsv) if (c == '\n') ++lines;
    CHECK(lines == 2);
    CHECK(tsv.find("not-a-code\t\t") != std::string::npos);
}
