#pragma once
#include <cstdint>
#include <cmath>
#include "util/math.h"

namespace spky {

enum class QuantMode { Scale, Chrom, Free };

// Global scale list in three groups: modes, pentatonics, exotic/handpan.
// Dark -> bright ordering survives inside each group, so the direction of the
// selection sweep still means what it always meant; the groups themselves run
// familiar -> exotic and are what makes the longer list blind-navigable
// (count blocks of 4/5/4, then step within one). Bit i set = semitone i
// relative to root is allowed.
enum ScaleId {
    // A -- modes
    SCALE_AEOLIAN = 0,
    SCALE_DORIAN,          // boot default
    SCALE_MIXO,
    SCALE_LYDIAN,
    // B -- pentatonics
    SCALE_HIRAJOSHI,
    SCALE_PYGMY,
    SCALE_MIN_PENT,
    SCALE_KUMOI,
    SCALE_MAJ_PENT,
    // C -- exotic / handpan
    SCALE_PHRYGIAN,
    SCALE_HIJAZ,
    SCALE_HARM_MIN,
    SCALE_WHOLE,
    SCALE_LIST_COUNT
};

constexpr uint16_t SCALE_MASKS[SCALE_LIST_COUNT] = {
    0x05AD,  // aeolian           0 2 3 5 7 8 10
    0x06AD,  // dorian            0 2 3 5 7 9 10
    0x06B5,  // mixolydian        0 2 4 5 7 9 10
    0x0AD5,  // lydian            0 2 4 6 7 9 11
    0x018D,  // hirajoshi         0 2 3 7 8
    0x048D,  // pygmy             0 2 3 7 10
    0x04A9,  // minor pentatonic  0 3 5 7 10
    0x028D,  // kumoi             0 2 3 7 9
    0x0295,  // major pentatonic  0 2 4 7 9
    0x05AB,  // phrygian          0 1 3 5 7 8 10
    0x05B3,  // hijaz             0 1 4 5 7 8 10
    0x09AD,  // harmonic minor    0 2 3 5 7 8 11
    0x0555,  // whole tone        0 2 4 6 8 10
};

// Display names, read by the VCV tooltip. Kept here rather than in the host so
// the two lists cannot drift apart.
constexpr const char* SCALE_NAMES[SCALE_LIST_COUNT] = {
    "Aeolian", "Dorian", "Mixolydian", "Lydian",
    "Hirajoshi", "Pygmy", "Minor pent", "Kumoi", "Major pent",
    "Phrygian", "Hijaz", "Harmonic minor", "Whole tone",
};

constexpr uint16_t CHROM_MASK = 0x0FFF;

// Scale quantizer on the pitch contract: normalized 0..1 = 36 semitones
// (3 octaves). Part applies it as the last stage of the PITCH target; voices
// later apply it to the target + V/Oct sum. FREE returns the input untouched.
class Quantizer {
public:
    static constexpr float SPAN_SEMIS = 36.f;
    static constexpr float HYST_SEMIS = 0.30f;   // switch ~15 cents past midpoint

    // call_interval = how many samples pass between process() calls. Part
    // drives the quantizer at the engine's control tick (96), so the 40 ms
    // change slew has to be counted in calls, not samples. Floored at 1 so a
    // large interval cannot collapse the slew to nothing.
    void init(float sample_rate, int call_interval = 1) {
        if (call_interval < 1) call_interval = 1;
        _slew_len = static_cast<int>(sample_rate * 0.04f
                                     / static_cast<float>(call_interval));
        if (_slew_len < 1) _slew_len = 1;
        _slew_ctr = 0;
        _have_note = false;
        _have_out = false;
        // PULL's gravity mask is runtime-derived, same footing as _have_out
        // above: Part re-pushes it via set_gravity() every control tick, so
        // a reinit must not let a stale pre-reinit mask survive into the
        // first post-reinit process() call.
        _grav_mask = 0;
        _grav_on   = false;
    }

    void set_scale(uint16_t mask12) { if (mask12 != _scale) { _scale = mask12; on_change(); } }
    void set_mode(QuantMode m)      { if (m != _mode)       { _mode = m;       on_change(); } }
    void set_root(int semis)        { if (semis != _root)   { _root = semis;   on_change(); } }

    // PULL (spec 2026-07-19 pull-chord-gravity): a SECOND mask, absolute.
    // Bit i = pitch class i with no root shift -- the scale mask stays
    // root-relative, this one does not, because it comes from the sibling
    // deck's sounding chord and that is already absolute. While it is on it
    // REPLACES the scale/chrom mask in every mode, FREE included: binding a
    // note to the neighbour's harmony is the whole point of the feature, so a
    // free-running deck is not exempt. A zero mask is not gravity.
    void set_gravity(uint16_t abs_pc_mask, bool on) {
        const bool want = on && abs_pc_mask != 0;
        // Load-bearing, not a redundant fast path: Part::_control_tick calls
        // this every control tick (500 Hz at 48k/96), so without this guard
        // on_change() would fire that often even while gravity sits off or
        // unchanged, clearing _have_note and re-arming the slew on every
        // call. That destroys the hysteresis and the change-slew shape
        // process() above relies on, and moves ctrl_identity and
        // wave_formant_sweep -- the same failure shape Instrument::set_pace
        // guards against with its own early-out (instrument.cpp). Do not
        // "simplify" this away.
        if (want == _grav_on && (!want || abs_pc_mask == _grav_mask)) return;
        // _grav_mask is written unconditionally here, including on the "off"
        // path (want == false): harmless today because _grav_mask is only
        // ever read while _grav_on is true (process() above), so a stale
        // value sitting here while gravity is off is never observed. Kept
        // rather than special-cased to skip the write on that path, so this
        // setter stays the one place both members change together.
        _grav_mask = abs_pc_mask;
        _grav_on   = want;
        on_change();
    }
    bool gravity_on() const { return _grav_on; }

    QuantMode mode() const { return _mode; }
    uint16_t scale_mask() const { return _scale; }   // last active scale (survives FREE)
    int      root_semis() const { return _root; }

    float process(float norm) {
        if (!_quantizing()) {                  // FREE, and no note bound
            _last_out = norm;
            _have_out = true;
            _have_note = false;
            return norm;
        }
        const uint16_t mask = _grav_on ? _grav_mask
                            : (_mode == QuantMode::Chrom ? CHROM_MASK : _scale);
        const int root = _grav_on ? 0 : _root;
        const float semis = clampf(norm, 0.f, 1.f) * SPAN_SEMIS;
        int note = nearest_note(semis, mask, root);
        if (_have_note && note != _last_note && allowed(_last_note, mask, root)) {
            const float d_last = std::fabs(semis - static_cast<float>(_last_note));
            const float d_note = std::fabs(semis - static_cast<float>(note));
            if (d_last - d_note < HYST_SEMIS) note = _last_note;   // hold
        }
        _last_note = note;
        _have_note = true;

        float out = static_cast<float>(note) / SPAN_SEMIS;
        if (_slew_ctr > 0) {
            --_slew_ctr;
            const float t = 1.f - static_cast<float>(_slew_ctr) / static_cast<float>(_slew_len);
            out = lerpf(_slew_from, out, t);
        }
        _last_out = out;
        _have_out = true;
        return out;
    }

private:
    // "Is the next process() call going to snap?" -- FREE alone no longer
    // answers it, because a bound note quantizes in FREE too.
    bool _quantizing() const { return _grav_on || _mode != QuantMode::Free; }

    void on_change() {
        _have_note = false;                       // re-pick without hysteresis
        if (_have_out && _quantizing()) {
            _slew_from = _last_out;               // soften the jump (~40 ms)
            _slew_ctr = _slew_len;
        } else {
            // Into passthrough: instant, on purpose. This is the PULL-release
            // case in FREE mode -- gravity going off there drops straight
            // into _quantizing() == false, so the very next process() call
            // takes process()'s early "not quantizing" return and hands back
            // the raw value with no slew at all. The spec's edge case ("...
            // slewing back into the scale") describes Scale/Chrom release,
            // where _quantizing() stays true and this branch is not taken;
            // it does not describe FREE. tests/test_quantizer.cpp's
            // "gravity: FREE is not exempt while bound, and passthrough
            // returns" gates this snap deliberately -- do not widen this
            // branch to slew without checking that test and the identity
            // gates first.
            _slew_ctr = 0;
        }
    }

    bool allowed(int k, uint16_t mask, int root) const {
        int deg = (k - root) % 12;
        if (deg < 0) deg += 12;
        return (mask >> deg) & 1;
    }

    // Outward search from the rounded center: the first allowed note at
    // integer distance d is the float-nearest up to the lo/hi tie, which is
    // resolved by comparing real distances (equal -> lower note wins).
    int nearest_note(float semis, uint16_t mask, int root) const {
        const int center = static_cast<int>(semis + 0.5f);
        for (int d = 0; d <= 12; ++d) {
            const int lo = center - d, hi = center + d;
            const bool lo_ok = lo >= 0 && allowed(lo, mask, root);
            const bool hi_ok = hi <= 36 && allowed(hi, mask, root);
            if (lo_ok && hi_ok && lo != hi)
                return std::fabs(semis - static_cast<float>(hi))
                     < std::fabs(semis - static_cast<float>(lo)) ? hi : lo;
            if (lo_ok) return lo;
            if (hi_ok) return hi;
        }
        return center;  // unreachable with a non-empty mask
    }

    QuantMode _mode  = QuantMode::Scale;
    uint16_t  _scale = SCALE_MASKS[SCALE_DORIAN];
    int       _root  = 0;
    int       _last_note = 0;
    bool      _have_note = false;
    float     _last_out  = 0.f;
    bool      _have_out  = false;
    float     _slew_from = 0.f;
    int       _slew_ctr  = 0;
    int       _slew_len  = 1920;
    uint16_t  _grav_mask = 0;
    bool      _grav_on   = false;
};

} // namespace spky
