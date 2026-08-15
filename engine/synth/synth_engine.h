#pragma once
#include <array>
#include <cstdint>
#include "mod/rng.h"
#include "parts/engine_iface.h"
#include "pitch/chord.h"
#include "synth/voice.h"
#include "synth/wt_osc.h"
#include "util/onepole.h"
#include "body/body_voice.h"

namespace spky {

// The M2 polyphonic part engine: 4 trigger-driven voices behind IPartEngine.
//
// - Allocation: round-robin over free voices; none free -> steal the OLDEST
//   (by trigger order); the steal retriggers the envelope from its current
//   level (click-free, Voice::trigger).
// - STEP (flow == false): plain AD - notes end, silence is legitimate.
// - FLOW (flow == true): the most recently triggered voice is the SUSTAINING
//   voice - it decays to 0.7 and holds, and its pitch continuously follows
//   the quantized PITCH target. A new fire demotes it (decays to zero) and
//   takes over. Entering FLOW with no sustaining voice auto-triggers one at
//   the current PITCH target (the drone promise) - deferred to the next
//   process() call so the targets are fresh.
// - Targets: TIMBRE (oscillator morph/frame), FILTER (60 Hz-14 kHz
//   exp), PITCH (latched at trigger, 110*8^p), MOTION (pan fan
//   [-1,+1,-0.5,+0.5] * width + drift ~ width), LEVEL (OnePole-smoothed
//   master gain). All but PITCH act on all voices continuously.
// - Control rate: drift LFOs + envelope coefficients + all voice parameter
//   pushes run once per kCtrlInterval samples (CPU-budget constraint).
template <class V>
class SynthEngineT : public IPartEngine {
public:
    static constexpr int   kVoices       = V::kEngineVoices;
    static constexpr int   kCtrlInterval = 96;
    static constexpr float kAttackFloorS = 0.002f;
    static constexpr float kDecayMinS    = 0.05f;
    static constexpr float kDecayMaxS    = 20.f;
    // Raised from 35 ct (spec 2026-08-09 §4): DETUNE came back onto the panel
    // as a performance control and drones want the reach. SynthEngine and
    // WaveEngine are the intended beneficiaries; BodyEngine is the same
    // template and pays it back at BodyVoice::kDetuneScale, so its own
    // 140 ct rail is unchanged. BbdEngine and the sampler have their own
    // paths and keep 35 ct.
    static constexpr float kDetuneCeilCt = 105.f;
    static constexpr int   kMaxChord     = 4;
    static constexpr float kStabSpreadS  = 0.008f;   // stab humanization (ear-tunable)
    // How hard the WEAKEST note of a full pattern strikes, relative to the
    // rank-0 anchor. By ear, first try, deliberately equal to
    // kAccentDecFloor so a listening session says which half wants to differ.
    static constexpr float kAccentVelFloor = 0.3f;
    // How long the WEAKEST note of a full pattern rings, relative to the DEC
    // knob's setting, at DEC fully up. By ear, first try.
    static constexpr float kAccentDecFloor = 0.3f;

    static_assert(kMaxChord == ChordBuilder::kMaxNotes,
                  "chord slot count must match the builder");
    // kPanFan (synth_engine.cpp) has exactly 4 entries and is indexed by
    // voice index in _update_control(); raising kVoices past 4 would read
    // past its end.
    static_assert(kVoices <= 4, "kPanFan has only 4 entries");

    // FILT: linke Haelfte uebersteuert die Schiene um genau die Blendzone,
    // damit t = -1 bei JEDER Lane-Stellung in Stille endet (Invariante:
    // kFiltLeftScale >= 1 + kFiltFadeRange).
    static constexpr float kFiltLeftScale = 1.25f;
    static constexpr float kFiltFadeRange = 0.25f;

    void set_seed(uint32_t seed) { _seed = seed; }   // call BEFORE init

    void init(float sample_rate) override;
    void set_targets(const float* t, float tune) override;
    void trigger(float pitch_norm) override;
    void trigger_chord(const float* pitches_norm, int n) override;
    void set_chord(const float* pitches_norm, int n) override;
    void process(float& outL, float& outR) override;
    void set_cycle(float seconds) override;
    void set_flow(bool flow) override;
    void set_hold(bool on) override;

    // Excitation bus (spec 2026-07-26 body-resonator, §6, Task 9): forwards
    // to every voice unconditionally, same idiom as set_hold/set_material_
    // character in BODY's voice contract. A no-op on SYNTH and WAVE --
    // VoiceT::set_excitation (synth/voice.h) is an empty inline -- so this
    // costs those two engines one dead store call per voice per control
    // tick and changes nothing about their signal path (ctrl_identity and
    // wave_formant_sweep are the proof).
    void set_excitation(float x) override {
        for (int i = 0; i < kVoices; ++i) _voices[i].set_excitation(x);
    }

    // VOICE edit layer (normalized knobs; boot defaults live as raw ratios)
    void set_attack(float n);      // ratio = 0.002 * 250^n  (0.2%..50% of cycle)
    void set_decay(float n);       // ratio = 0.1 * 80^n     (0.1x..8x cycle)
    void set_accent(float a) override;   // STEP accent, 0..1 (0 = full strength)
    void set_resonance(float n);
    void set_sub(float n);
    void set_detune(float n);      // independent symmetric spread = n * 105 ct
    void set_filt(float n);        // -1..+1 cutoff trim; left end fades to silence

    int   active_voices() const;
    float voice_env(int v) const;
    int   sustain_voice() const;
    int   sustain_count() const;

    // --- observation (tests) ---
    // Raw values behind set_sub()/set_detune(), exposed so a test can pin the
    // Synth-only leg of the SUB/DTUN split (spec 2026-07-21
    // morphagene-controls, Part::set_voice_sub/set_voice_detune) without
    // reaching into private state. Not used on the audio path.
    float sub_level() const         { return _sub_level; }
    float detune_spread_ct() const  { return _detune_spread_ct; }
    // Every control tick pushes the same spread to all voices.
    float applied_detune_ct() const { return _voices[0].detune_cents(); }
    // How many notes the last set_chord() pushed. Lets a test pin that
    // Part::_flatten_for_sampler collapses the chord for the SAMPLER only and
    // leaves the synth's chord surface intact. Not used on the audio path.
    int chord_n() const { return _chord_n; }
#ifdef SPKY_TESTING
    // The accent set_accent() last stored, same idiom as the SPKY_TESTING
    // accessors in engine/instrument.h, engine/mod/lane.h and
    // engine/mod/super_modulator.h: a test-only window into private state,
    // compiled only for the tests target so `render` and the firmware never
    // see it.
    float accent_for_test() const { return _accent; }
#endif

private:
    void _do_trigger(float pitch_norm, float vel, int chord_slot);
    void _demote_all();
    void _update_control();
    void _adjust_surface();

    // std::array<int, kVoices> filled with -1 -- a brace initialiser can't
    // express "every element" independent of kVoices, so this fills it at
    // compile time instead (spec 2026-07-26 body-resonator, fix round 1:
    // the old `{ -1, -1, -1, -1 }` only compiled because kVoices was always
    // exactly 4).
    static constexpr std::array<int, kVoices> _no_chord_slots() {
        std::array<int, kVoices> a{};
        for (int i = 0; i < kVoices; ++i) a[i] = -1;
        return a;
    }

    std::array<V, kVoices> _voices;
    std::array<uint32_t, kVoices> _order {};   // trigger sequence per voice
    uint32_t _seq = 0;
    uint32_t _seed = 0xC0FFEEu;

    float _sr = 48000.f;
    float _targets[LANE_COUNT] = { 0.f, 0.5f, 0.5f, 0.f, 0.8f };
    bool  _flow = false;
    bool  _hold = false;   // CHOKE: drone released + auto-retrigger paused
    bool  _auto_pending = false;   // drone promise, fires in process()
    int   _next_rr = 0;
    int   _ctrl_ctr = 0;

    float _chord[kMaxChord] = { 0.f, 0.f, 0.f, 0.f };   // surface targets (Part)
    int   _chord_n = 1;
    // COLOR-as-material (spec §7), pushed to every voice; a no-op on every
    // voice but BodyVoice. Derived from the chord surface ONLY when the
    // surface actually moved: the derivation lives in _update_control(), the
    // control tick, and set_chord() only raises the flag. See set_chord() for
    // why the flag stays even though Part pushes at control rate, not per
    // sample.
    float _material_char = 0.f;
    bool  _material_dirty = true;
    std::array<bool, kVoices> _sustaining {};                 // value-init: all false
    std::array<int, kVoices>  _chord_slot = _no_chord_slots();
    struct Pending { int ctr; float pitch; int slot; };
    Pending _pending[kMaxChord] = {};
    int   _pending_n = 0;
    float _vel_now = 1.f;
    Rng   _stab_rng;                                    // draws ONLY for n>=2 chords

    float _cycle_s = 1.f;
    float _attack_ratio = 0.02f;   // boot: 2 % of cycle (spec)
    float _decay_ratio  = 1.5f;    // boot: 1.5 x cycle (spec)
    float _decay_n = 0.f;          // DEC knob position; the accent's room
    float _accent = 0.f;           // STEP accent of the note being struck
    float _resonance = 0.15f;      // boot (spec)
    float _sub_level = 0.3f;       // boot (spec)
    // Boot default (spec). An ABSOLUTE cents value, not derived from
    // kDetuneCeilCt -- unlike set_detune(n), which always computes
    // n * kDetuneCeilCt, so raising the ceiling (spec 2026-08-09 task 10,
    // 35 -> 105 ct) does not move this number. It is a SYNTH-oriented
    // constant (18 ct is a tasteful pre-push default for that family); BODY
    // reads it too before anything ever calls set_voice_detune (only
    // BodyVoice's own kDetuneScale sits between this and BODY's spread), so
    // BODY's own untouched-knob default moved when kDetuneScale did
    // (18 * 4 = 72 ct before, 18 * 4/3 = 24 ct after) -- deliberate, not a
    // bug: this default was never chosen to serve BODY, and nothing
    // preserves it for both engines when only one ceiling exists to derive
    // from at all (same shared-constant/two-engine conflict as the panel's
    // own DETUNE_A/B init split).
    float _detune_spread_ct = 18.f;
    float _filt_amt  = 0.f;        // FILT knob -1..+1 (boot: neutral)
    float _filt_gain = 1.f;        // silence fade below the 60 Hz rail (control-rate)

    OnePole _level;                // smoothed master gain (LEVEL target)
};

using SynthEngine = SynthEngineT<VoiceT<MorphOsc>>;
extern template class SynthEngineT<VoiceT<MorphOsc>>;

using WaveEngine = SynthEngineT<VoiceT<WtOsc>>;
extern template class SynthEngineT<VoiceT<WtOsc>>;

// BODY (spec 2026-07-26 body-resonator, Task 8). Same machine, a resonator
// instead of an oscillator+envelope voice, and kVoices == 1 rather than 4 --
// BodyVoice::kEngineVoices, measured at 1395 cycles/sample, is what the Seed
// affords. It runs the shared part-engine contract
// (tests/synth_engine_contract.h) and NOT the AD-envelope one next to it;
// tests/test_body_engine.cpp and both header comments explain the boundary.
using BodyEngine = SynthEngineT<BodyVoice>;
extern template class SynthEngineT<BodyVoice>;

#ifdef SPKY_TESTING
// Test-only, like the SPKY_TESTING accessors in engine/instrument.h,
// engine/mod/lane.h and engine/mod/super_modulator.h. Only the tests target
// defines SPKY_TESTING, so `render` and the firmware never instantiate this
// template class at all -- the linker was already dropping it, but not
// compiling it is cheaper than dropping it, and Task 7's author reading this
// header should not have to wonder whether a second engine alias next to
// SynthEngine and WaveEngine is something they need to extend.
namespace detail {
// Exists solely so SynthEngineT<V> gets exercised at a voice count other
// than 4 in this build (fix round 1, 2026-07-26 body-resonator, Task 5
// review): the template's deliverable is the V::kEngineVoices indirection
// actually working, and nothing else instantiates it below kVoices == 4
// until BODY's real voice (Task 7) does. Not a real voice -- does not need
// to make sound. See tests/test_synth_engine_voice_count.cpp.
struct VoiceCountProbe {
    static constexpr int kEngineVoices = 1;

    // Counts triggers so a test can pin how OFTEN the engine fires, not just
    // how many voices ended up active -- the one observation a real voice
    // cannot give (see the surface-bloom case in
    // tests/test_synth_engine_voice_count.cpp). Static because SynthEngineT
    // owns its voices by value; the test zeroes it before each run.
    static inline int trig_count = 0;

    void init(float /*sample_rate*/, uint32_t /*seed*/) {}
    void trigger(float /*freq_hz*/) { ++trig_count; _active = true; }
    void set_sustaining(bool on) { _sustaining = on; if (!on) _active = false; }
    void set_pitch_hz(float /*freq_hz*/) {}
    void set_vel(float /*v*/) {}
    void set_env_times(float /*attack_s*/, float /*decay_s*/) {}
    void set_decay_scale(float /*s*/) {}
    void set_morph(float /*m*/) {}
    void set_detune_cents(float /*max_ct*/) {}
    void set_sub_level(float /*n*/) {}
    void set_cutoff_hz(float /*hz*/) {}
    void set_resonance(float /*n*/) {}
    void set_pan(float /*pan*/) {}
    void set_drift_amount(float /*a*/) {}
    void update_control(float /*dt_s*/) {}
    void process(float& accL, float& accR) { if (_active) { accL += 0.f; accR += 0.f; } }
    void set_hold(bool /*on*/) {}
    void set_excitation(float /*x*/) {}
    void set_material_character(float /*c*/) {}

    bool  active() const { return _active; }
    float env_value() const { return _active ? 1.f : 0.f; }
    float detune_cents() const { return 0.f; }

    bool _active = false;
    bool _sustaining = false;
};
} // namespace detail

using SynthEngineVoiceCountProof = SynthEngineT<detail::VoiceCountProbe>;
extern template class SynthEngineT<detail::VoiceCountProbe>;
#endif // SPKY_TESTING

} // namespace spky
