#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "instrument.h"
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
    CHECK_MESSAGE(spkyvcv::NUM_PARAMS == 80,
                  "NUM_PARAMS is " << spkyvcv::NUM_PARAMS << ", want 80 -- "
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
    CHECK(spkyvcv::initParamDefault(spkyvcv::FLUXTIME_A)
          == doctest::Approx(0.5f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::FLUXTIME_B)
          == doctest::Approx(0.5f));
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
    // PART_A's pitch is quantized, not the raw STAGES_A=0.8125 dispatched
    // above: this snapshot only overrides ENGINE/STAGES/TUNE and inherits
    // STEPS_A's real default (16) from kInitParamDefaults, and
    // part.cpp's _pitch_q rule quantizes a BBD deck's pitch whenever
    // _step_on is true ("STEP puts the clock on scale steps... FLOW leaves
    // it continuous"). bench/audition/init_patch.cpp derives _step_on as
    // `steps > 0` (spec 2026-08-09 hw-control-reduction task 3 review,
    // fixing a build break -- STEP's own pad was retired by an earlier
    // task in this plan and this bench file was never updated to match
    // Fireflow.cpp's `inst.set_step(p, steps > 0, steps)`, so it had never
    // actually compiled, let alone run, since). STEPS_A=16 > 0, so STEP is
    // on by default and 0.8125 (29.25 of 36 semitones, quantizer.h's
    // SPAN_SEMIS) snaps to the nearest Lydian-scale step, 31/36 -- verified
    // by temporarily forcing _step_on false, which restores the raw
    // 0.8125 exactly. This is correct production behavior, not a defect:
    // a real BBD deck boots with STEP on, same as here.
    CHECK(inst.pitch_cv(spky::PART_A) == doctest::Approx(31.f / 36.f));
    CHECK(inst.pitch_cv(spky::PART_B) == doctest::Approx(0.5f));
}
