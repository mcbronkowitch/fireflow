#include "fx/part_fx.h"
#include "util/fast_sin.h"
#include "util/fast_tanh.h"
#include <cmath>

using namespace spky;

void PartFx::init(float sample_rate, float* echo_l, float* echo_r) {
    _grit.init(sample_rate);
    _flux.init(sample_rate, echo_l, echo_r);
    _comp.init(sample_rate);
    for (auto& s : _smooth) s.init(sample_rate, 0.002f);
    _grit_applied = -1.f;
    _primed = false;
    _tap_dc.Init(sample_rate);
    _tape_tap = 0.f;
}

void PartFx::set_fx_on(FxBlock b, bool on, bool immediate) {
    if (b == FxBlock::Flux) _flux.set_on(on, immediate);
    else                    _grit.set_on(on, immediate);
}

void PartFx::process(float& l, float& r, float& send_l, float& send_r,
                     const float* fxv) {
    if (!_primed) {   // snap to the first real values: no phantom boot slew
        for (int i = 0; i < FXT_COUNT; ++i) _smooth[i].reset(fxv[i]);
        _primed = true;
    }
    float v[FXT_COUNT];
    for (int i = 0; i < FXT_COUNT; ++i) v[i] = _smooth[i].process(fxv[i]);

    if (_grit.engaged() || _flux.engaged()) {
        if (v[FXT_GRIT_INT] != _grit_applied) {
            _grit.set_intensity(v[FXT_GRIT_INT]);
            _grit_applied = v[FXT_GRIT_INT];
        }
        _flux.set_feedback(v[FXT_FLUX_FB]);
        // v[FXT_FLUX_TIME] was smoothed and then DISCARDED here -- alone
        // among the five targets -- for as long as FLUX was a crossfade
        // delay, where modulating the delay time made no musical sense. In a
        // BBD, clock modulation IS the sound generation, so it lands. This
        // rides the 2 ms smoother above, deliberately NOT the 30 ms ladder
        // slew inside Flux: through that path a 4 Hz vibrato would not
        // survive (spec "Modulation": two smoothers, two jobs).
        _flux.set_time_mod(v[FXT_FLUX_TIME]);
        const float dry_l = l, dry_r = r;
        _grit.process(l, r);

        // Tape tap capture point (spec §6: "the part's echo playback signal,
        // not the mixed output"). Flux::process only ever ADDS its echo (and
        // taps) onto l/r -- both live branches in flux.cpp are `l += ...`,
        // and the idle-switch early return leaves l/r untouched -- so
        // (l - pre_flux_l) is EXACTLY what Flux contributed this sample, no
        // approximation. Captured here, before FX MIX below and before
        // Comp() further down, so an FX-MIX-0 deck still feeds the
        // excitation bus: the tap is a send off the echo, not a read of the
        // mix.
        const float pre_flux_l = l, pre_flux_r = r;
        _flux.process(l, r);
        // Gate on Flux specifically, not just "this outer branch ran": a
        // deck can have GRIT engaged with FLUX fully idle, and in that case
        // l/r are untouched by _flux.process() (pre_flux == post exactly),
        // but _tap_dc still carries filter memory from the last time FLUX
        // WAS on. Feeding that memory zeros for one sample would not read
        // as 0.f yet -- it decays over many samples instead -- which is
        // exactly the "freezes at a stale value" failure mode the design
        // forbids. Checking engaged() here, instead of trusting the zero
        // input, makes the cut instantaneous and exact.
        if (_flux.engaged()) {
            const float echo_mono = 0.5f * ((l - pre_flux_l) + (r - pre_flux_r));
            _tape_tap = fast_tanh(_tap_dc.Process(echo_mono));
        } else {
            _tape_tap = 0.f;
        }

        const float m = v[FXT_FX_MIX];
        l = dry_l + (l - dry_l) * m;
        r = dry_r + (r - dry_r) * m;
    } else {
        // Neither block engaged: nothing ran _tap_dc this sample, so the
        // cached tap must be forced to exactly 0.f rather than left holding
        // whatever it was the last time FLUX was on (addendum D: "must not
        // freeze at a stale value").
        _tape_tap = 0.f;
    }

    _comp.process(l, r);   // one-knob comp — BEFORE the send tap (spec: full-wet must profit)

    // Equal-power send law. fast_sin(p) IS sin(2*pi*p), so 0.25 * v is
    // exactly the quarter-turn this used to spell as v * pi/2 -- same curve,
    // <1.2e-3 absolute error on a send gain (util/fast_sin.h). This call site
    // runs once per sample per part: 192 libm sinf per 96-sample block on the
    // two-part instrument, measured at ~120 cycles each, and it is a THIRD of
    // what the whole FX-off chain (bench fx_none) costs. Do not put libm back.
    const float g = fast_sin(v[FXT_REV_SEND] * 0.25f);
    send_l = l * g;
    send_r = r * g;
}
