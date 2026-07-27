#pragma once
#include <cstdint>

#include "mod/rng.h"

#include "Dynamics/crossfade.h"
#include "Filters/onepole.h"
#include "Utility/dcblock.h"
#include "Utility/delayline.h"

namespace spky {

// A Karplus-Strong string: the harmonic half of BodyVoice.
//
// Ported from daisysp::String (lib/DaisySP/Source/PhysicalModeling/
// KarplusString.{h,cpp}, MIT, Electrosmith + Emilie Gillet, itself from
// Emilie Gillet's Rings -- see THIRD_PARTY.md). The DSP is unchanged. What
// changed is WHEN the parameter block runs.
//
// daisysp::String::ProcessInternal recomputes, for every sample and in both
// nonlinearity branches, the entire block above its `src_phase_ +=` line:
// two powf, one atanf, and a OnePole::SetFrequency that costs a tanf. On the
// Seed that measured 906 cycles per sample for a string carrying no
// nonlinearity at all (docs/bench/2026-07-26-f58644f-body.md), against a
// budget of 10000 cycles per sample for the whole instrument. None of it
// depends on the sample; all of it depends only on the four parameters.
//
// Here that block is set_params(), which the engine calls on the 96-sample
// control tick, and process() is the per-sample half and nothing else. This
// is the same fix ModeBank applies to daisysp::Resonator, for the same
// reason -- see engine/body/mode_bank.h.
//
// The split is exact, not an approximation: with the parameters held still,
// this produces bit-identical output to daisysp::String (tests/
// test_ks_string.cpp). Moving a parameter takes effect on the next control
// tick instead of the next sample, which is what every other control in this
// engine already does.
//
// ONE deliberate divergence from the reference, and it is not about
// scheduling. daisysp::String draws its dispersion noise from libc rand():
// process-global state, seeded by nobody, and a different sequence under
// glibc, msvcrt and newlib. This engine's rule is that one seed gives
// bit-identical renders on desktop, in VCV and on the Seed (engine/mod/
// rng.h), so the draw comes from an owned spky::Rng instead, seeded through
// init() and re-seeded by reset(). Same range ([-0.5, +0.5)), same role,
// different sequence. Below nonlinearity 0.75 the noise is multiplied by a
// zero amount and the outputs stay bit-identical anyway; above it they
// diverge by construction. tests/test_ks_string.cpp asserts exactly that
// split rather than quietly relaxing to a tolerance.
class KsString {
public:
    void init(float sample_rate, uint32_t seed);
    void reset();

    // Control rate ONLY.
    //   freq_hz       string pitch
    //   brightness    0..1, damping filter cutoff (FILTER)
    //   damping       0..1, ring time; >= 0.95 crossfades to infinite decay
    //   nonlinearity  0..1; 0 is the curved bridge with nothing to curve,
    //                 rising values move to dispersion (RESO)
    // Recomputes the cached coefficients when an argument actually changed;
    // otherwise returns without touching them.
    void set_params(float freq_hz, float brightness, float damping,
                    float nonlinearity);

    // Per sample.
    float process(float in);

    uint32_t coeff_updates() const { return _updates; }

private:
    static constexpr size_t kDelayLineSize = 1024;

    void recompute();

    daisysp::DelayLine<float, kDelayLineSize>     _string;
    daisysp::DelayLine<float, kDelayLineSize / 4> _stretch;
    daisysp::OnePole                              _iir_damping;
    daisysp::DcBlock                              _dc_blocker;
    daisysp::CrossFade                            _crossfade;

    // Dispersion noise. Held so reset() can restore the sequence: a reset
    // string has to ring the same way twice, or a render stops being
    // reproducible across a retrigger.
    Rng      _rng;
    uint32_t _seed = 0x5EEDu;

    float _sample_rate = 48000.f;

    // Requested parameters, as handed to set_params.
    float _freq_hz      = 0.f;
    float _brightness   = 0.f;
    float _damping      = 0.f;
    float _nonlinearity = 0.f;
    bool  _dirty        = true;

    // Cached: computed by recompute(), read by process().
    bool  _dispersion     = false;   // which nonlinearity branch process() takes
    float _src_ratio      = 1.f;
    float _base_delay     = 4.f;     // clamped delay, damping-compensated
    float _noise_amount   = 0.f;
    float _noise_filter   = 0.f;
    float _bridge_curving = 0.f;
    float _stretch_point  = 0.f;
    // Two factors, not one product: see the note in recompute(). Folding them
    // changes the rounding of main_delay.
    float _stretch_k          = 0.f;   // 0.408 - stretch_point * 0.308
    float _stretch_correction = 1.f;
    float _ap_gain            = 0.f;

    // Per-sample state.
    float _dispersion_noise = 0.f;
    float _curved_bridge    = 0.f;
    float _src_phase        = 0.f;
    float _out_sample[2]    = {0.f, 0.f};

    uint32_t _updates = 0;
};

} // namespace spky
