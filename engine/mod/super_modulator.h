#pragma once
#include <array>
#include <cstdint>
#include "mod/lane.h"
#include "mod/lane_id.h"
#include "mod/divisions.h"
#include "mod/lane_len.h"
#include "mod/rhythm_view.h"

namespace spky {

// One performable macro surface driving five independent lanes at fixed
// musical ratios of the master RATE. The PITCH lane leads (rate x1).
class SuperModulator {
public:
    void init(float sample_rate, uint32_t seed_base);

    void set_tempo_bpm(float bpm)  { _bpm = bpm; _update_rate(); }
    void set_rate(float norm)      { _rate_norm = norm; _update_rate(); }
    void set_synced(bool on)       { _synced = on; _update_tide(); _update_rate(); }
    void set_tide(float norm);     // 0..1 texture-lane rate scale; 0.5 = neutral
    float tide_mult() const { return _tide_mult; }
    void set_shape(float s);
    void set_density(float d) { _lanes[LANE_PITCH].set_density(d); }
    void set_smooth(float s);
    void set_range(float r);
    void set_variation(float v);
    void set_shuffle(float amount);
    void set_step(bool on, int steps);
    bool step_mode()  const { return _step_on; }
    int  deck_steps() const { return _deck_steps; }
    void set_fixed_slew(bool on);
    void set_form(Principle form) { _lanes[LANE_PITCH].set_form(form); }
    void set_song(SongMode song) { _lanes[LANE_PITCH].set_song(song); }
    Principle form() const { return _lanes[LANE_PITCH].form(); }
    SongMode song() const { return _lanes[LANE_PITCH].song(); }
    void set_principle(Principle p) { set_form(p); }
    void new_phrase() { _lanes[LANE_PITCH].new_phrase(); }
#ifdef SPKY_TESTING
    uint32_t song_position_for_test() const {
        return _lanes[LANE_PITCH].song_position();
    }
    uint8_t active_pattern_for_test() const {
        return _lanes[LANE_PITCH].active_pattern();
    }
    float   lane_rate_hz_for_test(int i) const { return _lanes[i].rate_hz_for_test(); }
    int     lane_slots_for_test(int i)   const { return _lanes[i].steps(); }
    int32_t deck_step_for_test()         const { return _deck_step; }
    // The amount PITCH's own phase (and so the deck's follow fraction) was
    // actually built from -- the reference a follower's phase must be
    // measured against, per shuffle_latched()'s contract (lane.h).
    float   pitch_shuffle_latched_for_test() const {
        return _lanes[LANE_PITCH].shuffle_latched();
    }
#endif
    bool pitch_gate() const { return _lanes[LANE_PITCH].gate_state(); }
    bool pitch_sustain() const { return _lanes[LANE_PITCH].note_sustain(); }
    // Slice-groove side channel (spec 2026-07-22), master/PITCH lane only.
    int   pitch_cur_step()     const { return _lanes[LANE_PITCH].cur_step(); }
    int   pitch_steps()        const { return _lanes[LANE_PITCH].steps(); }
    int   pitch_step_at_phase(float phase) const {
        return _lanes[LANE_PITCH].step_at_phase(phase);
    }
    float pitch_step_samples() const { return _lanes[LANE_PITCH].step_samples(); }

    void process();                // advance all lanes one sample

    float lane_output(int i) const { return _out[i]; }
    bool  lane_fired(int i)  const { return _lanes[i].fired(); }
    bool  lane_frozen(int i) const { return _lanes[i].frozen(); }
    float lane_phase(int i)  const { return _lanes[i].phase(); }
    float pitch_phase()      const { return _lanes[LANE_PITCH].phase(); }
    float pitch_phase_eff()  const { return _lanes[LANE_PITCH].phase_eff(); }  // audible pitch phase
    float master_hz()        const { return _master_hz; }

    // --- M4 center hooks ---
    void set_rate_scale(float pitch_s, float mod_s) {
        _pitch_scale = pitch_s; _mod_scale = mod_s; _apply_rate();
    }
    float pitch_scale() const { return _pitch_scale; }
    float mod_scale()   const { return _mod_scale; }
    void set_shape_offset(float o){ for (auto& l : _lanes) l.set_shape_offset(o); }
    void spot(Rng& rng);          // per-lane SPOT kicks (skips the PITCH lane)
    void settle()                { for (auto& l : _lanes) l.settle(); }
    // RST bar resync: all lanes restart at phase 0, so the loops land ON the
    // fresh downbeat instead of being dragged onto it by the grid servo.
    // The PITCH lane restarts on the very next sample (per-sample path). The
    // texture lanes only restart at their next 96-sample tick() call, up to
    // ~2 ms later; an off-grid RST leaves texture lane-time up to 95 samples
    // ahead of the reset pitch lane until that tick -- same accepted-
    // asymmetry class as the engine-swap case (see the spec's "Accepted
    // asymmetry" and part.h).
    // Resets every lane's phase AND this object's own onset-gap ring (moved
    // up from ModLane -- see the ring's declaration below): ModLane::reset()
    // no longer clears any rhythm state of its own, so the ring's reset must
    // happen here alongside it, the same way it used to happen inside
    // ModLane::reset() for every lane (even though only LANE_PITCH's ring
    // was ever read).
    void reset_phases() {
        for (auto& l : _lanes) l.reset(0.f);
        // Every follower derives its slot from THIS counter, not from any
        // state ModLane::reset() clears -- resetting the lanes without also
        // resetting the deck clock leaves each texture lane re-arming at
        // slot_of(_deck_step, slots) on its next follow(), an arbitrary slot
        // that depends on how long the deck had been running, while PITCH
        // restarts at phase 0. RST is the resync gesture for the whole
        // deck's clock, not just the lanes' own state.
        _deck_step = 0;
        _last_pitch_step = -1;
        _since_onset = 0;
        _onsets = 0;
        _gap[0] = _gap[1] = 0;
        _rhythm = RhythmView{};
    }
    // STEP-Einstiegs-Snap (spec 2026-07-23 sampler-performance-fixes): setzt
    // NUR die PITCH-Lane auf eine gegebene Phase. Bewusst nicht reset_phases:
    // das ist die RST-Geste und setzt alle fuenf Lanes -- ein Sprung in den
    // Texturlanes waere ein hoerbarer Ruck in Filter und Pan, der dem
    // Rhythmus nicht hilft.
    //
    // Der Onset-Gap-Ring wird mitgenullt, dieselbe Kopplung, auf der
    // reset_phases oben besteht: nach einem Phasensprung misst der erste
    // Onset sonst einen Abstand, den es nie gab. Das Nullen hier setzt
    // _rhythm.valid auf false (super_modulator.cpp, _onsets >= 3).
    //
    // Bis zur BBD-Neufassung von FLUX
    // (docs/superpowers/plans/2026-07-27-flux-bbd-delay.md, Task 6) steuerte
    // dieser Rhythmus-Blick ueber Instrument die FX-Abgriffe des ANDEREN
    // Decks: derive_offsets (taps.cpp) las ein ungueltiges RhythmView als
    // kMuted auf beiden Taps, das andere Deck verlor also seine beiden
    // Tape-Abgriffe, bis das snappende Deck erneut drei Onsets gezaehlt
    // hatte. Diese Tap-Bank ist inzwischen entfernt -- eine BBD hat keinen
    // Lesezeiger, also widersprach die Tap-Bank dem Modell, das sie
    // fuettern sollte, nicht nur ihrem Budget. `rhythm()` hat heute wieder
    // einen FX-Konsumenten: THIN liest diesen Ring ueber Instrument's
    // Kontroll-Tick und fuettert damit die FLUX des ANDEREN Decks -- als
    // Repeat-Muster, nicht mehr als Position wie bei der alten Tap-Bank.
    //
    // Das Nullen hier bleibt trotzdem richtig, unabhaengig von diesem
    // Konsumenten: reset_phases loescht denselben Ring auf dieselbe Weise,
    // RST traegt also schon denselben Preis eines Phasensprungs, und ein
    // kuenftiger Cross-Deck-Konsument von rhythm() faende sonst denselben
    // unechten Onset vor. Ob dieser Preis bleibt, ist eine offene
    // Entscheidung des Autors, keine, die dieser Kommentar trifft.
    void snap_pitch_phase(float ph) {
        _lanes[LANE_PITCH].reset(ph);
        // ModLane::reset() leaves _cur_step at -1; without also resetting
        // this, the deck-step counter in process() reads the next real step
        // change as an extra elapsed step (cs != _last_pitch_step with
        // _last_pitch_step >= 0 still holding its pre-snap value) and
        // rotates every follower by one phantom slot it never actually
        // crossed.
        _last_pitch_step = -1;
        _since_onset = 0;
        _onsets = 0;
        _gap[0] = _gap[1] = 0;
        _rhythm = RhythmView{};
    }
    float base_hz()   const { return _base_hz; }   // rate before COUPLE/DRIFT scale
    bool  synced()    const { return _synced; }
    int   division()  const { return division_index(_rate_norm); }
    // Step-clock factor of the pitch/master lane (8/steps in STEP, 1 in FLOW):
    // the grid servo scales its transport target by this (spec 2026-07-17).
    float clock_scale() const { return _lanes[LANE_PITCH].clock_scale(); }
    // The master lane's rhythm. Fed the FX tap bank before it was deleted (a
    // BBD has no read pointer, docs/superpowers/plans/2026-07-27-flux-bbd-delay.md
    // Task 6); THIN is its current FX consumer: Instrument's control tick reads
    // this deck's rhythm
    // through this accessor and feeds it to the SIBLING deck's
    // `Flux::set_rhythm` (see the longer comment on engine/instrument.h's
    // rhythm(int p)). Also read directly by tests.
    const RhythmView& rhythm() const { return _rhythm; }

private:
    void _update_rate();
    void _apply_rate();
    void _update_tide();
    void _apply_steps();

    std::array<ModLane, LANE_COUNT> _lanes;
    std::array<float, LANE_COUNT>   _out {};

    float    _sr = 48000.f;
    float    _bpm = 120.f;
    float    _rate_norm = 0.5f;
    bool     _synced = false;
    bool    _step_on    = false;   // the deck's STEP flag; drives the grid lock
    int     _deck_steps = 8;       // the phrase length; PITCH's slot count
    // The deck's own clock, in whole steps. This integer is what makes the
    // grid exact: every follower derives its slot from it, so no float
    // rounding can put two lanes on different boundaries. int32_t is ample
    // for how long this can actually run continuously. steps/s is always
    // 8 x master_hz regardless of STEPS (clock_scale() = 8/steps cancels
    // the step count back out -- lane.h), so the bound is on master_hz, and
    // the worst case is SYNC, not FREE: FREE tops out at kRateFreeMax (30 Hz,
    // divisions.h), but SYNC's fastest division, "1/32", has cpb = 8
    // (divisions.h) and its Hz scales with tempo, and the external-clock path
    // accepts BPM up to 400 (host/vcv/src/Spotymod.cpp), well past the 240
    // BPM the TEMPO knob alone reaches. That puts the true fastest rate at
    // 8 x (400/60 x 8) ~= 427 steps/s. INT32_MAX / 427 is about 58 days; at a
    // musical 8 steps/s it's still 8.5 years. That is a "practically never"
    // bound, not a "and it's fine if it does" one -- signed overflow is UB in
    // C++, not the graceful wraparound this comment used to imply. int32_t is
    // still the right choice: this engine ships on a Cortex-M7, where 64-bit
    // arithmetic is not free.
    int32_t _deck_step       = 0;
    int     _last_pitch_step = -1;
    float    _pitch_scale = 1.f;   // COUPLE/DRIFT on the melody clock
    float    _mod_scale   = 1.f;   // COUPLE/DRIFT on the texture lanes
    float    _master_hz = 1.f;
    float    _base_hz    = 1.f;   // rate from knob/sync, before rate_scale
    float    _tide_norm = 0.5f;   // TIDE knob position (0..1)
    float    _tide_mult = 1.f;    // effective factor: ladder rung or free curve
    int      _tick_ctr = 0;        // texture-lane raster; 0 = tick on next process()

    // Onset-gap ring for the PITCH lane only. Was 5 copies (one per lane) on
    // ModLane -- 28 bytes each, 10 total across both parts' lanes, of which
    // 9 were never read (only LANE_PITCH's rhythm ever reaches rhythm(),
    // above). Moved up here: this is also the object that actually knows
    // which lane is the master, so it is the structurally right owner, not
    // just a cheaper one.
    //
    // _since_onset counts samples since the last gated boundary; _gap holds
    // the last two completed gaps, most recent first; _onsets saturates at 3
    // (the count that makes two gaps real). _rhythm is the snapshot
    // consumers see, latched from the ring at a wrap. Fed from
    // _lanes[LANE_PITCH].wrapped()/.fired() in process() -- see there for
    // the load-bearing latch-then-record order this mirrors from ModLane's
    // old _wrap_events()-before-_on_boundary() sequence.
    static constexpr int32_t kSinceOnsetMax = 1 << 24;   // ~5.8 min @ 48 kHz
    int32_t    _since_onset = 0;
    int32_t    _gap[2] = { 0, 0 };
    int        _onsets = 0;
    RhythmView _rhythm;
};

} // namespace spky
