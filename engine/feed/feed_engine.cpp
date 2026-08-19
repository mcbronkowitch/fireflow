#include "feed/feed_engine.h"
#include "synth/synth_engine.h"
#include "util/fast_sin.h"
#include "util/fast_tanh.h"
#include "util/math.h"
#include <cmath>

namespace spky {

static_assert(FeedEngine::kCtrlInterval == SynthEngine::kCtrlInterval,
              "FEED's control raster must be the instrument's, or its glides "
              "and Part::_control_tick's pushes fall off each other's grid");

namespace {

// The instrument's pitch contract: 0..1 = 36 semitones, 110 * 8^p. This is a
// duplicate of the file-static of the same name in synth/synth_engine.cpp,
// written out here rather than reached for through that file's anonymous
// namespace. If the two ever disagree, every engine plays a different scale --
// tests/test_feed_engine.cpp's pitch_to_hz_ref is a third copy for exactly
// that reason, so a gate compares against the law and not against this line.
// std::pow at control rate only, never per sample.
inline float pitch_to_hz(float p) { return 110.f * std::pow(8.f, clampf(p, 0.f, 1.f)); }

}  // namespace

void FeedEngine::init(float sample_rate) {
    _sr = sample_rate > 1.f ? sample_rate : 48000.f;
    _inv_sqrt_pairs = 1.f / std::sqrt(static_cast<float>(feed_cfg::kPairs));
    _env.init(_sr);
    _bank.init(_sr);
    _rng.seed(_seed);
    _draw_individual();
    // Derive every cached knob mapping from the knob value it belongs to,
    // rather than letting the member initialisers carry one value for the knob
    // and another for its ratio. Without this the boot state holds _rise_n 0.5
    // beside a _rise_ratio that belongs to knob 0. set_filt additionally reads
    // _sr, so it MUST run after _sr is assigned -- otherwise a 44.1 kHz host
    // gets a DAMP coefficient computed for 48 kHz, which is the kind of
    // ordering bug a render never shows.
    set_attack(_rise_n);
    set_decay(_fall_n);
    set_resonance(_ratio_n);
    set_filt(_damp_t);
    _ctrl_ctr = 0;
    _sub_phase = 0.f;
    _sub_inc = 0.f;
    _rebuild_allocation();
}

void FeedEngine::set_targets(const float* t, float /*tune*/) {
    _bond     = clampf(t[LANE_SOURCE], 0.f, 1.f);
    _spread_n = clampf(t[LANE_SIZE],   0.f, 1.f);
    _pitch_n  = clampf(t[LANE_PITCH],  0.f, 1.f);
    _depth_n  = clampf(t[LANE_MOTION], 0.f, 1.f);
    _level    = clampf(t[LANE_LEVEL],  0.f, 1.f);
}

// Everything NEW redraws, and nothing else. Called from init() and reseed(),
// never from a retune.
void FeedEngine::_draw_individual() {
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        // The SPREAD signature: one value per pair, and then forced to sum to
        // zero WITHIN each tone group, which is what makes spec 3.4's
        // symmetric-centre claim exact rather than statistical (G13). The
        // zeroing happens in _rebuild_allocation, the only place that knows
        // the grouping.
        _spread_sig[i] = _rng.next_bipolar();
        // Small deterministic per-pair feedback offsets: with one shared
        // fb_amount the only individuality is the detune signature, which is
        // thin if SWARM's "it always sounds the same" is the bar. These make
        // each pair tip at a slightly different BOND position, so the cliff
        // becomes a gradient the ear can ride instead of an edge (spec 3.4).
        // Per-pair RATIO offsets were considered for the same job and rejected
        // -- they push the sidebands inharmonic, which is exactly the detune
        // section 2.6 forbids.
        _fb_offset[i] = _rng.next_bipolar() * feed_cfg::kFbOffsetRange;
    }
}

// Pairs onto chord tones, then the spread, then the pitch attenuation. Runs at
// control rate, every tick.
void FeedEngine::_rebuild_allocation() {
    // How many chord tones the bank voices. Every voiced tone keeps a group of
    // at least kPairsPerTone, because SPREAD detunes a tone's pairs against
    // each other and a group of one has nothing to beat against (plan open
    // point 4). Sounding fewer tones than the chord holds is established here:
    // a BODY deck sounds only the root.
    const int cap = feed_cfg::kPairs / feed_cfg::kPairsPerTone;
    _voiced_n = _chord_n < cap ? _chord_n : cap;
    if (_voiced_n < 1) _voiced_n = 1;

    // SPREAD in cents. Two segments so the lower half stays in single digits
    // (spec 3.4) while the top still reaches dense roughness.
    const float s = _spread_n;
    _spread_ct = s <= 0.5f
        ? feed_cfg::kSpreadKneeCt * (s * 2.f)
        : feed_cfg::kSpreadKneeCt +
          (feed_cfg::kSpreadMaxCt - feed_cfg::kSpreadKneeCt) * ((s - 0.5f) * 2.f);

    // Per-group zero-mean of the signature. Groups are strided, not blocked:
    // pair i belongs to tone i % _voiced_n, so a chord that grows re-groups
    // without moving pair 0 off the root (G25).
    float group_sum[kMaxChord] = {};
    int   group_cnt[kMaxChord] = {};
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        const int g = i % _voiced_n;
        group_sum[g] += _spread_sig[i];
        ++group_cnt[g];
    }

    // The signature is zero-MEAN, which makes the group's geometric mean the
    // tone exactly; scaling it so the widest pair sits at +-_spread_ct is what
    // makes the knob's number mean cents rather than "cents times whatever the
    // draw happened to be". Without it a lucky seed would reach a third of the
    // range the caption promises.
    float peak = 0.f;
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        const int g = i % _voiced_n;
        if (group_cnt[g] > 1) {
            const float d = std::fabs(_spread_sig[i] -
                                      group_sum[g] / static_cast<float>(group_cnt[g]));
            if (d > peak) peak = d;
        }
    }
    const float norm = peak > 1e-6f ? 1.f / peak : 0.f;

    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        const int g = i % _voiced_n;
        const float mean = group_sum[g] / static_cast<float>(group_cnt[g]);
        // A group of one gets exactly zero offset. It cannot beat against
        // itself, and giving it a nonzero offset would move that tone's pitch
        // -- the detune spec 2.6 forbids.
        const float sig = group_cnt[g] > 1 ? (_spread_sig[i] - mean) * norm : 0.f;
        // Symmetric in CENTS, i.e. in the log-frequency domain, which is what
        // an ear hears as symmetric (plan open point 2).
        const float ct = sig * _spread_ct;
        const float tone_n = _chord[g];
        const float hz = pitch_to_hz(tone_n) * std::exp2(ct * (1.f / 1200.f));

        // The pitch-dependent feedback attenuation (spec 3.2.2). ONE
        // attenuation, BOTH terms -- it multiplies the blended input, so it
        // dampens the neighbour term exactly as much as the self-feedback.
        // High chord tones are therefore not only more stable, they are also
        // less INFECTED: BOND audibly weakens toward the top of a chord. That
        // is a decision, not a side effect (spec 3.2); a review that files
        // "coupling doesn't reach high notes" as a defect should be pointed
        // here -- the fireflow-bbd-range-cap-is-flow-only precedent.
        const float atten = clampf(1.f - feed_cfg::kFbPitchSlope * tone_n,
                                   feed_cfg::kFbAttenMin, 1.f);
        _bank.set_fb_amount(i, feed_cfg::kFbBaseCycles * atten *
                               (1.f + _fb_offset[i]));

        // Equal power across the bank, and a deterministic pan spread.
        // _hit_gain is here AND on SUB in process(), both -- an accent that
        // scaled only the ring would change the balance between the network
        // and its foundation, which is a timbre change wearing a dynamics
        // costume.
        const float amp = _level * _hit_gain * _env.value() * _inv_sqrt_pairs;
        const float pan = (feed_cfg::kPairs > 1
                           ? (-1.f + 2.f * i / (feed_cfg::kPairs - 1)) : 0.f) * _width;
        _bank.set_target(i, hz, amp, pan);
    }
}

// The order matters and is stated here so a later edit cannot quietly reorder
// it: envelope coefficients, then the bank's global parameters, then the
// allocation (which reads _env.value() for the amplitude).
void FeedEngine::_control_tick() {
    // Until something explicitly names a pitch, the ring's root IS the PITCH
    // lane. The ring runs whether or not it has been triggered (spec 2.1), so
    // a deck that has not been struck yet still has to sit somewhere, and the
    // only defensible somewhere is the lane the player is holding -- not the
    // 0.5 the member initialiser happens to carry. On a real deck this governs
    // the boot instant only: Part::_control_tick pushes set_chord every tick,
    // and from the first push the composed chord is the authority. Without it
    // the pitch-dependent feedback attenuation would read a constant (G12).
    if (!_pitch_named) _chord[0] = _pitch_n;
    _env.set_times(_rise_s(), _fall_s());
    // CHOKE wins over both the FLOOR knob and FLOW's minimum: while held, the
    // sustain is 0 and the drone decays out. This is read fresh every tick
    // rather than written once in set_hold, because otherwise the very next
    // tick would restore the floor and the hold would last 96 samples.
    _env.set_sustain(_hold ? 0.f
                           : (_flow ? std::max(_floor_n, feed_cfg::kFlowFloorMin)
                                    : _floor_n));
    _bank.set_bond(_bond);
    _bank.set_index(_depth_n * feed_cfg::kIndexMaxCycles * _env.value());
    _bank.set_ratio(_ratio);
    _bank.set_damp_coef(_damp_coef());
    _sub_inc = 0.5f * pitch_to_hz(_chord[0]) / _sr;   // one octave below the root
    _rebuild_allocation();
}

void FeedEngine::process(float& outL, float& outR) {
    if (--_ctrl_ctr <= 0) { _ctrl_ctr = kCtrlInterval; _control_tick(); }
    // FLOW's drone promise, deferred to here rather than fired inside the
    // setter for the reason SynthEngineT defers it: the targets are fresh here
    // and stale at the setter.
    if (_auto_pending) { _auto_pending = false; _env.trigger(); }
    const float env = _env.process();

    float l = 0.f, r = 0.f;
    _bank.process(l, r);

    // SUB: one sine an octave below the root. Not part of the ring and not
    // coupled -- it is the foundation the network stands on, and coupling it
    // would put the one stable thing in the deck inside the unstable loop.
    _sub_phase += _sub_inc;
    _sub_phase -= std::floor(_sub_phase);
    const float sub = fast_sin(_sub_phase) * _sub_n * feed_cfg::kSubMax * env
                    * _level * _hit_gain;
    l += sub;
    r += sub;

    // The ceiling (spec 3.3), the BodyVoice::kFlowSatCeil pattern and for the
    // same stated reason: where opening a path lets a value diverge, add the
    // bounding nonlinearity the instrument already has rather than re-imposing
    // a limit downstream.
    outL = feed_cfg::kSatCeil * fast_tanh(l * feed_cfg::kSatInv);
    outR = feed_cfg::kSatCeil * fast_tanh(r * feed_cfg::kSatInv);
}

void FeedEngine::trigger(float pitch_norm) {
    // A trigger RETUNES the ring and injects energy; it does not start it
    // (spec 2.1). The network was already running.
    _chord[0] = clampf(pitch_norm, 0.f, 1.f);
    _chord_n = 1;
    _pitch_named = true;
    // The hit half of the accent, composed the way SynthEngineT composes it:
    // a scale on the strike, not a replacement for it.
    _hit_gain = 1.f - (1.f - feed_cfg::kAccentVelFloor) * _accent;
    _env.trigger();          // rises from the CURRENT level: click-free (G22)
    _auto_pending = false;
    _rebuild_allocation();   // the retune lands as a glide, this tick
}

void FeedEngine::trigger_chord(const float* p, int n) {
    if (n < 1) return;
    if (n > kMaxChord) n = kMaxChord;
    _set_chord_tones(p, n);
    _hit_gain = 1.f - (1.f - feed_cfg::kAccentVelFloor) * _accent;
    _env.trigger();          // ONE hit, whatever the chord holds
    _auto_pending = false;
    _rebuild_allocation();
}

void FeedEngine::set_chord(const float* p, int n) {
    // Arrives once per control tick from Part::_control_tick, so this must be
    // cheap and must NOT touch the envelope: a COLOR move re-voices the network
    // as a glissando, with no retrigger (spec section 5).
    if (n < 1) return;
    if (n > kMaxChord) n = kMaxChord;
    _set_chord_tones(p, n);
}

// Stores the tones SORTED ASCENDING, which is what makes i % _voiced_n a
// nearest-neighbour allocation rather than an arbitrary one: sorted, tone 0 is
// the root, so a chord that grows upward leaves pair 0 -- and every pair whose
// index is 0 mod the voiced count -- exactly where it was (G25).
void FeedEngine::_set_chord_tones(const float* p, int n) {
    for (int i = 0; i < n; ++i) _chord[i] = clampf(p[i], 0.f, 1.f);
    // Insertion sort, n <= 4.
    for (int i = 1; i < n; ++i) {
        const float v = _chord[i];
        int j = i - 1;
        while (j >= 0 && _chord[j] > v) { _chord[j + 1] = _chord[j]; --j; }
        _chord[j + 1] = v;
    }
    _chord_n = n;
    _pitch_named = true;
}

void FeedEngine::set_cycle(float seconds) {
    _cycle_s = seconds > 1e-4f ? seconds : 1e-4f;
}

void FeedEngine::set_flow(bool flow) {
    if (flow == _flow) return;
    _flow = flow;
    // Nothing is demoted on either edge: FEED has no voices to demote, and the
    // ring runs either way. What changes is the envelope's sustain, which
    // _control_tick reads fresh, and whether the drone re-arms.
    _auto_pending = flow && !_hold && !_env.active();
}

void FeedEngine::set_hold(bool on) {
    if (on == _hold) return;
    _hold = on;
    if (on) {
        // CHOKE: the sustain goes to 0 while holding, which IS the demotion
        // release -- the same coefficient now converges to zero (env.h). The
        // floor decays out click-free and auto-retrigger stops. _control_tick
        // re-reads _hold every tick, so this is not a value the next tick
        // overwrites.
        _env.set_sustain(0.f);
        _auto_pending = false;
    } else if (_flow) {
        _auto_pending = true;
    }
}

void FeedEngine::set_width(float n) { _width = clampf(n, 0.f, 1.f); }

void FeedEngine::set_accent(float a) { _accent = clampf(a, 0.f, 1.f); }

// RISE and FALL as ratios of the master cycle, the SynthEngineT law so the two
// engines' knobs mean the same thing: attack 0.002 * 250^n of the cycle,
// decay 0.1 * 80^n. std::pow at CONTROL rate only -- both are recomputed in
// the setters, not in _control_tick, because the knobs move at gesture rate and
// Env::set_times already guards against recomputing identical coefficients.
void FeedEngine::set_attack(float n) {
    _rise_n = clampf(n, 0.f, 1.f);
    _rise_ratio = 0.002f * std::pow(250.f, _rise_n);
}

void FeedEngine::set_decay(float n) {
    _fall_n = clampf(n, 0.f, 1.f);
    _fall_ratio = 0.1f * std::pow(80.f, _fall_n);
    // FLOOR rides the top quarter (the plan's control map). Below the fold
    // start the deck blooms and dies; above it the tail stops decaying to zero
    // and stands, reaching an endless drone at DEC 1. The knob keeps its whole
    // travel for FALL, so nothing about the tail's LENGTH is given up.
    _floor_n = clampf((_fall_n - feed_cfg::kFloorFoldStart) /
                      (1.f - feed_cfg::kFloorFoldStart), 0.f, 1.f);
}

// Task 8: the integer magnet. Until then the knob is stored and the ratio is
// the neutral 1:1, so this task's gates measure a ring whose RATIO is not yet
// under test and cannot be blamed for a failure.
void FeedEngine::set_resonance(float n) { _ratio_n = clampf(n, 0.f, 1.f); _ratio = 1.f; }

void FeedEngine::set_sub(float n) { _sub_n = clampf(n, 0.f, 1.f); }

void FeedEngine::set_filt(float t) { _damp_t = clampf(t, -1.f, 1.f); }    // Task 8

void FeedEngine::reseed(uint32_t s) { _rng.seed(s); }                     // Task 9

float FeedEngine::_rise_s() const {
    return clampf(_rise_ratio * _cycle_s, SynthEngine::kAttackFloorS, 20.f);
}

float FeedEngine::_fall_s() const {
    // The ring half of the STEP accent, gated by the DEC knob exactly as
    // SYNTH/WAVE/BODY do it: DEC 0 leaves ring time untouched, so a player who
    // never raises DEC never hears the accent shorten a note.
    const float acc = 1.f - (1.f - feed_cfg::kAccentDecFloor) * _accent * _fall_n;
    return clampf(_fall_ratio * _cycle_s * acc,
                  SynthEngine::kDecayMinS, SynthEngine::kDecayMaxS);
}

float FeedEngine::_damp_coef() const { return 1.f; }       // Task 8

float FeedEngine::pair_hz_for_test(int i) const { return _bank.hz(i); }
float FeedEngine::pair_amp_for_test(int i) const { return _bank.amp(i); }
float FeedEngine::pair_fb_amount_for_test(int i) const { return _bank.fb_amount(i); }

}  // namespace spky
