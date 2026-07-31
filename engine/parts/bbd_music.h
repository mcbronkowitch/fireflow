#pragma once
#include <cmath>
#include "fx/bbd.h"
#include "util/math.h"

namespace spky {
namespace bbd_music {

// The division ladder. `div` multiplies the master-lane CYCLE -- Part feeds
// set_cycle 1/master_hz, i.e. the whole phrase, not a beat (part.cpp:423-426),
// and master_hz excludes clock_scale() and the EVOLVE walk. The ladder must
// therefore reach down to a step boundary, which in STEP is cycle/steps.
//
// Eleven rungs, straight and triplet interleaved, fastest first.
inline constexpr float kDivs[] = {
    1.f / 32.f, 1.f / 24.f, 1.f / 16.f, 1.f / 12.f, 1.f / 8.f, 1.f / 6.f,
    1.f / 4.f,  1.f / 3.f,  1.f / 2.f,  2.f / 3.f,  1.f,
};
inline constexpr int kDivCount = 11;

// The shortest delay the physics can hold: below it kMinStages forces a clock
// above kClockMaxHz. See "Corrections to the spec" in the plan -- the clamp is
// at the SHORT end, because f_lo is defined as kMinStages/(2T), which makes the
// stage count at f_lo exactly kMinStages for EVERY T. The long end cannot
// overflow.
inline constexpr float kMinDelayS =
    static_cast<float>(bbd_tuning::kMinStages) / (2.f * bbd_tuning::kClockMaxHz);

// LANE_SIZE is a continuously modulated lane, so a bare nearest-rung round
// chatters at every boundary. One rung of overlap: the held rung keeps the lane
// until it passes a NEIGHBOURING rung's centre.
class DivLadder {
public:
    int process(float lane) {
        const float x = clampf(lane, 0.f, 1.f) * (kDivCount - 1);
        if (x >= static_cast<float>(_i + 1)) _i = static_cast<int>(x);
        else if (x <= static_cast<float>(_i - 1)) _i = static_cast<int>(x + 0.999999f);
        if (_i < 0) _i = 0;
        if (_i > kDivCount - 1) _i = kDivCount - 1;
        return _i;
    }
    int  index() const { return _i; }
    void reset(int i) { _i = i < 0 ? 0 : (i > kDivCount - 1 ? kDivCount - 1 : i); }
private:
    int _i = 6;   // 1/4
};

// The reachable clock window for a delay time T, plus what had to give.
struct Window {
    float t_eff = kMinDelayS;
    float f_lo = bbd_tuning::kClockMaxHz;
    float f_hi = bbd_tuning::kClockMaxHz;
    bool  time_clamped = false;      // T was raised to kMinDelayS
    bool  scale_truncated = false;   // span < 3 octaves: STEP loses the top
};

inline Window window(float t_seconds) {
    Window w;
    w.time_clamped = !(t_seconds >= kMinDelayS);
    w.t_eff = w.time_clamped ? kMinDelayS : t_seconds;
    w.f_lo = (bbd_tuning::kMinStages * 0.5f) / w.t_eff;
    const float ceil_stages = (bbd_tuning::kMaxStages * 0.5f) / w.t_eff;
    w.f_hi = ceil_stages < bbd_tuning::kClockMaxHz ? ceil_stages
                                                   : bbd_tuning::kClockMaxHz;
    if (w.f_hi < w.f_lo) w.f_hi = w.f_lo;
    w.scale_truncated = w.f_hi < w.f_lo * 8.f;   // 8x == 36 semitones
    return w;
}

// LANE_PITCH in FLOW: geometric across the whole window, so the lane always
// spans its full travel and there is no dead zone at the top. The interval per
// lane step is NOT constant across divisions -- accepted, because this lane is
// a bend, not a keyboard.
inline float clock_flow(const Window& w, float lane) {
    const float n = clampf(lane, 0.f, 1.f);
    if (!(w.f_hi > w.f_lo)) return w.f_lo;
    return w.f_lo * std::pow(w.f_hi / w.f_lo, n);
}

// LANE_PITCH in STEP: the quantizer's normalized output carries 36 semitones
// (Quantizer::SPAN_SEMIS == 36, quantizer.h:66), so convert back to semitones
// and apply them as a ratio on the clock. Clamped to the window, which is what
// `scale_truncated` warns about.
inline float clock_step(const Window& w, float q_norm) {
    const float semis = clampf(q_norm, 0.f, 1.f) * 36.f;
    const float f = w.f_lo * std::pow(2.f, semis * (1.f / 12.f));
    return f > w.f_hi ? w.f_hi : f;
}

// delay = stages/(2 f_clk), so holding the delay at T means stages = 2 T f_clk.
// Note it does not involve fs: the reachable delay range is identical at 44.1
// and 192 kHz, unlike a buffer measured in samples.
inline int stages_for(const Window& w, float f_clk) {
    int s = static_cast<int>(2.f * w.t_eff * f_clk + 0.5f);
    if (s < bbd_tuning::kMinStages) s = bbd_tuning::kMinStages;
    if (s > bbd_tuning::kMaxStages) s = bbd_tuning::kMaxStages;
    return s;
}

}  // namespace bbd_music
}  // namespace spky
