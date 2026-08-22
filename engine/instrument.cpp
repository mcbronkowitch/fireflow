#include "instrument.h"
#include "mod/divisions.h"   // pace_mult(), used below -- arrives transitively
                              // through instrument.h -> parts/part.h ->
                              // mod/super_modulator.h too, but this file reads
                              // it directly, so it names its own dependency
                              // instead of leaning on that chain (see the
                              // SPKY_DECK_BUS #error just below for what an
                              // unnamed transitive dependency costs when the
                              // chain ever changes).
#include "util/math.h"
#include <algorithm>
#include <cmath>

// SPKY_DECK_BUS is defined by parts/part.h (`#ifndef`/`#define`, default 1),
// reached transitively through instrument.h's `#include "parts/part.h"`.
// The `#if SPKY_DECK_BUS` guards below rely on that chain having already run
// by the time they're reached. If the include order above ever changes so
// that guard is no longer defined when the preprocessor gets here, `#if` on
// an undefined identifier evaluates to 0 silently -- the cross-deck bus
// would vanish from this translation unit with no diagnostic, and the
// desktop build never separately exercises SPKY_DECK_BUS=0 to catch it (only
// the bench build does, deliberately, via BENCH_DECK_BUS). This #error turns
// that silent failure into a build failure instead.
#if !defined(SPKY_DECK_BUS)
#error "SPKY_DECK_BUS is undefined here -- instrument.h must include " \
       "parts/part.h (which defines it) before this point; check the " \
       "include order at the top of this file and of instrument.h."
#endif

using namespace spky;

namespace {
constexpr float kHalfPi = 1.57079632679489661923f;
// Keeps the dry level (cos = 0.92, -0.7 dB) with a leaner room than the
// pre-M4.8 fixed mix (wet rides ON TOP of the reverb's internal trim, so
// 0.25 puts it -8.3 dB against the old join; the old balance sits at 0.5,
// -3 dB overall). Chosen by ear, 2026-07-14.
constexpr float kDefaultReverbMix = 0.25f;
constexpr float kMixSmoothS = 0.010f;    // dry/wet gain glide; ear-tunable

// Bloom duck (spec 2026-08-03-reverb-bloom-duck-design.md). While the room
// self-drives, its return envelope pulls the dry bus back so the sum the
// master sees stays at the level the player set. All ear-tunable.
constexpr float kDuckThresh = 0.30f;  // below: exactly 1.0 even when armed
constexpr float kDuckFull   = 0.60f;  // env at which the floor is reached
constexpr float kDuckFloor  = 0.316f; // -10 dB: makes room, does not mute

// SLOW, seconds not milliseconds, both directions: the reverb work measured
// (twice) that fast gain rides on a wash read as dirt, not level. Down a
// little faster than the 2-3 s swell it answers; up slower still.
constexpr float kDuckDownS = 1.5f;
constexpr float kDuckUpS   = 4.0f;

constexpr float kRevReturnFadeS = 0.15f;  // ear-tunable: tail fade-out before the room sleeps

// CHOKE sidechain duck (plan 2026-08-22-choke-sidechain-duck, task 1). Same
// shape as the Bloom duck above -- an envelope picks a floor-style gain -- but
// fed from the PRIORITY deck's own output instead of the room's return, and
// applied to the YIELDING deck's contribution to the mix instead of the whole
// dry bus. Audio-rate, not control-rate: this one answers a note, not a swell.
//
// All three are BY-EAR STARTING POINTS, chosen blind and not yet heard. They
// are the classic sidechain shape (fast in, slow out, a floor rather than a
// mute) at plausible values -- nothing here has been tuned against the
// instrument, and none of it is a measurement.
constexpr float kChokeDuckFloor = 0.15f;   // gain at full depth, top of the
                                           // window: -16.5 dB, ducked not muted
constexpr float kChokeDuckAtkS  = 0.005f;  // envelope attack -- catches a note's edge
constexpr float kChokeDuckRelS  = 0.150f;  // envelope release -- rides back between notes

// The window the envelope is normalised against, and it is NOT 0..1 -- that is
// the whole point. A deck's part output never approaches full scale, so
// normalising against it made kChokeDuckFloor unreachable: measured before this
// window existed, the duck bottomed out at 0.8315 (-1.60 dB) on FLOW drones and
// 0.8489 (-1.42 dB) on STEP plucks, against the -16.5 dB the floor names. Same
// shape and same reason as the Bloom duck's kDuckThresh/kDuckFull above.
//
// Sized from the envelope's measured distribution rather than from its peak
// (re-measured 2026-08-22 after the detector moved behind the MORPH/LEVEL gain,
// which rescaled every figure by the boot morph's 0.7071; three rigs, 10 s each
// after a 1 s settle, at the boot MORPH and LVL, identity window):
//
//   FLOW drones  min 0.0296  p10 0.0495  p50 0.0790  p90 0.1099  max 0.1402  <0.02:  0.0%
//   STEP plucks  min 0.0021  p10 0.0122  p50 0.0324  p90 0.0940  max 0.1257  <0.02: 25.5%
//   STEP sparse  min 0.0000  p10 0.0000  p50 0.0000  p90 0.0213  max 0.1132  <0.02: 89.6%
//
// THRESH 0.015 is the silence gate, set at about half the quietest SUSTAINED
// deck's floor: the sparse rig sits at exactly 0 for 89.6 % of its run while the
// drone rig never drops below 0.0296, so this lets a silent priority deck stop
// ducking without ever gating a sounding one.
// FULL 0.11 is the drone rig's p90 -- the level a deck reaches when it is
// properly sounding, not a peak it touches once. Choosing the p90 rather than
// the max is what makes the floor genuinely reachable instead of a corner case.
// Both are by-ear STARTING POINTS like the three above; what is measured is the
// distribution they are sized against, not that they sound right.
//
// These are in POST-GAIN units, so they are not comparable to a raw part
// output, and a deck played quiet ducks less by design -- that is the point of
// the detector's placement, not a defect to tune out.
constexpr float kChokeDuckThresh = 0.015f;  // below: no duck, whatever the knob
constexpr float kChokeDuckFull   = 0.11f;   // at and above: the floor is reached
}

void Instrument::init(float sample_rate) { init(sample_rate, FxMem{}); }

void Instrument::init(float sample_rate, const FxMem& mem) {
    _sr = sample_rate;
    _reverb = mem.reverb;
    _parts[PART_A].init(sample_rate, 0x1234abcdu,
                        mem.echo[PART_A][0], mem.echo[PART_A][1],
                        mem.sampler_buf[PART_A], mem.sampler_frames,
                        mem.bbd[PART_A][0], mem.bbd[PART_A][1]);
    _parts[PART_B].init(sample_rate, 0x9e3779b9u,
                        mem.echo[PART_B][0], mem.echo[PART_B][1],
                        mem.sampler_buf[PART_B], mem.sampler_frames,
                        mem.bbd[PART_B][0], mem.bbd[PART_B][1]);
    if (_reverb) _reverb->init(sample_rate);
    for (int p = 0; p < PART_COUNT; ++p) {
        _rev_dry[p].init(sample_rate, kMixSmoothS);
        _rev_wet[p].init(sample_rate, kMixSmoothS);
    }
    _rev_primed = false;
    _rev_asleep = false;
    _rev_return_gain = 1.f;
    _rev_return_step = 1.f / (kRevReturnFadeS * sample_rate);
    _duck_gain = 1.f;
    _duck_target = 1.f;
    _duck_residual = 0.f;
    _duck_armed = false;   // set_reverb_decay() re-arms on the next param push
    set_reverb_mix(kDefaultReverbMix);   // convenience overload -> both decks
    _limiter.init();
    _duck_keep_down = std::exp(-1.f / (kDuckDownS * sample_rate));
    _duck_keep_up   = std::exp(-1.f / (kDuckUpS * sample_rate));
    _choke_env = 0.f;
    _choke_atk = 1.f - std::exp(-1.f / (kChokeDuckAtkS * sample_rate));
    _choke_rel = 1.f - std::exp(-1.f / (kChokeDuckRelS * sample_rate));
    // The knob glide, not the envelope: it exists so a CHOKE move cannot step
    // the mix gain. Shares kMixSmoothS with the reverb crossfade because it is
    // the same job on the same bus, not because the two are coupled.
    _choke_depth.init(sample_rate, kMixSmoothS);
    _choke_depth.reset(0.f);
    _choke_duck_gain = 1.f;
    _center.init(sample_rate, 0x5ce47e12u);
    _ctrl_ctr = 0;
    set_tempo_bpm(_bpm);
}

void Instrument::set_reverb_mix(int part, float n) {
    n = clampf(n, 0.f, 1.f);
    if (n <= 0.f)      { _rev_dry_target[part] = 1.f; _rev_wet_target[part] = 0.f; }
    else if (n >= 1.f) { _rev_dry_target[part] = 0.f; _rev_wet_target[part] = 1.f; }
    else {
        _rev_dry_target[part] = std::cos(n * kHalfPi);   // equal-power crossfade
        _rev_wet_target[part] = std::sin(n * kHalfPi);   // rides the SEND, not the return
    }
    if (_rev_wet_target[part] > 0.f) _rev_asleep = false;   // wake into the cleared room
}

void Instrument::set_reverb_mix(float n) {   // convenience: both decks together
    set_reverb_mix(PART_A, n);
    set_reverb_mix(PART_B, n);
}

void Instrument::set_tempo_bpm(float bpm) {
    // The real single door (task 12 finding 2): Transport::set_bpm guards its
    // own readers, but SuperModulator::set_tempo_bpm and Flux::set_bpm each
    // keep their own _bpm and bypass Transport entirely -- guarding only
    // Transport left both reachable with an unvalidated value, including
    // host/render/scenario.cpp's unvalidated scenario-file `bpm` field, which
    // forwards straight into this method. Dropped silently, same policy as
    // Transport::set_bpm: the last good tempo is kept rather than clamped to
    // an arbitrary floor.
    if (!(bpm > 0.f) || !std::isfinite(bpm)) return;
    _bpm = bpm;
    _apply_tempo();
}

void Instrument::set_pace(float norm) {
    if (!std::isfinite(norm)) return;
    const float m = pace_mult(norm);
    if (m == _pace) return;              // Fireflow pushes every knob per tick
    _pace = m;
    _center.set_pace_anchor();           // the clock grid follows the knob
    _apply_tempo();
}

void Instrument::_apply_tempo() {
    _center.set_tempo_bpm(_bpm * _pace);
    for (auto& p : _parts) p.mod().set_pace(_pace);
    for (auto& p : _parts) p.mod().set_tempo_bpm(_bpm);
    for (auto& p : _parts) p.fx().set_bpm(_bpm);
    for (auto& p : _parts) p.fx().set_rhythm_pace(_pace);
}

void Instrument::process(const float* inL, const float* inR,
                         float* outL, float* outR, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const float in_l = inL ? inL[i] : 0.f;
        const float in_r = inR ? inR[i] : 0.f;
        if (_ctrl_ctr == 0) {                 // control-rate center update (per 96 samples)
            _center.update(_parts[PART_A].mod(), _parts[PART_B].mod(),
                           _parts[PART_A], _parts[PART_B]);
            // Excitation bus, cross-deck hand-over (spec §6, Task 10): each
            // part gets the SIBLING's dry mono output as latched at the end
            // of the block that just finished (see the capture point below,
            // after MORPH panning). _dry_tap still holds last block's values
            // here -- this block's capture has not run yet -- so the swap is
            // exactly "read the previous block", the same one-block lag
            // Task 9 gets for free from _control_tick() running before
            // _fx.process() each sample.
            _parts[PART_A].set_other_deck_tap(_dry_tap[PART_B]);
            _parts[PART_B].set_other_deck_tap(_dry_tap[PART_A]);
            // Each deck's FLUX derives its THIN pattern from the SIBLING's
            // PITCH-lane rhythm. Publication keeps the same cadence and
            // cross-over as the excitation tap above.
            _parts[PART_A].fx().set_rhythm(rhythm(PART_B));
            _parts[PART_B].fx().set_rhythm(rhythm(PART_A));

            // Bloom duck target (spec 2026-08-03): feedforward from the
            // room's own return envelope -- seconds-slow by construction,
            // so it cannot pump the way the dead return-side rides did.
            float new_duck_target = 1.f;
            if (_reverb && _duck_armed) {
                const float env = _reverb->return_level();
                new_duck_target = env <= kDuckThresh
                    ? 1.f
                    : 1.f - (1.f - kDuckFloor)
                          * std::min(1.f, (env - kDuckThresh)
                                              / (kDuckFull - kDuckThresh));
            }
            // Re-base the residual onto the new target when (and only when)
            // the target actually moves -- see _duck_residual's declaration
            // in instrument.h for why this, not a per-sample rebuild, is what
            // keeps the slew from stalling.
            if (new_duck_target != _duck_target) {
                _duck_residual += _duck_target - new_duck_target;
                _duck_target = new_duck_target;
            }
            _ctrl_ctr = Center::kCtrlInterval;
        }
        --_ctrl_ctr;

        // MORPH/LEVEL gains, hoisted above the CHOKE block because the duck's
        // detector needs them (see below). Value-identical to reading them at
        // the mix: within this loop _center is touched only by update() in the
        // control-tick branch above, and nothing between here and the mix calls
        // back into it -- Center::update takes the parts by reference, but a
        // Part never reaches Center. So this is one read, not two.
        const float ga = _center.gain_a();
        const float gb = _center.gain_b();

        // CHOKE: sidechain duck + event-priority between the decks (spec
        // 2026-07-16 choke-priority, rev. 2 discrete zones + rev. 3 env
        // windows; rev. 4 = plan 2026-08-22-choke-sidechain-duck). The knob is
        // continuous now, three zones per side by |c|; negative = A priority.
        const int pri = _choke > 0.f ? PART_B : PART_A;
        const int yld = 1 - pri;
        const float amt = _choke < 0.f ? -_choke : _choke;

        // Read the bus at the TOP of the sample, for BOTH decks, before either
        // one runs. Doing it here rather than between the two process() calls
        // is what makes the latency CHOKE-independent: a "whoever runs first
        // feeds whoever runs second" bus would be 0 samples one way and 1 the
        // other, and would swap as the knob crossed zero -- and a mutual
        // routing would then contain a 0-sample algebraic loop.
#if SPKY_DECK_BUS
        for (int p = 0; p < PART_COUNT; ++p)
            _parts[p].set_deck_in(_deck_tap[1 - p][0], _deck_tap[1 - p][1]);
#endif

        float pl[PART_COUNT], prr[PART_COUNT];
        float psl[PART_COUNT], psr[PART_COUNT];
        _parts[pri].set_inhibit(false);   // knob flips must never strand a part
        _parts[pri].process(in_l, in_r, pl[pri], prr[pri], psl[pri], psr[pri]);

        // The duck stage, read straight after the priority deck ran so it
        // answers THIS sample. One follower, always on whichever deck has
        // priority right now: on a sign flip the roles swap and this state
        // carries over, which is what keeps the crossing continuous.
        //
        // The detector rectifies the priority deck's output AFTER its MORPH and
        // LEVEL gain, not the raw part output. A sidechain follows what you
        // HEAR: on the raw signal, a deck morphed away or with LVL at 0 was
        // inaudible and still pulled the other one to the floor. It is also the
        // same rule the reverb send obeys three lines down ("a morphed-away deck
        // injects no new reverb", M4) -- reading the raw output here gave the
        // engine two different answers to whether a silent deck still acts on
        // the bus.
        const float pri_gain = (pri == PART_A) ? ga : gb;
        const float rect = std::max(std::fabs(pl[pri] * pri_gain),
                                    std::fabs(prr[pri] * pri_gain));
        _choke_env += (rect > _choke_env ? _choke_atk : _choke_rel)
                    * (rect - _choke_env);
        // Depth: the duck zone (0 < |c| <= 0.5) maps 0 -> 1, and both choke
        // zones sit pinned at 1 above it. Glided, so a knob move cannot step
        // the mix gain.
        const float depth = _choke_depth.process(std::min(amt, 0.5f) * 2.f);
        // Floor-style, like the Bloom duck, and windowed for the same reason:
        // at full depth with the priority deck at or above kChokeDuckFull the
        // yielding deck sits at kChokeDuckFloor, never muted; below
        // kChokeDuckThresh it is not ducked at all, whatever the knob says.
        // See the window's declaration for the distribution it is sized to.
        // Exactly 1.0f whenever depth is 0 -- which is the whole of CHOKE's
        // noon bypass, since a multiply by 1.0f is exact and the mix below
        // therefore stays bit-identical without a branch.
        const float env_n = _choke_env <= kChokeDuckThresh
            ? 0.f
            : std::min(1.f, (_choke_env - kChokeDuckThresh)
                                / (kChokeDuckFull - kChokeDuckThresh));
        _choke_duck_gain = 1.f - depth * (1.f - kChokeDuckFloor) * env_n;
        float duck[PART_COUNT] = { 1.f, 1.f };
        duck[yld] = _choke_duck_gain;             // the priority deck never ducks

        // Choke zones, on top of the duck. Held (0.5 < |c| <= 0.75): blocked
        // while the priority side HOLDS a note — STEP: gate high (note +
        // sustain, the tail is free); FLOW: a drone is always "on". Decay
        // (|c| > 0.75): additionally through the whole audible decay (env
        // floor 1e-4). Below 0.5 the events are free and the duck is alone.
        if (amt > 0.5f) {
            bool window = _parts[pri].gate() || _parts[pri].flow();
            if (!window && amt > 0.75f)
                window = _parts[pri].max_voice_env() > 1e-4f;
            _parts[yld].set_inhibit(window);
        } else {
            _parts[yld].set_inhibit(false);
        }
        _parts[yld].process(in_l, in_r, pl[yld], prr[yld], psl[yld], psr[yld]);

        const float al = pl[PART_A],  ar = prr[PART_A];
        const float bl = pl[PART_B],  br = prr[PART_B];
        const float asl = psl[PART_A], asr = psr[PART_A];
        const float bsl = psl[PART_B], bsr = psr[PART_B];

        // Excitation bus, cross-deck capture (spec §6, Task 10): write this
        // sample's dry mono output into _dry_tap only on the block's last
        // sample (the instant _ctrl_ctr -- already decremented above --
        // reaches 0). This guard is a micro-optimisation, not what makes the
        // hand-over "one block late": the top-of-loop read above only ever
        // runs once per block too, so it always sees whichever value was
        // written LAST before that boundary -- unconditional writes every
        // sample would leave the SAME value sitting there when the read
        // happens, just after 96 redundant writes instead of one. What the
        // guard actually buys is skipping those 96 writes (task-10-review.md
        // finding 6 -- an earlier version of this comment overclaimed that
        // the guard itself produced the one-block lag; it does not, the
        // once-per-block READ does).
        if (_ctrl_ctr == 0) {
            _dry_tap[PART_A] = 0.5f * (al + ar);
            _dry_tap[PART_B] = 0.5f * (bl + br);
        }

        // Write at the BOTTOM, every sample -- unlike _dry_tap's once-per-block
        // guard above, which is a control-rate quantity.
#if SPKY_DECK_BUS
        _deck_tap[PART_A][0] = al;  _deck_tap[PART_A][1] = ar;
        _deck_tap[PART_B][0] = bl;  _deck_tap[PART_B][1] = br;
#endif

        // MORPH blend (null-reverb path keeps this). The CHOKE duck joins HERE
        // and nowhere earlier: al/ar/bl/br, _dry_tap and _deck_tap above are
        // already written and must stay untouched -- same argument as the
        // Bloom duck's at the reverb mix below. duck[] is { 1, 1 } whenever
        // CHOKE is at noon, and a multiply by 1.0f is exact.
        float l = al * ga * duck[PART_A] + bl * gb * duck[PART_B];
        float r = ar * ga * duck[PART_A] + br * gb * duck[PART_B];
        if (_reverb) {
            if (!_rev_primed) {              // snap the mix set before the first block
                for (int p = 0; p < PART_COUNT; ++p) {
                    _rev_dry[p].reset(_rev_dry_target[p]);
                    _rev_wet[p].reset(_rev_wet_target[p]);
                }
                if (_rev_wet_target[PART_A] == 0.f && _rev_wet_target[PART_B] == 0.f) {
                    _reverb->clear(); _rev_asleep = true;
                }
                _duck_gain = 1.f;
                _duck_target = 1.f;
                _duck_residual = 0.f;
                _rev_return_gain = 1.f;
                _rev_primed = true;
            }
            const float dga = _rev_dry[PART_A].process(_rev_dry_target[PART_A]);
            const float dgb = _rev_dry[PART_B].process(_rev_dry_target[PART_B]);
            const float wga = _rev_wet[PART_A].process(_rev_wet_target[PART_A]);
            const float wgb = _rev_wet[PART_B].process(_rev_wet_target[PART_B]);
            // Per-deck dry: each deck's dry gain rides its own cos before the
            // MORPH sum, so one deck can be wet-only while the other stays dry.
            // Duck multiplies the dry SUM only. al/ar/bl/br must stay
            // untouched: they feed _dry_tap (BODY's excitation) and
            // _deck_tap, which must not starve when the bloom peaks.
            // Per-sample ride toward the raster target. When idle both are
            // exactly 1.0 and this is a multiply-add by zero.
            //
            // NOT g = target + (g - target) * keep computed from the ROUNDED
            // g each sample: that reconstructs the residual by re-adding it
            // to target every sample, and the addition rounds the residual to
            // target's own precision grid (ulp ~1.2e-7 near 1.0) before the
            // *next* sample ever sees it -- so it stalls at the exact same
            // ~0.994 (measured, 48 kHz) as the additive form g += c*(t-g)
            // it was meant to replace; the two are algebraically identical
            // and share the same rounding failure (verified both ways with a
            // float32 simulation before writing this). `_duck_residual`
            // instead persists as its OWN float, decayed by a pure
            // multiply -- no addition, so no absorption -- and is only
            // re-based onto target up above, once per control tick and only
            // when the target actually moves. `_duck_gain` here is a fresh
            // read-out, never fed back as state, so it correctly rounds to
            // exactly `_duck_target` once the residual's magnitude drops
            // below half its ulp -- which happens in finite time because the
            // state itself never stalls.
            const float keep = _duck_target < _duck_gain ? _duck_keep_down
                                                         : _duck_keep_up;
            _duck_residual *= keep;
            _duck_gain = _duck_target + _duck_residual;
            l = (al * ga * dga * duck[PART_A] + bl * gb * dgb * duck[PART_B]) * _duck_gain;
            r = (ar * ga * dga * duck[PART_A] + br * gb * dgb * duck[PART_B]) * _duck_gain;
            if (!_rev_asleep) {
                // Per-deck send: the equal-power wet curve (sin) rides the SEND
                // -- one shared room has only one return. MORPH fades the send
                // too (M4 rule): a morphed-away deck injects no new reverb.
                // So does the CHOKE duck: a ducked deck must not go on filling
                // the room at full level, or the duck would only be audible on
                // the dry half of a wet patch.
                float wl, wr;
                _reverb->process(asl * ga * wga * duck[PART_A] + bsl * gb * wgb * duck[PART_B],
                                 asr * ga * wga * duck[PART_A] + bsr * gb * wgb * duck[PART_B],
                                 wl, wr);
                const bool closing = wga == 0.f && wgb == 0.f &&
                    _rev_wet_target[PART_A] == 0.f && _rev_wet_target[PART_B] == 0.f;
                if (closing) {
                    _rev_return_gain -= _rev_return_step;      // linear, exact zero
                    if (_rev_return_gain < 0.f) _rev_return_gain = 0.f;
                } else if (_rev_return_gain < 1.f) {
                    _rev_return_gain += _rev_return_step;      // reopened mid-fade
                    if (_rev_return_gain > 1.f) _rev_return_gain = 1.f;
                }
                l += wl * _rev_return_gain;  // wl already carries kWetGain; the return joins at unity
                r += wr * _rev_return_gain;
                if (closing && _rev_return_gain == 0.f) {
                    _reverb->clear();        // clear-on-sleep: waking starts empty
                    _rev_asleep = true;      // Oliverb CPU is off until a MIX reopens
                    _rev_return_gain = 1.f;  // room is empty; the next wake starts at unity
                }
            }
            // asleep: dga/dgb have snapped to 1 (both decks mix 0), so l/r stay full dry
        }
        _limiter.process(l, r);   // master ceiling (M6 engine delta 3, delivered early)
        outL[i] = l;
        outR[i] = r;
    }
}
