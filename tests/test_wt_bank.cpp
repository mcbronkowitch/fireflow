#include "doctest/doctest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "synth/wt_bank.h"

using namespace spky;

TEST_CASE("wt_bank: fixed 16-frame seven-mip layout") {
    CHECK(wt::kFrameCount == 16);
    CHECK(wt::kMipCount == 7);
    const int expected_len[] = {1024, 512, 256, 128, 64, 32, 16};
    const int expected_off[] = {0, 1024, 1536, 1792, 1920, 1984, 2016};
    for (int m = 0; m < wt::kMipCount; ++m) {
        CHECK(wt::kMipLength[m] == expected_len[m]);
        CHECK(wt::kMipOffset[m] == expected_off[m]);
    }
    CHECK(wt::kSamplesPerFrame == 2032);
    CHECK(wt::kTotalSamples == 32512);
}

TEST_CASE("wt_bank: every table is bounded, centered, and audible") {
    for (int f = 0; f < wt::kFrameCount; ++f) {
        for (int m = 0; m < wt::kMipCount; ++m) {
            const int16_t* p = wt::table(f, m);
            const int n = wt::kMipLength[m];
            int peak = 0;
            int64_t sum = 0;
            for (int i = 0; i < n; ++i) {
                peak = std::max(peak, std::abs(static_cast<int>(p[i])));
                sum += p[i];
            }
            CHECK(peak >= 12000);
            CHECK(peak <= 32112);
            CHECK(std::abs(static_cast<double>(sum) / n) < 2.0);
        }
    }
}

TEST_CASE("wt_bank: frame zero is the sine anchor") {
    const int16_t* p = wt::table(0, 0);
    for (int i = 0; i < wt::kMipLength[0]; i += 17) {
        const float expected = std::sin(6.283185307179586f * i / 1024.f);
        CHECK(p[i] / 32112.f == doctest::Approx(expected).epsilon(0.002));
    }
}
