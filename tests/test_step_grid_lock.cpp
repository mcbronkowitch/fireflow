#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "mod/super_modulator.h"
#include "mod/lane_len.h"
#include "mod/divisions.h"
#include "mod/shuffle_grid.h"
using namespace spky;

TEST_CASE("steplock: STEP gives each lane its own slot count") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);

    CHECK(m.lane_slots_for_test(LANE_SOURCE) ==  4);
    CHECK(m.lane_slots_for_test(LANE_LEVEL)  ==  6);
    CHECK(m.lane_slots_for_test(LANE_PITCH)  ==  8);
    CHECK(m.lane_slots_for_test(LANE_MOTION) == 12);
    CHECK(m.lane_slots_for_test(LANE_SIZE)   == 16);
}

TEST_CASE("steplock: TIDE moves slot counts in STEP, not rates") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);
    const float r = m.lane_rate_hz_for_test(LANE_PITCH);

    m.set_tide(0.25f);                       // ladder rung x1/2
    REQUIRE(kTideRatios[tide_index(0.25f)] == doctest::Approx(0.5f));
    CHECK(m.lane_rate_hz_for_test(LANE_PITCH) == doctest::Approx(r));
    CHECK(m.lane_slots_for_test(LANE_SOURCE) ==  8);
    CHECK(m.lane_slots_for_test(LANE_SIZE)   == 32);
    CHECK(m.lane_slots_for_test(LANE_MOTION) == 24);
    CHECK(m.lane_slots_for_test(LANE_LEVEL)  == 12);
    CHECK(m.lane_slots_for_test(LANE_PITCH)  ==  8);   // the phrase, always
}

TEST_CASE("steplock: the deck step count follows the pitch lane") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);

    int last = m.pitch_cur_step();
    int changes = 0;
    for (int i = 0; i < 200000; ++i) {
        m.process();
        if (m.pitch_cur_step() != last) { last = m.pitch_cur_step(); ++changes; }
    }
    REQUIRE(changes > 8);
    // The count starts at 0 on the first step, so it trails the change count
    // by exactly one.
    CHECK(m.deck_step_for_test() == changes - 1);
}

TEST_CASE("steplock: FLOW keeps the old ratios, TIDE and mod_scale") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(false, 8);
    m.set_rate_scale(1.f, 2.f);

    const float pitch = m.lane_rate_hz_for_test(LANE_PITCH);
    CHECK(m.lane_rate_hz_for_test(LANE_SOURCE)
          == doctest::Approx(pitch * 2.f * 2.f));      // mod_scale x ratio
    CHECK(m.lane_rate_hz_for_test(LANE_MOTION)
          == doctest::Approx(pitch * 2.f * 0.75f));
    for (int i = 0; i < LANE_COUNT; ++i)
        CHECK(m.lane_slots_for_test(i) == 8);          // no per-lane slots
}

TEST_CASE("steplock: FLOW lane ratios are unchanged on the phase") {
    SuperModulator m;
    m.init(48000.f, 42u);
    m.set_rate(0.3f);
    m.set_step(false, 8);
    for (int i = 0; i < ModLane::kTickInterval; ++i) m.process();
    const float pitch = m.lane_phase(LANE_PITCH);
    CHECK(m.lane_phase(LANE_SOURCE) == doctest::Approx(pitch * 2.00f));
    CHECK(m.lane_phase(LANE_SIZE)   == doctest::Approx(pitch * 0.50f));
    CHECK(m.lane_phase(LANE_MOTION) == doctest::Approx(pitch * 0.75f));
    CHECK(m.lane_phase(LANE_LEVEL)  == doctest::Approx(pitch * 1.50f));
}

TEST_CASE("steplock: a live SHUFFLE turn does not clamp the follower phase") {
    // Bug (review of 4c90027): SuperModulator::process() derives the deck's
    // follow fraction from a mirrored SHUFFLE target that moves the instant
    // set_shuffle() is called, but the PITCH lane's own _shuffle_latched --
    // the amount that actually produced its _phase -- only updates on an
    // even-indexed step entry. A live SHUFFLE turn landing on an odd PITCH
    // step therefore computes the fraction against boundaries that never
    // produced that phase, and shuffle_step_fraction's clamp to [0,1] pins
    // every texture lane's phase to a slot edge until the next even entry.
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.2f);          // slow: a STEP takes tens of thousands of
                                // samples, far more than one 96-sample tick,
                                // so PITCH cannot cross a boundary mid-test
    m.set_shuffle(0.2f);
    m.set_step(true, 8);

    // lane_fired() latches until the next follow() call (once per raster
    // tick), so it has to be sampled right at the tick, not on every sample
    // -- otherwise the same fire gets counted on all 96 samples it persists.
    int call_idx = 0;
    int fires_source = 0;
    auto step_once = [&]() {
        m.process();
        const bool is_tick = (call_idx % ModLane::kTickInterval) == 0;
        ++call_idx;
        if (is_tick && m.lane_fired(LANE_SOURCE)) ++fires_source;
    };

    // Advance until PITCH sits well inside an ODD step (clear of the entry
    // boundary, so the margin below cannot itself cross into the next step).
    while (m.pitch_cur_step() < 0 || m.pitch_cur_step() % 2 == 0) step_once();
    for (int i = 0; i < 50; ++i) step_once();
    REQUIRE(m.pitch_cur_step() % 2 == 1);
    const int odd_step = m.pitch_cur_step();

    m.set_shuffle(0.9f);       // the live SHUFFLE turn, mid odd PITCH step

    // One more raster tick: any 96 consecutive process() calls contain
    // exactly one texture-lane follow() update.
    for (int i = 0; i < ModLane::kTickInterval; ++i) step_once();

    REQUIRE(m.pitch_cur_step() == odd_step);   // no boundary crossed meanwhile

    // The amount PITCH's phase (and so the deck's fraction) was actually
    // built from -- the one every follower's position must agree with.
    const float pitch_amt = m.pitch_shuffle_latched_for_test();
    for (int i = 0; i < LANE_COUNT; ++i) {
        if (i == LANE_PITCH) continue;
        const int slots = m.lane_slots_for_test(i);
        const float phase = m.lane_phase(i);
        const int step = shuffle_step_index(phase, slots, pitch_amt);
        const float frac = shuffle_step_fraction(phase, step, slots, pitch_amt);
        CHECK(frac > 0.f);
        CHECK(frac < 1.f);
    }

    // The fix must not disturb the deck-step/fire-count bookkeeping: one
    // fire per distinct deck step, including the cold-start landing.
    CHECK(fires_source == m.deck_step_for_test() + 1);
}

namespace {
// In follower mode every deck step gives every texture lane exactly one
// boundary. So "on the grid" needs no sample arithmetic and no tolerance at
// all: the fire counts simply have to match the deck's step count. That
// equality is what an equal-rate design could not hold -- it drifted about two
// samples per step and lost a whole one every 90 seconds.
//
// A boundary is reported at the raster edge that covers it, and fired()
// latches until the next follow(), so counting at raster edges counts each
// boundary exactly once as long as a step is never shorter than the raster.
// At the fastest panel rate a step is ~200 samples against a 96-sample raster.
struct LockResult {
    int   deck_steps = 0;
    int   fires[LANE_COUNT] = {0, 0, 0, 0, 0};
    float end_phase[LANE_COUNT] = {0.f, 0.f, 0.f, 0.f, 0.f};
};

LockResult run_locked(float shape, bool chaos, int samples, int spot_at = -1) {
    SuperModulator m;
    m.init(48000.f, 99u);
    m.set_rate(0.45f);
    m.set_step(true, 8);
    m.set_shape(shape);
    m.set_smooth(0.f);
    m.set_tide(0.25f);                       // off-centre ladder rung
    if (chaos) {
        m.set_variation(0.8f);               // EVOLVE walks the master rate
        m.set_rate_scale(1.f, 1.6f);         // DRIFT: mod_scale != pitch_scale
        m.set_shuffle(0.6f);
    }
    Rng spot_rng;
    spot_rng.seed(5u);

    // The loop runs one call PAST `samples`, and `samples` is required to be a
    // whole number of raster windows, so that final call is itself a tick. A
    // deck step landing in the last window is counted the sample it happens,
    // but its boundary is only reported at the NEXT follow() -- without the
    // flush the equality below would hold by luck of where the last transition
    // fell rather than by construction. Two deck steps inside one window would
    // still collapse into one latched fired(), but at any panel-reachable rate
    // a step is ~200 samples against a 96-sample raster.
    REQUIRE(samples % ModLane::kTickInterval == 0);
    LockResult r;
    int last = m.pitch_cur_step();
    for (int i = 0; i <= samples; ++i) {
        if (i == spot_at) m.spot(spot_rng);
        m.process();
        if (m.pitch_cur_step() != last) { last = m.pitch_cur_step(); ++r.deck_steps; }
        if (i % ModLane::kTickInterval == 0)
            for (int l = 0; l < LANE_COUNT; ++l)
                if (l != LANE_PITCH && m.lane_fired(l)) ++r.fires[l];
    }
    for (int l = 0; l < LANE_COUNT; ++l) r.end_phase[l] = m.lane_phase(l);
    return r;
}
} // namespace

TEST_CASE("steplock: every texture boundary is a deck step, under chaos") {
    const LockResult r = run_locked(0.5f, true, 480000, -1);
    REQUIRE(r.deck_steps >= 16);
    for (int l = 0; l < LANE_COUNT; ++l)
        if (l != LANE_PITCH) CHECK(r.fires[l] == r.deck_steps);
}

TEST_CASE("steplock: SPOT stumbles by whole slots and stays on the grid") {
    const LockResult plain = run_locked(0.5f, true, 480000);
    const LockResult spot  = run_locked(0.5f, true, 480000, 160000);
    REQUIRE(spot.deck_steps >= 16);
    CHECK(spot.deck_steps == plain.deck_steps);   // SPOT never touches PITCH

    // The offset has to have landed somewhere: at least one texture lane sits
    // at a different point in its cycle than the un-stumbled run.
    bool moved = false;
    for (int l = 0; l < LANE_COUNT; ++l)
        if (l != LANE_PITCH && spot.end_phase[l] != plain.end_phase[l]) moved = true;
    CHECK(moved);

    // And it stayed on the grid. The jump adds at most one extra boundary --
    // the stumble itself -- and it is only separately countable when it falls
    // in a raster window that had no deck step of its own, so the count is
    // deck_steps or deck_steps + 1, never more and never less.
    for (int l = 0; l < LANE_COUNT; ++l) {
        if (l == LANE_PITCH) continue;
        CHECK(spot.fires[l] >= spot.deck_steps);
        CHECK(spot.fires[l] <= spot.deck_steps + 1);
    }
}

TEST_CASE("steplock: the lock holds over ten minutes of audio") {
    // The test the reverted equal-rate design would have failed. Ten minutes
    // is well past the ~90 s at which that design lost a whole step.
    const LockResult r = run_locked(0.5f, true, 48000 * 600);
    REQUIRE(r.deck_steps >= 2000);
    for (int l = 0; l < LANE_COUNT; ++l)
        if (l != LANE_PITCH) CHECK(r.fires[l] == r.deck_steps);
}

TEST_CASE("steplock: the fire grid is identical at every SHAPE setting") {
    // This is the whole point of the change: SHAPE was the detector, never the
    // cause. At the S&H end an offset reads as different random values; left
    // of it, as a ramp stepping beside the beat.
    //
    // Counts alone are not enough: a shape-dependent slot shift moves where
    // every lane sits without changing how often it fires. Comparing only
    // deck_steps and fires[] would pass a follow() that adds a shape-gated
    // offset to its slot -- every lane still fires once per deck step, just
    // at a different slot -- so end_phase, which run_locked already
    // computes, has to agree too.
    const LockResult ref = run_locked(1.f, true, 480000);
    for (float shape : {0.f, 0.25f, 0.5f, 0.75f}) {
        const LockResult r = run_locked(shape, true, 480000);
        CHECK(r.deck_steps == ref.deck_steps);
        for (int l = 0; l < LANE_COUNT; ++l) {
            if (l == LANE_PITCH) continue;
            CHECK(r.fires[l] == ref.fires[l]);
            CHECK(r.end_phase[l] == doctest::Approx(ref.end_phase[l]));
        }
    }
}

TEST_CASE("steplock: rests advance the grid, so followers move when the melody does not") {
    // SuperModulator::process() counts step-index CHANGES on the PITCH lane,
    // not fires -- the comment above that block says why: "a gated melodic
    // step still advances the grid -- the followers must move even when the
    // melody rests." set_density() only reaches the PITCH lane (see its
    // forwarder above), so a low DENSE masks most of PITCH's own note fires
    // while its step index keeps changing on schedule, giving a direct test
    // of that claim.
    SuperModulator m;
    m.init(48000.f, 99u);
    m.set_rate(0.45f);
    m.set_density(0.25f);      // most PITCH steps rest (gated off)
    m.set_step(true, 8);

    const int32_t kRunSamples = 480000;
    REQUIRE(kRunSamples % ModLane::kTickInterval == 0);   // same trailing-tick reasoning as run_locked

    int deck_steps = 0, pitch_fires = 0, fires[LANE_COUNT] = {0, 0, 0, 0, 0};
    int last = m.pitch_cur_step();
    for (int32_t i = 0; i <= kRunSamples; ++i) {
        m.process();
        if (m.pitch_cur_step() != last) { last = m.pitch_cur_step(); ++deck_steps; }
        // PITCH is driven per-sample, and fired() latches until the next
        // process() call -- unlike the texture lanes' once-per-raster-tick
        // fired(), sampling every sample counts each note onset exactly once.
        if (m.lane_fired(LANE_PITCH)) ++pitch_fires;
        if (i % ModLane::kTickInterval == 0)
            for (int l = 0; l < LANE_COUNT; ++l)
                if (l != LANE_PITCH && m.lane_fired(l)) ++fires[l];
    }
    REQUIRE(deck_steps >= 16);

    // Prove the rests actually happened -- otherwise this case could pass
    // vacuously (e.g. under a lane_fired(LANE_PITCH)-driven deck counter,
    // where PITCH's fire count and the deck-step count are the same thing
    // by construction, so equality would tell us nothing).
    CHECK(pitch_fires < deck_steps);

    // The claim under test: every texture lane still gets one boundary per
    // deck step, rests or not.
    for (int l = 0; l < LANE_COUNT; ++l)
        if (l != LANE_PITCH) CHECK(fires[l] == deck_steps);
}

TEST_CASE("steplock: a live STEPS turn keeps the deck aligned") {
    SuperModulator m;
    m.init(48000.f, 99u);
    m.set_rate(0.45f);
    m.set_step(true, 8);

    int deck_steps = 0, fires[LANE_COUNT] = {0, 0, 0, 0, 0};
    int last = m.pitch_cur_step();
    // The loop runs one call PAST 480000 samples, same as run_locked: 480000
    // is a whole number of 96-sample raster windows, so the extra call is
    // itself a tick and flushes a fire that landed in the final window with
    // no follow() left to report it (see run_locked's own comment above).
    REQUIRE(480000 % ModLane::kTickInterval == 0);
    for (int i = 0; i <= 480000; ++i) {
        if (i == 160000) m.set_step(true, 16);
        if (i == 320000) m.set_step(true, 8);
        m.process();
        if (m.pitch_cur_step() != last) { last = m.pitch_cur_step(); ++deck_steps; }
        if (i % ModLane::kTickInterval == 0)
            for (int l = 0; l < LANE_COUNT; ++l)
                if (l != LANE_PITCH && m.lane_fired(l)) ++fires[l];
    }
    REQUIRE(deck_steps >= 16);
    for (int l = 0; l < LANE_COUNT; ++l)
        if (l != LANE_PITCH) CHECK(fires[l] == deck_steps);
}

TEST_CASE("steplock: RST puts every lane back on slot 0") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.45f);
    m.set_step(true, 8);

    // Run well past every texture lane's slot count (max 16, for SIZE) so a
    // stumbled resync would land on an obviously wrong slot, not on 0 by
    // luck. Matches the bug report's own repro point (_deck_step == 22).
    while (m.deck_step_for_test() < 22) m.process();
    REQUIRE(m.deck_step_for_test() >= 22);

    // Slow PITCH to a crawl before resetting, so the one raster tick this
    // test advances afterward cannot itself carry PITCH into step 1 -- the
    // point is to isolate reset_phases()'s own effect from step timing.
    m.set_rate(0.f);

    m.reset_phases();

    // The PITCH lane restarts on the very next sample (per-sample path); the
    // texture lanes only catch up at their next raster tick, up to 96
    // samples later -- see reset_phases()'s own comment.
    for (int i = 0; i < ModLane::kTickInterval; ++i) m.process();

    CHECK(m.deck_step_for_test() == 0);
    CHECK(m.pitch_phase() < 0.001f);
    for (int i = 0; i < LANE_COUNT; ++i) {
        if (i == LANE_PITCH) continue;
        const int slots = m.lane_slots_for_test(i);
        const int slot  = shuffle_step_index(m.lane_phase(i), slots, 0.f);
        CHECK(slot == 0);
    }
}

TEST_CASE("steplock: a mid-STEP snap does not bank a phantom deck step") {
    // snap_pitch_phase() is a positional jump, not elapsed time: it moves
    // PITCH straight to a given phase without ever crossing the boundaries
    // in between. ModLane::reset() (called from snap_pitch_phase()) leaves
    // _cur_step at -1, and snap_pitch_phase() must re-arm
    // _last_pitch_step to -1 right along with it -- SuperModulator::process()
    // banks a deck step on every _cur_step CHANGE it sees, with no way to
    // tell "the lane actually played through a step" apart from "the lane's
    // step index just looks different because of a jump". Without that
    // rearm, the stale pre-snap _last_pitch_step disagrees with the
    // post-snap cur_step on the very next process() call, and the deck
    // counts a step that was never played -- rotating all four texture
    // followers onto a slot the deck never crossed.
    SuperModulator m; m.init(48000.f, 7u); m.set_rate(0.45f); m.set_step(true, 8);
    while (m.deck_step_for_test() < 5) m.process();
    const int32_t before = m.deck_step_for_test();
    m.snap_pitch_phase(m.pitch_cur_step() == 0 ? 0.6f : 0.02f);
    for (int i = 0; i < 4; ++i) m.process();
    CHECK(m.deck_step_for_test() == before);
}

TEST_CASE("steplock: leaving STEP hands the lanes back their own clocks") {
    SuperModulator m;
    m.init(48000.f, 99u);
    m.set_rate(0.45f);
    m.set_step(true, 8);
    for (int i = 0; i < 240000; ++i) m.process();

    m.set_step(false, 8);
    for (int i = 0; i < ModLane::kTickInterval * 64; ++i) m.process();

    // Back on their own ratios: SOURCE runs at twice the master's rate.
    CHECK(m.lane_rate_hz_for_test(LANE_SOURCE)
          == doctest::Approx(m.lane_rate_hz_for_test(LANE_PITCH) * 2.f));
    for (int i = 0; i < LANE_COUNT; ++i) {
        CHECK(m.lane_slots_for_test(i) == 8);
        CHECK(m.lane_phase(i) >= 0.f);
        CHECK(m.lane_phase(i) <  1.f);
    }

    // Rates, slot counts and a phase sitting in [0, 1) all survive a frozen
    // clock -- a tick() turned into a no-op for these lanes leaves every
    // assertion above green. Only running the deck further and counting
    // actual wrap boundaries proves the clock is moving at all, let alone at
    // the right speed. In FLOW a non-melodic lane fires once per cycle wrap,
    // so over a known number of samples each texture lane's wrap count has
    // to match that lane's own configured rate -- pinning three things at
    // once: the clock advances, it advances at the lane's rate, and the old
    // per-lane ratios are back (SOURCE wraps twice as often as PITCH, SIZE
    // half as often).
    //
    // One call past kRunSamples, same loop shape as run_locked above -- but
    // unlike STEP's follow(), FLOW's tick() decides a wrap and latches
    // fired() entirely within the call whose window contains it, so there is
    // never anything pending for a later call to flush (contrast run_locked's
    // comment, which is about follow() and does not apply here). The extra
    // iteration is simply the tick() call for the LAST raster window: since
    // kRunSamples is a whole number of 96-sample windows, without it that
    // window's fired() flags would never get read at all.
    const int32_t kRunSamples = 480000;
    REQUIRE(kRunSamples % ModLane::kTickInterval == 0);
    int32_t fires[LANE_COUNT] = {0, 0, 0, 0, 0};
    for (int32_t i = 0; i <= kRunSamples; ++i) {
        m.process();
        if (i % ModLane::kTickInterval == 0)
            for (int l = 0; l < LANE_COUNT; ++l)
                if (l != LANE_PITCH && m.lane_fired(l)) ++fires[l];
    }
    for (int l = 0; l < LANE_COUNT; ++l) {
        if (l == LANE_PITCH) continue;
        const float rate_hz  = m.lane_rate_hz_for_test(l);
        const float expected = rate_hz * 480000.f / 48000.f;
        CHECK(std::fabs(static_cast<float>(fires[l]) - expected) <= 1.f);
    }
}
