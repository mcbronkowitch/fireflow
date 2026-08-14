// tests/test_melody_reachable.cpp
// Gates for spec 2026-08-14-melody-reachable-design.md §2.1.
//
// Construction order: set_melodic() BEFORE init() -- init() branches on
// _melodic when it seeds the pattern (lane.cpp:70), and SuperModulator
// orders them that way (super_modulator.cpp:14-15). A probe in the other
// order measures a lane whose RNG stream was spent on a contour walk.
#include <doctest/doctest.h>
#include <set>
#include <vector>
#include "mod/lane.h"
using namespace spky;

namespace {

// The distinct value set a melodic STEP lane emits over 20 s at 0.5 Hz.
// SMOOTH 0 so the raw target is visible; VARY 0 so nothing mutates.
std::set<float> emitted(Principle form, float shape, uint32_t seed,
                        bool note_engine = true) {
    ModLane l;
    l.set_melodic(true);                 // BEFORE init -- see header
    l.init(48000.f, seed);
    l.set_form(form);
    l.set_flow_melody(note_engine);      // engine class: note vs SAMPLER/BBD
    l.set_step(true, 8);
    l.set_rate_hz(0.5f);
    l.set_shape(shape);
    l.set_smooth(0.f);
    l.set_range(1.f);
    l.set_variation(0.f);
    std::set<float> vals;
    for (int i = 0; i < 48000 * 20; ++i) vals.insert(l.process());
    return vals;
}

// How many of the four other Principles differ from TwoMotif.
int forms_differing(float shape, uint32_t seed) {
    const std::set<float> ref = emitted(Principle::TwoMotif, shape, seed);
    int n = 0;
    for (int f = 1; f < static_cast<int>(Principle::kCount); ++f)
        if (emitted(Principle(f), shape, seed) != ref) ++n;
    return n;
}

} // namespace

TEST_CASE("melody-reachable: FORM changes the sequence at every SHAPE") {
    // Before this change a STEP note deck ran its pitch through the waveform
    // bank, and sh_hold -- the phrase -- is only weighted in shape_value's
    // fourth arm (waveforms.h:32, shape >= 0.75). Below that the phrase was
    // computed and discarded, so every Principle emitted the same sine
    // staircase: measured 0 of 4 differing at SHAPE 0.00 (5 distinct values)
    // and at 0.50 (9 distinct values), on all four seeds; 3 of 4 differ at
    // SHAPE 1.00, where the phrase finally reaches the output.
    for (uint32_t seed : {999u, 12345u, 7u, 4242u}) {
        CAPTURE(seed);
        CHECK(forms_differing(0.00f, seed) >= 3);
        CHECK(forms_differing(0.50f, seed) >= 3);
        CHECK(forms_differing(1.00f, seed) >= 3);
    }
}

// The full emitted stream, for sample-exact comparison.
namespace {
std::vector<float> stream(bool melodic, bool step, bool note_engine,
                          float shape, uint32_t seed) {
    ModLane l;
    l.set_melodic(melodic);
    l.init(48000.f, seed);
    l.set_flow_melody(note_engine);
    l.set_step(step, 8);
    l.set_rate_hz(0.5f);
    l.set_shape(shape);
    l.set_smooth(0.f);
    l.set_range(1.f);
    l.set_variation(0.f);
    std::vector<float> out(48000 * 5);
    for (float& v : out) v = l.process();
    return out;
}
} // namespace

TEST_CASE("melody-reachable: SHAPE is inert on a note lane, both modes") {
    for (bool step : {false, true}) {
        CAPTURE(step);
        const std::vector<float> ref = stream(true, step, true, 0.00f, 999u);
        for (float sh : {0.25f, 0.50f, 0.75f, 1.00f}) {
            CAPTURE(sh);
            CHECK(stream(true, step, true, sh, 999u) == ref);
        }
    }
}

TEST_CASE("melody-reachable: SAMPLER and BBD PITCH lanes are untouched") {
    // Their decks push set_flow_melody(false) from the engine id
    // (part.cpp:43,441), so _note_lane() is false in BOTH modes and the
    // guard must not reach them: a BBD deck's PITCH lane is the delay clock
    // and not a note (owner's ruling, 2026-08-07). It is NOT kBbdFlowRangeMax
    // that this protects, as an earlier version of this comment claimed --
    // that cap is FLOW-only (flow.cpp:583-585, `== ENGINE_BBD && !_mode_now`),
    // i.e. it never runs in STEP, the one mode where the two candidate guards
    // differ. Measured over 400 masters: all 35 BBD decks drawn in STEP carry
    // RANGE above the cap, up to 0.7266 against a cap of 0.0083.
    // RED against the _melody_engine_on() variant: STEP case drops from
    // 9 distinct values to 4 (measured; p2p was not probed).
    for (bool step : {false, true}) {
        CAPTURE(step);
        const std::vector<float> s = stream(true, step, false, 0.50f, 999u);
        std::set<float> vals(s.begin(), s.end());
        CHECK(vals.size() > 8);          // still a waveform, not an 8-slot phrase
    }
}

TEST_CASE("melody-reachable: non-melodic lanes are untouched") {
    // Boundary pin, not a proven-red gate: every guard this line could
    // plausibly be written as (_flow_melody_on, _melody_engine_on,
    // _note_lane -- lane.h:195,200,210) leads with `_melodic &&`, so none
    // of them can ever reach a lane with _melodic == false. No mutation of
    // this family was found that reddens this case; a `return true`
    // mutation would not be a plausible variant of the guard and was not
    // attempted (Ruling B).
    for (bool step : {false, true}) {
        CAPTURE(step);
        const std::vector<float> s = stream(false, step, false, 0.50f, 999u);
        std::set<float> vals(s.begin(), s.end());
        CHECK(vals.size() > 8);
    }
}
