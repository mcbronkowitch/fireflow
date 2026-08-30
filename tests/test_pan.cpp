// Include order copied from tests/test_choke.cpp, which builds the same
// FxMem: parts/part.h before instrument.h is what puts Flux::kMaxSamples and
// AmbientReverb in scope.
#include <doctest/doctest.h>
#include "parts/part.h"
#include "instrument.h"
#include <vector>
#include <cmath>

using namespace spky;

namespace {

// One reverb and one set of tape buffers per instrument -- the room is shared
// state, so two instruments cannot borrow each other's. Same shape as
// ChokeFx in tests/test_choke.cpp.
struct PanFx {
    std::vector<float> echo[PART_COUNT][2];
    AmbientReverb reverb;
    PanFx() {
        for (int p = 0; p < PART_COUNT; ++p)
            for (int ch = 0; ch < 2; ++ch) echo[p][ch].resize(Flux::kMaxSamples);
    }
    FxMem mem() {
        FxMem m;
        for (int p = 0; p < PART_COUNT; ++p)
            for (int ch = 0; ch < 2; ++ch) m.echo[p][ch] = echo[p][ch].data();
        m.reverb = &reverb;
        return m;
    }
};

// Render n samples of silence-in, collecting the stereo output.
void render(Instrument& in, int n, std::vector<float>& l, std::vector<float>& r) {
    l.clear(); r.clear();
    for (int i = 0; i < n; ++i) {
        float il = 0.f, ir = 0.f, ol = 0.f, orr = 0.f;
        in.process(&il, &ir, &ol, &orr, 1);
        l.push_back(ol); r.push_back(orr);
    }
}

} // namespace

// Gate 1. The whole exactness argument of the design rests on this: under the
// balance law p == 0 gives gL == gR == 1.0f, a multiply by 1.0f is exact, and
// therefore a centred PAN cannot move a single sample of an existing render.
// If this reddens, the render-hash gates (ctrl_identity, spky_tests) will
// redden too, and this test says WHY where they only say "reference moved".
TEST_CASE("pan: centre is exactly unity and cannot move a render") {
    Instrument untouched, centred;
    untouched.init(48000.f);
    centred.init(48000.f);
    for (Instrument* in : { &untouched, &centred }) {
        in->set_engine(PART_A, ENGINE_SYNTH);
        in->set_engine(PART_B, ENGINE_SYNTH);
        in->set_tempo_bpm(120.f);
    }
    centred.set_pan(PART_A, 0.f);
    centred.set_pan(PART_B, 0.f);

    std::vector<float> ul, ur, cl, cr;
    render(untouched, 48000, ul, ur);
    render(centred,   48000, cl, cr);

    CHECK(centred.pan_l_for_test(PART_A) == 1.0f);
    CHECK(centred.pan_r_for_test(PART_A) == 1.0f);
    for (size_t i = 0; i < ul.size(); ++i) {
        REQUIRE(cl[i] == ul[i]);
        REQUIRE(cr[i] == ur[i]);
    }
}

// Gate 2. A stop is a stop: hard left silences the right channel exactly.
// Deck B is held at level 0 so gb is exactly 0 and contributes nothing, and
// the engine-only init() builds no FX chain, so there is no reverb return to
// leak into the right channel from somewhere else.
TEST_CASE("pan: a hard stop empties the opposite channel exactly") {
    Instrument in;
    in.init(48000.f);
    in.set_engine(PART_A, ENGINE_SYNTH);
    in.set_tempo_bpm(120.f);
    in.set_part_level(PART_A, 1.f);
    in.set_part_level(PART_B, 0.f);
    in.set_pan(PART_A, -1.f);

    std::vector<float> l, r;
    render(in, 48000, l, r);              // 1 s: the smoother needs ~222 ms

    CHECK(in.pan_l_for_test(PART_A) == 1.0f);
    CHECK(in.pan_r_for_test(PART_A) == 0.0f);
    double heard_l = 0.0, heard_r = 0.0;
    for (size_t i = 24000; i < l.size(); ++i) {   // after the smoother arrived
        heard_l += std::fabs(l[i]);
        heard_r += std::fabs(r[i]);
    }
    CHECK(heard_l > 0.0);                 // the deck is actually sounding
    CHECK(heard_r == 0.0);
}

// Gate 3. THE gate of this feature. PAN must not reach the reverb send, so a
// deck panned hard left must leave the room's return untouched. Deck A is
// wet-only (set_reverb_mix(A, 1) -> its dry gain is exactly 0 from sample 0,
// snapped before the first block by _rev_primed), so its ONLY path to the
// output is the shared room -- any difference between the two renders below
// can only have come through the send.
//
// This is the gate that defends the design against its own obvious
// simplification: folding PAN into ga/gb would look like a tidy-up, would pass
// gates 1 and 2, and would silently pan the cloud.
TEST_CASE("pan: the reverb send does not move with the knob") {
    static PanFx fx_centre, fx_left;
    Instrument centre, left;
    centre.init(48000.f, fx_centre.mem());
    left.init(48000.f, fx_left.mem());
    for (Instrument* in : { &centre, &left }) {
        in->set_engine(PART_A, ENGINE_SYNTH);
        in->set_tempo_bpm(120.f);
        in->set_part_level(PART_A, 1.f);
        in->set_part_level(PART_B, 0.f);
        in->set_reverb_mix(PART_A, 1.f);      // wet only: no dry path at all
        in->set_reverb_mix(PART_B, 0.f);
        in->set_reverb_decay(0.5f);
    }
    centre.set_pan(PART_A, 0.f);
    left.set_pan(PART_A, -1.f);

    std::vector<float> cl, cr, ll, lr;
    render(centre, 48000, cl, cr);
    render(left,   48000, ll, lr);

    double energy = 0.0;
    for (size_t i = 0; i < cl.size(); ++i) energy += std::fabs(cl[i]) + std::fabs(cr[i]);
    CHECK(energy > 0.0);                      // the room is actually returning

    for (size_t i = 0; i < cl.size(); ++i) {
        REQUIRE(ll[i] == cl[i]);
        REQUIRE(lr[i] == cr[i]);
    }
}

// Gate 4. The position is smoothed, not stepped. Measured on the LVL twin,
// which is the same OnePole(_cr, 0.03f) at the same control rate (probe,
// 2026-08-30, Instrument::init(48000) engine only, deck A SYNTH, FLOW): one
// control tick moves 0.0667 of the remaining distance, and a full-scale step
// arrives after 111 ticks = 222 ms. So after ONE tick of a 0 -> -1 move the
// position is about -0.0667 and gR is about 0.9333 -- nowhere near its 0.0
// destination. An unsmoothed PAN, or one built with time_s == 0, lands on
// 0.0 here immediately.
TEST_CASE("pan: the position is smoothed, not stepped") {
    Instrument in;
    in.init(48000.f);
    in.set_engine(PART_A, ENGINE_SYNTH);
    in.set_tempo_bpm(120.f);
    in.set_pan(PART_A, 0.f);
    std::vector<float> l, r;
    render(in, 48000, l, r);                  // settle at centre
    REQUIRE(in.pan_r_for_test(PART_A) == 1.0f);

    in.set_pan(PART_A, -1.f);
    render(in, Center::kCtrlInterval, l, r);  // exactly one control tick

    const float gr = in.pan_r_for_test(PART_A);
    CHECK(gr > 0.90f);
    CHECK(gr < 0.96f);
}
