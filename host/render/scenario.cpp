#include "render/scenario.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <exception>
#include "nlohmann/json.hpp"
#include "shared/wav_reader.h"
#include "flow/flow.h"
#include "flow/terrain_code.h"

using namespace spky;
using json = nlohmann::json;

static Event parse_event(const json& j, bool timed) {
    Event e;
    if (timed) e.time_s = j.value("t", 0.0);
    e.action = j.value("action", std::string());
    e.part   = j.value("part", 0);
    e.slot   = j.value("slot", 0);
    e.flag   = j.value("flag", false);
    e.ivalue = j.value("ivalue", 0);
    if (j.contains("value")) {
        if (j["value"].is_string()) e.svalue = j["value"].get<std::string>();
        else                        e.value  = j["value"].get<float>();
    }

    // Fix 6: part/slot come straight from untrusted JSON and Instrument
    // indexes fixed-size arrays with them (PART_COUNT parts; LANE_COUNT/
    // FXT_COUNT slots) with no bounds checking of its own -- a bad index
    // (e.g. "part": 5) would read/write out of bounds. Clamp rather than
    // reject the whole event, so one bad field in a scenario doesn't lose
    // the rest of an otherwise-valid timeline; warn loudly either way.
    if (e.part < 0 || e.part >= PART_COUNT) {
        const int clamped = std::max(0, std::min(e.part, PART_COUNT - 1));
        std::fprintf(stderr,
            "scenario: event '%s' has out-of-range part %d, clamping to %d\n",
            e.action.c_str(), e.part, clamped);
        e.part = clamped;
    }
    // "slot" means a different thing for flow_macro/flow_cv (a 0..MACRO_COUNT-1
    // macro index, MACRO_COUNT == 6) than it does for every action above
    // (a LANE_COUNT/FXT_COUNT index, both 5) -- clamping a flow slot against
    // LANE_COUNT/FXT_COUNT would silently fold macro 5 (SPACE) onto macro 4
    // (WANDER). Flow actions get their own MACRO_COUNT-ranged clamp in
    // apply_event(Instrument&, flow::Flow*, const Event&) instead.
    if (e.action.rfind("flow_", 0) != 0) {
        const int max_slot = std::min(int(LANE_COUNT), int(FXT_COUNT)) - 1;
        if (e.slot < 0 || e.slot > max_slot) {
            const int clamped = std::max(0, std::min(e.slot, max_slot));
            std::fprintf(stderr,
                "scenario: event '%s' has out-of-range slot %d, clamping to %d\n",
                e.action.c_str(), e.slot, clamped);
            e.slot = clamped;
        }
    }
    return e;
}

bool spky::load_scenario(const std::string& path, Scenario& out, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }
    json j;
    try { in >> j; }
    catch (const std::exception& ex) { err = ex.what(); return false; }

    out.sample_rate = j.value("sample_rate", 48000);
    out.bpm         = j.value("bpm", 120.f);
    out.duration_s  = j.value("duration_s", 10.0);
    out.input_wav   = j.value("input_wav", std::string());

    if (j.contains("init"))
        for (const auto& e : j["init"]) out.init_events.push_back(parse_event(e, false));
    if (j.contains("events"))
        for (const auto& e : j["events"]) out.events.push_back(parse_event(e, true));

    std::stable_sort(out.events.begin(), out.events.end(),
                     [](const Event& a, const Event& b) { return a.time_s < b.time_s; });

    // Task 9: has_flow / flow_code. init before timeline, in that order, so
    // "the first flow_wake found" reads as the scenario author would expect.
    const auto scan = [&](const std::vector<Event>& evs) {
        for (const Event& e : evs) {
            if (e.action.rfind("flow_", 0) != 0) continue;
            out.has_flow = true;
            if (e.action == "flow_wake" && out.flow_code.empty())
                out.flow_code = e.svalue;
        }
    };
    scan(out.init_events);
    scan(out.events);

    return true;
}

static QuantMode parse_qmode(const std::string& s) {
    if (s == "chrom") return QuantMode::Chrom;
    if (s == "free")  return QuantMode::Free;
    return QuantMode::Scale;
}

static FxBlock parse_fx_block(const std::string& s) {
    return s == "grit" ? FxBlock::Grit : FxBlock::Flux;
}

static GritMode parse_grit_mode(const std::string& s) {
    return s == "reduce" ? GritMode::Reduce : GritMode::Drive;
}

static EngineId parse_engine(const std::string& s) {
    if (s == "test_tone") return ENGINE_TEST_TONE;
    if (s == "sampler")   return ENGINE_SAMPLER;
    if (s == "wave")      return ENGINE_WAVE;
    if (s == "body")      return ENGINE_BODY;
    if (s == "bbd")       return ENGINE_BBD;
    return ENGINE_SYNTH;
}

static int parse_scale_name(const std::string& s) {
    if (s == "aeolian")   return SCALE_AEOLIAN;
    if (s == "mixo")      return SCALE_MIXO;
    if (s == "lydian")    return SCALE_LYDIAN;
    if (s == "hirajoshi") return SCALE_HIRAJOSHI;
    if (s == "pygmy")     return SCALE_PYGMY;
    if (s == "min_pent")  return SCALE_MIN_PENT;
    if (s == "kumoi")     return SCALE_KUMOI;
    if (s == "maj_pent")  return SCALE_MAJ_PENT;
    if (s == "phrygian")  return SCALE_PHRYGIAN;
    if (s == "hijaz")     return SCALE_HIJAZ;
    if (s == "harm_min")  return SCALE_HARM_MIN;
    if (s == "whole")     return SCALE_WHOLE;
    return SCALE_DORIAN;   // "dorian" and anything unknown -> the default
}

void spky::apply_event(Instrument& inst, const Event& e) {
    const std::string& a = e.action;
    if      (a == "set_tempo_bpm")     inst.set_tempo_bpm(e.value);
    else if (a == "set_shuffle")       inst.set_shuffle(e.value);
    else if (a == "set_rate")          inst.set_rate(e.part, e.value);
    else if (a == "set_sync")          inst.set_sync(e.ivalue != 0);
    else if (a == "set_shape")         inst.set_shape(e.part, e.value);
    else if (a == "set_density")       inst.set_density(e.part, e.value);
    else if (a == "set_smooth")        inst.set_smooth(e.part, e.value);
    else if (a == "set_range")         inst.set_range(e.part, e.value);
    else if (a == "set_entropy" || a == "set_variation") inst.set_variation(e.part, e.value);
    else if (a == "set_form")          inst.set_form(e.part, e.ivalue);
    else if (a == "set_song")          inst.set_song(e.part, e.ivalue);
    else if (a == "set_principle")     inst.set_principle(e.part, e.ivalue);
    else if (a == "set_depth")         inst.set_depth(e.part, e.value);
    else if (a == "set_tune")          inst.set_tune(e.part, e.value);
    else if (a == "set_color")         inst.set_color(e.part, e.value);
    else if (a == "set_step")          inst.set_step(e.part, e.flag, e.ivalue);
    else if (a == "new_phrase")        inst.new_phrase(e.part);
    else if (a == "set_fixed_slew")    inst.set_fixed_slew(e.part, e.flag);
    else if (a == "set_target_active") inst.set_target_active(e.part, e.slot, e.flag);
    else if (a == "set_target_base")   inst.set_target_base(e.part, e.slot, e.value);
    else if (a == "set_target_depth")  inst.set_target_depth(e.part, e.slot, e.value);
    else if (a == "set_scale")      inst.set_scale(parse_scale_name(e.svalue));
    else if (a == "set_quant_mode") inst.set_quant_mode(e.part, parse_qmode(e.svalue));
    else if (a == "set_root")       inst.set_root(e.part, e.ivalue);
    else if (a == "set_fx_on")            inst.set_fx_on(e.part, parse_fx_block(e.svalue), e.flag);
    else if (a == "set_grit_mode")        inst.set_grit_mode(e.part, parse_grit_mode(e.svalue));
    else if (a == "set_fx_target_active") inst.set_fx_target_active(e.part, e.slot, e.flag);
    else if (a == "set_fx_target_base")   inst.set_fx_target_base(e.part, e.slot, e.value);
    else if (a == "set_fx_target_depth")  inst.set_fx_target_depth(e.part, e.slot, e.value);
    else if (a == "set_flux_mix")         inst.set_flux_mix(e.part, e.value);
    else if (a == "set_flux_rate")        inst.set_flux_rate(e.part, e.ivalue);
    else if (a == "set_grit_mix")         inst.set_grit_mix(e.part, e.value);
    else if (a == "set_comp")             inst.set_comp(e.part, e.value);
    else if (a == "set_master_drive")     inst.set_master_drive(e.value);
    else if (a == "set_tide")             inst.set_tide(e.value);
    else if (a == "set_choke")            inst.set_choke(e.value);
    else if (a == "set_reverb_size")      inst.set_reverb_size(e.value);
    else if (a == "set_reverb_tone")      inst.set_reverb_tone(e.value);
    else if (a == "set_reverb_decay")     inst.set_reverb_decay(e.value);
    else if (a == "set_reverb_diffusion") inst.set_reverb_diffusion(e.value);
    else if (a == "set_reverb_mix")       inst.set_reverb_mix(e.value);
    else if (a == "set_reverb_smear")     inst.set_reverb_smear(e.value);
    else if (a == "set_reverb_mod")       inst.set_reverb_mod(e.value);
    else if (a == "set_engine")          inst.set_engine(e.part, parse_engine(e.svalue));
    else if (a == "set_voice_attack")    inst.set_voice_attack(e.part, e.value);
    else if (a == "set_voice_decay")     inst.set_voice_decay(e.part, e.value);
    else if (a == "set_voice_resonance") inst.set_voice_resonance(e.part, e.value);
    else if (a == "set_voice_sub")       inst.set_voice_sub(e.part, e.value);
    else if (a == "set_voice_detune")    inst.set_voice_detune(e.part, e.value);
    else if (a == "set_voice_filt")      inst.set_voice_filt(e.part, e.value);
    // Excitation bus source selection (spec §6, Task 10 -- Instrument::
    // set_excitation_sources, engine/instrument.h). Three booleans, no new
    // Event field: `flag` carries the tape source (the same slot every other
    // single-bool action already uses), `ivalue` carries the remaining two
    // as a bitmask -- bit 0 = other deck, bit 1 = audio in -- following
    // set_step's precedent of packing flag + ivalue into one action.
    else if (a == "set_excitation_sources")
        inst.set_excitation_sources(e.part, e.flag,
                                     (e.ivalue & 1) != 0, (e.ivalue & 2) != 0);
    else if (a == "trigger_manual")      inst.trigger_manual(e.part);
    else if (a == "set_morph")           inst.set_morph(e.value);
    else if (a == "set_couple")          inst.set_couple(e.value);
    else if (a == "set_drift")           inst.set_drift(e.value);
    else if (a == "spot")                inst.spot();
    else if (a == "settle")              inst.settle();
    else if (a == "sampler_record")     inst.sampler_record(e.part, e.flag);
    else if (a == "sampler_clear")      inst.sampler_clear(e.part);
    else if (a == "sampler_monitor")    inst.sampler_monitor(e.part, e.flag);
    else if (a == "sampler_speed_mode") inst.sampler_speed_mode(e.part, e.svalue == "tape");
    else if (a == "sampler_reverse")    inst.sampler_reverse(e.part, e.flag);
    else if (a == "sampler_feedback")   inst.sampler_feedback(e.part, e.value);
    else if (a == "sampler_overlap")    inst.sampler_overlap(e.part, e.value);
    else if (a == "sampler_scan")       inst.sampler_scan(e.part, e.value);
    else if (a == "sampler_punch")      inst.sampler_punch(e.part);
    else if (a == "load_wav") {
        WavData d;
        std::string err;
        if (read_wav(e.svalue, d, err))
            inst.load_sample(e.part, d.l.data(), d.r.data(), d.l.size());
        else
            std::fprintf(stderr, "load_wav: %s\n", err.c_str());
    }
    // unknown actions are ignored on purpose (forward-compatible scenarios)
}

void spky::apply_event(Instrument& inst, flow::Flow* fl, const Event& e) {
    const std::string& a = e.action;
    if (a.rfind("flow_", 0) != 0) {
        apply_event(inst, e);   // not a flow action: fall through unchanged
        return;
    }
    if (!fl) {
        // A scenario with no flow_* action never gets a Flow instance
        // (Scenario::has_flow), so this only fires if a flow_* action is
        // dispatched some other way. Warn, don't crash, don't silently
        // pretend it worked.
        std::fprintf(stderr,
            "scenario: '%s' needs the flow layer, but none is active; ignoring\n",
            a.c_str());
        return;
    }
    if (a == "flow_wake") {
        flow::TerrainState st;
        // decode_code is strict by design (Task 5): a malformed or missing
        // code is a hard error here, never a silent fallback seed -- a
        // silently different terrain would make an audition file lie about
        // what it demonstrates (task-9 resolution #5).
        if (!flow::decode_code(e.svalue.c_str(), st)) {
            std::fprintf(stderr,
                "scenario: flow_wake has a malformed terrain code '%s'\n",
                e.svalue.c_str());
            return;
        }
        fl->wake(st);
    } else if (a == "flow_macro" || a == "flow_cv") {
        // Flow::set_macro/set_cv index a fixed MACRO_COUNT-sized array with
        // no bounds check of their own (same "Fix 6" reasoning as the
        // part/slot clamp above, just a different-sized array: MACRO_COUNT
        // == 6, not LANE_COUNT/FXT_COUNT's 5).
        if (e.slot < 0 || e.slot >= flow::MACRO_COUNT) {
            const int clamped =
                std::max(0, std::min(e.slot, int(flow::MACRO_COUNT) - 1));
            std::fprintf(stderr,
                "scenario: event '%s' has out-of-range macro slot %d, clamping to %d\n",
                a.c_str(), e.slot, clamped);
            if (a == "flow_macro") fl->set_macro(clamped, e.value);
            else                   fl->set_cv(clamped, e.value);
        } else if (a == "flow_macro") {
            fl->set_macro(e.slot, e.value);
        } else {
            fl->set_cv(e.slot, e.value);
        }
    } else if (a == "flow_new") {
        fl->new_full();
    } else if (a == "flow_new_partial") {
        fl->new_partial(static_cast<uint8_t>(e.ivalue));
    } else if (a == "flow_undo") {
        fl->undo();
    } else if (a == "flow_lock") {
        fl->set_lock(e.flag);
    }
    // unknown flow_* actions are ignored on purpose too (forward-compat,
    // same policy as the legacy dispatch above)
}
