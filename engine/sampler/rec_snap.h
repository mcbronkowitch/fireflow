#pragma once
// The REC snap rule (spec 2026-11-07-sampler-records-other-deck-on-the-bar
// §4.2, plan Task 5): a REC press becomes a target frame. Pure, no engine
// state; Instrument feeds it the bar clock and the deck's writer delay.
//
//   late   (the press is less than one window after the last bar line)
//          -> Past: the target is that line, which the delayed writer has
//             not reached yet;
//   early  (the next bar line is at most one beat away)
//          -> HeldNext: the request waits for the line;
//   else, or a beat longer than kSnapMaxBeatFrames
//          -> Now: the target is the press.
//
// The window is one beat for a start. For a stop it is min(one beat, the
// writer's delay), since a writer D behind live cannot reach further back.
#include <cmath>
#include <cstdint>

namespace spky {

inline constexpr float kSnapMaxBeatFrames = 72000.f;   // 1.5 s at 48 kHz
inline constexpr int   kSnapBeatsPerBar   = 4;

struct SnapIn {
    uint64_t now;              // the press's frame
    uint64_t last_bar;         // the last bar line's frame (<= now)
    double   beats;            // transport beats at the press
    float    frames_per_beat;
    uint32_t delay;            // the writer's D (a stop's late window)
    bool     start;            // a start press, or a stop
};

enum class SnapKind : uint8_t { Now, Past, HeldNext };

struct SnapOut {
    SnapKind kind;
    uint64_t target;           // valid for Now and Past
};

// A beat this long or shorter snaps; a longer one acts at once (Bastian,
// 2026-11-07: a bar over 6 s is a seventh of the buffer, snapping makes no
// sense there).
inline bool rec_snap_enabled_at(float frames_per_beat) {
    return frames_per_beat > 0.f && frames_per_beat <= kSnapMaxBeatFrames;
}

inline SnapOut rec_snap(const SnapIn& in) {
    if (!rec_snap_enabled_at(in.frames_per_beat)) return { SnapKind::Now, in.now };
    const double beat = double(in.frames_per_beat);
    const double window = in.start ? beat : (double(in.delay) < beat ? double(in.delay) : beat);
    const uint64_t since = in.now >= in.last_bar ? in.now - in.last_bar : 0;
    if (double(since) < window) return { SnapKind::Past, in.last_bar };
    double pos = std::fmod(in.beats, double(kSnapBeatsPerBar));
    if (pos < 0.0) pos += double(kSnapBeatsPerBar);
    if (double(kSnapBeatsPerBar) - pos <= 1.0) return { SnapKind::HeldNext, 0 };
    return { SnapKind::Now, in.now };
}

} // namespace spky
