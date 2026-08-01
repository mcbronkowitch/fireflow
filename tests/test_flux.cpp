#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "fx/flux.h"
#include "mod/divisions.h"

using namespace spky;

struct FluxTapeMem {
    std::vector<float> l;
    std::vector<float> r;
    FluxTapeMem() : l(Flux::kMaxSamples), r(Flux::kMaxSamples) {}
    void init(Flux& f, float sr = 48000.f) { f.init(sr, l.data(), r.data()); }
};

static RhythmView rhythm(int32_t g0, int32_t g1, bool valid = true) {
    RhythmView rv;
    rv.gap[0] = g0;
    rv.gap[1] = g1;
    rv.valid = valid;
    return rv;
}

static void run_silence(Flux& f, int samples) {
    for (int i = 0; i < samples; ++i) {
        float l = 0.f, r = 0.f;
        f.process(l, r);
    }
}

TEST_CASE("flux tape: synced 1/4 at 120 BPM targets 0.5 seconds") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_bpm(120.f);
    f.set_rate(3);
    CHECK(f.delay_time() == doctest::Approx(0.5f));
    CHECK(f.delay_target_for_test() == doctest::Approx(0.5f));
}

TEST_CASE("flux tape: stereo input remains stereo") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_on(true, true);
    f.set_mix(1.f);
    f.set_feedback(0.f);
    float energy_l = 0.f, energy_r = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l = i == 0 ? 1.f : 0.f;
        float r = 0.f;
        f.process(l, r);
        energy_l += l * l;
        energy_r += r * r;
    }
    CHECK(energy_l > 1e-6f);
    CHECK(energy_r == 0.f);
}

TEST_CASE("flux tape: FXT time is x1/4 x1 x4 through the shared slew") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_on(true, true);
    f.set_rate(3);
    f.set_bpm(120.f);
    f.set_time_mod(0.5f);
    CHECK(f.delay_target_for_test() == doctest::Approx(0.5f));
    f.set_time_mod(0.f);
    CHECK(f.delay_target_for_test() == doctest::Approx(0.125f));
    f.set_time_mod(1.f);
    CHECK(f.delay_target_for_test() == doctest::Approx(2.f));
    CHECK(f.delay_current_for_test() != doctest::Approx(2.f));
    run_silence(f, 48000);
    CHECK(f.delay_current_for_test() == doctest::Approx(2.f).epsilon(0.01));
}

TEST_CASE("flux tape: one missing channel leaves the block disengaged") {
    std::vector<float> l(Flux::kMaxSamples), r(Flux::kMaxSamples);
    Flux a, b;
    a.init(48000.f, l.data(), nullptr);
    b.init(48000.f, nullptr, r.data());
    a.set_on(true, true);
    b.set_on(true, true);
    CHECK(!a.engaged());
    CHECK(!b.engaged());
}

TEST_CASE("flux tape: benchmark RATE targets strictly decrease") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_bpm(120.f);
    float previous = 100.f;
    for (const int rate : {0, 3, 6, 8, 11}) {
        f.set_rate(rate);
        const float target = f.delay_target_for_test();
        CHECK(target > 0.f);
        CHECK(target < previous);
        previous = target;
    }
}

TEST_CASE("flux tape: fully off is bit-exact dry") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    for (int i = 0; i < 2000; ++i) {
        const float dry_l = std::sin(0.01f * i) * 0.4f;
        const float dry_r = std::cos(0.013f * i) * 0.3f;
        float l = dry_l, r = dry_r;
        f.process(l, r);
        CHECK(l == dry_l);
        CHECK(r == dry_r);
    }
}

TEST_CASE("flux tape: feedback produces decaying repeats") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(6);
    f.set_feedback(0.45f);
    f.set_mix(1.f);
    run_silence(f, 20000);
    std::vector<float> out(50000);
    for (int i = 0; i < static_cast<int>(out.size()); ++i) {
        float l = i < 32 ? 1.f : 0.f;
        float r = 0.f;
        f.process(l, r);
        out[i] = l;
    }
    auto peak = [&](int center) {
        float p = 0.f;
        for (int i = center - 700; i < center + 700; ++i)
            p = std::max(p, std::fabs(out[i]));
        return p;
    };
    const float p1 = peak(12000);
    const float p2 = peak(24000);
    const float p3 = peak(36000);
    CHECK(p1 > 1e-3f);
    CHECK(p2 < p1);
    CHECK(p3 < p2);
}

TEST_CASE("flux slice: norm endpoints hit 1/2 and 1/32") {
    CHECK(kFluxRateCount == 12);
    CHECK(std::string(kDivisions[kFluxRateOffset + flux_division_index(0.f)].name) == "1/2");
    CHECK(std::string(kDivisions[kFluxRateOffset + flux_division_index(1.f)].name) == "1/32");
}

TEST_CASE("flux tape: LINK neutral is bit-identical to no rhythm") {
    FluxTapeMem ma, mb;
    Flux plain, linked;
    ma.init(plain);
    mb.init(linked);
    for (Flux* f : {&plain, &linked}) {
        f->set_on(true, true);
        f->set_mix(1.f);
        f->set_feedback(0.5f);
    }
    linked.set_rhythm(rhythm(12000, 6000));
    linked.set_link(0.f);
    for (int i = 0; i < 60000; ++i) {
        const float in_l = i < 32 ? 1.f : 0.f;
        const float in_r = i < 32 ? -0.5f : 0.f;
        float al = in_l, ar = in_r, bl = in_l, br = in_r;
        plain.process(al, ar);
        linked.process(bl, br);
        REQUIRE(al == bl);
        REQUIRE(ar == br);
    }
}

TEST_CASE("flux tape: DRAG alternates between neighbour intervals") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_on(true, true);
    f.set_rate(3);
    f.set_rhythm(rhythm(12000, 6000));
    f.set_link(1.f);
    CHECK(f.drag_time_s() == doctest::Approx(0.25f));
    run_silence(f, 12000);
    CHECK(f.drag_time_s() == doctest::Approx(0.125f));
    run_silence(f, 6000);
    CHECK(f.drag_time_s() == doctest::Approx(0.25f));
}

TEST_CASE("flux tape: DRAG interpolates geometrically") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_rhythm(rhythm(12000, 18000));
    f.set_link(0.5f);
    CHECK(f.drag_time_s() == doctest::Approx(0.353553f).epsilon(0.001));
}

static void thin_setup(Flux& f, FluxTapeMem& mem, int32_t g0, int32_t g1) {
    mem.init(f);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(11);
    f.set_mix(1.f);
    f.set_feedback(0.f);
    f.set_rhythm(rhythm(g0, g1));
}

TEST_CASE("flux tape: THIN preserves the neighbour gaps in whole repeats") {
    FluxTapeMem mem;
    Flux f;
    thin_setup(f, mem, 12000, 6000);
    f.set_link(-1.f);
    CHECK(f.thin_n_for_test(0) == 4);
    CHECK(f.thin_n_for_test(1) == 2);
}

TEST_CASE("flux tape: THIN sounds one repeat in n and ducks the rest") {
    FluxTapeMem mem;
    Flux f;
    thin_setup(f, mem, 12000, 6000);
    f.set_link(-1.f);
    run_silence(f, 4500);
    CHECK(f.gate_for_test() == doctest::Approx(1.f).epsilon(0.02));
    run_silence(f, 3000);
    CHECK(f.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));
    run_silence(f, 9000);
    CHECK(f.gate_for_test() == doctest::Approx(1.f).epsilon(0.02));
}

TEST_CASE("flux tape: THIN scheduler works for a DRAG-unusable rhythm") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_on(true, true);
    f.set_rate(11);
    f.set_link(-1.f);
    f.set_rhythm(rhythm(6000, 20));
    bool ducked = false;
    for (int i = 0; i < 18000; ++i) {
        float l = 0.f, r = 0.f;
        f.process(l, r);
        if (f.gate_for_test() < 0.1f) ducked = true;
    }
    CHECK(ducked);
}

TEST_CASE("flux tape: invalid rhythm leaves DRAG inert and THIN open") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_rate(3);
    f.set_rhythm(rhythm(12000, 6000, false));
    f.set_link(1.f);
    CHECK(f.drag_time_s() == doctest::Approx(0.5f));
    f.set_link(-1.f);
    f.set_on(true, true);
    run_silence(f, 20000);
    CHECK(f.gate_for_test() == 1.f);
}

TEST_CASE("flux tape: re-init resets the LINK guard") {
    FluxTapeMem mem;
    Flux f;
    auto setup = [&] {
        mem.init(f);
        f.set_on(true, true);
        f.set_rate(11);
        f.set_rhythm(rhythm(12000, 6000));
    };
    setup();
    f.set_link(-1.f);
    setup();
    f.set_link(-1.f);
    run_silence(f, 7500);
    CHECK(f.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));
}

TEST_CASE("flux tape: re-init resets the FXT time guard") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_time_mod(1.f);
    CHECK(f.delay_target_for_test() == doctest::Approx(2.f));
    mem.init(f);
    f.set_time_mod(1.f);
    CHECK(f.delay_target_for_test() == doctest::Approx(2.f));
}
