#include "body/body_voice.h"

#include <cmath>

#include "mod/rng.h"
#include "util/fast_sin.h"
#include "util/fast_tanh.h"
#include "util/math.h"

namespace spky {

namespace {
constexpr float kDriftDetuneCt = 3.f;     // micro-detune drift ceiling (+/-3 ct)
constexpr float kDriftPanAmt   = 0.25f;   // pan drift ceiling around the fan slot

// FLOW's output ceiling -- see process() for what it bounds and why it sits
// there rather than inside a resonator. TUNING MATERIAL: it is the one number
// a listening pass turns. Lower means the loud corners compress harder and
// distort sooner; higher means more of the runaway survives. Measured worst
// FLOW peak against it: 0.708 at 1.0, 0.283 at 0.4, 0.142 at 0.2.
constexpr float kFlowSatCeil = 0.4f;
constexpr float kFlowSatInv  = 1.f / kFlowSatCeil;

// FILTER's loudness tilt -- see set_cutoff_hz for what it repairs and why the
// numbers are what they are. TUNING MATERIAL, both of them.
constexpr float kBrightTiltDb    = 17.f;   // dB down at brightness 0
constexpr float kBrightTiltShape = 3.f;    // documented; the cube is written out
constexpr float kLn10Over20      = 0.11512925f;   // dB -> natural log

// Mode-bank stretch with COLOR at minimum, i.e. the direction DETUNE spreads
// the bank in before COLOR bends it (see _apply_params). TUNING MATERIAL --
// no test pins this value, only the property that it is non-zero.
//
// Where 0.3 comes from, so it can be argued with. Two ends bound it:
//   - big enough that DETUNE is audibly doing something at COLOR 0. 0.3
//     gives stiffness 0.12, which puts the 24th partial about 3.2x above its
//     harmonic position -- unmistakably a bell rather than a string.
//   - small enough that COLOR keeps most of the compressed side. character
//     runs to -1, so the sum reaches -0.7: 70 % of the drum/plate range is
//     still reachable, and the deepest real chord (a quartal voicing, -0.67)
//     still lands at -0.37, comfortably compressed.
// It is deliberately NOT 1.0 (the pre-Task-8b behaviour, where DETUNE alone
// drove the bank): that would leave COLOR able to subtract only.
constexpr float kBaseStretch = 0.3f;

// BODY reads DETUNE four times as wide as SYNTH does: spec §5, "inharmonicity
// amount: string spread x ~4 (up to ~140 ct) AND mode-bank stretch -- one
// 'how broken is this material' axis". SynthEngineT hands every engine the
// same 0..kDetuneCeilCt = 0..35 ct spread; this is where BODY's own rail
// comes from, and it is what makes the /kDetuneMaxCt divisor below reach 1.0
// instead of topping out at a quarter of its range.
//
// It is applied here rather than in set_detune_cents() so that
// detune_cents() -- which SynthEngineT::applied_detune_ct() and the shared
// part-engine contract read -- keeps reporting the spread the ENGINE pushed,
// the same number on every engine.
constexpr float kDetuneScale = 4.f;
constexpr float kDetuneMaxCt = 140.f;   // = kDetuneCeilCt * kDetuneScale
} // namespace

void BodyVoice::init(float sample_rate, uint32_t seed) {
    _sr = sample_rate;
    // Distinct seeds, not the same one twice: the pair is two strings a few
    // cents apart, and identical dispersion noise on both would move them
    // together instead of letting them beat. The offsets are arbitrary odd
    // constants -- only their distinctness matters.
    _str_a.init(sample_rate, seed ^ 0x9E3779B9u);
    _str_b.init(sample_rate, seed ^ 0x85EBCA6Bu);
    _bank.init(sample_rate);
    _exciter.init(seed, sample_rate);

    // Same drift-character derivation as VoiceT::init: a LOCAL Rng, drawn
    // from once at construction time, never stored -- the per-sample path
    // has no RNG of its own to keep bit-reproducible.
    Rng rng;
    rng.seed(seed);
    _drift_pan_hz    = 0.05f + 0.15f * rng.next_unipolar();   // 0.05..0.2 Hz
    _drift_det_hz    = 0.05f + 0.15f * rng.next_unipolar();
    _drift_pan_phase = rng.next_unipolar();
    _drift_det_phase = rng.next_unipolar();

    _drift_ct_cur = 0.f;
    _follower = 0.f;
    _peak = 0.f;
    _hold_samples = 0;
    _vel = _vel_target;

    set_pitch_hz(220.f);
    update_control(0.f);
}

void BodyVoice::trigger(float freq_hz) {
    set_pitch_hz(freq_hz);                 // applies immediately, same as VoiceT
    _exciter.strike(_vel_target);
    _hold_samples = kMinHoldSamples;        // quiet strikes are not stolen instantly
}

void BodyVoice::set_sustaining(bool on) {
    _sustaining = on;
    _exciter.set_continuous(on);            // FLOW bows instead of striking
}

void BodyVoice::set_pitch_hz(float freq_hz) {
    _freq = freq_hz < 0.f ? 0.f : freq_hz;
    _apply_params();
}

void BodyVoice::set_vel(float v) {
    _vel_target = clampf(v, 0.f, 1.f);
    if (!active()) _vel = _vel_target;      // idle: snap -- nothing can click
}

void BodyVoice::set_morph(float m)          { _matl = m; }
void BodyVoice::set_detune_cents(float ct)  { _detune_ct = ct; }
void BodyVoice::set_sub_level(float n)      { _sub = clampf(n, 0.f, 1.f); }
void BodyVoice::set_pan(float pan)          { _pan_base = clampf(pan, -1.f, 1.f); }
void BodyVoice::set_drift_amount(float a)   { _drift_amt = clampf(a, 0.f, 1.f); }
void BodyVoice::set_hold(bool on)           { _hold = on; }
void BodyVoice::set_excitation(float x)     { _excitation = x; }
// Clamped even though spky::chord_character already bounds its result: this
// setter is the voice's edge of the contract, and the mode bank's compressed
// side has a collapse point a caller must not be able to walk into.
void BodyVoice::set_material_character(float c) { _material_char = clampf(c, -1.f, 1.f); }

// ATTACK is exciter length, DECAY is damping (spec §5).
void BodyVoice::set_env_times(float attack_s, float decay_s) {
    _exciter.set_length(attack_s);
    // Longer decay = less damping = longer ring. Curve is tuning material.
    const float d = decay_s / (decay_s + 1.f);
    _damping = d;
}

// RESO is the exciter character, not filter resonance.
void BodyVoice::set_resonance(float n) { _exciter.set_character(n); }

// FILTER's Hz value becomes brightness on a log map over the engine's own
// 60 Hz - 14 kHz rail, plus the loudness tilt that brightness alone does not
// produce.
//
// Why the tilt exists. On VoiceT, FILTER drives a real SVF lowpass, so turning
// it down removes energy: measured against its own brightest setting, SYNTH
// runs flat from b = 1 down to b ~ 0.3 and then falls 28 dB in the last
// quarter. On BODY the same control is a TIMBRE parameter -- it re-tunes the
// strings' damping filter and the mode bank -- and a duller resonator is only
// slightly quieter: BODY measured a near-perfect straight line spanning just
// 10.8 dB end to end.
//
// That gap is what made FILT unusable below -0.5. SynthEngineT fades the voice
// out over the last 0.25 of n_raw (kFiltFadeRange), a window sized for an
// engine that is ALREADY ~28 dB down when the fade begins. BODY arrived there
// at -5.7 dB, so the fade had to do the whole job inside 0.2 of knob travel --
// heard as the sound falling off a cliff and vanishing by -0.6.
//
// The tilt adds the missing ~17 dB, weighted toward the dark end so the top of
// the control stays neutral the way it is on every other engine. Both
// constants are TUNING MATERIAL: kBrightTiltDb sets how much quieter the
// darkest setting is, kBrightTiltShape where along the travel it happens
// (1 = a straight line in dB, higher = flatter at the top and steeper at the
// bottom). The 17 dB / cubic pair is the measured starting point, not a law.
void BodyVoice::set_cutoff_hz(float hz) {
    const float lo = std::log(60.f), hi = std::log(14000.f);
    float b = (std::log(hz < 60.f ? 60.f : hz) - lo) / (hi - lo);
    _brightness = clampf(b, 0.f, 1.f);

    // Control rate, like every other derivation in this class -- set_cutoff_hz
    // is called once per engine control tick, and process() stays free of libm.
    const float dark = 1.f - _brightness;
    _bright_gain = std::exp(-kBrightTiltDb * kLn10Over20
                            * std::pow(dark, kBrightTiltShape));
}

void BodyVoice::_apply_params() {
#ifdef SPKY_TESTING
    ++_apply_calls_for_test;
#endif
    // DETUNE on BODY is the engine's spread times kDetuneScale (spec §5).
    // Both halves of the axis read this one value: the string pair below and
    // the bank's stretch amount further down.
    const float spread_ct = _detune_ct * kDetuneScale;      // 0..140 ct

    // Detune splits +/- half the spread (plus drift) across the two strings,
    // exactly as VoiceT does for its oscillator pair. The drift ceiling is
    // NOT scaled -- it is its own +/-3 ct micro-motion, not part of DETUNE.
    const float half = (spread_ct + _drift_ct_cur) * 0.5f;
    const float ratio_a = std::pow(2.f, -half / 1200.f);
    const float ratio_b = std::pow(2.f, +half / 1200.f);

    // Palm mute snaps damping high on both structures.
    const float damp = _hold ? 0.02f : _damping;

    // MATL rides the string's dispersion up as it heads for the bank: the
    // three sound worlds are one physical axis (spec §3). KsString::
    // set_params collapses SetFreq/SetDamping/SetBrightness/SetNonLinearity
    // into one control-rate call; its internal dirty check makes calling it
    // unconditionally on every tick nearly free.
    const float m = clampf(_matl, 0.f, 1.f);
    _str_a.set_params(_freq * ratio_a, _brightness, damp, m);
    _str_b.set_params(_freq * ratio_b, _brightness, damp, m);

    // The bank's stretch is DETUNE times a BASE DIRECTION that COLOR bends,
    // its Q from DECAY, its roll-off from FILTER. All of this is control rate
    // -- ModeBank::set_params is the one place the coefficient math is
    // allowed to run.
    //
    // DETUNE is the AMOUNT of inharmonicity and keeps exactly the curve §5
    // gives it; COLOR is the signed CHARACTER, which way (§7). They MULTIPLY,
    // so "DETUNE = 0 is harmonic and COLOR is inaudible" stays arithmetic
    // rather than a special case somebody can forget to write.
    //
    // kBaseStretch is what stops COLOR at minimum from silencing DETUNE's
    // half of the knob (user decision, 2026-07-26, review round on Task 8b):
    // character is exactly 0 for a one-note chord, so without a base the bank
    // was permanently harmonic with COLOR down -- and at MATL = 1, where the
    // strings are inaudible, DETUNE was a dead control. Spec §5 wants DETUNE
    // to be "one 'how broken is this material' axis" on its own; §7 wants
    // COLOR to choose the direction. A positive base gives both: DETUNE
    // always spreads the bank toward the bell, COLOR shifts which way from
    // there, and amount = 0 still zeroes the whole product.
    const float amount = clampf(spread_ct / kDetuneMaxCt, 0.f, 1.f);
    // Clamped because ModeBank::set_params's contract is -1..+1 and the sum
    // can leave it: only a semitone cluster (character ~ +0.95) saturates,
    // and a chord that broken is already asking for the maximum.
    const float stretch = clampf(kBaseStretch + _material_char, -1.f, 1.f);
    _bank.set_params(_freq, amount * stretch,
                     _hold ? 0.f : _damping, _brightness);

    _exciter.set_freq(_freq);

    // Equal-power blend gains for MATL, computed here so process() carries no
    // square root. Derived quantities live on the control tick.
    _mix_string = std::sqrt(1.f - m);
    _mix_modal  = std::sqrt(m);
}

void BodyVoice::process(float& accL, float& accR) {
    // SUB = 0 hard-gates the bus: bit-exact off (spec §6).
    const float drive = _exciter.process()
                      + (_sub > 0.f ? _excitation * _sub * _sub * 0.5f : 0.f);

    const float string = 0.5f * (_str_a.process(drive) + _str_b.process(drive));
    const float modal  = _bank.process(drive);

    // Equal-power blend along MATL. The two gains are computed in
    // _apply_params, NOT here: MATL is a control-rate parameter.
    const float s_raw = (string * _mix_string + modal * _mix_modal) * _vel;

    // FLOW's bounding nonlinearity. A struck resonator decays; a continuously
    // driven one accumulates until dissipation balances the input, and the
    // gain that balance settles at is the structure's own -- which here spans
    // three orders of magnitude across MATL and DECAY. Measured peak of a
    // single voice before this: 629, i.e. 56 dB over full scale, which does
    // not merely clip, it slams the master limiter and ducks the other deck
    // with it. That is why FLOW read as flat and lifeless whatever the
    // controls said.
    //
    // A ceiling rather than a computed compensation, following M5a's rule:
    // where opening a path lets a value diverge, add the bounding
    // nonlinearity the instrument already has rather than re-imposing a
    // limit. `KsString` already carries one for the same reason (its
    // fclamp at +/-20 inside the loop) -- it simply cannot see the mode bank,
    // which is where the divergence actually lives.
    //
    // STEP takes the raw branch untouched -- not multiplied by anything, so
    // the struck path this engine was tuned on stays bit-identical.
    const float s = _sustaining ? kFlowSatCeil * fast_tanh(s_raw * kFlowSatInv)
                                : s_raw;

    const float mag = s < 0.f ? -s : s;
    if (mag > _peak) _peak = mag;
    if (_hold_samples > 0) --_hold_samples;

    // The FILTER tilt lands AFTER the follower on purpose. active() reads the
    // follower, so tilting before it would make a dark voice look dead and get
    // stolen early -- a note-length change dressed up as a loudness change.
    accL += s * _gain_l * _bright_gain;
    accR += s * _gain_r * _bright_gain;
}

void BodyVoice::update_control(float dt_s) {
    // Energy follower: block peak, decaying. This is what active() reads --
    // there is no envelope to ask (spec §1).
    const float fall = _hold ? 0.5f : 0.92f;
    _follower = _peak > _follower ? _peak : _follower * fall;
    _peak = 0.f;

    // Velocity slew + drift LFOs: copied verbatim from VoiceT::update_control.
    _vel += 0.35f * (_vel_target - _vel);     // ~10 ms at the 96-sample tick

    _drift_pan_phase += _drift_pan_hz * dt_s;
    _drift_pan_phase -= std::floor(_drift_pan_phase);
    _drift_det_phase += _drift_det_hz * dt_s;
    _drift_det_phase -= std::floor(_drift_det_phase);

    const float drift_pan = fast_sin(_drift_pan_phase) * kDriftPanAmt * _drift_amt;
    _drift_ct_cur = fast_sin(_drift_det_phase) * kDriftDetuneCt * _drift_amt;

    // equal-power pan: angle 0..0.25 turns; gl = cos, gr = sin (via fast_sin)
    const float pan = clampf(_pan_base + drift_pan, -1.f, 1.f);
    const float a = (pan + 1.f) * 0.125f;
    _gain_r = fast_sin(a);
    _gain_l = fast_sin(a + 0.25f);

    _apply_params();
}

} // namespace spky
