#include "body/mode_bank.h"
#include <cmath>

namespace spky {

namespace {

// Ported character-for-character from daisysp::NthHarmonicCompensation
// (lib/DaisySP/Source/PhysicalModeling/resonator.cpp). Rescales f0 so the
// fundamental's perceived pitch stays put as stiffness (stretch) grows,
// instead of drifting. The negative-stiffness branch was dead when this was
// ported (the curve then ran 0..0.4); Task 8b gave COLOR the other side, so
// stiffness now runs -0.06..+0.4 and BOTH branches are live.
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
    // -1 = strongly compressed (drum, plate), 0 = harmonic (string),
    // +1 = strongly stretched (bell). This curve is tuning material, not a
    // port of anything -- see the comment above.
    //
    // The two sides are NOT symmetric and that is a numerical limit, not
    // taste. Below, stretch_factor starts at 1 and accumulates stiffness with
    // a 0.93 decay across the 24 modes, so its minimum is 1 + stiffness *
    // sum(0.93^i, i<23) = 1 + stiffness * 11.55. Negative stiffness therefore
    // walks it through ZERO at about -0.0866, past which mode frequencies go
    // negative and std::tan(kPi * f) stops meaning anything. Measured over the
    // 24 modes: -0.060 -> min 0.304, -0.080 -> min 0.073, -0.087 -> min
    // -0.009 (collapsed). kCompressScale = 0.06 stops at the first of those:
    // a 4x margin below the collapse, and still deep enough that the top
    // partials sit near a third of their harmonic frequency -- a flat,
    // drum/plate ladder. Do not raise it toward 0.087 to "use the range":
    // the last 0.02 is the cliff, not headroom.
    //
    // kStretchScale keeps the original 0.4 exactly, so the bell side is
    // bit-identical to what it was before stretch became signed.
    const float stiffness = _stretch >= 0.f ? _stretch * kStretchScale
                                            : _stretch * kCompressScale;

    // Q from damping: the Resonator mapping, evaluated once.
    const float q_sqrt = std::pow(2.f, _damping * 79.7f / 12.f);
    const float q_base = 500.f * q_sqrt * q_sqrt;

    // Brightness rolls the upper modes off; same shape as Resonator's q_loss.
    // The stretch term is "inharmonicity dulls the top", so it follows the
    // AMOUNT, not the direction: signed _stretch here would RAISE brightness
    // on the compressed side, which is the opposite of what the line means.
    // At _stretch >= 0 this is the original expression unchanged.
    const float stretch_amt = _stretch < 0.f ? -_stretch : _stretch;
    float bright = _brightness * (1.f - stretch_amt * 0.3f);
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
        // ...and the floor the reference never needed. With a negative
        // stiffness stretch_factor descends, and a caller that ignores the
        // -1..+1 contract can drive it through zero: f goes negative, tan()
        // returns a negative g and the SVF coefficients stop meaning
        // anything. kCompressScale is chosen so this branch is unreachable
        // from the knob (tests/test_mode_bank.cpp pins that separately); this
        // is the second line of defence. Written as !(f > kFMin) rather than
        // f < kFMin so a NaN -- reachable only if f0 itself is inf -- lands
        // here too instead of surviving both guards.
        else if (!(f > kFMin)) f = kFMin;

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
            // This is the compressed (drum/plate) side, live since Task 8b:
            // the faster 0.93 decay is what bounds how far stretch_factor can
            // descend, and kCompressScale is derived from it -- see the
            // stiffness comment at the top of this function.
            stiff_iter *= 0.93f;
        } else {
            // Adds a few extra partials in the highest frequencies.
            stiff_iter *= 0.98f;
        }
        harmonic += f0_norm;
        loss *= q_loss;
    }
}

// Observation only (see the header). std::atan / the division are fine here:
// nothing on the audio path calls these.
float ModeBank::mode_freq(int i) const {
    const int b = i / kBatch, s = i % kBatch;
    return std::atan(_svf[b].g(s)) / kPi;
}

float ModeBank::mode_q(int i) const {
    const int b = i / kBatch, s = i % kBatch;
    const float r = _svf[b].r_plus_g(s) - _svf[b].g(s);   // r_plus_g = 1/q + g
    return 1.f / r;
}

float ModeBank::process(float in) {
    float out = 0.f;
    for (int b = 0; b < kBatches; ++b) out += _svf[b].process(_gain[b], in);
    return out;
}

} // namespace spky
