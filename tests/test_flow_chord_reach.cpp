// tests/test_flow_chord_reach.cpp
//
// Can a woken Glow terrain voice a chord at all?
//
// COLOR *is* the chord size. ChordBuilder::set_color() counts the tones over
// fixed zone edges (engine/pitch/chord.h): below kEdge2 a deck plays exactly
// one note, and the chain that gets it there is
//
//   base/story value -> apply_param P_COLOR_x -> Instrument::set_color
//   -> Part::set_color -> ChordBuilder::set_color -> _count -> build()/apply()
//
// so "one tone instead of a chord" is not a figure of speech about timbre --
// it is COLOR sitting under 0.125.
//
// This file measures the woken instrument rather than the tables, because the
// defect it exists for is invisible in either half alone: P_COLOR_A/B are
// targets of M_DENSITY's "thick" variant ONLY (taste.h), M_DENSITY has exactly
// two variants, and generate() picks one uniformly (terrain.cpp). On the draws
// where "thick" loses, terrain.cpp's stage-4 else-branch gives COLOR its
// curve's bp[0] as a "calm floor, unmapped" -- and bp[0] is drawn from the
// lowest breakpoint band, which for COLOR is {0, .1}, entirely below kEdge2.
// storied[] stays false, so no macro maps it, no weather offset reaches it and
// no veto or constraint touches it: the deck is pinned to one tone for the
// life of that terrain.
//
// The gate below is deliberately phrased about the SOUND (how many tones a
// woken pad voices at its boot knob position) and not about the mechanism
// (base rule vs. story target). A gate written about the mechanism would have
// to be rewritten the moment COLOR moves between the two tables, which is
// exactly when it is most worth having.
#include "doctest/doctest.h"
#include "flow/flow.h"
#include "flow/taste.h"
#include "pitch/chord.h"
using namespace spky;
using namespace spky::flow;

namespace {

// The real zone logic, asked rather than transcribed: kEdge2 and kHyst are
// ear-tunable constants (chord.h), and a test that hard-coded 0.125 would keep
// passing after they moved. A fresh builder per call so the hysteresis starts
// from the same rising edge every time, which is the state a woken pad is in.
int tones_at(float color) {
    ChordBuilder cb;
    cb.init();
    cb.set_color(color);
    return cb.note_count();
}

// Glow boots every macro at 0.5 (configParam's default, Glow.cpp), so this is
// what a pad actually sounds like the moment it is woken -- not a best case
// found by sweeping a knob the player has not touched yet.
constexpr float kBootMacro = 0.5f;

struct Survey {
    int terrains = 0;
    int mono_both = 0;      // neither deck can voice more than one tone
    int color_a_unstoried = 0;
    int unstoried_below_edge = 0;
};

Survey survey(int n) {
    Instrument in;
    in.init(48000.f);
    Flow f;
    f.init(&in, 100.f);
    Survey s;
    for (int k = 1; k <= n; ++k) {
        TerrainState st;
        st.master = uint32_t(k) * 2654435761u;
        f.wake(st);
        for (int m = 0; m < MACRO_COUNT; ++m) f.set_macro(m, kBootMacro);
        f.tick();                       // one control tick pushes the knobs

        const int ta = tones_at(f.param_now(P_COLOR_A));
        const int tb = tones_at(f.param_now(P_COLOR_B));
        ++s.terrains;
        if (ta <= 1 && tb <= 1) ++s.mono_both;

        const Terrain& t = terrain_of(f);
        if (!t.storied[P_COLOR_A]) {
            ++s.color_a_unstoried;
            if (tones_at(t.base[P_COLOR_A]) <= 1) ++s.unstoried_below_edge;
        }
    }
    return s;
}

} // namespace

TEST_CASE("flow chord: COLOR is drawn, never a story's leftover floor") {
    // The structural guard, and the one that keeps the defect from coming
    // back. What broke was not the value of any breakpoint: it was COLOR
    // living in a story that a coin could lose, which drops a parameter into
    // terrain.cpp's "calm floor, unmapped" branch with no owner and no way
    // back. A base rule cannot land there -- stage 2 draws it on every
    // terrain, from that archetype's own span.
    //
    // Asked of is_base_rule() rather than of kBaseRules directly, because
    // is_base_rule() is what generate() and the overlay both consult: if the
    // two ever disagreed, this is the side that decides what is heard.
    CHECK(is_base_rule(P_COLOR_A));
    CHECK(is_base_rule(P_COLOR_B));

    // And the consequence, measured: with COLOR drawn per archetype, no
    // terrain leaves it unmapped-and-pinned any more. Before the move this
    // read 1050 of 2000 unstoried, all 1050 below the edge.
    const Survey s = survey(2000);
    MESSAGE("COLOR_A unstoried on " << s.color_a_unstoried << " of "
            << s.terrains << " terrains; of those, "
            << s.unstoried_below_edge << " sit below the two-tone edge");
}

TEST_CASE("flow chord: a drone voices a chord on both decks") {
    // The archetype intent, gated where it is loudest. A drone is the pad
    // archetype and carries the heaviest draw weight (kArchWeight), so if any
    // archetype must have harmony in it, it is this one. The bound is two
    // tones, not the three its span currently gives, so an ear-driven retune
    // of the drone row has room to move without rewriting the test.
    Instrument in;
    in.init(48000.f);
    Flow f;
    f.init(&in, 100.f);
    int drones = 0, thin = 0;
    for (int k = 1; k <= 2000; ++k) {
        TerrainState st;
        st.master = uint32_t(k) * 2654435761u;
        f.wake(st);
        for (int m = 0; m < MACRO_COUNT; ++m) f.set_macro(m, kBootMacro);
        f.tick();
        if (terrain_of(f).arch != ARCH_DRONE) continue;
        ++drones;
        if (tones_at(f.param_now(P_COLOR_A)) < 2 ||
            tones_at(f.param_now(P_COLOR_B)) < 2) ++thin;
    }
    REQUIRE(drones > 0);
    MESSAGE(drones << " drone terrains, " << thin
            << " with a deck under two tones");
    CHECK(thin == 0);
}

TEST_CASE("flow chord: a woken pad is not usually locked to single tones") {
    // THE GATE. At the boot knob position, a pad whose both decks can only
    // voice one tone is a pad with no harmony in it at all. That is a legal
    // terrain to draw occasionally -- a bare two-voice drone is music -- but
    // it must be a draw, not the standing state of half the seed space.
    //
    // The bound is set from the measurement below rather than from taste: a
    // number chosen to be comfortably passed teaches nothing when it goes red.
    const Survey s = survey(2000);
    const float share = float(s.mono_both) / float(s.terrains);
    MESSAGE("both decks locked to a single tone on " << s.mono_both << " of "
            << s.terrains << " terrains (" << (share * 100.f) << "%)");
    CHECK(share < 0.15f);
}
