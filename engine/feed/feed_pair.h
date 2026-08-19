#pragma once
#include "feed/feed_config.h"
#include "util/fast_sin.h"
#include "util/math.h"
#include <cmath>

namespace spky {

// One operator pair: two phase accumulators, two two-sample history slots,
// and the slopes that carry frequency and amplitude toward their targets.
//
// Every phase-domain quantity here is in CYCLES (feed_config.h, PHASE UNITS).
struct FeedPair {
    float phase_c = 0.f;      // carrier phase, normalized [0,1)
    float phase_m = 0.f;      // modulator phase
    float inc_c = 0.f;        // per-sample carrier increment
    float d_inc_c = 0.f;      // slope toward the target increment
    float t_inc_c = 0.f;      // the target itself, so hz() reports the target
    float amp = 0.f;
    float d_amp = 0.f;
    float t_amp = 0.f;
    float gl = 0.70710678f;
    float gr = 0.70710678f;
    float m1 = 0.f, m2 = 0.f; // modulator history -- the self-feedback tap
    float o1 = 0.f, o2 = 0.f; // carrier history -- what the ring reads
    float fb = 0.f;           // pitch-attenuated feedback amount, cycles
    float lp = 0.f;           // the DAMP one-pole's state, cycles
};

// The ring. P is a template parameter so the bench can price several values of
// it in separate images (one row, several builds -- see the plan's Task 4), and
// so every loop bound is a compile-time constant.
template <int P>
class FeedBankT {
public:
    // How many control ticks a retarget takes to arrive. The glide is a plain
    // linear slope rather than a one-pole, because a one-pole never arrives
    // and "the frequency the pair is at" then has no exact answer for a gate
    // to check (feed P3's second half). Four ticks at 96 samples is ~8 ms at
    // 48 kHz -- long enough to be click-free (G24), short enough that a
    // sequenced retune is not audibly late.
    static constexpr int kSlopeTicks = 4;
    static constexpr int kSlopeSamples = kSlopeTicks * feed_cfg::kCtrlInterval;

    void init(float sr) {
        _sr = sr > 1.f ? sr : 48000.f;
        _inv_sr = 1.f / _sr;
        for (int i = 0; i < P; ++i) _p[i] = FeedPair{};
        _bond = 0.f; _index = 0.f; _ratio = 1.f; _damp = 1.f;
        _slope_left = 0;
    }

    void set_bond(float k)        { _bond = clampf(k, 0.f, 1.f); }
    void set_index(float cycles)  { _index = cycles; }
    void set_ratio(float r)       { _ratio = r; }
    void set_damp_coef(float c)   { _damp = clampf(c, 0.f, 1.f); }
    void set_fb_amount(int i, float cycles) { _p[i].fb = cycles; }

    void snap(int i, float hz, float amp, float pan) {
        FeedPair& p = _p[i];
        p.inc_c = p.t_inc_c = hz * _inv_sr;
        p.d_inc_c = 0.f;
        p.amp = p.t_amp = amp;
        p.d_amp = 0.f;
        _set_pan(p, pan);
    }

    void set_target(int i, float hz, float amp, float pan) {
        FeedPair& p = _p[i];
        p.t_inc_c = hz * _inv_sr;
        p.t_amp = amp;
        constexpr float inv = 1.f / static_cast<float>(kSlopeSamples);
        p.d_inc_c = (p.t_inc_c - p.inc_c) * inv;
        p.d_amp   = (p.t_amp   - p.amp)   * inv;
        _set_pan(p, pan);
        // Arm the arrival. A linear slope that is only ever ADDED does not
        // arrive -- it passes through the target and keeps going: measured
        // 440.00 Hz at exactly kSlopeSamples and 792.92 Hz at 1000 samples on
        // a 220 -> 440 retarget, which is the whole travel again and a half.
        // FeedEngine never sees it, because _rebuild_allocation re-targets
        // every pair every control tick and each push recomputes the slope
        // from the CURRENT value -- so the frequency converges geometrically
        // and the runaway needs a bank nobody re-targets. A bank nobody
        // re-targets is exactly what feed P3 is, and "the frequency the pair
        // is at" has to have an exact answer or the glide has no arrival to
        // assert. One shared counter for the whole bank rather than one per
        // pair, because the engine retargets all P pairs inside one tick.
        _slope_left = kSlopeSamples;
    }

    // The hot loop. Two passes per sample: pass 1 computes every pair's new
    // carrier output reading only PREVIOUS samples, pass 2 commits the
    // histories and sums. Without the split, pair i would read a neighbour
    // that had already advanced when j < i and one that had not when j > i --
    // the ring's behaviour would depend on the loop's direction, which is not
    // a property any spec can state.
    inline void process(float& outL, float& outR) {
        float o_new[P];
        const float k = _bond;
        const float ik = 1.f - k;
        for (int i = 0; i < P; ++i) {
            FeedPair& p = _p[i];
            const FeedPair& n = _p[(i + 1) % P];
            // The DX7 trick, as Plaits implements it: every feedback tap is
            // the average of the last two samples (spec 3.2.1). One add, one
            // multiply, and the path is low-passed -- this is simultaneously
            // the anti-aliasing and the anti-blowup measure.
            const float self_tap = 0.5f * (p.m1 + p.m2);
            const float ring_tap = 0.5f * (n.o1 + n.o2);
            const float raw = p.fb * (ik * self_tap + k * ring_tap);
            // DAMP: a one-pole INSIDE the feedback path (spec section 4).
            p.lp += _damp * (raw - p.lp);
            const float m = fast_sin(p.phase_m + p.lp);
            const float o = fast_sin(p.phase_c + _index * m);
            p.m2 = p.m1; p.m1 = m;
            o_new[i] = o;

            p.phase_c += p.inc_c;
            p.phase_c -= std::floor(p.phase_c);
            p.phase_m += p.inc_c * _ratio;
            p.phase_m -= std::floor(p.phase_m);
            p.inc_c += p.d_inc_c;
            p.amp   += p.d_amp;
        }
        float l = 0.f, r = 0.f;
        for (int i = 0; i < P; ++i) {
            FeedPair& p = _p[i];
            p.o2 = p.o1; p.o1 = o_new[i];
            const float s = o_new[i] * p.amp;
            l += s * p.gl;
            r += s * p.gr;
        }
        outL = l;
        outR = r;
        // The arrival, one compare and one decrement for the whole bank. The
        // O(P) snap below runs at most once per retarget, and in the engine it
        // never runs at all: a control tick lands every kCtrlInterval samples,
        // well inside kSlopeSamples, so the counter is re-armed before it
        // expires. See set_target for why it exists anyway.
        if (_slope_left > 0 && --_slope_left == 0) {
            for (int i = 0; i < P; ++i) {
                FeedPair& p = _p[i];
                p.inc_c = p.t_inc_c;
                p.amp   = p.t_amp;
                p.d_inc_c = 0.f;
                p.d_amp = 0.f;
            }
        }
    }

    float hz(int i) const  { return _p[i].inc_c * _sr; }
    float amp(int i) const { return _p[i].amp; }
    float fb_amount(int i) const { return _p[i].fb; }

    // --- observation (tests). Not used on the audio path. ---
    //
    // The blend itself, so a gate can check the arithmetic rather than infer
    // it from audio. The claim "BOND crossfades, it does not sum" is not
    // observable at the output whenever the index is 0 -- there `_index * m`
    // erases the modulator entirely and every feedback formula agrees -- and
    // it is buried under FM sidebands whenever the index is not. feed P7 reads
    // these three and reconstructs (1-k)*self + k*ring from them.
    float self_tap_for_test(int i) const { return 0.5f * (_p[i].m1 + _p[i].m2); }
    float ring_tap_for_test(int i) const {
        const FeedPair& n = _p[(i + 1) % P];
        return 0.5f * (n.o1 + n.o2);
    }
    // The DAMP one-pole's state, which at coefficient 1 IS the blended and
    // fb-scaled input the modulator's phase receives.
    float mod_input_for_test(int i) const { return _p[i].lp; }

private:
    // equal-power pan, the Voice::_apply_pan law (synth/voice.cpp): angle
    // 0..0.25 turns, gl = cos, gr = sin, both through fast_sin so the desktop
    // render and the firmware run one implementation.
    static void _set_pan(FeedPair& p, float pan) {
        const float a = (clampf(pan, -1.f, 1.f) + 1.f) * 0.125f;
        p.gr = fast_sin(a);
        p.gl = fast_sin(a + 0.25f);
    }

    FeedPair _p[P];
    float _sr = 48000.f;
    float _inv_sr = 1.f / 48000.f;
    float _bond = 0.f;
    float _index = 0.f;
    float _ratio = 1.f;
    float _damp = 1.f;
    int   _slope_left = 0;   // samples until the glide arrives; see set_target
};

using FeedBank = FeedBankT<feed_cfg::kPairs>;

}  // namespace spky
