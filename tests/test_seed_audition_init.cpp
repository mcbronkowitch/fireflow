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
    CHECK_MESSAGE(spkyvcv::NUM_PARAMS == 75,
                  "NUM_PARAMS is " << spkyvcv::NUM_PARAMS << ", want 75 -- "
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
    CHECK(spkyvcv::initParamDefault(spkyvcv::DETUNE_B)
          == doctest::Approx(6.f / 35.f));
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
