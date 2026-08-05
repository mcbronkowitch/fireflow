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
    void wake(const TerrainState& s);                 // instant, no blend
    void set_macro(int m, float v);                   // knob, 0..1
    void set_cv(int m, float v);                      // additive, any range
    void tick();                                      // one control tick
    // The NEW gesture family (§5). All three are no-ops while locked or
    // before the first wake(); an ACCEPTED press moves state() to the new
    // terrain immediately and drops blend_phase() to 0, from where it ramps
    // back to 1 over kBlendS.
    void new_full();                                  // a whole new place
    void new_partial(uint8_t macro_mask);             // reroll marked domains
    void undo();                                      // one slot; re-undo = redo
    void set_lock(bool on);                           // always works itself
    bool locked() const { return _locked; }
    bool can_undo() const { return _have_undo; }
    const TerrainState& state() const { return _state; }
    float blend_phase() const { return _blend_phase; }  // 1.f when settled
    float eff_macro(int m) const { return _eff[m]; }  // clamp(knob+cv+weather)
    float param_now(int p) const { return _pushed[p]; }  // last pushed value
    double now_s() const { return _t; }               // flow-internal clock

#ifdef SPKY_TESTING
    const Terrain& terrain_for_test() const { return _terrain; }
#endif

private:
    void recompute_and_push(bool force);
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

    float _knob[MACRO_COUNT] = {};
    float _cv[MACRO_COUNT]   = {};
    float _eff[MACRO_COUNT]  = {};

    float _pushed[P_COUNT]   = {};  // last value handed to apply_param
    int   _step_now[P_COUNT] = {};  // discrete hysteresis state (step index)
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
    double _duck_t[2] = { -1e9, -1e9 };  // per-deck switch instants, flow clock
};

#ifdef SPKY_TESTING
// Test-only terrain accessor (the pattern song_position_for_test set):
// the tests read the live curves without the hosts ever seeing them.
inline const Terrain& terrain_of(const Flow& f) { return f.terrain_for_test(); }
#endif

} } // namespace spky::flow
