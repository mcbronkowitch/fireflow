// Sampler takes on SD: the save policy (when, how fast, when to give up).
// Pure data logic, no hardware type.
// Spec: docs/superpowers/specs/2026-11-06-sampler-takes-on-sd-design.md §4.3.
#include <doctest/doctest.h>
#include <cstdint>
#include <vector>
#include "../shell/take_policy.h"

using namespace shell;

namespace {

// A board stand-in: two decks the test edits, a block counter, and a driver
// that runs the saver like the firmware's foreground does -- every step it
// asks for succeeds at once unless told otherwise.
struct Board {
    DeckView d[2] = {{0, false, false, 0}, {0, false, false, 0}};
    uint32_t now = 0;
    TakeSaver s;
    std::vector<TakeStep> log;
    std::vector<uint32_t> piece_at;
    int saves_ok[2] = {0, 0};
    int fails_seen = 0;
    bool fail_finish = false;

    Board() { take_seed(s, 0, d[0].gen, 0); take_seed(s, 1, d[1].gen, 0); }

    TakeStep tick()
    {
        const TakeStep st = take_next(s, d, now);
        log.push_back(st);
        const int deck = s.deck;
        switch (st) {
            case TakeStep::Begin: take_begun(s, now); break;
            case TakeStep::Piece:
                piece_at.push_back(now);
                if (take_still_valid(s, d[deck])) take_piece_done(s, now);
                else take_aborted(s, now);
                break;
            case TakeStep::Finish:
                if (fail_finish) { take_finished(s, false, now); ++fails_seen; break; }
                if (take_still_valid(s, d[deck])) { take_finished(s, true, now); ++saves_ok[deck]; }
                else take_aborted(s, now);
                break;
            default: break;
        }
        ++now;
        return st;
    }
    void run(uint32_t blocks) { for (uint32_t i = 0; i < blocks; ++i) tick(); }
    // A take on deck p: recording for `len` blocks, then idle with a new gen.
    void take(int p, uint32_t len, uint32_t frames)
    {
        d[p].gen += 1; d[p].recording = true;
        run(len);
        d[p].gen += 1; d[p].recording = false; d[p].frames = frames;
    }
    int count(TakeStep st) const
    {
        int n = 0;
        for (TakeStep x : log) n += x == st;
        return n;
    }
};

} // namespace

TEST_CASE("policy: saves 5 s after a take, once") {
    Board b;
    b.run(50);
    b.take(0, 50, 4800);                   // the take ends as block 100 begins
    REQUIRE(b.now == 100);
    b.run(kTakeQuietBlocks);               // blocks 100 .. 2599
    CHECK(b.count(TakeStep::Begin) == 0);  // not at 2 599
    b.tick();                              // block 2 600
    CHECK(b.log.back() == TakeStep::Begin);
    b.run(1000);
    CHECK(b.saves_ok[0] == 1);
    b.run(100000);
    CHECK(b.saves_ok[0] == 1);             // and never again for the same take
    CHECK(b.count(TakeStep::Begin) == 1);
}

TEST_CASE("policy: overdub within the quiet window saves once") {
    Board b;
    b.take(0, 100, 4800);
    b.run(1000);                           // inside the window
    b.take(0, 200, 4800);                  // the overdub
    b.run(kTakeQuietBlocks + 1000);
    CHECK(b.saves_ok[0] == 1);
    CHECK(b.count(TakeStep::Begin) == 1);
}

TEST_CASE("policy: no save right after a restore") {
    Board b;
    b.d[0].gen = 41;
    b.d[0].frames = 96000;
    take_seed(b.s, 0, 41, b.now);
    b.run(100000);
    CHECK(b.count(TakeStep::Begin) == 0);
}

TEST_CASE("policy: an unseeded deck is never due") {
    Board b;
    b.s = TakeSaver{};                     // nothing seeded (no card yet, or a load in flight)
    b.d[0].gen = 5; b.d[0].frames = 4800;
    b.run(100000);
    CHECK(b.count(TakeStep::Begin) == 0);
}

TEST_CASE("policy: pacing") {
    Board b;
    const uint32_t frames = 10 * kTakePieceBytes / 8 + 100;   // 11 pieces, the last one short
    b.take(0, 10, frames);
    b.run(kTakeQuietBlocks + 11 * kTakePieceEveryBlocks + 50);
    REQUIRE(b.saves_ok[0] == 1);
    REQUIRE(b.piece_at.size() == 11);
    for (size_t i = 1; i < b.piece_at.size(); ++i)
        CHECK(b.piece_at[i] - b.piece_at[i - 1] == kTakePieceEveryBlocks);
}

TEST_CASE("policy: piece lengths cover the take exactly") {
    TakeSaver s;
    take_seed(s, 0, 0, 0);
    DeckView d[2] = {{1, false, false, 5000}, {0, false, false, 0}};
    uint32_t now = kTakeQuietBlocks + 10;
    // gen 0 -> 1 seen now; quiet from here
    REQUIRE(take_next(s, d, 0) == TakeStep::Idle);
    REQUIRE(take_next(s, d, now) == TakeStep::Begin);
    take_begun(s, now);
    uint32_t covered = 0;
    while (true) {
        const TakeStep st = take_next(s, d, now);
        if (st == TakeStep::Finish) break;
        if (st == TakeStep::Piece) {
            CHECK(take_piece_offset(s) == covered);
            covered += take_piece_len(s);
            take_piece_done(s, now);
        }
        ++now;
    }
    CHECK(covered == 5000u * 8u);
}

TEST_CASE("policy: REC, a clear and new content abort a save in flight") {
    for (int what = 0; what < 3; ++what) {
        Board b;
        b.take(0, 10, 200000);             // many pieces: the save runs a while
        b.run(kTakeQuietBlocks + 5 * kTakePieceEveryBlocks);
        REQUIRE(b.s.deck == 0);
        REQUIRE(b.s.begun);
        if (what == 0) b.d[0].recording = true;              // REC pressed
        if (what == 1) { b.d[0].gen += 1; b.d[0].frames = 0; }  // SHIFT + REC: a clear
        if (what == 2) b.d[0].gen += 2;                      // a take and its end slipped in
        b.tick();
        INFO("case " << what);
        CHECK(b.log.back() == TakeStep::Abort);
        CHECK(b.s.deck == -1);
        CHECK(b.s.fails == 0);             // an abort is not a failure
        // ...and the deck waits a full new quiet window.
        b.d[0].recording = false;
        const uint32_t from = b.now;
        while (b.log.back() != TakeStep::Begin && b.now < from + 2 * kTakeQuietBlocks) b.tick();
        CHECK(b.now - 1 - from >= kTakeQuietBlocks - 1);
    }
}

TEST_CASE("policy: the sequence lock catches a change between the two reads") {
    TakeSaver s;
    take_seed(s, 0, 0, 0);
    DeckView d[2] = {{2, false, false, 100}, {0, false, false, 0}};
    take_next(s, d, 0);
    REQUIRE(take_next(s, d, kTakeQuietBlocks) == TakeStep::Begin);
    take_begun(s, kTakeQuietBlocks);
    CHECK(take_still_valid(s, d[0]));
    DeckView changed = d[0];
    changed.gen += 1;
    CHECK_FALSE(take_still_valid(s, changed));
    changed = d[0];
    changed.recording = true;
    CHECK_FALSE(take_still_valid(s, changed));
}

TEST_CASE("policy: clear saves frames 0") {
    Board b;
    b.take(0, 10, 0);                      // a deck with nothing in it, but a new generation
    b.run(kTakeQuietBlocks + 10);
    CHECK(b.saves_ok[0] == 1);
    CHECK(b.piece_at.empty());
    CHECK(b.count(TakeStep::Finish) == 1);
}

TEST_CASE("policy: A before B; one at a time") {
    Board b;
    b.d[1].gen += 2; b.d[1].frames = 30000;
    b.d[0].gen += 2; b.d[0].frames = 30000;
    // "One at a time" is structural (TakeSaver holds one save in flight);
    // what is checked is the order and that B is not starved.
    std::vector<int> begins;
    for (uint32_t i = 0; i < 2 * kTakeQuietBlocks + 1000; ++i) {
        const TakeStep st = b.tick();
        if (st == TakeStep::Begin && (begins.empty() || begins.back() != b.s.deck))
            begins.push_back(b.s.deck);
    }
    REQUIRE(begins.size() == 2);
    CHECK(begins[0] == 0);
    CHECK(begins[1] == 1);
    CHECK(b.saves_ok[0] == 1);
    CHECK(b.saves_ok[1] == 1);
}

TEST_CASE("policy: a failure retries after a full quiet window") {
    Board b;
    b.fail_finish = true;
    b.take(0, 10, 100);
    b.run(kTakeQuietBlocks + 50);
    REQUIRE(b.fails_seen == 1);
    b.run(kTakeQuietBlocks - 100);         // not yet
    CHECK(b.fails_seen == 1);
    b.run(200);                            // now
    CHECK(b.fails_seen == 2);
}

TEST_CASE("policy: three failures turn saving off") {
    Board b;
    b.fail_finish = true;
    b.take(0, 10, 100);
    b.run(kTakeFailLimit * (kTakeQuietBlocks + 50));
    CHECK(b.fails_seen == kTakeFailLimit);
    CHECK(b.s.off);
    b.take(0, 10, 100);
    b.run(10);
    CHECK(b.log.back() == TakeStep::Off);
}

TEST_CASE("policy: a success in between resets the failure count") {
    Board b;
    b.fail_finish = true;
    for (int k = 0; k < kTakeFailLimit - 1; ++k) { b.take(0, 10, 100); b.run(kTakeQuietBlocks + 50); }
    b.fail_finish = false;
    b.take(0, 10, 100);
    b.run(kTakeQuietBlocks + 50);
    CHECK(b.s.fails == 0);
    b.fail_finish = true;
    for (int k = 0; k < kTakeFailLimit - 1; ++k) { b.take(0, 10, 100); b.run(kTakeQuietBlocks + 50); }
    CHECK_FALSE(b.s.off);
}

TEST_CASE("policy: a loading deck is never due") {
    Board b;
    b.d[0].gen += 1; b.d[0].loading = true; b.d[0].frames = 100;
    b.run(100000);
    CHECK(b.count(TakeStep::Begin) == 0);
}

TEST_CASE("policy: the deck stuck in P5 saves once it is unstuck") {
    // Spec P5: switched off SAMPLER mid-take, the deck reads "recording"
    // until it is back on SAMPLER and its fade-out ends.
    Board b;
    b.d[0].gen += 1; b.d[0].recording = true;
    b.run(100000);
    CHECK(b.count(TakeStep::Begin) == 0);
    b.d[0].gen += 1; b.d[0].recording = false; b.d[0].frames = 20000;
    b.run(kTakeQuietBlocks + 500);
    CHECK(b.saves_ok[0] == 1);
}

TEST_CASE("policy: a forced save ignores the generation") {
    Board b;
    b.d[0].frames = 1000;
    take_force(b.s, 0);
    b.run(kTakeQuietBlocks + 50);
    CHECK(b.saves_ok[0] == 1);
    b.run(100000);
    CHECK(b.saves_ok[0] == 1);             // once per force
}
