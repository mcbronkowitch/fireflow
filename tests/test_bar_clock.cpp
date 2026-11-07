// The bar clock (spec 2026-11-07-sampler-records-other-deck-on-the-bar §4.2,
// plan Task 3): Instrument::last_bar_frame() and friends.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include "instrument.h"
using namespace spky;

namespace {

// Engine-only instrument (no FxMem): the bar clock does not need the FX chain.
struct BarRig {
    Instrument inst;
    explicit BarRig(float bpm, float pace_norm = 0.5f) {
        inst.init(48000.f);
        inst.set_tempo_bpm(bpm);
        inst.set_pace(pace_norm);
    }
    // Run n samples, one at a time; record every new line's frame.
    std::vector<uint64_t> run(size_t n) {
        std::vector<uint64_t> lines;
        uint32_t seq = inst.bar_seq();
        float l, r;
        for (size_t i = 0; i < n; ++i) {
            inst.process(nullptr, nullptr, &l, &r, 1);
            if (inst.bar_seq() != seq) { seq = inst.bar_seq(); lines.push_back(inst.last_bar_frame()); }
        }
        return lines;
    }
};

// The reference: the transport counts frame * rate (rate = bpm*pace per
// 60*48000 frames) and a tick's value stands at the tick's frame, one control
// block ahead of the count it carries (the first tick, at frame 0, already
// holds one block's worth). So line k sits where frame * rate + 96 * rate
// reaches 4k: frame = 4k / rate - 96.
double ref_line(int k, double transport_bpm) {
    const double rate = transport_bpm / (60.0 * 48000.0);
    return 4.0 * k / rate - 96.0;
}

} // namespace

TEST_CASE("bar clock: the line frame matches a per-sample reference") {
    for (float bpm : {120.f, 130.f}) {
        INFO("bpm ", bpm);
        BarRig g(bpm);
        const double tbpm = g.inst.transport_bpm_for_test();
        const auto lines = g.run(48000 * 20);
        REQUIRE(lines.size() >= 9);              // 20 s at 120 BPM: 10 bars
        for (size_t j = 0; j < lines.size(); ++j) {
            const double want = ref_line(int(j) + 1, tbpm);
            INFO("line ", j + 1, " at ", lines[j], " want ", want);
            CHECK(std::fabs(double(lines[j]) - want) <= 1.0);
        }
        CHECK(g.inst.last_bar_frame() <= g.inst.frame_now());
    }
}

TEST_CASE("bar clock: PACE x1/4 and x4 move the lines") {
    for (float norm : {0.3f, 1.0f}) {
        INFO("pace norm ", norm);
        BarRig g(120.f, norm);
        const double tbpm = g.inst.transport_bpm_for_test();
        REQUIRE(tbpm == doctest::Approx(norm < 0.5f ? 30.0 : 480.0));
        const auto lines = g.run(48000 * 40);
        REQUIRE(lines.size() >= 4);
        const double bar = 4.0 * 60.0 * 48000.0 / tbpm;
        for (size_t j = 1; j < lines.size(); ++j)
            CHECK(std::fabs(double(lines[j] - lines[j - 1]) - bar) <= 1.0);
    }
}

TEST_CASE("bar clock: RST logs a line at the reset frame") {
    BarRig g(120.f);
    g.run(48000 + 12345);                        // mid-bar
    const uint32_t seq = g.inst.bar_seq();
    const uint64_t at = g.inst.frame_now();
    g.inst.reset_transport();
    CHECK(g.inst.bar_seq() == seq + 1);
    CHECK(g.inst.last_bar_frame() == at);
    // The count restarts from 0 and the ticks keep their 96-frame grid
    // (multiples of 96 from init), so the first tick after the reset plays
    // the part frame 0 plays in the reference.
    const auto lines = g.run(48000 * 3);
    REQUIRE(lines.size() >= 1);
    const uint64_t tick1 = at - at % 96 + 96;
    CHECK(std::fabs(double(lines[0] - tick1) - ref_line(1, g.inst.transport_bpm_for_test())) <= 1.0);
}

TEST_CASE("bar clock: restart(age) logs its back-dated frame") {
    BarRig g(120.f);
    g.inst.set_step(PART_B, true, 8);            // restart() needs a STEP deck
    g.run(48000 + 777);
    const uint64_t at = g.inst.frame_now();
    REQUIRE(g.inst.restart(480));
    CHECK(g.inst.last_bar_frame() == at - 480);
}

TEST_CASE("bar clock: a backwards clock snap logs nothing and doubles nothing") {
    // After a reset, clock_pulse_counted() sets the count to anchor + pace
    // (transport.h), so a transport that has run past beat 4 since the reset
    // is thrown back below it. The line at 4 was logged once; re-crossing it
    // must not log it again.
    BarRig g(120.f);
    g.inst.reset_transport();                    // arms the counted pulse
    g.run(48000 * 5 / 2);                        // 5 beats: line 1 logged
    const uint32_t seq = g.inst.bar_seq();
    REQUIRE(g.inst.transport_beats_for_test() > 4.0);
    g.inst.clock_pulse_counted();
    REQUIRE(g.inst.transport_beats_for_test() < 4.0);
    CHECK(g.inst.bar_seq() == seq);              // a jump back logs nothing
    const auto lines = g.run(48000 * 2);         // re-crosses 4, not yet 8
    CHECK(lines.empty());
    REQUIRE(g.inst.transport_beats_for_test() > 4.0);
    REQUIRE(g.inst.transport_beats_for_test() < 8.0);
    const auto later = g.run(48000 * 3);         // crosses 8
    CHECK(later.size() == 1);
}

TEST_CASE("bar clock: beat_in_bar counts 0..3") {
    BarRig g(120.f);
    std::vector<int> seen;
    int last = -1;
    float l, r;
    for (int i = 0; i < 48000 * 2 + 200; ++i) {   // one bar and a bit
        g.inst.process(nullptr, nullptr, &l, &r, 1);
        const int b = g.inst.beat_in_bar();
        if (b != last) { seen.push_back(b); last = b; }
    }
    REQUIRE(seen.size() >= 5);
    CHECK(seen[0] == 0);
    CHECK(seen[1] == 1);
    CHECK(seen[2] == 2);
    CHECK(seen[3] == 3);
    CHECK(seen[4] == 0);
}
