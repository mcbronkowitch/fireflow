#pragma once
#include "fx/comp.h"
#include "fx/flux.h"
#include "fx/grit.h"
#include "util/onepole.h"
#include "Utility/dcblock.h"

namespace spky {

// The second target row: pad slot in the FX layer == lane index, mirroring the
// engine targets' "lane index == pad slot == target slot" principle (spec).
enum FxTargetId {
    FXT_GRIT_INT  = 0,   // lane 0 (x2)    fast rhythmic texture
    FXT_FLUX_TIME = 1,   // lane 1 (x1/2)  slow tape drift / dub steps
    FXT_FX_MIX    = 2,   // lane 2 (x1)    accents locked to the melody cycle
    FXT_REV_SEND  = 3,   // lane 3 (x3/4)  polyrhythmic breathing
    FXT_FLUX_FB   = 4,   // lane 4 (x3/2)  swells
    FXT_COUNT     = 5
};

enum class FxBlock { Flux, Grit };

// Per-part chain: GRIT -> FLUX -> FX MIX -> COMP, plus the post-COMP reverb
// send tap (M4.6: comp BEFORE the tap — dry and send are compressed and
// auto-gained together, so full-wet profits fully).
// FX MIX is a linear (equal-gain) dry/wet — dry and wet are correlated, and
// bypass must be bit-exact (wet == dry => out == dry). The square-law XFade
// stays inside Drive/Reduce where it belongs. When both blocks are off the
// whole chain is skipped, so "FX off" costs nothing and changes nothing.
class PartFx {
public:
    void init(float sample_rate, float* echo_l, float* echo_r);

    Grit& grit() { return _grit; }
    Flux& flux() { return _flux; }
    const Grit& grit() const { return _grit; }
    const Flux& flux() const { return _flux; }
    Comp& comp() { return _comp; }
    const Comp& comp() const { return _comp; }
    void set_comp(float n) { _comp.set_amount(n); }

    void set_fx_on(FxBlock b, bool on, bool immediate = false);
    void set_grit_mode(GritMode m) { _grit.set_mode(m); }
    void set_flux_mix(float n) { _flux.set_mix(n); }
    void set_grit_mix(float n) { _grit.set_mix(n); }
    void set_bpm(float bpm)           { _flux.set_bpm(bpm); }
    void set_flux_rate(int slice_idx) { _flux.set_rate(slice_idx); }
    void set_drive(float n)  { _flux.set_drive(n); }
    void set_stages(float n) { _flux.set_stages(n); }

    // fxv[FXT_COUNT]: already-modulated values from Part::fx_target_value().
    void process(float& l, float& r, float& send_l, float& send_r,
                 const float* fxv);

    // The excitation bus's own-FLUX source (spec §6): the part's echo
    // playback signal, one sample behind (per-sample cache, not a block
    // buffer -- see the comment above _tape_tap for why that already lands
    // on "the previous block" once Part reads it at its control tick).
    // DC-blocked and soft-clipped so it is safe to feed straight into a
    // resonator's excitation input; NOT the FX-MIXed/comp'd output -- see
    // the capture point in process().
    //
    // FX MIX (fxv[FXT_FX_MIX], the dry/wet blend PartFx itself applies
    // AFTER the capture point) does NOT gate this. FLUX's OWN internal wet
    // level does: `_flux.process()` folds `_mix_lin` (set_flux_mix /
    // PartFx::set_flux_mix) into the echo BEFORE this class ever sees it
    // (flux.cpp), and the capture point is downstream of that. So FLUX
    // MIX == 0 silences the excitation bus even while the echo is still
    // running internally -- deliberate (with no wet level there is
    // arguably no "echo playback signal" to tap), but worth knowing before
    // debugging a BODY deck that seems deaf to its own FLUX.
    float tape_tap() const { return _tape_tap; }

private:
    Grit _grit;
    Flux _flux;
    Comp _comp;
    OnePole _smooth[FXT_COUNT];
    float _grit_applied = -1.f;   // change guard: Overdrive::SetDrive costs
    bool _primed = false;         // first process() snaps the smoothers

    // Tape tap (spec §6). Only reached inside the existing
    // `_grit.engaged() || _flux.engaged()` branch, so a fully FX-off deck
    // pays nothing extra; within it, the DC block/soft clip only actually
    // RUN when `_flux.engaged()` itself (process()'s inner gate) -- a
    // GRIT-only deck must not read a slowly-decaying tail off old FLUX
    // filter memory. _tap_dc's internal state is allowed to persist across a
    // FLUX-off period (a real filter's memory, same as ks_string's
    // always-running DcBlock) -- what must NOT persist is the visible
    // output: _tape_tap is force-set to exactly 0.f whenever FLUX is not
    // engaged, so a deck that turns FLUX off never leaves the excitation bus
    // hearing a stale echo.
    daisysp::DcBlock _tap_dc;
    float _tape_tap = 0.f;
};

} // namespace spky
