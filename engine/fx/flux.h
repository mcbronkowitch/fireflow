#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include "Utility/dsp.h"
#include "fx/drag.h"
#include "fx/fx_util.h"
#include "fx/tape_echo.h"
#include "mod/divisions.h"
#include "mod/rhythm_view.h"

namespace spky {

inline constexpr size_t kTapeSamples = 262144;

// Stereo, caller-memory tape echo behind a click-free SoftSwitch. The block
// is full-wet internally and adds its two independent returns to the dry
// signal at FLUX MIX.
class Flux {
public:
    static constexpr size_t kMaxSamples = kTapeSamples;

    void init(float sample_rate, float* left, float* right);
    void set_on(bool on, bool immediate = false) {
        // Waking a stale line must flush BEFORE the switch mutates its
        // stage: an immediate off leaves no fall for process() to observe,
        // and an immediate on lands in hold before process() runs again --
        // a was_idle check inside process() misses that pair entirely.
        if (on && _sw.is_idle() && _line_dirty) flush_lines();
        _sw.set_on(on, immediate);
    }
    bool is_on() const { return _sw.is_on(); }
    bool engaged() const { return _buf_ok && (_sw.is_on() || !_sw.is_idle()); }
    bool has_buffers() const { return _buf_ok; }
    void set_bpm(float bpm);
    void set_rate(int slice_idx);
    float delay_time() const { return _delay_time; }
    // Where the FLUX time period stands, in [0, 1): a free-running phase over
    // delay_time() for the FTIME lamp. Observer only -- nothing in process()
    // reads it. It is NOT _repeat_phase_samples: that counter runs only while
    // THIN is thinning (LINK > 0 with a valid rhythm) and sits at 0 otherwise,
    // which would leave the lamp dark at LINK = 0, the factory value.
    float period_phase() const {
        const float period = _delay_time * _sr;
        return period > 0.f ? _period_phase_samples / period : 0.f;
    }
    void set_feedback(float norm);
    void set_mix(float norm);
    void set_time_mod(float norm);
    void set_link(float norm);
    void set_rhythm(const RhythmView& rv);
    // FLUX's own delay stays in real time and never sees PACE's paced BPM
    // (set_bpm above always gets the raw _bpm). But _rhy_gap arrives already
    // paced -- it counts samples between PITCH-lane onsets, and the lanes
    // themselves are paced -- so update_thin_pattern needs the factor back to
    // put both sides of its ratio in the same time frame (spec 2026-08-12
    // §3.3). Instrument::_apply_tempo forwards PACE here on every change.
    void set_rhythm_pace(float p) { _rhythm_pace = p; update_thin_pattern(); }
    void process(float& l, float& r);

#if defined(SPKY_PROBE_TOGGLES)
    // Probe only (util/probe_toggles.h bits 1 and 2, measurement image):
    // skip the two echo lines altogether / keep them but skip their band-pass
    // and tanh. Handed down once per block by Instrument::process.
    // Bit 3 (dtcm) runs both lines on the short host-provided lines handed
    // over once by set_probe_lines().
    void set_probe(bool no_echo, bool no_filter, bool dtcm) {
        _probe_no_echo = no_echo;
        _echo_l.set_probe(no_filter, dtcm);
        _echo_r.set_probe(no_filter, dtcm);
    }
    void set_probe_lines(float* left, float* right) {
        _echo_l.set_probe_line(left);
        _echo_r.set_probe_line(right);
    }
#endif

    float delay_target_for_test() const { return _dt_target; }
    float delay_current_for_test() const { return _dt_current; }
    float gate_for_test() const { return _gate; }
    int thin_n_for_test(int i) const { return _thin_n[i]; }
    void set_rhythm_gap_for_test(int i, int32_t gap) {
        _rhy_gap[i] = gap;
        update_thin_pattern();
    }

private:
    void recompute_time(bool immediate);
    void update_time_target(bool immediate);
    void refresh_repeat_scheduler();
    void update_thin_pattern();
    void advance_gate();
    void flush_lines();

    TapeEcho<kTapeSamples> _echo_l;
    TapeEcho<kTapeSamples> _echo_r;
#if defined(SPKY_PROBE_TOGGLES)
    bool _probe_no_echo = false;
#endif
    SoftSwitch _sw;
    float _mix_lin = 0.f;
    bool _buf_ok = false;
    float _sr = 48000.f;
    float _bpm = 120.f;
    int _rate_idx = 3;
    float _delay_time = 0.5f;
    float _dt_current = 0.5f;
    float _dt_target = 0.5f;
    float _dt_coef = 1.f;
    float _time_mult = 1.f;
    float _time_mod_norm = -1.f;
    float _fb_norm = -1.f;

    float _link = 0.f;
    float _repeat_phase_samples = 0.f;
    float _repeat_period_samples = 0.f;
    float _period_phase_samples = 0.f;   // observer only, see period_phase()

    float _thin = 0.f;
    float _rhythm_pace = 1.f;
    int32_t _rhy_gap[2] = {0, 0};
    bool _rhy_valid = false;
    int _thin_n[2] = {1, 1};
    int _thin_i = 0;
    int _thin_count = 0;
    float _gate = 1.f;
    float _gate_target = 1.f;
    float _gate_coef = 1.f;

    bool _line_dirty = false;
};

} // namespace spky
