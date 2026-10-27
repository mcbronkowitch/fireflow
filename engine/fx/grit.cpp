#include "fx/grit.h"
#include "Utility/dsp.h"
#include "util/math.h"

using namespace spky;

namespace {
inline float dbfs2lin(float db) { return daisysp::pow10f(db * 0.05f); }
inline float attenuation_for_intensity(float intensity) {
    return dbfs2lin(daisysp::fmap(intensity, -3.f, -24.f));
}
} // namespace

void GritDrive::init(float /*sample_rate*/) {
    _drive.Init();
    _drive.SetDrive(0.f);
    _mix.SetStage(0.33f);
    apply();
}

void GritDrive::set_intensity(float norm) {
    _intensity = clampf(norm, 0.f, 1.f);
    apply();
}

void GritDrive::apply() {
    _attenuation = attenuation_for_intensity(_intensity);
    _drive.SetDrive(dbfs2lin(daisysp::fmap(_intensity, -6.f, 0.f)));
}

void GritDrive::process(float& l, float& r SPKY_PROBE_PART_PARAMS) {
    // Callback profile only: stage 1 is the Overdrive (attenuation
    // included), stage 2 is empty in this mode -- its two stamps sit back
    // to back so the slots mean the same in both modes.
    float d0 = _drive.Process(l) * _attenuation;
    SPKY_PROBE_PART_MARK(17);
    SPKY_PROBE_PART_MARK(18);
    float d1 = _drive.Process(r) * _attenuation;
    SPKY_PROBE_PART_MARK(19);
    SPKY_PROBE_PART_MARK(20);
    _mix.Process(l, r, d0, d1, l, r);
}

void GritReduce::init(float /*sample_rate*/) {
    for (int c = 0; c < 2; ++c) {
        _decimator[c].Init();
        _decimator[c].SetBitsToCrush(16);
        _reducer[c].Init();
        _reducer[c].SetFreq(0.6f);
    }
    _mix.SetStage(0.5f);
    apply();
}

void GritReduce::set_intensity(float norm) {
    _intensity = clampf(norm, 0.f, 1.f);
    apply();
}

void GritReduce::apply() {
    const float crush =
        daisysp::fmap(1.f - _intensity, 0.5f, 0.7f, daisysp::Mapping::EXP);
    for (int c = 0; c < 2; ++c) {
        _decimator[c].SetDownsampleFactor(_intensity);
        _decimator[c].SetBitcrushFactor(crush);
    }
}

void GritReduce::process(float& l, float& r SPKY_PROBE_PART_PARAMS) {
    // One Decimator + SampleRateReducer per channel, each stepped twice per
    // frame with the same input (see grit.h): that is the rate the shared
    // pair ran at, so the crush keeps its inherited speed. Split into
    // statements only so the callback profile can stamp stage 1 (the
    // Decimator) and stage 2 (the SampleRateReducer).
    _decimator[0].Process(l);
    const float c0 = _decimator[0].Process(l);
    SPKY_PROBE_PART_MARK(17);
    _reducer[0].Process(c0);
    float r0 = _reducer[0].Process(c0);
    SPKY_PROBE_PART_MARK(18);
    _decimator[1].Process(r);
    const float c1 = _decimator[1].Process(r);
    SPKY_PROBE_PART_MARK(19);
    _reducer[1].Process(c1);
    float r1 = _reducer[1].Process(c1);
    SPKY_PROBE_PART_MARK(20);
    _mix.Process(l, r, r0, r1, l, r);
}

void Grit::init(float sample_rate) {
    _sw.init(sample_rate);
    _drive.init(sample_rate);
    _reduce.init(sample_rate);
    set_intensity(_intensity);
    set_mix(_mix_norm);
}

void Grit::set_mode(GritMode m) {
    _mode = m;
    set_intensity(_intensity);   // re-apply current values to the new mode
    set_mix(_mix_norm);
}

void Grit::set_intensity(float norm) {
    _intensity = clampf(norm, 0.f, 1.f);
    if (_mode == GritMode::Drive) _drive.set_intensity(_intensity);
    else                          _reduce.set_intensity(_intensity);
}

float Grit::intensity() const {
    return _mode == GritMode::Drive ? _drive.intensity() : _reduce.intensity();
}

void Grit::set_mix(float norm) {
    _mix_norm = clampf(norm, 0.f, 1.f);
    _drive.set_mix(_mix_norm);
    _reduce.set_mix(_mix_norm);
}

void Grit::process(float& l, float& r SPKY_PROBE_PART_PARAMS) {
    SPKY_PROBE_PART_MARK(15);   // callback profile only: GRIT starts here
    float k = _sw.process();
    if (_sw.is_idle()) {        // fully off: bit-exact dry
        SPKY_PROBE_PART_MARK(16); SPKY_PROBE_PART_MARK(17); SPKY_PROBE_PART_MARK(18);
        SPKY_PROBE_PART_MARK(19); SPKY_PROBE_PART_MARK(20);
        return;
    }
    float gl = l, gr = r;
    SPKY_PROBE_PART_MARK(16);   // the switch ends here
    if (_mode == GritMode::Drive) _drive.process(gl, gr SPKY_PROBE_PART_FWD);
    else                          _reduce.process(gl, gr SPKY_PROBE_PART_FWD);
    l = gl * k + l * (1.f - k);
    r = gr * k + r * (1.f - k);
}
