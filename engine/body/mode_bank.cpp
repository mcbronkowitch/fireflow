#include "body/mode_bank.h"
#include <cmath>

namespace spky {

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
void ModeBank::_recompute() {
    // Stiffness drives how far the partials depart from the harmonic series:
    // 0 = harmonic (string), 1 = strongly stretched (bell). Ported from
    // Resonator's CalcStiff/structure path, reduced to the branch this engine
    // uses (positive stiffness only -- negative stiffness compresses partials
    // toward the fundamental, which the MATL axis reaches through the string
    // side instead).
    const float stiffness = _stretch * 0.4f;

    // Q from damping: the Resonator mapping, evaluated once.
    const float q_sqrt = std::pow(2.f, _damping * 79.7f / 12.f);
    const float q_base = 500.f * q_sqrt * q_sqrt;

    // Brightness rolls the upper modes off; same shape as Resonator's q_loss.
    float bright = _brightness * (1.f - _stretch * 0.3f);
    bright *= 1.f - _damping * 0.3f;
    const float q_loss = bright * (2.f - bright) * 0.85f + 0.15f;

    const float amp0 = std::cos(kPosition * 2.f * kPi) * 0.25f;

    float stretch_factor = 1.f;
    float stiff_iter = stiffness;
    float loss = 1.f;

    for (int i = 0; i < kModes; ++i) {
        const float mode_hz = _f0 * stretch_factor;
        float f = mode_hz / _sr;                 // cycles per sample
        if (f > 0.49f) f = 0.49f;                // Nyquist guard

        const float attenuation = 1.f - f * 2.f;
        const float q = 1.f + f * q_base * loss;

        const float g = std::tan(kPi * f);
        const float r = 1.f / q;
        const float h = 1.f / (1.f + r * g + g * g);

        const int b = i / kBatch, s = i % kBatch;
        _svf[b].set_coeffs(s, g, r + g, h);
        _gain[b][s] = amp0 * attenuation;

        // Advance to the next partial. stretch_factor grows superlinearly with
        // stiffness -- that is what turns a harmonic series into a bell.
        stretch_factor += 1.f + stiff_iter;
        stiff_iter *= 0.98f;
        loss *= q_loss;
    }
}

float ModeBank::process(float in) {
    float out = 0.f;
    for (int b = 0; b < kBatches; ++b) out += _svf[b].process(_gain[b], in);
    return out;
}

} // namespace spky
