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

#if defined(SPKY_WT_BANK_RAM)
// Probe (CPU headroom round, 2026-10-18): the host copies the bank out of QSPI
// into RAM at boot and points g_bank at the copy. Until then it points at
// kBankSamples itself, so every read is valid either way.
extern const int16_t* g_bank;
inline const int16_t* table(int frame, int mip) {
    return g_bank + frame * kSamplesPerFrame + kMipOffset[mip];
}
#else
inline const int16_t* table(int frame, int mip) {
    return kBankSamples + frame * kSamplesPerFrame + kMipOffset[mip];
}
#endif

} // namespace spky::wt
