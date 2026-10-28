// The playing image's per-block glue (shell/panel_step.h): one pot reading
// from its row to the knob vector, the law's events, the FLUX rows' channels.
// panel_scan.cpp is firmware only; these are the seams it runs, on the host.
// Spec: docs/superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md
// sections 2, 3, 5 and 7.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include "../shell/controls.h"
#include "../shell/generated_panel_map.h"
#include "../shell/mux_plan.h"
#include "../shell/panel_step.h"
#include "../shell/pot_owner.h"
#include "../shell/scan_value.h"
#include "control/clock_law.hpp"
#include "control/control_law.h"
#include "control/params.hpp"

namespace {

constexpr int kT = shell::kPotTouch;
constexpr shell::Span kSpan{1000, 61000, true};

struct Knobs {
    float v[ffctl::NUM_PARAMS];
    Knobs() { for (int i = 0; i < ffctl::NUM_PARAMS; ++i) v[i] = ffctl::kInitParamDefaults[i]; }
};

// The knob value a raw word becomes for `param`, through the same path.
float knob_at(int param, int raw) {
    return shell::knob_from_pot(param, shell::span_normalize(static_cast<uint16_t>(raw), kSpan));
}

// The Rev A row of `param` (exactly one per FLUX deck, see the last case).
const shell::ControlEntry& reva_row(int param) {
    for (int i = 0; i < shell::kRevaTable.count; ++i)
        if (shell::kRevaTable.entries[i].param == param) return shell::kRevaTable.entries[i];
    FAIL("no Rev A row for this param");
    return shell::kRevaTable.entries[0];
}

const shell::ControlEntry kFiltA = shell::kCouponControls[2];   // FILT_A, wreathed

} // namespace

TEST_CASE("panel step: the move that takes a pot writes its current position") {
    REQUIRE(kFiltA.param == ffctl::FILT_A);
    Knobs k;
    const float before = k.v[ffctl::FILT_A];
    shell::PotOwner o{};
    shell::PotFilter f{};
    float v = -1.f;

    // The first valid reading is the reference; the filter reports it, nobody writes.
    CHECK(shell::pot_row_step(kFiltA, o, f, 30000, kSpan, k.v, &v));
    CHECK(k.v[ffctl::FILT_A] == before);
    // Past the hysteresis, inside the touch threshold: reported, not written.
    constexpr int kNear = 30000 + kT - 14;
    constexpr int kTake = 30000 + kT + 1;
    CHECK(shell::pot_row_step(kFiltA, o, f, kNear, kSpan, k.v, &v));
    CHECK(k.v[ffctl::FILT_A] == before);
    // ref + kT + 1 takes the pot. It is only 15 counts from the last reported
    // word, inside the filter's band: without the reset on Took the filter
    // would swallow it and the jump would wait for the next 16-count move.
    REQUIRE(kTake - kNear <= shell::kPotHysteresis);
    REQUIRE(kTake - 30000 == kT + 1);
    CHECK(shell::pot_row_step(kFiltA, o, f, kTake, kSpan, k.v, &v));
    CHECK(o.owned);
    CHECK(k.v[ffctl::FILT_A] == doctest::Approx(knob_at(ffctl::FILT_A, kTake)));
    CHECK(k.v[ffctl::FILT_A] != doctest::Approx(knob_at(ffctl::FILT_A, 30000)));
    // Owned: it follows.
    CHECK(shell::pot_row_step(kFiltA, o, f, 31000, kSpan, k.v, &v));
    CHECK(k.v[ffctl::FILT_A] == doctest::Approx(knob_at(ffctl::FILT_A, 31000)));
}

TEST_CASE("panel step: an owned Rev A pot holds against its pin's measured wander (fault R3)") {
    // SENSE_0's worst untouched one-second spread was 57 counts, SENSE_1-3's 23
    // (docs/hardware/reva-bring-up.md, R3). An owned pot must not re-emit
    // inside those, and must still follow a move past its pin's band.
    struct Case { int param; int sense; int quiet; int moved; };
    for (const Case c : {Case{ffctl::SHAPE_A, 0, 57, 65}, Case{ffctl::TUNE_B, 1, 23, 33}}) {
        const shell::ControlEntry& e = reva_row(c.param);
        REQUIRE(e.sense == c.sense);
        Knobs k;
        shell::PotOwner o{};
        shell::PotFilter f{};
        float v = -1.f;
        constexpr int kTake = 30000 + kT + 1;
        shell::pot_row_step(e, o, f, 30000, kSpan, k.v, &v);
        REQUIRE(shell::pot_row_step(e, o, f, kTake, kSpan, k.v, &v));
        REQUIRE(o.owned);
        const float held = k.v[c.param];
        CHECK_FALSE(shell::pot_row_step(e, o, f, static_cast<uint16_t>(kTake + c.quiet), kSpan, k.v, &v));
        CHECK_FALSE(shell::pot_row_step(e, o, f, static_cast<uint16_t>(kTake - c.quiet), kSpan, k.v, &v));
        CHECK(k.v[c.param] == held);
        CHECK(shell::pot_row_step(e, o, f, static_cast<uint16_t>(kTake + c.moved), kSpan, k.v, &v));
        CHECK(k.v[c.param] == doctest::Approx(knob_at(c.param, kTake + c.moved)));
    }
}

TEST_CASE("panel step: MOD toggled mid-turn writes the depth only after a new move") {
    Knobs k;
    shell::PotOwner o{};
    shell::PotFilter f{};
    float v = -1.f;
    k.v[ffctl::MODBTN] = 0.f;

    // An owned FILT_A pot, turning.
    shell::pot_row_step(kFiltA, o, f, 30000, kSpan, k.v, &v);
    shell::pot_row_step(kFiltA, o, f, 30100, kSpan, k.v, &v);
    shell::pot_row_step(kFiltA, o, f, 30200, kSpan, k.v, &v);
    REQUIRE(o.owned);
    REQUIRE(k.v[ffctl::FILT_A] == doctest::Approx(knob_at(ffctl::FILT_A, 30200)));
    const float filt = k.v[ffctl::FILT_A];
    const float modd = k.v[ffctl::MODD_FILT_A];

    // MOD latches (apply_key_edges() sets MODBTN to 1) while the pot stands
    // at 30210. Nothing is written until it moves more than kT from there --
    // including the reading at 30240, which the filter does report.
    k.v[ffctl::MODBTN] = 1.f;
    for (int raw : {30210, 30220, 30240, 30210 + kT, 30210 - kT}) {
        CAPTURE(raw);
        shell::pot_row_step(kFiltA, o, f, static_cast<uint16_t>(raw), kSpan, k.v, &v);
        CHECK(k.v[ffctl::MODD_FILT_A] == modd);
        CHECK(k.v[ffctl::FILT_A] == filt);
    }
    CHECK_FALSE(o.owned);

    // Past kT from the toggle position: the depth jumps to where the pot is.
    CHECK(shell::pot_row_step(kFiltA, o, f, 30210 + kT + 1, kSpan, k.v, &v));
    CHECK(o.owned);
    CHECK(k.v[ffctl::MODD_FILT_A]
          == doctest::Approx(knob_at(ffctl::MODD_FILT_A, 30210 + kT + 1)));
    CHECK(k.v[ffctl::FILT_A] == filt);
}

TEST_CASE("panel step: a bbd_edge drops FLUX to 0 and holds it until the pot moves") {
    int flux[2];
    shell::flux_channels(shell::kRevaTable, shell::kRevaChain, flux);
    REQUIRE(flux[0] >= 0);
    REQUIRE(flux[1] >= 0);
    constexpr int kN = shell::mux_total(shell::kRevaChain);
    static shell::PotOwner  owners[kN];
    static shell::PotFilter filters[kN];
    for (int i = 0; i < kN; ++i) { owners[i] = {}; filters[i] = {}; }
    const shell::ControlEntry& e = reva_row(ffctl::FLUX_A);
    shell::PotOwner&  o = owners[flux[0]];
    shell::PotFilter& f = filters[flux[0]];
    Knobs k;
    float v = -1.f;

    // FLUX_A owned and turning.
    shell::pot_row_step(e, o, f, 20000, kSpan, k.v, &v);
    shell::pot_row_step(e, o, f, 20100, kSpan, k.v, &v);
    shell::pot_row_step(e, o, f, 20200, kSpan, k.v, &v);
    REQUIRE(o.owned);
    REQUIRE(k.v[ffctl::FLUX_A] > 0.f);
    const float flux_b = k.v[ffctl::FLUX_B];

    control::Events ev;
    ev.bbd_edge[0] = true;
    shell::apply_law_events(ev, k.v, owners, flux);
    CHECK(k.v[ffctl::FLUX_A] == 0.f);
    CHECK(k.v[ffctl::FLUX_B] == flux_b);
    CHECK_FALSE(o.owned);

    // The pot keeps standing about where it was: FLUX holds 0, including on
    // readings the filter reports (20247 is 47 counts from 20200).
    for (int raw : {20200, 20200 + kT - 1, 20200 - (kT - 1), 20200 + kT}) {
        CAPTURE(raw);
        shell::pot_row_step(e, o, f, static_cast<uint16_t>(raw), kSpan, k.v, &v);
        CHECK(k.v[ffctl::FLUX_A] == 0.f);
    }
    // A move past kT takes it again, at the pot's position.
    CHECK(shell::pot_row_step(e, o, f, 20200 + kT + 1, kSpan, k.v, &v));
    CHECK(k.v[ffctl::FLUX_A] == doctest::Approx(knob_at(ffctl::FLUX_A, 20200 + kT + 1)));
    CHECK(k.v[ffctl::FLUX_A] > 0.f);
}

TEST_CASE("panel step: excite_other_deck follows the BBD visit") {
    control::Options opt;
    REQUIRE_FALSE(opt.deck[0].excite_other_deck);
    REQUIRE_FALSE(opt.deck[1].excite_other_deck);

    // Entry: the edge and the level arrive together.
    control::Events ev;
    ev.bbd_edge[0] = true;
    ev.bbd_part[0] = true;
    shell::apply_excite_events(ev, opt);
    CHECK(opt.deck[0].excite_other_deck);
    CHECK_FALSE(opt.deck[1].excite_other_deck);

    // Still on BBD: no edge, the flag stays.
    ev = control::Events{};
    ev.bbd_part[0] = true;
    shell::apply_excite_events(ev, opt);
    CHECK(opt.deck[0].excite_other_deck);
    CHECK_FALSE(opt.deck[1].excite_other_deck);

    // Left BBD: the flag goes, deck 1 (never on BBD) stays false throughout.
    ev = control::Events{};
    shell::apply_excite_events(ev, opt);
    CHECK_FALSE(opt.deck[0].excite_other_deck);
    CHECK_FALSE(opt.deck[1].excite_other_deck);

    // Deck 1 on its own visit leaves deck 0 alone.
    ev = control::Events{};
    ev.bbd_edge[1] = true;
    ev.bbd_part[1] = true;
    shell::apply_excite_events(ev, opt);
    CHECK_FALSE(opt.deck[0].excite_other_deck);
    CHECK(opt.deck[1].excite_other_deck);
}

// Panel-state persistence (spec 2026-10-28 section 4.3): a restored BBD deck
// boots on another engine until the swap lands, and bbd_part follows the
// running engine, so the restored bit has to be held across the swap -- one
// tick of apply_excite_events() alone would clear it for good.
TEST_CASE("panel step: restored excite is held until the BBD engine runs") {
    Knobs k;
    k.v[ffctl::ENGINE_A] = static_cast<float>(shell::kEngineSlotBbd);
    control::Options opt;
    opt.deck[0].excite_other_deck = true;            // as restored
    bool hold[2] = {true, false};

    // Still switching: bbd_part false, no edge (on_restore()): forced on.
    control::Events ev;
    for (int tick = 0; tick < 50; ++tick) {
        shell::apply_excite_events(ev, opt);
        shell::apply_excite_hold(ev, k.v, hold, opt);
        CHECK(opt.deck[0].excite_other_deck);
        CHECK(hold[0]);
        CHECK_FALSE(opt.deck[1].excite_other_deck);   // no hold, never forced
    }
    // Landed: the hold lets go, the bit stays by the ordinary rule.
    ev.bbd_part[0] = true;
    shell::apply_excite_events(ev, opt);
    shell::apply_excite_hold(ev, k.v, hold, opt);
    CHECK(opt.deck[0].excite_other_deck);
    CHECK_FALSE(hold[0]);
    // Off BBD later: cleared as always, nothing forces it back.
    ev = control::Events{};
    k.v[ffctl::ENGINE_A] = 1.0f;
    shell::apply_excite_events(ev, opt);
    shell::apply_excite_hold(ev, k.v, hold, opt);
    CHECK_FALSE(opt.deck[0].excite_other_deck);
}

TEST_CASE("panel step: excite hold lets go when ENGINE leaves BBD before it lands") {
    Knobs k;
    k.v[ffctl::ENGINE_A] = 1.0f;                     // moved off BBD mid-switch
    control::Options opt;
    opt.deck[0].excite_other_deck = true;
    bool hold[2] = {true, false};
    control::Events ev;
    shell::apply_excite_events(ev, opt);
    shell::apply_excite_hold(ev, k.v, hold, opt);
    CHECK_FALSE(hold[0]);
    CHECK_FALSE(opt.deck[0].excite_other_deck);
}

TEST_CASE("panel step: rec_started changes no knob and releases no pot") {
    int flux[2];
    shell::flux_channels(shell::kRevaTable, shell::kRevaChain, flux);
    constexpr int kN = shell::mux_total(shell::kRevaChain);
    static shell::PotOwner owners[kN];
    for (int i = 0; i < kN; ++i) { owners[i] = {}; owners[i].owned = true; owners[i].has_ref = true; }
    Knobs k;
    Knobs ref;
    control::Events ev;
    ev.rec_started[0] = ev.rec_started[1] = true;
    shell::apply_law_events(ev, k.v, owners, flux);
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) {
        CAPTURE(i);
        CHECK(k.v[i] == ref.v[i]);
    }
    for (int i = 0; i < kN; ++i) CHECK(owners[i].owned);
}

TEST_CASE("panel step: a board without FLUX rows releases nothing on a bbd_edge") {
    int flux[2] = {7, 7};
    shell::flux_channels(shell::kCouponTable, shell::kCouponPlayChain, flux);
    CHECK(flux[0] == -1);
    CHECK(flux[1] == -1);
    Knobs k;
    control::Events ev;
    ev.bbd_edge[1] = true;
    shell::apply_law_events(ev, k.v, nullptr, flux);   // no owner is touched
    CHECK(k.v[ffctl::FLUX_B] == 0.f);
}

TEST_CASE("panel step: the FLUX channels of kRevaTable are the FLUX_A and FLUX_B rows") {
    int flux[2];
    shell::flux_channels(shell::kRevaTable, shell::kRevaChain, flux);
    REQUIRE(flux[0] >= 0);
    REQUIRE(flux[1] >= 0);
    CHECK(flux[0] != flux[1]);
    const int want[2] = {ffctl::FLUX_A, ffctl::FLUX_B};
    const shell::ChainProfile& p = shell::kRevaChain;
    for (int d = 0; d < 2; ++d) {
        CAPTURE(d);
        // The scan reads index flux[d] at exactly one live (step, sense) pair,
        // and the row it finds there -- as panel_scan_tick() looks it up -- is
        // that deck's FLUX row.
        int hits = 0;
        for (int step = 0; step < shell::scan_steps(p); ++step)
            for (int s = 0; s < p.sense_pins; ++s) {
                if (!shell::sense_live(p, step, s)) continue;
                if (shell::mux_channel(p, step, s) != flux[d]) continue;
                ++hits;
                const shell::ControlEntry* e = shell::find_control(
                    shell::kRevaTable, shell::group_at(p, step, s), shell::channel_at(p, step, s));
                REQUIRE(e != nullptr);
                CHECK(e->param == want[d]);
            }
        CHECK(hits == 1);
    }
    // And there is one FLUX row per deck in the table.
    int rows[2] = {0, 0};
    for (int i = 0; i < shell::kRevaTable.count; ++i) {
        rows[0] += shell::kRevaTable.entries[i].param == ffctl::FLUX_A;
        rows[1] += shell::kRevaTable.entries[i].param == ffctl::FLUX_B;
    }
    CHECK(rows[0] == 1);
    CHECK(rows[1] == 1);
}

// CLOCK and RESET (spec 2026-10-19-clock-and-reset-jacks §6, "Knob takeover"):
// the firmware releases the TEMPO pot when the clock is lost, so the pot must
// move past kPotTouch before the knob vector's TEMPO changes and hands the
// tempo back to the knob.
TEST_CASE("panel step: TEMPO channel found") {
    const int tempo = shell::tempo_channel(shell::kRevaTable, shell::kRevaChain);
    REQUIRE(tempo >= 0);
    const shell::ChainProfile& p = shell::kRevaChain;
    // The scan reads index `tempo` at exactly one live (step, sense) pair, and
    // the row it finds there -- as panel_scan_tick() looks it up -- is TEMPO.
    int hits = 0;
    for (int step = 0; step < shell::scan_steps(p); ++step)
        for (int s = 0; s < p.sense_pins; ++s) {
            if (!shell::sense_live(p, step, s)) continue;
            if (shell::mux_channel(p, step, s) != tempo) continue;
            ++hits;
            const shell::ControlEntry* e = shell::find_control(
                shell::kRevaTable, shell::group_at(p, step, s), shell::channel_at(p, step, s));
            REQUIRE(e != nullptr);
            CHECK(e->param == ffctl::TEMPO);
        }
    CHECK(hits == 1);
    int rows = 0;
    for (int i = 0; i < shell::kRevaTable.count; ++i)
        rows += shell::kRevaTable.entries[i].param == ffctl::TEMPO;
    CHECK(rows == 1);
    // The coupon has no TEMPO row.
    CHECK(shell::tempo_channel(shell::kCouponTable, shell::kCouponPlayChain) == -1);
}

TEST_CASE("panel step: clock loss releases the TEMPO pot") {
    const int tempo = shell::tempo_channel(shell::kRevaTable, shell::kRevaChain);
    REQUIRE(tempo >= 0);
    constexpr int kN = shell::mux_total(shell::kRevaChain);
    static shell::PotOwner  owners[kN];
    static shell::PotFilter filters[kN];
    for (int i = 0; i < kN; ++i) { owners[i] = {}; filters[i] = {}; }
    const shell::ControlEntry& e = reva_row(ffctl::TEMPO);
    shell::PotOwner&  o = owners[tempo];
    shell::PotFilter& f = filters[tempo];
    Knobs k;
    float v = -1.f;

    // TEMPO owned and turning; every other pot owned too.
    shell::pot_row_step(e, o, f, 20000, kSpan, k.v, &v);
    shell::pot_row_step(e, o, f, 20000 + kT + 1, kSpan, k.v, &v);
    REQUIRE(o.owned);
    for (int i = 0; i < kN; ++i)
        if (i != tempo) { owners[i].owned = true; owners[i].has_ref = true; }

    // No loss: nothing is touched.
    control::ClockOut out;
    out.pulse = out.clock_lamp = true;
    out.measured_bpm = 120.f;
    shell::apply_clock_events(out, owners, tempo);
    for (int i = 0; i < kN; ++i) { CAPTURE(i); CHECK(owners[i].owned); }

    // Loss: TEMPO alone is released.
    out = control::ClockOut{};
    out.lost = true;
    out.measured_bpm = 120.f;
    shell::apply_clock_events(out, owners, tempo);
    CHECK_FALSE(o.owned);
    for (int i = 0; i < kN; ++i)
        if (i != tempo) { CAPTURE(i); CHECK(owners[i].owned); }

    // Released, the pot's noise does not reach the knob vector: TEMPO holds
    // until the pot moves past kPotTouch, then jumps to the pot's position.
    const float held = k.v[ffctl::TEMPO];
    for (int raw : {20000 + kT + 1, 20000 + kT + 1 + (kT - 1), 20000 + kT + 1 - (kT - 1)}) {
        CAPTURE(raw);
        shell::pot_row_step(e, o, f, static_cast<uint16_t>(raw), kSpan, k.v, &v);
        CHECK(k.v[ffctl::TEMPO] == held);
    }
    constexpr int kMove = 20000 + kT + 1 + 2 * kT + 2;
    shell::pot_row_step(e, o, f, kMove, kSpan, k.v, &v);
    CHECK(o.owned);
    CHECK(std::fabs(k.v[ffctl::TEMPO] - knob_at(ffctl::TEMPO, kMove)) < 1e-6f);
    CHECK(k.v[ffctl::TEMPO] != held);

    // A board without a TEMPO row (-1) touches no owner on a loss.
    shell::apply_clock_events(out, nullptr, -1);
}
