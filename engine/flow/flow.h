// engine/flow/flow.h
//
// The flow runtime core (spec §3-§5): owns the live Terrain, the macro
// knob/CV sums, the weather clock, and one control-rate tick() that
// evaluates every story curve and pushes changed values into the
// Instrument through apply_param. No heap, no hardware types -- the same
// object runs under the desktop hosts and (later) the firmware shell.
//
// The gesture layer (spec §5) lives here too: new_full / new_partial / undo
// / set_lock all funnel into ONE blend -- a kBlendS ramp between the
// outgoing and the incoming terrain, both evaluated live every tick so the
// knobs keep working through it. Only the button/timing state machine
// (tap vs. hold vs. mark) belongs to the host; this class is its verbs.
#pragma once
#include <cstdint>
#include "flow/terrain.h"
#include "flow/taste.h"
#include "flow/flow_rng.h"
#include "instrument.h"

namespace spky { namespace flow {

class Flow {
public:
    void init(Instrument* inst, float ctrl_hz);       // ctrl_hz = tick rate
    // Change the control rate on a LIVE instrument (VCV calls
    // onSampleRateChange at runtime). Recomputes only what the rate feeds --
    // the tick period and the SPACE slew coefficient -- and touches no other
    // state: lock, undo slot, a blend in flight, both macro arrays, the
    // weather clock and every pushed value survive. init() is the only verb
    // that resets those, and it now routes its own rate work through here so
    // the two can never drift apart.
    void set_ctrl_hz(float ctrl_hz);
    void wake(const TerrainState& s);                 // instant, no blend
    void set_macro(int m, float v);                   // knob, 0..1
    void set_cv(int m, float v);                      // additive, any range
    void tick();                                      // one control tick
    // The NEW gesture family (§5). All three refuse (return false, change
    // nothing) while locked or before the first wake(); an ACCEPTED press
    // returns true, moves state() to the new terrain immediately and drops
    // blend_phase() to 0, from where it ramps back to 1 over kBlendS.
    //
    // The bool is what lets a host tell an accepted press from a refused one:
    // the gesture decoder knows a press was refused only for the locked case
    // it was told about, so without a return here a refusal blink would be
    // unrenderable for the other reasons (not woken, empty undo slot, empty
    // macro mask).
    bool new_full();                                  // a whole new place
    bool new_partial(uint8_t macro_mask);             // reroll marked domains
    bool undo();                                      // one slot; re-undo = redo
    void set_lock(bool on);                           // always works itself
    bool locked() const { return _locked; }
    // The genre lock (spec 2026-08-07 §2). Constrains which archetype
    // new_full() may draw and NOTHING else -- no parameter moves when this
    // changes, which test_flow_runtime.cpp pins. ARCH_ANY (flow_ids.h) is the
    // unconstrained default.
    //
    // Note what this is: state that lives in neither TerrainState nor the
    // terrain code, and that wake()/init() do NOT reset. Flow therefore stops
    // being a pure function of (TerrainState, macros). The host owns it and
    // re-pushes it every control tick.
    //
    // CONTRACT: callers must pass a valid Archetype (0..ARCH_COUNT-1) or
    // ARCH_ANY. This is deliberately NOT validated here, so exactly one place
    // decides -- the caller, which is the only one that knows what an invalid
    // value means for its own control. Pass anything else and draw_new's genre
    // branch matches no master in kGenreDrawCap draws: new_full() then returns
    // the default TerrainState every press, which from the second press on
    // equals the current one, so NEW goes silently dead. Glow.cpp's
    // controlTick guards its switch position for that reason.
    void set_genre(int arch) { _genre = arch; }
    int  genre() const { return _genre; }
    // Explicit tonality (spec 2026-08-07 §3). -1 means AUTO: the terrain's own
    // drawn value, i.e. exactly today's behaviour. Any other value replaces
    // what this Flow pushes for that parameter, immediately, without touching
    // the terrain -- so AUTO gives the terrain's value back intact.
    //
    // Like _genre, these are host-owned settings and not part of TerrainState;
    // wake()/init() do not reset them.
    void set_scale_override(int scale) { _scale_ovr = scale; }
    void set_root_override(int root)   { _root_ovr = root; }
    int  scale_override() const { return _scale_ovr; }
    int  root_override() const  { return _root_ovr; }
    bool can_undo() const { return _have_undo; }
    const TerrainState& state() const { return _state; }
    // The undo slot, for persistence (§5: "Patch reload ... and later hardware
    // boots restore the full saved state -- current terrain code, lock, AND
    // the undo slot"). wake() deliberately clears the slot, so a host restores
    // in that order: wake(saved state), set_lock(saved lock),
    // restore_undo(saved undo, saved can_undo). Restoring a slot is
    // bookkeeping, not a gesture -- it starts no blend and moves no parameter,
    // so a reloaded patch sounds exactly as it did when saved and the first
    // undo press behaves as if the session had never ended.
    const TerrainState& undo_state() const { return _undo; }
    void restore_undo(const TerrainState& s, bool have_undo);
    float blend_phase() const { return _blend_phase; }  // 1.f when settled
    float eff_macro(int m) const { return _eff[m]; }  // clamp(knob+cv+weather)
    float param_now(int p) const { return _pushed[p]; }  // last pushed value
    double now_s() const { return _t; }               // flow-internal clock

#ifdef SPKY_TESTING
    const Terrain& terrain_for_test() const { return _terrain; }
    // The two halves of the NEW schedule, for the tests that pin it: the blend
    // phase a discrete switches at, and the flow-clock instant each deck's
    // duck is centred on. Hosts have no business reading either.
    float switch_phase_for_test(int p) const { return switch_phase_for(p); }
    // Both indices come from literals in the tests, and this accessor exists
    // only under SPKY_TESTING -- so there is deliberately no range assert
    // here. Release is mandatory for this project (CMAKE_CXX_FLAGS_RELEASE
    // carries -DNDEBUG), which would compile any assert out of every build
    // anyone is allowed to run: a guard that cannot go red.
    double duck_t_for_test(int deck, int slot) const {
        return _duck_t[deck][slot];
    }
#endif

private:
    void recompute_and_push(bool force);
    void push_mode_and_steps(bool force);
    float quantize_hyst(int p, float v, bool force);
    float space_slew(int slot, float target, bool force);
    // Gesture helpers (flow.cpp).
    void begin_blend(const TerrainState& target);
    void weather_of(const Terrain& t, double ts, float* off) const;
    void eval_terrain(const Terrain& t, const float* eff, float* out) const;
    float switch_phase_for(int p) const;
    float duck(int deck, float revmix) const;

    Instrument* _inst = nullptr;
    float  _ctrl_hz = 100.f;
    double _dt = 0.01;
    double _t = 0.0;              // flow clock, advances 1/ctrl_hz per tick
    double _terrain_t0 = 0.0;     // weather time origin = last wake
    bool   _woken = false;
    bool   _locked = false;       // refuses NEW / partial / undo, not set_lock
    float  _blend_phase = 1.f;

    TerrainState _state;          // the TARGET state: accepted presses show up
                                  // here immediately, blend_phase() is the lag
    Terrain      _terrain{};      // incoming terrain (== live once settled)
    Terrain      _prev_terrain{}; // outgoing terrain, alive while blending
    TerrainState _undo;           // the one slot; undo() swaps it with _state
    bool  _have_undo = false;
    Rng   _seq;                   // NEW's press-chain Rng, re-seeded by wake()
    int   _genre = ARCH_ANY;      // draw constraint for new_full(), not state
    int   _scale_ovr = -1;        // -1 = AUTO (use the terrain's P_SCALE)
    int   _root_ovr  = -1;        // -1 = AUTO (use the terrain's P_ROOT)

    float _knob[MACRO_COUNT] = {};
    float _cv[MACRO_COUNT]   = {};
    float _eff[MACRO_COUNT]  = {};

    float _pushed[P_COUNT]   = {};  // last value handed to apply_param
    int   _step_now[P_COUNT] = {};  // discrete hysteresis state (step index)
    bool  _mode_now          = false;      // last pushed mode, change guard
    int   _steps_now[2]      = { -1, -1 }; // last pushed step counts
    float _slew_v[2]         = {};  // one-pole state: [0]=REV_SIZE [1]=REV_DECAY
    float _slew_a = 0.f;            // its coefficient, from _dt / kSpaceSlewS

    // Blend bookkeeping. _cont_now is last tick's combined CONTINUOUS value
    // (pre-quantize, pre-slew, duck-free) and _cand_cur the incoming
    // terrain's own candidate for it; their difference at press time becomes
    // _resid, the continuity offset that makes a mid-blend re-press restart
    // the ramp from the interpolated state instead of from the outgoing
    // terrain's settled value. _resid decays linearly with the ramp.
    float _cont_now[P_COUNT] = {};
    float _cand_cur[P_COUNT] = {};
    float _resid[P_COUNT]    = {};
    bool  _disc_done[P_COUNT] = {};  // has this discrete taken its one switch?
    int   _carrier_deck = 0;         // 0 = A, 1 = B (incoming terrain's role)

    // Per-deck duck schedule: the flow-clock instants each deck's reverb-send
    // hump is centred on, -1e9 meaning "no duck in that slot". A deck normally
    // needs one, but the CARRIER deck needs two on a mode-changing press: the
    // global set_sync clocking flip lands at the press (slot 1) while its own
    // engine/scale switch still lands at kCarrierStaggerFrac (slot 0). Two is
    // therefore the exact maximum, not a guess -- see begin_blend().
    static constexpr int kDucksPerDeck = 2;
    double _duck_t[2][kDucksPerDeck] = {{ -1e9, -1e9 }, { -1e9, -1e9 }};
};

#ifdef SPKY_TESTING
// Test-only terrain accessor (the pattern song_position_for_test set):
// the tests read the live curves without the hosts ever seeing them.
inline const Terrain& terrain_of(const Flow& f) { return f.terrain_for_test(); }
#endif

} } // namespace spky::flow
