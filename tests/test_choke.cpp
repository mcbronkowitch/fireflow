#include <doctest/doctest.h>
#include "parts/part.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include "instrument.h"
using namespace spky;

// Drives a bare Part fast enough that the pitch lane fires several times a
// second, then checks that inhibit removes every new gate/trigger while the
// lanes keep running.
static void run(Part& p, int samples, bool& saw_gate) {
    float l, r;
    for (int i = 0; i < samples; ++i) {
        p.process(l, r);
        if (p.gate()) saw_gate = true;
    }
}

TEST_CASE("part: inhibit blocks new gates in FLOW") {
    Part part;
    part.init(48000.f, 0xabcd1234u);
    part.mod().set_tempo_bpm(120.f);
    part.mod().set_rate(0.8f);
    part.mod().set_density(1.f);

    bool gated = false;
    run(part, 48000, gated);
    CHECK(gated);                       // sanity: it fires without inhibit

    part.set_inhibit(true);
    float l, r;                         // ride out the current ~5 ms pulse
    for (int i = 0; i < 480 && part.gate(); ++i) part.process(l, r);

    gated = false;
    run(part, 96000, gated);            // 2 s inhibited: silence on the gate
    CHECK_FALSE(gated);

    part.set_inhibit(false);            // and it comes back
    gated = false;
    run(part, 96000, gated);
    CHECK(gated);
}

TEST_CASE("part: inhibit also mutes the STEP sustain of a suppressed note") {
    Part part;
    part.init(48000.f, 0xabcd1234u);
    part.mod().set_tempo_bpm(120.f);
    part.mod().set_rate(0.8f);
    part.mod().set_density(1.f);
    part.set_step(true, 8);             // STEP mode: gate() includes note sustain

    part.set_inhibit(true);
    float l, r;
    for (int i = 0; i < 480 && part.gate(); ++i) part.process(l, r);

    bool gated = false;
    run(part, 96000, gated);
    CHECK_FALSE(gated);                 // sustain of suppressed notes stays low
}

TEST_CASE("part: max_voice_env is 0 idle and >0 after a trigger") {
    Part part;
    part.init(48000.f, 0xabcd1234u);
    part.mod().set_tempo_bpm(120.f);
    CHECK(part.max_voice_env() == 0.f);

    part.trigger_manual();
    float l, r;
    for (int i = 0; i < 480; ++i) part.process(l, r);
    CHECK(part.max_voice_env() > 0.f);
}

// --- instrument-level CHOKE ---------------------------------------------------

// Both decks firing fast and free-running, so their pulses overlap often.
static void arm_both(Instrument& inst) {
    inst.init(48000.f);
    inst.set_tempo_bpm(120.f);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_rate(p, p == PART_A ? 0.8f : 0.9f);
        inst.set_density(p, 1.f);
        inst.set_range(p, 1.f);
    }
}

// One sample step; returns the yielding part's gate onset (rising edge).
static bool onset(Instrument& inst, int part, bool& prev) {
    const bool g = inst.gate(part);
    const bool rise = g && !prev;
    prev = g;
    return rise;
}

static bool window_open(const Instrument& inst, int pri) {
    if (inst.gate(pri)) return true;
    for (int v = 0; v < 4; ++v)
        if (inst.voice_env(pri, v) > 1e-4f) return true;
    return false;
}

TEST_CASE("choke 0 is bit-identical to an untouched instrument") {
    Instrument a, b;
    a.init(48000.f);
    b.init(48000.f);
    b.set_choke(0.f);
    std::vector<float> al(1), ar(1), bl(1), br(1);
    for (int i = 0; i < 48000; ++i) {
        a.process(nullptr, nullptr, al.data(), ar.data(), 1);
        b.process(nullptr, nullptr, bl.data(), br.data(), 1);
        REQUIRE(al[0] == bl[0]);
        REQUIRE(ar[0] == br[0]);
        // The sidechain duck's gain is EXACTLY 1.0f at noon, which is what
        // makes the bypass bit-identical without a branch around the mix
        // (a multiply by 1.0f is exact). Asserted directly rather than
        // inferred, the same way the Bloom duck's idle 1.0 is.
        REQUIRE(a.choke_duck_gain() == 1.f);
        REQUIRE(b.choke_duck_gain() == 1.f);
    }
}

TEST_CASE("choke -1: B never fires while A is audible (gate or decay)") {
    Instrument inst;
    arm_both(inst);
    inst.set_choke(-1.f);
    std::vector<float> l(1), r(1);
    bool prevB = false, sawA = false;
    for (int i = 0; i < 480000; ++i) {          // 10 s
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
        sawA = sawA || inst.gate(PART_A);
        if (onset(inst, PART_B, prevB))
            CHECK_FALSE(window_open(inst, PART_A));
    }
    CHECK(sawA);                                 // sanity: A actually plays
}

TEST_CASE("choke +1 is the mirror: A yields to B") {
    Instrument inst;
    arm_both(inst);
    inst.set_choke(1.f);
    std::vector<float> l(1), r(1);
    bool prevA = false, sawB = false;
    for (int i = 0; i < 480000; ++i) {
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
        sawB = sawB || inst.gate(PART_B);
        if (onset(inst, PART_A, prevA))
            CHECK_FALSE(window_open(inst, PART_B));
    }
    CHECK(sawB);
}

static float max_env(const Instrument& inst, int p) {
    float m = 0.f;
    for (int v = 0; v < 4; ++v) m = std::max(m, inst.voice_env(p, v));
    return m;
}

TEST_CASE("choke -0.6 (held zone): B starts only while A's note is not held; the tail is free") {
    // STEP mode with rests in A's groove (arm_both's FLOW drone counts as a
    // note that is always held and would block the stage-1 window forever).
    Instrument inst;
    inst.init(48000.f);
    inst.set_tempo_bpm(120.f);
    inst.set_sync(true);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_step(p, true, 8);
        inst.set_range(p, 1.f);
        inst.set_voice_decay(p, 0.7f);           // long tails: ring past the gate
    }
    inst.set_density(PART_A, 0.5f);              // A has rests (DENSE 1 = legato)
    inst.set_density(PART_B, 1.f);
    inst.set_rate(PART_A, 0.0625f);              // A slow, B fast: rates differ
    inst.set_rate(PART_B, 0.5f);
    inst.set_choke(-0.6f);                       // held zone: 0.5 < |c| <= 0.75
    std::vector<float> l(1), r(1);
    bool prevB = false;
    int onsets = 0, in_loud_tail = 0;
    for (int i = 0; i < 960000; ++i) {           // 20 s
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
        if (onset(inst, PART_B, prevB)) {
            ++onsets;
            CHECK_FALSE(inst.gate(PART_A));      // never while A's note is held
            if (max_env(inst, PART_A) > 0.1f)
                ++in_loud_tail;                  // ...but A's ringing tail is free
        }
    }
    CHECK(onsets > 0);                            // held zone still lets B play
    CHECK(in_loud_tail > 0);                      // even while A's tail is loud
}

TEST_CASE("choke -0.6 with a FLOW priority: the drone counts as always held") {
    Instrument inst;
    arm_both(inst);                               // FLOW both: A holds a drone
    inst.set_choke(-0.6f);                        // the held window already blocks
    std::vector<float> l(1), r(1);
    bool prevB = false;
    int b_onsets = 0;
    for (int i = 0; i < 480000; ++i) {            // 10 s
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
        if (onset(inst, PART_B, prevB)) ++b_onsets;
    }
    CHECK(b_onsets == 0);                         // no B retrigs under the drone
    CHECK(max_env(inst, PART_B) <= 1e-4f);        // and B's own drone ducked out
}

TEST_CASE("choke never touches the clocks: lanes and pitch CV stay bit-identical") {
    // Skip-not-delay contract: choking mutes notes but must not shift any
    // lane phase, groove position or pitch CV — no "running out of sync".
    auto arm = [](Instrument& inst) {
        inst.init(48000.f);
        inst.set_tempo_bpm(120.f);
        inst.set_sync(true);
        inst.set_couple(1.f);
        for (int p = 0; p < PART_COUNT; ++p) {
            inst.set_step(p, true, 8);
            inst.set_density(p, 0.8f);
            inst.set_range(p, 1.f);
        }
        inst.set_rate(PART_A, 0.25f);
        inst.set_rate(PART_B, 0.5f);             // different rates on purpose
    };
    Instrument ref, choked;
    arm(ref);
    arm(choked);
    choked.set_choke(-1.f);
    std::vector<float> l(1), r(1);
    for (int i = 0; i < 480000; ++i) {           // 10 s
        ref.process(nullptr, nullptr, l.data(), r.data(), 1);
        choked.process(nullptr, nullptr, l.data(), r.data(), 1);
        for (int p = 0; p < PART_COUNT; ++p) {
            REQUIRE(ref.lane_output(p, LANE_PITCH) == choked.lane_output(p, LANE_PITCH));
            REQUIRE(ref.pitch_cv(p) == choked.pitch_cv(p));
            REQUIRE(ref.lane_fired(p, LANE_PITCH) == choked.lane_fired(p, LANE_PITCH));
        }
    }
}

TEST_CASE("choke -1: the yielding FLOW drone ducks out and comes back") {
    Instrument inst;
    arm_both(inst);                               // FLOW: B holds a drone voice
    std::vector<float> l(1), r(1);
    for (int i = 0; i < 96000; ++i)               // let both drones establish
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
    CHECK(max_env(inst, PART_B) > 0.1f);          // B's drone is up

    inst.set_choke(-1.f);                         // A takes the floor
    for (int i = 0; i < 480000; ++i)              // 10 s to decay out
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
    CHECK(max_env(inst, PART_B) <= 1e-4f);        // drone gone, not just gated

    inst.set_choke(0.f);                          // floor is free again
    for (int i = 0; i < 480000; ++i)
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
    CHECK(max_env(inst, PART_B) > 0.1f);          // drone re-armed and audible
}

// --- CHOKE zones and the sidechain duck --------------------------------------
// Plan docs/superpowers/plans/2026-08-22-choke-sidechain-duck-plan.md, task 1.
// Three zones per side, with a = |choke|: duck only (0 < a <= 0.5), duck plus
// the held window (0.5 < a <= 0.75), duck plus the full-decay window (a > 0.75).

// The rig the task-1 probe measured the OLD boundaries on, reused unchanged so
// the numbers below are comparable to it: STEP on both decks with rests on each
// (DENSE 1 is legato and yields a single onset in 20 s), long tails, different
// rates. What makes it discriminate: A's voices are audible the whole run, so
// the decay window blocks B outright while the held window does not.
static void arm_zone_rig(Instrument& inst, float choke) {
    inst.init(48000.f);
    inst.set_tempo_bpm(120.f);
    inst.set_sync(true);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_step(p, true, 8);
        inst.set_range(p, 1.f);
        inst.set_voice_decay(p, 0.7f);           // tails that outlast the gate
    }
    inst.set_density(PART_A, 0.5f);              // A has rests
    inst.set_density(PART_B, 0.75f);             // so does B, so gate() has edges
    inst.set_rate(PART_A, 0.0625f);
    inst.set_rate(PART_B, 0.5f);
    inst.set_choke(choke);
}

struct ZoneCount { int onsets = 0; int during_a_gate = 0; };

static ZoneCount zone_count(float choke) {
    Instrument inst;
    arm_zone_rig(inst, choke);
    std::vector<float> l(1), r(1);
    ZoneCount z;
    bool prevB = false;
    for (int i = 0; i < 960000; ++i) {           // 20 s, as the probe ran
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
        if (onset(inst, PART_B, prevB)) {
            ++z.onsets;
            if (inst.gate(PART_A)) ++z.during_a_gate;
        }
    }
    return z;
}

TEST_CASE("choke zones: 0.5 and 0.75 are the boundaries between duck, held and decay") {
    const ZoneCount free_run = zone_count(0.f);
    REQUIRE(free_run.onsets > 0);                // sanity: B plays at all
    REQUIRE(free_run.during_a_gate > 0);         // and does start under A's note

    // Duck zone: no event choke at all. The duck touches the MIX only, never a
    // deck's own state, so B's event stream is the noon one exactly.
    const ZoneCount duck_top = zone_count(-0.5f);
    CHECK(duck_top.onsets == free_run.onsets);
    CHECK(duck_top.during_a_gate == free_run.during_a_gate);

    // Held zone, both ends of it: B never starts under A's held note, but
    // still gets in between them.
    for (float c : { -0.6f, -0.75f }) {
        const ZoneCount held = zone_count(c);
        CHECK(held.onsets > 0);
        CHECK(held.onsets < free_run.onsets);
        CHECK(held.during_a_gate == 0);
    }

    // Decay zone: A rings for the whole run here, so B never starts at all.
    CHECK(zone_count(-0.9f).onsets == 0);
}

// Both decks droning in FLOW at the boot MORPH and LVL. Those defaults matter
// now that the duck's detector reads the priority deck AFTER its MORPH/LEVEL
// gain: the window constants are sized against what a deck contributes at
// neutral morph, so a rig that quietly turned LVL down would be measuring a
// different signal than the probe did. The master limiter stays in its
// bit-transparent region here on its own -- the test asserts the reference peak
// against the limiter's own knee rather than assuming it.
static void arm_duck_rig(Instrument& inst, float choke) {
    arm_both(inst);
    inst.set_choke(choke);
}

struct DuckDiff { double energy = 0.0; float ref_peak = 0.f; float gain_min = 1.f; };

// Energy of (noon render - ducked render). Every knob value used here is inside
// the duck zone, so nothing is inhibited and both instruments run bit-identical
// deck state -- the whole difference IS the duck.
static DuckDiff duck_diff(float choke) {
    Instrument ref, ducked;
    arm_duck_rig(ref, 0.f);
    arm_duck_rig(ducked, choke);
    DuckDiff d;
    std::vector<float> rl(1), rr(1), dl(1), dr(1);
    for (int i = 0; i < 480000; ++i) {           // 10 s
        ref.process(nullptr, nullptr, rl.data(), rr.data(), 1);
        ducked.process(nullptr, nullptr, dl.data(), dr.data(), 1);
        const double el = double(rl[0]) - dl[0], er = double(rr[0]) - dr[0];
        d.energy += el * el + er * er;
        d.ref_peak = std::max(d.ref_peak,
                              std::max(std::fabs(rl[0]), std::fabs(rr[0])));
        d.gain_min = std::min(d.gain_min, ducked.choke_duck_gain());
    }
    return d;
}

TEST_CASE("choke duck: the yielding deck's contribution drops, deeper the further in") {
    const DuckDiff quarter = duck_diff(-0.125f);
    const DuckDiff half    = duck_diff(-0.25f);
    const DuckDiff full    = duck_diff(-0.5f);
    // 0.89125 is Limiter's transparent knee: below it the master is an exact
    // identity, so these three renders differ by the duck and nothing else.
    REQUIRE(full.ref_peak < 0.89f);
    CHECK(quarter.energy > 0.0);                 // the duck exists at all
    CHECK(half.energy > quarter.energy);         // and deepens with the knob
    CHECK(full.energy > half.energy);
    // ...and at the bottom of the zone it is a real duck, not a trim. Before
    // the envelope window existed this rig bottomed out at 0.8315 (-1.60 dB),
    // because the gain normalised against a full scale the deck never reaches;
    // the gate is set at -12 dB, comfortably past that and comfortably short of
    // the -16.5 dB floor, so it fails on a reverted window without pinning the
    // by-ear constants. The number read here is measured, not derived.
    CHECK(full.gain_min < 0.25f);
    // Never below the floor -- with an absolute epsilon, because the floor is
    // computed as 1 - (1 - 0.15f) and lands on 0.14999998, not on 0.15
    // (engine-map.md section 5: write such a gate as ~6e-08 absolute, never as
    // an exact or ULP-relative comparison). Measured, 2.4e-08 under.
    CHECK(full.gain_min >= 0.15f - 1e-6f);
    CHECK(quarter.gain_min > full.gain_min);     // depth still orders the floor
}

TEST_CASE("choke duck: the priority deck and the cross-deck taps stay untouched") {
    // MORPH hard to A zeroes the yielding deck's mix gain, so any difference
    // left would be the duck bleeding onto the PRIORITY deck's path. The deck
    // taps feed the cross-deck audio bus and BODY's excitation and must not
    // see the duck either, at any morph.
    Instrument ref, ducked;
    arm_duck_rig(ref, 0.f);
    arm_duck_rig(ducked, -0.5f);
    ref.set_morph(0.f);
    ducked.set_morph(0.f);
    std::vector<float> rl(1), rr(1), dl(1), dr(1);
    for (int i = 0; i < 48000; ++i) {            // 1 s: ride out the MORPH glide
        ref.process(nullptr, nullptr, rl.data(), rr.data(), 1);
        ducked.process(nullptr, nullptr, dl.data(), dr.data(), 1);
    }
    for (int i = 0; i < 240000; ++i) {           // 5 s
        ref.process(nullptr, nullptr, rl.data(), rr.data(), 1);
        ducked.process(nullptr, nullptr, dl.data(), dr.data(), 1);
        REQUIRE(rl[0] == dl[0]);
        REQUIRE(rr[0] == dr[0]);
        for (int p = 0; p < PART_COUNT; ++p)
            for (int ch = 0; ch < 2; ++ch)
                REQUIRE(ref.deck_tap(p, ch) == ducked.deck_tap(p, ch));
    }
}

TEST_CASE("choke duck: a priority deck you cannot hear does not duck the one you can") {
    // The sidechain follows what you HEAR. MORPH hard to B makes the PRIORITY
    // deck (A, at negative choke) inaudible, so the yielding deck must come out
    // exactly as it does at CHOKE noon -- an inaudible deck has nothing to duck
    // with. Before the detector moved behind the MORPH/LEVEL gain this ducked B
    // to the floor on a deck contributing nothing to the bus, which also
    // contradicted the reverb send's M4 rule three lines below it.
    //
    // Bit-identity, not a tolerance: once MORPH has settled, ga is exactly 0,
    // so the detector's input is exactly 0, the envelope releases to 0 and the
    // gain is exactly 1.0f. The settle is ridden out before comparing rather
    // than tolerated -- during the glide the priority deck IS partly audible and
    // ducking is correct there.
    SUBCASE("morphed away") {
        Instrument ref, ducked;
        arm_duck_rig(ref, 0.f);
        arm_duck_rig(ducked, -0.4f);             // the ruling's case
        ref.set_morph(1.f);                      // hard to B: A is inaudible
        ducked.set_morph(1.f);
        std::vector<float> rl(1), rr(1), dl(1), dr(1);
        for (int i = 0; i < 48000; ++i) {        // 1 s: MORPH glide + duck release
            ref.process(nullptr, nullptr, rl.data(), rr.data(), 1);
            ducked.process(nullptr, nullptr, dl.data(), dr.data(), 1);
        }
        for (int i = 0; i < 240000; ++i) {       // 5 s
            ref.process(nullptr, nullptr, rl.data(), rr.data(), 1);
            ducked.process(nullptr, nullptr, dl.data(), dr.data(), 1);
            REQUIRE(ducked.choke_duck_gain() == 1.f);
            REQUIRE(rl[0] == dl[0]);
            REQUIRE(rr[0] == dr[0]);
        }
    }
    // Same claim through the other gain the detector now honours.
    SUBCASE("LEVEL at zero") {
        Instrument ref, ducked;
        arm_duck_rig(ref, 0.f);
        arm_duck_rig(ducked, -0.4f);
        ref.set_part_level(PART_A, 0.f);
        ducked.set_part_level(PART_A, 0.f);
        std::vector<float> rl(1), rr(1), dl(1), dr(1);
        for (int i = 0; i < 48000; ++i) {
            ref.process(nullptr, nullptr, rl.data(), rr.data(), 1);
            ducked.process(nullptr, nullptr, dl.data(), dr.data(), 1);
        }
        for (int i = 0; i < 240000; ++i) {
            ref.process(nullptr, nullptr, rl.data(), rr.data(), 1);
            ducked.process(nullptr, nullptr, dl.data(), dr.data(), 1);
            REQUIRE(ducked.choke_duck_gain() == 1.f);
            REQUIRE(rl[0] == dl[0]);
            REQUIRE(rr[0] == dr[0]);
        }
    }
}

// One reverb and one set of tape buffers per instrument -- the room is shared
// state, so the two renders cannot borrow each other's.
struct ChokeFx {
    std::vector<float> echo[PART_COUNT][2];
    AmbientReverb reverb;
    ChokeFx() {
        for (int p = 0; p < PART_COUNT; ++p)
            for (int ch = 0; ch < 2; ++ch) echo[p][ch].resize(Flux::kMaxSamples);
    }
    FxMem mem() {
        FxMem m;
        for (int p = 0; p < PART_COUNT; ++p)
            for (int ch = 0; ch < 2; ++ch) m.echo[p][ch] = echo[p][ch].data();
        m.reverb = &reverb;
        return m;
    }
};

TEST_CASE("choke duck: it reaches the reverb send, not only the dry mix") {
    // Priority deck dry-only, yielding deck WET-only: B's single path to the
    // output is the shared room's return, so a difference can only have come
    // through the send. set_reverb_mix runs before the first block, which is
    // where the mix gains snap (Instrument::process, _rev_primed), so B's dry
    // gain is exactly 0 from sample 0.
    static ChokeFx fx_ref, fx_duck;
    Instrument ref, ducked;
    ref.init(48000.f, fx_ref.mem());
    ducked.init(48000.f, fx_duck.mem());
    for (Instrument* in : { &ref, &ducked }) {
        in->set_tempo_bpm(120.f);
        for (int p = 0; p < PART_COUNT; ++p) {
            in->set_rate(p, p == PART_A ? 0.8f : 0.9f);
            in->set_density(p, 1.f);
            in->set_range(p, 1.f);
            in->set_part_level(p, 0.3f);
        }
        in->set_reverb_mix(PART_A, 0.f);         // priority deck stays dry
        in->set_reverb_mix(PART_B, 1.f);         // yielding deck is wet-only
        in->set_fx_target_base(PART_B, FXT_REV_SEND, 1.f);
    }
    ducked.set_choke(-0.5f);                     // duck zone: nothing is inhibited
    std::vector<float> rl(1), rr(1), dl(1), dr(1);
    double energy = 0.0;
    float ref_peak = 0.f;
    for (int i = 0; i < 480000; ++i) {           // 10 s
        ref.process(nullptr, nullptr, rl.data(), rr.data(), 1);
        ducked.process(nullptr, nullptr, dl.data(), dr.data(), 1);
        const double el = double(rl[0]) - dl[0], er = double(rr[0]) - dr[0];
        energy += el * el + er * er;
        ref_peak = std::max(ref_peak, std::max(std::fabs(rl[0]), std::fabs(rr[0])));
    }
    REQUIRE(ref_peak > 0.f);                     // sanity: the room returns audio
    CHECK(energy > 0.0);
}
