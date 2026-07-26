#include "body/mode_bank.h"
#include <cmath>

namespace spky {

namespace {

// Ported character-for-character from daisysp::NthHarmonicCompensation
// (lib/DaisySP/Source/PhysicalModeling/resonator.cpp). Rescales f0 so the
// fundamental's perceived pitch stays put as stiffness (stretch) grows,
// instead of drifting. Includes the negative-stiffness branch even though
// this engine's stretch -> stiffness curve (0..0.4) never produces a
// negative value; the reference takes stiffness both ways and this is a
// faithful port of the whole function, not the branch BODY happens to use.
float NthHarmonicCompensation(int n, float stiffness) {
    float stretch_factor = 1.0f;
    for (int i = 0; i < n - 1; ++i) {
        stretch_factor += stiffness;
        if (stiffness < 0.0f) {
            stiffness *= 0.93f;
        } else {
            stiffness *= 0.98f;
        }
    }
    return 1.0f / stretch_factor;
}

} // namespace

void ModeBank::init(float sample_rate) {
    _sr = sample_rate;
    reset();
    _dirty = true;
    _updates = 0;
}

void ModeBank::reset() {
    for (int b = 0; b < kBatches; ++b) _svf[b].reset();
}

void ModeBank::set_params(float f0_hz, float stretch, float damping,
                          float brightness) {
    if (f0_hz == _f0 && stretch == _stretch && damping == _damping
        && brightness == _brightness && !_dirty)
        return;
    _f0 = f0_hz; _stretch = stretch; _damping = damping;
    _brightness = brightness;
    _recompute();
    _dirty = false;
    ++_updates;
}

// Everything below runs once per control tick, never per sample.
//
// The mode ladder (harmonic / stretch_factor / NthHarmonicCompensation) is
// ported character-for-character from daisysp::Resonator::Process
// (lib/DaisySP/Source/PhysicalModeling/resonator.cpp). The knob curve that
// feeds it -- how _stretch maps to stiffness -- is this engine's own:
// Resonator derives stiffness from a `structure` control via CalcStiff,
// which this engine has no knob for (spec 2), so CalcStiff is not ported.
void ModeBank::_recompute() {
    // 0 = harmonic (string), 1 = strongly stretched (bell). This curve is
    // tuning material, not a port of anything -- see the comment above.
    const float stiffness = _stretch * 0.4f;

    // Q from damping: the Resonator mapping, evaluated once.
    const float q_sqrt = std::pow(2.f, _damping * 79.7f / 12.f);
    const float q_base = 500.f * q_sqrt * q_sqrt;

    // Brightness rolls the upper modes off; same shape as Resonator's q_loss.
    float bright = _brightness * (1.f - _stretch * 0.3f);
    bright *= 1.f - _damping * 0.3f;
    const float q_loss = bright * (2.f - bright) * 0.85f + 0.15f;

    const float amp0 = std::cos(kPosition * 2.f * kPi) * 0.25f;

    // f0, in cycles/sample, rescaled by NthHarmonicCompensation so the
    // fundamental's pitch does not drift as stiffness grows.
    const float f0_norm = (_f0 / _sr) * NthHarmonicCompensation(3, stiffness);

    // Two accumulators, multiplied per mode -- not one additive one.
    // `harmonic` steps by f0_norm each mode (n * f0_norm, the harmonic
    // series); `stretch_factor` accumulates decaying stiffness and is what
    // pulls the ladder away from that series. mode_frequency = harmonic *
    // stretch_factor is what makes stretch scale with the harmonic index
    // instead of growing linearly.
    float harmonic = f0_norm;
    float stretch_factor = 1.f;
    float stiff_iter = stiffness;
    float loss = 1.f;

    for (int i = 0; i < kModes; ++i) {
        float f = harmonic * stretch_factor;      // cycles per sample
        if (f >= 0.499f) f = 0.499f;              // Nyquist guard

        const float attenuation = 1.f - f * 2.f;
        const float q = 1.f + f * q_base * loss;

        const float g = std::tan(kPi * f);
        const float r = 1.f / q;
        const float h = 1.f / (1.f + r * g + g * g);

        const int b = i / kBatch, s = i % kBatch;
        _svf[b].set_coeffs(s, g, r + g, h);
        _gain[b][s] = amp0 * attenuation;

        // Advance to the next partial.
        stretch_factor += stiff_iter;
        if (stiff_iter < 0.f) {
            // Keep partials from folding back into negative frequencies.
            // This engine's stretch never makes stiffness negative; the
            // branch is kept because it is part of the reference's ladder.
            stiff_iter *= 0.93f;
        } else {
            // Adds a few extra partials in the highest frequencies.
            stiff_iter *= 0.98f;
        }
        harmonic += f0_norm;
        loss *= q_loss;
    }
}

float ModeBank::process(float in) {
    float out = 0.f;
    for (int b = 0; b < kBatches; ++b) out += _svf[b].process(_gain[b], in);
    return out;
}

} // namespace spky
