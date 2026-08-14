#include "mod/lane.h"
#include "mod/waveforms.h"
#include "mod/range.h"
#include "util/math.h"
#include <cmath>

using namespace spky;

// Mutation character — tuned by ear; the spec fixes behavior, not constants.
static constexpr float kGravity  = 0.10f;  // GROW: mild pull toward 0 (the root)
static constexpr float kEndpointSampleEpsilon = 0.001f;

struct ProcessWindowEnd {
    double phase;
    int wraps;
};

// Rare endpoint fallback for tick(): replay only process()'s raw phase
// additions for the whole control window. Per-wrap rates come from the real
// tick traversal, so this shadow performs no events and consumes no RNG.
//
// This is a MODEL of process(), so its arithmetic must match process()'s
// exactly -- it moved from float to double together with _phase on 2026-08-12
// (PACE). If the two ever diverge again, tick()'s endpoint decision is made
// against a path the lane does not run, and the line "_phase =
// shadow_end.phase" below silently re-quantizes the accumulator.
static ProcessWindowEnd process_window_end(
    double phase, const double* phase_per_sample_by_wrap, int rate_count) {
    int wraps = 0;
    for (int sample = 0; sample < ModLane::kTickInterval; ++sample) {
        const int rate_index = wraps < rate_count ? wraps : rate_count - 1;
        phase += phase_per_sample_by_wrap[rate_index];
        while (phase >= 1.0) {
            phase -= 1.0;
            ++wraps;
        }
    }
    return {phase, wraps};
}

// Positive modulo: _follow_offset can be negative after a backwards SPOT
// nudge, and C++'s % keeps the sign of the dividend.
static int slot_of(int32_t pos, int slots) {
    int m = static_cast<int>(pos % slots);
    if (m < 0) m += slots;
    return m;
}

void ModLane::init(float sample_rate, uint32_t seed) {
    _sr = sample_rate;
    _note_min_samples   = static_cast<int>(kFlowNoteMinS * _sr);
    _phrase_min_samples = kFlowPhraseSlots * _note_min_samples;
    _prime_floors();                        // primed: the first boundary fires
    _rng.seed(seed);
    _phase = 0.0;
#ifdef SPKY_TESTING
    _wraps = 0;   // re-init must zero the counter along with _phase, or a
                  // turns computation spanning a re-init reads a stale count
#endif
    _cur_step = -1;
    _shuffle_target = 0.f;
    _shuffle_latched = 0.f;
    if (_song.form_pending)
        _song.selected_form = _song.pending_form;
    if (_song.song_pending)
        _song.selected_song = _song.pending_song;
    _song.active_pattern = 0;
    _song.phrase_index = 0;
    MelodyPattern& pattern = _active_pattern();
    if (_melodic) {
        _generate_pattern_a();
        _derive_pattern_b();
        _song.active_pattern =
            song_symbol_at(_song.selected_song, _song.phrase_index);
    } else {
        _fill_walk();
        for (int i = 0; i < kSeqSlots; ++i) {
            pattern.gate[i] = true;
            pattern.motif_id[i] = 0;
        }
    }
    _melodic_at_init = _melodic;
    _song.form_pending = false;
    _song.song_pending = false;
    _song.new_pending = false;
    _song.length_pending = false;
    _density = 1.f;
    _target = 0.f;
    _fired = false;
    _wrapped = false;
    _frozen = false;
    _note_age = 0;
    _note_hold = 0;
    _ev_phase = 0.f;
    _ev_shape = 0.f;
    _ev_rate  = 0.f;
    _follow_pos    = 0;
    _follow_offset = 0;
    _follow_armed  = false;
    _follow_jumped = false;
    _shape_offset = 0.f;
    _kick_shape   = 0.f;
    _kick_coef    = std::exp(-1.f / (1.5f * _sr));   // SPOT shape decay tau = 1.5 s
    _settle_coef  = std::exp(-1.f / (0.3f * _sr));   // SETTLE glide tau = 0.3 s
    _kick_coef_tick   = std::pow(_kick_coef,   static_cast<float>(kTickInterval));
    _settle_coef_tick = std::pow(_settle_coef, static_cast<float>(kTickInterval));
    _settle_ctr   = 0;
    _update_slew();
    _slew.reset(0.f);
    _slew_tick.reset(0.f);
}

float ModLane::phase_eff() const {
    const double p = _phase + double(_ev_phase);
    return static_cast<float>(p - std::floor(p));
}

void ModLane::set_rate_hz(float hz)   { _rate_hz = hz > 0.f ? hz : 0.f; _update_inc(); }
void ModLane::set_shape(float s)      { _shape = clampf(s, 0.f, 1.f); }
void ModLane::set_range(float r)      { _range = clampf(r, 0.f, 1.f); }
void ModLane::set_variation(float v)  { _variation = clampf(v, -1.f, 1.f); }

void ModLane::set_form(Principle form) {
    int value = static_cast<int>(form);
    if (value < 0) value = 0;
    const int last = static_cast<int>(Principle::kCount) - 1;
    if (value > last) value = last;
    _song.pending_form = static_cast<Principle>(value);
    _song.form_pending = _song.pending_form != _song.selected_form;
}

void ModLane::set_song(SongMode song) {
    _song.pending_song = clamp_song(static_cast<int>(song));
    _song.song_pending = _song.pending_song != _song.selected_song;
}

void ModLane::set_flow_melody(bool on) {
    const bool mode_changed = on != _flow_melody;
    _flow_melody = on;
    // Only on a real flip, mirroring set_step's own `if (mode_changed)` line.
    // Part pushes this flag at BOTH sites that write _engine_id (part.cpp:43,
    // :441), so a same-class swap (SYNTH -> WAVE) calls this with the value it
    // already holds; re-priming the floors there would let a note fire sooner
    // than kFlowNoteMinS after the previous one, and clearing _cur_step there
    // would re-fire the slot the lane is already in.
    if (mode_changed) {
        // See set_step: a slot index and a freeze decision from the other
        // state mean nothing in this one. _cur_step = -1 makes the next sample
        // re-evaluate the boundary instead of holding whatever the FLOW LFO
        // path last wrote into _target -- the LFO writes _target every sample
        // but never moves _cur_step, so coming back to a still-matching index
        // fires nothing until the phase leaves the slot.
        //
        // Not while _step_mode is true: there the index is STEP's own live
        // position, and clearing it would re-fire the current step mid-step.
        // set_step clears it on its way out of STEP anyway.
        if (!_step_mode) _cur_step = -1;
        _frozen = false;
        _prime_floors();
    }
    // Absolute, not one-sided, and not conditional on the flip. ENTERING melody
    // mode the pattern may have been generated at another length -- at boot, or
    // whenever a host pushed a STEPS other than kFlowPhraseSlots before this
    // flag -- and pitch[] past that length is zero, so it MUST regenerate.
    // LEAVING it owes exactly the same flag: this call changes what
    // _effective_length() returns, and set_step's own length check is a DELTA
    // (old_len captured before its assignments) whose premise is that old_len
    // is the length the pattern was last generated at. An unflagged exit
    // destroys that premise, and STEP entered afterwards at an unchanged STEPS
    // then keeps the 8-slot melody groove -- see tests/test_flow_melody.cpp,
    // "leaving melody mode re-lengths the phrase for STEP". length_pending is
    // sticky and only consumed under _melody_engine_on(), so a lane that
    // leaves for the FLOW LFO path simply carries it until it enters a melody
    // mode again, which is the correct moment. Testing the pattern rather than
    // the flip gives every one of those cases from one condition.
    //
    // An ASSIGNMENT, not a one-sided raise -- and that is not tidiness, it is
    // the round trip. SYNTH -> SAMPLER -> SYNTH raises the flag on the way out
    // (8 != STEPS) and correctly declines to raise it on the way back (8 == 8),
    // but nothing on the LFO path consumes it, and the return leg's
    // `_cur_step = -1` above is precisely the condition _apply_preroll_work
    // waits for -- so a raise-only check rebuilds both patterns and restarts
    // the SONG ladder on the next sample, for a gesture that changed nothing.
    // The assignment clears it at the moment the two lengths agree again.
    //
    // Clearing here can never lose work another writer owes. The only other
    // raise is set_step's length delta, and the condition below is the ground
    // truth that delta approximates: pattern_groove.len IS the length the
    // phrase was last generated at (expand_pattern_groove writes it, and
    // derive_turnaround copies pattern A, so both patterns always carry the
    // same one), so when it equals _effective_length() there is no length work
    // left to do, whoever asked for it. The other pending flags are separate
    // fields and are untouched. The `_melodic` guard stays a guard rather than
    // folding into the assignment: a non-melodic lane can never raise this flag
    // (both raise sites test _melodic) and never consumes it either, so this
    // must not start writing one.
    if (_melodic)
        _song.length_pending =
            _active_pattern().pattern_groove.len != _effective_length();
    _update_slew();
}

void ModLane::set_step(bool on, int steps) {
    const bool entering_step = on && !_step_mode;
    const bool mode_changed  = on != _step_mode;
    if (entering_step) _shuffle_latched = _shuffle_target;
    if (entering_step) { _note_age = 0; _note_hold = 0; }  // STEP entry: no stale sustain
    // Either direction: a slot index and a freeze decision from the other mode
    // mean nothing in this one. _cur_step = -1 makes the first sample fire slot
    // 0, the same way init and reset do.
    if (mode_changed) { _cur_step = -1; _frozen = false; _prime_floors(); }
    // Entering STEP disarms the follower so its first follow() call lands on
    // the deck's current position instead of replaying the whole count.
    if (entering_step) { _follow_armed = false; _follow_jumped = false; }
    // Captured BEFORE the assignments below: _effective_length() reads
    // _step_mode and _steps, both of which this function is about to change.
    const int old_len = _melodic ? _effective_length() : 0;
    int new_steps = steps < 1 ? 1 : steps;
    if (on && _step_mode && new_steps != _steps && _cur_step >= 0) {
        // Seamless live STEPS turn (spec: step-clock): keep the step index and
        // the fraction inside it so the boundary grid never jumps; _cur_step
        // follows along so the next sample sees no ghost boundary. The
        // _cur_step >= 0 guard keeps pre-run configuration (init -> set_step
        // before the first process()) on the old path, where the first sample
        // must still fire step 0.
        // _cur_step is derived from the stored _phase (assigned first) using
        // the same warped lookup as process(), so a same-tick SPOT kick() +
        // STEPS turn is absorbed into this rescale (accepted trade).
        int old_step = shuffle_step_index(
            static_cast<float>(_phase), _steps, _shuffle_latched);
        float old_frac = shuffle_step_fraction(
            static_cast<float>(_phase), old_step, _steps, _shuffle_latched);
        float pos = std::fmod(
            static_cast<float>(old_step) + old_frac,
            static_cast<float>(new_steps));
        _phase = shuffle_phase_for_position(pos, new_steps, _shuffle_latched);
        _cur_step = shuffle_step_index(
            static_cast<float>(_phase), new_steps, _shuffle_latched);
    }
    _step_mode = on;
    _steps = new_steps;
    // Only when the EFFECTIVE length changes. Keep the _melodic guard: the
    // helper clamps to kSeqSlots, and lane_slots() can hand a texture lane up
    // to 64 (tests/test_lane_len.cpp:26), so an unguarded check would start
    // clamping lengths that today pass through.
    if (_melodic && _effective_length() != old_len)
        _song.length_pending = true;
    if (entering_step && _melodic_at_init &&
        _song.patterns[1].pattern_groove.len == 0)
        _song.new_pending = true;
    _update_inc();
}

// Spec 2026-07-17 step-clock: STEP runs RATE as a step clock with an 8-step
// reference; FLOW keeps RATE as the cycle rate. At 8 steps the factor is
// exactly 1.0f, so the panel default stays bit-identical to the old
// pattern-clock behavior.
void ModLane::_update_inc() {
    _phase_inc = (double(_rate_hz) / double(_sr)) * double(clock_scale());
    _update_slew();     // the melody clamp is a function of the slot interval
}

void ModLane::new_phrase() {
    if (_melodic) _song.new_pending = true;
}

int ModLane::_effective_length() const {
    if (_flow_melody_on()) return kFlowPhraseSlots;
    int n = _steps < 1 ? 1 : _steps;
    return n > kSeqSlots ? kSeqSlots : n;
}

void ModLane::_generate_pattern_a() {
    MelodyPattern& pattern = _song.patterns[0];
    const int len = _effective_length();
    generate_phrase(_song.selected_form, _rng, len,
                    pattern.pitch, pattern.gate, pattern.motif_id,
                    pattern.layout);
    pg_gen_groove(_rng, pattern.layout.motif_len, pattern.cell_groove);
    expand_pattern_groove(
        pattern.cell_groove, len, pattern.pattern_groove);
}

void ModLane::_derive_pattern_b() {
    derive_turnaround(_song.patterns[0], _effective_length(), _rng,
                      _song.patterns[1],
                      _song.cadence_slot, _song.bound_a_opening);
}

void ModLane::_clear_fresh_phrase_state() {
    _note_age = 0;
    _note_hold = 0;
    _ev_phase = 0.f;
    _ev_shape = 0.f;
    _ev_rate = 0.f;
}

void ModLane::_apply_pending_song_work() {
    const bool rebuild =
        _song.form_pending || _song.new_pending ||
        _song.length_pending ||
        _song.patterns[1].pattern_groove.len == 0;

    if (_song.form_pending)
        _song.selected_form = _song.pending_form;
    if (_song.song_pending)
        _song.selected_song = _song.pending_song;

    if (rebuild) {
        _generate_pattern_a();
        _derive_pattern_b();
        _clear_fresh_phrase_state();
    }

    _song.phrase_index = 0;
    _song.active_pattern =
        song_symbol_at(_song.selected_song, _song.phrase_index);
    if (_song.active_pattern == 1u)
        bind_song_cadence(
            _song.patterns[0], _song.patterns[1],
            _song.cadence_slot, _song.bound_a_opening);

    _song.form_pending = false;
    _song.song_pending = false;
    _song.new_pending = false;
    _song.length_pending = false;
}

void ModLane::_apply_preroll_work() {
    if (_melody_engine_on() && _cur_step < 0 &&
        (_song.form_pending || _song.song_pending ||
         _song.new_pending || _song.length_pending))
        _apply_pending_song_work();
}

void ModLane::_advance_song() {
    ++_song.phrase_index;
    const uint8_t incoming =
        song_symbol_at(_song.selected_song, _song.phrase_index);
    if (incoming == 1u) {
        bind_song_cadence(
            _song.patterns[0], _song.patterns[1],
            _song.cadence_slot, _song.bound_a_opening);
    }
    _song.active_pattern = incoming;
}

void ModLane::set_smooth(float s) {
    _smooth = clampf(s, 0.f, 1.f);
    _update_slew();
}

void ModLane::set_fixed_slew(bool on) {
    _fixed_slew = on;
    _update_slew();
}

void ModLane::_update_slew() {
    // smooth 0 -> ~1 sample (near passthrough), smooth 1 -> ~0.5 s.
    float t = _fixed_slew ? 0.02f : (0.00002f * std::pow(25000.f, _smooth));
    if (_flow_melody_on()) {
        // Clamp against the interval the notes ACTUALLY have, not the raw slot:
        // where the note floor decimates, the raw slot is far shorter than the
        // notes are, and clamping to it would make the glide much tighter than
        // anything needs. Guard _phase_inc == 0 the way step_samples() does --
        // in double it yields inf and the clamp goes inert, which is benign but
        // silent, and a silent inert guard is the shape this project fixes.
        // _ev_rate is deliberately not a recompute trigger here: re-deriving
        // the slew at every wrap for a +-20% term inside a 0.35 safety factor
        // buys nothing, and the value is refreshed at the next rate change
        // anyway.
        const double denom = _phase_inc * (1.0 + double(_ev_rate))
                           * double(_effective_length());
        if (denom > 0.0) {
            const double slot_samples = 1.0 / denom;
            const double effective = slot_samples > double(_note_min_samples)
                                   ? slot_samples : double(_note_min_samples);
            // OnePole::init takes SECONDS (onepole.h:14, k = 1/(time_s*sr)),
            // so the sample count has to be divided by _sr. Without this the
            // clamp never binds.
            const float cap =
                static_cast<float>(double(kFlowSlewFrac) * effective / double(_sr));
            if (t > cap) t = cap;
        }
    }
    _slew.init(_sr, t);
    // Tick twin: the exact kTickInterval-sample compound of the per-sample
    // coefficient, so held segments converge identically at tick sampling.
    // The `k` derivation + clamp below intentionally mirrors OnePole::init's
    // own coefficient formula (engine/util/onepole.h) so that compounding it
    // kTickInterval times reproduces the per-sample coefficient's effect
    // exactly. If that formula changes, this must change with it -- these
    // two are a matched pair, not independent code, and this tick twin would
    // otherwise silently diverge from process()'s slew.
    float k = 1.f / (t * _sr);
    if (k > 1.f) k = 1.f;
    _slew_tick.set_coef(1.f - std::pow(1.f - k, static_cast<float>(kTickInterval)));
}

void ModLane::kick(float dphase, float dshape) {
    _phase += double(dphase);
    _phase -= std::floor(_phase);          // permanent wrap into [0,1)
    _kick_shape += dshape;                 // decays back to 0 over ~1.5 s
}

void ModLane::nudge_slots(int n, float dshape) {
    // Move the offset AND the remembered position by the same amount: the
    // jump must not look like elapsed time, or the next follow() would replay
    // n slots (or, for a negative n, stall until the deck caught back up).
    _follow_offset += n;
    _follow_pos    += n;
    // A draw that rounds to n == 0 lands on the same slot it already occupies
    // -- there is no new slot to make audible, so no forced re-entry at the
    // next follow(). Same call FLOW makes for a near-zero dphase in kick():
    // the shape kick still applies unconditionally (it is real at any draw),
    // but nothing else moves.
    if (n != 0) _follow_jumped = true;
    _kick_shape += dshape;
}

float ModLane::follow(int32_t deck_step, float frac, float shuffle) {
    _fired   = false;
    _wrapped = false;
    _apply_preroll_work();
    _kick_shape *= _kick_coef_tick;
    if (_settle_ctr > 0) {
        _settle_ctr = _settle_ctr > kTickInterval ? _settle_ctr - kTickInterval : 0;
        _ev_phase   *= _settle_coef_tick;
        _ev_shape   *= _settle_coef_tick;
        _ev_rate    *= _settle_coef_tick;
        _kick_shape *= _settle_coef_tick;
    }

    const int     slots = _steps < 1 ? 1 : _steps;
    const int32_t pos   = deck_step + _follow_offset;
    const int     here  = slot_of(pos, slots);

    bool    land_only = false;
    int32_t elapsed   = 0;
    if (!_follow_armed) {
        // First call after init/reset/STEP entry. Land on the current position
        // without replaying every step the deck has taken since it started --
        // and, just as important, WITHOUT running a wrap even when the landing
        // slot is 0. Wrap events evolve the pattern that just ENDED
        // (_evolve_outgoing_pattern); at a cold start none did. STEP entry
        // restarts the deck count at 0, so treating that as a wrap would walk
        // EVOLVE once on all four texture lanes every time the switch is
        // thrown. tick() makes the same choice at its own cold start: it
        // enters the step its phase points at and runs no wrap events.
        _follow_armed = true;
        land_only     = true;
    } else {
        elapsed = pos - _follow_pos;
        if (elapsed < 0) elapsed = 0;          // only nudge_slots moves backwards
        // A jump this large means the caller skipped a long stretch (a stopped
        // transport, a mode switch). Replaying it would burn RNG draws for
        // cycles nobody heard; land on the current slot instead. Same spirit
        // as tick()'s edge-walk guard.
        if (elapsed > 2 * kSeqSlots) land_only = true;
    }

    bool entered = false;
    if (land_only) {
        _phase = shuffle_phase_for_position(
            static_cast<float>(here), slots, shuffle);
        _enter_step(here);
        entered = true;
    } else {
        const int32_t prev = pos - elapsed;
        for (int32_t k = 1; k <= elapsed; ++k) {
            const int slot = slot_of(prev + k, slots);
            // Boundary targets are evaluated at the exact grid phase, the same
            // sampling tick() documents for its edge walk.
            _phase = shuffle_phase_for_position(
                static_cast<float>(slot), slots, shuffle);
            if (slot == 0) {
                _wrapped = true;
                _wrap_events();      // before the new cycle's step 0, as in tick()
            }
            _enter_step(slot);
            entered = true;
        }
    }
    _follow_pos = pos;

    if (_follow_jumped) {
        _follow_jumped = false;
        if (!entered) {
            _phase = shuffle_phase_for_position(
                static_cast<float>(here), slots, shuffle);
            _enter_step(here);
        }
    }

    // Park at the live position so phase(), phase_eff() and any external
    // reader see where the lane actually is inside its slot.
    _phase = shuffle_phase_for_position(
        static_cast<float>(here) + frac, slots, shuffle);

    float smoothed = _slew_tick.process(_target);
    return apply_range(smoothed, _range);
}

void ModLane::settle() {
    _settle_ctr = static_cast<int>(_sr * 1.0f);   // glide EVOLVE + kick over ~1 s
}

void ModLane::reset(float phase) {
    _phase = clampf(phase, 0.f, 0.999999f);
    _shuffle_latched = _shuffle_target;
    _cur_step = -1;
    // Until 2026-08-13 (Task 11, FLOW melody engine plan) this line was
    // observable through tick() only: process() re-evaluates the boundary on
    // its very next call regardless (_cur_step == -1 forces slot 0, always
    // open, so _on_boundary recomputes _frozen to false on its own), but
    // tick()'s FLOW-melody path had no such immediate re-check, so a freeze
    // could survive RST into the next tick() call with nothing to clear it.
    // Task 11 gave tick() the same immediate pending-mismatch re-check
    // process() already had (both paths now force-evaluate slot 0 on their
    // very next call after reset()), so this line is superseded by BOTH
    // paths' next call the same way and no longer has a downstream-behavior
    // consequence through either one. It is still observable directly,
    // though: nothing else in reset() touches _frozen, so querying
    // frozen() between reset() and the next process()/tick() call reads
    // this line and only this line. See tests/test_flow_melody.cpp, "RST
    // clears a stale freeze immediately, before the next process()/tick()
    // call" (re-homed there by Task 11 when the original tick()-path case
    // stopped being able to fail -- see that task's report for the
    // measurement).
    _frozen = false;
    // RST is the resync gesture: it clears the SPOT offset too, so the lane
    // comes back to the deck's own slot 0 rather than to a stumbled one.
    _follow_pos    = 0;
    _follow_offset = 0;
    _follow_armed  = false;
    _follow_jumped = false;
    _note_age = 0;
    _note_hold = 0;
    _prime_floors();
    _slew.reset(_target);
    _slew_tick.reset(_target);
}

float ModLane::_compute_raw() const {
    // A note lane emits its phrase directly, in STEP as in FLOW. Routing it
    // through shape_value instead would weight the phrase only in the bank's
    // fourth arm (waveforms.h:32, shape >= 0.75): below that the composed pitch
    // is computed and discarded, and every FORM emitted the same sine staircase
    // -- measured at SHAPE 0, p2p 2.000 over 5 distinct values on seeds
    // 999/12345/7/4242/31337 alike, and 0 of 4 Principles differing from
    // TwoMotif. The whole melody system therefore lived in the top quarter of
    // the PLAYED SHAPE and nowhere else (docs/engine-map.md §7), which is what
    // made it effectively unreachable where the instrument plays -- a patch
    // whose SHAPE knob sat below 0.35 could never push the played value
    // (knob plus in-lane offsets, up to ±0.40) up to the 0.75 crossfade point,
    // so it never heard it at all. SHAPE is inert on this lane in both modes,
    // consistently. What SHAPE should mean for a melody is the SHAPE/SMOOTH
    // rework's question.
    if (_note_lane()) return _active_pattern().pitch[_sh_slot()];
    const double phd = _phase + double(_ev_phase);
    float ph = static_cast<float>(phd - std::floor(phd));
    float sh = clampf(_shape + _ev_shape + _shape_offset + _kick_shape, 0.f, 1.f);
    return shape_value(ph, sh, _active_pattern().pitch[_sh_slot()]);
}

int ModLane::_sh_slot() const {
    // FLOW LFO: one slot, loop-stable per cycle. STEP and FLOW melody both walk
    // the phrase. Getting this wrong is silent: leave it returning 0 in melody
    // mode and the lane emits pitch[0] forever, and because
    // expand_pattern_groove pins rank_of_slot[0] to 0 the gate is then always
    // open too, so DENSITY moves nothing and the whole mechanism is a no-op.
    if (!_step_mode && !_flow_melody_on()) return 0;
    int s = _cur_step < 0 ? 0 : _cur_step;
    return s % kSeqSlots;
}

int ModLane::_groove_k() const {
    const MelodyPattern& pattern = _active_pattern();
    // Melodic lanes rank over the phrase-length expansion in BOTH modes. The
    // cell_groove arm this used to have was unreachable: _groove_k has exactly
    // one caller, _effective_gate, which returns before this for a non-melodic
    // lane. It is also unobservable in the free mode -- pg_target_len is 8 for
    // every principle (phrase_gen.h:53), so at kFlowPhraseSlots the sizing is
    // k = 1, L = 8, r = 0 and cell_groove.len equals pattern_groove.len by
    // construction. No fixture can tell the two apart, which is why this
    // deletion carries no RED proof of its own.
    int L = _melodic ? static_cast<int>(pattern.pattern_groove.len)
                     : static_cast<int>(pattern.cell_groove.len);
    if (L < 1) L = 1;
    int k = static_cast<int>(std::lround(_density * static_cast<float>(L)));
    if (k < 1) k = 1;              // the anchor is unmaskable
    if (k > L) k = L;
    return k;
}

bool ModLane::_effective_gate(int slot) const {
    const MelodyPattern& pattern = _active_pattern();
    if (!_melodic)
        return pattern.gate[slot];   // non-melodic lanes: all-true, DENSE unrouted
    const int groove_length =
        pattern.pattern_groove.len < 1 ? 1 : pattern.pattern_groove.len;
    return pattern.pattern_groove.rank_of_slot[slot % groove_length] <
           _groove_k();
}

void ModLane::_on_boundary() {
    int slot = _sh_slot();
    // Both melody modes consult the groove rank against DENSE. The FLOW LFO
    // has no per-step gate and always fires (no freeze source after
    // PROBABILITY).
    bool gated = (_step_mode || _flow_melody_on()) ? _effective_gate(slot) : true;
    if (gated && _flow_melody_on() && _since_fire < _note_min_samples)
        gated = false;                       // note-rate floor
    _frozen = !gated;
    if (gated) {
        _fired = true;
        _since_fire = 0;
        if (_melodic && _step_mode) _start_note(slot);   // rhythm: STEP only
        if (_variation > 0.f && (!_melodic || _step_mode || _flow_melody_on()))
            _mutate_slot(slot);  // GROW pitch
        _target = _compute_raw();
    } else if (_step_mode) {
        ++_note_age;   // rest step: the running note ages toward its release
                       // -- a STEP concept. Unreachable in FLOW before this
                       // change (gated was unconditionally true there), so the
                       // guard is bit-identical for every existing path.
    }
    // if !gated: hold the previous _target (frozen) -- and the buffer slot with it
}

void ModLane::_enter_step(int step, bool latch_now) {
    if (latch_now || (step % 2 == 0)) _shuffle_latched = _shuffle_target;
    _cur_step = step;
    _on_boundary();
}

void ModLane::_start_note(int slot) {
    // For _steps > kSeqSlots this scan models the 32-slot buffer's own order
    // (slot after step _steps-1 wraps to slot 0), not the outer cycle seam —
    // unreachable from the panel, where STEPS clamps to 16.
    int n = _effective_length();                        // effective phrase length
    if (n < 1) n = 1;
    int dist = 1;                                       // steps to the next note
    while (dist < n && !_effective_gate((slot + dist) % n)) ++dist;
    const MelodyPattern& pattern = _active_pattern();
    // _start_note is called only from _on_boundary under _melodic && _step_mode,
    // so the cell_groove arm this used to carry was unreachable.
    const int groove_length =
        pattern.pattern_groove.len < 1 ? 1 : pattern.pattern_groove.len;
    int hold = static_cast<int>(
        pattern.pattern_groove.note_len[slot % groove_length]);
    _note_hold = hold > dist ? dist : hold;             // reaching the next note = tie
    _note_age = 0;
}

void ModLane::_mutate_slot(int slot) {
    // GROW only: dice ∝ variation^2 (squared for fine control near LOOP).
    if (_rng.next_unipolar() >= _variation * _variation) return; // dice ∝ variation²
    MelodyPattern& pattern = _active_pattern();
    float v = pattern.pitch[slot];
    // Random walk from the old value. The cubed draw makes small intervals
    // common and leaps rare; width opens with variation; the (1 - kGravity)
    // factor is the tonic gravity keeping lines anchored.
    float r = _rng.next_bipolar();
    float delta = r * r * r * lerpf(0.5f, 2.f, _variation); // cubed: small common
    v = clampf((v + delta) * (1.f - kGravity), -1.f, 1.f);  // mild tonic gravity
    pattern.pitch[slot] = v;
}

void ModLane::_fill_walk() {
    pg_contour_walk(_rng, _active_pattern().pitch,
                    kSeqSlots, 0.f, 0.6f, 0.12f);
}

void ModLane::_renew_units() {
    MelodyPattern& pattern = _active_pattern();
    int units = pattern.layout.motif_count;          // number of renewal units
    const Principle basis = _song.selected_form;
    for (int u = 0; u < units; ++u) {
        if (_rng.next_unipolar() < _variation * _variation)  // per-unit dice
            regenerate_unit(basis, _rng, pattern.layout,
                            pattern.motif_id, u,
                            pattern.pitch, pattern.gate);
    }
}
void ModLane::_renew_walk() {
    pg_contour_walk(_rng, _active_pattern().pitch,
                    kSeqSlots, 0.f, 0.6f, 0.12f);
}

void ModLane::_mutate_groove(bool renew_side) {
    if (!_melody_engine_on()) return;
    mutate_pattern_groove(
        _rng, _active_pattern().pattern_groove,
        _variation, renew_side);
}

void ModLane::_evolve_outgoing_pattern() {
    if (_variation > 0.f) {                 // GROW: EVOLVE contour walk (live)
        _ev_phase = clampf(_ev_phase + _rng.next_bipolar() * 0.01f * _variation, -0.5f, 0.5f);
        _ev_shape = clampf(_ev_shape + _rng.next_bipolar() * 0.02f * _variation, -0.25f, 0.25f);
        _ev_rate  = clampf(_ev_rate  + _rng.next_bipolar() * 0.01f * _variation, -0.2f, 0.2f);
        _mutate_groove(false);              // outer zone: rhythm drifts too
    } else if (_variation < 0.f) {          // RENEW: per-unit regen + walk decay
        if (_melody_engine_on()) _renew_units();
        else if (!_melodic) {
            if (_rng.next_unipolar() < _variation * _variation) _renew_walk();
        }
        float decay = 1.f + 0.2f * _variation;  // variation -1 -> x0.8/cycle
        _ev_phase *= decay; _ev_shape *= decay; _ev_rate *= decay;
        _mutate_groove(true);               // outer zone: re-decide pushes
    }                                       // variation 0 (LOOP): walk frozen
}

// Cycle-wrap events, shared by process() and tick(): outgoing evolution,
// then pending structural work or SONG advancement.
void ModLane::_wrap_events() {
    const bool pending =
        _song.form_pending ||
        _song.song_pending ||
        _song.new_pending ||
        _song.length_pending;

    // The FLOW LFO keeps its old contract: no evolution, no song advancement.
    if (_melodic && !_step_mode && !_flow_melody)
        return;
    if (_flow_melody_on()) {
        if (_since_phrase < _phrase_min_samples) return;
        _since_phrase = 0;
    }

    _evolve_outgoing_pattern();
    if (_melody_engine_on()) {
        if (pending) _apply_pending_song_work();
        else         _advance_song();
    }
}

float ModLane::process() {
    _fired = false;
    _wrapped = false;
    _apply_preroll_work();
    if (_since_fire   < _note_min_samples)   ++_since_fire;
    if (_since_phrase < _phrase_min_samples) ++_since_phrase;
    _kick_shape *= _kick_coef;                 // SPOT shape offset fades toward 0
    if (_settle_ctr > 0) {                     // SETTLE: glide EVOLVE walks + kick to 0
        --_settle_ctr;
        _ev_phase   *= _settle_coef;
        _ev_shape   *= _settle_coef;
        _ev_rate    *= _settle_coef;
        _kick_shape *= _settle_coef;
    }
    _phase += _phase_inc * (1.0 + double(_ev_rate));
    bool wrapped = false;
    while (_phase >= 1.0) {
        _phase -= 1.0;
        wrapped = true;
#ifdef SPKY_TESTING
        ++_wraps;
#endif
    }
    _wrapped = wrapped;

    if (wrapped) _wrap_events();

    if (_step_mode) {
        const int step = shuffle_step_index(
            static_cast<float>(_phase), _steps, _shuffle_latched);
        if (step != _cur_step) _enter_step(step);
    } else if (_flow_melody_on()) {
        // One cycle is one phrase pass -- the same relation STEP already has.
        // A STRAIGHT grid, not shuffle_step_index: SHUFFLE is a rhythmic
        // control and stays out of FLOW, and tests/test_instrument.cpp:653
        // pins a FLOW deck bit-exact under a live shared SHUFFLE turn.
        const int slot = step_index(static_cast<float>(_phase),
                                    _effective_length());
        if (slot != _cur_step) { _cur_step = slot; _on_boundary(); }
        // No per-sample recompute: _target holds between boundaries.
    } else {
        if (wrapped) _on_boundary();
        if (!_frozen) _target = _compute_raw();     // continuous in FLOW
    }

    float smoothed = _slew.process(_target);
    return apply_range(smoothed, _range);
}

// Advance exactly kTickInterval samples in one call -- the texture-lane path
// (spec 2026-07-19 mod-plane-control-rate). Mirrors process()'s observable
// sequence: every edge (step boundary or wrap) inside the interval runs in
// phase order with identical RNG draws, note aging and mutations; wrap
// events run at their phase position (before the new cycle's step 0); only
// the last target is visible. Boundary targets are evaluated at the grid
// phase (step/steps, resp. 0 at a wrap) instead of the per-sample path's
// detection overshoot (< 1 sample of phase) -- an equally valid sampling of
// the same waveform, covered by the equivalence suite.
//
// STEP note (spec 2026-07-25 mod-lane-step-grid-lock): SuperModulator calls
// this from exactly one site, and only on the !_step_on branch -- a STEP
// lane's own _step_mode always mirrors _step_on (set_step()), so every
// `if (_step_mode)` below (the pending-step-mismatch entry, the
// shuffle_boundary_phase edge walk, the shadow process_window_end
// arithmetic) is unreachable in production. SuperModulator now drives STEP
// lanes through follow() instead (see there and lane.h). Kept deliberately,
// not deleted: this is ModLane's own standalone contract, exercised directly
// by tests/test_lane_tick.cpp's STEP cases, independent of whatever engine
// wiring happens to call it today.
//
// FLOW-melody note (spec 2026-08-13 flow-melody-engine, Task 11): the
// `if (_flow_melody_on())` arms below walk the melody-mode slot raster the
// same way, but SuperModulator never takes this path for LANE_PITCH either
// -- it drives LANE_PITCH through process() exclusively (super_modulator.cpp)
// -- so, same as the STEP arms above, nothing in production exercises this.
// It is kept in sync anyway because tick()'s documented contract is that it
// mirrors process()'s observable sequence, and tests/test_lane_tick.cpp
// exercises it directly.
float ModLane::tick() {
    _fired = false;
    _wrapped = false;
    _apply_preroll_work();
    _kick_shape *= _kick_coef_tick;
    if (_settle_ctr > 0) {
        // Clamp-to-0 (rather than letting the counter go negative) plus the
        // fact every decayed quantity here targets zero means a mid-window
        // _settle_ctr expiry is harmless: at most it decays one extra partial
        // window's worth on values that are gliding to 0 anyway, never past
        // it and never in the wrong direction. This also means _settle_ctr
        // is NOT guaranteed to be an exact multiple of kTickInterval at
        // every supported sample rate (e.g. 44.1 kHz, as used by Rack) --
        // the clamp is what makes that safe rather than an off-by-one bug.
        _settle_ctr = _settle_ctr > kTickInterval ? _settle_ctr - kTickInterval : 0;
        _ev_phase   *= _settle_coef_tick;
        _ev_shape   *= _settle_coef_tick;
        _ev_rate    *= _settle_coef_tick;
        _kick_shape *= _settle_coef_tick;
    }

    const double window_start_phase = _phase;
    double window_dp[2 * kSeqSlots + 1];
    int window_dp_count = 1;
    int window_wraps = 0;
    window_dp[0] = _phase_inc * (1.0 + double(_ev_rate));

    // Advance the note/phrase floors in lockstep with the edge walk below
    // (per edge, via advance_floors(to_edge), plus the final leftover at
    // whichever break/return ends the walk) rather than in one lump before
    // it. A single up-front kTickInterval lump was tried first and measured
    // to desync GROW/RENEW's RNG stream from process()'s: a fire (gated
    // _on_boundary(), which resets _since_fire to 0) partway through a tick
    // window leaves every OTHER boundary in that same window reading the
    // stale post-reset count instead of its own elapsed time, and the next
    // tick's lump then adds a full window on top of that undercount. At slow
    // melody rates (a boundary every 3000+ samples, well past both floors
    // before the next one arrives) the gating decision this feeds is not
    // close enough to the floor for the error to matter, but at the rates
    // the floor exists FOR -- test_flow_melody.cpp's "the note rate has a
    // floor" case runs 14 Hz, kFlowPhraseSlots(8)/14 Hz is close to the
    // 60 ms floor itself -- the lump measurably flips which boundary gates,
    // and unlike a phase/step skew (which resyncs the next cycle) a flipped
    // gate permanently offsets the RNG stream every GROW/RENEW draw reads
    // from afterward. process() advances the same counters by 1 every
    // sample; advance_floors' clamp mirrors process()'s own
    // `if (_since_x < min) ++_since_x` so a lane already past its floor does
    // not overflow.
    auto advance_floors = [this](double samples) {
        if (samples <= 0.0) return;
        const int add = static_cast<int>(samples + 0.5);   // nearest sample
        // += add, saturating at the floor instead of overshooting past it --
        // the "- since > add" form is that same saturating add written so it
        // cannot overflow: it compares the remaining headroom to add BEFORE
        // computing since + add, rather than computing the sum first and
        // clamping after.
        if (_since_fire < _note_min_samples)
            _since_fire = _note_min_samples - _since_fire > add
                ? _since_fire + add : _note_min_samples;
        if (_since_phrase < _phrase_min_samples)
            _since_phrase = _phrase_min_samples - _since_phrase > add
                ? _since_phrase + add : _phrase_min_samples;
    };

    // Pending step/slot mismatch first: init/reset leave _cur_step = -1 and
    // the per-sample path fires step 0 (STEP) or slot 0 (FLOW melody) on its
    // very first sample the same way. This same check also absorbs kick()'s
    // phase jumps (a kick can land _phase past the current step's boundary
    // without _cur_step having moved), FLOW<->STEP re-entry (_cur_step is
    // stale from before the other mode was engaged), and accumulator-
    // rounding overshoot of the final partial-interval phase advance
    // (rounding can nudge _phase a hair past an edge the walk below already
    // accounted for). Do not simplify this to an init/reset-only check -- all
    // these cases share the same "phase says a different step/slot than
    // _cur_step remembers" symptom and this one branch catches them all.
    if (_step_mode) {
        const int step = shuffle_step_index(
            static_cast<float>(_phase), _steps, _shuffle_latched);
        if (step != _cur_step) _enter_step(step);
    } else if (_flow_melody_on()) {
        const int slot = step_index(static_cast<float>(_phase), _effective_length());
        // No advance_floors() call here, unlike every other _on_boundary()/
        // _wrap_events() call below -- deliberately, not an oversight.
        // _since_fire/_since_phrase already hold, at this exact point,
        // exactly the sample count process() would show for "elapsed time
        // up to the start of this window": every prior tick() call fully
        // accounted for its own kTickInterval samples before returning
        // (per edge, plus the leftover at whichever break ends its walk --
        // see advance_floors above), and this line runs before this
        // window's own walk has consumed anything (samples_left is still
        // the full kTickInterval). Zero samples of THIS window have
        // elapsed yet, so zero is the correct credit.
        // The two triggers named above (kick's phase jump, FLOW<->STEP
        // re-entry) do not change that: both are control-rate events that
        // happen BETWEEN tick() calls, not during one -- they perturb
        // _phase/_cur_step/mode flags, not the sample clock. process()'s
        // own equivalent path agrees: a kick() applied between process()
        // calls does not get its own credit either -- the next process()
        // call's unconditional `++_since_fire` (one sample, matching real
        // elapsed time) runs BEFORE its mismatch check, same as here.
        // Measured: tests/test_lane_tick.cpp, "tick: a kick near the
        // note-rate floor does not grant it extra credit" -- crediting a
        // full kTickInterval here instead (the wrong alternative) fires a
        // boundary tick() shows open where process() still shows frozen,
        // one tick short of the floor.
        if (slot != _cur_step) { _cur_step = slot; _on_boundary(); }
    }

    // Walk every edge inside the interval, in order. Panel-reachable worst
    // case is ~8 edges (480 Hz effective STEP rate, ~12.5 samples/step); the
    // cap is a safety bound, unreachable from the panel (spec: 2*kSeqSlots).
    double samples_left = static_cast<double>(kTickInterval);
    int guard = 2 * kSeqSlots;
    while (guard-- > 0) {
        // _ev_rate can change at a wrap (GROW walk), so the per-sample rate
        // is re-derived per edge -- the per-sample path does the same.
        const double dp1 = _phase_inc * (1.0 + double(_ev_rate));
        const double next_edge = _step_mode
            ? double(shuffle_boundary_phase(
                  _cur_step + 1, _steps, _shuffle_latched))
            // FLOW melody: the next edge is the next slot boundary, straight
            // (not shuffled -- process()'s flow-melody branch uses
            // step_index(), never shuffle_step_index()). At the last slot
            // (_cur_step + 1) / length is exactly 1.0, same as the wrap
            // arm below. FLOW LFO keeps the old contract: the only edge is
            // the wrap.
            : _flow_melody_on()
                ? double(_cur_step + 1) / double(_effective_length())
                : 1.0;
        const double dist = next_edge - _phase;
        const double to_edge = dp1 > 0.0 ? dist / dp1 : 1e30;
        const bool near_endpoint =
            std::fabs(to_edge - samples_left) <= kEndpointSampleEpsilon;
        bool reached = to_edge <= samples_left;
        ProcessWindowEnd shadow_end = {_phase, window_wraps};
        bool have_shadow_end = false;
        if (near_endpoint) {
            shadow_end =
                process_window_end(window_start_phase, window_dp, window_dp_count);
            have_shadow_end = true;
            if (_step_mode) {
                const int end_step = shuffle_step_index(
                    static_cast<float>(shadow_end.phase), _steps,
                    _shuffle_latched);
                const int end_position = shadow_end.wraps * _steps + end_step;
                const int edge_position = next_edge >= 1.0
                    ? (window_wraps + 1) * _steps
                    : window_wraps * _steps + _cur_step + 1;
                reached = end_position >= edge_position;
            } else if (_flow_melody_on()) {
                // Same position-count comparison as the STEP arm above, on
                // the straight slot grid (_effective_length() slots, no
                // shuffle) instead of the shuffled step grid.
                const int length = _effective_length();
                const int end_slot = step_index(
                    static_cast<float>(shadow_end.phase), length);
                const int end_position = shadow_end.wraps * length + end_slot;
                const int edge_position = next_edge >= 1.0
                    ? (window_wraps + 1) * length
                    : window_wraps * length + _cur_step + 1;
                reached = end_position >= edge_position;
            } else {
                reached = shadow_end.wraps > window_wraps;
            }
        }
        if (!reached) {
            advance_floors(samples_left);   // leftover: no more edges this window
            _phase = have_shadow_end && shadow_end.wraps == window_wraps
                ? shadow_end.phase
                : _phase + samples_left * dp1;
            break;
        }
        advance_floors(to_edge);   // this edge's own share, before it fires
        samples_left -= to_edge;
        if (samples_left < 0.0) samples_left = 0.0; // numerical endpoint only
        if (next_edge >= 1.0) {
            _phase = 0.0;
            _wrapped = true;
            _wrap_events();
            ++window_wraps;
            if (window_dp_count < 2 * kSeqSlots + 1)
                window_dp[window_dp_count++] =
                    _phase_inc * (1.0 + double(_ev_rate));
            if (_step_mode) {
                _enter_step(0);
            } else {
                // FLOW LFO fires per wrap with _cur_step untouched (it never
                // moves in that mode); FLOW melody enters slot 0, mirroring
                // _enter_step(0)'s STEP-side assignment but without the
                // shuffle-latch update _enter_step also does (flow melody
                // uses a straight grid -- process()'s own flow-melody branch
                // never touches _shuffle_latched either).
                if (_flow_melody_on()) _cur_step = 0;
                _on_boundary();
            }
        } else {
            _phase = next_edge;
            // Non-wrap edge: an interior STEP boundary, or now (Task 11) an
            // interior FLOW-melody slot boundary. FLOW LFO's next_edge is
            // always 1.0, so it never reaches this arm. Mirror _enter_step
            // only for STEP (it also updates _shuffle_latched, a STEP-only
            // concept); FLOW melody sets _cur_step directly, matching
            // process()'s own flow-melody branch.
            if (_step_mode) _enter_step(_cur_step + 1);
            else { _cur_step = _cur_step + 1; _on_boundary(); }
        }
    }

    // Continuous FLOW LFO only: FLOW melody holds _target between boundaries
    // (set inside _on_boundary() above), same as process()'s flow-melody
    // branch at process()'s trailing "No per-sample recompute" comment.
    if (!_step_mode && !_flow_melody_on() && !_frozen)
        _target = _compute_raw();

    float smoothed = _slew_tick.process(_target);
    return apply_range(smoothed, _range);
}
