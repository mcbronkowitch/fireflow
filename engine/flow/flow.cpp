// engine/flow/flow.cpp
//
// One control tick, in spec order (§3/§4): clock, pre-weather macro sums,
// weather offsets, story-curve candidates (with the shared-target combine),
// discrete hysteresis, the SPACE slew, then guarded pushes through
// apply_param. Everything is fixed-size arithmetic over the live Terrain --
// no heap, no host includes.
//
// The NEW gesture family (§5) rides the same tick: while a blend runs, every
// stage above is evaluated for BOTH the outgoing and the incoming terrain at
// the CURRENT macro values and crossfaded, so knob, CV and weather stay live
// all the way through a 6-second transition.
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

// Width of one discrete step. The callers key off steps > 0, so a future
// single-valued param (steps == 1) would divide by zero here; it gets a
// harmless width of 1 instead and quantizes to its single step 0 = lo.
inline float step_size(const ParamInfo& pi) {
    return pi.steps > 1 ? (pi.hi - pi.lo) / float(pi.steps - 1) : 1.f;
}

// Which deck owns a discrete param: 0 = A, 1 = B, -1 = neither (SCALE and
// ROOT are global -- one scale for both decks is structural, see terrain.cpp
// stage 2). The NEW stagger schedules by this.
inline int deck_of(int p) {
    switch (p) {
    case P_ENGINE_A: case P_FORM_A: case P_SONG_A: case P_STEPS_A: return 0;
    case P_ENGINE_B: case P_FORM_B: case P_SONG_B: case P_STEPS_B: return 1;
    default: return -1;
    }
}

} // namespace

// The BODY FILT runtime floor in recompute_and_push() reads this tick's
// already-pushed engine for the deck it is guarding, which only works while
// ENGINE_A/B precede FILT_A/B in the parameter table.
static_assert(P_ENGINE_A < P_FILT_A && P_ENGINE_B < P_FILT_B,
              "ENGINE_A/B must be pushed before FILT_A/B");

// Everything the control rate feeds, and nothing else. Split out of init()
// so a live host can follow a sample-rate change without rebuilding the
// instrument's whole state around it (see flow.h).
void Flow::set_ctrl_hz(float ctrl_hz) {
    _ctrl_hz = ctrl_hz;
    _dt = 1.0 / double(ctrl_hz);
    // The one-pole SPACE follower is defined in SECONDS (kSpaceSlewS), so its
    // per-tick coefficient has to be re-derived or the room would drift at
    // the wrong speed after a rate change -- audibly, at the 2x and 4x jumps
    // a host actually makes.
    _slew_a = 1.f - std::exp(-float(_dt) / kSpaceSlewS);
}

void Flow::init(Instrument* inst, float ctrl_hz) {
    _inst = inst;
    set_ctrl_hz(ctrl_hz);
    _t = 0.0;
    _woken = false;
    _locked = false;
    _have_undo = false;
    _blend_phase = 1.f;
    for (int d = 0; d < 2; ++d)
        for (int i = 0; i < kDucksPerDeck; ++i) _duck_t[d][i] = -1e9;
    for (int m = 0; m < MACRO_COUNT; ++m) {
        _knob[m] = 0.f; _cv[m] = 0.f; _eff[m] = 0.f;
    }
}

void Flow::wake(const TerrainState& s) {
    _state = s;
    _terrain = generate(s);
    _prev_terrain = _terrain;  // no blend in flight; nothing to fade from
    _terrain_t0 = _t;          // weather time origin = wake
    _woken = true;
    _blend_phase = 1.f;        // instant, no blend
    _undo = s;
    _have_undo = false;        // undo before any accepted press is a no-op
    // The NEW press chain is a pure function of the woken master: a fixed
    // start reproduces the whole chain, and because _seq keeps running,
    // consecutive presses still land on different terrains.
    _seq = make_stream(s.master, kStreamNewSeq, 0);
    _carrier_deck = _terrain.a_carries ? 0 : 1;
    for (int p = 0; p < P_COUNT; ++p) {
        _resid[p] = 0.f;
        _disc_done[p] = true;  // no switch pending
    }
    for (int d = 0; d < 2; ++d)      // no duck in flight
        for (int i = 0; i < kDucksPerDeck; ++i) _duck_t[d][i] = -1e9;
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
    if (_blend_phase < 1.f) {                         // (1b) then the ramp
        _blend_phase += float(_dt) / kBlendS;
        if (_blend_phase > 1.f) _blend_phase = 1.f;
    }
    recompute_and_push(false);
}

// ---------------------------------------------------------------------------
// The NEW gesture family (§5). Only the verbs live here: tap-vs-hold timing,
// knob marking and the refusal blink belong to the host's button state
// machine. set_lock() itself always works -- otherwise a locked terrain
// could never be unlocked.

// Start a blend toward `target`, from wherever we currently are.
//
// The ramp is between two TERRAINS, not two frozen value vectors: the
// outgoing terrain stays alive in _prev_terrain and both are re-evaluated
// every tick, so a knob turned during the transition acts on both ends at
// once. The one frozen quantity is _resid, the continuity offset: at the
// moment of the press the pushed continuous value may sit anywhere between
// the two terrains (a re-press lands mid-flight), so we remember how far it
// was from the newly-outgoing terrain's own candidate and let that offset
// decay with the new ramp. Nothing jumps at a retarget, and the offset is
// exactly zero when the press lands on a settled instrument.
void Flow::begin_blend(const TerrainState& target) {
    for (int p = 0; p < P_COUNT; ++p)
        _resid[p] = kParams[p].steps > 0 ? 0.f       // discretes never lerp
                                         : _cont_now[p] - _cand_cur[p];
    _prev_terrain = _terrain;
    _undo  = _state;            // one slot; undo() feeds it back in, so the
    _state = target;            // pair swaps and undo-after-undo is a redo
    _terrain = generate(target);
    _have_undo = true;
    _blend_phase = 0.f;

    // Schedule the staggered discrete switch off the INCOMING terrain's
    // roles -- the stagger exists so the new sound's lead voice does not
    // arrive at the same instant as its texture.
    _carrier_deck = _terrain.a_carries ? 0 : 1;
    const int texture_deck = 1 - _carrier_deck;
    for (int p = 0; p < P_COUNT; ++p)
        if (kParams[p].steps > 0) _disc_done[p] = false;
    for (int d = 0; d < 2; ++d)
        for (int i = 0; i < kDucksPerDeck; ++i) _duck_t[d][i] = -1e9;
    _duck_t[texture_deck][0]  = _t;
    _duck_t[_carrier_deck][0] = _t + double(kCarrierStaggerFrac * kBlendS);

    // A mode change is the one press that needs THREE ducks, because it is two
    // events on two different schedules:
    //
    //  - the clocking flip. switch_phase_for() puts P_MODE at phase 0, and
    //    set_sync is global, so this hits BOTH decks at the press. The texture
    //    deck's duck is already there; the carrier's is not -- its slot 0 sits
    //    1.5 s away, where duck() computes u = 6 and returns the send
    //    untouched. Hence the second carrier slot, at the press.
    //  - the carrier's own engine/scale switch, still at kCarrierStaggerFrac.
    //    The stagger is a by-ear decision and stays: collapsing it would move
    //    the carrier's engine change into the open, which is louder than the
    //    clocking flip it would have been traded for.
    //
    // So the carrier is ducked twice and the texture deck once, and no switch
    // of either kind ever happens outside a wash.
    const bool mode_moves =
        (_terrain.base[P_MODE] > 0.5f) != (_prev_terrain.base[P_MODE] > 0.5f);
    if (mode_moves) _duck_t[_carrier_deck][1] = _t;
}

bool Flow::new_full() {
    if (!_woken || _locked) return false;
    begin_blend(draw_new(_state, _seq));
    return true;
}

// Partial reroll: bump the marked macros' override counters and regenerate.
// Every draw outside those domains comes from a stream keyed by an untouched
// counter, so it is bit-identical -- that is the whole point of the
// (master, stream, counter) triple. The weather DOES refresh: its counter is
// the sum of all six (terrain.h), by design.
bool Flow::new_partial(uint8_t macro_mask) {
    if (!_woken || _locked) return false;
    const uint8_t valid = uint8_t((1u << MACRO_COUNT) - 1u);
    if (!(macro_mask & valid)) return false;      // nothing marked, nothing to do
    TerrainState t = _state;
    for (int m = 0; m < MACRO_COUNT; ++m)
        if (macro_mask & (1u << m)) ++t.reroll[m];
    begin_blend(t);
    return true;
}

bool Flow::undo() {
    if (!_woken || _locked || !_have_undo) return false;
    const TerrainState back = _undo;   // copy first: begin_blend overwrites
    begin_blend(back);                 // _undo with the state we are leaving
    return true;
}

void Flow::set_lock(bool on) { _locked = on; }

// Persistence only (§5's power-on paragraph). Deliberately NOT a gesture:
// no begin_blend, no _state change, no push -- restoring the slot a patch
// was saved with must be inaudible, or reloading a patch would move the
// instrument. It is legal before wake() too; wake() then clears it again,
// which is why the documented restore order puts this last.
void Flow::restore_undo(const TerrainState& s, bool have_undo) {
    _undo = s;
    _have_undo = have_undo;
}

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
    // _slew_a is a function of _dt and kSpaceSlewS only -- computed once in
    // init(), not twice per tick (this runs at control rate on the M7).
    _slew_v[slot] += (target - _slew_v[slot]) * _slew_a;
    return _slew_v[slot];
}

// One terrain's weather contribution, per macro, BEFORE the MOTION scaling.
// Each oscillator adds depth * sin into its target macro; the per-macro
// total is the AVERAGE over the oscillators that landed on that macro, not
// the sum -- generate() may aim up to 4 oscs at one macro, and the spec
// bound (|offset| <= kWeatherDepthMax) must hold by construction, not by
// luck.
void Flow::weather_of(const Terrain& t, double ts, float* off) const {
    int cnt[MACRO_COUNT] = {};
    for (int m = 0; m < MACRO_COUNT; ++m) off[m] = 0.f;
    for (int i = 0; i < t.weather_n; ++i) {
        const int m = t.weather_target[i];
        // MOTION itself is never weathered: it is the weather's depth
        // control, and letting the weather modulate its own depth would be
        // exactly the feedback loop the spec excludes. generate() may
        // still aim an osc here; it goes quiet at runtime.
        if (m == M_MOTION) continue;
        off[m] += t.weather_depth[i]
                * fast_sin(float(ts / double(t.weather_period_s[i])));
        ++cnt[m];
    }
    for (int m = 0; m < MACRO_COUNT; ++m)
        if (cnt[m]) off[m] /= float(cnt[m]);
}

// One terrain's story-curve candidate for every param, at the given eff
// macro values. Params no story owns keep the terrain base. When two macros
// curve the same param (BRIGHT and SPACE both own REVMIX_A / REV_DECAY), the
// winner is the candidate FARTHEST from the terrain base (tie -> lower macro
// index, via the strict > below on an ascending walk): deterministic, and
// whichever knob is actually doing something wins, so neither knob ever
// feels dead. NOTE the winner can flip mid-sweep, and a terrain whose two
// candidates straddle the base would make that flip a genuine jump -- the
// blend below must therefore not assume shared targets move smoothly.
void Flow::eval_terrain(const Terrain& t, const float* eff, float* out) const {
    float dist[P_COUNT];
    bool  has[P_COUNT] = {};
    for (int p = 0; p < P_COUNT; ++p) out[p] = t.base[p];
    for (int m = 0; m < MACRO_COUNT; ++m) {
        const MacroMap& mm = t.map[m];
        for (int i = 0; i < mm.n_targets; ++i) {
            const Curve& c = mm.targets[i];
            const float v = eval_curve(c, eff[m]);
            const float d = std::fabs(v - t.base[c.param]);
            if (!has[c.param] || d > dist[c.param]) {
                out[c.param] = v; dist[c.param] = d; has[c.param] = true;
            }
        }
    }
}

// Blend phase at which param p takes its single discrete switch: the texture
// deck goes at the start of the ramp, the carrier deck kCarrierStaggerFrac
// in, so the two decks never jump together (§5). Params that belong to
// neither deck (SCALE, ROOT) ride with the CARRIER, so the tonality change
// lands with the lead voice rather than ahead of it.
float Flow::switch_phase_for(int p) const {
    // P_MODE is the exception: it is a whole-terrain event, not a per-deck
    // one. set_sync is global (instrument.h), so its switch flips BOTH decks'
    // rate mapping at once, and the deckless fall-through below would ride it
    // with the carrier -- jumping the texture deck's clocking 1.5 s after that
    // deck's own duck had closed, which is exactly what the stagger prevents.
    // So it goes at the press, with the texture deck, and begin_blend() opens
    // both ducks for it.
    if (p == P_MODE) return 0.f;
    int d = deck_of(p);
    if (d < 0) d = _carrier_deck;
    return d == _carrier_deck ? kCarrierStaggerFrac : 0.f;
}

// The duck around one deck's discrete switch (§5): that deck's reverb send
// is pushed toward wet, so the engine/scale change happens inside a wash
// instead of in the open. Two properties are load-bearing:
//
//  - It is a MAXIMUM against the macro-computed send. The duck may only add
//    wetness; it must never pull the send below what SPACE and BRIGHT asked
//    for, or the gesture would quietly override the knobs.
//  - The dry leg comes for free. set_reverb_mix is equal-power, so wet up IS
//    dry down -- the same mechanism the BRIGHT story's ember cell uses.
//    There is deliberately no separate gain path.
//
// The envelope is a raised cosine of total width kDuckWindowS centred on the
// switch instant. The texture deck switches at the press itself, so its
// rising half falls before the press and is simply clipped: that deck's duck
// opens at full and returns smoothly, which is what a duck is. (Whether the
// texture deck would rather have its switch delayed by half a window so the
// duck can ramp in is a listening-loop question, not a correctness one.)
// cos(pi*u) == sin(2*pi*(u/2 + 1/4)), and fast_sin takes normalized phase.
//
// A deck can have more than one duck pending (begin_blend: the carrier gets a
// second one at the press when the mode moves). Their windows are 0.5 s wide
// and 1.5 s apart so today they never overlap -- but the combine is a MAXIMUM
// anyway, not a product or a sum: two overlapping ducks must never dig a
// deeper hole than one duck ever makes, or the gesture would exceed the depth
// kDuckDepth was tuned to.
float Flow::duck(int deck, float revmix) const {
    const float half = kDuckWindowS * 0.5f;
    float out = revmix;
    for (int i = 0; i < kDucksPerDeck; ++i) {
        const float u = float(_t - _duck_t[deck][i]) / half;
        if (u <= -1.f || u >= 1.f) continue;
        const float env = 0.5f * (1.f + fast_sin(u * 0.5f + 0.25f));
        const float wet = revmix + (kDuckWetTarget - revmix) * env * kDuckDepth;
        if (wet > out) out = wet;
    }
    return out;
}

// P_MODE + P_STEPS_A/B, pushed as one unit (spec 2026-08-06 §5.3).
// apply_param cannot express this: set_step takes mode and count together, and
// set_sync is global across both parts (instrument.h:274). Issuing them here,
// from _pushed[], means no tick can ever observe steps without a grid.
void Flow::push_mode_and_steps(bool force) {
    if (!_inst) return;
    const bool step = _pushed[P_MODE] > 0.5f;
    const int  sa = int(clamp_to(kParams[P_STEPS_A], _pushed[P_STEPS_A]) + 0.5f);
    const int  sb = int(clamp_to(kParams[P_STEPS_B], _pushed[P_STEPS_B]) + 0.5f);
    if (!force && step == _mode_now && sa == _steps_now[0] && sb == _steps_now[1])
        return;
    _mode_now = step; _steps_now[0] = sa; _steps_now[1] = sb;
    _inst->set_sync(step);
    _inst->set_step(PART_A, step, sa);
    _inst->set_step(PART_B, step, sb);
}

void Flow::recompute_and_push(bool force) {
    const bool  blending = _blend_phase < 1.f;
    const float ph = _blend_phase;

    // (2) Pre-weather macro sums: knob + CV, clamped.
    float s[MACRO_COUNT];
    for (int m = 0; m < MACRO_COUNT; ++m) s[m] = clamp01(_knob[m] + _cv[m]);

    // (3) Weather offsets, crossfaded old -> new on the blend ramp (§5).
    // _terrain_t0 is NOT reset by a press, so the outgoing terrain's weather
    // stays phase-continuous through the transition and the incoming one
    // fades in from zero weight. The whole offset is then scaled by the
    // pre-weather MOTION sum: MOTION is the weather's depth control.
    const double ts = _t - _terrain_t0;
    float off[MACRO_COUNT];
    weather_of(_terrain, ts, off);
    if (blending) {
        float off_prev[MACRO_COUNT];
        weather_of(_prev_terrain, ts, off_prev);
        for (int m = 0; m < MACRO_COUNT; ++m)
            off[m] = off_prev[m] + (off[m] - off_prev[m]) * ph;
    }
    for (int m = 0; m < MACRO_COUNT; ++m)
        _eff[m] = clamp01(s[m] + off[m] * s[M_MOTION]);

    // (4) Story-curve candidates, from BOTH terrains at the SAME live eff.
    float cur[P_COUNT];
    eval_terrain(_terrain, _eff, cur);
    float prv[P_COUNT];
    if (blending) eval_terrain(_prev_terrain, _eff, prv);

    // (5)-(8) Combine / quantize / duck / slew / push. Non-storied params
    // sit at base, so the change guard means they only reach apply_param on
    // wake (force) or when a blend moves them. SIZE/DECAY slew applies to
    // the final combined value -- REV_DECAY may arrive from BRIGHT's ember
    // bloom as well as SPACE's story, and the room drifts either way.
    for (int p = 0; p < P_COUNT; ++p) {
        _cand_cur[p] = cur[p];
        const bool discrete = kParams[p].steps > 0;
        bool fdisc = force;
        float v;
        if (discrete && !_disc_done[p]) {
            // Discretes never lerp. Until its scheduled phase a pending
            // discrete HOLDS the value it is already showing, then takes one
            // step -- forced through the hysteresis guard so the new step
            // lands now rather than after a further half-step of macro
            // travel.
            //
            // "The value it is already showing" is _pushed[p], and reading it
            // rather than prv[p] is the whole point. On a mid-blend re-press
            // prv[p] becomes the terrain we just walked AWAY from, which a
            // pending discrete never reached: feeding it here would yank the
            // carrier deck's engine/scale to a place the instrument was never
            // going to play, outside any duck, and park it there until the
            // new 0.25 switch. Holding instead means a discrete changes at
            // most once per blend, always at its scheduled phase, always
            // under that deck's duck -- so mashing NEW churns the texture
            // while the lead voice simply waits for you to stop. (Feeding
            // _pushed[p] back is a fixed point of quantize_hyst: x lands
            // exactly on _step_now, so the guard holds and the value does not
            // drift. That also makes the hold survive a carrier/texture role
            // swap between presses, which a per-terrain snapshot would not.)
            const bool due = !blending || ph >= switch_phase_for(p);
            v = due ? cur[p] : _pushed[p];
            if (due) { _disc_done[p] = true; fdisc = true; }
        } else if (blending && !discrete) {
            // Clamped: _resid is a constant correction, so a macro moving
            // during the blend can push the sum past the parameter's range
            // even though both terrains' candidates are inside it.
            // param_now() is a public observer (Plan B's display reads it),
            // and the change guard below compares against it -- neither may
            // ever see an out-of-range value just because apply_param would
            // have clamped it later.
            v = clamp_to(kParams[p],
                         prv[p] + (cur[p] - prv[p]) * ph
                                + _resid[p] * (1.f - ph));
        } else {
            v = cur[p];
        }
        _cont_now[p] = v;          // ramp origin for a future re-press:
                                   // pre-quantize, pre-duck, pre-slew

        if (discrete)                  v = quantize_hyst(p, v, fdisc);
        else if (p == P_REVMIX_A)      v = duck(0, v);
        else if (p == P_REVMIX_B)      v = duck(1, v);
        if (p == P_REV_SIZE)           v = space_slew(0, v, force);
        else if (p == P_REV_DECAY)     v = space_slew(1, v, force);
        // The BODY FILT floor, enforced HERE, at runtime -- §4's named hard
        // constraint (terrain.cpp's apply_constraints only makes it true of
        // each terrain in isolation). The blend interpolates P_FILT_A/B
        // between two terrains whose floors were computed under DIFFERENT
        // engine assignments, so a deck currently pushed as ENGINE_BODY can
        // sit on the outgoing terrain's legally un-floored FILT (taste.h's
        // BRIGHT "dawn" story draws bp0 down to -0.55 on purpose: engines
        // other than BODY may dive) for very nearly the whole ramp. Measured
        // without this guard: 188 of 800 deck-blends, worst FILT -0.5485,
        // worst duration 5.99 s of a 6.00 s blend -- and -0.55 costs a BODY
        // deck -13.77 dB against the -0.3 floor.
        //
        // The key is the engine the deck is CURRENTLY PUSHED as, not either
        // terrain's assignment: that is well defined at every tick, including
        // mid-stagger, so the guard is too. Reading _pushed[P_ENGINE_*] gives
        // THIS tick's engine because ENGINE_A/B lead the parameter table and
        // have already been through the loop (static_assert above).
        else if (p == P_FILT_A || p == P_FILT_B) {
            const int ep = (p == P_FILT_A) ? P_ENGINE_A : P_ENGINE_B;
            if (int(_pushed[ep] + 0.5f) == ENGINE_BODY && v < kBodyFiltFloor)
                v = kBodyFiltFloor;
        }

        // The veto band (taste.h kVetos, spec §3), enforced HERE and only
        // here at runtime. The build-time test already proves no table span
        // leaves the band, so a settled terrain cannot breach one. What can:
        // the blend line above clamps to kParams, not to the veto band, and
        // _resid is frozen at press time while prv[] keeps re-evaluating --
        // so a macro moved DURING a ramp can push the sum outside even though
        // both terrains are legal. This runs before the change guard because
        // param_now() is a public observer and must never show a vetoed value.
        for (int vi = 0; vi < kVetoCount; ++vi) {
            if (kVetos[vi].param != p) continue;
            if (v < kVetos[vi].lo) v = kVetos[vi].lo;
            else if (v > kVetos[vi].hi) v = kVetos[vi].hi;
            break;
        }

        // Setter spam guard: push only real changes -- exact compare for
        // discrete (already snapped to the step grid), epsilon for
        // continuous.
        const bool changed = force ||
            (discrete ? v != _pushed[p]
                      : std::fabs(v - _pushed[p]) > 1e-6f);
        if (changed) {
            _pushed[p] = v;
            if (_inst) apply_param(*_inst, p, v);
        }
    }
    // All three of this unit's values are now in _pushed[]; route them as one.
    push_mode_and_steps(force);
}

} } // namespace spky::flow
