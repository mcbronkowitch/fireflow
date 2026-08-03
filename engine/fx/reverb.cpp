#include "fx/reverb.h"
#include "Utility/dsp.h"
#include "util/math.h"
#include <cmath>

using namespace spky;

namespace {
constexpr int kCtrlInterval = 96;          // engine control-rate raster
constexpr uint32_t kRngSeed = 0x0BE21D5u;  // fixed: bit-deterministic renders
constexpr float kModRate = 0.5f;           // internal LFO speed; DIFFUSION weakly couples the amount
// ~80 Hz one-pole low-cut inside the loop: keeps the >100% bloom from
// accumulating DC / low-mid mud (parasites' anti-DC offset, same value).
constexpr float kHpPin = 0.01f;
constexpr float kInputGain = 0.5f;         // L+R sum -> mono average into the room
// The self-oscillating bloom (decay > 1.0) plateaus near digital full scale
// at the core's output taps (they carry 2x the in-loop signal, plus Hermite
// overshoot under depth modulation). Trim the wet-only room -8 dB so the
// bloom leaves headroom at the master sum — the M4.6 limiter is a ceiling,
// not a mixer; don't lean on it. Ear-tunable in [0.40, 0.50]; kept at the
// low end to hold the ambient_wash showcase's bloom clear of the ceiling.
constexpr float kWetGain = 0.40f;
// Bloom trim. Past unity loop gain the room stops being driven by the send and
// starts being driven by its own saturation: the return plateaus at a level
// the loop picks, not one the player set (measured +18 dB over the send for a
// quiet source at DECAY 1.0), and arrives at the master with no headroom left
// for DRIVE. Trimming ONLY the blooming leg keeps every ear-tuned level below
// it exactly where it was -- kWetGain and MIX are untouched, this multiplies
// on top and is exactly 1.0 at DECAY <= kBloomTrimFrom.
constexpr float kBloomTrimFrom = 0.80f;   // trim starts here, inactive below
constexpr float kBloomTrim     = 0.708f;  // -3 dB at the stop (ear-tunable)
constexpr float kTrimSmoothS   = 0.020f;  // return-gain glide, no step on a jump
}

void AmbientReverb::init(float sample_rate) {
    _sr = sample_rate;
    _ctrl = 0;
    _trim_coef = 1.f - std::exp(-1.f / (kTrimSmoothS * _sr));
    _verb.Init(_buffer, kRngSeed);
    _verb.set_input_gain(kInputGain);
    _verb.set_hp(kHpPin);
    _verb.set_mod_rate(kModRate);
    set_size(0.6f);     // boot defaults (spec: audible, nothing screams,
    set_decay(0.55f);   // nothing self-oscillates)
    set_tone(0.5f);
    set_diffusion(0.7f);          // coeff 0.63 ~= the old stock 0.625 room
    set_diffuser_mod_depth(0.5f); // wash smear default (~56)
    set_mod_depth(0.2f);          // gentle tail wobble default (~the calm 22.5)
    _bloom_trim = _bloom_trim_target;   // boot at the settled value, no ramp-in
    _verb.Prepare();
}

void AmbientReverb::clear() {
    _verb.Clear();
    _ctrl = 0;   // refresh the LFO slopes on the next process()
    _bloom_trim = _bloom_trim_target;   // waking room starts at its settled gain
}

void AmbientReverb::set_size(float norm) {
    // parasites mapping: keep the room inside the tuned sweet range
    _verb.set_size(0.05f + 0.94f * clampf(norm, 0.f, 1.f));
}

void AmbientReverb::set_decay(float norm) {
    // Linear to 1.0 at norm 0.9; the top 10% of travel pushes past unity
    // into the soft-limited bloom, reaching 1.05 at the stop. (Ear-tunable knee.)
    //
    // The bloom leg is its own ramp rather than the same 1/0.9 slope clamped
    // at 1.05: that slope hit the cap at norm 0.945, so the top 5.5% of the
    // knob was dead -- 0.95, 0.97 and 1.0 all rang bit-identically. Giving the
    // bloom the full 0.9..1.0 travel makes the region playable at 1/2.2 the
    // sensitivity. Patches saved in that band shift (0.95 was 1.05, now 1.025).
    norm = clampf(norm, 0.f, 1.f);
    _verb.set_decay(norm <= 0.9f ? norm * (1.f / 0.9f)
                                 : 1.0f + (norm - 0.9f) * 0.5f);
    // ...and trim the return over the same upper leg (see kBloomTrim).
    _bloom_trim_target =
        norm <= kBloomTrimFrom
            ? 1.f
            : 1.f - (1.f - kBloomTrim) * (norm - kBloomTrimFrom)
                                       / (1.f - kBloomTrimFrom);
}

void AmbientReverb::set_tone(float norm) {
    float fc = daisysp::fmap(clampf(norm, 0.f, 1.f), 500.f, 16000.f,
                             daisysp::Mapping::LOG);
    // exact one-pole coefficient for that cutoff (control-rate libm is fine)
    _verb.set_lp(1.f - std::exp(-TWO_PI * fc / _sr));
}

void AmbientReverb::set_diffusion(float norm) {
    norm = clampf(norm, 0.f, 1.f);
    // Pure density now: the allpass coefficient only. The two LFO paths that
    // used to ride along with DIFFUSION are on their own knobs
    // (set_diffuser_mod_depth, set_mod_depth) so each can be tested alone.
    // applied instantly like decay/tone (only SIZE smooths, for the Doppler)
    _verb.set_diffusion(0.90f * norm);
}

void AmbientReverb::set_diffuser_mod_depth(float norm) {
    // SMEAR knob: the input-diffuser LFO (ap1..ap4). This is what melts
    // discrete slap-echoes into a dense wet wash. 0 = static diffusers.
    _verb.set_diffuser_mod_amount(clampf(norm, 0.f, 1.f) * 0.25f * 450.f);
}

void AmbientReverb::set_mod_depth(float norm) {
    // MOD knob: the tail-delay LFO wobble (del1/del2 + loop APs). 0 = a still
    // tail (no pitch-vibrato); 1 = the old maximum. Ear-tuned in Rack.
    _verb.set_mod_amount(clampf(norm, 0.f, 1.f) * 0.25f * 450.f);
}

void AmbientReverb::process(float in_l, float in_r, float& out_l, float& out_r) {
    if (_ctrl == 0) {
        _verb.Prepare();
        _ctrl = kCtrlInterval;
    }
    --_ctrl;
    _bloom_trim += _trim_coef * (_bloom_trim_target - _bloom_trim);
    out_l = in_l;
    out_r = in_r;
    _verb.Process(&out_l, &out_r);
    const float g = kWetGain * _bloom_trim;
    out_l *= g;
    out_r *= g;
}
