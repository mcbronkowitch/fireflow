#pragma once

#include <cstdint>
#include "mod/phrase_gen.h"
#include "mod/song_form.h"

namespace spky {

// The hardware SONG knob is one axis through the 5x7 (Principle, SongMode)
// grid -- 35 combinations do not fit on a 9 mm pot and nobody learns them.
// The path runs tame -> churning, modelled on the WANDER macro of the terrain
// layer that used to sit on top of this engine: it already swept FORM and SONG
// together and was tuned by ear there. That layer was deleted 2026-08-14; the
// macro's own curve is recorded in
// docs/attic/2026-08-05-flow-engine-layer.md.
//
// One deliberate difference from WANDER: it excludes SongMode::Off because a
// WANDER macro must never disable wandering. This knob is a structure SELECTOR,
// not a wander amount, so "no alternation at all" is a legitimate destination
// and owns rung 0.
//
// This table is TASTE. The tests below pin its structure (legal values, no
// duplicates, Off at rung 0) and deliberately do NOT pin the order -- retune it
// by ear without fighting a test.
struct SongRung { uint8_t form; uint8_t song; };

inline constexpr int kSongLadderCount = 14;

inline const SongRung& song_ladder_at(int idx) {
    static constexpr SongRung kLadder[kSongLadderCount] = {
        {0, 6}, {1, 6},                  // no alternation, two generators
        {0, 0}, {1, 0},                  // AAAB: the sparsest alternation
        {0, 1}, {1, 1},                  // ABAB
        {2, 0}, {2, 1}, {2, 2},          // hierarchical, opening up
        {3, 1}, {3, 3}, {3, 4},          // call/response, then Build, Rotate
        {4, 4}, {4, 5},                  // ostinato against Rotate, then Mirror
    };
    if (idx < 0) idx = 0;
    if (idx >= kSongLadderCount) idx = kSongLadderCount - 1;
    return kLadder[idx];
}

// Hysteretic quantizer for a pot that selects a discrete value. Without it a
// pot parked on a seam re-quantises every tick and the engine gets a new
// FORM/SONG dozens of times a minute -- 14 flips over one hover sweep, measured
// in the deleted terrain layer, which carried the twin of this function
// (docs/attic/2026-08-07-glow-genre-and-scale-design.md discusses that twin and
// its kHysteresisFrac = 0.5). Hold `cur` until the value passes the seam by a
// further half step, then snap to whatever step is nearest, so a big turn still
// lands in one move.
//
// >= / <= , not > / < : in Rack, SONG is a configSwitch with snapping, so
// params[SONG_A].getValue() is always an exact integer rung, and the host
// divides by (count - 1) before calling this -- the same normalisation
// re-multiplies back to an exact integer here. A single-detent turn from
// rung n therefore lands x EXACTLY on n + 1.0f, one full step past the seam
// at n + 0.5f, which is a real move, not chatter held for hysteresis. A
// strict `>` guard silently ate 25 of 26 adjacent single-detent turns on
// this branch's flagship control (tests/test_song_ladder.cpp, review
// finding CRITICAL 1).
inline int hyst_step(int cur, float norm, int count) {
    if (count < 2) return 0;
    if (norm < 0.f) norm = 0.f;
    if (norm > 1.f) norm = 1.f;
    const float x = norm * static_cast<float>(count - 1);
    int nearest = static_cast<int>(x + 0.5f);
    if (nearest < 0) nearest = 0;
    if (nearest > count - 1) nearest = count - 1;
    const float n = static_cast<float>(cur);
    // kEps absorbs the float rounding a caller's own normalise-then-scale
    // round trip introduces (Fireflow.cpp divides SONG's integer rung by
    // (count - 1), this file multiplies it back) -- a single detent should
    // land x on n +/- 1.0f exactly, but round trip error can leave it a few
    // ULPs to either side of the seam, which a bare >= / <= would treat as
    // still-inside for roughly half of all rungs.
    constexpr float kEps = 1e-4f;
    if (x >= n + 1.0f - kEps || x <= n - 1.0f + kEps) return nearest;
    return cur;
}

} // namespace spky
