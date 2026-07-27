#include "synth/synth_engine.h"
#include <cmath>
#include "body/material.h"
#include "util/math.h"

using namespace spky;

namespace {
// 4 voices at full level + sub must stay inside the part's +/-1.5 headroom.
constexpr float kVoiceGain = 0.22f;
constexpr float kPanFan[4] = { -1.f, 1.f, -0.5f, 0.5f };

// Pitch contract (identical numbers to TestToneEngine): 0..1 = 36 semitones.
// std::pow is fine here - trigger/control rate, never per sample.
inline float pitch_to_hz(float p) { return 110.f * std::pow(8.f, clampf(p, 0.f, 1.f)); }

// FILTER target: exponential 60 Hz .. 14 kHz (spec).
inline float filter_hz(float n) {
    return 60.f * std::pow(14000.f / 60.f, clampf(n, 0.f, 1.f));
}
} // namespace

template <class V>
void SynthEngineT<V>::init(float sample_rate) {
    _sr = sample_rate;
    for (int v = 0; v < kVoices; ++v) {
        _voices[v].init(sample_rate, _seed + 0x9e3779b9u * static_cast<uint32_t>(v + 1));
        _order[v] = 0;
        _sustaining[v] = false;
        _chord_slot[v] = -1;
    }
    _seq = 0;
    _auto_pending = false;
    _hold = false;
    _next_rr = 0;
    _ctrl_ctr = 0;                 // first process() runs a control tick
    _pending_n = 0;
    _chord_n = 1;
    _chord[0] = _targets[LANE_PITCH];
    _material_char = 0.f;          // one note has no quality
    _material_dirty = true;
    _vel_now = 1.f;
    _stab_rng.seed(_seed ^ 0x57AB5EEDu);
    _level.init(sample_rate, 0.01f);
    _level.reset(_targets[LANE_LEVEL]);
    _update_control();
}

template <class V>
int SynthEngineT<V>::sustain_voice() const {
    for (int v = 0; v < kVoices; ++v)
        if (_sustaining[v]) return v;
    return -1;
}

template <class V>
int SynthEngineT<V>::sustain_count() const {
    int n = 0;
    for (int v = 0; v < kVoices; ++v)
        if (_sustaining[v]) ++n;
    return n;
}

template <class V>
void SynthEngineT<V>::_demote_all() {
    for (int v = 0; v < kVoices; ++v)
        if (_sustaining[v]) {
            _voices[v].set_sustaining(false);
            _sustaining[v] = false;
            _chord_slot[v] = -1;
        }
}

template <class V>
void SynthEngineT<V>::set_targets(const float* t, float /*tune*/) {
    // tune is already summed into the quantized PITCH target upstream (Part).
    for (int i = 0; i < LANE_COUNT; ++i) _targets[i] = t[i];
    // engine-standalone default: with no chord fed, the surface is the pitch
    // target (keeps engine-only tests and the test tone semantics unchanged)
    if (_chord_n <= 1) _chord[0] = _targets[LANE_PITCH];
}

template <class V>
void SynthEngineT<V>::set_chord(const float* p, int n) {
    if (n < 1) n = 1;
    if (n > kMaxChord) n = kMaxChord;
    // Part pushes the surface from its own control tick and on each step fire
    // (IPartEngine::set_chord), so this is control rate, not per sample --
    // and the material derivation still does not belong here. Two reasons it
    // lives in _update_control() behind this flag instead:
    //
    //  - Part re-pushes the same chord every tick whether or not it moved, so
    //    without the flag chord_character() would re-derive a static chord
    //    every 2 ms. The compare below is cheaper than the derivation.
    //  - trigger_chord() computes the character from the chord being STRUCK,
    //    which is fresher than the surface. Leaving the flag down lets that
    //    value stand until the surface actually moves.
    bool moved = (n != _chord_n);
    for (int i = 0; i < n; ++i) {
        if (_chord[i] != p[i]) moved = true;
        _chord[i] = p[i];
    }
    _chord_n = n;
    if (moved) _material_dirty = true;
}

template <class V>
void SynthEngineT<V>::set_cycle(float seconds) {
    _cycle_s = clampf(seconds, 0.01f, 120.f);   // applied at the next ctrl tick
}

template <class V>
void SynthEngineT<V>::set_flow(bool flow) {
    if (flow == _flow) return;
    _flow = flow;
    if (!flow) {
        _demote_all();                 // STEP: no surface; drones decay out
        _pending_n = 0;
        _auto_pending = false;
    } else if (sustain_count() == 0 && !_hold) {
        _auto_pending = true;          // drone promise; fires in process()
    }
}

template <class V>
void SynthEngineT<V>::set_hold(bool on) {
    if (on == _hold) return;
    _hold = on;
    if (on) {
        _demote_all();                 // CHOKE: the whole surface ducks out
        _pending_n = 0;
        _auto_pending = false;
    } else if (_flow && sustain_count() == 0) {
        _auto_pending = true;
    }
}

template <class V>
void SynthEngineT<V>::trigger(float pitch_norm) { _do_trigger(pitch_norm, 1.f, 0); }

template <class V>
void SynthEngineT<V>::trigger_chord(const float* p, int n) {
    if (n < 1) return;                                   // nothing to trigger
    if (n > kMaxChord) n = kMaxChord;
    // The chord's QUALITY is read from every note the layer built, BEFORE the
    // voice clamp below throws the unplayable ones away (spec §7: a BODY deck
    // sounds only the root, but what COLOR did to the material is the whole
    // chord's doing). Trigger rate, and it makes the strike land on the right
    // material instead of picking it up at the next control tick; the
    // surface's own derivation in _update_control() still owns the steady
    // state, so _material_dirty is deliberately left alone here.
    _material_char = chord_character(p, n);
    // ...and never more notes than there are voices to hold them. At
    // kVoices >= kMaxChord this line never fires, so SYNTH and WAVE are
    // untouched. Below it, the notes past kVoices had nowhere to go: each one
    // stole a voice that had just been struck, so a 4-note chord on a
    // one-voice engine was four strikes inside kStabSpreadS (~8 ms) that all
    // landed on the same voice, and only the LAST one survived -- a flam, at
    // 1/sqrt(4) of the level, where the caller asked for one note. Taking the
    // first kVoices notes keeps the root, which _do_trigger already treats as
    // the slot that owns the surface.
    if (n > kVoices) n = kVoices;
    if (n <= 1) { _do_trigger(p[0], 1.f, 0); return; }   // COLOR-0 exact path
    _vel_now = 1.f / std::sqrt(static_cast<float>(n));   // equal-power comp
    _pending_n = 0;
    _do_trigger(p[0], _vel_now, 0);                      // root lands on the beat
    for (int i = 1; i < n; ++i) {                        // rest strews over ~8 ms
        const int ctr = 1 + static_cast<int>(_stab_rng.next_unipolar()
                                             * kStabSpreadS * _sr);
        _pending[_pending_n].ctr = ctr;
        _pending[_pending_n].pitch = p[i];
        _pending[_pending_n].slot = i;
        ++_pending_n;
    }
}

template <class V>
void SynthEngineT<V>::_do_trigger(float pitch_norm, float vel, int chord_slot) {
    int pick = -1;
    for (int i = 0; i < kVoices; ++i) {              // round-robin over free voices
        int v = (_next_rr + i) % kVoices;
        if (!_voices[v].active()) { pick = v; break; }
    }
    if (pick < 0) {
        // none free: steal the oldest NON-sustaining voice first (a decaying
        // demoted voice or a plain STEP note) so a live surface voice is
        // never cannibalized by a mere bloom/retrigger; fall back to the
        // oldest sustaining voice only if every voice currently sustains
        // (unreachable during a bloom, since bloom implies m < _chord_n <=
        // kVoices, i.e. at least one voice is free or non-sustaining).
        int free_pick = -1, sus_pick = -1;
        uint32_t oldest_free = 0, oldest_sus = 0;
        for (int v = 0; v < kVoices; ++v) {
            if (_sustaining[v]) {
                if (sus_pick < 0 || _order[v] < oldest_sus) { oldest_sus = _order[v]; sus_pick = v; }
            } else {
                if (free_pick < 0 || _order[v] < oldest_free) { oldest_free = _order[v]; free_pick = v; }
            }
        }
        pick = (free_pick >= 0) ? free_pick : sus_pick;
    }
    _next_rr = (pick + 1) % kVoices;
    _order[pick] = ++_seq;

    if (_flow) {
        if (chord_slot == 0) _demote_all();   // a new chord replaces the surface
        _sustaining[pick] = true;
        _chord_slot[pick] = chord_slot;
        _voices[pick].set_sustaining(true);
    } else {
        _sustaining[pick] = false;
        _chord_slot[pick] = -1;
    }
    // Material before the strike: BodyVoice::trigger() recomputes the mode
    // bank, and it should do that with the chord that is being struck rather
    // than with the one the last control tick saw. No-op on VoiceT.
    _voices[pick].set_material_character(_material_char);
    _voices[pick].set_vel(vel);
    _voices[pick].trigger(pitch_to_hz(pitch_norm));   // pitch LATCHED here
}

template <class V>
void SynthEngineT<V>::_adjust_surface() {
    int m = 0, worst = -1;
    bool has[kMaxChord] = { false, false, false, false };
    for (int v = 0; v < kVoices; ++v)
        if (_sustaining[v]) {
            ++m;
            const int s = _chord_slot[v];
            if (s >= 0 && s < kMaxChord) has[s] = true;
            if (worst < 0 || _chord_slot[v] > _chord_slot[worst]) worst = v;
        }
    if (m == 0) return;                    // no surface -> nothing to grow
    // How much of the requested chord this engine can actually hold. At
    // kVoices >= kMaxChord this is always _chord_n (set_chord clamps to
    // kMaxChord), so SYNTH and WAVE are untouched -- the render gates check
    // that byte for byte. Below it, the clamp is what stops the bloom from
    // chasing a slot it has no voice for: without it, every control tick
    // found a missing slot, stole the voice already sustaining and retriggered
    // it, measured at 501 triggers/second on a one-voice engine (see
    // tests/test_synth_engine_voice_count.cpp). It also restores the
    // invariant _do_trigger's steal comment relies on -- "bloom implies
    // m < _chord_n <= kVoices".
    const int want = _chord_n < kVoices ? _chord_n : kVoices;
    // Equal-power compensation follows the notes that will actually sound,
    // not the notes that were asked for -- same value at kVoices >= kMaxChord.
    _vel_now = 1.f / std::sqrt(static_cast<float>(want));
    if (want > m) {                        // bloom: add the first missing slot
        for (int s = 0; s < want; ++s)
            if (!has[s]) { _do_trigger(_chord[s], _vel_now, s); break; }
    } else if (m > want && worst >= 0 && _chord_slot[worst] >= _chord_n) {
        _voices[worst].set_sustaining(false);   // collapse: drop the top slot
        _sustaining[worst] = false;
        _chord_slot[worst] = -1;
    }
}

template <class V>
void SynthEngineT<V>::_update_control() {
    // chord surface follows COLOR live (spec §2 amendment): one voice per
    // control tick blooms in / the top slot collapses out — never while a
    // stab is still strewing in
    if (_flow && !_hold && _pending_n == 0) _adjust_surface();

    // COLOR-as-material (spec §7). This function is the control tick --
    // process() only calls it when _ctrl_ctr runs out, once per
    // kCtrlInterval samples -- so the derivation is off the per-sample path
    // by construction whatever rate set_chord() is called at, and the dirty
    // flag keeps it off the per-tick path as well (see set_chord).
    if (_material_dirty) {
        _material_char = chord_character(_chord, _chord_n);
        _material_dirty = false;
    }

    const float attack_s = clampf(_attack_ratio * _cycle_s, kAttackFloorS, kDecayMaxS);
    const float decay_s  = clampf(_decay_ratio  * _cycle_s, kDecayMinS,  kDecayMaxS);

    const float timbre = _targets[LANE_SOURCE];       // pad 1 = TIMBRE
    const float off    = _filt_amt < 0.f ? kFiltLeftScale * _filt_amt : _filt_amt;
    const float n_raw  = _targets[LANE_SIZE] + off;          // pad 2 = FILTER + trim
    const float cutoff = filter_hz(n_raw);                   // clamps 0..1 internally
    _filt_gain = clampf(1.f + n_raw / kFiltFadeRange, 0.f, 1.f);
    const float width  = clampf(_targets[LANE_MOTION], 0.f, 1.f);
    const float dt_s   = kCtrlInterval / _sr;

    for (int v = 0; v < kVoices; ++v) {
        V& vc = _voices[v];
        vc.set_env_times(attack_s, decay_s);
        vc.set_morph(timbre);
        vc.set_detune_cents(_detune_spread_ct);
        vc.set_sub_level(_sub_level);
        vc.set_cutoff_hz(cutoff);
        vc.set_resonance(_resonance);
        // kPanFan is a FAN, not a pan law: it spreads a four-voice chord
        // across the field, and the four slots balance each other out. A
        // ONE-voice engine (BodyVoice::
        // kEngineVoices == 1) would permanently take slot 0, which is hard
        // LEFT -- at MOTION 1 the right channel gets exactly zero, and at the
        // boot width it is about 7.7 dB down. Reported by ear before any test
        // looked: nothing in the suite compares l against r, only run against
        // run (see the balance test in tests/test_body_engine.cpp).
        //
        // Centre is the only sensible slot for a single voice, and it costs
        // nothing else: set_drift_amount(width) below is untouched, so MOTION
        // still moves a BODY deck -- it wanders around the centre instead of
        // being nailed to one side of it.
        //
        // kVoices == 4 keeps kPanFan[v] exactly, so SYNTH and WAVE are
        // bit-identical (ctrl_identity, wave_formant_sweep).
        //
        // The guard covers kVoices <= 1 and nothing else, deliberately. Two
        // and three voices would ALSO be off-centre under this table (2 gives
        // -1/+1, which balances; 3 gives -1/+1/-0.5, which does not), but no
        // such engine exists, and a fan for a voice count nobody has built is
        // a guess about music, not a fix. If one ever appears, this is the
        // line to revisit -- and the balance test in tests/test_body_engine.cpp
        // is the shape to copy for it.
        vc.set_pan((kVoices > 1 ? kPanFan[v] : 0.f) * width);
        vc.set_drift_amount(width);
        vc.set_material_character(_material_char);   // no-op on VoiceT
        // CHOKE's palm mute. set_hold() above records _hold and demotes the
        // surface, but never pushed the flag to the voices -- so BodyVoice::
        // set_hold was dead code and a BODY deck ignored CHOKE entirely
        // (found by ear during the Task 12 listening pass: the palm mute had
        // "no measurable effect"). Three branches inside BodyVoice were
        // unreachable because of it: the strings' 0.02 damping, the mode
        // bank's zeroed ring, and the energy follower's faster fall.
        //
        // Pushed here rather than inside set_hold() so a voice allocated
        // WHILE the hold is on still gets it -- same reason every other
        // parameter on this list is pushed per tick instead of on its edge.
        // A no-op on VoiceT (synth/voice.h), so SYNTH and WAVE are
        // bit-identical (ctrl_identity, wave_formant_sweep).
        vc.set_hold(_hold);
        vc.update_control(dt_s);
    }

    // surface voices track their chord slot; vel follows the chord size
    for (int v = 0; v < kVoices; ++v)
        if (_sustaining[v]) {
            const int s = _chord_slot[v];
            if (s >= 0 && s < _chord_n)
                _voices[v].set_pitch_hz(pitch_to_hz(_chord[s]));
            _voices[v].set_vel(_vel_now);
        }
}

template <class V>
void SynthEngineT<V>::process(float& outL, float& outR) {
    if (_pending_n > 0) {                        // strewed stab tones land here
        int w = 0;
        for (int i = 0; i < _pending_n; ++i) {
            if (--_pending[i].ctr <= 0)
                _do_trigger(_pending[i].pitch, _vel_now, _pending[i].slot);
            else
                _pending[w++] = _pending[i];
        }
        _pending_n = w;
    }
    if (_auto_pending) {
        _auto_pending = false;
        trigger_chord(_chord, _chord_n);         // full chord at current COLOR
    }
    // Part::_ctrl_ctr (engine/parts/part.h) is phase-locked to this counter:
    // both init to 0 and advance once per call to their respective
    // process(), so they tick on the same samples as long as this engine
    // stays active. Do not change this counter's init value or the
    // decrement-then-compare idiom without re-reading that comment -- it
    // documents what depends on the phase this establishes, including a
    // permanent offset this counter accrues while inactive that no test
    // can realistically observe.
    if (--_ctrl_ctr <= 0) {
        _ctrl_ctr = kCtrlInterval;
        _update_control();
    }
    const float gain = _level.process(_targets[LANE_LEVEL] * _filt_gain) * kVoiceGain;
    float l = 0.f, r = 0.f;
    for (auto& v : _voices) v.process(l, r);
    outL = l * gain;
    outR = r * gain;
}

template <class V>
void SynthEngineT<V>::set_attack(float n) {
    _attack_ratio = 0.002f * std::pow(250.f, clampf(n, 0.f, 1.f));
}

template <class V>
void SynthEngineT<V>::set_decay(float n) {
    _decay_ratio = 0.1f * std::pow(80.f, clampf(n, 0.f, 1.f));
}

template <class V>
void SynthEngineT<V>::set_resonance(float n) { _resonance = clampf(n, 0.f, 1.f); }
template <class V>
void SynthEngineT<V>::set_sub(float n)       { _sub_level = clampf(n, 0.f, 1.f); }
template <class V>
void SynthEngineT<V>::set_filt(float n) { _filt_amt = clampf(n, -1.f, 1.f); }

template <class V>
void SynthEngineT<V>::set_detune(float n) {
    _detune_spread_ct = clampf(n, 0.f, 1.f) * kDetuneCeilCt;
}

template <class V>
int SynthEngineT<V>::active_voices() const {
    int n = 0;
    for (const auto& v : _voices)
        if (v.active()) ++n;
    return n;
}

template <class V>
float SynthEngineT<V>::voice_env(int v) const {
    if (v < 0 || v >= kVoices) return 0.f;
    return _voices[v].env_value();
}

template class spky::SynthEngineT<spky::VoiceT<spky::MorphOsc>>;
template class spky::SynthEngineT<spky::VoiceT<spky::WtOsc>>;
template class spky::SynthEngineT<spky::BodyVoice>;

// Compile-time proof (fix round 1, Task 5 review): SynthEngineT actually
// works at a voice count other than 4. See detail::VoiceCountProbe in
// synth_engine.h, which is guarded the same way -- only the tests target
// defines SPKY_TESTING. BodyEngine now covers that with a real voice, but
// the probe is what lets a test count TRIGGERS, which no real voice exposes.
#ifdef SPKY_TESTING
template class spky::SynthEngineT<spky::detail::VoiceCountProbe>;
#endif
