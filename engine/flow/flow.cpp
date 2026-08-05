// engine/flow/flow.cpp
//
// One control tick, in spec order (§3/§4): clock, pre-weather macro sums,
// weather offsets, story-curve candidates (with the shared-target combine),
// discrete hysteresis, the SPACE slew, then guarded pushes through
// apply_param. Everything is fixed-size arithmetic over the live Terrain --
// no heap, no host includes.
#include "flow/flow.h"
#include "util/fast_sin.h"
#include <cmath>

namespace spky { namespace flow {
namespace {

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Piecewise-linear story curve on the quadrant grid {0, .25, .5, .75, 1}.
float eval_curve(const Curve& c, float x) {
    float g = x * 4.f;
    int i = int(g);
    if (i > 3) i = 3;
    return c.bp[i] + (c.bp[i + 1] - c.bp[i]) * (g - float(i));
}

inline float step_size(const ParamInfo& pi) {
    return (pi.hi - pi.lo) / float(pi.steps - 1);
}

} // namespace

void Flow::init(Instrument* inst, float ctrl_hz) {
    _inst = inst;
    _ctrl_hz = ctrl_hz;
    _dt = 1.0 / double(ctrl_hz);
    _t = 0.0;
    _woken = false;
    for (int m = 0; m < MACRO_COUNT; ++m) {
        _knob[m] = 0.f; _cv[m] = 0.f; _eff[m] = 0.f;
    }
}

void Flow::wake(const TerrainState& s) {
    _state = s;
    _terrain = generate(s);
    _terrain_t0 = _t;          // weather time origin = wake
    _woken = true;
    _blend_phase = 1.f;        // instant, no blend
    // Force: land hysteresis and slew states on their computed values and
    // push EVERY param once -- non-storied at base, storied at their curve
    // value under the current eff.
    recompute_and_push(true);
}

void Flow::set_macro(int m, float v) { _knob[m] = clamp01(v); }
void Flow::set_cv(int m, float v)    { _cv[m] = v; }  // clamped in the sum

void Flow::tick() {
    if (!_woken) return;
    _t += _dt;                                        // (1) clock first
    recompute_and_push(false);
}

void Flow::new_full() {}                              // Task 7
void Flow::new_partial(uint8_t) {}                    // Task 7
void Flow::undo() {}                                  // Task 7
void Flow::set_lock(bool on) { _locked = on; }        // Task 7 uses the flag

// Discrete step with hysteresis (§4). Without the guard, a macro parked on
// a step seam re-quantizes every tick and the engine gets a new FORM/SONG
// dozens of times a minute -- the pass-through version of this function
// flips P_FORM_A 14 times over the test's hover sweep. The guard: hold the
// current step until the value passes the seam by a further
// kHysteresisFrac of a step, then snap to whatever step the value is
// nearest (so a large jump still lands in one move, not one step at a
// time). force (wake) lands directly on the nearest step.
float Flow::quantize_hyst(int p, float v, bool force) {
    const ParamInfo& pi = kParams[p];
    const float size = step_size(pi);
    const float x = (v - pi.lo) / size;        // continuous step coordinate
    int nearest = int(std::floor(x + 0.5f));
    if (nearest < 0) nearest = 0;
    if (nearest > pi.steps - 1) nearest = pi.steps - 1;
    const float n = float(_step_now[p]);
    if (force || x > n + 0.5f + kHysteresisFrac
              || x < n - 0.5f - kHysteresisFrac)
        _step_now[p] = nearest;
    return pi.lo + float(_step_now[p]) * size;
}

// One-pole lazy follower for SPACE's SIZE/DECAY (kSpaceSlewS): the room
// drifts toward its new size instead of jumping. force (wake) lands it.
float Flow::space_slew(int slot, float target, bool force) {
    if (force) { _slew_v[slot] = target; return target; }
    const float a = 1.f - std::exp(-float(_dt) / kSpaceSlewS);
    _slew_v[slot] += (target - _slew_v[slot]) * a;
    return _slew_v[slot];
}

void Flow::recompute_and_push(bool force) {
    // (2) Pre-weather macro sums: knob + CV, clamped.
    float s[MACRO_COUNT];
    for (int m = 0; m < MACRO_COUNT; ++m) s[m] = clamp01(_knob[m] + _cv[m]);

    // (3) Weather offsets. Each oscillator adds depth * sin into its target
    // macro; the per-macro total is the AVERAGE over the oscillators that
    // landed on that macro, not the sum -- generate() may aim up to 4 oscs
    // at one macro, and the spec bound (|offset| <= kWeatherDepthMax) must
    // hold by construction, not by luck. The whole offset is then scaled
    // by the pre-weather MOTION sum: MOTION is the weather's depth control.
    float off[MACRO_COUNT] = {};
    int   cnt[MACRO_COUNT] = {};
    const double ts = _t - _terrain_t0;
    for (int i = 0; i < _terrain.weather_n; ++i) {
        const int m = _terrain.weather_target[i];
        // MOTION itself is never weathered: it is the weather's depth
        // control, and letting the weather modulate its own depth would be
        // exactly the feedback loop the spec excludes. generate() may
        // still aim an osc here; it goes quiet at runtime.
        if (m == M_MOTION) continue;
        off[m] += _terrain.weather_depth[i]
                * fast_sin(float(ts / double(_terrain.weather_period_s[i])));
        ++cnt[m];
    }
    for (int m = 0; m < MACRO_COUNT; ++m) {
        const float o = cnt[m] ? (off[m] / float(cnt[m])) * s[M_MOTION] : 0.f;
        _eff[m] = clamp01(s[m] + o);
    }

    // (4) Story-curve candidates. When two macros curve the same param
    // (BRIGHT and SPACE both own REVMIX_A / REV_DECAY), the pushed value
    // is the candidate FARTHEST from the terrain base (tie -> lower macro
    // index, via the strict > below on an ascending walk): deterministic,
    // and whichever knob is actually doing something wins, so neither
    // knob ever feels dead.
    float cand[P_COUNT];
    float dist[P_COUNT];
    bool  has[P_COUNT] = {};
    for (int m = 0; m < MACRO_COUNT; ++m) {
        const MacroMap& mm = _terrain.map[m];
        for (int i = 0; i < mm.n_targets; ++i) {
            const Curve& c = mm.targets[i];
            const float v = eval_curve(c, _eff[m]);
            const float d = std::fabs(v - _terrain.base[c.param]);
            if (!has[c.param] || d > dist[c.param]) {
                cand[c.param] = v; dist[c.param] = d; has[c.param] = true;
            }
        }
    }

    // (5)-(7) Quantize / slew / push. Non-storied params sit at base, so
    // the change guard means they only reach apply_param on wake (force).
    // SIZE/DECAY slew applies to the final combined value -- REV_DECAY may
    // arrive from BRIGHT's ember bloom as well as SPACE's story, and the
    // room drifts either way.
    for (int p = 0; p < P_COUNT; ++p) {
        float v = has[p] ? cand[p] : _terrain.base[p];
        if (kParams[p].steps > 0)      v = quantize_hyst(p, v, force);
        else if (p == P_REV_SIZE)      v = space_slew(0, v, force);
        else if (p == P_REV_DECAY)     v = space_slew(1, v, force);
        // Setter spam guard: push only real changes -- exact compare for
        // discrete (already snapped to the step grid), epsilon for
        // continuous.
        const bool changed = force ||
            (kParams[p].steps > 0 ? v != _pushed[p]
                                  : std::fabs(v - _pushed[p]) > 1e-6f);
        if (changed) {
            _pushed[p] = v;
            if (_inst) apply_param(*_inst, p, v);
        }
    }
}

} } // namespace spky::flow
