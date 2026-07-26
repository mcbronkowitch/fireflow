#pragma once
#include <cstdint>
#include "util/svf_bp.h"

namespace spky {

// A bank of kModes band-pass resonators, the modal half of BodyVoice.
//
// The mode frequency / Q / amplitude formulas are ported from
// daisysp::Resonator::Process (lib/DaisySP/Source/PhysicalModeling/
// resonator.cpp, MIT, Electrosmith + Emilie Gillet -- see THIRD_PARTY.md).
// The difference is WHERE they run: Resonator recomputes all of them for
// every mode on every sample, including a powf (measured at 198 cycles per
// call on the Seed) and a stretch-factor loop. Here they run once per
// set_params() call, which the engine makes on the 96-sample control tick.
// process() is the SvfBp bank sum and nothing else.
//
// Strike position is fixed (kPosition): modal synthesis wants a position
// parameter, this engine does not have a knob to spend on one (spec 2).
class ModeBank {
public:
    static constexpr int kModes = 24;
    static constexpr int kBatch = 4;
    static constexpr int kBatches = kModes / kBatch;
    static_assert(kModes % kBatch == 0, "mode count must fill whole batches");

    void init(float sample_rate);
    void reset();

    // Control rate ONLY. f0_hz: fundamental. stretch/damping/brightness: 0..1
    // (DETUNE / DECAY / FILTER). Recomputes cached coefficients when any
    // argument actually changed; otherwise returns without touching them.
    void set_params(float f0_hz, float stretch, float damping, float brightness);

    // Per sample.
    float process(float in);

    uint32_t coeff_updates() const { return _updates; }

private:
    void _recompute();

    static constexpr float kPosition = 0.31f;   // strike position, tuning material
    static constexpr float kPi = 3.14159265358979f;

    SvfBp<kBatch> _svf[kBatches];
    float _gain[kBatches][kBatch] = {};

    float _sr = 48000.f;
    float _f0 = 220.f, _stretch = 0.f, _damping = 0.5f, _brightness = 0.5f;
    bool  _dirty = true;
    uint32_t _updates = 0;
};

} // namespace spky
