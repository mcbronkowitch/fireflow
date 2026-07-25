#pragma once
#include <cstdint>
#include "util/onepole.h"
#include "mod/rng.h"
#include "mod/song_form.h"
#include "mod/shuffle_grid.h"

namespace spky {

// One modulation lane: wavetable core -> gate (groove rank x DENSE) -> step/flow
// -> smooth -> range. Bipolar output in [-1,1]. Deterministic given its seed.
class ModLane {
public:
    // Control-raster interval of the tick() path (spec 2026-07-19
    // mod-plane-control-rate). part.cpp static_asserts this against
    // SynthEngine::kCtrlInterval -- the mod layer must not include synth.
    static constexpr int kTickInterval = 96;

    void init(float sample_rate, uint32_t seed);

    void set_rate_hz(float hz);
    void set_shape(float s);          // 0..1
    void set_density(float d) { _density = pg_clampf(d, 0.f, 1.f); }  // 0..1 -> how deep into the groove ranking (k of L cell notes)
    void set_step(bool on, int steps);
    void set_shuffle(float amount) { _shuffle_target = shuffle_amount(amount); }
    void set_fixed_slew(bool on);     // panel switch 3 middle position
    void set_smooth(float s);         // 0..1
    void set_range(float r);          // 0..1
    void set_variation(float v);      // -1..+1: renew / loop (0) / grow

    void set_melodic(bool m) { _melodic = m; }
    void set_form(Principle form);
    Principle form() const { return _song.selected_form; }
    void set_song(SongMode song);
    SongMode song() const { return _song.selected_song; }
    uint32_t song_position() const { return _song.phrase_index; }
    uint8_t active_pattern() const { return _song.active_pattern; }
    void set_principle(Principle p) { set_form(p); }
    void new_phrase();                 // audition a fresh phrase at the next STEP-mode wrap
#ifdef SPKY_TESTING
    const MelodyPattern& pattern_for_test(uint8_t index) const {
        return _song.patterns[index & 1u];
    }
    uint8_t cadence_slot_for_test() const { return _song.cadence_slot; }
    float bound_a_opening_for_test() const { return _song.bound_a_opening; }
    float rate_hz_for_test() const { return _rate_hz; }
#endif

    float process();                  // advance one sample, return post-range value
    float tick();                     // advance kTickInterval samples in one call

    bool  fired()   const { return _fired; }    // true on the sample a gated boundary fired
    // True on the sample the cycle phase wrapped (0 <= _phase < 1 crossing
    // back through 0). Distinct from fired(): a STEP lane can fire a
    // boundary on every step without wrapping, and a wrap in FLOW mode fires
    // exactly one boundary, so the two coincide there but not in STEP. Exists
    // so SuperModulator can maintain the onset-gap ring for LANE_PITCH
    // without ModLane itself carrying that state -- see super_modulator.cpp.
    bool  wrapped() const { return _wrapped; }
    bool  frozen() const { return _frozen; }  // last dice failed -> holding
    // GATE: melodic STEP sustains the composed note (age < hold); else high.
    bool  gate_state() const { return _step_mode ? (!_melodic || _note_age < _note_hold) : true; }
    // Step-mode-qualified sustain: true only while a melodic STEP note holds.
    // Unlike gate_state() this is false in FLOW and non-melodic lanes, so it
    // is safe to OR into a pulse-based gate without forcing it permanently high.
    bool  note_sustain() const { return _step_mode && _melodic && _note_age < _note_hold; }
    float phase()  const { return _phase; }
    // Step-clock factor on the cycle rate (spec 2026-07-17): 8/steps in STEP,
    // 1 in FLOW. The grid servo scales its transport target by this so a
    // synced bank locks its S-step cycle across S/8 divisions.
    float clock_scale() const { return _step_mode ? 8.f / static_cast<float>(_steps) : 1.f; }
    // Slice-groove side channel (spec 2026-07-22): the sampler needs to know
    // where in the phrase a fire sits and how long a step is in samples.
    int   cur_step() const { return _cur_step; }
    int   steps()    const { return _steps; }
    int step_at_phase(float phase) const {
        return shuffle_step_index(phase, _steps, _shuffle_latched);
    }
    // The shuffle amount this lane's current grid was built with. _shuffle_target
    // only reaches _shuffle_latched at an even step entry, so a live SHUFFLE turn
    // leaves the two apart for up to a step -- anyone deriving a position from
    // this lane's phase has to use the latched value or they are measuring
    // against boundaries that never existed.
    float shuffle_latched() const { return _shuffle_latched; }
    // Legacy straight-grid lookup kept for external callers until they
    // migrate to step_at_phase(), which follows this lane's latched shuffle.
    static int step_index(float phase, int steps) {
        int s = static_cast<int>(phase * static_cast<float>(steps));
        if (s >= steps) s = steps - 1;
        // Guard for callers that do not already constrain phase to [0, 1).
        if (s < 0)      s = 0;
        return s;
    }
    // Samples per STEP slot at the current rate: one slot is 1/_steps of the
    // cycle and the phase advances by _phase_inc * (1 + _ev_rate) per sample.
    // The EVOLVE rate walk is part of the answer, not a detail: lane.cpp
    // advances the phase by exactly that product (both in process() and in
    // tick()'s dp1), and _ev_rate is clamped to +-0.2, so leaving it out made
    // this up to 20% wrong under EVOLVE/GROW -- and the sampler's step clock,
    // pushed here via set_step_clock and sliced on directly by the STEP grid
    // fallback, inherits the error straight from here. 0 when stopped.
    float step_samples() const {
        return _phase_inc > 0.f
            ? 1.f / (_phase_inc * (1.f + _ev_rate) * static_cast<float>(_steps))
            : 0.f;
    }
    float phase_eff() const;                  // audible phase = (_phase + EVOLVE offset), wrapped
    float target() const { return _target; }  // pre-smooth, pre-range held value

    void reset(float phase = 0.f);

    // --- M4 center hooks ---
    void set_shape_offset(float o) { _shape_offset = o; }  // DRIFT bank-wide shape tap
    void kick(float dphase, float dshape);                 // SPOT: phase jump + decaying shape
    void settle();                                         // panic: glide EVOLVE + kick to 0

    // --- STEP follower mode (spec 2026-07-25 mod-lane-step-grid-lock) ---
    //
    // In STEP a texture lane owns no clock. Instead of integrating a phase it
    // is told where the deck is: `deck_step` is the cumulative count of steps
    // the master lane has entered, `frac` where the deck currently sits inside
    // its step. This lane's position is (deck_step + offset) mod _steps plus
    // that fraction.
    //
    // That is not a cheaper way to stay aligned, it is the only exact one.
    // Five float phasors at the same nominal rate round differently depending
    // on how large their phase gets, so lanes with different cycle lengths
    // drift apart -- measured at ~2 samples per 3000-sample step between an
    // 8-slot and a 16-slot lane, a full step of slip in about 90 seconds. One
    // integer count and one shared fraction cannot do that.
    //
    // Returns the post-range output, exactly like tick() does for FLOW.
    //
    // `shuffle` is the amount to build this lane's grid with -- NOT this
    // lane's own _shuffle_latched. In STEP a follower owns no clock: its
    // boundary times come entirely from the deck, so its slot-to-phase
    // mapping must use the same amount the deck's own phase (and `frac`,
    // which was measured against that phase) was built from, or the mapping
    // and `frac` disagree -- the mismatch this signature exists to prevent.
    // The caller passes the PITCH lane's shuffle_latched() (super_modulator.cpp).
    float follow(int32_t deck_step, float frac, float shuffle);
    // SPOT in STEP: shift this lane by `n` whole slots. The offset persists
    // and is exact; the new slot fires at the next follow() call, which is the
    // audible stumble. No rounding or parity care is needed -- boundary times
    // come from the deck, never from this lane's own warp. A draw that rounds
    // to n == 0 is common (SPOT's dphase can round to zero slots on a short
    // lane) and is a shape kick and nothing more, the same as a near-zero
    // dphase reaching kick() in FLOW: dshape still applies, but there is no
    // new slot to fire at the next follow().
    void  nudge_slots(int n, float dshape);

private:
    void  _update_slew();
    void  _update_inc();            // step-clock: inc = rate/sr * (STEP ? 8/steps : 1)
    void  _enter_step(int step, bool latch_now = false);
    void  _on_boundary();
    void  _wrap_events();           // regen/EVOLVE/groove events at a cycle wrap
    float _compute_raw() const;
    int   _sh_slot() const;         // which _seq slot the S&H end reads now
    void  _mutate_slot(int slot);   // GROW: variation dice + pitch walk on a fired step
    void  _fill_walk();             // deterministic contour-walk prefill (non-melodic lanes)
    bool  _effective_gate(int slot) const;  // melodic: groove rank < DENSE depth; else all-true
    int   _groove_k() const;              // DENSE -> how many ranked cell notes play
    void  _renew_units();           // RENEW (melodic/STEP): per-unit dice regeneration
    void  _renew_walk();            // RENEW (non-melodic): dice-gated whole-walk regen
    void  _mutate_groove(bool renew_side);  // VARIATION outer zone: rhythm dice (wrap only)
    void  _start_note(int slot);    // groove: set _note_hold (tie-capped) on fire
    int   _effective_length() const;
    void  _generate_pattern_a();
    void  _derive_pattern_b();
    void  _apply_pending_song_work();
    void  _apply_preroll_work();
    void  _advance_song();
    void  _evolve_outgoing_pattern();
    void  _clear_fresh_phrase_state();
    MelodyPattern& _active_pattern() {
        return _song.patterns[_song.active_pattern & 1u];
    }
    const MelodyPattern& _active_pattern() const {
        return _song.patterns[_song.active_pattern & 1u];
    }

    Rng     _rng;
    OnePole _slew;
    OnePole _slew_tick;          // tick-rate twin of _slew; a lane is driven by
                                 // exactly ONE path, so the twin's state never
                                 // fights the per-sample instance

    float _sr = 48000.f;
    float _phase = 0.f;
    float _phase_inc = 0.f;
    float _rate_hz = 0.f;
    float _shape = 0.f;
    float _range = 1.f;
    float _smooth = 0.f;
    float _variation = 0.f;

    bool  _step_mode = false;
    int   _steps = 8;
    float _shuffle_target = 0.f;
    float _shuffle_latched = 0.f;
    bool  _fixed_slew = false;

    int   _cur_step = -1;
    static constexpr int kSeqSlots = 32;
    SongForm _song;
    bool      _melodic   = false;
    bool      _melodic_at_init = false;
    float     _density   = 1.f;
    float _target = 0.f;     // pre-smooth held value
    bool  _fired = false;
    bool  _wrapped = false;
    bool  _frozen = false;
    int   _note_age  = 0;    // steps since the current note fired
    int   _note_hold = 0;    // composed note length (capped at the next note)

    float _ev_phase = 0.f;   // EVOLVE random-walk offsets: shape / phase / rate (Task 7)
    float _ev_shape = 0.f;
    float _ev_rate  = 0.f;

    // M4 center hooks
    float _shape_offset = 0.f;   // DRIFT shape tap (set per control tick)
    float _kick_shape   = 0.f;   // SPOT shape offset, decays with _kick_coef
    float _kick_coef    = 1.f;   // per-sample decay for _kick_shape (tau ~ 1.5 s)
    int   _settle_ctr   = 0;     // >0: gliding EVOLVE walks + kick to 0
    float _settle_coef  = 1.f;   // per-sample settle glide (tau ~ 0.3 s)
    float _kick_coef_tick   = 1.f;   // _kick_coef ^ kTickInterval
    float _settle_coef_tick = 1.f;   // _settle_coef ^ kTickInterval

    // Follower state. _follow_pos is the last absolute position this lane was
    // advanced to (deck count + offset), _follow_armed false until the first
    // follow() call after init/reset/STEP entry so that entering STEP does not
    // replay history. _follow_jumped makes a nudge audible on the next call
    // even when no deck step has elapsed.
    int32_t _follow_pos    = 0;
    int32_t _follow_offset = 0;
    bool    _follow_armed  = false;
    bool    _follow_jumped = false;
};

} // namespace spky
