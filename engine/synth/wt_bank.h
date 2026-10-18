#pragma once

#include <cstdint>

namespace spky::wt {

inline constexpr int kFrameCount = 16;
inline constexpr int kMipCount = 7;
inline constexpr int kSamplesPerFrame = 2032;
inline constexpr int kTotalSamples = kFrameCount * kSamplesPerFrame;
extern const int kMipLength[kMipCount];
extern const int kMipOffset[kMipCount];
extern const int16_t kBankSamples[kTotalSamples];

// Where table() reads the bank from. The bank is a flash constant
// (kBankSamples); the firmware copies it into faster RAM at boot and points
// here at the copy (shell/wt_bank_d2.cpp, spec 2026-10-18-cpu-headroom-design.md
// section 4.1). Constant-initialised, so it is valid before any constructor
// runs; the desktop and VCV never move it.
namespace detail {
inline const int16_t* bank_ptr = kBankSamples;
} // namespace detail

// samples must hold kTotalSamples values in kBankSamples' layout; nullptr goes
// back to kBankSamples. Not for the audio thread: call it before anything
// plays.
inline void set_bank(const int16_t* samples) {
    detail::bank_ptr = samples ? samples : kBankSamples;
}
inline const int16_t* bank() { return detail::bank_ptr; }

inline const int16_t* table(int frame, int mip) {
    return bank() + frame * kSamplesPerFrame + kMipOffset[mip];
}

} // namespace spky::wt
