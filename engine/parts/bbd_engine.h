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
// wet/dry engine the mix IS the level, and putting it there lets the plane open
// and close the echo rhythmically.
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
    void set_flow(bool flow) override { _flow = flow; if (_flow) _recompute(); }
    bool flow() const { return _flow; }

    // The freeze (spec 5.6). Gate high in STEP mutes the input and holds the
    // loop at k0, so the content keeps circulating and stays audible.
    void set_gate(bool on) override;
    // CHOKE: closes the input and lets the tail run out. Unlike the gate this
    // is honoured in both modes -- it is not a freeze, it is a mute.
    void set_hold(bool on) override;
    // ATTACK -> the freeze's engage/release time; DECAY -> a trim BELOW k0.
    void set_attack(float n);
    void set_decay(float n);
    // Whether the freeze is ENGAGED, not whether its ramp has finished: the
    // gate's own state, so a caller (and the FLOW rule's test) can read the
    // decision without first running the ramp out. _freeze_want is only ever
    // 0 or 1.
    bool frozen() const { return _freeze_want > 0.5f; }
    // Observer: the ATTACK time actually in force, in seconds.
    float freeze_ramp_s() const { return _freeze_ramp_s; }
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
    // Pushes the three legs of the freeze -- DC blocker, feedback-path tilt
    // (corner at _f_clk/4) and the loop gain with DRIVE divided out -- for
    // whatever _freeze currently is. Control rate: it reaches SetFeedbackTilt,
    // whose corner costs an exp(). Must be reached from every path that can
    // move _f_clk, _drive, _fb_lane, _decay or _freeze; _recompute() is the
    // one that owns _f_clk, and _refresh_window() deliberately is not (it
    // never touches the clock).
    void _apply_freeze();

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
    // Held rather than pushed straight down, because the freeze crossfades
    // AWAY from them: _apply_freeze() is the only writer of the two lines'
    // feedback coefficient, and it needs both the lane's value and the DRIVE
    // it has to divide back out.
    float _drive = 0.f;
    float _fb_lane = 0.f;
    // DECAY: a trim below k0, so the freeze runs out instead of holding
    // forever. 1 is "no trim", which is the operating point k0 is measured at.
    float _decay = 1.f;
    // The freeze crossfade: _freeze_want is the gate's decision (0 or 1),
    // _freeze the ramped value, _freeze_last the value last pushed into the
    // lines. _freeze_ramp_s is ATTACK in seconds and _freeze_step the linear
    // per-sample travel it implies; the latter is re-derived in init() and in
    // set_attack(), both of which know the real sample rate.
    // _freeze_last is what the lines were last told -- see kFreezePushStep.
    float _freeze = 0.f;
    float _freeze_want = 0.f;
    float _freeze_last = 0.f;
    float _freeze_ramp_s = 0.002f;
    float _freeze_step = 1.f / (0.002f * 48000.f);
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
