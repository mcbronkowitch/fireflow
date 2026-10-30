#pragma once
#include <cstdint>
#include "Effects/overdrive.h"
#include "Effects/sampleratereducer.h"
#include "fx/fx_util.h"
#include "util/probe_mark.h"

namespace spky {

enum class GritMode : uint8_t { Drive, Reduce };

// Port of the original firmware's Drive (src/core/fx.drive.*): Overdrive with
// intensity-coupled attenuation, square-law dry/wet mix. Original curves kept.
class GritDrive {
public:
    void init(float sample_rate);
    float intensity() const { return _intensity; }
    void set_intensity(float norm);
    float mix() const { return _mix.Stage(); }
    void set_mix(float norm) { _mix.SetStage(norm); }
    void process(float& l, float& r SPKY_PROBE_PART_PARAMS);

private:
    void apply();

    daisysp::Overdrive _drive;
    XFade _mix;
    float _intensity = 0.2f;
    float _attenuation = 1.f;
};

// daisysp::Decimator's maths for the one setting GritReduce uses (no smooth
// crushing), split in two so a caller that discards a return value can skip
// the bitcrush. step() is Process() without the bitcrush: the same
// threshold / counter / latch state, nothing computed for the return value.
// process() is step() plus the bitcrush and returns exactly what
// daisysp::Decimator::Process returns (tests/test_grit.cpp holds that bit for
// bit). Kept identical on purpose, division included: this class changes what
// is computed, not how.
class GritDecimator {
public:
    void init() {
        _downsample_factor = 1.f;
        _bits_to_crush     = 16;     // daisysp: SetBitsToCrush(16)
        _downsampled       = 0.f;
        _inc               = 0;
    }
    void set_downsample_factor(float f) { _downsample_factor = f; }
    // As daisysp's: the bit count follows the factor (it replaces the 16 that
    // init() set); the smooth-crushing overflow term is not needed here.
    void set_bitcrush_factor(float f) { _bits_to_crush = (uint32_t)(f * 16.f); }

    inline void step(float input) {
        const uint32_t threshold =
            (uint32_t)((_downsample_factor * _downsample_factor) * 96.0f);
        _inc += 1;
        if (_inc > threshold) {
            _inc         = 0;
            _downsampled = input;
        }
    }
    // Not inlined, on purpose: daisysp::Decimator::Process was an
    // out-of-line call, and inlined here the optimiser could drop the
    // discarded first call's bitcrush by itself -- which would make the
    // toggle's OFF path cheaper than today's code and hide the saving.
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    float process(float input) {
        step(input);
        int32_t temp = (int32_t)(_downsampled * 65536.0f);
        temp >>= _bits_to_crush;     // shift off
        temp <<= _bits_to_crush;     // move back with zeros
        return (float)temp / 65536.0f;
    }

private:
    float    _downsample_factor = 1.f;
    uint32_t _bits_to_crush     = 16;
    float    _downsampled       = 0.f;
    uint32_t _inc               = 0;
};

// Port of the original firmware's Reduce (src/core/fx.reduce.*): Decimator +
// SampleRateReducer. Unlike the original, each channel owns its own pair, for
// two reasons:
// - Separate state: the Decimator latches when its counter passes
//   floor(i^2 * 96), and where that threshold + 1 is even (intensity 0.55,
//   0.8) every latch fell on the R call of a shared object, so the wet path
//   never saw L.
// - Inherited rate: the shared pair was stepped twice per frame (L, then R),
//   so it latched twice as often and its reducer phase advanced 2 x 0.6 per
//   frame. Each per-channel object is stepped twice per frame with the same
//   input to keep that crush character; a factor mapping cannot (hold
//   lengths are whole samples, the old ones averaged half-samples), and the
//   reducer clamps its frequency at 1.0. For L == R the output equals the
//   shared pair's sample for sample.
class GritReduce {
public:
    void init(float sample_rate);
    float intensity() const { return _intensity; }
    void set_intensity(float norm);
    float mix() const { return _mix.Stage(); }
    void set_mix(float norm) { _mix.SetStage(norm); }
    // The discarded first Decimator step of each channel does state only
    // (probe toggle GRIT_DECIM_STEP, util/probe_toggles.h). Output and state
    // are identical either way, so it can flip mid-stream. Off by default.
    void set_fast(bool fast) { _fast = fast; }
    void process(float& l, float& r SPKY_PROBE_PART_PARAMS);

private:
    void apply();

    GritDecimator _decimator[2];
    daisysp::SampleRateReducer _reducer[2];
    XFade _mix;
    float _intensity = 0.55f;
    bool _fast = false;
};

// GRIT block: Drive <-> Reduce behind one click-free SoftSwitch. Replaces the
// original Fx class's grit half; the mode is set explicitly (the original's
// switch_grit_mode() toggle becomes an M6 gesture on top of set_mode()).
class Grit {
public:
    void init(float sample_rate);
    void set_on(bool on, bool immediate = false) { _sw.set_on(on, immediate); }
    bool is_on() const { return _sw.is_on(); }
    // true while audible: on, or still ramping out after switch-off
    bool engaged() const { return _sw.is_on() || !_sw.is_idle(); }
    void set_mode(GritMode m);
    GritMode mode() const { return _mode; }
    void set_intensity(float norm);   // applies to the active mode (original behavior)
    float intensity() const;
    void set_mix(float norm);         // single GRIT MIX layer param -> both modes
    // The parameters exist only in the callback profile's build
    // (util/probe_mark.h, fault R4): PartFx::process forwards its stamp clock
    // and slots, and the asm label keeps the name shell/itcm_hot.ld lists
    // (GNU toolchains only).
    void process(float& l, float& r SPKY_PROBE_PART_PARAMS)
#if defined(SPKY_PROBE_MARKS) && defined(__GNUC__) && !defined(_MSC_VER)
        __asm__("_ZN4spky4Grit7processERfS1_")
#endif
        ;
    // Observer only, for tests: the raw MIX layer norm set_mix() last
    // received (both GritDrive/GritReduce apply the same value; there is no
    // per-mode split to read back).
    float mix_for_test() const { return _mix_norm; }
    // Forwarded to GritReduce (see there); latched once per block by the
    // Instrument, never per sample.
    void set_fast(bool fast) { _reduce.set_fast(fast); }

private:
    GritDrive _drive;
    GritReduce _reduce;
    SoftSwitch _sw;
    GritMode _mode = GritMode::Drive;
    float _intensity = 0.3f;
    float _mix_norm = 0.5f;
};

} // namespace spky
