#pragma once
#include "util/math.h"

namespace spky {

// COLOR reads the chord the layer would have voiced as a QUALITY rather than
// a set of pitches (spec 2026-07-26-body-resonator §5/§7). A BODY deck has one
// voice (BodyVoice::kEngineVoices == 1), so the chord never sounds as pitches;
// what survives of it is its character. Returns the material CHARACTER,
// -1..+1: negative compresses the partials (flat -- drum, plate), 0 is
// harmonic, positive stretches them (sharp -- bell). The AMOUNT is DETUNE's
// and is applied in BodyVoice::_apply_params, not here.
//
// CONTRACT (tests/test_material.cpp):
//   - n <= 1, or a null pointer, returns exactly 0.f -- one note has no quality
//   - the result is inside [-1, +1] for ANY input, including unsorted,
//     out-of-range and duplicated pitches
//   - deterministic and stateless: same input, same output, no state kept
//   - two chords of different quality give different values
//
// Everything below that -- the partial table, kGain, the slot divisor -- is
// TUNING MATERIAL for the Task 12 listening pass, NOT a contract (spec §7:
// "the mapping from chord quality to ratio character is tuning material for
// the listening pass, not a contract"). Change the numbers freely. Do not add
// state, libm calls, or a per-sample caller.
//
// --- why these numbers, so they can be argued with ------------------------
//
// The DIRECTION is derived rather than invented. Each chord tone is folded
// into one octave and compared against the nearest partial of the ROOT's own
// harmonic series. A tone sitting SHARP of the nearest natural partial can
// only be contained by a body whose partials are stretched; a tone sitting
// FLAT of it asks for a compressed body. The signed distance to the nearest
// partial IS "which way the partials stretch" -- §5's words, in semitones.
//
// The table is 12*log2(k) mod 12 for k = 2..15 (only the distinct classes):
//
//   k    pitch class     k     pitch class
//   2/4  0.0000          11    5.5132
//   9    2.0391          3/6   7.0196
//   5    3.8631          13    8.4053
//                        7     9.6883
//                        15   10.8827
//
// What falls out of it, for equal-tempered intervals above the root:
//
//   minor 3rd  -0.86   major 3rd  +0.14      perfect 5th  -0.02
//   perfect4th -0.51   tritone    +0.49      minor 6th    -0.41
//   major 6th  +0.60   minor 7th  +0.31      major 7th    +0.12
//   semitone   +1.00   major 2nd  -0.04
//
// i.e. the minor intervals ask for compression and the major ones for a mild
// stretch, without that having been put in by hand -- and a power chord
// (root + fifth) lands on ~0, which is the most harmonic thing a chord can be.
// A major triad reads about +0.08 at the gain below: "near-harmonic partials,
// a tuned singing bell", which is the sound §5 asks a major triad for.
//
// MAGNITUDE and DIRECTION are taken separately, and that is deliberate:
//
//   magnitude = mean |d| over the three non-root slots
//   direction = sign of the sum of d
//
// A single scalar cannot be both "very broken" and "no net direction", and a
// semitone cluster is exactly that chord: it holds a tone a semitone sharp of
// the octave AND a tone flat of the fifth partial, so a plain signed mean
// cancels it down to near-harmonic. That would make the most extreme COLOR
// setting the tamest sound on the deck, against §5's "each step toward
// clusters and extensions asks for a more broken mode ratio". Magnitude from
// |d| keeps the ordering the spec asks for --
//
//   power chord < major triad < dom7 < minor triad < min7 < cluster
//
// -- and costs continuity where the sum crosses zero: the character then jumps
// between +mag and -mag. That jump only happens when a chord tone is added,
// removed, or changes quality, which is already a discrete event that
// recomputes the whole bank, so nothing smooth is being broken. If the
// listening pass disagrees, the continuous alternative is one line: return
// kGain * sum / kSlots and drop the |d| accumulator.
inline float chord_character(const float* pitches_norm, int n) {
    if (pitches_norm == nullptr || n <= 1) return 0.f;   // one note has no quality

    // Harmonic-series pitch classes, ascending, with the octave repeated at
    // 12 so a tone just under it measures against the octave and not against
    // partial 15. TUNING MATERIAL.
    static constexpr int   kPartials = 9;
    static constexpr float kPartialPc[kPartials] = {
        0.f,     2.0391f, 3.8631f, 5.5132f, 7.0196f,
        8.4053f, 9.6883f, 10.8827f, 12.f
    };
    // Divisor: the chord layer's non-root slot count (ChordBuilder::kMaxNotes
    // - 1). FIXED, not n-1, so that adding a slot adds character instead of
    // re-averaging it away -- "each step toward clusters and extensions asks
    // for a more broken mode ratio" (spec §5). TUNING MATERIAL.
    static constexpr float kSlots = 3.f;
    // Spread control. At 1.0 only a full stack of semitones would reach +-1;
    // 1.5 puts a semitone cluster at ~0.95, a min7 at ~0.60, a minor triad at
    // ~0.44 and a major triad at ~0.08, and clamps almost nothing. TUNING
    // MATERIAL -- this is the knob for "how much does COLOR do".
    static constexpr float kGain = 1.5f;

    // Pitches arrive on the 0..1 = 36-semitone contract (engine/pitch/chord.h,
    // synth_engine.cpp::pitch_to_hz), and slot 0 is the root. Clamping to the
    // contract first is what makes out-of-range input harmless.
    const float root = clampf(pitches_norm[0], 0.f, 1.f);

    float sum = 0.f;   // net direction
    float mag = 0.f;   // how far from natural, regardless of direction
    for (int i = 1; i < n; ++i) {
        // Interval in semitones, folded into one octave. The bounded while
        // loops replace fmod: the input is clamped, so |s| <= 36 and each
        // loop runs at most three times. No libm anywhere on this path.
        float pc = (clampf(pitches_norm[i], 0.f, 1.f) - root) * 36.f;
        while (pc < 0.f)   pc += 12.f;
        while (pc >= 12.f) pc -= 12.f;

        float best = 12.f;                       // larger than any real distance
        float best_abs = 12.f;
        for (int k = 0; k < kPartials; ++k) {
            const float d = pc - kPartialPc[k];
            const float ad = d < 0.f ? -d : d;
            if (ad < best_abs) { best_abs = ad; best = d; }
        }
        sum += best;
        mag += best_abs;
    }

    // sum == 0 with mag > 0 needs an exact float cancellation; it takes the
    // stretched side, and the clamp bounds the jump either way.
    const float dir = sum < 0.f ? -1.f : 1.f;
    // Clamped, so the bound holds for any n and any input -- including chords
    // longer than the four slots the layer can actually build.
    return clampf(dir * kGain * mag / kSlots, -1.f, 1.f);
}

} // namespace spky
