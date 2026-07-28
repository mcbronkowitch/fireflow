#pragma once
#include <cstdint>
#include "body/exciter.h"
#include "body/ks_string.h"
#include "body/mode_bank.h"

namespace spky {

// One BODY voice (spec 2026-07-26 body-resonator, §2):
//
//   Exciter ──┬──→ String A ─┐
//   bus (SUB) ┘    String B ─┼─ MATL morph ─→ pan → vel
//                  ModeBank ─┘
//
// No Svf, no Env: the decay IS the envelope, for both structures. active()
// is an energy follower, not an envelope flag.
//
// Setter names are SYNTH's (the SynthEngineT voice contract); the meanings
// are resonator-native -- see the mapping table in the plan and spec §5.
//
// kEngineVoices = 1: measured 1395 cycles/sample per voice (docs/bench/
// 2026-07-26-1ec4429-body.md). SynthEngineT<BodyVoice> reads this to size
// its per-voice arrays (Task 5's V::kEngineVoices indirection); it is not
// SynthEngineT's kVoices = 4 default.
class BodyVoice {
public:
    static constexpr int kEngineVoices = 1;

    void init(float sample_rate, uint32_t seed);

    void trigger(float freq_hz);
    void set_sustaining(bool on);
    void set_pitch_hz(float freq_hz);
    void set_vel(float v);

    void set_env_times(float attack_s, float decay_s);  // exciter length, damping
    void set_morph(float m);                            // MATL
    void set_detune_cents(float max_ct);                // spread + mode stretch
    void set_sub_level(float n);                        // excitation bus level
    void set_cutoff_hz(float hz);                       // brightness
    void set_resonance(float n);                        // exciter character
    void set_pan(float pan);
    void set_drift_amount(float a);
    void set_hold(bool on);                             // palm mute
    void set_excitation(float x);                       // per-sample bus feed
    // COLOR, read as the chord's QUALITY (spec §5/§7). Signed -1..+1: which
    // WAY the partials stretch. DETUNE (set_detune_cents) is how far, and
    // multiplies this -- so DETUNE = 0 is harmonic whatever COLOR says.
    void set_material_character(float c);

    void update_control(float dt_s);
    void process(float& accL, float& accR);

    bool  active() const { return _follower > kFloor || _hold_samples > 0; }
    float env_value() const { return _follower; }
    float detune_cents() const { return _detune_ct; }

#ifdef SPKY_TESTING
    // Counts entries into _apply_params() itself -- NOT a primitive's own
    // coeff_updates() (ModeBank/KsString), which only increments when a
    // value actually CHANGED. That indirection is exactly what would hide a
    // per-sample caller that keeps re-entering with unchanged numbers (I-1,
    // final review, 2026-07-26-body-resonator-engine/final-review.md). This
    // counter answers "how often does the parameter block itself run",
    // which is the spec §4 claim, directly. Test-only, like the SPKY_TESTING
    // accessors in engine/instrument.h, engine/mod/lane.h and
    // engine/mod/super_modulator.h.
    int apply_params_calls_for_test() const { return _apply_calls_for_test; }
#endif

private:
    void _apply_params();

    static constexpr float kFloor = 0.000251f;   // -72 dB
    static constexpr int   kMinHoldSamples = 4800;   // 100 ms

    KsString        _str_a, _str_b;
    ModeBank        _bank;
    Exciter         _exciter;

    float _sr = 48000.f;
    float _freq = 220.f, _matl = 0.f, _detune_ct = 0.f;
    float _material_char = 0.f;                  // COLOR: signed, -1..+1
    float _damping = 0.5f, _brightness = 0.5f, _sub = 0.f;
    // Loudness tilt derived from _brightness -- see set_cutoff_hz. 1 at the
    // brightest setting, so a voice that never sees FILTER is unaffected.
    float _bright_gain = 1.f;
    float _vel = 1.f, _vel_target = 1.f;
    float _mix_string = 1.f, _mix_modal = 0.f;   // equal-power MATL gains
    float _excitation = 0.f;
    float _follower = 0.f, _peak = 0.f;
    float _gain_l = 0.70710678f, _gain_r = 0.70710678f;
    float _pan_base = 0.f, _drift_amt = 0.f;
    float _drift_pan_phase = 0.f, _drift_det_phase = 0.f;
    float _drift_pan_hz = 0.1f, _drift_det_hz = 0.1f;
    float _drift_ct_cur = 0.f;
    int   _hold_samples = 0;
    bool  _sustaining = false, _hold = false;

#ifdef SPKY_TESTING
    int _apply_calls_for_test = 0;
#endif
};

} // namespace spky
