#include "body/ks_string.h"

#include <cmath>

#include "Utility/dsp.h"

namespace spky {

using daisysp::fclamp;
using daisysp::fonepole;

namespace {
// daisysp::String's own constants, by the names it uses them under.
constexpr float kOneTwelfth = 1.f / 12.f;
constexpr float kTwoPi      = 2.f * 3.1415926535897932f;
} // namespace

void KsString::init(float sample_rate, uint32_t seed)
{
    _sample_rate = sample_rate;
    _seed        = seed;
    _string.Init();
    _stretch.Init();
    _crossfade.Init();

    // The reference's Init() defaults, so an uninitialised string is the same
    // string here as there. Set BEFORE reset(), which recomputes from them.
    _freq_hz      = 440.f;
    _brightness   = 0.5f;
    _damping      = 0.8f;
    _nonlinearity = 0.1f;

    reset();
}

void KsString::reset()
{
    _string.Reset();
    _stretch.Reset();
    _iir_damping.Init();
    _dc_blocker.Init(_sample_rate);
    _rng.seed(_seed);   // same string, same noise -- see the header note

    _dispersion_noise = 0.f;
    _curved_bridge    = 0.f;
    _out_sample[0] = _out_sample[1] = 0.f;
    _src_phase                      = 0.f;

    // OnePole::Init() above cleared the coefficients SetFrequency wrote, so
    // they have to be written again -- and only recompute() knows how. A
    // reset string is the same string, ready to ring on the next sample; it
    // is not one waiting for a set_params that the caller has no reason to
    // send (the parameters did not change, so the dirty check would swallow
    // it anyway).
    _dirty = true;
    recompute();
}

void KsString::set_params(float freq_hz, float brightness, float damping,
                          float nonlinearity)
{
    if (!_dirty && freq_hz == _freq_hz && brightness == _brightness
        && damping == _damping && nonlinearity == _nonlinearity) {
        return;
    }
    _freq_hz      = freq_hz;
    _brightness   = brightness;
    _damping      = damping;
    _nonlinearity = nonlinearity;
    recompute();
}

// Everything below is daisysp::String::ProcessInternal's parameter block,
// transcribed in its original order. The order matters in two places and both
// are easy to "tidy" into a bug: damping_cutoff is computed from the
// UNMODIFIED brightness and only afterwards does the infinite-decay crossfade
// raise brightness and damping_cutoff, and `ratio` then uses the RAISED
// damping_cutoff while noise_filter uses the RAISED brightness.
void KsString::recompute()
{
    ++_updates;
    _dirty = false;

    // SetFreq / SetNonLinearity / SetBrightness / SetDamping clamping.
    const float frequency    = fclamp(_freq_hz / _sample_rate, 0.f, 0.25f);
    const float nonlinearity = fclamp(_nonlinearity, 0.f, 1.f);
    const float damping      = fclamp(_damping, 0.f, 1.f);
    float       brightness   = fclamp(_brightness, 0.f, 1.f);

    // The reference dispatches on the SIGN of the stored amount: <= 0 means
    // the curved bridge (with the amount negated, so zero stays zero), > 0
    // means dispersion. Clamped to 0..1 here, so zero is the curved bridge.
    _dispersion = nonlinearity > 0.f;

    float delay = 1.0f / frequency;
    delay       = fclamp(delay, 4.f, kDelayLineSize - 4.0f);

    float src_ratio = delay * frequency;
    if (src_ratio >= 0.9999f) {
        // Above ~11.7 Hz the linear interpolation upsampler is not needed.
        // Assigning _src_phase here rather than per sample is safe: with
        // src_ratio 1.0 the reference's per-sample `_src_phase += 1; if (> 1)
        // -= 1` holds it at exactly 1.0 forever, so both schedules settle on
        // the same value from the first sample onward.
        _src_phase = 1.0f;
        src_ratio  = 1.0f;
    }
    _src_ratio = src_ratio;

    float damping_cutoff
        = fmin(12.0f + damping * damping * 60.0f + brightness * 24.0f, 84.0f);
    float damping_f
        = fmin(frequency * powf(2.f, damping_cutoff * kOneTwelfth), 0.499f);

    // Crossfade to infinite decay.
    if (damping >= 0.95f) {
        const float to_infinite = 20.0f * (damping - 0.95f);
        brightness += to_infinite * (1.0f - brightness);
        damping_f += to_infinite * (0.4999f - damping_f);
        damping_cutoff += to_infinite * (128.0f - damping_cutoff);
    }

    _iir_damping.SetFrequency(damping_f);

    const float ratio                = powf(2.f, damping_cutoff * kOneTwelfth);
    const float damping_compensation = 1.f - 2.f * atanf(1.f / ratio) / kTwoPi;

    // The reference applies damping_compensation to `delay` inside the
    // per-sample block, before any nonlinearity modulates it. Folding it in
    // here is the same multiplication, hoisted.
    _base_delay = delay * damping_compensation;

    _stretch_point = nonlinearity * (2.0f - nonlinearity) * 0.225f;
    float stretch_correction = (160.0f / _sample_rate) * delay;
    stretch_correction       = fclamp(stretch_correction, 1.f, 2.1f);

    // Kept as two factors, deliberately. The reference writes
    //   main_delay = delay - ap_delay * (0.408f - stretch_point * 0.308f)
    //                                 * stretch_correction;
    // and `*` is left-associative, so it rounds as
    //   (ap_delay * k) * correction.
    // Folding k and correction together here would be one multiply cheaper
    // per sample and would round differently -- caught by
    // tests/test_ks_string.cpp, which diverged in the last bit from the
    // first delay-line round trip onward. A multiply is not what this port
    // is trying to save.
    _stretch_k          = 0.408f - _stretch_point * 0.308f;
    _stretch_correction = stretch_correction;

    const float noise_amount_sqrt
        = nonlinearity > 0.75f ? 4.0f * (nonlinearity - 0.75f) : 0.0f;
    _noise_amount = noise_amount_sqrt * noise_amount_sqrt * 0.1f;
    _noise_filter = 0.06f + 0.94f * brightness * brightness;

    const float bridge_curving_sqrt = nonlinearity;
    _bridge_curving = bridge_curving_sqrt * bridge_curving_sqrt * 0.01f;

    _ap_gain = -0.618f * nonlinearity / (0.15f + fabsf(nonlinearity));
}

float KsString::process(float in)
{
    _src_phase += _src_ratio;
    if (_src_phase > 1.0f) {
        _src_phase -= 1.0f;

        float delay = _base_delay;
        float s     = 0.0f;

        if (_dispersion) {
            // Reference: rand() * (1/RAND_MAX) - 0.5. Same range, owned
            // state -- header note.
            const float noise = _rng.next_unipolar() - 0.5f;
            fonepole(_dispersion_noise, noise, _noise_filter);
            delay *= 1.0f + _dispersion_noise * _noise_amount;

            const float ap_delay = delay * _stretch_point;
            const float main_delay
                = delay - ap_delay * _stretch_k * _stretch_correction;
            if (ap_delay >= 4.0f && main_delay >= 4.0f) {
                s = _string.Read(main_delay);
                s = _stretch.Allpass(s, ap_delay, _ap_gain);
            } else {
                s = _string.ReadHermite(delay);
            }
        } else {
            delay *= 1.0f - _curved_bridge * _bridge_curving;
            s = _string.ReadHermite(delay);

            const float value = fabsf(s) - 0.025f;
            const float sign  = s > 0.0f ? 1.0f : -1.5f;
            _curved_bridge    = (fabsf(value) + value) * sign;
        }

        s += in;
        s = fclamp(s, -20.f, +20.f);
        s = _dc_blocker.Process(s);
        s = _iir_damping.Process(s);
        _string.Write(s);

        _out_sample[1] = _out_sample[0];
        _out_sample[0] = s;
    }

    _crossfade.SetPos(_src_phase);
    return _crossfade.Process(_out_sample[1], _out_sample[0]);
}

} // namespace spky
