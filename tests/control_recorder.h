#pragma once
// An Instrument stand-in for control/control_law.h: logs every setter call
// (name, deck, value) and answers the five getters the law reads.
#include <string>
#include <type_traits>
#include <vector>
#include "instrument.h"

struct Rec {
    struct Call { std::string fn; int p; float v; };
    std::vector<Call> calls;
    spky::EngineId eng[2] = {spky::ENGINE_SYNTH, spky::ENGINE_SYNTH};
    bool  recording[2] = {};
    bool  empty[2] = {true, true};
    float lane[2][spky::LANE_COUNT] = {};
    float laneStep[2][spky::LANE_COUNT] = {};

    template <class T> static float f(T v) {
        if constexpr (std::is_enum_v<T>) return float(int(v)); else return float(v);
    }
    void log(std::string fn, int p, float v) { calls.push_back({std::move(fn), p, v}); }
    // last value a setter got for deck p (p = -1: global), NaN if never called
    float last(const std::string& fn, int p = -1) const {
        for (auto it = calls.rbegin(); it != calls.rend(); ++it)
            if (it->fn == fn && it->p == p) return it->v;
        return std::nanf("");
    }
    int count(const std::string& fn) const {
        int n = 0; for (auto& c : calls) n += c.fn == fn; return n;
    }
    int index(const std::string& fn, int p) const {
        for (size_t i = 0; i < calls.size(); ++i)
            if (calls[i].fn == fn && calls[i].p == p) return int(i);
        return -1;
    }

    // getters
    float lane_output(int p, int s) const { return lane[p][s]; }
    float lane_output_stepped(int p, int s) const { return laneStep[p][s]; }
    spky::EngineId engine_id(int p) const { return eng[p]; }
    bool  sampler_is_recording(int p) const { return recording[p]; }
    bool  sampler_empty(int p) const { return empty[p]; }

#define REC_G(name) template <class T> void name(T v) { log(#name, -1, f(v)); }
#define REC_P(name) template <class T> void name(int p, T v) { log(#name, p, f(v)); }
#define REC_PS(name) template <class S, class T> void name(int p, S s, T v) \
        { log(std::string(#name) + "/" + std::to_string(int(s)), p, f(v)); }
    REC_G(set_shuffle) REC_G(set_morph) REC_G(set_sync) REC_G(set_couple)
    REC_G(set_drift) REC_G(set_tide) REC_G(set_choke) REC_G(set_pull)
    REC_G(set_reverb_size) REC_G(set_reverb_decay) REC_G(set_reverb_tone)
    REC_G(set_reverb_diffusion) REC_G(set_master_drive) REC_G(set_reverb_smear)
    REC_G(set_reverb_mod) REC_G(set_scale) REC_G(set_tempo_bpm) REC_G(set_pace)
    REC_P(set_rate) REC_P(set_shape) REC_P(set_density) REC_P(set_smooth)
    REC_P(set_range) REC_P(set_depth) REC_P(set_tune) REC_P(set_voice_attack)
    REC_P(set_voice_decay) REC_P(set_voice_resonance) REC_P(set_voice_filt)
    REC_P(set_color) REC_P(set_voice_sub) REC_P(set_voice_detune)
    REC_P(set_flux_mix) REC_P(set_flux_rate) REC_P(set_link) REC_P(set_part_level)
    REC_P(set_comp) REC_P(set_pan) REC_P(sampler_speed_mode) REC_P(sampler_reverse)
    REC_P(sampler_feedback) REC_P(sampler_overlap) REC_P(set_variation)
    REC_P(sampler_scan) REC_P(set_grit_mode) REC_P(set_grit_mix) REC_P(set_form)
    REC_P(set_song) REC_P(set_reverb_mix)
    REC_PS(set_fx_target_base) REC_PS(set_target_base) REC_PS(set_target_depth)
    REC_PS(set_fx_target_depth) REC_PS(set_fx_target_active)
    REC_PS(set_target_active) REC_PS(set_fx_on)
#undef REC_G
#undef REC_P
#undef REC_PS
    void set_engine(int p, spky::EngineId id) { eng[p] = id; log("set_engine", p, f(id)); }
    void sampler_record(int p, bool on) { recording[p] = on; log("sampler_record", p, on); }
    void set_step(int p, bool on, int steps) { log("set_step", p, on ? float(steps) : -1.f); }
    void set_excitation_sources(int p, bool a, bool b, bool c) {
        log("set_excitation_sources", p, float(a) + 2.f * b + 4.f * c);
    }
    void settle() { log("settle", -1, 1.f); }
    void new_phrase(int p) { log("new_phrase", p, 1.f); }
    void sampler_punch(int p) { log("sampler_punch", p, 1.f); }
};
