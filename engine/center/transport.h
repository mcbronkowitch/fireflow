#pragma once
#include <cmath>

namespace spky {

// Master transport: a running beat counter advanced at the Center's control
// rate. The host reports external clock edges (one pulse per REAL beat, which
// is `pace` paced beats -- see clock_pulse()) through clock_pulse(); RST
// zeroes the downbeat. The accumulator is double — float loses beat-phase
// precision within minutes at 500 ticks/s.
class Transport {
public:
    void init(float ctrl_rate) { _cr = ctrl_rate; _beats = 0.0; _anchor = 0.0; }
    // Guarded at the source, not at each reader: bpm() feeds a divide in
    // every consumer (nearest_division()/division_hz() for COUPLE's grid
    // gravity, and the transport's own beat_phase()/beats() readers), so a
    // single non-positive value stored here would otherwise reach all of
    // them as a non-finite result. A non-positive or non-finite
    // request is dropped and the last good tempo (default 120) is kept,
    // rather than clamped to some arbitrary floor BPM: scenario files forward
    // their `bpm` field unvalidated (host/render/scenario.cpp), and a
    // zero/negative value there is bad input, not a real slow tempo to honor.
    void set_bpm(float bpm) { if (bpm > 0.f && std::isfinite(bpm)) _bpm = bpm; }
    float bpm() const          { return _bpm; }

    void tick()        { _beats += static_cast<double>(_bpm) / (60.0 * static_cast<double>(_cr)); }
    // One external pulse is one REAL beat, which is `pace` paced-beats. The
    // snap grid is therefore a multiple of pace -- but anchored at the PREVIOUS
    // PULSE, not at absolute zero. _beats is the integral of bpm*pace, so after
    // a pace change _beats/pace is no longer the real beat count, and a
    // zero-anchored grid jumps: 100 beats at x1, then pace 1.32, and the next
    // pulse computes round(100/1.32)*1.32 = 100.28. Under a swept PACE macro
    // that fires on EVERY pulse, up to half a real beat, into the hard grid
    // servo (kLockCap 0.35). At pace == 1 the whole expression is bit-identical
    // to the round(_beats) it replaced.
    void clock_pulse(float pace) {
        if (!(pace > 0.f) || !std::isfinite(pace)) return;
        _beats  = _anchor + double(pace)
                          * std::round((_beats - _anchor) / double(pace));
        _anchor = _beats;
    }
    // PACE moved: re-anchor so the grid follows the knob instead of lagging it.
    void set_pace_anchor() { _anchor = _beats; }
    void reset()       { _beats = 0.0; _anchor = 0.0; }

    double beats() const     { return _beats; }
    float beat_phase() const { return static_cast<float>(_beats - std::floor(_beats)); }

private:
    double _beats = 0.0;
    double _anchor = 0.0;
    float  _bpm = 120.f;
    float  _cr  = 500.f;
};

} // namespace spky
