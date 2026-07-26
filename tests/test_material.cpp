// spky::chord_character -- COLOR read as material quality (spec 2026-07-26
// body-resonator, §5/§7, Task 8b).
//
// What is asserted here is the CONTRACT ONLY: the shape of the parameter, not
// its taste. The spec is explicit that "the mapping from chord quality to
// ratio character is tuning material for the listening pass, not a contract",
// so no test in this file pins a named chord to a number -- that would freeze
// Bastian's tuning knobs as a requirement. Where a chord IS named it is only
// to say which two inputs must not collide, and the assertion is inequality.
#include "doctest/doctest.h"

#include "body/material.h"

#include <cmath>
#include <vector>

using namespace spky;

// 0..1 == 36 semitones (engine/pitch/chord.h; synth_engine.cpp::pitch_to_hz).
static constexpr float kSemi = 1.f / 36.f;

TEST_CASE("chord_character: one note has no quality") {
    const float root[4] = { 0.5f, 0.56f, 0.61f, 0.7f };
    // Exact zero, not a tolerance: this is what makes a COLOR-0 deck harmonic.
    CHECK(chord_character(root, 1) == 0.f);
    CHECK(chord_character(root, 0) == 0.f);
    CHECK(chord_character(root, -3) == 0.f);
    CHECK(chord_character(nullptr, 4) == 0.f);
    CHECK(chord_character(nullptr, 1) == 0.f);
}

TEST_CASE("chord_character: bounded to [-1,+1] for any input at all") {
    // Every semitone above the root, alone and stacked, plus the degenerate
    // and the illegal. The bound is what protects the mode bank's collapse
    // point, so it may not depend on the caller behaving.
    for (int s = -48; s <= 48; ++s) {
        const float pair[2] = { 0.5f, 0.5f + static_cast<float>(s) * kSemi };
        const float c = chord_character(pair, 2);
        CAPTURE(s);
        CHECK(std::isfinite(c));
        CHECK(c >= -1.f);
        CHECK(c <= 1.f);
    }

    struct Case { const char* what; std::vector<float> p; };
    const Case cases[] = {
        { "unsorted",            { 0.5f, 0.2f, 0.9f, 0.31f } },
        { "duplicated",          { 0.5f, 0.5f, 0.5f, 0.5f } },
        { "below the rail",      { -4.f, -9.f, 0.5f, -0.001f } },
        { "above the rail",      { 12.f, 40.f, 1.001f, 7.f } },
        { "straddling the rail", { -5.f, 5.f, -5.f, 5.f } },
        { "a semitone cluster",  { 0.5f, 0.5f + kSemi, 0.5f + 2.f * kSemi,
                                   0.5f + 3.f * kSemi } },
        { "the widest spread",   { 0.f, 1.f, 0.f, 1.f } },
    };
    for (const auto& c : cases) {
        CAPTURE(c.what);
        for (int n = 1; n <= 4; ++n) {
            const float v = chord_character(c.p.data(), n);
            CAPTURE(n);
            CHECK(std::isfinite(v));
            CHECK(v >= -1.f);
            CHECK(v <= 1.f);
        }
    }

    // More notes than the chord layer can build: the bound is n-independent.
    std::vector<float> many(64);
    for (size_t i = 0; i < many.size(); ++i)
        many[i] = static_cast<float>(i % 13) * kSemi;
    const float v = chord_character(many.data(), static_cast<int>(many.size()));
    CHECK(v >= -1.f);
    CHECK(v <= 1.f);
}

TEST_CASE("chord_character: deterministic and stateless") {
    const float a[4] = { 0.30f, 0.30f - 5.f * kSemi, 0.30f + 4.f * kSemi,
                         0.30f + 11.f * kSemi };
    const float b[4] = { 0.72f, 0.72f + 1.f * kSemi, 0.72f + 6.f * kSemi,
                         0.72f + 10.f * kSemi };

    const float a0 = chord_character(a, 4);
    const float b0 = chord_character(b, 4);

    // Same input, same output -- bit-identical, not approximately. Interleaved
    // with a different chord so a hidden accumulator would show up as drift.
    for (int i = 0; i < 64; ++i) {
        CHECK(chord_character(b, 4) == b0);
        CHECK(chord_character(a, 4) == a0);
    }

    // Transposition invariance is the same claim from the other side: the
    // function may only read INTERVALS, so moving the whole chord may not
    // move the character. (Slot 0 is the root -- chord.h's slot ladder.)
    // Range limited to what keeps every tone of `a` inside the 0..1 rail:
    // outside it the clamp is doing its job and the intervals really do change.
    for (int t = -5; t <= 14; ++t) {
        float moved[4];
        for (int i = 0; i < 4; ++i) moved[i] = a[i] + static_cast<float>(t) * kSemi;
        CAPTURE(t);
        CHECK(chord_character(moved, 4) == doctest::Approx(a0).epsilon(1e-5));
    }
}

TEST_CASE("chord_character: different chord qualities give different values") {
    // If these collided, COLOR would do nothing on a BODY deck. The VALUES are
    // tuning material and are deliberately not asserted -- only that the
    // function can tell these apart.
    const float root = 0.5f;
    auto chord = [&](int a, int b, int c) {
        return std::vector<float>{ root,
                                   root + static_cast<float>(a) * kSemi,
                                   root + static_cast<float>(b) * kSemi,
                                   root + static_cast<float>(c) * kSemi };
    };
    // Slot ladder (chord.h): root, fifth an octave down, third, seventh.
    const auto major_triad = chord(-5, 4, 0);    // C  G(-8va) E
    const auto minor_triad = chord(-5, 3, 0);    // C  G(-8va) Eb
    const auto dom_seventh = chord(-5, 4, 10);   // C7
    const auto min_seventh = chord(-5, 3, 10);   // Cm7
    const auto cluster     = chord(1, 2, 3);     // the most broken thing there is

    const float maj  = chord_character(major_triad.data(), 3);
    const float min  = chord_character(minor_triad.data(), 3);
    const float dom7 = chord_character(dom_seventh.data(), 4);
    const float min7 = chord_character(min_seventh.data(), 4);
    const float clus = chord_character(cluster.data(), 4);

    CHECK(maj != min);
    CHECK(maj != dom7);
    CHECK(min != min7);
    CHECK(clus != maj);
    CHECK(clus != min);

    // The one directional claim the spec DOES make (§5): "a major triad asks
    // for near-harmonic partials", and each step toward clusters asks for a
    // more broken ratio. So a major triad must be nearer harmonic than a
    // semitone cluster is. Still not a value -- an ordering.
    CHECK(std::fabs(maj) < std::fabs(clus));

    // NOT a spec claim, unlike the ordering above. §5 gives the
    // triad-to-cluster direction and says nothing about a perfect fifth
    // reading nearer-harmonic than a minor third; this holds because
    // kPartialPc happens to contain 7.0196 (partial 3) and no partial within
    // 0.8 semitones of the minor third. That table is declared tuning
    // material, so if Bastian retunes it this line is expected to move with
    // it -- it documents the current table, it does not constrain the next
    // one. Delete it rather than bending the table to keep it green.
    const auto power = chord(-5, 0, 0);
    CHECK(std::fabs(chord_character(power.data(), 2)) < std::fabs(min));
}
