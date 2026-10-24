#include <doctest/doctest.h>
#include "mod/lane.h"
#include <cstring>
#include <vector>

using namespace spky;

// process()'s STEP step cache (fault R4): the step is searched only when the
// phase leaves the last result's interval or the grid changes. One lane run
// with the cache off (a search on every sample, as before) and the same lane
// run again with it on, both driven through every input that moves the grid
// or the phase -- STEPS, SHUFFLE (latched at even steps), RATE, a kick, a
// reset, a mode flip -- must emit the same bits and the same steps on every
// sample. One after the other because the cache switch is class-wide.
namespace {

constexpr int kSamples = 48000 * 30;

struct Run {
    std::vector<float> out;
    std::vector<int>   step;
    uint32_t           searches = 0;
};

Run run(bool cache) {
    ModLane::set_step_cache_for_test(cache);
    const uint32_t searches0 = ModLane::step_searches_for_test();
    ModLane l;
    l.set_melodic(true);
    l.set_flow_melody(true);
    l.init(48000.f, 4242);
    l.set_step(true, 8);
    l.set_rate_hz(2.f);
    l.set_shape(0.5f);
    l.set_smooth(0.3f);
    l.set_range(0.8f);
    l.set_variation(0.4f);
    l.set_shuffle(0.458f);
    Run r;
    r.out.reserve(kSamples);
    r.step.reserve(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        if (i == 48000 * 3)  l.set_shuffle(0.9f);
        if (i == 48000 * 6)  l.set_step(true, 9);
        if (i == 48000 * 9)  l.set_shuffle(0.f);
        if (i == 48000 * 12) l.kick(0.37f, 0.1f);
        // Backward jumps, short of a step and longer: the only way into an
        // earlier interval, which forward motion never reaches.
        if (i > 48000 * 12 && i < 48000 * 14 && i % 4800 == 0)
            l.kick((i / 4800) % 2 ? -0.05f : -0.2f, 0.f);
        if (i == 48000 * 15) l.set_step(true, 16);
        if (i == 48000 * 18) l.reset(0.61f);
        if (i == 48000 * 20) l.set_step(false, 16);
        if (i == 48000 * 22) l.set_step(true, 5);
        if (i == 48000 * 24) l.set_shuffle(0.3f);
        if (i >= 48000 * 26 && i % 480 == 0)                     // fast: many entries
            l.set_rate_hz(6.f + 4.f * static_cast<float>(i % 7));
        r.out.push_back(l.process());
        r.step.push_back(l.cur_step());
    }
    r.searches = ModLane::step_searches_for_test() - searches0;
    ModLane::set_step_cache_for_test(true);
    return r;
}

} // namespace

TEST_CASE("lane step cache: STEP output and steps are bit-identical to a search per sample") {
    const Run ref = run(false);
    const Run dut = run(true);
    long diffs = 0, step_diffs = 0;
    for (int i = 0; i < kSamples; ++i) {
        diffs += std::memcmp(&ref.out[i], &dut.out[i], sizeof(float)) != 0;
        step_diffs += ref.step[i] != dut.step[i];
    }
    CHECK(diffs == 0);
    CHECK(step_diffs == 0);
    // The reference really searched on every STEP sample, and the cache
    // really skipped: otherwise this test compares a path with itself.
    CHECK(ref.searches > static_cast<uint32_t>(kSamples * 9 / 10));
    CHECK(dut.searches < static_cast<uint32_t>(kSamples / 100));
    CHECK(dut.searches > 0u);
}
