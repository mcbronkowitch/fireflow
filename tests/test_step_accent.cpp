// The STEP accent: per-note velocity and decay from the groove rank.
// Spec: docs/superpowers/specs/2026-08-15-step-accent-design.md
//
// Setup note: set_step() does NOT regenerate the groove -- the next cycle
// wrap does. Every helper below therefore runs the lane past a wrap before it
// believes anything it reads, and the accents are collected over a whole
// cycle delimited by wrap_count_for_test(), never by a hand-computed sample
// count (the STEP clock scales the cycle by 8/steps, so a fixed count is a
// different fraction of a cycle at every STEPS value).
#include <doctest/doctest.h>
#include "mod/lane.h"
#include "parts/part.h"
#include "synth_engine_contract.h"   // spky_contract::fresh / render_l
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using namespace spky;

namespace {

// A note deck in STEP: melodic AND flow_melody, which is what Part pushes for
// SYNTH/WAVE/BODY. set_melodic() BEFORE init() -- docs/engine-map.md section 6.
ModLane note_step(uint32_t seed, int steps) {
    ModLane l;
    l.set_melodic(true);
    l.init(48000.f, seed);
    l.set_flow_melody(true);
    l.set_step(true, steps);
    l.set_rate_hz(0.5f);
    l.set_shape(0.f);
    l.set_smooth(0.f);
    l.set_range(1.f);
    l.set_variation(0.f);
    return l;
}

constexpr int kSampleCap = 8'000'000;   // ~166 s: far past any cycle here

void run_to_wrap(ModLane& l, uint32_t target) {
    for (int i = 0; i < kSampleCap; ++i) {
        l.process();
        if (l.wrap_count_for_test() >= target) return;
    }
    FAIL("lane never reached the requested wrap count");
}

// The accents emitted over exactly one cycle, in fire order, measured after
// the groove has settled.
std::vector<float> accents_in_cycle(ModLane& l) {
    run_to_wrap(l, 2);
    const uint32_t end = l.wrap_count_for_test() + 1;
    std::vector<float> out;
    for (int i = 0; i < kSampleCap; ++i) {
        l.process();
        if (l.fired()) out.push_back(l.note_accent());
        if (l.wrap_count_for_test() >= end) return out;
    }
    FAIL("lane never wrapped while collecting");
    return out;
}

const int kStepSet[] = {4, 8, 16};
const uint32_t kSeeds[] = {999u, 12345u, 7u, 4242u};

}  // namespace

TEST_CASE("accent G1: the anchor is at full strength, whatever DENSE is") {
    for (int steps : kStepSet) {
        for (uint32_t seed : kSeeds) {
            CAPTURE(steps);
            CAPTURE(seed);

            ModLane sparse = note_step(seed, steps);
            sparse.set_density(0.f);
            std::vector<float> a_sparse = accents_in_cycle(sparse);
            REQUIRE(a_sparse.size() == 1);          // k == 1: only the anchor fires
            CHECK(a_sparse[0] == doctest::Approx(0.f));

            // The contrast is part of the gate, not decoration: without it a
            // stub returning a constant 0 would pass this case.
            ModLane dense = note_step(seed, steps);
            dense.set_density(1.f);
            std::vector<float> a_dense = accents_in_cycle(dense);
            REQUIRE(!a_dense.empty());
            CHECK(*std::max_element(a_dense.begin(), a_dense.end()) > 0.9f);
        }
    }
}

TEST_CASE("accent G2: at DENSE 1 the contour is the whole rank scale") {
    for (int steps : kStepSet) {
        for (uint32_t seed : kSeeds) {
            CAPTURE(steps);
            CAPTURE(seed);
            ModLane l = note_step(seed, steps);
            l.set_density(1.f);
            std::vector<float> a = accents_in_cycle(l);

            // One note per step: this is also what pins L == the STEPS count.
            REQUIRE(a.size() == static_cast<size_t>(steps));
            std::set<float> uniq(a.begin(), a.end());
            CHECK(uniq.size() == static_cast<size_t>(steps));   // a permutation
            CHECK(*uniq.begin() == doctest::Approx(0.f));
            CHECK(*uniq.rbegin() == doctest::Approx(1.f));
        }
    }
}

TEST_CASE("accent G2: at an intermediate DENSE, the fired accents are exactly "
          "the k lowest ranks over L-1") {
    // Discriminates L-normalization from k-normalization in _start_note --
    // something neither G1 nor G2 above can do. G1 runs at k == 1, where the
    // lone firing slot is always rank 0, so the accent is 0/anything == 0
    // under either normalization. G2 above runs at DENSE 1.0, where
    // _groove_k() computes k == L exactly (round(1.0*L) clamped to L), so
    // _groove_k() - 1 and (the actual denominator) L - 1 are numerically
    // identical there. The two forms only diverge where k < L, so this case
    // sits at DENSE 0.5, which yields k < L for every STEPS in kStepSet, and
    // asserts the exact accent SET (not just its max) so it also re-pins
    // that the firing slots are precisely the k lowest ranks (lane.cpp:655).
    constexpr float kDense = 0.5f;
    for (int steps : kStepSet) {
        for (uint32_t seed : kSeeds) {
            CAPTURE(steps);
            CAPTURE(seed);

            const int L = steps;   // pattern_groove.len == STEPS count (G2 above)
            const int k = std::clamp(
                static_cast<int>(std::lround(kDense * static_cast<float>(L))),
                1, L);
            REQUIRE(k < L);   // the setup must actually exercise k < L

            ModLane l = note_step(seed, steps);
            l.set_density(kDense);
            std::vector<float> a = accents_in_cycle(l);

            // Exactly k notes fire per cycle (the k lowest ranks), and their
            // accents are exactly { r/(L-1) : r in 0..k-1 }.
            REQUIRE(a.size() == static_cast<size_t>(k));
            std::set<float> got(a.begin(), a.end());
            std::set<float> want;
            for (int r = 0; r < k; ++r)
                want.insert(static_cast<float>(r) / static_cast<float>(L - 1));

            REQUIRE(got.size() == want.size());
            auto git = got.begin();
            auto wit = want.begin();
            for (; git != got.end(); ++git, ++wit)
                CHECK(*git == doctest::Approx(*wit));
        }
    }
}

TEST_CASE("accent G3: FLOW reports 0, including right after leaving STEP") {
    ModLane l = note_step(12345u, 8);
    l.set_density(1.f);
    run_to_wrap(l, 2);

    bool saw_nonzero = false;
    for (int i = 0; i < 400000; ++i) {
        l.process();
        if (l.fired() && l.note_accent() != 0.f) saw_nonzero = true;
    }
    REQUIRE(saw_nonzero);      // the STEP leg really produced accents

    l.set_step(false, 8);
    bool leaked = false;
    for (int i = 0; i < 400000; ++i) {
        l.process();
        if (l.note_accent() != 0.f) leaked = true;
    }
    CHECK_FALSE(leaked);
}

TEST_CASE("accent: a STEP deck pushes its note accent into the active engine") {
    Part part;
    part.init(48000.f, 0xabcd1234u);          // null FX memory is fine here
    part.set_engine(ENGINE_SYNTH);
    part.mod().set_tempo_bpm(120.f);
    part.mod().set_rate(0.8f);
    part.mod().set_density(1.f);
    part.set_step(true, 8);

    float l = 0.f, r = 0.f;
    float seen_max = 0.f;
    bool seen_any = false;
    for (int i = 0; i < 48000 * 8; ++i) {
        part.process(l, r);
        const float a = part.synth().accent_for_test();
        if (a > 0.f) { seen_any = true; seen_max = std::max(seen_max, a); }
    }
    CHECK(seen_any);              // the push happens at all
    // 0.9 rather than 1.0: at DENSE 1.0 the fired accents are the whole rank
    // scale { r/(L-1) : r in 0..L-1 } (G2), whose top value IS exactly 1.0 and
    // is what this setup measurably reaches (probed: seen_max == 1.0 exactly,
    // 120 bpm, rate 0.8, 8 steps, over the 8 s window below). The bound is
    // deliberately looser than that measured value, not a concession to it:
    // the point of this case is only to separate "the whole DENSE-reveal
    // range reaches the engine" from a stub that tops out at a floor like
    // kAccentVelFloor/kAccentDecFloor (0.3f), and 0.9 does that with room to
    // spare without pinning this seam test to an exact top value the way G2
    // already does at the lane level.
    CHECK(seen_max > 0.9f);       // and it carries the whole range, not a floor

    // FLOW must not push a stale accent into the drone. No settle window here
    // on purpose: the accent is a stored scalar with no settling behaviour of
    // its own, so discarding samples after the switch would only shrink the
    // window this gate is supposed to cover. Measured (docs/engine-map.md
    // section 8): Part::process runs _fire_trigger() before _engine->
    // process(), and the lane fires on the first FLOW sample after
    // set_step(false), so the auto-drone's trigger never spends a stale STEP
    // accent -- 0 leaks over 360 switch points. Reading from sample 0 is what
    // makes this gate measure that invariant instead of assuming it.
    part.set_step(false, 8);
    float flow_max = 0.f;
    for (int i = 0; i < 48000 * 4; ++i) {
        part.process(l, r);
        flow_max = std::max(flow_max, part.synth().accent_for_test());
    }
    CHECK(flow_max == doctest::Approx(0.f));
}

namespace {

// Peak of one struck note, in STEP, at a given accent. Everything except the
// accent is identical between calls, so the ratio of two of these isolates
// exactly what the accent did.
template <class EngineT>
float note_peak(uint32_t seed, float accent, int n_chord = 1) {
    EngineT e;
    spky_contract::fresh(e, seed);
    e.set_flow(false);                       // STEP: struck notes, no drone
    e.set_accent(accent);
    const float chord[3] = {0.35f, 0.5f, 0.65f};
    e.trigger_chord(chord, n_chord);
    std::vector<float> buf = spky_contract::render_l(e, 48000);
    float pk = 0.f;
    for (float v : buf) pk = std::max(pk, std::fabs(v));
    return pk;
}

}  // namespace

TEST_CASE("accent G4: the accent scales a struck note down to the VEL floor") {
    const float loud = note_peak<SynthEngine>(99u, 0.f);
    const float soft = note_peak<SynthEngine>(99u, 1.f);
    REQUIRE(loud > 1e-4f);                   // the reference note actually sounded
    CHECK(soft / loud
          == doctest::Approx(SynthEngine::kAccentVelFloor).epsilon(0.05));
}

TEST_CASE("accent G4: WAVE gets the same scaling as SYNTH") {
    // Both are VoiceT instantiations, so this is cheap; it exists so that a
    // future engine added to the SynthEngineT family cannot quietly miss the
    // accent while SYNTH keeps the gate green.
    const float loud = note_peak<WaveEngine>(99u, 0.f);
    const float soft = note_peak<WaveEngine>(99u, 1.f);
    REQUIRE(loud > 1e-4f);
    CHECK(soft / loud
          == doctest::Approx(WaveEngine::kAccentVelFloor).epsilon(0.05));
}

TEST_CASE("accent G6: at accent 0, the chord still carries its equal-power compensation") {
    // Spec G6 (docs/superpowers/specs/2026-08-15-step-accent-design.md section 8)
    // is an ABSOLUTE-level claim: at accent == 0, a COLOR > 0 chord produces the
    // level it produced before this task -- i.e. vel's 1/sqrt(n) equal-power
    // compensation is still reaching the voice, not silently dropped by
    // whatever the accent multiply landed next to it.
    //
    // An earlier draft of this gate compared the accent-1-to-accent-0 ratio at
    // n==3 against the same ratio at n==1. That does NOT discriminate the
    // claim above: at a FIXED n, vel's 1/sqrt(n) factor is common to the
    // numerator and denominator of that ratio and cancels out of it whether it
    // is present or not. A version of _do_trigger that DROPS the `vel *`
    // multiply and REPLACES vel with the accent factor instead of composing
    // with it passes that comparison unchanged -- proven by measurement, not
    // argument (see the fix report for the exact numbers).
    //
    // This gate instead measures the chord/solo peak ratio directly, at
    // accent 0, where the two implementations diverge. Measured on this
    // machine: the correct code (vel * accent-factor) gives chord/solo ~=
    // 1.565; the replace-bug (accent-factor only, vel dropped) gives
    // chord/solo ~= 2.711 (~sqrt(3) times higher, because the bug hands every
    // voice of a 3-note chord a vel of 1.0 where the correct code hands it
    // 1/sqrt(3)). kChordSoloRatioCeiling sits strictly between the two. It is
    // a regression bound on today's mix, not a physical constant -- a
    // deliberate change to the chord voicing, the pan fan, or the gain
    // staging could legitimately move the correct-code value and this ceiling
    // would need re-measuring, same as any other by-ear bound in this suite.
    constexpr float kChordSoloRatioCeiling = 2.0f;
    const float solo  = note_peak<SynthEngine>(99u, 0.f, 1);
    const float chord = note_peak<SynthEngine>(99u, 0.f, 3);
    REQUIRE(solo > 1e-4f);
    CHECK(chord / solo < kChordSoloRatioCeiling);
}

namespace {

// Index of the last sample above 5 % of the note's own peak: a length measure
// that does not depend on the envelope's exact shape. Returns -1 if the note
// is still sounding when the buffer runs out, which the caller must reject --
// a truncated note would make every comparison meaningless.
template <class EngineT>
int note_len_samples(uint32_t seed, float dec_knob, float accent) {
    EngineT e;
    spky_contract::fresh(e, seed);
    e.set_cycle(0.25f);                  // keeps even DEC 1 inside the buffer
    e.set_flow(false);
    e.set_decay(dec_knob);
    e.set_accent(accent);
    const float chord[1] = {0.5f};
    e.trigger_chord(chord, 1);
    std::vector<float> buf = spky_contract::render_l(e, 48000 * 6);
    float pk = 0.f;
    for (float v : buf) pk = std::max(pk, std::fabs(v));
    if (pk <= 1e-5f) return -1;
    const float thr = pk * 0.05f;
    int last = 0;
    for (int i = 0; i < static_cast<int>(buf.size()); ++i)
        if (std::fabs(buf[i]) > thr) last = i;
    return (last >= static_cast<int>(buf.size()) - 2) ? -1 : last;
}

}  // namespace

TEST_CASE("accent G5: the DEC accent is inert at DEC 0 and real at DEC 1") {
    // Half one: at DEC 0 there is no room to take away, so the accent must not
    // change the note at all. A gate asserting only half two would pass on an
    // implementation that ignores the knob entirely.
    const int flat_loud = note_len_samples<SynthEngine>(99u, 0.f, 0.f);
    const int flat_soft = note_len_samples<SynthEngine>(99u, 0.f, 1.f);
    REQUIRE(flat_loud > 0);
    CHECK(flat_loud == flat_soft);

    // Half two: at DEC 1 the weakest note is measurably shorter.
    const int full = note_len_samples<SynthEngine>(99u, 1.f, 0.f);
    const int cut  = note_len_samples<SynthEngine>(99u, 1.f, 1.f);
    REQUIRE(full > 0);
    REQUIRE(cut > 0);
    CHECK(cut < full);
    // Bound tied to the named constant, not to a free-standing literal (the
    // plan's Global Constraints forbid a gate depending on the by-ear 0.3):
    // same shape as G4 above. Strictly stronger than the `< 0.6f` bound this
    // replaced -- measured margin at the shipped kAccentDecFloor (0.3082) is
    // 0.008 against this 10% window, whereas the old fixed bound only went
    // red once kAccentDecFloor was raised past 0.60 (measured: 0.30 -> 0.3082
    // PASS, 0.50 -> 0.5077 PASS, 0.55 -> 0.5562 PASS, 0.60 -> 0.6065 RED,
    // 0.70 -> 0.7053 RED against `< 0.6f`), so a by-ear retune in that gap
    // could have moved past 0.6 without this test ever seeing it.
    CHECK(static_cast<float>(cut) / static_cast<float>(full)
          == doctest::Approx(SynthEngine::kAccentDecFloor).epsilon(0.10));
}
