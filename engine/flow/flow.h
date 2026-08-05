// engine/flow/flow.h
//
// The flow runtime core (spec §3-§5): owns the live Terrain, the macro
// knob/CV sums, the weather clock, and one control-rate tick() that
// evaluates every story curve and pushes changed values into the
// Instrument through apply_param. No heap, no hardware types -- the same
// object runs under the desktop hosts and (later) the firmware shell.
// The gesture layer (NEW / partial NEW / UNDO / LOCK blending) is Task 7;
// its entry points exist here as stubs so Plan B's surface is complete.
#pragma once
#include <cstdint>
#include "flow/terrain.h"
#include "flow/taste.h"
#include "instrument.h"

namespace spky { namespace flow {

class Flow {
public:
    void init(Instrument* inst, float ctrl_hz);       // ctrl_hz = tick rate
    void wake(const TerrainState& s);                 // instant, no blend
    void set_macro(int m, float v);                   // knob, 0..1
    void set_cv(int m, float v);                      // additive, any range
    void tick();                                      // one control tick
    void new_full();                                  // Task 7
    void new_partial(uint8_t macro_mask);             // Task 7
    void undo();                                      // Task 7
    void set_lock(bool on);
    bool locked() const { return _locked; }
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

    Instrument* _inst = nullptr;
    float  _ctrl_hz = 100.f;
    double _dt = 0.01;
    double _t = 0.0;              // flow clock, advances 1/ctrl_hz per tick
    double _terrain_t0 = 0.0;     // weather time origin = last wake
    bool   _woken = false;
    bool   _locked = false;       // stored here; gesture semantics are Task 7
    float  _blend_phase = 1.f;

    TerrainState _state;
    Terrain      _terrain{};
    float _knob[MACRO_COUNT] = {};
    float _cv[MACRO_COUNT]   = {};
    float _eff[MACRO_COUNT]  = {};

    float _pushed[P_COUNT]   = {};  // last value handed to apply_param
    int   _step_now[P_COUNT] = {};  // discrete hysteresis state (step index)
    float _slew_v[2]         = {};  // one-pole state: [0]=REV_SIZE [1]=REV_DECAY
};

#ifdef SPKY_TESTING
// Test-only terrain accessor (the pattern song_position_for_test set):
// the tests read the live curves without the hosts ever seeing them.
inline const Terrain& terrain_of(const Flow& f) { return f.terrain_for_test(); }
#endif

} } // namespace spky::flow
