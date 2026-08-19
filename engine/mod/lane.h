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

    // SMOOTH's ceiling on the four texture lanes, as a fraction of whichever
    // INTERVAL _update_slew() selected -- the lane cycle in FLOW LFO, one step
    // in STEP. Not always the cycle.
    //
    // LANE_PITCH uses kFlowSlewFrac instead, and one value cannot serve both
    // cases (spec 2.1). Its motivating case is "a note must arrive inside its
    // own slot", but it is the melodic lane's top on EVERY path, including the
    // ones with no notes at all: a Sampler or BBD deck's PITCH lane runs FLOW
    // LFO, where the interval is the whole cycle. Measured there: tau/cycle =
    // 0.2499 at SMOOTH 0.714, i.e. 0.35 * smooth of a full cycle.
    //
    // 0.5 rather than 1.0 by ear, 2026-08-14: at 0.5 the right stop lands
    // within 0.3% of where the OLD law's right stop already sat (p2p 0.322 vs
    // 0.323 at a 0.5 Hz patch), so the change is confined to the middle of the
    // axis -- which is where the complaint was. 1.0 buys 6 dB more ceiling at
    // a stop nobody asked for. Revisable by ear; the law does not depend on it.
    static constexpr float kSmoothTopTexture = 0.5f;

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
    // FLOW melody mode (spec 2026-08-13 flow-melody-engine). Off by default:
    // the boot value is the legacy continuous-LFO behaviour and the new state
    // has to be asked for, so a missing push from Part is a silent revert to
    // the old sound rather than a silent adoption of the new one. Part drives
    // this from the engine id -- SAMPLER and BBD keep the LFO, because on those
    // decks the PITCH lane is not a note.
    void set_flow_melody(bool on);
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
    // The length the phrase is CURRENTLY supposed to have: kFlowPhraseSlots in
    // FLOW melody mode, the (clamped) STEPS count everywhere else. Exposed so a
    // gate can compare it against the length the pattern was actually generated
    // at (pattern_for_test(...).pattern_groove.len) instead of hard-coding one
    // side of that comparison -- the invariant this lane has to keep across a
    // mode change is that the two agree.
    int effective_length_for_test() const { return _effective_length(); }
    // "this lane carries composed notes" -- the private _note_lane() predicate,
    // exposed so a test can pin that a newly added engine still reaches the
    // melodic phrase machinery. See _note_lane()'s NAMING TRAP comment below.
    bool note_lane_for_test() const { return _note_lane(); }
    // The active pattern's generated groove length, the other half of that
    // comparison, without the caller having to route through active_pattern().
    int pattern_groove_len_for_test() const {
        return static_cast<int>(_active_pattern().pattern_groove.len);
    }
    // Real motion, not the commanded rate: every Hz observer above stays
    // correct even while the phase accumulator is frozen solid, so only a
    // count of actual wraps can tell a stalled lane from a healthy one
    // (spec 2026-08-12 modulation-pace, Task 7). Incremented in process()'s
    // wrap loop, lane.cpp.
    uint32_t wrap_count_for_test() const { return _wraps; }
    float last_out_for_test() const { return _last_out; }
    // kFlowSlewFrac itself stays private (it's an implementation constant of
    // _update_slew's top selection, not part of the public control surface);
    // this exposes its value so a test can derive an expectation from it
    // instead of pasting the number.
    static constexpr float kFlowSlewFrac_for_test() { return kFlowSlewFrac; }
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
    // STEP accent: 0 = the rank-0 anchor at full strength, 1 = the last note
    // DENSE reveals. Spec 2026-08-15-step-accent-design.md section 3.
    //
    // The guard is deliberate redundancy, not the load-bearing mechanism:
    // set_step()'s mode-changed branch already zeroes _note_accent
    // (lane.cpp:211) on every transition, before this guard is ever
    // consulted, and _start_note (STEP-only) is the value's only writer. No
    // gate can tell a guarded accessor from a guardless one here, which is
    // why none exists -- the guard is insurance against a future second
    // writer of _note_accent.
    float note_accent() const { return _step_mode ? _note_accent : 0.f; }
    float phase()  const { return static_cast<float>(_phase); }
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
        return _phase_inc > 0.0
            ? static_cast<float>(1.0 / (_phase_inc * (1.0 + double(_ev_rate))
                                        * double(_steps)))
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
    // Five phasors at the same nominal rate still round differently
    // depending on how large their phase gets, so lanes with different cycle
    // lengths drift apart -- a measurement once quoted here as ~2 samples
    // per 3000-sample step (a full step of slip in about 90 seconds) between
    // an 8-slot and a 16-slot lane, taken while `_phase` was `float`. It
    // predates `_phase`/`_phase_inc` moving to `double` (`e449bf4`) and has
    // not been re-measured since, so that number no longer applies and is
    // dropped rather than restated. One integer count and one shared
    // fraction cannot drift at all, which is the reason this mode exists
    // regardless of the phasors' width.
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
    // "this lane is running the FLOW melody engine right now"
    bool _flow_melody_on() const { return _melodic && !_step_mode && _flow_melody; }
    // "this lane runs the melody system at all" (STEP or FLOW melody). NOT
    // named melody(): it would sit next to set_melodic/_melodic and read as a
    // getter for the flag, which is the naming collision spotykach-gotchas
    // records for set_depth.
    bool _melody_engine_on() const { return _melodic && (_step_mode || _flow_melody); }
    // "this lane carries composed notes", i.e. the deck runs a note engine.
    // NAMING TRAP: _flow_melody reads as "FLOW melody is on" and is not that.
    // Part pushes it from the engine id at parts/part.cpp:43 and :441 --
    // `_engine_id != ENGINE_SAMPLER && != ENGINE_BBD` -- in BOTH modes, so it
    // is an engine-class flag. Gating on it rather than on _melody_engine_on()
    // is what keeps this out of a BBD deck's PITCH lane, which is the delay
    // clock and not a note, and the owner ruled for that trade 2026-08-07.
    // NOT the kBbdFlowRangeMax cap, which an earlier version of this comment
    // claimed: that cap lived in the terrain layer, applied in FLOW only, and
    // so never ran in STEP -- the one mode where the two candidate guards
    // differ. Measured over 400 masters while that layer still existed: all
    // 35 BBD decks drawn in STEP carried RANGE above the cap, up to 0.7266
    // against a cap of 0.0083. The layer was deleted 2026-08-14; the cap's
    // by-ear rationale survives in docs/attic/taste-by-ear-notes.md, "The BBD
    // flow bend budget".
    bool _note_lane() const { return _melodic && _flow_melody; }
    void _prime_floors() { _since_fire = _note_min_samples;
                           _since_phrase = _phrase_min_samples; }
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

#ifdef SPKY_TESTING
    // The value this lane last EMITTED, whichever smoother produced it.
    // Reading _slew.value() instead would be wrong for every lane driven by
    // tick() -- which is the four texture lanes in FLOW -- because tick()
    // writes _slew_tick and leaves _slew at its init value. Written only
    // under SPKY_TESTING: the per-sample path sits inside a block budget
    // currently near 96%, and a test accessor does not get to spend it.
    float _last_out = 0.f;
#endif

    float _sr = 48000.f;
    // The phase accumulator is double, and so is its increment (spec
    // 2026-08-12 modulation-pace). LANE_PITCH adds _phase_inc once per sample:
    // in float, an increment below half an ulp of the current binade rounds
    // away entirely and the lane FREEZES -- measured stalls at phase 0.50 for
    // 0.00125 Hz and 0.25 for 0.000625 Hz, with a band just above where every
    // add rounds up to a full ulp and the lane runs fast (0.0025 Hz was 7%
    // fast). kRateFreeMin is 0.02 Hz, so float left only ~14x of margin and
    // PACE x1/32 spends all of it. tick()'s process_window_end shadow is part
    // of this accumulator and moves with it -- see lane.cpp.
    double _phase = 0.0;
    double _phase_inc = 0.0;
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
    // The free mode owns its phrase length. It cannot come from STEPS: Fireflow
    // spends STEPS == 0 on the mode switch (Fireflow.cpp:892-893) and set_step
    // clamps that to 1, while a caller that sets mode and count independently
    // -- the render host's set_step action, and engine/param_table.h's own
    // P_STEPS range -- pushes 2..16 in both modes, so STEPS would mean two
    // different things. It also cannot be made variable: generate_phrase
    // fills only [0, n) (phrase_gen.h:165-200) and pitch[32] is zero-init, so a
    // length that grows past the generated one plays the root instead of a note.
    static constexpr int kFlowPhraseSlots = 8;
    // Note-rate floor. A boundary arriving sooner than this after the last fire
    // HOLDS instead of firing, so the rate decimates to the floor rather than
    // falling off a cliff, and DENSITY keeps its full effect everywhere below
    // the ceiling. 60 ms is ~16 notes/s -- above anything ambient, below
    // anything that reads as a buzz. SET BY ARITHMETIC, then CONFIRMED BY EAR
    // (owner, 2026-08-13, against flow_melody.wav) -- the arithmetic above is
    // where the value came from, not an open question.
    static constexpr float kFlowNoteMinS = 0.060f;
    // Melody-mode slew ceiling, as a fraction of the slot interval: a note
    // reaches 1 - e^(-1/0.35) ~= 94 % of its target inside its own slot. This
    // is the MINIMUM needed for a melody to be heard as notes rather than a
    // wobble; everything else about SMOOTH belongs to the SHAPE/SMOOTH rework.
    // SET BY ARITHMETIC, then CONFIRMED BY EAR (owner, 2026-08-13).
    static constexpr float kFlowSlewFrac = 0.35f;
    SongForm _song;
    bool      _melodic   = false;
    bool      _flow_melody = false;
    bool      _melodic_at_init = false;
    float     _density   = 1.f;
    float _target = 0.f;     // pre-smooth held value
    bool  _fired = false;
    bool  _wrapped = false;
    bool  _frozen = false;
#ifdef SPKY_TESTING
    uint32_t _wraps = 0;   // real wrap count; see wrap_count_for_test() above
#endif
    int   _note_age  = 0;    // steps since the current note fired
    int   _note_hold = 0;    // composed note length (capped at the next note)
    float _note_accent = 0.f;   // groove rank of the running note, normalized

    // Samples since the last fire / the last phrase event, for the two floors.
    // Primed rather than zeroed at init/reset/mode entry -- at 0 the floor
    // would swallow the first note of every phrase start, including RST's.
    int _since_fire   = 0;
    int _since_phrase = 0;
    int _note_min_samples   = 0;   // kFlowNoteMinS * _sr, cached at init
    int _phrase_min_samples = 0;   // kFlowPhraseSlots * _note_min_samples

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
