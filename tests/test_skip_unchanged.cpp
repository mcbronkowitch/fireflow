// The unchanged-value early-outs in the lane-retiming setters (task 9a of the
// shared control law, 2026-10-09). Every host pushes every knob on every
// control tick; before the early-outs each push rebuilt all five lanes' slew
// pair per deck -- 110 rebuilds per law tick, 85 % of its cost on the coupon.
//
// Three things are gated here:
//   1. per setter: an identical repeat rebuilds nothing, a change still does,
//      and the first push after init() takes the full path;
//   2. the one input that moves without a setter (_ev_rate) is still
//      refreshed on an unchanged push, exactly as the full path did;
//   3. a law-driven instrument renders bit-identically with the early-outs on
//      and off. This is an equivalence between two paths of the SAME build,
//      not a checksum against a stored render.
#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include "instrument.h"
#include "control/control_law.h"

using namespace spky;

namespace {

constexpr float kSr = 48000.f;

uint32_t slews(const Instrument& in, int p) {
    uint32_t n = 0;
    for (int l = 0; l < LANE_COUNT; ++l) n += in.lane_slew_updates_for_test(p, l);
    return n;
}
uint32_t slews(const Instrument& in) { return slews(in, PART_A) + slews(in, PART_B); }

// push(a) establishes a; a repeat must rebuild nothing; push(b) must rebuild
// at least every lane of every deck the setter reaches; a repeat of b again
// nothing. No process() runs in between, so _ev_rate cannot have drifted and
// the refresh in the skip path has nothing to do.
template <class Push>
void check_setter(Instrument& in, Push push, float a, float b, int decks) {
    push(a);
    const uint32_t s0 = slews(in);
    push(a);
    CHECK(slews(in) == s0);
    push(b);
    CHECK(slews(in) >= s0 + static_cast<uint32_t>(LANE_COUNT * decks));
    const uint32_t s1 = slews(in);
    push(b);
    CHECK(slews(in) == s1);
}

} // namespace

TEST_CASE("skip-unchanged: each retiming setter rebuilds the slews on a change only") {
    Instrument in;
    in.init(kSr);
    SUBCASE("set_rate") {
        check_setter(in, [&](float v) { in.set_rate(PART_A, v); }, 0.3f, 0.6f, 1);
    }
    SUBCASE("set_smooth") {
        check_setter(in, [&](float v) { in.set_smooth(PART_B, v); }, 0.2f, 0.7f, 1);
    }
    SUBCASE("set_tide") {
        check_setter(in, [&](float v) { in.set_tide(v); }, 0.2f, 0.8f, 2);
    }
    SUBCASE("set_sync") {
        check_setter(in, [&](float v) { in.set_sync(v > 0.5f); }, 0.f, 1.f, 2);
    }
    SUBCASE("set_tempo_bpm") {
        check_setter(in, [&](float v) { in.set_tempo_bpm(v); }, 90.f, 133.f, 2);
    }
    SUBCASE("set_pace") {
        // Instrument::set_pace early-outs on an unchanged value before it
        // reaches SuperModulator::set_pace, so this subcase never exercises
        // the SuperModulator guard. That one is gated through the
        // set_tempo_bpm subcase above (its _apply_tempo calls it on every push).
        check_setter(in, [&](float v) { in.set_pace(v); }, 0.2f, 0.7f, 2);
    }
    SUBCASE("set_step compares both arguments") {
        in.set_step(PART_A, true, 8);
        const uint32_t s0 = slews(in);
        in.set_step(PART_A, true, 8);
        CHECK(slews(in) == s0);
        in.set_step(PART_A, true, 12);              // STEPS only
        CHECK(slews(in) >= s0 + LANE_COUNT);
        const uint32_t s1 = slews(in);
        in.set_step(PART_A, false, 12);             // the flag only
        CHECK(slews(in) >= s1 + LANE_COUNT);
        const uint32_t s2 = slews(in);
        in.set_step(PART_A, false, 12);
        CHECK(slews(in) == s2);
        in.set_step(PART_A, false, 0);              // clamps to 1: a change
        CHECK(slews(in) > s2);
        const uint32_t s3 = slews(in);
        in.set_step(PART_A, false, -3);             // clamps to 1 as well
        CHECK(slews(in) == s3);
    }
}

TEST_CASE("skip-unchanged: the first push after init takes the full path") {
    // init() derives the lane rates (_update_rate) but neither TIDE's factor
    // nor the lanes' slot counts, so a push of the stored DEFAULT must still
    // rebuild once -- and again after a re-init (VCV re-inits mid-session).
    Instrument in;
    in.init(kSr);
    uint32_t s = slews(in);
    in.set_tide(0.5f);                    // SuperModulator's stored default
    CHECK(slews(in) > s);
    s = slews(in);
    in.set_tide(0.5f);
    CHECK(slews(in) == s);

    in.init(kSr);
    s = slews(in);
    in.set_sync(false);                   // the stored default
    CHECK(slews(in) > s);

    in.init(kSr);
    s = slews(in);
    in.set_step(PART_B, false, 8);        // the stored defaults
    CHECK(slews(in) > s);
    s = slews(in);
    in.set_step(PART_B, false, 8);
    CHECK(slews(in) == s);

    in.set_tide(0.3f);
    in.init(kSr);
    s = slews(in);
    in.set_tide(0.3f);                    // unchanged value, but a re-init between
    CHECK(slews(in) > s);
}

TEST_CASE("skip-unchanged: an unchanged push still follows a drifted _ev_rate") {
    // _update_slew reads _ev_rate, which GROW walks at every lane wrap with no
    // recompute of its own. The full path picked the new value up on every
    // push; the skip path must too -- once per drifted lane, and only once.
    Instrument in;
    in.init(kSr);
    for (int p = 0; p < PART_COUNT; ++p) {
        in.set_variation(p, 1.f);         // GROW, full walk
        in.set_rate(p, 1.f);              // fast lanes: many wraps
        in.set_smooth(p, 0.5f);
    }
    // Center::update rebuilds every lane through set_rate_scale at the top of
    // each block, so what is left for the push after the block is the drift
    // inside it (texture ticks and PITCH wraps after that rebuild). It is
    // there on some blocks and not on others, hence the 2 s of blocks.
    float il[96] = {}, ir[96] = {}, ol[96], orr[96];
    uint32_t refreshed = 0, worst = 0, again = 0;
    for (int b = 0; b < 1000; ++b) {
        in.process(il, ir, ol, orr, 96);
        const uint32_t s0 = slews(in, PART_A);
        in.set_rate(PART_A, 1.f);         // unchanged
        const uint32_t d = slews(in, PART_A) - s0;
        refreshed += d;
        if (d > worst) worst = d;
        const uint32_t s1 = slews(in, PART_A);
        in.set_smooth(PART_A, 0.5f);      // unchanged, nothing drifted since
        again += slews(in, PART_A) - s1;
    }
    CHECK(refreshed > 0);                 // drift happened and was picked up
    CHECK(worst <= static_cast<uint32_t>(LANE_COUNT));   // at most once per lane
    CHECK(again == 0);
}

TEST_CASE("skip-unchanged: the law renders bit-identically with the early-outs on and off") {
    // Two instruments, one forced back onto the full path on every push. Ten
    // seconds of the init patch with GROW turned up (so _ev_rate walks), and
    // each early-out's input both held and moved: RATE swept, TEMPO, TIDE,
    // SYNC (COUPLE across its split), STEP on/off and STEPS, SMOOTH, PACE,
    // and one SETTLE (DRIFT back into its zone).
    //
    // Both tick divisions the hosts use. At 96 (the firmware) Center::update
    // rebuilds every lane through set_rate_scale at the top of each block,
    // before any lane reads its slew, so a missing _ev_rate refresh in the
    // skip path cannot show; at 16 (VCV) the law's push lands mid-block and
    // the PITCH lane's per-sample slew reads it -- that is where it shows.
    int div = 0;
    SUBCASE("law every 96 samples (firmware)") { div = 96; }
    SUBCASE("law every 16 samples (VCV)")      { div = 16; }
    Instrument ref, dut;
    ref.init(kSr);
    dut.init(kSr);
    ref.set_skip_unchanged_for_test(false);
    control::ControlLaw lref, ldut;
    lref.on_restore();
    ldut.on_restore();
    float k[ffctl::NUM_PARAMS];
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) k[i] = ffctl::kInitParamDefaults[i];
    k[ffctl::MELODY_A] = 1.f;
    k[ffctl::MELODY_B] = 0.95f;
    // RANGE is the PITCH lane's ambitus, and the init patch parks it at 0:
    // a flat PITCH lane whose slew nobody could see.
    k[ffctl::RANGE_A] = 1.f;
    k[ffctl::RANGE_B] = 1.f;
    const float rate0 = k[ffctl::RATE_A];
    const control::Options opt{};

    float il[96] = {}, ir[96] = {};
    float aL[96], aR[96], bL[96], bR[96];
    long diffs = 0, lane_diffs = 0;
    const int kBlocks = static_cast<int>(10.f * kSr) / div;
    for (int b = 0; b < kBlocks; ++b) {
        const float t = static_cast<float>(b * div) / kSr;
        // From 8.5 s deck A runs a fast STEP clock under GROW and SMOOTH: its
        // PITCH lane wraps mid-block and walks _ev_rate there (measured: ~1200
        // PITCH refreshes in the last 1.4 s at div 16, 0 before 8.5 s).
        k[ffctl::RATE_A] = (t >= 1.f && t < 2.f)
            ? rate0 + 0.08f * std::sin(6.2831853f * t)
            : (t >= 8.5f ? 0.9f : rate0);
        k[ffctl::DRIFT]  = (t >= 1.f && t < 8.f) ? 0.5f : 0.f;
        if (t >= 2.f) k[ffctl::TEMPO] = 0.5f;
        if (t >= 3.f && t < 4.f) k[ffctl::TIDE] = 0.8f * (t - 3.f);
        k[ffctl::COUPLE] = (t >= 4.f && t < 6.f) ? 0.2f : 1.f;
        if (t >= 5.f) k[ffctl::STEPS_A] = 8.f;
        if (t >= 7.f) k[ffctl::STEPS_B] = 12.f;
        if (t >= 5.5f && t < 6.f) k[ffctl::SMOOTH_A] = 1.2f * (t - 5.5f);
        if (t >= 6.5f) k[ffctl::PACE] = 0.4f;
        lref.tick(k, opt, ref);
        ldut.tick(k, opt, dut);
        ref.process(il, ir, aL, aR, static_cast<size_t>(div));
        dut.process(il, ir, bL, bR, static_cast<size_t>(div));
        for (int i = 0; i < div; ++i) {
            diffs += std::memcmp(&aL[i], &bL[i], sizeof(float)) != 0;
            diffs += std::memcmp(&aR[i], &bR[i], sizeof(float)) != 0;
        }
        // The lanes themselves too: a slew difference on the PITCH lane can
        // vanish in the quantizer before it reaches the audio.
        for (int p = 0; p < PART_COUNT; ++p)
            for (int l = 0; l < LANE_COUNT; ++l) {
                const float va = ref.lane_value_for_test(p, l);
                const float vb = dut.lane_value_for_test(p, l);
                lane_diffs += std::memcmp(&va, &vb, sizeof(float)) != 0;
            }
    }
    CHECK(diffs == 0);
    CHECK(lane_diffs == 0);
    // Not vacuous: the early-outs really skipped most of the rebuilds.
    CHECK(2 * slews(dut) < slews(ref));
}
