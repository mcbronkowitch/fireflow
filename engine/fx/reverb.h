#pragma once
#include "oliverb/oliverb.h"

namespace spky {

// The one shared room behind both parts. Input is the summed per-part sends
// (post-FX, morph-scaled in the Instrument mix) and joins the master AFTER
// the part mix as a wet-only signal.
//
// M4.5: the core is a vendored Oliverb (Clouds Parasite, MIT) — Erbe-Verb-
// style playable room. SIZE really rescales the delay reads (turning it
// Doppler-warps the tail), DECAY crosses 1.0 near the top of its travel
// into a soft-limited self-sustaining bloom, DIFFUSION morphs the room from
// discrete slap echoes to a dense wash and drags a little line modulation
// along with it (the old DEPTH knob is gone). Shimmer is gone (so is the
// separately-licensed DaisySP dependency it relied on).
//
// BIG object (~130 KB — the float delay buffer is an inline member). Never
// stack-allocate: the desktop host owns it as a static; the M6 firmware
// shell places it in SDRAM. Injected via FxMem.
class AmbientReverb {
public:
    void init(float sample_rate);
    void clear();                 // empty the room (buffer + loop filter state); params survive
    void set_size(float norm);    // room size; smoothed inside -> Doppler ride
    void set_decay(float norm);   // loop gain; crosses 100% at 0.8 (bloom above)
    // Knob -> loop gain, as a plain number where 1.0 means 100%. Public and
    // static because the panel shows this exact figure in its tooltip, the way
    // FLUX FB does: two copies of the curve would drift apart and the number on
    // screen would stop meaning what the room is doing.
    static float decay_loop_gain(float norm);
    void set_tone(float norm);    // loop LP damping 500 Hz .. 16 kHz, exp
    void set_diffusion(float norm);       // room density: AP coeff 0..0.9 only
    void set_diffuser_mod_depth(float norm); // ap1..ap4 LFO smear depth (wash), independent
    void set_mod_depth(float norm);       // tail-delay LFO wobble depth, independent
    void process(float in_l, float in_r, float& out_l, float& out_r);
    // The return limiter's ride, for tests: exactly 1.0 while it is idle. From
    // outside, a ride is indistinguishable from a quieter room, so this is the
    // only way to check that quiet material is passed through untouched.
    float limiter_gain() const { return _lim_gain; }

private:
    clouds::Oliverb _verb;
    float _sr = 48000.f;
    int _ctrl = 0;   // control-rate divider for the LFO slope refresh
    // Return limiter: a ceiling on the wet-only return, so the room can never
    // hand the master more than a known amount however hard it blooms. Cheap on
    // purpose -- a per-sample peak follower and a gain multiply, with the one
    // divide taken at control rate, not per sample.
    float _wet_peak = 0.f;    // peak follower on the return
    float _lim_gain = 1.f;    // the ride; exactly 1.0 below the ceiling
    float _lim_target = 1.f;  // recomputed on the control raster
    float _pk_rel = 0.f, _lim_down = 0.f, _lim_up = 0.f;
    float _buffer[clouds::Oliverb::kBufferSize];
};

} // namespace spky
