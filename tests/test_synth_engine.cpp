#include <cmath>
#include <vector>

#include <doctest/doctest.h>

#include "synth/synth_engine.h"
#include "synth_engine_contract.h"

using namespace spky;

namespace {

void feed(SynthEngine& e, float pitch) {
    float t[LANE_COUNT] = { 0.f, 1.f, pitch, 0.f, 1.f };
    e.set_targets(t, 0.5f);
}

void fresh(SynthEngine& e) {
    e.set_seed(99);
    e.init(48000.f);
    e.set_sub(0.f);
    e.set_detune(0.f);
    e.set_cycle(1.f);
    feed(e, 0.5f);
}

std::vector<float> render_l(SynthEngine& e, int n) {
    std::vector<float> out(n);
    for (float& s : out) {
        float l = 0.f, r = 0.f;
        e.process(l, r);
        s = l;
    }
    return out;
}

int crossings(const std::vector<float>& v, size_t from = 0) {
    int n = 0;
    for (size_t i = from + 1; i < v.size(); ++i)
        if (v[i - 1] <= 0.f && v[i] > 0.f) ++n;
    return n;
}

} // namespace

TEST_CASE("synth: pitch contract - trigger(p) sounds at 110*8^p Hz, latched in STEP") {
    SynthEngine e;
    fresh(e);
    e.trigger(0.5f);
    auto v1 = render_l(e, 48000);
    const int n1 = crossings(v1, 4800);
    CHECK(n1 >= 275);
    CHECK(n1 <= 285);

    SynthEngine e2;
    fresh(e2);
    e2.trigger(0.5f);
    feed(e2, 0.9f);
    auto v2 = render_l(e2, 48000);
    CHECK(crossings(v2, 4800) == n1);
}

TEST_CASE("synth: FLOW drone pitch tracks") {
    SynthEngine e;
    fresh(e);
    feed(e, 0.25f);
    e.set_flow(true);
    auto v = render_l(e, 48000);
    const int n = crossings(v, 4800);
    CHECK(n >= 160);
    CHECK(n <= 172);

    feed(e, 0.75f);
    render_l(e, 9600);
    auto v2 = render_l(e, 48000);
    const int n2 = crossings(v2);
    CHECK(n2 >= 515);
    CHECK(n2 <= 532);
}

TEST_CASE("synth engine satisfies the shared part-engine contract") {
    contract_round_robin_and_steal<SynthEngine>();
    contract_flow_drone_and_surface<SynthEngine>();
    contract_chord_surface_and_hold<SynthEngine>();
    contract_deterministic_seed<SynthEngine>();
    contract_detune_is_independent_of_source<SynthEngine>();
}
