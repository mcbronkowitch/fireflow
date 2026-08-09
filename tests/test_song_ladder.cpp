#include <doctest/doctest.h>
#include "mod/song_ladder.h"
#include "mod/song_form.h"
#include "mod/phrase_gen.h"

using namespace spky;

TEST_CASE("song ladder covers every rung with legal enum values") {
    for (int i = 0; i < kSongLadderCount; ++i) {
        const SongRung& r = song_ladder_at(i);
        CHECK(r.form < static_cast<uint8_t>(Principle::kCount));
        CHECK(r.song < static_cast<uint8_t>(SongMode::kCount));
    }
}

TEST_CASE("rung 0 is the one that does not alternate") {
    CHECK(song_ladder_at(0).song == static_cast<uint8_t>(SongMode::Off));
}

TEST_CASE("no rung repeats another rung") {
    for (int i = 0; i < kSongLadderCount; ++i)
        for (int j = i + 1; j < kSongLadderCount; ++j)
            CHECK_FALSE((song_ladder_at(i).form == song_ladder_at(j).form &&
                         song_ladder_at(i).song == song_ladder_at(j).song));
}

TEST_CASE("the ladder clamps instead of reading out of bounds") {
    CHECK(song_ladder_at(-5).form == song_ladder_at(0).form);
    CHECK(song_ladder_at(999).form == song_ladder_at(kSongLadderCount - 1).form);
}

TEST_CASE("hysteresis holds a rung until the value clears the seam") {
    // 14 rungs span x = 0..13. Holding rung 3 means the value must pass 4.0
    // (a full step beyond the rung, = flow.cpp's 0.5 seam + 0.5 kHysteresisFrac)
    // before anything moves. norm = x / 13.
    const int   n = kSongLadderCount;
    const float d = 1.f / float(n - 1);
    CHECK(hyst_step(3, 3.4f * d, n) == 3);   // drifting inside the rung: hold
    CHECK(hyst_step(3, 3.9f * d, n) == 3);   // past the seam but not the guard
    CHECK(hyst_step(3, 4.2f * d, n) == 4);   // clears the guard: move
    CHECK(hyst_step(3, 1.8f * d, n) == 2);   // and the same downward
}

TEST_CASE("a large jump lands in one move, not one rung at a time") {
    CHECK(hyst_step(0, 1.f, kSongLadderCount) == kSongLadderCount - 1);
}
