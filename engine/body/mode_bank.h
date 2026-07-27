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

    // Control rate ONLY. f0_hz: fundamental. damping/brightness: 0..1
    // (DECAY / FILTER). stretch: SIGNED, -1..+1 -- the inharmonicity that
    // DETUNE's amount and COLOR's character multiply out to (spec §5/§7).
    // 0 is harmonic, positive stretches the partials sharp (bell), negative
    // compresses them flat (drum, plate). The two sides do not scale alike;
    // _recompute() says why, and the reason is numerical, not taste.
    // Recomputes cached coefficients when any argument actually changed;
    // otherwise returns without touching them.
    void set_params(float f0_hz, float stretch, float damping, float brightness);

    // Per sample.
    float process(float in);

    uint32_t coeff_updates() const { return _updates; }

    // Frequency floor in cycles/sample, the partner of the 0.499 Nyquist
    // ceiling. Public so a test can assert the guard did NOT fire (an f that
    // came out of the mapping is never exactly this value). See _recompute().
    static constexpr float kFMin = 1e-5f;

    // --- observation (tests) --------------------------------------------
    // Mode i's cached normalized frequency and Q, recovered from the
    // coefficients the bank actually pushed into the SVF (g = tan(pi*f),
    // r_plus_g = 1/q + g). A test that re-derived the ladder next to
    // _recompute() would only be checking its own copy of the arithmetic;
    // these read production state. Not used on the audio path.
    float mode_freq(int i) const;
    float mode_q(int i) const;

private:
    void _recompute();

    static constexpr float kPosition = 0.31f;   // strike position, tuning material
    static constexpr float kPi = 3.14159265358979f;

    // Stretch -> stiffness, one scale per side. See _recompute().
    static constexpr float kStretchScale  = 0.4f;    // bell side (unchanged)
    static constexpr float kCompressScale = 0.06f;   // drum/plate side


    SvfBp<kBatch> _svf[kBatches];
    float _gain[kBatches][kBatch] = {};

    float _sr = 48000.f;
    float _f0 = 220.f, _stretch = 0.f, _damping = 0.5f, _brightness = 0.5f;
    bool  _dirty = true;
    uint32_t _updates = 0;
};

} // namespace spky
