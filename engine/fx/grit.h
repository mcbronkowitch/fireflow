#pragma once
#include <cstdint>
#include "Effects/overdrive.h"
#include "Effects/decimator.h"
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

// Port of the original firmware's Reduce (src/core/fx.reduce.*): Decimator +
// SampleRateReducer. Unlike the original, each channel owns its own pair: the
// Decimator latches when its counter passes floor(i^2 * 96), and where that
// threshold + 1 is even (intensity 0.55, 0.8) every latch fell on the R call
// of a shared object, so the wet path never saw L.
class GritReduce {
public:
    void init(float sample_rate);
    float intensity() const { return _intensity; }
    void set_intensity(float norm);
    float mix() const { return _mix.Stage(); }
    void set_mix(float norm) { _mix.SetStage(norm); }
    void process(float& l, float& r SPKY_PROBE_PART_PARAMS);

private:
    void apply();

    daisysp::Decimator _decimator[2];
    daisysp::SampleRateReducer _reducer[2];
    XFade _mix;
    float _intensity = 0.55f;
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

private:
    GritDrive _drive;
    GritReduce _reduce;
    SoftSwitch _sw;
    GritMode _mode = GritMode::Drive;
    float _intensity = 0.3f;
    float _mix_norm = 0.5f;
};

} // namespace spky
