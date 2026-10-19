#pragma once
// The CLOCK and RESET jack law both hosts run (spec docs/superpowers/specs/
// 2026-10-19-clock-and-reset-jacks-design.md §2.1, §2.2, §6). Edges and a
// sample count in; the tempo, the beat pulse and the restart out. It decides
// nothing about pins and knows no Rack or libDaisy type: VCV calls it per
// sample, the firmware once per 96-sample block. Header-only, no heap.
// tests/test_clock_law.cpp drives it at both call sizes.
#include <cstdint>

namespace control {

// The accepted external tempo, as control_law.h already gates
// Options::measured_bpm (control_law.h:956). That gate is two literals inside a
// header that pulls in the whole Instrument, so there is no constant to reuse
// without the heavy include; these name the same range.
inline constexpr float kClockBpmMin = 20.f;
inline constexpr float kClockBpmMax = 400.f;

// N: the tempo is the mean of the last N-1 periods (the span of the last N
// edges). Derivation, at 48 kHz with the firmware's 96-sample block (q):
//  - Quantization. Each edge stamp is late by [0, q), so a span of k periods
//    is off by less than q, and the tempo by less than bpm*q/(k*P - q). At the
//    non-block-exact 130 BPM (P = 22153.8 samples = 230.77 blocks) the span
//    flips between two neighbouring block counts, so the tempo steps by up
//    to that bound per pulse: 0.566 BPM (0.435 %) for k = 1, i.e. 130.43 /
//    129.87 (§3); 0.141 for k = 4, 0.094 for k = 6, 0.070 (0.054 %) for k = 8.
//    The law at k = 8 steps by at most 0.0704 BPM (probe, 2026-10-19).
//  - Swing. With an even number of periods the window always holds whole
//    long+short pairs, so a swung 1-per-beat source averages to its beat
//    exactly (55/45 at 120 BPM: 0 error once full); an odd count is off by
//    (swing excess)/k on every pulse.
//  - Settling. A tempo step inside the band (kClockBand) ramps through the
//    window and has settled after k pulses at the new period (120 -> 105:
//    the 8th); a step out of the band settles at the 2nd (below).
// So k = N-1 = 8: even, an eighth of the single-period step, and a ramp of two
// bars of 4/4. k = 16 would halve the step again but take four bars to follow
// a tempo change. tests/test_clock_law.cpp pins step, swing and settling.
inline constexpr int kClockWindowEdges   = 9;
inline constexpr int kClockWindowPeriods = kClockWindowEdges - 1;

// Clock loss: no valid edge for longer than this many measured periods (§6).
inline constexpr float kClockTimeoutPeriods = 2.f;

// The band around a full window's mean, as a ratio: a period in
// [mean / kClockBand, mean * kClockBand] enters the window. A stop/start
// under the timeout must not move the tempo (§8.1 "no tempo excursion"), and
// a quick one can leave a gap of any length, shorter or longer than a beat.
//  - Out of the band: the period still gives a pulse (the beat aligns), but it
//    is held aside, and so is the period right after it; the tempo holds.
//  - A tempo change is two consecutive out-of-band periods on the same side
//    that agree with each other within the band: the window restarts with
//    the two, and the step has settled at its 2nd pulse (120 -> 90, 75, 150).
//  - In the band: the period enters. One odd period r (in beats) among k
//    leaves |r-1| / (k-1+r) until it leaves the window: at the band's edges
//    2.44 % (r = 1.2) and 2.13 % (r = 1/1.2), under the 2.5 % target
//    (probe at 120 BPM: r = 1.19 -> 2.32 %, r = 0.84 -> 2.04 %, r = 1.1 ->
//    1.23 %; 0.4, 0.6, 0.78, 1.21, 1.25, 1.35, 1.5, 1.9 -> 0).
//  - 1.2 keeps 55/45 swing (1.1x and 0.91x of a full even window's mean, which
//    is the beat) inside. Accepted cost: quarter-note swing heavy enough to
//    leave the band (beyond about 59/41) is never measured: both halves are
//    held, so the tempo freezes at its window value (probe: 60/40 and 66/34
//    at 120 BPM hold 120.000) and a tempo change under such swing is not
//    followed; the pulses still align the beat.
// Applies only to a full window, whose mean is the beat.
inline constexpr float kClockBand = 1.2f;

// A reset up to this long after a valid pulse restarts as if it had come just
// before that pulse, back-dated by the elapsed samples (§6). Inclusive, and
// rounded to whole samples in init(): with a float constant,
// double(0.010f) * 48000 is 479.99998927 (probe), and an age of exactly 480
// (five blocks) must back-date.
inline constexpr double kClockResetWindowSeconds = 0.010;

struct ClockOut {
    bool     pulse        = false;  // a valid pulse: the host calls inst.clock_pulse()
    bool     restart      = false;  // the host calls inst.restart(restart_age)
    uint32_t restart_age  = 0;      // samples since the edge the restart belongs to
    bool     lost         = false;  // running -> gone on this call (firmware releases TEMPO)
    bool     clock_lamp   = false;  // any CLOCK edge at the jack
    bool     reset_lamp   = false;  // any RESET edge at the jack
    float    measured_bpm = 0.f;    // for Options::measured_bpm; 0 = the knob sets the tempo
};

class ClockLaw {
public:
    // Clean state; also what boot calls. The knob owns the tempo.
    void init(float sample_rate) {
        *this = ClockLaw{};
        _sr = sample_rate;
        _resetWindow = reset_window_samples(sample_rate);
    }

    // Drop the period history (VCV reinit and un-bypass). The held tempo stays,
    // but the clock runs again only after two new valid edges; a pending reset
    // is dropped. The knob's reference is taken on the next call.
    void discontinuity() {
        forget_edges();
        _running = false;
        _armed = false;
        _haveKnobRef = false;
    }

    // samples: elapsed since the previous call (1 in VCV, 96 per block on the
    // board). An edge seen in a call is stamped at the end of its samples.
    // tempo_knob: raw TEMPO param 0..1, compared for change only.
    // beat_phase: inst.beat_phase(), for the armed-at-loss restart on the next
    // wrap (a value below the previous call's) when no CLOCK edge returns first.
    ClockOut step(uint32_t samples, bool clock_edge, bool reset_edge,
                  float tempo_knob, float beat_phase) {
        ClockOut o;
        _now += samples;
        o.clock_lamp = clock_edge;
        o.reset_lamp = reset_edge;

        // Loss first: an edge in the same call comes after the gap, so it is
        // the first edge of a new clock.
        if (_running && double(_now - _lastValid) > double(kClockTimeoutPeriods) * _period) {
            _running = false;
            o.lost = true;
            forget_edges();
            _knobRef = tempo_knob;
            _haveKnobRef = true;
        }

        if (clock_edge) o.pulse = on_edge();

        // Knob takeover: while the clock is gone, a held tempo yields to the
        // first knob value that differs from the one at the loss.
        if (!_running && _bpm > 0.f) {
            if (!_haveKnobRef) { _knobRef = tempo_knob; _haveKnobRef = true; }
            else if (tempo_knob != _knobRef) _bpm = 0.f;
        }

        // Reset. In the same call as a pulse it counts as arriving first, so
        // that pulse is the 1. Without a running clock it is the 1 itself.
        if (reset_edge) {
            if (o.pulse || !_running) {
                restart(o, 0);
            } else if (_now - _lastValid <= _resetWindow) {
                restart(o, uint32_t(_now - _lastValid));
            } else {
                _armed = true;
            }
        } else if (_armed) {
            // Armed while running, then lost: the first CLOCK edge back is the
            // 1, and FireFlow's own beat wrap (carries PACE) is the fallback.
            // That edge is no pulse and does not make the clock run (two valid
            // edges still). It needs no range check: the loss forgot every
            // edge, so the first one back is a reference with no interval yet,
            // and it fires and disarms, so no later edge reaches this branch.
            if (o.pulse) restart(o, 0);
            else if (!_running && clock_edge) restart(o, 0);
            else if (!_running && beat_phase < _prevPhase) restart(o, 0);
        }
        _prevPhase = beat_phase;

        o.measured_bpm = _bpm;
        return o;
    }

    bool running() const { return _running; }

private:
    static uint64_t reset_window_samples(float sample_rate) {
        return uint64_t(kClockResetWindowSeconds * double(sample_rate) + 0.5);
    }

    void restart(ClockOut& o, uint32_t age) {
        o.restart = true;
        o.restart_age = age;
        _armed = false;
    }

    void forget_edges() {
        _haveEdge = false;
        _count = 0;
        _head = 0;
        _pendingSide = 0;
    }

    void push(double interval) {
        _ring[_head] = interval;
        _head = (_head + 1) % kClockWindowPeriods;
        if (_count < kClockWindowPeriods) ++_count;
    }

    double mean() const {
        double s = 0.0;
        for (int i = 0; i < _count; ++i) s += _ring[i];
        return s / _count;
    }

    void measure() {
        _period = mean();
        _bpm = float(60.0 * double(_sr) / _period);
    }

    // One CLOCK edge at _now. Returns whether it is a valid pulse.
    bool on_edge() {
        if (!_haveEdge) {               // the first edge: a reference, no period
            _haveEdge = true;
            _lastEdge = _now;
            return false;
        }
        const double interval = double(_now - _lastEdge);
        _lastEdge = _now;               // out-of-range edges are references too
        if (interval <= 0.0) return false;
        const double bpm = 60.0 * double(_sr) / interval;
        if (bpm < kClockBpmMin || bpm > kClockBpmMax) return false;   // no clock

        _lastValid = _now;
        if (!_running) {                // the second valid edge: running
            _running = true;
            push(interval);
            measure();
            return true;
        }
        if (_count == kClockWindowPeriods) {
            const double m = mean(), band = double(kClockBand);
            const int side = interval > m * band ? 1 : interval < m / band ? -1 : 0;
            if (side != 0) {
                if (side == _pendingSide && interval <= _pendingInterval * band &&
                    interval >= _pendingInterval / band) {
                    _count = 0;         // two agreeing out-of-band periods: a new tempo
                    _head = 0;
                    _pendingSide = 0;
                    push(_pendingInterval);
                    push(interval);
                    measure();
                    return true;
                }
                _pendingSide = side;    // held aside: the beat aligns, the tempo holds
                _pendingInterval = interval;
                return true;
            }
            if (_pendingSide != 0) {    // the period after a held one: held too
                _pendingSide = 0;
                return true;
            }
        }
        push(interval);
        measure();
        return true;
    }

    float    _sr = 48000.f;
    uint64_t _now = 0;
    bool     _running = false;
    float    _bpm = 0.f;            // the tempo handed out; 0 = the knob's
    double   _period = 0.0;         // samples per beat behind _bpm

    bool     _haveEdge = false;
    uint64_t _lastEdge = 0;         // last CLOCK edge of any kind
    uint64_t _lastValid = 0;        // last valid pulse (timeout, reset window)
    double   _ring[kClockWindowPeriods] = {};
    int      _count = 0, _head = 0;
    int      _pendingSide = 0;      // a held out-of-band period: +1 long, -1 short, 0 none
    double   _pendingInterval = 0.0;
    uint64_t _resetWindow = reset_window_samples(48000.f);

    bool     _armed = false;
    float    _prevPhase = 0.f;
    bool     _haveKnobRef = false;
    float    _knobRef = 0.f;
};

} // namespace control
