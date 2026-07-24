#include <doctest/doctest.h>
#include <cmath>
#include <initializer_list>
#include <string>
#include "pitch/quantizer.h"
using namespace spky;

TEST_CASE("quantizer: FREE is a bit-identical passthrough") {
    Quantizer q;
    q.init(48000.f);
    q.set_mode(QuantMode::Free);
    for (float v : {0.f, 0.123456f, 0.5f, 0.987654f, 1.f})
        CHECK(q.process(v) == v);   // exact, not Approx
}

TEST_CASE("quantizer: CHROM snaps to the nearest semitone (36-semi contract)") {
    Quantizer q;
    q.init(48000.f);
    q.set_mode(QuantMode::Chrom);
    CHECK(q.process(17.4f / 36.f) == doctest::Approx(17.f / 36.f));
    Quantizer q2;
    q2.init(48000.f);
    q2.set_mode(QuantMode::Chrom);
    CHECK(q2.process(17.6f / 36.f) == doctest::Approx(18.f / 36.f));
}

TEST_CASE("quantizer: SCALE default is dorian, ties resolve to the lower note") {
    Quantizer q;
    q.init(48000.f);
    // 18 semis: degree 6 is not in dorian; 17 and 19 are equidistant -> 17
    CHECK(q.process(0.5f) == doctest::Approx(17.f / 36.f));
}

TEST_CASE("quantizer: root shifts the allowed degrees") {
    Quantizer q;
    q.init(48000.f);
    q.set_root(1);   // dorian on root 1: degree (18-1)%12 = 5 is allowed
    CHECK(q.process(0.5f) == doctest::Approx(18.f / 36.f));
}

TEST_CASE("quantizer: every scale mask maps output onto its own degrees") {
    for (int s = 0; s < SCALE_LIST_COUNT; ++s) {
        for (float v : {0.f, 0.2f, 0.4f, 0.6f, 0.8f, 1.f}) {
            Quantizer fresh;                       // fresh per value: no hysteresis/slew carry-over
            fresh.init(48000.f);
            fresh.set_scale(SCALE_MASKS[s]);
            float semis = fresh.process(v) * 36.f;
            int k = static_cast<int>(semis + 0.5f);
            CHECK(std::fabs(semis - k) < 1e-4f);
            CHECK(((SCALE_MASKS[s] >> (k % 12)) & 1) == 1);
        }
    }
}

TEST_CASE("quantizer: hysteresis holds the note ~15 cents past the midpoint") {
    Quantizer q;
    q.init(48000.f);
    q.set_mode(QuantMode::Chrom);
    CHECK(q.process(17.0f / 36.f) == doctest::Approx(17.f / 36.f));
    // 17.55 is past the 17.5 midpoint but inside the hysteresis band -> hold
    CHECK(q.process(17.55f / 36.f) == doctest::Approx(17.f / 36.f));
    // 17.7 is clearly past -> switch
    CHECK(q.process(17.7f / 36.f) == doctest::Approx(18.f / 36.f));
    // coming back: 17.55 from above stays on 18 (symmetric band)
    CHECK(q.process(17.55f / 36.f) == doctest::Approx(18.f / 36.f));
}

TEST_CASE("quantizer: config change slews ~40 ms instead of clicking") {
    Quantizer q;
    q.init(48000.f);
    for (int i = 0; i < 100; ++i) q.process(0.5f);          // settled on 17/36
    q.set_root(1);                                          // target becomes 18/36
    float first = q.process(0.5f);
    CHECK(first > 17.f / 36.f);
    CHECK(first < 18.f / 36.f);                             // mid-slew, no jump
    float prev = first, last = first;
    for (int i = 0; i < 1920; ++i) {                        // 40 ms @ 48k
        last = q.process(0.5f);
        CHECK(last >= prev - 1e-6f);                        // monotonic ramp up
        prev = last;
    }
    CHECK(last == doctest::Approx(18.f / 36.f));
}

TEST_CASE("quantizer: switching to FREE is an instant passthrough") {
    Quantizer q;
    q.init(48000.f);
    for (int i = 0; i < 100; ++i) q.process(0.5f);
    q.set_mode(QuantMode::Free);
    CHECK(q.process(0.512345f) == 0.512345f);               // exact, no slew
}

TEST_CASE("quantizer: leaving FREE slews from the last raw output") {
    Quantizer q;
    q.init(48000.f);
    q.set_mode(QuantMode::Free);
    for (int i = 0; i < 100; ++i) q.process(0.5f);
    q.set_mode(QuantMode::Scale);                           // dorian -> 17/36
    float first = q.process(0.5f);
    CHECK(first < 0.5f);
    CHECK(first > 17.f / 36.f);                             // gliding down
    for (int i = 0; i < 1920; ++i) q.process(0.5f);
    CHECK(q.process(0.5f) == doctest::Approx(17.f / 36.f));
}

TEST_CASE("quantizer: slew length scales with the caller's interval") {
    // Called once per 96 samples, a 40 ms slew must still be 40 ms of audio
    // -- 1920 samples -- which is 20 calls, not 1920.
    //
    // Settling is detected by convergence, not by comparing against a fixed
    // value: the slewed output rises monotonically and then holds, so the
    // first post-change sample is a transient the output never revisits.
    // At root 0 and input 0.5 (= 18 semitones) the scale change moves the
    // nearest allowed note from 17 (dorian) to 18 (whole tone), so there is
    // a real distance to slew across.
    auto calls_to_settle = [](int interval) {
        Quantizer q;
        q.init(48000.f, interval);
        q.set_root(0);
        q.process(0.5f);                          // establish _last_out
        q.set_scale(SCALE_MASKS[SCALE_WHOLE]);    // on_change() arms the slew
        int calls = 0;
        float prev = q.process(0.5f);
        for (; calls < 5000; ++calls) {
            const float cur = q.process(0.5f);
            if (cur == prev) break;               // slew done, output holds
            prev = cur;
        }
        return calls;
    };

    CHECK(calls_to_settle(96) <= 25);    // ~20 control ticks, not ~1920
    CHECK(calls_to_settle(1)  > 100);    // default: still a sample-rate slew
}

// The masks are hand-written hex. A mistyped digit otherwise surfaces only as
// a melody that sounds slightly wrong, so state the semitones independently
// and rebuild the mask from them.
TEST_CASE("quantizer: masks match their documented semitones and names") {
    struct Row { int id; const char* name; int n; int semis[7]; };
    static const Row kRows[] = {
        { SCALE_AEOLIAN,   "Aeolian",        7, {0,2,3,5,7,8,10} },
        { SCALE_DORIAN,    "Dorian",         7, {0,2,3,5,7,9,10} },
        { SCALE_MIXO,      "Mixolydian",     7, {0,2,4,5,7,9,10} },
        { SCALE_LYDIAN,    "Lydian",         7, {0,2,4,6,7,9,11} },
        { SCALE_HIRAJOSHI, "Hirajoshi",      5, {0,2,3,7,8} },
        { SCALE_PYGMY,     "Pygmy",          5, {0,2,3,7,10} },
        { SCALE_MIN_PENT,  "Minor pent",     5, {0,3,5,7,10} },
        { SCALE_KUMOI,     "Kumoi",          5, {0,2,3,7,9} },
        { SCALE_MAJ_PENT,  "Major pent",     5, {0,2,4,7,9} },
        { SCALE_PHRYGIAN,  "Phrygian",       7, {0,1,3,5,7,8,10} },
        { SCALE_HIJAZ,     "Hijaz",          7, {0,1,4,5,7,8,10} },
        { SCALE_HARM_MIN,  "Harmonic minor", 7, {0,2,3,5,7,8,11} },
        { SCALE_WHOLE,     "Whole tone",     6, {0,2,4,6,8,10} },
    };
    CHECK(sizeof(kRows) / sizeof(kRows[0]) == static_cast<size_t>(SCALE_LIST_COUNT));

    for (const auto& r : kRows) {
        CAPTURE(r.name);
        uint16_t want = 0;
        for (int i = 0; i < r.n; ++i) want |= static_cast<uint16_t>(1u << r.semis[i]);
        CHECK(SCALE_MASKS[r.id] == want);
        CHECK((SCALE_MASKS[r.id] & 1u) == 1u);            // root always allowed
        CHECK((SCALE_MASKS[r.id] & ~0x0FFFu) == 0u);      // fits in 12 bits
        CHECK(std::string(SCALE_NAMES[r.id]) == std::string(r.name));
    }
}
