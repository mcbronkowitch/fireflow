#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "instrument.h"
#include "fx/flux.h"
#include "mod/divisions.h"
#include "mod/song_ladder.h"
#include "../bench/audition/init_patch.h"
#include "vcv/src/generated_panel.hpp"
#include "vcv/src/init_patch.hpp"


TEST_CASE("Seed audition applies the VCV init engine and arranger state")
{
    spky::Instrument inst;
    inst.init(48000.f);

    audition::apply_init_patch(inst);

    float in[96] = {};
    float out_l[96] = {};
    float out_r[96] = {};
    for(int block = 0; block < 8; ++block)
        inst.process(in, in, out_l, out_r, 96);

    CHECK(inst.engine_id(spky::PART_A) == spky::ENGINE_SYNTH);
    CHECK(inst.engine_id(spky::PART_B) == spky::ENGINE_BODY);

    // SONG_A/B's init default is a ladder rung index, not a raw Principle/
    // SongMode pair (spec 2026-08-09 hw-control-reduction task 3). Derive
    // the expectation from the same generated snapshot and the same ladder
    // the host reads, rather than restating the rung's contents as
    // literals -- the ladder is TASTE (song_ladder.h) and retuning it must
    // not break this wiring check.
    const int rung_a = static_cast<int>(
        std::lround(spkyvcv::initParamDefault(spkyvcv::SONG_A)));
    const int rung_b = static_cast<int>(
        std::lround(spkyvcv::initParamDefault(spkyvcv::SONG_B)));
    const spky::SongRung& song_a = spky::song_ladder_at(rung_a);
    const spky::SongRung& song_b = spky::song_ladder_at(rung_b);
    CHECK(inst.form(spky::PART_A) == song_a.form);
    CHECK(inst.form(spky::PART_B) == song_b.form);
    CHECK(inst.song(spky::PART_A) == song_a.song);
    CHECK(inst.song(spky::PART_B) == song_b.song);
}


TEST_CASE("Seed audition shares the complete generated VCV parameter snapshot")
{
    // This literal is expected to move whenever the panel inventory changes
    // -- most of the remaining tasks in the 2026-08-09 hw-control-reduction
    // plan remove more parameters. Update it to the new true count (and
    // nothing else) when that happens; do not carry a mismatch forward as a
    // "pre-existing failure" (spec 2026-08-09 hw-control-reduction task 3
    // review, Finding 5 -- this exact mistake shipped once already).
    CHECK_MESSAGE(spkyvcv::NUM_PARAMS == 68,
                  "NUM_PARAMS is " << spkyvcv::NUM_PARAMS << ", want 68 -- "
                  "if the panel inventory genuinely changed, update this "
                  "literal to match");
    CHECK(spkyvcv::initParamDefault(spkyvcv::ENGINE_A)
          == doctest::Approx(0.f));
    // drone.vcvm boots deck B on BODY (3), not on the sampler. The pin moved
    // with the snapshot; the two must never disagree, because
    // bench/audition/init_patch.cpp dispatches off this same table.
    CHECK(spkyvcv::initParamDefault(spkyvcv::ENGINE_B)
          == doctest::Approx(3.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::TEMPO)
          == doctest::Approx(0.169333577f));
    // DETUNE_A/B used to share one raw value, 6.f / 35.f (a linear knob into
    // the old 35 ct ceiling). Task 10 (spec 2026-08-09 hw-control-reduction)
    // squared the taper and tripled the synth-family ceiling to 105 ct, with
    // BODY's compensating kDetuneScale shrinking from 4 to 4/3 to hold its
    // own 140 ct rail -- but that compensation only agrees with the OLD
    // shared raw value at full knob travel, not at this init position. Deck
    // B boots on BODY, so its raw value is solved to keep BODY's own 24 ct
    // (what the old shared value produced there): sqrt(24 / 140).
    CHECK(spkyvcv::initParamDefault(spkyvcv::DETUNE_B)
          == doctest::Approx(std::sqrt(24.f / 140.f)));
    // LINK's default is the branch's one load-bearing product invariant:
    // centre (0) is the bit-identical path (spec 2026-07-28 flux-link).
    CHECK(spkyvcv::initParamDefault(spkyvcv::LINK_A) == doctest::Approx(0.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::LINK_B) == doctest::Approx(0.f));
    // FLUXRATE_A/B used to be normalized 0..1 floats run through
    // flux_division_index() (engine/mod/divisions.h); task 6 (spec
    // 2026-08-09 hw-control-reduction) turned the knob into a 12-detent
    // KNOBI whose value IS the index, so the snapshot now carries the
    // converted indices the old floats used to round to. FLUXTIME_A/B
    // (MULT) is retired along with its ParamId -- there is nothing left to
    // check here for it.
    CHECK(spky::flux_division_index(0.392727494f) == 4);
    CHECK(spky::flux_division_index(0.254666120f) == 3);
    CHECK(spkyvcv::initParamDefault(spkyvcv::FLUXRATE_A)
          == doctest::Approx(4.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::FLUXRATE_B)
          == doctest::Approx(3.f));
}


TEST_CASE("Seed audition's converted FLUXRATE index reproduces the "
          "pre-task-6 factory delay time")
{
    // Task 6 (spec 2026-08-09 hw-control-reduction) turned FLUXRATE_A/B
    // from a 0..1 knob run through flux_division_index() into the raw
    // division index itself, and retired FLUXTIME_A/B (MULT), pinning its
    // modulation sink FXT_FLUX_TIME to a hard-coded neutral base (0.5,
    // tape_time_mult(0.5) == 1). This proves the conversion didn't move the
    // factory delay time: an instrument built by hand off the OLD formula
    // (flux_division_index of the OLD normalized default) must land on
    // exactly the same tape delay target as one built by apply_init_patch
    // off the NEW converted snapshot. Flux::delay_target_for_test() is a
    // no-op until FLUX has real echo memory (Instrument::init(sr) alone is
    // "engine only, no FX chain"), so both instruments need an FxMem.
    struct TapeMem {
        std::vector<float> echo[spky::PART_COUNT][2];
        TapeMem() {
            for (int p = 0; p < spky::PART_COUNT; ++p)
                for (int ch = 0; ch < 2; ++ch)
                    echo[p][ch].resize(spky::Flux::kMaxSamples);
        }
        spky::FxMem mem() {
            spky::FxMem m;
            for (int p = 0; p < spky::PART_COUNT; ++p)
                for (int ch = 0; ch < 2; ++ch)
                    m.echo[p][ch] = echo[p][ch].data();
            return m;
        }
    };
    static TapeMem old_tape, new_tape;

    const float bpm = 40.f + spkyvcv::initParamDefault(spkyvcv::TEMPO) * 200.f;

    spky::Instrument old_inst;
    old_inst.init(48000.f, old_tape.mem());
    old_inst.set_tempo_bpm(bpm);
    old_inst.set_flux_rate(spky::PART_A,
                            spky::flux_division_index(0.392727494f));
    old_inst.set_flux_rate(spky::PART_B,
                            spky::flux_division_index(0.254666120f));
    old_inst.set_fx_target_base(spky::PART_A, spky::FXT_FLUX_TIME, 0.5f);
    old_inst.set_fx_target_base(spky::PART_B, spky::FXT_FLUX_TIME, 0.5f);

    spky::Instrument new_inst;
    new_inst.init(48000.f, new_tape.mem());
    audition::apply_init_patch(new_inst);

    float in[96] = {};
    float out_l[96] = {};
    float out_r[96] = {};
    for(int block = 0; block < 8; ++block) {
        old_inst.process(in, in, out_l, out_r, 96);
        new_inst.process(in, in, out_l, out_r, 96);
    }

    MESSAGE("bpm=" << bpm
        << " old_A=" << old_inst.flux_delay_target_for_test(spky::PART_A) << "s"
        << " new_A=" << new_inst.flux_delay_target_for_test(spky::PART_A) << "s"
        << " old_B=" << old_inst.flux_delay_target_for_test(spky::PART_B) << "s"
        << " new_B=" << new_inst.flux_delay_target_for_test(spky::PART_B) << "s");
    CHECK(new_inst.flux_delay_target_for_test(spky::PART_A)
          == doctest::Approx(old_inst.flux_delay_target_for_test(spky::PART_A)));
    CHECK(new_inst.flux_delay_target_for_test(spky::PART_B)
          == doctest::Approx(old_inst.flux_delay_target_for_test(spky::PART_B)));
}


TEST_CASE("Seed audition boots the same engines the VCV host does")
{
    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst);

    // set_engine() only arms a click-free swap; it completes inside
    // process() at the idle point, so engine_id() must be read after
    // running audio, exactly as the sibling test case above does.
    float in[96] = {};
    float out_l[96] = {};
    float out_r[96] = {};
    for(int block = 0; block < 8; ++block)
        inst.process(in, in, out_l, out_r, 96);

    // The snapshot puts deck B on BODY. Before 2026-07-31 this file had no
    // BODY arm and deck B silently booted as SAMPLER -- audible on the Seed,
    // invisible in every test.
    CHECK(inst.engine_id(spky::PART_A) == spky::ENGINE_SYNTH);
    CHECK(inst.engine_id(spky::PART_B) == spky::ENGINE_BODY);
}

TEST_CASE("Seed audition dispatcher routes generated STAGES by generated engine")
{
    std::array<float, spkyvcv::NUM_PARAMS> snapshot{};
    std::copy(std::begin(spkyvcv::kInitParamDefaults),
              std::end(spkyvcv::kInitParamDefaults), snapshot.begin());
    snapshot[spkyvcv::ENGINE_A] = 4.f;  // BBD
    snapshot[spkyvcv::ENGINE_B] = 1.f;  // SAMPLER (non-BBD, unquantized pitch)
    snapshot[spkyvcv::STAGES_A] = 0.8125f;
    snapshot[spkyvcv::STAGES_B] = 0.9375f;
    snapshot[spkyvcv::TUNE_A] = 0.5f;
    snapshot[spkyvcv::TUNE_B] = 0.5f;

    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst, snapshot.data());

    // Make pitch_cv expose the dispatched bases without lane modulation.
    inst.set_target_active(spky::PART_A, spky::LANE_PITCH, false);
    inst.set_target_active(spky::PART_B, spky::LANE_PITCH, false);
    float l = 0.f, r = 0.f;
    for(int i = 0; i < 4000; ++i)
        inst.process(nullptr, nullptr, &l, &r, 1);

    CHECK(inst.engine_id(spky::PART_A) == spky::ENGINE_BBD);
    CHECK(inst.engine_id(spky::PART_B) == spky::ENGINE_SAMPLER);
    // Raw, unquantized STAGES_A dispatched straight to pitch_cv -- this
    // depends on the deck booting in FLOW (STEPS_A's default is 0, i.e.
    // step mode off, spec 2026-08-09 hw-control-reduction task 3 review,
    // Finding 7). part.cpp's _pitch_q rule quantizes a BBD deck's pitch to
    // the scale grid whenever _step_on is true; only FLOW leaves it
    // continuous. If STEPS_A's default ever moves off 0 again, this value
    // will too -- see the same review's Finding 6 for the quantized number
    // and the full mechanism trace.
    CHECK(inst.pitch_cv(spky::PART_A) == doctest::Approx(0.8125f));
    CHECK(inst.pitch_cv(spky::PART_B) == doctest::Approx(0.5f));
}

// GRIT is one bipolar knob (spec 2026-08-09 hw-control-reduction task 4):
// sign picks Reduce/Drive, magnitude (past a 0.03 dead zone) is the mix.
// Task 4's review flagged that nothing exercised this mapping end to end --
// the panel guard only substring-checks Fireflow.cpp's source text, so a
// regression that declares kGritDead but stops wiring it into the fx_on
// gate (exactly the bug task 4 fixed) would still pass every existing
// check. These cases drive bench/audition/init_patch.cpp's
// apply_init_patch -- the same sign/magnitude/dead-zone mapping
// Fireflow.cpp's pushParams performs, reachable here from a doctest -- and
// read the engine back through the grit_*_for_test() observers
// (engine/instrument.h), not through source text.
namespace {
// Mirrors Fireflow.cpp's kGritDead / bench/audition/init_patch.cpp's
// kGritDead. res/test_panel.py's test_grit_dead_zone_constant_agrees
// keeps those two -- and only those two -- in sync; this local copy is a
// third, independent expectation, not a fourth source of truth to sync.
constexpr float kGritDeadForTest = 0.03f;
}  // namespace

TEST_CASE("Seed audition: negative GRIT engages Reduce and leaves the effect on")
{
    std::array<float, spkyvcv::NUM_PARAMS> snapshot{};
    std::copy(std::begin(spkyvcv::kInitParamDefaults),
              std::end(spkyvcv::kInitParamDefaults), snapshot.begin());
    snapshot[spkyvcv::GRIT_A] = -0.5f;

    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst, snapshot.data());

    // This is the exact case the task-4 fix rescued: set_fx_on's old
    // `pp(GRIT_A, p) > 1e-4f` gate tested the RAW (now signed) value, so
    // every negative GRIT value failed it and Reduce/CRSH was silently
    // muted no matter how far left the knob turned. If that regresses,
    // this CHECK goes red.
    CHECK(static_cast<int>(inst.grit_mode_for_test(spky::PART_A))
          == static_cast<int>(spky::GritMode::Reduce));
    CHECK(inst.grit_engaged_for_test(spky::PART_A));
}

TEST_CASE("Seed audition: positive GRIT engages Drive and leaves the effect on")
{
    std::array<float, spkyvcv::NUM_PARAMS> snapshot{};
    std::copy(std::begin(spkyvcv::kInitParamDefaults),
              std::end(spkyvcv::kInitParamDefaults), snapshot.begin());
    snapshot[spkyvcv::GRIT_B] = 0.5f;

    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst, snapshot.data());

    CHECK(static_cast<int>(inst.grit_mode_for_test(spky::PART_B))
          == static_cast<int>(spky::GritMode::Drive));
    CHECK(inst.grit_engaged_for_test(spky::PART_B));
}

TEST_CASE("Seed audition: GRIT at the dead-zone boundary is off with zero mix, both signs")
{
    std::array<float, spkyvcv::NUM_PARAMS> snapshot{};
    std::copy(std::begin(spkyvcv::kInitParamDefaults),
              std::end(spkyvcv::kInitParamDefaults), snapshot.begin());
    snapshot[spkyvcv::GRIT_A] = -kGritDeadForTest;
    snapshot[spkyvcv::GRIT_B] = kGritDeadForTest;

    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst, snapshot.data());

    CHECK_FALSE(inst.grit_engaged_for_test(spky::PART_A));
    CHECK(inst.grit_mix_for_test(spky::PART_A) == doctest::Approx(0.f));
    CHECK_FALSE(inst.grit_engaged_for_test(spky::PART_B));
    CHECK(inst.grit_mix_for_test(spky::PART_B) == doctest::Approx(0.f));
}

TEST_CASE("Seed audition: full-deflection GRIT reaches mix 1.0, both signs")
{
    std::array<float, spkyvcv::NUM_PARAMS> snapshot{};
    std::copy(std::begin(spkyvcv::kInitParamDefaults),
              std::end(spkyvcv::kInitParamDefaults), snapshot.begin());
    snapshot[spkyvcv::GRIT_A] = -1.f;
    snapshot[spkyvcv::GRIT_B] = 1.f;

    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst, snapshot.data());

    CHECK(inst.grit_mix_for_test(spky::PART_A) == doctest::Approx(1.f));
    CHECK(inst.grit_mix_for_test(spky::PART_B) == doctest::Approx(1.f));
}

// Review finding IMPORTANT 6 (2026-08-09 hw-control-reduction final review):
// four times in this branch a control changed meaning while its stored init
// value stayed put, silently changing how the factory patch sounds --
// caught each time only because a human reasoned it through. The FLUXRATE
// case above is the one exception: a bespoke test compares the old
// formula's delay time against the new one. This generalises that idea into
// one golden test that pins every engine-side observable the owner would
// actually hear, applying the SAME snapshot apply_init_patch() gives the
// real host. Every expected number below is a LITERAL, computed by hand
// from the formulas in bench/audition/init_patch.cpp / Fireflow.cpp's
// pushParams as they stand today, NOT re-derived from the same constants
// the production code uses -- if it referenced e.g. spky::SynthEngine::
// kDetuneCeilCt directly, a ceiling change would move both sides together
// and this test could never catch it. A future control merge that changes
// what the factory patch sounds like must change a number here and justify
// why, instead of depending on somebody noticing by ear.
TEST_CASE("Seed audition's factory init pins every engine-side observable "
          "a future control merge could silently move")
{
    // FLUX needs real echo memory to report a delay target (Flux::
    // delay_target_for_test() is a no-op otherwise -- see the FLUXRATE case
    // above). The reverb, sampler and BBD buffers are left null: this test
    // does not touch any of them, and FxMem documents null as "that part
    // runs silent," not a crash.
    struct TapeMem {
        std::vector<float> echo[spky::PART_COUNT][2];
        TapeMem() {
            for (int p = 0; p < spky::PART_COUNT; ++p)
                for (int ch = 0; ch < 2; ++ch)
                    echo[p][ch].resize(spky::Flux::kMaxSamples);
        }
        spky::FxMem mem() {
            spky::FxMem m;
            for (int p = 0; p < spky::PART_COUNT; ++p)
                for (int ch = 0; ch < 2; ++ch)
                    m.echo[p][ch] = echo[p][ch].data();
            return m;
        }
    };
    static TapeMem tape;
    static spky::AmbientReverb reverb;

    spky::Instrument inst;
    spky::FxMem mem = tape.mem();
    mem.reverb = &reverb;
    inst.init(48000.f, mem);
    audition::apply_init_patch(inst);

    float in[96] = {};
    float out_l[96] = {};
    float out_r[96] = {};
    // DRIFT glides through a 0.3 s OnePole (Center::init), unlike the
    // instantaneous controls checked below -- run several time constants'
    // worth of blocks (2500 * 96 samples == 5 s) so drift() has actually
    // converged, the same wait test_center.cpp uses for the same reason.
    for (int block = 0; block < 2500; ++block)
        inst.process(in, in, out_l, out_r, 96);

    // --- per-deck grit mode and mix: both decks boot GRIT_A/B == 0.0, ---
    // inside the 0.03 dead zone -- CRSH/DRV is present but silent.
    CHECK(static_cast<int>(inst.grit_mode_for_test(spky::PART_A))
          == static_cast<int>(spky::GritMode::Drive));
    CHECK(static_cast<int>(inst.grit_mode_for_test(spky::PART_B))
          == static_cast<int>(spky::GritMode::Drive));
    CHECK(inst.grit_mix_for_test(spky::PART_A) == doctest::Approx(0.f));
    CHECK(inst.grit_mix_for_test(spky::PART_B) == doctest::Approx(0.f));

    // --- flux delay target: the tape echo time TIME/FLUXRATE resolves to. ---
    // Same numbers the FLUXRATE-index test case above independently derives
    // from the pre-task-6 formula (bpm 73.8667): deck A's division index 4,
    // deck B's index 3, at the factory tempo.
    CHECK(inst.flux_delay_target_for_test(spky::PART_A)
          == doctest::Approx(0.609205f).epsilon(0.001));
    CHECK(inst.flux_delay_target_for_test(spky::PART_B)
          == doctest::Approx(0.812274f).epsilon(0.001));

    // --- form and song per deck: rung 6 on both decks -- the "no ---
    // alternation, two generators" rung that keeps the approved
    // HIERARCHICAL/AAAB boot sound (song_ladder.h rung 6 == {2, 0}).
    CHECK(inst.form(spky::PART_A) == 2);   // Principle::Hierarchical
    CHECK(inst.form(spky::PART_B) == 2);
    CHECK(inst.song(spky::PART_A) == 0);   // SongMode::AAAB
    CHECK(inst.song(spky::PART_B) == 0);

    // --- per-part level and compressor amount: COMP_A/B == 0.6 sits ---
    // exactly on kLvlCompSplit (0.6) -- both decks boot at unity output
    // gain with the compressor fully disengaged (its top two fifths never
    // open at this knob position). The init value tracks the split: move
    // one without the other and the factory patch boots into make-up gain.
    CHECK(inst.part_level_for_test(spky::PART_A) == doctest::Approx(1.f));
    CHECK(inst.part_level_for_test(spky::PART_B) == doctest::Approx(1.f));
    CHECK(inst.comp_amount_for_test(spky::PART_A) == doctest::Approx(0.f));
    CHECK(inst.comp_amount_for_test(spky::PART_B) == doctest::Approx(0.f));

    // --- applied detune cents per deck: deck A boots SYNTH, deck B boots ---
    // BODY -- they differ by design, both by knob position and by engine.
    // This is the pre-kDetuneScale spread SynthEngineT::set_detune stores
    // (0..105 ct units shared by both engine families); BODY additionally
    // scales this by its own kDetuneScale (4/3) at the audio callsite, so
    // deck B's actual partial spread is 18 * 4/3 == 24 ct -- the by-ear
    // BODY default this branch's move preserved (see the DETUNE_B comment
    // on the snapshot test above).
    CHECK(inst.applied_detune_ct_for_test(spky::PART_A)
          == doctest::Approx(6.f).epsilon(0.0001));    // deck A (SYNTH): ~6 ct, the old shared default
    CHECK(inst.applied_detune_ct_for_test(spky::PART_B)
          == doctest::Approx(18.f).epsilon(0.0001));   // deck B (BODY): pre-scale; *4/3 == 24 ct final

    // --- drift, couple, sync ---
    // COUPLE == 1.0 (top of travel): grid zone, SYNC on, couple == 1.0 (the
    // texture lanes lock fully to the grid).
    CHECK(inst.synced(spky::PART_A) == true);
    CHECK(inst.synced(spky::PART_B) == true);
    CHECK(inst.couple() == doctest::Approx(1.f));
    // DRIFT == 0.959493291, past the 0.02 SETL zone: (0.959493291 - 0.02) /
    // (1 - 0.02) -- a lively but not maxed-out weather walk.
    CHECK(inst.drift() == doctest::Approx(0.958667f).epsilon(0.0001));

    // --- master drive, reverb smear, reverb mod: fixed-by-ear constants ---
    // (spec 2026-08-09 hw-control-reduction task 9). PUSH == 0.40 ->
    // Limiter's pre-gain 1 + 3*0.4*0.4 == 1.48 (a warm, not transparent,
    // ceiling). SMEAR == 0.30 (a moderate wash), WOBL/MOD == 0.15 (gentle
    // tail wobble).
    CHECK(inst.master_drive_pre_gain_for_test() == doctest::Approx(1.48f));
    CHECK(inst.reverb_smear_for_test() == doctest::Approx(0.30f));
    CHECK(inst.reverb_mod_for_test() == doctest::Approx(0.15f));
}
