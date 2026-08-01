#pragma once
namespace spky {

namespace link_tuning {

// The sparse end of the pattern. Beyond sixteen repeats between audible ones
// the result stops reading as an echo pattern; clamping keeps something
// audible rather than silently muting the control, and at a 1/16 rung sixteen
// repeats is a bar.
constexpr int kMaxSkip = 16;

// Edge on the gate's gain. The only new smoother in this design, and it sits
// on a LEVEL rather than on the clock, so it cannot interact with the 30 ms
// delay-time slew. Without it a gate on a continuous signal clicks.
constexpr float kGateRampS = 0.003f;

}  // namespace link_tuning

}  // namespace spky
