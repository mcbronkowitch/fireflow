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
// Return limiter. Past unity loop gain the room stops being driven by the send
// and starts being driven by its own saturation, so it settles on a level the
// LOOP picks, not one the player set: measured at DECAY 1.0, sends of 0.05,
// 0.15 and 0.50 all plateau at 0.26..0.27. Whatever else that is, it means the
// room can hand the master a level nobody asked for, and it stacks on top of
// however hot the decks already are.
//
// So the return gets its own ceiling. Not clever, deliberately: two earlier
// attempts tried to be. A DECAY-tied trim failed because knob position is not
// level, and a send-relative ratio failed because the bus does not care how
// loud you played -- it clips on the sum. A plain ceiling is the only one of
// the three that bounds what actually reaches the master.
//
// Cheap by construction: a one-pole peak follower and one multiply per sample.
// The divide that turns the peak into a gain is taken on the control raster
// (every kCtrlInterval samples) and glided in between, so the per-sample cost
// is two adds and two multiplies.
// A HARD ceiling was tried first and it pumps, badly. Holding the return at
// exactly kWetKnee means the gain has to cancel every fluctuation of a wash
// that fluctuates by nature, so it rode ordinary material (send 0.30 at DECAY
// 0.85) down 2 dB while breathing 34 % peak-to-peak at about 1 Hz. Audible, and
// on a wash it reads as dirt rather than as level.
//
// So the knee is soft and the ratio finite: above kWetKnee the return still
// grows, just four times more slowly. The gain then only has to attenuate a
// quarter of each fluctuation, which cuts the breathing by the same factor,
// and the return is still bounded -- a 1.12 peak lands at 0.66 instead of
// running into the master at full scale.
constexpr float kWetKnee    = 0.45f;  // where the return starts being held back
constexpr float kWetRatio   = 0.15f;  // ~7:1 above it (ear-tunable)
// And it is SLOW -- seconds, not milliseconds. Measured: the gain ride is not
// free, it adds modulation that no fixed volume setting explains, and at
// limiter speeds that residue lands at -25 dB, right where the master's own
// distortion sits. Stretching the constants to seconds pushes it to -40 dB,
// about 1 %, which is the practical floor for riding a wash at all. Anything
// faster is audible as breathing, and on a reverb it reads as dirt, not level.
constexpr float kPkRelS     = 4.0f;   // peak bleeds off over seconds
constexpr float kLimDownS   = 1.0f;   // ...and the ride is slower still
constexpr float kLimUpS     = 6.0f;
}

void AmbientReverb::init(float sample_rate) {
    _sr = sample_rate;
    _ctrl = 0;
    _pk_rel   = 1.f - std::exp(-1.f / (kPkRelS * _sr));
    _lim_down = 1.f - std::exp(-1.f / (kLimDownS * _sr));
    _lim_up   = 1.f - std::exp(-1.f / (kLimUpS * _sr));
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
    _wet_peak = 0.f;
    _lim_gain = 1.f;   // boot wide open; nothing has overshot yet
    _lim_target = 1.f;
    _verb.Prepare();
}

void AmbientReverb::clear() {
    _verb.Clear();
    _ctrl = 0;   // refresh the LFO slopes on the next process()
    // An emptied room holds no level: forget the ride rather than let the last
    // bloom duck the first note of the next one.
    _wet_peak = 0.f;
    _lim_gain = 1.f;
    _lim_target = 1.f;
}

void AmbientReverb::set_size(float norm) {
    // parasites mapping: keep the room inside the tuned sweet range
    _verb.set_size(0.05f + 0.94f * clampf(norm, 0.f, 1.f));
}

// Knob -> loop gain. Two legs: everything up to kUnityAt is the ordinary room,
// 0 to 100%; above it the room is over unity and blooms, out to kMaxGain.
//
// Both numbers moved because the bloom used to be unreachable. Measured with
// the stock TONE, the room does not begin to sustain itself until about 105%
// -- and 105% was the knob's MAXIMUM, so self-oscillation existed only at the
// very last point of the travel and its speed could not be played at all.
// Unity now sits at 0.80 and the top reaches 110%, so the blooming region owns
// the last fifth of the travel instead of a single point at the stop.
//
// The top was briefly 120%, matching FLUX FB, and that was wrong. Measured on
// a real patch: the settled bloom is the same at 110% and at 120% (return 0.636
// vs 0.641), so the extra span buys no sound -- but it costs headroom exactly
// where it hurts, during the 2-3 s swell, where it added 1.9 dB and pushed a
// master that had been sitting just under its limit into riding continuously.
// The core stays bounded far past this (checked to 130%); the limit is musical,
// not numerical.
//
// This remaps saved DECAY values. The ordinary range is slightly compressed
// (unity was at 0.90) and anything above it means considerably more bloom.
float AmbientReverb::decay_loop_gain(float norm) {
    constexpr float kUnityAt = 0.80f;   // knob position of 100% (ear-tunable)
    constexpr float kMaxGain = 1.10f;   // 110% at the stop (ear-tunable)
    norm = clampf(norm, 0.f, 1.f);
    return norm <= kUnityAt
               ? norm * (1.f / kUnityAt)
               : 1.f + (norm - kUnityAt) * ((kMaxGain - 1.f) / (1.f - kUnityAt));
}

void AmbientReverb::set_decay(float norm) {
    _verb.set_decay(decay_loop_gain(norm));
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
    norm = clampf(norm, 0.f, 1.f);
    _diffuser_mod_norm = norm;
    _verb.set_diffuser_mod_amount(norm * 0.25f * 450.f);
}

void AmbientReverb::set_mod_depth(float norm) {
    // MOD knob: the tail-delay LFO wobble (del1/del2 + loop APs). 0 = a still
    // tail (no pitch-vibrato); 1 = the old maximum. Ear-tuned in Rack.
    norm = clampf(norm, 0.f, 1.f);
    _mod_norm = norm;
    _verb.set_mod_amount(norm * 0.25f * 450.f);
}

void AmbientReverb::process(float in_l, float in_r, float& out_l, float& out_r) {
    if (_ctrl == 0) {
        _verb.Prepare();
        _ctrl = kCtrlInterval;
    }
    --_ctrl;
    out_l = in_l;
    out_r = in_r;
    _verb.Process(&out_l, &out_r);
    out_l *= kWetGain;
    out_r *= kWetGain;

    // True peak, held and bled off: one compare, one multiply-add. A smoothed
    // attack was tried and is wrong here -- between the crests of a 220 Hz tone
    // a one-pole reads about 70 % of the real peak, which let the ceiling be
    // overshot by 3 dB. Catching the crest exactly is both correct and cheaper.
    const float pk = std::fmax(std::fabs(out_l), std::fabs(out_r));
    if (pk > _wet_peak) _wet_peak = pk;
    else _wet_peak += _pk_rel * (pk - _wet_peak);
    // The divide only on the control raster; glided in between.
    if (_ctrl == 0) {
        // Soft knee: everything under it passes at exactly 1.0; above it the
        // return is allowed to keep growing at kWetRatio of its natural rate.
        if (_wet_peak > kWetKnee) {
            const float allowed =
                kWetKnee + (_wet_peak - kWetKnee) * kWetRatio;
            _lim_target = allowed / _wet_peak;
        } else {
            _lim_target = 1.f;
        }
    }
    _lim_gain += (_lim_target < _lim_gain ? _lim_down : _lim_up)
                 * (_lim_target - _lim_gain);
    out_l *= _lim_gain;
    out_r *= _lim_gain;
}
