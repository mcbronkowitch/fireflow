// tests/test_pitch_range_law.cpp
// Gates for spec 2026-10-26-pitch-range-law-design.md, section 11.
//
// A1 pins the law on the lane itself (4.1): a note lane emits r times its
// RANGE-1 stream, exactly. A3 pins what that is for -- a melody at noon --
// through the real pitch chain on a SYNTH deck.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "instrument.h"
#include "mod/lane.h"
using namespace spky;

namespace {

// A PITCH lane built the way SuperModulator::init builds one: set_melodic()
// BEFORE init() (init() branches on _melodic when it seeds the pattern, and a
// probe in the other order measures a lane whose RNG stream went on a contour
// walk), then the engine class. `note_engine` false is the SAMPLER/BBD lane.
// SMOOTH 0 so the raw target is visible, VARY 0 so nothing mutates. Same
// construction as stream() in test_melody_reachable.cpp, with RANGE a
// parameter. `stepped`, when given, receives stepped_output() after every
// process() -- the S&H twin that _latch_stepped writes (lane.h).
std::vector<float> lane_stream(bool note_engine, bool step, float range,
                               uint32_t seed,
                               std::vector<float>* stepped = nullptr) {
    ModLane l;
    l.set_melodic(true);
    l.init(48000.f, seed);
    l.set_flow_melody(note_engine);
    l.set_step(step, 8);
    l.set_rate_hz(0.5f);
    l.set_shape(0.50f);
    l.set_smooth(0.f);
    l.set_range(range);
    l.set_variation(0.f);
    std::vector<float> out(48000 * 5);
    if (stepped) stepped->assign(out.size(), 0.f);
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = l.process();
        if (stepped) (*stepped)[i] = l.stepped_output();
    }
    return out;
}

} // namespace

TEST_CASE("pitch range law: a note lane emits r times its RANGE-1 stream") {
    // Deck A's PITCH lane seed in the probes the spec was measured with:
    // 0x1234abcd + (k + 2) * 2654435761 at k = 0 (spec 2).
    const uint32_t seed = 0x1234abcdu + 2u * 2654435761u;
    for (bool step : {false, true}) {
        CAPTURE(step);
        std::vector<float> full_stepped;
        const std::vector<float> full =
            lane_stream(true, step, 1.f, seed, &full_stepped);
        for (float r : {0.1f, 0.25f, 0.5f, 0.75f}) {
            CAPTURE(r);
            std::vector<float> got_stepped;
            const std::vector<float> got =
                lane_stream(true, step, r, seed, &got_stepped);
            REQUIRE(got.size() == full.size());
            // Not a value comparison with a tolerance: the law is one
            // multiply, so the result is r * v to the last bit.
            bool same = true, same_stepped = true;
            for (size_t i = 0; i < got.size(); ++i) {
                if (got[i] != r * full[i]) same = false;
                if (got_stepped[i] != r * full_stepped[i]) same_stepped = false;
            }
            CHECK(same);
            CHECK(same_stepped);
        }
        // RANGE 0 is off.
        std::vector<float> off_stepped;
        const std::vector<float> off =
            lane_stream(true, step, 0.f, seed, &off_stepped);
        bool all_zero = true, all_zero_stepped = true;
        for (size_t i = 0; i < off.size(); ++i) {
            if (off[i] != 0.f) all_zero = false;
            if (off_stepped[i] != 0.f) all_zero_stepped = false;
        }
        CHECK(all_zero);
        CHECK(all_zero_stepped);
        // The comparison above would pass on a lane that emits nothing.
        bool moves = false;
        for (float v : full) if (v != full.front()) { moves = true; break; }
        CHECK(moves);
    }
}

namespace {

// ---- The Instrument rig (A3; Task 2's A4 reuses it) ----------------------
//
// Deck A, SYNTH, STEP 8, synced, through the real pitch chain: lane ->
// Part::pitch_pre_quant() -> quantizer. VARY 0 and SMOOTH 0 so nothing mutates
// the phrase after it is built. The caller picks scale, TUNE, VARY and RANGE.
struct PitchRig {
    int   scale = SCALE_DORIAN;
    float tune  = 0.5f;
    float vary  = 0.f;
    float range = 0.5f;
    int   phrases = 200;
};

struct PhraseReading {
    int phrases   = 0;   // complete phrases measured
    int one_note  = 0;   // of those, phrases whose notes are all one pitch
    int notes     = 0;   // notes measured
    int rail      = 0;   // of those, notes on semitone 0 or 36 (both the root)
    float one_note_share() const { return phrases ? float(one_note) / phrases : 0.f; }
    float rail_share()     const { return notes   ? float(rail) / notes       : 0.f; }
};

PhraseReading read_phrases(const PitchRig& rig) {
    auto inst = std::make_unique<Instrument>();
    inst->init(48000.f);
    inst->set_engine(PART_A, ENGINE_SYNTH);
    inst->set_sync(true);
    inst->set_step(PART_A, true, 8);
    inst->set_scale(rig.scale);
    inst->set_root(PART_A, 0);
    inst->set_tune(PART_A, rig.tune);
    inst->set_variation(PART_A, rig.vary);
    inst->set_smooth(PART_A, 0.f);
    inst->set_range(PART_A, rig.range);

    PhraseReading out;
    float l = 0.f, r = 0.f;
    // A phrase is the run of PITCH fires between two builds of the pitch
    // phrase (SuperModulator::pitch_phrase_builds). new_phrase() asks for a
    // fresh one at the next wrap, so asking again the moment a build lands
    // gives 200 independent phrases off the one deck, each of them complete.
    // One note per fire, read on the settled quantized pitch -- the sample
    // BEFORE the next fire, which is also where the previous fire's note has
    // had its whole slot to settle.
    auto builds = [&] { return inst->part(PART_A).mod().pitch_phrase_builds(); };
    auto semis  = [&] { return inst->target_value(PART_A, LANE_PITCH) * Quantizer::SPAN_SEMIS; };
    auto flush = [&](const std::vector<int>& notes) {
        if (notes.empty()) return;
        ++out.phrases;
        bool one = true;
        for (int n : notes) {
            ++out.notes;
            if (n == 0 || n == 36) ++out.rail;
            if (n != notes.front()) one = false;
        }
        if (one) ++out.one_note;
    };
    uint32_t seen = builds();
    std::vector<int> notes;
    bool  pending = false;
    float last = 0.f;
    inst->new_phrase(PART_A);
    const long cap = 48000L * 60 * 60;          // a hang fails instead of spinning
    for (long i = 0; i < cap && out.phrases < rig.phrases; ++i) {
        inst->process(nullptr, nullptr, &l, &r, 1);
        if (builds() != seen) {                 // a new phrase has just been built
            seen = builds();
            if (pending) notes.push_back(int(std::lround(last)));
            pending = false;
            flush(notes);
            notes.clear();
            inst->new_phrase(PART_A);
        }
        if (inst->lane_fired(PART_A, LANE_PITCH)) {
            if (pending) notes.push_back(int(std::lround(last)));
            pending = true;
        }
        last = semis();
    }
    return out;
}

} // namespace

TEST_CASE("pitch range law: noon plays a melody") {
    // Deck A at the all-noon frame: Dorian, root 0, TUNE 0.5, VARY 0, RANGE
    // 0.5, 200 phrases through the real pitch chain. Under apply_range the
    // lane's unipolar half lifts every note by 1 - r = half the span, the sum
    // leaves [0, 1], Part::pitch_pre_quant()'s clamp parks it on 1.0 and the
    // quantizer calls that the root: a drone on semitone 36.
    //
    // Readings, 2026-10-26, both from this test (the stock row is the new
    // build with apply_range_centred stubbed to apply_range, which is
    // bit-identical to stock engine sources: every note lane then takes the
    // old law):
    //                    one-note phrases    rail notes (semitone 0 or 36)
    //   stock            106 of 200 = 53.0 %   1119 of 1600 = 69.9 %
    //   centred law       10 of 200 =  5.0 %      2 of 1600 =  0.1 %
    // The spec's P1 reads 29.4 -> 5.6 % and 58.0 -> 0.5 % on its own path (a
    // bare lane, 14 phrases x 200 seeds; this one is one deck, 200 phrases, so
    // the stock numbers are higher here). Each threshold sits where both
    // readings clear it by at least a factor of two: 20 % is 4x above the new
    // one-note reading and 2.6x below stock; 5 % is 40x above the new rail
    // reading and 14x below stock.
    PitchRig rig;
    const PhraseReading got = read_phrases(rig);
    REQUIRE(got.phrases == rig.phrases);
    REQUIRE(got.notes >= 4 * rig.phrases);      // a measurement, not an empty one
    CAPTURE(got.one_note);
    CAPTURE(got.rail);
    CHECK(got.one_note_share() < 0.20f);
    CHECK(got.rail_share() < 0.05f);
}
