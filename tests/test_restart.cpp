// RESET restarts the song on STEP decks (Instrument::restart), and the shared
// clock law drives the transport one pace step per pulse. Spec:
// docs/superpowers/specs/2026-10-19-clock-and-reset-jacks-design.md §2.2, §3,
// §5, §6 ("Engine") and §8.2/§8.3.
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "instrument.h"
#include "mod/divisions.h"
#include "control/clock_law.hpp"

using namespace spky;

namespace {

constexpr float kSr    = 48000.f;
constexpr int   kBlock = 96;

void run(Instrument& inst, int samples) {
    float l[kBlock], r[kBlock];
    while (samples > 0) {
        const int n = std::min(samples, kBlock);
        inst.process(nullptr, nullptr, l, r, static_cast<size_t>(n));
        samples -= n;
    }
}

// Both decks on SYNTH (the boot engine; set anyway so the rig says it), 120 BPM.
void setup(Instrument& inst, bool a_step, bool b_step, bool sync = true,
           float couple = 0.f) {
    inst.init(kSr);
    inst.set_engine(PART_A, ENGINE_SYNTH);
    inst.set_engine(PART_B, ENGINE_SYNTH);
    inst.set_tempo_bpm(120.f);
    inst.set_sync(sync);
    inst.set_couple(couple);
    inst.set_step(PART_A, a_step, 8);
    inst.set_step(PART_B, b_step, 8);
}

// Run until deck B has advanced past phrase 2 (its third phrase wrap), with a
// ceiling so a stalled song fails instead of hanging.
void run_until_b_past_phrase_2(Instrument& inst) {
    for (int blocks = 0; blocks < 120 * 500 &&
                         inst.song_position_for_test(PART_B) <= 2; ++blocks)
        run(inst, kBlock);
    REQUIRE(inst.song_position_for_test(PART_B) > 2);
}

// Distance on the phase circle: 0.999 and 0.001 are 0.002 apart.
float phase_dist(float a, float b) {
    const float d = std::fabs(a - b);
    return std::min(d, 1.f - d);
}

} // namespace

TEST_CASE("restart: a STEP deck restarts its song") {
    Instrument inst;
    setup(inst, /*a_step=*/false, /*b_step=*/true);
    run_until_b_past_phrase_2(inst);
    const int pat = inst.active_pattern_for_test(PART_B);
    std::printf("[restart] deck B active pattern before restart: %d\n", pat);
    REQUIRE(pat != 0);    // a pattern of 0 could not show a restart

    CHECK(inst.restart(0));
    CHECK(inst.song_position_for_test(PART_B) == 0);
    CHECK(inst.active_pattern_for_test(PART_B) == 0);
    run(inst, kBlock);
    const float ph = inst.lane_phase_for_test(PART_B, LANE_PITCH);
    std::printf("[restart] deck B pitch phase one block after restart: %.6f\n", ph);
    CHECK(ph < 0.01f);
}

TEST_CASE("restart: a FLOW deck keeps its song") {
    Instrument inst;
    setup(inst, /*a_step=*/false, /*b_step=*/true);
    run_until_b_past_phrase_2(inst);
    const uint32_t a_pos = inst.song_position_for_test(PART_A);
    std::printf("[restart] deck A (FLOW) song position before restart: %u\n", a_pos);
    REQUIRE(a_pos > 0);   // a position of 0 could not show a restart

    CHECK(inst.restart(0));
    CHECK(inst.song_position_for_test(PART_A) == a_pos);
}

TEST_CASE("restart: no STEP deck, no change") {
    Instrument inst;
    setup(inst, /*a_step=*/false, /*b_step=*/false);
    run(inst, 5 * 48000);

    uint32_t pos[PART_COUNT];
    float    ph[PART_COUNT][LANE_COUNT];
    for (int p = 0; p < PART_COUNT; ++p) {
        pos[p] = inst.song_position_for_test(p);
        for (int l = 0; l < LANE_COUNT; ++l) ph[p][l] = inst.lane_phase_for_test(p, l);
    }
    const float beat = inst.beat_phase();

    CHECK_FALSE(inst.restart(0));
    for (int p = 0; p < PART_COUNT; ++p) {
        CHECK(inst.song_position_for_test(p) == pos[p]);
        for (int l = 0; l < LANE_COUNT; ++l)
            CHECK(inst.lane_phase_for_test(p, l) == ph[p][l]);
    }
    CHECK(inst.beat_phase() == beat);
}

// §2.2 / §3: Center::update couples both decks to the transport and to each
// other, so a restart that left the FLOW deck's lanes running would drag it
// for seconds to minutes. The reference is today's RST (reset_transport, both
// decks' lanes to 0) at the same sample; deck A must not tell the two apart.
TEST_CASE("restart: a FLOW deck is not dragged") {
    struct Zone { const char* name; bool sync; float couple; };
    // control_law.h's COUPLE axis: at or above kCoupleZoneSplit (0.5) the
    // GRID world with sync on and couple = (knob - 0.5) / 0.5; below it the
    // FREE world with sync off and couple = knob / 0.5. Knob 1.0 is the
    // factory GRID setting; knob 0.45 is the FREE point §3 measured.
    const Zone zones[] = {
        { "GRID knob 1.00", true,  1.0f },
        { "FREE knob 0.45", false, 0.45f / 0.5f },
    };
    for (const Zone& z : zones) {
        CAPTURE(z.name);
        Instrument restarted, reference;
        setup(restarted, /*a_step=*/false, /*b_step=*/true, z.sync, z.couple);
        setup(reference, /*a_step=*/false, /*b_step=*/true, z.sync, z.couple);
        const int lead = static_cast<int>(10.3f * kSr);
        run(restarted, lead);
        run(reference, lead);

        CHECK(restarted.restart(0));
        reference.reset_transport();

        float worst = 0.f;
        for (int blk = 0; blk < 10 * 500; ++blk) {
            run(restarted, kBlock);
            run(reference, kBlock);
            worst = std::max(worst, phase_dist(
                restarted.lane_phase_for_test(PART_A, LANE_PITCH),
                reference.lane_phase_for_test(PART_A, LANE_PITCH)));
        }
        std::printf("[restart] %s: deck A pitch phase, worst |restart - RST| "
                    "over 10 s: %.3g\n", z.name, worst);
        CHECK(worst < 1e-3f);
    }
}

// §5: a restart is a wrap. Pending work is applied there (one rebuild), and
// with nothing pending the same song plays from the top (no rebuild).
TEST_CASE("restart: pending work is applied, nothing pending is not") {
    Instrument inst, untouched;
    setup(inst, /*a_step=*/false, /*b_step=*/true);
    setup(untouched, /*a_step=*/false, /*b_step=*/true);
    run(inst, 3 * 48000);
    run(untouched, 3 * 48000);
    auto builds = [](const Instrument& i) {
        return i.part(PART_B).mod().pitch_phrase_builds();
    };

    // Pending NEW: the restart applies it.
    inst.new_phrase(PART_B);
    untouched.new_phrase(PART_B);
    const uint32_t before = builds(inst);
    REQUIRE(builds(untouched) == before);
    CHECK(inst.restart(0));
    run(inst, kBlock);
    run(untouched, kBlock);
    CHECK(builds(inst) == before + 1);
    // Without the restart the same block builds nothing: the rise is the
    // restart's, not a phrase wrap that happened to fall in this block.
    CHECK(builds(untouched) == before);

    // Nothing pending: no rebuild.
    run(inst, 48000);
    const uint32_t settled = builds(inst);
    CHECK(inst.restart(0));
    run(inst, kBlock);
    CHECK(builds(inst) == settled);
}

// §6: a reset up to 10 ms after a clock edge restarts as if it had come just
// before that edge, advanced by the samples elapsed since.
TEST_CASE("restart: back-dating") {
    Instrument inst;
    setup(inst, /*a_step=*/false, /*b_step=*/true);
    run(inst, 3 * 48000 + 37);

    constexpr uint32_t kAge = 240;
    CHECK(inst.restart(kAge));
    // 120 BPM: 2 beats per second. One transport tick is one control block.
    const double want_beats = 2.0 * kAge / kSr;
    const double tick_beats = 2.0 * kBlock / kSr;
    std::printf("[restart] back-dated transport beat phase: %.6f (want %.6f)\n",
                inst.beat_phase(), want_beats);
    CHECK(std::fabs(inst.beat_phase() - want_beats) <= tick_beats);

    // The PITCH lane's increment per sample: rate x the STEP clock factor
    // 8/steps (lane.h clock_scale) / sr. VARY is 0 here, so EVOLVE's rate walk
    // (_ev_rate) is 0 and adds nothing.
    const double inc = double(inst.lane_rate_hz_for_test(PART_B, LANE_PITCH))
                     * (8.0 / inst.deck_steps(PART_B)) / kSr;
    const float ph = inst.lane_phase_for_test(PART_B, LANE_PITCH);
    std::printf("[restart] back-dated pitch phase: %.6f (want %.6f)\n",
                ph, inc * kAge);
    CHECK(std::fabs(ph - inc * kAge) <= 1e-4);
}

// Back-dating across a step boundary: the deck clock (_deck_step) must land on
// the step the PITCH lane landed on, or every follower lane sits one slot
// behind the melody until the next reset.
TEST_CASE("restart: back-dating across a step keeps the followers on the deck step") {
    Instrument inst;
    setup(inst, /*a_step=*/false, /*b_step=*/true);
    inst.set_rate(PART_B, 1.f);   // the fastest division: a step is shorter than the window
    run(inst, 48000);

    constexpr uint32_t kAge = 480;   // the whole 10 ms window
    CHECK(inst.restart(kAge));
    run(inst, 1);
    const int step = inst.part(PART_B).mod().pitch_cur_step();
    std::printf("[restart] back-dated by %u samples: pitch step %d, deck step %d\n",
                kAge, step, int(inst.part(PART_B).mod().deck_step_for_test()));
    REQUIRE(step > 0);   // the age crossed a step boundary, or this proves nothing
    CHECK(inst.part(PART_B).mod().deck_step_for_test() == step);
}

// §3 / §8.2: under a steady external clock the transport counts pace x one beat
// per pulse. The law rig is what both hosts will run; the VCV rig replays
// today's Fireflow.cpp formula (time since the last edge, not the period) and
// must fail the same assertion, which proves the assertion can see the bug.
namespace {

// control_law.h:954-957, replayed verbatim.
float law_tempo(float tempo_knob, float measured_bpm) {
    float bpm = 40.f + tempo_knob * 200.f;
    if (measured_bpm >= 20.f && measured_bpm <= 400.f) bpm = measured_bpm;
    return bpm;
}

constexpr float    kKnob       = 0.5f;          // TEMPO knob: 140 BPM, not the clock's
constexpr uint32_t kPeriod     = 24000;         // 120 BPM at 48 kHz
constexpr uint32_t kFirstEdge  = 100 * kBlock;  // some knob-tempo running first
constexpr int      kEdges      = 18;            // edge 2 .. edge 18 = 16 periods

bool edge_at(uint64_t sample) {
    return sample >= kFirstEdge && (sample - kFirstEdge) % kPeriod == 0 &&
           (sample - kFirstEdge) / kPeriod < kEdges;
}

// Transport beats from edge 2 to edge 18, read right after each edge's pulse.
double law_rig(float pace_norm) {
    Instrument inst;
    inst.init(kSr);
    inst.set_pace(pace_norm);
    inst.set_tempo_bpm(law_tempo(kKnob, 0.f));
    control::ClockLaw law;
    law.init(kSr);
    int edges = 0;
    double at2 = 0.0, at18 = 0.0;
    for (uint64_t t = 0; edges < kEdges; t += kBlock) {
        const bool edge = edge_at(t);
        const control::ClockOut o =
            law.step(kBlock, edge, false, kKnob, inst.beat_phase());
        if (o.pulse) inst.clock_pulse();
        inst.set_tempo_bpm(law_tempo(kKnob, o.measured_bpm));
        inst.set_pace(pace_norm);
        if (edge) {
            ++edges;
            if (edges == 2)  at2  = inst.transport_beats_for_test();
            if (edges == 18) at18 = inst.transport_beats_for_test();
        }
        run(inst, kBlock);
    }
    return at18 - at2;
}

// Fireflow.cpp before this round: clkSamples counts up per sample and is zeroed
// on every edge (:737-745), every edge calls clock_pulse(), and each control
// push (every 16 samples, :396) hands the law 60 * sr / clkSamples (:719-720).
double vcv_rig(float pace_norm) {
    constexpr uint32_t kPush = 16;
    Instrument inst;
    inst.init(kSr);
    inst.set_pace(pace_norm);
    inst.set_tempo_bpm(law_tempo(kKnob, 0.f));
    float clkSamples = 0.f;
    int edges = 0;
    double at2 = 0.0, at18 = 0.0;
    for (uint64_t t = 0; edges < kEdges; t += kPush) {
        if (edge_at(t)) {
            clkSamples = 0.f;
            inst.clock_pulse();
            ++edges;
            if (edges == 2)  at2  = inst.transport_beats_for_test();
            if (edges == 18) at18 = inst.transport_beats_for_test();
        }
        clkSamples += float(kPush);
        const float measured = clkSamples > 1.f ? 60.f * kSr / clkSamples : 0.f;
        inst.set_tempo_bpm(law_tempo(kKnob, measured));
        inst.set_pace(pace_norm);
        run(inst, int(kPush));
    }
    return at18 - at2;
}

} // namespace

TEST_CASE("clock law drives the transport one pace step per pulse") {
    const float kPaceX1 = 0.5f;    // divisions.h: 0.5 -> x1
    const float kPaceX16 = 0.1f;   // 32^(2 * 0.1 - 1) = 32^-0.8 = 1/16
    REQUIRE(std::fabs(pace_mult(kPaceX1) - 1.f) < 1e-6f);
    REQUIRE(std::fabs(pace_mult(kPaceX16) - 1.f / 16.f) < 1e-6f);

    const double law_x1 = law_rig(kPaceX1);
    const double law_x16 = law_rig(kPaceX16);
    const double vcv_x1 = vcv_rig(kPaceX1);
    const double vcv_x16 = vcv_rig(kPaceX16);
    std::printf("[clock] 16 pulses at 120 BPM: law x1 %.4f, law x1/16 %.4f, "
                "VCV formula x1 %.4f, VCV formula x1/16 %.4f beats\n",
                law_x1, law_x16, vcv_x1, vcv_x16);

    CHECK(std::fabs(law_x1 - 16.0) <= 0.5);
    CHECK(std::fabs(law_x16 - 1.0) <= 0.1);
    // Today's formula: the same assertion fails.
    CHECK_FALSE(std::fabs(vcv_x1 - 16.0) <= 0.5);
}

// Start from stop (spec §5 amendment): the clock starts with RESET on its first
// edge, so the bar restarts there, and the beat up to edge 2 runs at the stale
// tempo (the knob's here). Edge 2 must land exactly one pace step after the
// restart, whatever that stale tempo was: a rounded pulse snapped a stale tempo
// under ~0.5x or from ~1.5x of the source to 0 or 2, and the bar stayed a
// quarter off until the next RESET (170 BPM: 0,0,1,2; 50 BPM: 0,2,3,4).
namespace {

// Both hosts' pulse line, verbatim.
void host_pulse(Instrument& inst, const control::ClockOut& o) {
    if (o.pulse) o.counted ? inst.clock_pulse_counted() : inst.clock_pulse();
    if (o.restart) inst.restart(o.restart_age);
}

// Transport beats right after each of the first five edges, firmware block
// order, the knob at VCV's init tempo, RESET together with edge 1.
void start_rig(double src_bpm, float pace_norm, double beats[5]) {
    constexpr float kInitKnob = (79.5f - 40.f) / 200.f;   // law_tempo(): 79.5 BPM
    Instrument inst;
    setup(inst, /*a_step=*/false, /*b_step=*/true);
    inst.set_pace(pace_norm);
    inst.set_tempo_bpm(law_tempo(kInitKnob, 0.f));
    control::ClockLaw law;
    law.init(kSr);
    run(inst, 100 * kBlock);           // the knob's tempo, running before the clock
    const double period = 60.0 * double(kSr) / src_bpm;
    double next = 37.0;                // edge 1 mid-block
    int edges = 0;
    for (uint64_t t = kBlock; edges < 5; t += kBlock) {
        const bool edge = double(t) >= next;
        if (edge) next += period;
        const control::ClockOut o = law.step(kBlock, edge, edge && edges == 0,
                                             kInitKnob, inst.beat_phase());
        host_pulse(inst, o);
        inst.set_tempo_bpm(law_tempo(kInitKnob, o.measured_bpm));
        inst.set_pace(pace_norm);
        if (edge) beats[edges++] = inst.transport_beats_for_test();
        run(inst, kBlock);
    }
}

} // namespace

TEST_CASE("start from stop: edge 2 lands one pace step after the RESET") {
    struct Case { double bpm; float pace_norm; double step; };
    const Case cases[] = {
        { 120.0, 0.5f, 1.0 },          // the stale 79.5 is 0.66x: rounding was right
        { 170.0, 0.5f, 1.0 },          // 0.47x: rounding gave 0,0,1,2
        {  50.0, 0.5f, 1.0 },          // 1.59x: rounding gave 0,2,3,4
        {  50.0, 0.1f, 1.0 / 16.0 },   // the same at PACE x1/16
    };
    for (const Case& c : cases) {
        double beats[5];
        start_rig(c.bpm, c.pace_norm, beats);
        std::printf("[start] %5.1f BPM pace %.4f: %.3f %.3f %.3f %.3f %.3f\n",
                    c.bpm, double(pace_mult(c.pace_norm)),
                    beats[0], beats[1], beats[2], beats[3], beats[4]);
        CAPTURE(c.bpm);
        CAPTURE(c.pace_norm);
        for (int k = 0; k < 5; ++k) {
            CAPTURE(k);
            CHECK(std::fabs(beats[k] - k * c.step) <= 0.25 * c.step);
        }
    }
}
