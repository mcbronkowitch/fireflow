#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include "Utility/dsp.h"
#include "fx/bbd.h"
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
    void process(float& l, float& r);

    // Observers for tests: the clock and the stage count are the only two
    // numbers that make "the ladder, the lane and the ceiling all landed
    // where the spec says" assertable at all.
    int stages() const { return _stages_now; }
    float clock_hz() const { return _clock_hz; }
    float drive_norm_for_test() const { return _drive_norm; }

private:
    void recompute_time(bool immediate);

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
    float _clock_hz = 0.f;
    // Unchanged-value guards: set_stages runs a powf and set_drive a pow10f,
    // and both are forwarded at control rate. -1 is unreachable for a
    // clamped 0..1 norm, so the FIRST push after init always forwards.
    float _drive_norm = -1.f;
    float _stages_norm = -1.f;
};

} // namespace spky
