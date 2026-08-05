#include <doctest/doctest.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include "render/scenario.h"
#include "flow/flow.h"
#include "flow/terrain_code.h"
using namespace spky;
using namespace spky::flow;

static std::string repo_file(const char* relative) {
    std::string file = __FILE__;
    size_t slash = file.find_last_of("/\\");
    if (slash != std::string::npos) file.erase(slash);  // tests/
    slash = file.find_last_of("/\\");
    if (slash == std::string::npos) file.clear();
    else file.erase(slash + 1);                         // repository root
    return file + relative;
}

TEST_CASE("AAAB listening scenario: covers LOOP, GROW, and RENEW supercycles") {
    Scenario s;
    std::string err;
    const std::string path =
        repo_file("host/render/scenarios/demo_song_aaab.json");
    REQUIRE_MESSAGE(load_scenario(path, s, err), err);

    const double step_s = 60.0 / s.bpm;
    CHECK(s.duration_s >= 2.0 * 64.0 * step_s);

    bool form = false;
    bool song = false;
    bool step16 = false;
    bool loop = false;
    bool grow = false;
    bool renew = false;
    const auto inspect = [&](const Event& e) {
        if (e.action == "set_form" && e.part == PART_A && e.ivalue == 2)
            form = true;
        if (e.action == "set_song" && e.part == PART_A && e.ivalue == 0)
            song = true;
        if (e.action == "set_step" && e.part == PART_A &&
            e.flag && e.ivalue == 16)
            step16 = true;
        if (e.action == "set_variation" && e.part == PART_A) {
            loop  = loop  || e.value == 0.f;
            grow  = grow  || e.value > 0.f;
            renew = renew || e.value < 0.f;
        }
    };
    for (const Event& e : s.init_events) inspect(e);
    for (const Event& e : s.events) inspect(e);

    CHECK(form);
    CHECK(song);
    CHECK(step16);
    CHECK(loop);
    CHECK(grow);
    CHECK(renew);
}

TEST_CASE("SONG modes listening scenario covers all seven positions") {
    Scenario s;
    std::string err;
    const std::string path =
        repo_file("host/render/scenarios/demo_song_modes.json");
    REQUIRE_MESSAGE(load_scenario(path, s, err), err);
    CHECK(s.duration_s == 256.0);

    bool form = false;
    bool step16 = false;
    bool loop = false;
    int song_count[7] = {};
    const auto inspect = [&](const Event& e) {
        if (e.action == "set_form" && e.part == PART_A && e.ivalue == 2)
            form = true;
        if (e.action == "set_step" && e.part == PART_A &&
            e.flag && e.ivalue == 16)
            step16 = true;
        if (e.action == "set_variation" && e.part == PART_A &&
            e.value == 0.f)
            loop = true;
        if (e.action == "set_song" && e.part == PART_A &&
            e.ivalue >= 0 && e.ivalue < 7)
            ++song_count[e.ivalue];
    };
    for (const Event& e : s.init_events) inspect(e);
    for (const Event& e : s.events) inspect(e);

    CHECK(form);
    CHECK(step16);
    CHECK(loop);
    for (int mode = 0; mode < 7; ++mode)
        CHECK(song_count[mode] == 1);
}

TEST_CASE("scenario: parses init + timeline and sorts events by time") {
    const char* path = "test_scenario.json";
    {
        std::ofstream o(path);
        o << R"({
          "sample_rate": 48000,
          "bpm": 100,
          "duration_s": 5,
          "init": [
            {"action":"set_sync","ivalue":1},
            {"action":"set_rate","part":0,"value":0.5}
          ],
          "events": [
            {"t":3.0,"action":"set_probability","part":0,"value":0.2},
            {"t":1.0,"action":"set_step","part":0,"flag":true,"ivalue":16}
          ]
        })";
    }
    Scenario s;
    std::string err;
    REQUIRE(load_scenario(path, s, err));
    CHECK(s.sample_rate == 48000);
    CHECK(s.bpm == doctest::Approx(100.f));
    CHECK(s.duration_s == doctest::Approx(5.0));
    CHECK(s.init_events.size() == 2);
    REQUIRE(s.events.size() == 2);
    CHECK(s.events[0].time_s == doctest::Approx(1.0));   // sorted ascending
    CHECK(s.events[0].action == "set_step");
    CHECK(s.events[0].ivalue == 16);
    CHECK(s.events[1].action == "set_probability");

    Instrument inst;
    inst.init(48000.f);
    for (const auto& e : s.init_events) apply_event(inst, e);   // must not crash
    for (const auto& e : s.events)      apply_event(inst, e);
    std::remove(path);
}

TEST_CASE("scenario: quantizer actions reach the instrument") {
    Instrument inst;
    inst.init(48000.f);
    Event depth;  depth.action = "set_target_depth"; depth.part = 0;
    depth.slot = LANE_PITCH; depth.value = 0.f;
    apply_event(inst, depth);
    Event base;   base.action = "set_target_base"; base.part = 0;
    base.slot = LANE_PITCH; base.value = 0.5f;
    apply_event(inst, base);

    float l = 0.f, r = 0.f;
    for (int i = 0; i < 4000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.pitch_cv(0) == doctest::Approx(17.f / 36.f));   // boot dorian

    Event scale;  scale.action = "set_scale"; scale.svalue = "whole";
    apply_event(inst, scale);
    for (int i = 0; i < 4000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.pitch_cv(0) == doctest::Approx(18.f / 36.f));   // whole tone

    Event mode;   mode.action = "set_quant_mode"; mode.part = 0; mode.svalue = "free";
    apply_event(inst, mode);
    inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.pitch_cv(0) == doctest::Approx(0.5f));          // raw passthrough
}

TEST_CASE("scenario: fx actions reach the instrument") {
    Instrument inst;
    inst.init(48000.f);

    Event base;
    base.action = "set_fx_target_base";
    base.part = 0;
    base.slot = FXT_FLUX_TIME;
    base.value = 0.8f;
    apply_event(inst, base);
    CHECK(inst.fx_target_value(0, FXT_FLUX_TIME) == doctest::Approx(0.8f));

    Event on;      // must not crash even without FX memory
    on.action = "set_fx_on";
    on.part = 0;
    on.svalue = "flux";
    on.flag = true;
    apply_event(inst, on);

    Event mode;
    mode.action = "set_grit_mode";
    mode.part = 1;
    mode.svalue = "reduce";
    apply_event(inst, mode);

    Event dec;     // global reverb action: no part, null-safe
    dec.action = "set_reverb_decay";
    dec.value = 0.5f;
    apply_event(inst, dec);

    Event mix;     // global reverb action: no part, null-safe
    mix.action = "set_reverb_mix";
    mix.value = 0.3f;
    apply_event(inst, mix);

    Event dif;     // global reverb action: no part, null-safe
    dif.action = "set_reverb_diffusion";
    dif.value = 0.7f;
    apply_event(inst, dif);

    float l = 0.f, r = 0.f;
    inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(l == l);
}

TEST_CASE("FLUX tape listening scenario covers rate, feedback, and time gestures") {
    Scenario s;
    std::string err;
    const std::string path =
        repo_file("host/render/scenarios/flux_tape_echo.json");
    REQUIRE_MESSAGE(load_scenario(path, s, err), err);

    CHECK(s.sample_rate == 48000);
    CHECK(s.bpm == doctest::Approx(110.f));
    CHECK(s.duration_s == doctest::Approx(30.0));

    const auto has_init = [&](const char* action, int part, int slot,
                              float value, int ivalue, bool flag) {
        for (const Event& e : s.init_events) {
            if (e.action == action && e.part == part && e.slot == slot &&
                e.value == doctest::Approx(value) && e.ivalue == ivalue &&
                e.flag == flag)
                return true;
        }
        return false;
    };
    const auto has_event = [&](double time_s, const char* action, int slot,
                               float value, int ivalue, bool flag) {
        for (const Event& e : s.events) {
            if (e.time_s == doctest::Approx(time_s) && e.action == action &&
                e.part == PART_A && e.slot == slot &&
                e.value == doctest::Approx(value) && e.ivalue == ivalue &&
                e.flag == flag)
                return true;
        }
        return false;
    };

    CHECK(has_init("set_fx_on", PART_A, 0, 0.f, 0, true));
    CHECK(has_init("set_flux_mix", PART_A, 0, 0.8f, 0, false));
    CHECK(has_init("set_flux_rate", PART_A, 0, 0.f, 3, false));
    CHECK(has_init("set_fx_target_base", PART_A, FXT_FLUX_FB,
                   0.65f, 0, false));
    CHECK(has_init("set_fx_target_base", PART_A, FXT_FLUX_TIME,
                   0.5f, 0, false));

    CHECK(has_event(6.0, "set_flux_rate", 0, 0.f, 6, false));
    CHECK(has_event(10.0, "set_flux_rate", 0, 0.f, 0, false));
    CHECK(has_event(14.0, "set_fx_target_base", FXT_FLUX_FB,
                    0.9f, 0, false));
    CHECK(has_event(18.0, "set_fx_target_base", FXT_FLUX_FB,
                    0.55f, 0, false));
    CHECK(has_event(20.0, "set_fx_target_active", FXT_FLUX_TIME,
                    0.f, 0, true));
    CHECK(has_event(20.0, "set_fx_target_depth", FXT_FLUX_TIME,
                    0.35f, 0, false));
    CHECK(has_event(26.0, "set_fx_target_depth", FXT_FLUX_TIME,
                    0.f, 0, false));

    for (const Event& e : s.init_events) {
        CHECK(e.action != "set_drive");
        CHECK(e.action != "set_stages");
    }
    for (const Event& e : s.events) {
        CHECK(e.action != "set_drive");
        CHECK(e.action != "set_stages");
    }
}

TEST_CASE("render scenarios expose no retired FLUX actions") {
    const std::filesystem::path directory =
        repo_file("host/render/scenarios");
    size_t scenario_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        ++scenario_count;

        Scenario s;
        std::string err;
        const std::string path = entry.path().string();
        CAPTURE(path);
        REQUIRE_MESSAGE(load_scenario(path, s, err), err);

        const auto check = [&](const Event& e) {
            CHECK(e.action != "set_drive");
            CHECK(e.action != "set_stages");
        };
        for (const Event& e : s.init_events) check(e);
        for (const Event& e : s.events) check(e);
    }
    CHECK(scenario_count > 0);
}

// DENSE 0 leaves only the downbeat/anchor slot able to fire, so after the
// guaranteed first-sample fire (STEP entry: step -1 -> 0) the next natural
// note is a full cycle away. Settle past that single note's decay before
// checking silence, so the manual trigger is the only voice left.
TEST_CASE("scenario: M2 synth actions reach the instrument") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_voice_decay(0, 0.f);            // shortest decay: fast test
    inst.set_step(0, true, 8);
    inst.set_density(0, 0.f);                // anchor-only: next natural fire is a cycle away
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 10000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.active_voices(0) == 0);       // silent before the tap

    Event trig;                              // trigger_manual is observable
    trig.action = "trigger_manual";
    trig.part = 0;
    apply_event(inst, trig);
    inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.active_voices(0) == 1);

    Event eng;                               // set_engine is observable
    eng.action = "set_engine";
    eng.part = 0;
    eng.svalue = "test_tone";
    apply_event(inst, eng);
    for (int i = 0; i < 1000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.engine_id(0) == ENGINE_TEST_TONE);
    CHECK(inst.active_voices(0) == 0);

    // the five voice-parameter actions dispatch without crashing (their
    // audible effect is pinned by the engine/env unit tests)
    const char* voice_actions[] = { "set_voice_attack", "set_voice_decay",
                                    "set_voice_resonance", "set_voice_sub",
                                    "set_voice_detune" };
    for (const char* a : voice_actions) {
        Event e;
        e.action = a;
        e.part = 0;
        e.value = 0.5f;
        apply_event(inst, e);
    }
    inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(l == l);                           // not NaN
}

TEST_CASE("scenario: center actions dispatch to the instrument") {
    Instrument inst; inst.init(48000.f);

    Event ec; ec.action = "set_couple"; ec.value = 0.7f;
    apply_event(inst, ec);
    CHECK(inst.couple() == doctest::Approx(0.7f));     // couple is not smoothed

    Event ed; ed.action = "set_drift"; ed.value = 0.4f;
    apply_event(inst, ed);
    std::vector<float> l(1), r(1);
    for (int i = 0; i < 48000; ++i) inst.process(nullptr, nullptr, l.data(), r.data(), 1);
    CHECK(inst.drift() == doctest::Approx(0.4f).epsilon(0.05));   // smoothed toward target

    Event es; es.action = "spot";   apply_event(inst, es);   // must not crash
    Event et; et.action = "settle"; apply_event(inst, et);
    CHECK(true);
}

TEST_CASE("scenario: set_shuffle dispatch matches direct instruments across STEP pairs") {
    Instrument via_event;
    Instrument direct;
    via_event.init(48000.f);
    direct.init(48000.f);

    for (int p = 0; p < PART_COUNT; ++p) {
        via_event.set_rate(p, 1.f);       // 30 Hz: short, deterministic pairs
        direct.set_rate(p, 1.f);
        via_event.set_density(p, 1.f);
        direct.set_density(p, 1.f);
        via_event.set_step(p, true, 8);
        direct.set_step(p, true, 8);
    }

    Event e;
    e.action = "set_shuffle";
    e.value = 1.f;
    apply_event(via_event, e);
    direct.set_shuffle(1.f);

    float event_l = 0.f, event_r = 0.f;
    float direct_l = 0.f, direct_r = 0.f;
    // Twelve 8-step cycles covers many long-short pairs, not only the
    // initial edge where straight and shuffled grids agree.
    for (int sample = 0; sample < 19200; ++sample) {
        via_event.process(nullptr, nullptr, &event_l, &event_r, 1);
        direct.process(nullptr, nullptr, &direct_l, &direct_r, 1);
        for (int p = 0; p < PART_COUNT; ++p)
            REQUIRE(via_event.lane_fired(p, LANE_PITCH) ==
                    direct.lane_fired(p, LANE_PITCH));
    }
}

TEST_CASE("scenario: FORM SONG actions reach the instrument") {
    Instrument inst;
    inst.init(48000.f);

    Event form;
    form.action = "set_form";
    form.part = PART_A;
    form.ivalue = static_cast<int>(Principle::Ostinato);
    apply_event(inst, form);
    Event song;
    song.action = "set_song";
    song.part = PART_A;
    song.ivalue = static_cast<int>(SongMode::Mirror);
    apply_event(inst, song);
    inst.set_step(PART_A, true, 8);
    float l = 0.f, r = 0.f;
    inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.form(PART_A) == static_cast<int>(Principle::Ostinato));
    CHECK(inst.song(PART_A) == static_cast<int>(SongMode::Mirror));

    Instrument legacy;
    legacy.init(48000.f);
    Event principle;
    principle.action = "set_principle";
    principle.part = PART_A;
    principle.ivalue = static_cast<int>(Principle::CallResponse);
    apply_event(legacy, principle);
    legacy.set_step(PART_A, true, 8);
    legacy.process(nullptr, nullptr, &l, &r, 1);
    CHECK(legacy.form(PART_A) ==
          static_cast<int>(Principle::CallResponse));
}

TEST_CASE("scenario: set_comp and set_master_drive dispatch without throwing") {
    Instrument inst;
    inst.init(48000.f);
    Event e;
    e.action = "set_comp";
    e.part = 1;
    e.value = 0.8f;
    apply_event(inst, e);
    e.action = "set_master_drive";
    e.value = 0.5f;
    apply_event(inst, e);
    // No getters exist by design (matches set_reverb_*); reaching here alive
    // plus the Task 3/4 integration tests covers the wiring.
    CHECK(true);
}

TEST_CASE("scenario: set_color dispatches to the chord layer") {
    Instrument inst;
    inst.init(48000.f);
    Event e;
    e.action = "set_color";
    e.part = 0;
    e.value = 0.5f;
    apply_event(inst, e);          // dispatch smoke test: the voice-count check does NOT discriminate the
    // wiring (density-0 anchor fires alone reach 3 voices); the real evidence
    // is the chord_bloom render with-vs-without set_color (task-6 report)
    inst.set_density(0, 0.f);
    float outL[64], outR[64];
    for (int i = 0; i < 3000; ++i) inst.process(nullptr, nullptr, outL, outR, 64);
    CHECK(inst.active_voices(0) >= 3);
}

TEST_CASE("scenario: the sampler control actions reach the engine") {
    // Setup: the task-5 brief sketches an `Instrument::Config` rig that does
    // not exist in this repo. Follow the InstRig pattern already proven in
    // tests/test_sampler_part.cpp instead -- FxMem with injected per-part
    // sampler memory, then Instrument::init(sample_rate, mem).
    std::vector<float> echo[PART_COUNT][2];
    std::vector<SampleBuffer::Frame> sbuf[PART_COUNT];
    AmbientReverb reverb;
    FxMem mem;
    for (int p = 0; p < PART_COUNT; ++p) {
        for (int ch = 0; ch < 2; ++ch) {
            echo[p][ch].assign(Flux::kMaxSamples, 0.f);
            mem.echo[p][ch] = echo[p][ch].data();
        }
        sbuf[p].assign(48000, SampleBuffer::Frame{ 0.f, 0.f });
        mem.sampler_buf[p] = sbuf[p].data();
    }
    mem.sampler_frames = 48000;
    mem.reverb = &reverb;

    Instrument inst;
    inst.init(48000.f, mem);
    inst.set_engine(0, ENGINE_SAMPLER);
    // MOTION boots active (Part::_active) and adds its own swing on top of
    // the overlap knob (Part::_control_tick's omod, +/-kOverlapMod = 0.2).
    // Switch it off so the knob-only value is what lands on overlap_eff --
    // otherwise even a knob of exactly 0.0 rests wherever MOTION's LFO
    // happens to be, which is not what this test is checking.
    inst.set_target_active(0, LANE_MOTION, false);
    float in = 0.f, l = 0.f, r = 0.f;
    for (int i = 0; i < 960; ++i) inst.process(&in, &in, &l, &r, 1);   // click-free swap settle

    Event e;
    e.part = 0;

    e.action = "sampler_overlap"; e.value = 0.f;
    apply_event(inst, e);
    // Part stores the knob; the effective value reaches the engine on the
    // next control tick (Part::_control_tick, 96-sample raster), so drive
    // samples past that before reading it back. Default overlap_eff is 1.0
    // (Part::_overlap_eff init), so landing on 0.0 proves the action arrived.
    for (int i = 0; i < 200; ++i) inst.process(&in, &in, &l, &r, 1);
    CHECK(inst.sampler_overlap_eff(0) == doctest::Approx(0.f));

    // sampler_scan needs recorded content: SamplerEngine::_update_control
    // forces scan_pos() to 0 whenever rec_size() == 0, so proving the
    // playhead actually moves requires audio in the buffer first.
    in = 0.5f;
    inst.sampler_record(0, true);
    for (int i = 0; i < 24000; ++i) inst.process(&in, &in, &l, &r, 1);
    inst.sampler_record(0, false);
    in = 0.f;
    for (int i = 0; i < 960; ++i) inst.process(&in, &in, &l, &r, 1);
    REQUIRE(inst.sampler_fill(0) > 0.4f);

    e.action = "sampler_scan"; e.value = -1.f;
    apply_event(inst, e);
    for (int i = 0; i < 4800; ++i) inst.process(&in, &in, &l, &r, 1);
    // Reverse from a home position folds upward, so any motion at all
    // proves the action landed.
    const float pos_before_punch = inst.sampler_scan_pos(0);
    CHECK(pos_before_punch != 0.f);

    e.action = "sampler_punch";
    apply_event(inst, e);
    // punch() writes _scan_pos = 0.f immediately (not on the next control
    // tick), so the value it left behind was demonstrably non-zero above --
    // this isn't just reading a head that was already parked at home.
    CHECK(inst.sampler_scan_pos(0) == 0.f);
}

// One fresh instrument per scale: PITCH depth 0 and base fixed, so pitch_cv
// settles on whatever the scale allows nearest to `base`, with nothing
// carried over from a previous scale.
static float settled_pitch_semis(const char* scale_name, float base) {
    Instrument inst;
    inst.init(48000.f);
    Event depth; depth.action = "set_target_depth"; depth.part = 0;
    depth.slot = LANE_PITCH; depth.value = 0.f;
    apply_event(inst, depth);
    Event b;     b.action = "set_target_base"; b.part = 0;
    b.slot = LANE_PITCH; b.value = base;
    apply_event(inst, b);
    Event s;     s.action = "set_scale"; s.svalue = scale_name;
    apply_event(inst, s);

    float l = 0.f, r = 0.f;
    for (int i = 0; i < 4000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    return inst.pitch_cv(0) * 36.f;
}

TEST_CASE("scenario: the new scale names reach the instrument") {
    // 18 semis is degree 6. Hirajoshi (0 2 3 7 8) has neither 6 nor 5, so the
    // search walks up to 19; dorian would tie 17/19 and take 17.
    CHECK(settled_pitch_semis("hirajoshi", 0.5f) == doctest::Approx(19.f));

    // Below 16 semis, hijaz (0 1 4 5 7 8 10) snaps to 16 while dorian snaps
    // to 15. Using 15.8 also clears the quantizer's 0.30-semitone hysteresis
    // around the exact 15/17 midpoint.
    CHECK(settled_pitch_semis("hijaz",    15.8f / 36.f) == doctest::Approx(16.f));
    CHECK(settled_pitch_semis("nonsense", 15.8f / 36.f) == doctest::Approx(15.f));
}

TEST_CASE("scenario: wave engine spelling selects ENGINE_WAVE") {
    Instrument inst;
    inst.init(48000.f);
    Event e;
    e.action = "set_engine";
    e.part = 0;
    e.svalue = "wave";
    apply_event(inst, e);
    for (int i = 0; i < 500; ++i) {
        float in[1] {}, l[1], r[1];
        inst.process(in, in, l, r, 1);
    }
    CHECK(inst.engine_id(0) == ENGINE_WAVE);
}

TEST_CASE("scenario: body engine spelling selects ENGINE_BODY") {
    Instrument inst;
    inst.init(48000.f);
    Event e;
    e.action = "set_engine";
    e.part = 0;
    e.svalue = "body";
    apply_event(inst, e);
    for (int i = 0; i < 500; ++i) {
        float in[1] {}, l[1], r[1];
        inst.process(in, in, l, r, 1);
    }
    CHECK(inst.engine_id(0) == ENGINE_BODY);
}

// Task 9 (2026-07-31 bbd-part-engine): without a spelling here no render
// scenario can select the engine, and half of the definition of done is
// unmeasurable. The brief's Step 1 snippet invents a `parse_scenario_string`
// helper that does not exist in this file -- the real helper is
// `load_scenario(path, Scenario&, std::string&)`, which reads from a path, so
// this follows the file's own established idiom above ("scenario: parses
// init + timeline...") of writing the JSON to a temp file first.
TEST_CASE("scenario: the BBD engine is selectable by name") {
    const char* path = "test_scenario_bbd.json";
    {
        std::ofstream o(path);
        o << R"({"duration_s":0.1,"init":[
            {"action":"set_engine","part":0,"value":"bbd"}]})";
    }
    Scenario s;
    std::string err;
    REQUIRE_MESSAGE(load_scenario(path, s, err), err);
    REQUIRE(s.init_events.size() == 1);
    CHECK(s.init_events[0].svalue == "bbd");

    Instrument inst;
    inst.init(48000.f);
    apply_event(inst, s.init_events[0]);
    // Part::set_engine() is asynchronous: it fades out and swaps at process()'s
    // next idle point (part.cpp Part::set_engine), so engine_id() does not move
    // until enough samples run -- the same reason the "wave"/"body" spelling
    // tests just above loop process() before reading it back.
    for (int i = 0; i < 500; ++i) {
        float in[1] {}, l[1], r[1];
        inst.process(in, in, l, r, 1);
    }
    CHECK(inst.engine_id(0) == ENGINE_BBD);
    std::remove(path);
}

// Task 12: body_sympathetic.json needs the excitation bus source flags
// reachable from a scenario (task-12-brief-addendum.md §B -- no action for
// Instrument::set_excitation_sources existed before this task). Parity
// against a direct call, PLUS a divergence check against an instrument that
// never got the event, so a dispatch line that silently does nothing (the
// exact defect shape task-10 and task-8 already caught once each on this
// branch) cannot pass this test by accident -- see the mutation run in the
// task-12 report.
TEST_CASE("scenario: set_excitation_sources dispatches all three flags") {
    Instrument via_event, direct, untouched;
    for (Instrument* inst : { &via_event, &direct, &untouched }) {
        inst->init(48000.f);
        inst->set_engine(PART_A, ENGINE_SYNTH);
        inst->set_engine(PART_B, ENGINE_BODY);
        inst->set_voice_sub(PART_B, 1.f);
    }
    Event e;
    e.action = "set_excitation_sources";
    e.part   = PART_B;
    e.flag   = true;    // tape
    e.ivalue = 3;        // bit0 other_deck, bit1 audio_in -- all three on
    apply_event(via_event, e);
    direct.set_excitation_sources(PART_B, true, true, true);
    // untouched keeps the boot default (tape on, other_deck/audio_in off).

    float vl = 0.f, vr = 0.f, dl = 0.f, dr = 0.f, ul = 0.f, ur = 0.f;
    bool diverged_from_untouched = false;
    for (int i = 0; i < 48000; ++i) {
        via_event.process(nullptr, nullptr, &vl, &vr, 1);
        direct.process(nullptr, nullptr, &dl, &dr, 1);
        untouched.process(nullptr, nullptr, &ul, &ur, 1);
        REQUIRE(vl == dl);
        REQUIRE(vr == dr);
        if (vl != ul || vr != ur) diverged_from_untouched = true;
    }
    // Sanity: enabling the extra two sources actually changed something --
    // otherwise the parity check above would still pass with the dispatch
    // line missing entirely (both sides silently no-op'd the same way).
    CHECK(diverged_from_untouched);
}

// Task 12: body_bow.json's "CHOKE at the end" (task-12-brief-addendum.md §E)
// needs CHOKE reachable from a scenario too -- Instrument::set_choke had no
// action before this task (a second gap the addendum's file list didn't
// name; see the task-12 report). Same parity + divergence shape as the
// excitation test above, mirroring test_choke.cpp's own "+1 is the mirror"
// case so this is proven against known-good CHOKE behaviour, not just "some
// value changed."
TEST_CASE("scenario: set_choke dispatches the priority knob") {
    Instrument via_event, direct, untouched;
    for (Instrument* inst : { &via_event, &direct, &untouched }) {
        inst->init(48000.f);
        for (int p = 0; p < PART_COUNT; ++p) {
            inst->set_rate(p, p == PART_A ? 0.8f : 0.9f);
            inst->set_density(p, 1.f);
            inst->set_range(p, 1.f);
        }
    }
    Event e;
    e.action = "set_choke";
    e.value  = 1.f;    // "+1 is the mirror: A yields to B" (test_choke.cpp)
    apply_event(via_event, e);
    direct.set_choke(1.f);
    // untouched keeps choke at the boot default (0 -- no priority effect).

    float vl = 0.f, vr = 0.f, dl = 0.f, dr = 0.f, ul = 0.f, ur = 0.f;
    bool diverged_from_untouched = false;
    for (int i = 0; i < 48000; ++i) {
        via_event.process(nullptr, nullptr, &vl, &vr, 1);
        direct.process(nullptr, nullptr, &dl, &dr, 1);
        untouched.process(nullptr, nullptr, &ul, &ur, 1);
        REQUIRE(vl == dl);
        REQUIRE(vr == dr);
        if (vl != ul || vr != ur) diverged_from_untouched = true;
    }
    CHECK(diverged_from_untouched);
}

// Task 9 (2026-08-05 flow-engine-layer): the render host learns to drive the
// Flow layer from a scenario. "value":"F1-..." is a JSON STRING, so
// parse_event's existing string/float sniff (scenario.cpp parse_event) already
// routes it into Event::svalue -- the same convention set_engine's "value":
// "body" already uses. No slip to work around; see the task-9 report for the
// reasoning.
//
// eff_macro(M_MOTION) is used as the observable because MOTION is never
// weathered (flow.cpp Flow::weather_of skips M_MOTION on purpose), so
// eff_macro(M_MOTION) after a tick is EXACTLY the knob value with no
// terrain-dependent wobble to account for -- a strong, not-vacuously-passable
// assertion.
TEST_CASE("scenario: flow actions parse and drive the flow layer") {
    const char* path = "test_scenario_flow.json";
    {
        std::ofstream o(path);
        o << R"({
          "duration_s": 0.1,
          "init": [
            {"action":"flow_wake","value":"F1-0000002A-010203040506"},
            {"action":"flow_macro","slot":0,"value":0.75}
          ]
        })";
    }
    Scenario s;
    std::string err;
    REQUIRE_MESSAGE(load_scenario(path, s, err), err);
    CHECK(s.has_flow);
    REQUIRE(s.init_events.size() == 2);
    CHECK(s.init_events[0].action == "flow_wake");
    CHECK(s.init_events[0].svalue == "F1-0000002A-010203040506");
    CHECK(s.init_events[1].action == "flow_macro");
    CHECK(s.init_events[1].slot == 0);
    CHECK(s.init_events[1].value == doctest::Approx(0.75f));

    TerrainState expected;
    REQUIRE(decode_code(s.init_events[0].svalue.c_str(), expected));

    Instrument inst;
    inst.init(48000.f);
    Flow fl;
    fl.init(&inst, 500.f);

    for (const auto& e : s.init_events) apply_event(inst, &fl, e);
    fl.tick();   // the render loop's next control-block tick; eff_macro only
                 // updates on wake()/tick(), not on set_macro() itself.

    CHECK(fl.state().master == expected.master);
    for (int m = 0; m < MACRO_COUNT; ++m)
        CHECK(fl.state().reroll[m] == expected.reroll[m]);
    CHECK(fl.eff_macro(M_MOTION) == doctest::Approx(0.75f));

    std::remove(path);
}

// A malformed/missing code must not silently fall back to a default terrain
// (task-9 resolution #5) -- decode_code() itself is already strict (Task 5),
// this proves the scenario dispatch path honours that refusal instead of
// papering over it.
TEST_CASE("scenario: flow_wake with a malformed code refuses to wake") {
    Instrument inst;
    inst.init(48000.f);
    Flow fl;
    fl.init(&inst, 500.f);

    Event bad;
    bad.action = "flow_wake";
    bad.svalue = "not-a-terrain-code";
    apply_event(inst, &fl, bad);

    // wake() was never called: state() sits at the boot-default TerrainState.
    TerrainState boot;
    CHECK(fl.state().master == boot.master);
    for (int m = 0; m < MACRO_COUNT; ++m)
        CHECK(fl.state().reroll[m] == boot.reroll[m]);
}

// Flow actions with a null Flow* must no-op rather than crash (task-9
// brief), and must not disturb the Instrument either. Also proves the new
// three-argument overload still dispatches ordinary (non-"flow_") actions
// through to the Instrument, i.e. it is a superset of the old overload, not
// a parallel path that silently drops legacy actions.
TEST_CASE("scenario: flow actions no-op with a null Flow*, legacy actions still dispatch") {
    Instrument inst;
    inst.init(48000.f);

    Event fm;
    fm.action = "flow_macro";
    fm.slot = 0;
    fm.value = 0.5f;
    apply_event(inst, nullptr, fm);   // must not crash

    Event couple;
    couple.action = "set_couple";
    couple.value = 0.7f;
    apply_event(inst, nullptr, couple);
    CHECK(inst.couple() == doctest::Approx(0.7f));
}

// Task 10: scenario.cpp's flow_new_partial/flow_undo/flow_lock dispatch arms
// had zero test coverage (a Task 9 gap the task-10 brief asked to close).
// Same idiom as "scenario: flow actions parse and drive the flow layer"
// above -- dispatch through apply_event(inst, &fl, e) and assert real STATE
// effects (reroll counters, blend_phase(), can_undo(), locked()), not just
// that nothing crashed.
TEST_CASE("scenario: flow_new_partial, flow_undo and flow_lock dispatch and change state") {
    Instrument inst;
    inst.init(48000.f);
    Flow fl;
    fl.init(&inst, 500.f);
    TerrainState st;
    st.master = 0x1234u;
    fl.wake(st);
    const uint32_t master0 = fl.state().master;
    REQUIRE(fl.blend_phase() == doctest::Approx(1.f));
    REQUIRE_FALSE(fl.can_undo());   // wake() itself never arms undo

    // flow_new_partial: bumps the marked macro's reroll counter and starts
    // a blend (flow.cpp Flow::new_partial -> begin_blend).
    Event partial;
    partial.action = "flow_new_partial";
    partial.ivalue = int(1u << M_BRIGHT);
    apply_event(inst, &fl, partial);
    CHECK(fl.state().reroll[M_BRIGHT] == 1);
    CHECK(fl.state().master == master0);   // a partial reroll keeps the master
    CHECK(fl.blend_phase() == doctest::Approx(0.f));
    CHECK(fl.can_undo());

    // Settle the blend before undo -- undo() itself works mid-blend too
    // (flow.cpp begin_blend handles a re-press), but this case's assertions
    // are about STATE, not blend timing, so settle first to keep them
    // independent of it.
    for (int i = 0; i < 4000; ++i) fl.tick();

    // flow_undo: the one slot swaps state() back to what it was before the
    // partial reroll, and starts its own blend back.
    Event undo_e;
    undo_e.action = "flow_undo";
    apply_event(inst, &fl, undo_e);
    CHECK(fl.state().reroll[M_BRIGHT] == 0);
    CHECK(fl.state().master == master0);
    CHECK(fl.blend_phase() == doctest::Approx(0.f));
    for (int i = 0; i < 4000; ++i) fl.tick();

    // flow_lock(true): further NEW-family gestures refuse -- state() does
    // not move -- until unlocked (Flow::locked() itself never blocks
    // set_lock, per flow.h).
    Event lock_on;
    lock_on.action = "flow_lock";
    lock_on.flag = true;
    apply_event(inst, &fl, lock_on);
    CHECK(fl.locked());
    const uint32_t reroll_before_locked = fl.state().reroll[M_BRIGHT];
    apply_event(inst, &fl, partial);   // flow_new_partial again: refused while locked
    CHECK(fl.state().reroll[M_BRIGHT] == reroll_before_locked);
    CHECK(fl.blend_phase() == doctest::Approx(1.f));   // no blend started

    // flow_lock(false): unlocks, and the very same gesture now works again.
    Event lock_off;
    lock_off.action = "flow_lock";
    lock_off.flag = false;
    apply_event(inst, &fl, lock_off);
    CHECK_FALSE(fl.locked());
    apply_event(inst, &fl, partial);
    CHECK(fl.state().reroll[M_BRIGHT] == reroll_before_locked + 1);
}
