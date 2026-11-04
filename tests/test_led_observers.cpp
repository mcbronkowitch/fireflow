#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "instrument.h"
#include "fx/flux.h"
#include "../bench/audition/init_patch.h"

// The const observers the LED law reads (spec 2026-10-18 rev-a-p6b2 §4.2).
// That they do not change the audio is guarded by the render-hash gates
// (tests/check_render_hash.cmake), not here: a const read cannot change output,
// so a test saying so could never go red.

namespace {

struct ObsTape {
    std::vector<float> echo[spky::PART_COUNT][2];
    ObsTape() {
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

void run_silence(spky::Instrument& inst, int samples) {
    float l = 0.f, r = 0.f;
    for (int i = 0; i < samples; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
}

}  // namespace

TEST_CASE("observers: a playing deck's level sits above a silent one's") {
    spky::Instrument inst;
    inst.init(48000.f);
    audition::apply_init_patch(inst);

    run_silence(inst, 48000);
    CHECK(inst.deck_out_level(spky::PART_A) > 0.01f);

    inst.set_part_level(spky::PART_A, 0.f);
    run_silence(inst, 24000);
    CHECK(inst.deck_out_level(spky::PART_A) < 1e-4f);
}

TEST_CASE("observers: the input peak follows the input") {
    spky::Instrument inst;
    inst.init(48000.f);

    float l = 0.f, r = 0.f;
    for (int i = 0; i < 9600; ++i) {                       // 0.2 s
        const float x = 0.5f * std::sin(6.2831853f * 440.f * static_cast<float>(i) / 48000.f);
        inst.process(&x, &x, &l, &r, 1);
    }
    CHECK(inst.input_peak() >= 0.45f);
    CHECK(inst.input_peak() <= 0.55f);

    run_silence(inst, 24000);                              // 0.5 s
    CHECK(inst.input_peak() < 1e-4f);
}

TEST_CASE("observers: the FLUX phase wraps once per repeat period") {
    ObsTape tape;
    spky::Instrument inst;
    inst.init(48000.f, tape.mem());
    inst.set_fx_on(spky::PART_A, spky::FxBlock::Flux, true);
    inst.set_flux_mix(spky::PART_A, 1.f);
    inst.set_flux_rate(spky::PART_A, 1);

    run_silence(inst, 4800);   // let the switch fade in and the delay time settle
    REQUIRE(inst.flux_engaged(spky::PART_A));
    const float delay = inst.flux_delay_target_for_test(spky::PART_A);
    REQUIRE(delay > 0.1f);
    REQUIRE(delay < 5.f);

    int wraps = 0;
    float prev = inst.flux_repeat_phase(spky::PART_A);
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 480000; ++i) {                     // 10 s
        inst.process(nullptr, nullptr, &l, &r, 1);
        const float ph = inst.flux_repeat_phase(spky::PART_A);
        CHECK(ph >= 0.f);
        CHECK(ph < 1.f);
        if (ph < prev) ++wraps;
        prev = ph;
    }
    const float want = 10.f / delay;
    CHECK(std::fabs(static_cast<float>(wraps) - want) <= 1.f);
}

TEST_CASE("observers: the FLUX phase wraps on the TEMPO lamp's beat, wherever FLUX came on") {
    // The FTIME lamp used to count its own period from the moment FLUX was
    // switched on, so it sat a fixed, arbitrary offset off the TEMPO lamp
    // (Bastian, 2026-11-04). Every beat wrap must now be a FLUX wrap on the
    // same sample. FLUX comes on at an odd time (0.37 s) on purpose, and
    // PACE x1/4 checks the raw-tempo echo against the paced beat.
    struct C { float pace_knob; int rate_slice; };
    for (C c : {C{0.5f, 3}, C{0.5f, 11}, C{0.3f, 3}, C{0.3f, 0}}) {
        CAPTURE(c.pace_knob); CAPTURE(c.rate_slice);
        ObsTape tape;
        spky::Instrument inst;
        inst.init(48000.f, tape.mem());
        inst.set_tempo_bpm(120.f);
        inst.set_pace(c.pace_knob);
        inst.set_flux_rate(spky::PART_A, c.rate_slice);
        inst.set_flux_mix(spky::PART_A, 1.f);
        run_silence(inst, 17760);                          // 0.37 s
        inst.set_fx_on(spky::PART_A, spky::FxBlock::Flux, true);
        run_silence(inst, 480);
        REQUIRE(inst.flux_engaged(spky::PART_A));

        int beats = 0, missed = 0;
        float bp = inst.beat_phase();
        float fp = inst.flux_repeat_phase(spky::PART_A);
        float l = 0.f, r = 0.f;
        for (int i = 0; i < 480000; ++i) {                 // 10 s
            inst.process(nullptr, nullptr, &l, &r, 1);
            const float b = inst.beat_phase();
            const float f = inst.flux_repeat_phase(spky::PART_A);
            if (b < bp) { ++beats; if (!(f < fp)) ++missed; }
            bp = b; fp = f;
        }
        REQUIRE(beats >= 4);
        CHECK(missed == 0);
    }
}

TEST_CASE("observers: the FLUX phase stays at rest and engaged() is false when FLUX is off") {
    ObsTape tape;
    spky::Instrument inst;
    inst.init(48000.f, tape.mem());
    run_silence(inst, 9600);
    CHECK_FALSE(inst.flux_engaged(spky::PART_A));
    CHECK(inst.flux_repeat_phase(spky::PART_A) == 0.f);
}

TEST_CASE("observers: the engine epoch moves when the new engine sounds, not at the call") {
    spky::Instrument inst;
    inst.init(48000.f);
    run_silence(inst, 480);

    const uint32_t e0 = inst.engine_epoch(spky::PART_A);
    const uint32_t other = inst.engine_epoch(spky::PART_B);
    const spky::EngineId target = inst.engine_id(spky::PART_A) == spky::ENGINE_WAVE
                                      ? spky::ENGINE_SYNTH : spky::ENGINE_WAVE;
    inst.set_engine(spky::PART_A, target);
    CHECK(inst.engine_epoch(spky::PART_A) == e0);          // armed, not yet sounding

    run_silence(inst, 16);                                  // inside the fade-out
    CHECK(inst.engine_epoch(spky::PART_A) == e0);
    CHECK(inst.engine_id(spky::PART_A) != target);

    run_silence(inst, 4800);                                // the swap has happened
    CHECK(inst.engine_id(spky::PART_A) == target);
    CHECK(inst.engine_epoch(spky::PART_A) == e0 + 1);
    CHECK(inst.engine_epoch(spky::PART_B) == other);        // the other deck did not move
}

TEST_CASE("observers: the scale epoch ignores a repeated scale") {
    spky::Instrument inst;
    inst.init(48000.f);

    const uint32_t e0 = inst.scale_epoch();
    inst.set_scale(3);
    inst.set_scale(3);
    CHECK(inst.scale_epoch() == e0 + 1);
    inst.set_scale(4);
    CHECK(inst.scale_epoch() == e0 + 2);
}
