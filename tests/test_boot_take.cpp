// The boot take (shell/boot_take.h): one reading of every pot at boot, ENGINE
// rows written first so ATTACK vs STAGES follows the freshly written engine.
// Spec: docs/superpowers/specs/2026-10-28-panel-state-persistence-design.md 4.4.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include "../shell/boot_take.h"
#include "../shell/generated_panel_map.h"
#include "../shell/mux_plan.h"
#include "../shell/controls.h"
#include "../shell/panel_step.h"
#include "../shell/pot_owner.h"
#include "../shell/scan_value.h"
#include "control/params.hpp"

namespace {

constexpr shell::Span kSpan{1000, 61000, true};

struct Knobs {
    float v[ffctl::NUM_PARAMS];
    Knobs() { for (int i = 0; i < ffctl::NUM_PARAMS; ++i) v[i] = ffctl::kInitParamDefaults[i]; }
};

// A raw word whose ENGINE_A knob value rounds to `slot`.
uint16_t engine_raw(int slot) {
    for (int raw = 1000; raw <= 61000; raw += 8) {
        const float v = shell::span_normalize(static_cast<uint16_t>(raw), kSpan);
        if (static_cast<int>(std::lround(shell::knob_from_pot(ffctl::ENGINE_A, v))) == slot)
            return static_cast<uint16_t>(raw);
    }
    FAIL("no raw for this engine slot");
    return 0;
}

float knob_at(int param, uint16_t raw) {
    return shell::knob_from_pot(param, shell::span_normalize(raw, kSpan));
}

// attack_first: the ATTACK row precedes the ENGINE row in the table.
struct Fixture {
    shell::ControlEntry rows[4];
    shell::ControlTable t;
    explicit Fixture(bool attack_first) {
        const shell::ControlEntry eng{0, 0, ffctl::ENGINE_A};
        const shell::ControlEntry att{0, 1, ffctl::ATTACK_A, -1, ffctl::STAGES_A};
        rows[0] = attack_first ? att : eng;
        rows[1] = attack_first ? eng : att;
        rows[2] = {0, 2, ffctl::FLUX_A};
        rows[3] = {0, 3, -1};
        t = {rows, 4};
    }
    void see_all(shell::BootTake& b, uint16_t eng_raw, uint16_t att_raw) const {
        for (int r = 0; r < t.count; ++r) {
            uint16_t raw = 30000;
            if (rows[r].param == ffctl::ENGINE_A) raw = eng_raw;
            if (rows[r].param == ffctl::ATTACK_A) raw = att_raw;
            shell::boot_take_collect(b, r, raw);
        }
    }
};

} // namespace

TEST_CASE("boot take: not ready until every row seen") {
    Fixture f(false);
    shell::BootTake b{};
    CHECK_FALSE(shell::boot_take_ready(b, f.t));
    for (int r = 0; r < f.t.count - 1; ++r) shell::boot_take_collect(b, r, 20000);
    CHECK_FALSE(shell::boot_take_ready(b, f.t));
    shell::boot_take_collect(b, f.t.count - 1, 20000);
    CHECK(shell::boot_take_ready(b, f.t));
}

TEST_CASE("boot take: ENGINE row last in table still lands first") {
    Fixture f(true);
    Knobs k;   // ENGINE_A starts off the BBD
    REQUIRE(std::lround(k.v[ffctl::ENGINE_A]) != shell::kEngineSlotBbd);
    const float attack_before = k.v[ffctl::ATTACK_A];
    shell::BootTake b{};
    const uint16_t att = 45000;
    f.see_all(b, engine_raw(shell::kEngineSlotBbd), att);
    shell::boot_take_apply(b, f.t, kSpan, k.v);
    CHECK(b.done);
    CHECK(std::lround(k.v[ffctl::ENGINE_A]) == shell::kEngineSlotBbd);
    CHECK(k.v[ffctl::STAGES_A] == doctest::Approx(knob_at(ffctl::STAGES_A, att)));
    CHECK(k.v[ffctl::ATTACK_A] == attack_before);
}

TEST_CASE("boot take: non-BBD engine writes ATTACK") {
    Fixture f(true);
    Knobs k;
    k.v[ffctl::ENGINE_A] = static_cast<float>(shell::kEngineSlotBbd);   // prior state: BBD
    const float stages_before = k.v[ffctl::STAGES_A];
    shell::BootTake b{};
    const uint16_t att = 45000;
    f.see_all(b, engine_raw(1), att);
    shell::boot_take_apply(b, f.t, kSpan, k.v);
    CHECK(k.v[ffctl::ATTACK_A] == doctest::Approx(knob_at(ffctl::ATTACK_A, att)));
    CHECK(k.v[ffctl::STAGES_A] == stages_before);
}

TEST_CASE("boot take: MOD latch ignored") {
    Fixture f(false);
    Knobs k;
    k.v[ffctl::MODBTN] = 1.0f;
    const float depth_before = k.v[ffctl::MODD_FLUX_A];
    shell::BootTake b{};
    f.see_all(b, engine_raw(1), 30000);
    shell::boot_take_apply(b, f.t, kSpan, k.v);
    CHECK(k.v[ffctl::FLUX_A] == doctest::Approx(knob_at(ffctl::FLUX_A, 30000)));
    CHECK(k.v[ffctl::MODD_FLUX_A] == depth_before);
}

TEST_CASE("boot take: value matches the filter path") {
    const shell::ControlEntry row{0, 2, ffctl::FLUX_A};
    const shell::ControlTable t{&row, 1};
    for (uint16_t raw : {uint16_t(1000), uint16_t(31000), uint16_t(61000)}) {
        Knobs a, c;
        shell::BootTake b{};
        shell::boot_take_collect(b, 0, raw);
        shell::boot_take_apply(b, t, kSpan, a.v);

        // A pot that has just been taken at `raw`: reference far away, then the move.
        shell::PotOwner o{};
        shell::PotFilter pf{};
        float v = -1.f;
        shell::pot_row_step(row, o, pf, static_cast<uint16_t>(raw < 30000 ? raw + 4000 : raw - 4000),
                            kSpan, c.v, &v);
        shell::pot_row_step(row, o, pf, raw, kSpan, c.v, &v);
        REQUIRE(o.owned);
        CHECK(a.v[ffctl::FLUX_A] == c.v[ffctl::FLUX_A]);
    }
}

TEST_CASE("boot take: a SENSE_0 row snaps with its pin's band, as the live path does") {
    // 40 counts above zero: inside SENSE_0's band (64), outside the coupon's (16).
    const shell::ControlEntry row{0, 2, ffctl::FLUX_A, 0};
    const shell::ControlTable t{&row, 1};
    const uint16_t raw = 1040;
    Knobs a, c;
    shell::BootTake b{};
    shell::boot_take_collect(b, 0, raw);
    shell::boot_take_apply(b, t, kSpan, a.v);
    CHECK(a.v[ffctl::FLUX_A] == shell::knob_from_pot(ffctl::FLUX_A, 0.0f));

    shell::PotOwner o{};
    shell::PotFilter pf{};
    float v = -1.f;
    shell::pot_row_step(row, o, pf, static_cast<uint16_t>(raw + 4000), kSpan, c.v, &v);
    shell::pot_row_step(row, o, pf, raw, kSpan, c.v, &v);
    REQUIRE(o.owned);
    CHECK(a.v[ffctl::FLUX_A] == c.v[ffctl::FLUX_A]);
}

TEST_CASE("boot take: collect after done is ignored") {
    Fixture f(false);
    Knobs k;
    shell::BootTake b{};
    f.see_all(b, engine_raw(1), 30000);
    shell::boot_take_apply(b, f.t, kSpan, k.v);
    const uint16_t before = b.raw[2];
    shell::boot_take_collect(b, 2, 5000);
    CHECK(b.raw[2] == before);
}

TEST_CASE("boot take: apply writes only seen rows") {
    Fixture f(false);   // rows: ENGINE_A, ATTACK_A, FLUX_A, param -1
    Knobs k;
    Knobs before;
    shell::BootTake b{};
    shell::boot_take_collect(b, 2, 45000);   // FLUX_A only
    shell::boot_take_apply(b, f.t, kSpan, k.v);
    CHECK(k.v[ffctl::FLUX_A] == doctest::Approx(knob_at(ffctl::FLUX_A, 45000)));
    CHECK(k.v[ffctl::ENGINE_A] == before.v[ffctl::ENGINE_A]);
    CHECK(k.v[ffctl::ATTACK_A] == before.v[ffctl::ATTACK_A]);
    CHECK(k.v[ffctl::STAGES_A] == before.v[ffctl::STAGES_A]);
}

TEST_CASE("boot take: param -1 row writes nothing") {
    const shell::ControlEntry row{0, 3, -1};
    const shell::ControlTable t{&row, 1};
    Knobs k;
    Knobs before;
    shell::BootTake b{};
    shell::boot_take_collect(b, 0, 45000);
    shell::boot_take_apply(b, t, kSpan, k.v);
    CHECK(b.done);
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) CHECK(k.v[i] == before.v[i]);
}

TEST_CASE("boot take: invalid span writes nothing and stays not done") {
    Fixture f(false);
    Knobs k;
    Knobs before;
    shell::BootTake b{};
    f.see_all(b, engine_raw(1), 45000);
    shell::boot_take_apply(b, f.t, shell::Span{1000, 61000, false}, k.v);
    CHECK_FALSE(b.done);
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) CHECK(k.v[i] == before.v[i]);
    // A later apply with a valid span still works.
    shell::boot_take_apply(b, f.t, kSpan, k.v);
    CHECK(b.done);
    CHECK(k.v[ffctl::FLUX_A] == doctest::Approx(knob_at(ffctl::FLUX_A, 30000)));
}

TEST_CASE("boot take: table larger than kMaxRows is not ready and apply writes nothing") {
    static shell::ControlEntry rows[shell::kMaxRows + 1];
    for (auto& r : rows) r = {0, 0, ffctl::FLUX_A};
    const shell::ControlTable t{rows, shell::kMaxRows + 1};
    Knobs k;
    Knobs before;
    shell::BootTake b{};
    for (int r = 0; r < shell::kMaxRows; ++r) shell::boot_take_collect(b, r, 45000);
    shell::boot_take_collect(b, shell::kMaxRows, 45000);   // out of range: ignored
    CHECK_FALSE(shell::boot_take_ready(b, t));
    shell::boot_take_apply(b, t, kSpan, k.v);
    CHECK_FALSE(b.done);
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) CHECK(k.v[i] == before.v[i]);
}

// The firmware's liveness rests on this (spec 2026-10-28 section 4.4): the
// complete take needs every row seen, and panel_scan_tick() only ever sees a
// row through its walk -- every step, every sense pin, group_at() /
// channel_at() / find_control(). One sweep of Rev A's chain has to reach every
// kRevaTable row, param -1 rows included, or boot_take_ready() never turns
// true (the firmware then falls back to a partial take after three sweeps).
TEST_CASE("boot take: one sweep of kRevaChain reaches every kRevaTable row") {
    const shell::ChainProfile& c = shell::kRevaChain;
    const shell::ControlTable& t = shell::kRevaTable;
    shell::BootTake b{};
    int hits[shell::kMaxRows] = {};
    for (int step = 0; step < shell::scan_steps(c); ++step)
        for (int s = 0; s < c.sense_pins; ++s) {
            const int g = shell::group_at(c, step, s);
            if (g < 0) continue;
            const shell::ControlEntry* e = shell::find_control(t, g, shell::channel_at(c, step, s));
            if (e == nullptr) continue;
            const int row = static_cast<int>(e - t.entries);
            REQUIRE(row >= 0);
            REQUIRE(row < shell::kMaxRows);
            ++hits[row];
            shell::boot_take_collect(b, row, 30000);
        }
    for (int r = 0; r < t.count; ++r) {
        CAPTURE(r);
        CHECK(hits[r] == 1);
    }
    CHECK(shell::boot_take_ready(b, t));
}

// The boot mute (probe 4, 2026-10-28): until the take is done the engine
// still plays the record's knob values -- on the board, about 30 ms of the
// saved FILT A with the pot already turned shut. The gate silences that and
// fades in once the take has written.
TEST_CASE("boot gate: silent until the take is done, then a fade-in, then unity") {
    shell::BootGate g{};
    float l[48], r[48];
    auto fill = [&] { for (int i = 0; i < 48; ++i) { l[i] = 0.5f; r[i] = -0.5f; } };

    fill();
    shell::boot_gate_apply(g, false, l, r, 48);
    for (int i = 0; i < 48; ++i) { CHECK(l[i] == 0.0f); CHECK(r[i] == 0.0f); }

    // Done: the fade starts at zero gain and rises monotonically.
    float last = -1.0f;
    int   blocks = 0;
    for (; blocks < 100; ++blocks) {
        fill();
        shell::boot_gate_apply(g, true, l, r, 48);
        for (int i = 0; i < 48; ++i) {
            CHECK(l[i] >= last);
            CHECK(r[i] == doctest::Approx(-l[i]));
            last = l[i];
        }
        if (l[47] == 0.5f) break;
    }
    // About kBootFadeSamples long: whole blocks, at least one partial.
    CHECK((blocks + 1) * 48 >= shell::kBootFadeSamples);
    CHECK(blocks * 48 <= shell::kBootFadeSamples);
    // Once faded in, the signal passes untouched, done or not (no re-mute).
    fill();
    shell::boot_gate_apply(g, false, l, r, 48);
    CHECK(l[0] == 0.5f);
    CHECK(r[47] == -0.5f);
}
