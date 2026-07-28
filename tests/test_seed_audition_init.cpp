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
    CHECK(inst.engine_id(spky::PART_B) == spky::ENGINE_SAMPLER);
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
    CHECK(spkyvcv::initParamDefault(spkyvcv::ENGINE_B)
          == doctest::Approx(1.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::TEMPO)
          == doctest::Approx(0.5f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::DETUNE_B)
          == doctest::Approx(6.f / 35.f));
    // LINK's default is the branch's one load-bearing product invariant:
    // centre (0) is the bit-identical path (spec 2026-07-28 flux-link).
    CHECK(spkyvcv::initParamDefault(spkyvcv::LINK_A) == doctest::Approx(0.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::LINK_B) == doctest::Approx(0.f));
}
