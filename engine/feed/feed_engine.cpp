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
        const float amp = _level * _env.value() * _inv_sqrt_pairs;
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
    _env.set_sustain(_flow ? std::max(_floor_n, feed_cfg::kFlowFloorMin) : _floor_n);
    _bank.set_bond(_bond);
    _bank.set_index(_depth_n * feed_cfg::kIndexMaxCycles * _env.value());
    _bank.set_ratio(_ratio);
    _bank.set_damp_coef(_damp_coef());
    _sub_inc = 0.5f * pitch_to_hz(_chord[0]) / _sr;   // one octave below the root
    _rebuild_allocation();
}

void FeedEngine::process(float& outL, float& outR) {
    if (--_ctrl_ctr <= 0) { _ctrl_ctr = kCtrlInterval; _control_tick(); }
    const float env = _env.process();

    float l = 0.f, r = 0.f;
    _bank.process(l, r);

    // SUB: one sine an octave below the root. Not part of the ring and not
    // coupled -- it is the foundation the network stands on, and coupling it
    // would put the one stable thing in the deck inside the unstable loop.
    _sub_phase += _sub_inc;
    _sub_phase -= std::floor(_sub_phase);
    const float sub = fast_sin(_sub_phase) * _sub_n * feed_cfg::kSubMax * env * _level;
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
    _env.trigger();          // rises from the CURRENT level: click-free (G22)
    _rebuild_allocation();   // the retune lands as a glide, this tick
}

void FeedEngine::trigger_chord(const float* p, int n) {
    if (n < 1) return;       // Task 7: the whole chord, not just its root
    _chord[0] = clampf(p[0], 0.f, 1.f);
    _chord_n = 1;
    _pitch_named = true;
    _env.trigger();
    _rebuild_allocation();
}

void FeedEngine::set_chord(const float* /*p*/, int /*n*/) {}   // Task 7

void FeedEngine::set_cycle(float seconds) {
    _cycle_s = seconds > 1e-4f ? seconds : 1e-4f;
}

void FeedEngine::set_flow(bool flow) { _flow = flow; }   // Task 6: auto-retrigger

void FeedEngine::set_hold(bool on) { _hold = on; }       // Task 6: CHOKE

void FeedEngine::set_width(float n) { _width = clampf(n, 0.f, 1.f); }

void FeedEngine::set_accent(float a) { _accent = clampf(a, 0.f, 1.f); }

void FeedEngine::set_attack(float n) { _rise_n = clampf(n, 0.f, 1.f); }   // Task 6

// FLOOR rides the top quarter of the FALL knob (the plan's control map). The
// FALL half of the same knob is Task 6's; the FLOOR half lands here because
// every gate in this task needs a ring that stands still long enough to be
// measured, and at FLOOR 0 the deck is audible only for as long as one decay.
void FeedEngine::set_decay(float n) {
    _fall_n = clampf(n, 0.f, 1.f);
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

// Task 6 fills the two envelope helpers; until then they return a short attack
// and a medium decay, so this task's gates measure a ring whose envelope is
// not yet under test.
float FeedEngine::_rise_s() const { return 0.01f; }        // Task 6
float FeedEngine::_fall_s() const { return 1.0f; }         // Task 6
float FeedEngine::_damp_coef() const { return 1.f; }       // Task 8

float FeedEngine::pair_hz_for_test(int i) const { return _bank.hz(i); }
float FeedEngine::pair_amp_for_test(int i) const { return _bank.amp(i); }
float FeedEngine::pair_fb_amount_for_test(int i) const { return _bank.fb_amount(i); }

}  // namespace spky
