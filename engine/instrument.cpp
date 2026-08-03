#include "instrument.h"
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
    _duck_gain = 1.f;
    _duck_target = 1.f;
    _duck_armed = false;   // set_reverb_decay() re-arms on the next param push
    set_reverb_mix(kDefaultReverbMix);   // convenience overload -> both decks
    _limiter.init();
    _duck_down = 1.f - std::exp(-1.f / (kDuckDownS * sample_rate));
    _duck_up   = 1.f - std::exp(-1.f / (kDuckUpS * sample_rate));
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
    _center.set_tempo_bpm(bpm);
    for (auto& p : _parts) p.mod().set_tempo_bpm(bpm);
    for (auto& p : _parts) p.fx().set_bpm(bpm);
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
            if (_reverb && _duck_armed) {
                const float env = _reverb->return_level();
                _duck_target = env <= kDuckThresh
                    ? 1.f
                    : 1.f - (1.f - kDuckFloor)
                          * std::min(1.f, (env - kDuckThresh)
                                              / (kDuckFull - kDuckThresh));
            } else {
                _duck_target = 1.f;
            }
            _ctrl_ctr = Center::kCtrlInterval;
        }
        --_ctrl_ctr;

        // CHOKE: event-priority between the decks (spec 2026-07-16
        // choke-priority, rev. 2 discrete zones + rev. 3 env windows).
        // The panel snaps to -1/-0.5/0/+0.5/+1; negative = A priority.
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
        if (amt > 0.f) {
            // Stage 1 (|c| <= 0.5): blocked while the priority side HOLDS a
            // note — STEP: gate high (note + sustain, the tail is free);
            // FLOW: a drone is always "on". Stage 2 (|c| > 0.5): additionally
            // through the whole audible decay (env floor 1e-4).
            bool window = _parts[pri].gate() || _parts[pri].flow();
            if (!window && amt > 0.5f)
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

        const float ga = _center.gain_a();
        const float gb = _center.gain_b();
        float l = al * ga + bl * gb;          // MORPH blend (null-reverb path keeps this)
        float r = ar * ga + br * gb;
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
            _duck_gain += (_duck_target < _duck_gain ? _duck_down : _duck_up)
                          * (_duck_target - _duck_gain);
            l = (al * ga * dga + bl * gb * dgb) * _duck_gain;
            r = (ar * ga * dga + br * gb * dgb) * _duck_gain;
            if (!_rev_asleep) {
                // Per-deck send: the equal-power wet curve (sin) rides the SEND
                // -- one shared room has only one return. MORPH fades the send
                // too (M4 rule): a morphed-away deck injects no new reverb.
                float wl, wr;
                _reverb->process(asl * ga * wga + bsl * gb * wgb,
                                 asr * ga * wga + bsr * gb * wgb, wl, wr);
                l += wl;   // wl already carries kWetGain; the return joins at unity
                r += wr;
                if (wga == 0.f && wgb == 0.f &&
                    _rev_wet_target[PART_A] == 0.f && _rev_wet_target[PART_B] == 0.f) {
                    _reverb->clear();        // clear-on-sleep: waking starts empty
                    _rev_asleep = true;      // Oliverb CPU is off until a MIX reopens
                }
            }
            // asleep: dga/dgb have snapped to 1 (both decks mix 0), so l/r stay full dry
        }
        _limiter.process(l, r);   // master ceiling (M6 engine delta 3, delivered early)
        outL[i] = l;
        outR[i] = r;
    }
}
