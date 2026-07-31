#include <doctest/doctest.h>

#include "instrument.h"
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
    CHECK(inst.form(spky::PART_A) == 2);
    CHECK(inst.form(spky::PART_B) == 2);
    CHECK(inst.song(spky::PART_A) == 0);
    CHECK(inst.song(spky::PART_B) == 0);
}


TEST_CASE("Seed audition shares the complete generated VCV parameter snapshot")
{
    CHECK(spkyvcv::NUM_PARAMS == 84);
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
