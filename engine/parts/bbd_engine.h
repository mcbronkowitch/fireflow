#pragma once
#include <cstddef>
#include <cstdint>
#include "fx/bbd.h"
#include "parts/bbd_music.h"
#include "parts/engine_iface.h"
#include "util/fast_tanh.h"

namespace spky {

// The bucket-brigade delay as a part engine. Two BbdEcho, one per channel: a
// part engine has no dry path -- it IS the signal path -- and its input is
// stereo at every boundary.
//
// MIX is on LANE_LEVEL, not on a knob. Instrument::process composes
// l = al*ga + bl*gb, so the audio input reaches the output nowhere else: on a
// wet/dry engine the mix IS the level, and putting it there lets the plane
// open and close the echo rhythmically -- WITHIN Part::kLevelFloor (part.h),
// which stops modulation ducking LEVEL below 40% of its base: the plane can
// close the echo down to 0.4x base, never all the way to dry. A hand-set
// LEVEL of 0 is unaffected (0.4 * 0 is still 0), so a muted deck stays muted;
// only the MODULATED range has a floor.
class BbdEngine : public IPartEngine {
public:
    // Cells per line. Same sizing as Flux's, which is kMaxStages/2 -- a
    // two-phase BBD stores one sample per TWO stages.
    static constexpr size_t kCells = bbd_tuning::kMaxStages / 2;

    void init(float sample_rate) override;
    // The host owns the memory, as it does for Flux. Two lines per deck at
    // kCells floats = 64 KB, in SDRAM on the Seed. nullptr -> the deck is
    // silent, which is what a host that forgot to allocate deserves.
    void init_buffers(float* l, float* r, size_t cells);

    void set_targets(const float* targets, float tune) override;
    void trigger(float /*pitch_norm*/) override {}
    void process(float& outL, float& outR) override;
    void process_in(float inL, float inR) override;
    bool consumes_input() const override { return true; }
    void set_cycle(float seconds) override;
    // Does NOT re-evaluate _freeze_want: a STEP deck frozen with the gate high
    // and switched to FLOW keeps _freeze_want == 1 for a moment. Bounded to
    // ~5 ms, not indefinite -- Part::gate()'s note_sustain half is
    // _step_mode-qualified, so entering FLOW collapses gate() to
    // _gate_ctr > 0, which expires within Part::_gate_len (240 samples) and
    // fires a gate edge -> Part::set_gate(false) -> _freeze_want = 0. Left
    // implicit until the whole-branch review (2026-07-31); recorded here so
    // the next reader does not have to re-derive it.
    void set_flow(bool flow) override { _flow = flow; if (_flow) _recompute(); }
    bool flow() const { return _flow; }

    // COLOR (spec 5.7): a symmetric geometric clock spread with the delay
    // held on the grid for both lines -- see _apply_width() for the maths and
    // the trade it makes.
    //
    // Calls _apply_width() directly, NOT _recompute() -- deliberately, and it
    // cost a real regression to learn: Part::_control_tick() pushes set_width
    // BEFORE set_targets() in the same tick (both run every tick, but width
    // rides the same early push as set_chord). _recompute() is pitch-aware and
    // consumes the cold-start latch (_latched) the moment it runs; on a deck's
    // very first tick after an engine swap that means it would derive _f_clk
    // from clock_step(_win, _pitch) using whatever _pitch happened to be lying
    // around -- the ctor default, not the real value set_targets() supplies
    // moments later in that same tick -- and burn the arm on it. Measured: it
    // broke "a BBD deck swapped into an already-running STEP transport
    // reflects PITCH before its first fire" (both PITCH probes collapsed to
    // the clock at PITCH 0.5). _apply_width() only reads the already-settled
    // _f_clk/_win, so it cannot reach _pitch or _latched at all -- the same
    // reasoning _refresh_window() already rests on for the same reason.
    void set_width(float n) override { _width = clampf(n, 0.f, 1.f); _apply_width(); }
    // Observers: the two lines' actual clock and stage count, as split by
    // COLOR. clock_hz()/stages() above stay the un-spread CENTRE -- the value
    // COLOR spreads away from -- so existing callers of those two are
    // unaffected by this task.
    float clock_l() const { return _f_l; }
    float clock_r() const { return _f_r; }
    int   stages_l() const { return _st_l; }
    int   stages_r() const { return _st_r; }

    // The freeze (spec 5.6). Gate high in STEP mutes the input and holds the
    // loop at k0, so the content keeps circulating and stays audible.
    void set_gate(bool on) override;
    // CHOKE: closes the input and lets the tail run out. Unlike the gate this
    // is honoured in both modes -- it is not a freeze, it is a mute.
    void set_hold(bool on) override;
    // ATTACK -> the freeze's engage/release time; DECAY -> a trim BELOW k0.
    void set_attack(float n);
    void set_decay(float n);
    // RESONANCE -> the feedback-path tilt: how bright the repeats stay. Plays
    // the SAME filter the freeze needs (spec 5.6/5.8) -- see _apply_freeze()/
    // _push_freeze(), where _res_tilt is the crossfade's UNFROZEN endpoint.
    void set_resonance(float n);
    // SUB -> the input level: how much neighbour/audio-in actually reaches
    // the line, applied in process_in() so both the wet and dry paths agree
    // about how much signal arrived.
    void set_sub(float n);
    // DETUNE (menu-only) -> the slew time the clock chases a moved lane at.
    // Stores the per-sample coefficient only -- see process()'s geometric
    // glide, which is the one place that coefficient is spent.
    void set_detune(float n);
    // FILT -> the loss-pole corner, NOT kFilterHz (spec 5.8: kFilterHz is
    // constexpr, baked into butterworth_poles(), and shared by every line via
    // two file-scope singletons -- see the .cpp for the full argument).
    void set_filt(float t);
    // EDGE, bipolar, 0 == this engine's own neutral (spec 2026-08-19
    // voice-knobs-dpth-edge, 4.2).
    //
    // STUB. It stores the trim and does nothing else, so EDGE is silently
    // DEAD on a BBD deck. TASK 7 of that plan replaces it with the
    // pre-emphasis one-pole ahead of the line (spec 4.5 for why it is
    // pre-emphasis and not kFilterHz).
    void set_edge(float t) { _edge = clampf(t, -1.f, 1.f); }
    // Whether the freeze is ENGAGED, not whether its ramp has finished: the
    // gate's own state, so a caller (and the FLOW rule's test) can read the
    // decision without first running the ramp out. _freeze_want is only ever
    // 0 or 1.
    bool frozen() const { return _freeze_want > 0.5f; }
    // Observer: the ATTACK time actually in force, in seconds.
    float freeze_ramp_s() const { return _freeze_ramp_s; }
    // Observer: the crossfade ITSELF, 0 = input open / feedback at the lane,
    // 1 = input closed / feedback at k0. Distinct from frozen(), which reports
    // the gate's decision and therefore cannot witness the ramp at all.
    // Exposed because "the ramp lands on exactly 1.0" is an arithmetic claim
    // about this variable -- it is the whole reason the ramp is linear rather
    // than a one-pole -- and a claim about a variable is honestly tested by
    // reading it.
    float freeze_amount() const { return _freeze; }
    // A step fire latches the clock and holds it until the next one -- the
    // pattern SynthEngine already uses for pitch. In FLOW the engine ignores
    // fires and follows the plane continuously: lane.cpp:447-452 makes a FLOW
    // deck fire once per master-lane cycle un-gated, so latching there would
    // freeze the clock at the top of every cycle.
    void latch_clock() { if (!_flow) { _latched = true; _recompute(); } }

    // Clears both lines, both companders and both feedback states. Part has no
    // swap-away notification, so this is called on activation instead.
    void reset();

    // Observers (spec 9): a clamp that is invisible reads as a broken knob.
    float clock_hz() const { return _f_clk; }
    int   stages() const { return _stages; }
    int   div_index() const { return _ladder.index(); }
    bool  time_clamped() const { return _win.time_clamped; }
    bool  scale_truncated() const { return _win.scale_truncated; }

    // Task 8's VOICE-row observers. clock_hz()/stages() above stay the
    // un-spread TARGET the lane asks for; the four below read back what each
    // new knob actually pushed.
    //
    // FILT: the loss-pole coefficient in force. Centre (FILT 0) reads exactly
    // bbd_tuning::kLossCoef -- the knob's neutral position, provable because a
    // knob left alone must change nothing.
    float loss_coef() const { return _loss_a; }
    // RESONANCE: the feedback-path tilt AT REST (freeze_amount() == 0). 0 at
    // the knob's centre; see _apply_freeze()/_push_freeze() for how the
    // freeze crossfades away from it.
    float resonance_tilt() const { return _res_tilt; }
    // SUB: the gain applied to the audio actually reaching the line.
    float input_gain() const { return _in_gain; }
    // DETUNE (menu): the slew time, in seconds.
    float slew_seconds() const { return _slew_s; }
    // DECAY, read back as the 0..1 the caller last set it to -- distinct from
    // freeze_ramp_s() (ATTACK, already exposed by the freeze task), which this
    // extends rather than duplicates.
    float decay_norm() const { return _decay; }
    // The clock the line is actually running at, as opposed to the one the
    // lane is asking for. Before the slew existed the two were the same
    // number; they are not any more, and every test about how the engine
    // ANSWERS modulation has to read this one, not clock_hz().
    float clock_now() const { return _f_now; }

private:
    // Pitch-aware: derives _f_clk from _pitch/_flow/_latched (STEP/FLOW rule),
    // then always refreshes _win/_stages from whatever _f_clk ends up being.
    // Called from set_targets(), latch_clock() and set_flow() -- the three
    // places _pitch, a fire or the mode itself can actually change.
    void _recompute();
    // Structural-only half of the above: refreshes _win/_stages from the
    // CURRENT _f_clk, without touching _pitch, _flow or _latched. Called from
    // init()/init_buffers()/set_cycle(), none of which carry a real PITCH
    // value -- consuming the cold-start arm (_latched) here would burn it
    // against whatever _pitch happens to be lying around (the ctor default,
    // or a stale value left by a previous activation) before the engine's own
    // set_targets() ever gets a chance to supply the real one. Concretely:
    // Part::_engine_swap() calls reset() (which re-arms _latched) and THEN,
    // when the master lane's rate is already established, set_cycle() --
    // still before the freshly swapped-in engine's first set_targets(). If
    // set_cycle() consumed the arm, a BBD deck swapped into an already-active
    // STEP transport would latch onto stale/default pitch instead of the
    // deck's actual current one.
    //
    // It also deliberately does NOT call _apply_freeze(): _f_clk cannot move
    // here, so the tilt corner it would push is the one already in force.
    void _refresh_window();
    // COLOR's spread (spec 5.7): given the CURRENT _win and _f_clk, derives
    // the two lines' clocks and stage counts and pushes the latter into
    // _l/_r. Control rate only (one std::pow) -- called from both
    // _recompute() and _refresh_window(), i.e. every path that can move
    // either _f_clk or _win, so the two lines' actual SetStages() never lags
    // behind a cycle/SIZE change the way a bare _recompute()-only call would.
    void _apply_width();
    // The freeze's two-speed push, split by what it costs.
    //
    // _apply_freeze() is CONTROL RATE and owns both libm calls: the tilt's
    // corner (an exp, inside SetFeedbackTilt) and the DRIVE division (a pow,
    // inside bbd_drive_gain). It caches the latter in _freeze_k and ends by
    // calling _push_freeze(). It must be reached from every path that can move
    // _f_clk, _drive, _fb_lane or _decay -- _recompute() is the one that owns
    // _f_clk, and _refresh_window() deliberately is not, since it never
    // touches the clock -- and from reset(), which otherwise leaves the two
    // lines holding a frozen loop's coefficients with the input gate reopened.
    //
    // _push_freeze() is PER SAMPLE and does the rest with arithmetic only.
    // engine/** may not call libm in the audio path, so the split is a
    // constraint, not an optimisation.
    void _apply_freeze();
    void _push_freeze();

    BbdEcho _l, _r;
    bbd_music::DivLadder _ladder;
    bbd_music::Window _win;
    float _sr = 48000.f;
    float _cycle = 1.f;
    float _in_l = 0.f, _in_r = 0.f;
    float _mix = 0.f;
    float _pitch = 0.5f;
    float _f_clk = 4000.f;
    int   _stages = 8192;
    // COLOR's width, and the two lines it produces. _width is the clamped
    // knob; _f_l/_f_r and _st_l/_st_r are _apply_width()'s output, and what
    // process() and BbdEcho::SetStages() actually use -- _f_clk/_stages above
    // are deliberately left as the un-spread centre, for the observers that
    // predate this task.
    float _width = 0.f;
    float _f_l = 4000.f;
    float _f_r = 4000.f;
    int   _st_l = 8192;
    int   _st_r = 8192;
    // Held rather than pushed straight down, because the freeze crossfades
    // AWAY from them: _apply_freeze() is the only writer of the two lines'
    // feedback coefficient, and it needs both the lane's value and the DRIVE
    // it has to divide back out.
    float _drive = 0.f;
    float _fb_lane = 0.f;
    // DECAY: a trim below k0, so the freeze runs out instead of holding
    // forever. 1 is "no trim", which is the operating point k0 is measured at.
    float _decay = 1.f;
    // FILT: the loss-pole coefficient actually pushed into both lines.
    // Defaults to kLossCoef -- BbdLine's own ctor default already matches it,
    // so an engine never touched by set_filt() behaves exactly as it did
    // before this task.
    float _loss_a = bbd_tuning::kLossCoef;
    // EDGE knob -1..+1 (boot: neutral). Stored and unread -- see set_edge().
    float _edge = 0.f;
    // RESONANCE: the feedback-path tilt at freeze_amount() == 0. 0 at the
    // knob's centre, so an engine never touched by set_resonance() is
    // bit-exact through the tilt at rest, same as before this task.
    float _res_tilt = 0.f;
    // SUB: the gain on the audio actually reaching the line (process_in()).
    // Defaults to 1 (unity): a BbdEngine driven directly, without Part's
    // set_sub() forward, behaves exactly as it did before this task.
    float _in_gain = 1.f;
    // DETUNE: the slew time and its per-sample multiplicative step (process()'s
    // constant-RATE glide -- see set_detune()'s comment for why this replaced
    // an exponential-in-Hz approach that only looked geometric). _slew_s is
    // redefined as the time to cross ONE OCTAVE; _slew_mul is the per-sample
    // factor whose (_slew_s * _sr)-th power is exactly 2, so multiplying (or
    // dividing) by it every sample crosses equal ratios in equal time, exactly,
    // by construction. Kept apart because the glide needs the multiplier every
    // sample and the seconds figure is only for the observer. The literal
    // default here is 2^(1/(0.001*48000)) -- kSlewMinS's shape at the default
    // 48 kHz, same idiom as _freeze_step's default below; init() recomputes it
    // for the real _sr.
    float _slew_s = 0.001f;
    float _slew_mul = 1.014545f;
    // The clock each line is ACTUALLY running at, slewed toward _f_l/_f_r --
    // see process(). Initialised to _f_clk in init(), never to zero: a zero
    // would make the geometric glide's ratio non-finite.
    float _f_now = 4000.f;
    float _f_now_r = 4000.f;
    // The freeze crossfade: _freeze_want is the gate's decision (0 or 1),
    // _freeze the ramped value, _freeze_last the value last pushed into the
    // lines. _freeze_ramp_s is ATTACK in seconds and _freeze_step the linear
    // per-sample travel it implies; the latter is re-derived in init() and in
    // set_attack(), both of which know the real sample rate.
    // _freeze_last is the value the two lines were last told about.
    float _freeze = 0.f;
    float _freeze_want = 0.f;
    float _freeze_last = 0.f;
    float _freeze_ramp_s = 0.002f;
    float _freeze_step = 1.f / (0.002f * 48000.f);
    // kFreezeGain * _decay / bbd_drive_gain(_drive), cached by _apply_freeze()
    // so the per-sample crossfade never re-evaluates that pow().
    float _freeze_k = 0.f;
    bool  _choked = false;
    bool  _buf_ok = false;
    bool  _flow = false;
    // Armed at construction and by every reset() (Part::_engine_swap runs
    // reset() on every activation): a STEP deck that has never fired must
    // still show a clock derived from PITCH, not the raw ctor literal above,
    // the moment its first real set_targets() lands -- see _recompute().
    bool  _latched = true;
};

}  // namespace spky
