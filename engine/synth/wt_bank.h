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

inline const int16_t* table(int frame, int mip) {
    return kBankSamples + frame * kSamplesPerFrame + kMipOffset[mip];
}

} // namespace spky::wt
