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
    float phase;
    int wraps;
};

// Rare endpoint fallback for tick(): replay only process()'s raw float phase
// additions for the whole control window. Per-wrap rates come from the real
// tick traversal, so this shadow performs no events and consumes no RNG.
static ProcessWindowEnd process_window_end(
    float phase, const float* phase_per_sample_by_wrap, int rate_count) {
    int wraps = 0;
    for (int sample = 0; sample < ModLane::kTickInterval; ++sample) {
        const int rate_index = wraps < rate_count ? wraps : rate_count - 1;
        phase += phase_per_sample_by_wrap[rate_index];
        while (phase >= 1.f) {
            phase -= 1.f;
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
    _rng.seed(seed);
    _phase = 0.f;
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

float ModLane::phase_eff() const { float p = _phase + _ev_phase; return p - std::floor(p); }

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

void ModLane::set_step(bool on, int steps) {
    const bool entering_step = on && !_step_mode;
    if (entering_step) _shuffle_latched = _shuffle_target;
    if (entering_step) { _note_age = 0; _note_hold = 0; }  // STEP entry: no stale sustain
    // Entering STEP disarms the follower so its first follow() call lands on
    // the deck's current position instead of replaying the whole count.
    if (entering_step) { _follow_armed = false; _follow_jumped = false; }
    int new_steps = steps < 1 ? 1 : steps;
    if (_melodic) {
        int old_n = _steps > kSeqSlots ? kSeqSlots : _steps;
        int new_n = new_steps > kSeqSlots ? kSeqSlots : new_steps;
        if (new_n != old_n)
            _song.length_pending = true; // only when effective length changes
    }
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
        int old_step = shuffle_step_index(_phase, _steps, _shuffle_latched);
        float old_frac = shuffle_step_fraction(
            _phase, old_step, _steps, _shuffle_latched);
        float pos = std::fmod(
            static_cast<float>(old_step) + old_frac,
            static_cast<float>(new_steps));
        _phase = shuffle_phase_for_position(pos, new_steps, _shuffle_latched);
        _cur_step = shuffle_step_index(_phase, new_steps, _shuffle_latched);
    }
    _step_mode = on;
    _steps = new_steps;
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
    _phase_inc = (_rate_hz / _sr) * clock_scale();
}

void ModLane::new_phrase() {
    if (_melodic) _song.new_pending = true;
}

int ModLane::_effective_length() const {
    if (_steps < 1) return 1;
    return _steps > kSeqSlots ? kSeqSlots : _steps;
}

void ModLane::_generate_pattern_a() {
    MelodyPattern& pattern = _song.patterns[0];
    generate_phrase(_song.selected_form, _rng, _steps,
                    pattern.pitch, pattern.gate, pattern.motif_id,
                    pattern.layout);
    pg_gen_groove(_rng, pattern.layout.motif_len, pattern.cell_groove);
    expand_pattern_groove(
        pattern.cell_groove, _steps, pattern.pattern_groove);
}

void ModLane::_derive_pattern_b() {
    derive_turnaround(_song.patterns[0], _steps, _rng, _song.patterns[1],
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
    if (_melodic && _step_mode && _cur_step < 0 &&
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
    _phase += dphase;
    _phase -= std::floor(_phase);          // permanent wrap into [0,1)
    _kick_shape += dshape;                 // decays back to 0 over ~1.5 s
}

void ModLane::nudge_slots(int n, float dshape) {
    // Move the offset AND the remembered position by the same amount: the
    // jump must not look like elapsed time, or the next follow() would replay
    // n slots (or, for a negative n, stall until the deck caught back up).
    _follow_offset += n;
    _follow_pos    += n;
    _follow_jumped  = true;
    _kick_shape    += dshape;
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
    // RST is the resync gesture: it clears the SPOT offset too, so the lane
    // comes back to the deck's own slot 0 rather than to a stumbled one.
    _follow_pos    = 0;
    _follow_offset = 0;
    _follow_armed  = false;
    _follow_jumped = false;
    _note_age = 0;
    _note_hold = 0;
    _slew.reset(_target);
    _slew_tick.reset(_target);
}

float ModLane::_compute_raw() const {
    float ph = _phase + _ev_phase;
    ph -= std::floor(ph);
    float sh = clampf(_shape + _ev_shape + _shape_offset + _kick_shape, 0.f, 1.f);
    return shape_value(ph, sh, _active_pattern().pitch[_sh_slot()]);
}

int ModLane::_sh_slot() const {
    if (!_step_mode) return 0;                 // FLOW: one slot, loop-stable per cycle
    int s = _cur_step < 0 ? 0 : _cur_step;
    return s % kSeqSlots;
}

int ModLane::_groove_k() const {
    const MelodyPattern& pattern = _active_pattern();
    int L = (_melodic && _step_mode)
        ? static_cast<int>(pattern.pattern_groove.len)
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
    if (_step_mode) {
        const int groove_length =
            pattern.pattern_groove.len < 1
                ? 1 : pattern.pattern_groove.len;
        return pattern.pattern_groove.rank_of_slot[
                   slot % groove_length] < _groove_k();
    }
    const int groove_length =
        pattern.cell_groove.len < 1 ? 1 : pattern.cell_groove.len;
    return pattern.cell_groove.rank_of_slot[slot % groove_length] <
           _groove_k();
}

void ModLane::_on_boundary() {
    int slot = _sh_slot();
    // STEP consults the effective gate (groove rank vs DENSE); FLOW has no
    // per-step gate so it always fires (no freeze source after PROBABILITY).
    bool gated = _step_mode ? _effective_gate(slot) : true;
    _frozen = !gated;
    if (gated) {
        _fired = true;
        if (_melodic && _step_mode) _start_note(slot);
        if (_variation > 0.f && !(_melodic && !_step_mode))
            _mutate_slot(slot);  // GROW pitch
        _target = _compute_raw();
    } else {
        ++_note_age;   // rest step: the running note ages toward its release
    }
    // if !gated: hold the previous _target (frozen) — and the buffer slot with it
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
    int n = _steps > kSeqSlots ? kSeqSlots : _steps;   // effective phrase length
    if (n < 1) n = 1;
    int dist = 1;                                       // steps to the next note
    while (dist < n && !_effective_gate((slot + dist) % n)) ++dist;
    const MelodyPattern& pattern = _active_pattern();
    int hold = 1;
    if (_melodic && _step_mode) {
        const int groove_length =
            pattern.pattern_groove.len < 1
                ? 1 : pattern.pattern_groove.len;
        hold = static_cast<int>(
            pattern.pattern_groove.note_len[slot % groove_length]);
    } else {
        const int groove_length =
            pattern.cell_groove.len < 1 ? 1 : pattern.cell_groove.len;
        hold = static_cast<int>(
            pattern.cell_groove.note_len[slot % groove_length]);
    }
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
    if (!_melodic || !_step_mode) return;
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
        if (_melodic && _step_mode) _renew_units();
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

    if (_melodic && !_step_mode)
        return;

    _evolve_outgoing_pattern();
    if (_melodic && _step_mode) {
        if (pending) _apply_pending_song_work();
        else         _advance_song();
    }
}

float ModLane::process() {
    _fired = false;
    _wrapped = false;
    _apply_preroll_work();
    _kick_shape *= _kick_coef;                 // SPOT shape offset fades toward 0
    if (_settle_ctr > 0) {                     // SETTLE: glide EVOLVE walks + kick to 0
        --_settle_ctr;
        _ev_phase   *= _settle_coef;
        _ev_shape   *= _settle_coef;
        _ev_rate    *= _settle_coef;
        _kick_shape *= _settle_coef;
    }
    _phase += _phase_inc * (1.f + _ev_rate);
    bool wrapped = false;
    while (_phase >= 1.f) { _phase -= 1.f; wrapped = true; }
    _wrapped = wrapped;

    if (wrapped) _wrap_events();

    if (_step_mode) {
        const int step = shuffle_step_index(_phase, _steps, _shuffle_latched);
        if (step != _cur_step) _enter_step(step);
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

    const float window_start_phase = _phase;
    float window_dp[2 * kSeqSlots + 1];
    int window_dp_count = 1;
    int window_wraps = 0;
    window_dp[0] = _phase_inc * (1.f + _ev_rate);

    // Pending step mismatch first: init/reset leave _cur_step = -1 and the
    // per-sample path fires step 0 on its very first sample the same way.
    // This same check also absorbs kick()'s phase jumps (a kick can land
    // _phase past the current step's boundary without _cur_step having
    // moved), FLOW->STEP re-entry (_cur_step is stale from before FLOW was
    // engaged), and float overshoot of the final partial-interval phase
    // advance (rounding can nudge _phase a hair past a step edge the walk
    // below already accounted for). Do not simplify this to an init/reset-
    // only check -- all four cases share the same "phase says a different
    // step than _cur_step remembers" symptom and this one branch catches
    // them all.
    if (_step_mode) {
        const int step = shuffle_step_index(_phase, _steps, _shuffle_latched);
        if (step != _cur_step) _enter_step(step);
    }

    // Walk every edge inside the interval, in order. Panel-reachable worst
    // case is ~8 edges (480 Hz effective STEP rate, ~12.5 samples/step); the
    // cap is a safety bound, unreachable from the panel (spec: 2*kSeqSlots).
    float samples_left = static_cast<float>(kTickInterval);
    int guard = 2 * kSeqSlots;
    while (guard-- > 0) {
        // _ev_rate can change at a wrap (GROW walk), so the per-sample rate
        // is re-derived per edge -- the per-sample path does the same.
        const float dp1 = _phase_inc * (1.f + _ev_rate);
        const float next_edge = _step_mode
            ? shuffle_boundary_phase(_cur_step + 1, _steps, _shuffle_latched)
            : 1.f;
        const float dist = next_edge - _phase;
        const float to_edge = dp1 > 0.f ? dist / dp1 : 1e30f;
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
                const int end_step =
                    shuffle_step_index(shadow_end.phase, _steps, _shuffle_latched);
                const int end_position = shadow_end.wraps * _steps + end_step;
                const int edge_position = next_edge >= 1.f
                    ? (window_wraps + 1) * _steps
                    : window_wraps * _steps + _cur_step + 1;
                reached = end_position >= edge_position;
            } else {
                reached = shadow_end.wraps > window_wraps;
            }
        }
        if (!reached) {
            _phase = have_shadow_end && shadow_end.wraps == window_wraps
                ? shadow_end.phase
                : _phase + samples_left * dp1;
            break;
        }
        samples_left -= to_edge;
        if (samples_left < 0.f) samples_left = 0.f; // numerical endpoint only
        if (next_edge >= 1.f) {
            _phase = 0.f;
            _wrapped = true;
            _wrap_events();
            ++window_wraps;
            if (window_dp_count < 2 * kSeqSlots + 1)
                window_dp[window_dp_count++] =
                    _phase_inc * (1.f + _ev_rate);
            if (_step_mode) _enter_step(0);
            else _on_boundary();         // FLOW fires per wrap; STEP fires step 0
        } else {
            _phase = next_edge;
            _enter_step(_cur_step + 1);
        }
    }

    if (!_step_mode && !_frozen) _target = _compute_raw();   // continuous FLOW

    float smoothed = _slew_tick.process(_target);
    return apply_range(smoothed, _range);
}
