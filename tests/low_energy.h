#pragma once
#include <cmath>
#include <vector>

namespace spky {

// A crude low-band (~200 Hz) meter, built here rather than borrowed from any
// engine: whichever engine is under test owns its OWN filters, and using one
// of them to measure EDGE's effect on itself would beg the question. Shared
// by tests/test_sampler_engine.cpp (Task 6) and tests/test_bbd_engine.cpp
// (Task 7) -- both DPTH/EDGE tasks needed the identical meter, so it moved
// here rather than staying duplicated (spec 2026-08-19 voice-knobs-dpth-edge,
// Task 7's ruling on the brief).
inline float low_energy(const std::vector<float>& v, float sr = 48000.f) {
    const float a = 1.f - std::exp(-6.2831853f * 200.f / sr);
    float y = 0.f; double acc = 0.0;
    for (float x : v) { y += a * (x - y); acc += (double)y * y; }
    return std::sqrt(acc / v.size());
}

}  // namespace spky
