#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include "Utility/dsp.h"
#include "fx/bbd.h"
#include "fx/drag.h"
#include "fx/fx_util.h"
#include "mod/divisions.h"

namespace spky {

// FLUX block: a stereo bucket-brigade echo behind a click-free SoftSwitch,
// echo added onto the signal at FLUX MIX (original topology: send-style,
// full-wet echo).
//
// The class, its name and its public form are unchanged from the tape era --
// SoftSwitch, engaged(), the bit-exact off path, set_rate / set_mix /
// set_feedback / set_bpm, the shared delay-time slew. What changed is behind
// them: there is no read pointer any more. Flux knows only music, BbdLine
// knows only physics, and bbd_clock_hz sits between them.
class Flux {
public:
    // Physical stage counts -- the STAGES control's endpoints. 8192 is a pair
    // of MN3005s, i.e. a Deluxe Memory Man.
    static constexpr int kMinStages = bbd_tuning::kMinStages;
    static constexpr int kMaxStages = bbd_tuning::kMaxStages;

    // Floats per channel the host must provide. The NAME and MEANING are
    // unchanged (every FxMem consumer keeps compiling); only the value moved,
    // from 262144 to kMaxStages/2. A two-phase BBD stores one sample per TWO
    // stages -- see the "even ticks write, odd ticks read" comment on
    // BbdLine. 8192 floats x 4 lines = 128 KB, against 4.19 MB before.
    static constexpr size_t kMaxSamples = kMaxStages / 2;

    void init(float sample_rate, float* buf_l, float* buf_r);
    void set_on(bool on, bool immediate = false) { _sw.set_on(on, immediate); }
    bool is_on() const { return _sw.is_on(); }
    bool engaged() const { return _buf_ok && (_sw.is_on() || !_sw.is_idle()); }
    bool has_buffers() const { return _buf_ok; }
    void set_bpm(float bpm);
    void set_rate(int slice_idx);
    float delay_time() const { return _delay_time; }
    void set_feedback(float norm);
    void set_mix(float norm);
    void set_drive(float norm);      // 0..1 -> 0..+12 dB INSIDE the loop (bbd_tuning::kDriveLoDb/kDriveHiDb)
    void set_stages(float norm);     // 0..1 -> 512..16384, geometric
    // FXT_FLUX_TIME. Pulls MULTIPLICATIVELY on the clock, downstream of the
    // base time, so it rides PartFx's 2 ms smoother and not the 30 ms
    // ladder slew -- a 4 Hz vibrato would not survive the latter.
    void set_time_mod(float norm);
    // LINK: how the OTHER deck's rhythm reaches into this echo. Bipolar, and
    // the two halves never run together.
    //   0        neutral -- the bit-exact today-path.
    //   0 -> +1  DRAG: the neighbour's rhythm pulls the delay time. The clock
    //            moves, so the stored charge bends in pitch. Interpolation is
    //            geometric because pitch tracks the clock ratio.
    //   0 -> -1  THIN: the clock never moves. The delay stays on its RATE rung
    //            and the neighbour's rhythm decides which repeats sound.
    //            Rhythm without pitch (spec 2026-07-28 flux-link).
    void set_link(float norm);
    // The other deck's published rhythm, pushed at control rate by Instrument.
    void set_rhythm(const RhythmView& rv);
    void process(float& l, float& r);

    // Observers for tests: the clock and the stage count are the only two
    // numbers that make "the ladder, the lane and the ceiling all landed
    // where the spec says" assertable at all.
    int stages() const { return _stages_now; }
    float clock_hz() const { return _clock_hz; }
    float drive_norm_for_test() const { return _drive_norm; }
    // The feedback coefficient actually handed to each BbdEcho. The law is a
    // function of BOTH knobs (see the comment on apply_feedback), and the
    // control-rate round assembles it from a cached factor rather than
    // evaluating it whole -- which makes it the one quantity in this class
    // whose correctness is numerical rather than audible. A 1e-6 drift in it
    // is inaudible and still a bug, and no behavioural test in this file can
    // see one.
    float feedback_coef_l_for_test() const { return _echo_l.Feedback(); }
    float feedback_coef_r_for_test() const { return _echo_r.Feedback(); }
    // The delay time DRAG is currently aiming at, before the 30 ms slew. Equal
    // to the ladder time whenever DRAG is 0 or the neighbour has no rhythm.
    float drag_time_s() const { return _dt_target; }
    // The gate's current gain, 1 whenever thinning is disengaged, and the
    // neighbour's gaps expressed in whole repeats. Both exist because the
    // thinning pattern is otherwise only observable as a level over time.
    float gate_for_test() const { return _gate; }
    int   thin_n_for_test(int i) const { return _thin_n[i]; }

private:
    void recompute_time(bool immediate);
    // Pushes _fb_norm to both echoes with DRIVE's gain divided out. Called
    // from set_feedback AND set_drive, because either knob moving invalidates
    // the coefficient the other one produced -- see the comment on the
    // definition for why the division lives here rather than in BbdEcho.
    void apply_feedback();
    // The ONLY place _dt_target is written. Called from recompute_time,
    // set_link, set_rhythm and the step flip in process().
    void apply_drag();
    // Re-derives _thin_n from the neighbour's raw gaps and the CURRENT ladder
    // time. Called whenever either moves.
    void update_thin_pattern();
    // Advances the skip pattern by one repeat and sets the gate's target.
    void advance_gate();

    BbdEcho _echo_l;
    BbdEcho _echo_r;
    SoftSwitch _sw;
    float _mix_lin = 0.f;
    bool  _buf_ok = false;
    float _sr = 48000.f;
    float _bpm = 120.f;
    int   _rate_idx = 3;             // "1/4"
    float _delay_time = 0.5f;
    // Shared L/R delay-time slew (both channels always run the same length).
    // It stays, and it now doubles as the VCO slew of the real circuit:
    // division changes are click-free AND bend in pitch, like the hardware.
    float _dt_current = 0.05f;
    float _dt_target = 0.05f;
    float _dt_coef = 1.f;
    // STAGES rides the SAME 30 ms slew. Stage count is a buffer length, not a
    // continuous quantity; changing it means swapping the chip, and that
    // clicks. Slewing it is not what a physical part does, but it produces
    // exactly the class of artefact this device already makes -- a drift in
    // time and pitch -- which turns STAGES into a playable gesture rather
    // than a setup control.
    float _stage_current = 8192.f;
    float _stage_target = 8192.f;
    int   _stages_now = 8192;
    float _time_mult = 1.f;
    // set_time_mod's unchanged-value guard, compared against the RAW pushed
    // argument -- bbd_time_mult's own clamp to 0..1 runs a line later, inside
    // set_time_mod, and so cannot be what keeps -1 unreachable at THIS
    // compare. What actually keeps it unreachable is a caller-side
    // invariant: PartFx pushes FXT_FLUX_TIME through Part::fx_target_value,
    // which clamps to 0..1 at engine/parts/part.cpp:129, before the value
    // ever reaches PartFx's OnePole smoother and, downstream of that, this
    // setter. That is unlike _drive_norm and _stages_norm below: those two
    // clamp their OWN argument first and compare the clamped value, so -1 is
    // unreachable by construction, not by a caller's behaviour. Here it
    // depends on `Part` continuing to clamp before this setter is reached --
    // a future direct caller that skipped that clamp would reach -1 and
    // defeat the guard.
    float _time_mod_norm = -1.f;
    float _clock_hz = 0.f;
    // Unchanged-value guards: set_stages runs a powf and set_drive a pow10f,
    // and both are forwarded at control rate. -1 is unreachable for a
    // clamped 0..1 norm, so the FIRST push after init always forwards.
    float _drive_norm = -1.f;
    float _stages_norm = -1.f;
    // The FEEDBACK knob itself, kept because the coefficient handed to BbdEcho
    // is a function of BOTH knobs and has to be re-derived whenever either
    // moves. Matches init()'s set_feedback(0.45f).
    float _fb_norm = 0.45f;
    // FEEDBACK's law is _fb_norm * 1.2 / bbd_drive_gain(_drive_norm), and
    // that quotient depends on DRIVE alone. set_drive is the sole writer of
    // _drive_norm and already guards itself, so the quotient is a
    // control-rate constant -- cached here because apply_feedback used to
    // evaluate a std::pow for it once per SAMPLE: PartFx pushes set_feedback
    // unguarded from the per-sample path (part_fx.cpp), which made a ~198
    // cycle libm call part of the audio loop. init() writes this explicitly;
    // see the comment there for why the member default is not relied on.
    float _fb_scale = 1.2f;

    // DRAG state. _drag is now the derived positive half of _link (the
    // negative half, THIN, is separate and lands in a later step). _drag_iv
    // holds the neighbour's two intervals in samples; _drag_i selects which
    // one is in force; _drag_phase counts samples into the current step and
    // _drag_step_len is that step's length in samples, cached so process()
    // does not multiply per sample.
    float   _link = 0.f;
    float   _drag = 0.f;
    int32_t _drag_iv[2] = { 0, 0 };
    int     _drag_i = 0;
    float   _drag_phase = 0.f;
    float   _drag_step_len = 0.f;
    bool    _drag_active = false;

    // LINK's negative half. _rhy_gap holds the neighbour's RAW gaps -- not
    // derive_intervals' output: an even pattern is a failure for DRAG and a
    // RESULT here (link spec 2.1), so the uniformity guard must not reach this
    // path. _thin_n is those gaps in whole repeats of the current delay time,
    // _thin_i selects which is running, _thin_count counts repeats into it.
    float   _thin = 0.f;
    int32_t _rhy_gap[2] = { 0, 0 };
    bool    _rhy_valid = false;
    int     _thin_n[2] = { 1, 1 };
    int     _thin_i = 0;
    int     _thin_count = 0;
    float   _gate = 1.f;
    float   _gate_target = 1.f;
    float   _gate_coef = 1.f;
};

} // namespace spky
