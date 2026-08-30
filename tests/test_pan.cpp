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

// Gate 1. The whole exactness argument of the design rests on gL == gR ==
// 1.0f at centre: a multiply by 1.0f is exact, so a centred PAN cannot move a
// single sample of an existing render. On a FRESH instrument that is true by
// construction -- _pan_target boots at {0, 0} and reset(0.f) leaves the
// smoother there -- so asserting it on a fresh instrument asserts nothing, and
// the render-identity property it implies is already held down by
// ctrl_identity and the spky_tests render hashes.
//
// What is NOT trivial, and what nothing else verifies, is that unity comes
// BACK after the knob has been somewhere else. The position rides a OnePole,
// and a one-pole only approaches its target asymptotically -- it lands on it
// exactly because OnePole::process() writes _value = target once the distance
// falls inside its 0.0005 dead band (engine/util/onepole.h). That snap is the
// only reason a knob returned to centre leaves 1.0f in the mix rather than a
// 0.9999997 that would sit there, inaudible and de-exacting every render,
// forever. If the dead band ever goes, this is the gate that says so.
TEST_CASE("pan: a return to centre lands on exact unity again") {
    Instrument in;
    in.init(48000.f);
    in.set_tempo_bpm(120.f);

    std::vector<float> l, r;
    in.set_pan(PART_A, -1.f);
    render(in, 16000, l, r);              // > 111 control ticks: the move arrives
    REQUIRE(in.pan_l_for_test(PART_A) == 1.0f);
    REQUIRE(in.pan_r_for_test(PART_A) == 0.0f);   // sanity: it really left centre

    in.set_pan(PART_A, 0.f);
    render(in, 16000, l, r);

    CHECK(in.pan_l_for_test(PART_A) == 1.0f);
    CHECK(in.pan_r_for_test(PART_A) == 1.0f);
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

// Gate 2b. Deck B's half of the routing. Everything else in this file drives
// deck A, and a swap of plb/prb in the mix, a pla<->plb slip or a `part & 1`
// mistake would pass all of it -- tests/test_param_impact.cpp only asks
// whether P_PAN_B moves audio at all, which a swapped-but-live PAN does.
//
// The asymmetry is deliberate: gate 2 pans deck A hard LEFT and asserts the
// RIGHT channel empty, this one pans deck B hard RIGHT and asserts the LEFT
// channel empty. A copy-paste of gate 2 that forgot to flip the channel goes
// red here immediately.
TEST_CASE("pan: deck B's hard stop empties the opposite channel exactly") {
    Instrument in;
    in.init(48000.f);
    in.set_engine(PART_B, ENGINE_SYNTH);
    in.set_tempo_bpm(120.f);
    in.set_part_level(PART_A, 0.f);       // deck A silent: ga contributes nothing
    in.set_part_level(PART_B, 1.f);
    in.set_pan(PART_B, 1.f);              // hard RIGHT, the mirror of gate 2

    std::vector<float> l, r;
    render(in, 48000, l, r);              // 1 s: the smoother needs ~222 ms

    CHECK(in.pan_r_for_test(PART_B) == 1.0f);
    CHECK(in.pan_l_for_test(PART_B) == 0.0f);
    double heard_l = 0.0, heard_r = 0.0;
    for (size_t i = 24000; i < l.size(); ++i) {   // after the smoother arrived
        heard_l += std::fabs(l[i]);
        heard_r += std::fabs(r[i]);
    }
    CHECK(heard_r > 0.0);                 // the deck is actually sounding
    CHECK(heard_l == 0.0);
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

// Gate 5. The second exclusion, and until now the undefended one: PAN must not
// reach the CHOKE sidechain either. instrument.cpp's `pri_gain` reads the
// UNPANNED morph gain, so moving the priority deck across the stereo field has
// to leave the yielding deck's duck bit-identical. Folding PAN into pri_gain
// would look like the same tidy-up gate 3 guards against and would pass gates
// 1, 2, 2b and 4 (two of them engage no CHOKE at all, and gate 3's deck A is
// wet-only, so its dry path -- and with it the detector's input -- is 0).
//
// Instrument::choke_duck_gain() is the public observer for exactly this: from
// outside, a duck is indistinguishable from quieter playing, so the duck gain
// itself is what has to be compared.
//
// CHOKE at -0.4: negative puts deck A on priority (it is the deck being
// panned), and |c| <= 0.5 is the duck zone, so deck B is ducked but never
// inhibited -- the duck gain is the only thing under test.
TEST_CASE("pan: the CHOKE sidechain does not move with the knob") {
    // Uses ENGINE_SYNTH on purpose: this gate can only go red because SYNTH
    // carries real side energy (S/M 0.287, L/R correlation 0.848, spec
    // §2/§5). Folding the pan pair into pri_gain agrees with today's unpanned
    // max(|L|, |R|) whenever |L| == |R|, which is exact for a mono engine
    // (FEED, BBD, TEST_TONE) -- swap this in and the gate would be vacuous.
    Instrument centre, left;
    for (Instrument* in : { &centre, &left }) {
        in->init(48000.f);
        in->set_tempo_bpm(120.f);
        for (int p = 0; p < PART_COUNT; ++p) {
            in->set_engine(p, ENGINE_SYNTH);
            in->set_rate(p, p == PART_A ? 0.8f : 0.9f);
            in->set_density(p, 1.f);
            in->set_range(p, 1.f);
        }
        in->set_choke(-0.4f);
    }
    // centre's push is a no-op against the boot default -- written for the
    // reader, not for the assertion. What carries the gate is that `left`
    // really travelled, which the final REQUIRE proves.
    centre.set_pan(PART_A, 0.f);
    left.set_pan(PART_A, -1.f);

    bool ducked = false;
    for (int i = 0; i < 96000; ++i) {          // 2 s
        float il = 0.f, ir = 0.f, ol = 0.f, orr = 0.f;
        centre.process(&il, &ir, &ol, &orr, 1);
        left.process(&il, &ir, &ol, &orr, 1);
        if (centre.choke_duck_gain() < 1.f) ducked = true;
        REQUIRE(left.choke_duck_gain() == centre.choke_duck_gain());
    }
    CHECK(ducked);                             // sanity: the duck is working
    REQUIRE(left.pan_r_for_test(PART_A) == 0.0f);   // and the knob really moved
}
