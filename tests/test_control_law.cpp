#include <doctest/doctest.h>
#include <cmath>
#include "control/control_law.h"
#include "control_recorder.h"
#include "vcv/src/sampler_ui.hpp"

using Law = control::ControlLawT<Rec>;
using namespace ffctl;

namespace {
struct Rig {
    Law law; Rec inst; float k[NUM_PARAMS];
    control::Options opt;
    Rig() { for (int i = 0; i < NUM_PARAMS; ++i) k[i] = kInitParamDefaults[i];
            law.on_restore(); }
    control::Events tick() { inst.calls.clear(); return law.tick(k, opt, inst); }
};
}

TEST_CASE("law: LVL/COMP split -- gain below 0.6, compressor above") {
    Rig r;
    r.k[COMP_A] = 0.3f; r.tick();
    CHECK(r.inst.last("set_part_level", 0) == doctest::Approx(0.5f));
    CHECK(r.inst.last("set_comp", 0) == 0.f);
    r.k[COMP_A] = 1.0f; r.tick();
    CHECK(r.inst.last("set_part_level", 0) == 1.f);
    CHECK(r.inst.last("set_comp", 0) == doctest::Approx(0.7f));
    r.k[COMP_A] = 0.8f; r.tick();
    CHECK(r.inst.last("set_comp", 0) == doctest::Approx(0.7f * std::pow(0.5f, 0.6f)));
}

TEST_CASE("law: GRIT dead zone, sign picks the mode; FLUX on above 1e-4") {
    Rig r;
    r.k[GRIT_B] = 0.02f; r.tick();
    CHECK(r.inst.last("set_grit_mix", 1) == 0.f);
    CHECK(r.inst.last("set_fx_on/" + std::to_string(int(spky::FxBlock::Grit)), 1) == 0.f);
    r.k[GRIT_B] = -0.515f; r.tick();
    CHECK(r.inst.last("set_grit_mix", 1) == doctest::Approx(0.5f));
    CHECK(r.inst.last("set_grit_mode", 1) == float(int(spky::GritMode::Reduce)));
    r.k[FLUX_A] = 5e-5f; r.tick();
    CHECK(r.inst.last("set_fx_on/" + std::to_string(int(spky::FxBlock::Flux)), 0) == 0.f);
    r.k[FLUX_A] = 0.2f; r.tick();
    CHECK(r.inst.last("set_fx_on/" + std::to_string(int(spky::FxBlock::Flux)), 0) == 1.f);
}

TEST_CASE("law: ENGINE remap, test tone only on the sampler slot") {
    Rig r;
    const float want[6] = {float(spky::ENGINE_SYNTH), float(spky::ENGINE_SAMPLER),
                           float(spky::ENGINE_WAVE), float(spky::ENGINE_BODY),
                           float(spky::ENGINE_BBD), float(spky::ENGINE_FEED)};
    for (int e = 0; e < 6; ++e) {
        r.k[ENGINE_A] = float(e); r.tick();
        CHECK(r.inst.last("set_engine", 0) == want[e]);
    }
    r.opt.deck[0].test_tone = true;
    r.k[ENGINE_A] = 1.f; r.tick();
    CHECK(r.inst.last("set_engine", 0) == float(spky::ENGINE_TEST_TONE));
    r.k[ENGINE_A] = 0.f; r.tick();
    CHECK(r.inst.last("set_engine", 0) == float(spky::ENGINE_SYNTH));
}

TEST_CASE("law: DETUNE squared, skipped on FEED; SPREAD raw on FEED") {
    // The init patch boots deck A on FEED (ENGINE_A = 5) and deck B on WAVE;
    // Rec itself starts both on SYNTH. DETUNE's branch reads engine_id()
    // BEFORE this tick's set_engine, so each tick sees the engine the
    // previous tick set.
    Rig r;
    r.k[ENGINE_A] = 0.f; r.k[ENGINE_B] = 0.f; r.tick();   // both decks now SYNTH
    // 0.6, not 0.5: raw (0.6), squared (0.36) and the LANE_SIZE base a deck
    // that is neither SAMPLER nor FEED gets (0.5) must all differ, or the
    // SPREAD check below cannot tell the FEED branch from the else branch.
    r.k[DETUNE_A] = 0.6f; r.tick();
    CHECK(r.inst.last("set_voice_detune", 0) == doctest::Approx(0.36f));
    r.k[ENGINE_A] = 5.f; r.tick();                        // A becomes FEED
    r.tick();                                             // A reads FEED now
    CHECK(r.inst.count("set_voice_detune") == 1);         // deck B only
    CHECK(r.inst.last("set_target_base/" + std::to_string(int(spky::LANE_SIZE)), 0)
          == doctest::Approx(0.6f));
}

TEST_CASE("law: COUPLE zone split and DRIFT settle edge") {
    Rig r;
    r.k[COUPLE] = 0.25f; r.tick();
    CHECK(r.inst.last("set_sync") == 0.f);
    CHECK(r.inst.last("set_couple") == doctest::Approx(0.5f));
    r.k[COUPLE] = 0.75f; r.tick();
    CHECK(r.inst.last("set_sync") == 1.f);
    CHECK(r.inst.last("set_couple") == doctest::Approx(0.5f));
    r.k[DRIFT] = 0.5f; r.tick();
    CHECK(r.inst.count("settle") == 0);
    r.k[DRIFT] = 0.01f; r.tick();
    CHECK(r.inst.count("settle") == 1);
    CHECK(r.inst.last("set_drift") == 0.f);
    r.tick();
    CHECK(r.inst.count("settle") == 0);              // parked: no re-fire
}

TEST_CASE("law: TEMPO from the knob, CLOCK override only inside 20..400") {
    Rig r;
    r.k[TEMPO] = 0.5f; r.tick();
    CHECK(r.inst.last("set_tempo_bpm") == doctest::Approx(140.f));
    r.opt.measured_bpm = 90.f; r.tick();
    CHECK(r.inst.last("set_tempo_bpm") == doctest::Approx(90.f));
    r.opt.measured_bpm = 500.f; r.tick();
    CHECK(r.inst.last("set_tempo_bpm") == doctest::Approx(140.f));
    r.opt.measured_bpm = 10.f; r.tick();
    CHECK(r.inst.last("set_tempo_bpm") == doctest::Approx(140.f));
}

TEST_CASE("law: mv() -- lane, mirror and centre terms, clamped to the range") {
    Rig r;
    for (int i = MODBTN + 1; i < NUM_PARAMS; ++i) r.k[i] = 0.f;   // all depths at noon
    r.k[MOD_A] = 1.f; r.k[MOD_B] = 1.f;
    r.k[RATE_A] = 0.5f; r.k[MODD_RATE_A] = 1.f;
    for (int s = 0; s < spky::LANE_COUNT; ++s) r.inst.lane[0][s] = 1.f;
    r.tick();
    CHECK(r.inst.last("set_rate", 0) > 0.5f);         // lane pushed it up
    CHECK(r.inst.last("set_rate", 0) <= 1.f);         // and the range holds it
    r.k[RATE_A] = 1.f; r.tick();
    CHECK(r.inst.last("set_rate", 0) == 1.f);         // clamp at hi
    for (int s = 0; s < spky::LANE_COUNT; ++s) r.inst.lane[0][s] = -1.f;
    r.k[RATE_A] = 0.3f; r.tick();
    CHECK(r.inst.last("set_rate", 0) == 0.f);         // clamp at lo (0.3 - 1)
    for (int s = 0; s < spky::LANE_COUNT; ++s) r.inst.lane[0][s] = 1.f;
    // PAN_B mirrors deck A's lane, negated
    r.k[PAN_B] = 0.f; r.k[MODD_PAN_B] = 1.f; r.tick();
    CHECK(r.inst.last("set_pan", 1) < 0.f);
    // a centre target mixes both decks: deck A's lane at 1, deck B's at 0,
    // both masters up -> half of deck A's term
    r.k[REV_SIZE] = 0.2f; r.k[MODD_REV_SIZE] = 1.f; r.tick();
    CHECK(r.inst.last("set_reverb_size") == doctest::Approx(0.7f));
    // a depth at noon returns the knob exactly
    r.k[MODD_RATE_A] = 0.f; r.k[RATE_A] = 0.3f; r.tick();
    CHECK(r.inst.last("set_rate", 0) == 0.3f);
}

TEST_CASE("law: engine-backed depths go to the Part, host ones never do") {
    Rig r;
    r.tick();
    int tdepth = 0, fxdepth = 0;
    for (auto& c : r.inst.calls) {
        tdepth  += c.fn.rfind("set_target_depth/", 0) == 0;
        fxdepth += c.fn.rfind("set_fx_target_depth/", 0) == 0;
    }
    CHECK(tdepth == 6);     // SOURCE, DEPTH, FILT x 2 decks
    CHECK(fxdepth == 6);    // FLUX, FLUXFB, REV_MIX x 2 decks
}

TEST_CASE("law: BBD edge fires once per genuine entry, never on restore") {
    Rig r;
    r.inst.eng[0] = spky::ENGINE_BBD; r.k[ENGINE_A] = 4.f;
    CHECK_FALSE(r.tick().bbd_edge[0]);                // restore seeded it
    r.inst.eng[0] = spky::ENGINE_SYNTH; r.k[ENGINE_A] = 0.f; r.tick();
    r.inst.eng[0] = spky::ENGINE_BBD; r.k[ENGINE_A] = 4.f;
    CHECK(r.tick().bbd_edge[0]);
    CHECK_FALSE(r.tick().bbd_edge[0]);
}

TEST_CASE("law: SONG rung change re-rolls, the first tick after restore does not") {
    Rig r;
    // A non-init rung on the very first tick: the seed must adopt it. (At the
    // init value 0 a seed that failed to adopt would be silent too, because
    // SongRungState::rung also starts at 0.) DRIFT's init 0 sits in the
    // settle zone, so the same first tick also pins DRIFT's seed.
    r.k[SONG_B] = 10.f; r.tick();
    CHECK(r.inst.count("new_phrase") == 0);
    CHECK(r.inst.count("settle") == 0);
    r.k[SONG_B] = 2.f; r.tick();
    CHECK(r.inst.last("new_phrase", 1) == 1.f);
}

TEST_CASE("law: on_restore re-arms song and drift too") {
    Rig r;
    r.k[DRIFT] = 0.5f; r.tick();                    // DRIFT out of the zone
    r.k[SONG_B] = 10.f; r.k[DRIFT] = 0.01f;         // both change across the restore
    r.law.on_restore(); r.tick();
    CHECK(r.inst.count("new_phrase") == 0);
    CHECK(r.inst.count("settle") == 0);
}

TEST_CASE("law: on_reset re-arms song and drift but not the BBD edge") {
    Rig r;
    // song and drift: a change that lands across on_reset() is adopted as the
    // new baseline, not fired as a turn
    r.k[DRIFT] = 0.5f; r.tick();
    r.k[DRIFT] = 0.01f; r.k[SONG_B] = 10.f;
    r.law.on_reset(); r.tick();
    CHECK(r.inst.count("settle") == 0);
    CHECK(r.inst.count("new_phrase") == 0);
    r.inst.eng[0] = spky::ENGINE_SYNTH; r.k[ENGINE_A] = 0.f; r.tick();
    r.law.on_reset();
    r.inst.eng[0] = spky::ENGINE_BBD; r.k[ENGINE_A] = 4.f;
    CHECK(r.tick().bbd_edge[0]);                      // still armed: fires
    r.inst.eng[0] = spky::ENGINE_SYNTH; r.k[ENGINE_A] = 0.f; r.tick();
    r.law.on_restore();
    r.inst.eng[0] = spky::ENGINE_BBD; r.k[ENGINE_A] = 4.f;
    CHECK_FALSE(r.tick().bbd_edge[0]);                // restore: adopts
}

TEST_CASE("law: REC starts only on a sampler deck and reports it once") {
    Rig r;
    r.k[REC_A] = 1.f; r.tick();
    CHECK(r.inst.count("sampler_record") == 0);       // synth deck: inert
    r.inst.eng[0] = spky::ENGINE_SAMPLER; r.k[ENGINE_A] = 1.f;
    CHECK(r.tick().rec_started[0]);
    CHECK_FALSE(r.tick().rec_started[0]);
}

TEST_CASE("law: autoload hook sits between excitation and sampler options") {
    struct H { int at = -1; int eng = -1; Rec* rec;
               void after_engine(int p, int e, Rec&) {
                   if (p == 0) { at = int(rec->calls.size()); eng = e; } } };
    control::ControlLawT<Rec, H> law; Rec inst; float k[NUM_PARAMS];
    for (int i = 0; i < NUM_PARAMS; ++i) k[i] = kInitParamDefaults[i];
    // Deck A on SAMPLER with REC up, so sampler_record logs and the whole
    // chain set_engine -> excitation -> hook -> sampler options -> REC is
    // visible in one tick.
    k[ENGINE_A] = 1.f; k[REC_A] = 1.f;
    law.on_restore();
    H h{-1, -1, &inst}; control::Options opt;
    law.tick(k, opt, inst, h);
    CHECK(inst.index("set_engine", 0) >= 0);
    CHECK(inst.index("set_engine", 0) < inst.index("set_excitation_sources", 0));
    CHECK(h.at == inst.index("set_excitation_sources", 0) + 1);
    CHECK(h.at == inst.index("sampler_speed_mode", 0));
    CHECK(inst.index("sampler_feedback", 0) >= 0);
    CHECK(inst.index("sampler_feedback", 0) < inst.index("sampler_record", 0));
    // the hook gets the ENGINE knob's slot (1 = Sampler), not the EngineId
    CHECK(h.eng == 1);
    CHECK(int(spky::ENGINE_SAMPLER) != 1);
}

TEST_CASE("law: deck B's appended ids land on part 1") {
    Rig r;
    r.k[FILT_B] = -0.4f; r.k[COLOR_B] = 0.6f; r.k[LINK_B] = 0.7f;
    r.k[FLUXRATE_B] = 3.f; r.k[REV_MIX_B] = 0.45f; r.k[DEPTH_B] = 0.2f;
    r.tick();
    CHECK(r.inst.last("set_voice_filt", 1) == doctest::Approx(-0.4f));
    CHECK(r.inst.last("set_color", 1) == doctest::Approx(0.6f));
    CHECK(r.inst.last("set_link", 1) == doctest::Approx(0.7f));
    CHECK(r.inst.last("set_flux_rate", 1) == 3.f);
    CHECK(r.inst.last("set_reverb_mix", 1) == doctest::Approx(0.45f));
    CHECK(r.inst.last("set_target_base/" + std::to_string(int(spky::LANE_MOTION)), 1)
          == doctest::Approx(0.2f));
    CHECK(r.inst.last("set_voice_filt", 0) != doctest::Approx(-0.4f));
}

TEST_CASE("law: deck B's FLUXFB, STAGES and REC land on part 1, not part 0") {
    const std::string fb = "set_fx_target_base/" + std::to_string(int(spky::FXT_FLUX_FB));
    const std::string pitch = "set_target_base/" + std::to_string(int(spky::LANE_PITCH));
    {   // FLUXFB: init A 0.427, B 0.791
        Rig r;
        r.k[FLUXFB_B] = 0.3f; r.tick();
        CHECK(r.inst.last(fb, 1) == doctest::Approx(0.3f));
        CHECK(r.inst.last(fb, 0) == doctest::Approx(kInitParamDefaults[FLUXFB_A]));
    }
    {   // STAGES is the LANE_PITCH base on a BBD deck; init A 1.0, B 0.0
        Rig r;
        r.k[ENGINE_A] = 4.f; r.k[ENGINE_B] = 4.f; r.k[STAGES_B] = 0.25f; r.tick();
        CHECK(r.inst.last(pitch, 1) == doctest::Approx(0.25f));
        CHECK(r.inst.last(pitch, 0) == doctest::Approx(1.f));
    }
    {   // REC: both decks SAMPLER, only deck B's latch up
        Rig r;
        r.k[ENGINE_A] = 1.f; r.k[ENGINE_B] = 1.f; r.k[REC_A] = 0.f; r.k[REC_B] = 1.f;
        const control::Events ev = r.tick();
        CHECK(r.inst.last("sampler_record", 1) == 1.f);
        CHECK(r.inst.index("sampler_record", 0) == -1);
        CHECK(ev.rec_started[1]);
        CHECK_FALSE(ev.rec_started[0]);
    }
}

// Spec 3.2 / 7.1: DeckOptions' defaults are pinned against the VCV host's own
// per-deck state, so the two cannot drift apart. sampler_ui.hpp is Rack-free.
TEST_CASE("law: DeckOptions{} is a fresh VCV deck") {
    control::DeckOptions d;
    const spkyvcv::SamplerPartState s;
    CHECK(d.tape_idx == s.tapeIdx);
    CHECK(d.reverse == s.reverse);
    CHECK(d.feedback == s.feedback);
    CHECK(d.test_tone == s.testTone);
    CHECK(d.excite_tape == s.exciteTape);
    CHECK(d.excite_other_deck == s.exciteOtherDeck);
    CHECK(d.excite_audio_in == s.exciteAudioIn);
}
