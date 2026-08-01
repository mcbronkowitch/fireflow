#pragma once
#include <cstdint>

namespace spkyvcv {
inline bool is_modern_link_version(bool marker_is_integer,
                                   std::int64_t marker_value) {
    return marker_is_integer && marker_value >= 1;
}

inline float migrate_legacy_link(float v) {
    if (!(v < 0.f)) return 0.f;  // positive, zero and NaN become neutral
    const float thin = -v;
    return thin > 1.f ? 1.f : thin;
}
} // namespace spkyvcv
