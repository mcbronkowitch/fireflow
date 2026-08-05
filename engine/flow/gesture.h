// engine/flow/gesture.h
//
// The gesture decoder (spec §5): turns raw NEW-button and macro-knob events
// into exactly one of the four ops Flow's NEW family understands (new_full /
// new_partial(mask) / undo / set_lock's toggle, plus REFUSED for feedback).
// Pure -- it decides, it never calls Flow. The host polls poll() after each
// event and drives Flow's verbs itself; Plan B's VCV module wires a real
// button and six knobs to precisely this interface.
//
// No heap, no host headers: gesture.h must never include flow.h, and nothing
// here may touch host/vcv/. Time is handed in as double now_s from the
// caller -- the decoder owns no clock of its own, only the last value it has
// seen (needed for the refusal LED's flash window, since led() takes no
// now_s of its own).
#pragma once
#include <cstdint>
#include "flow/flow_ids.h"
#include "flow/taste.h"

namespace spky { namespace flow {

struct GestureOut {
    enum Op { NONE, NEW_FULL, NEW_PARTIAL, UNDO, LOCK_TOGGLE, REFUSED } op = NONE;
    uint8_t mask = 0;                 // NEW_PARTIAL: macro bitmask
};

class Gesture {
public:
    // Feed events; poll() after each to collect an op (at most one).
    void button(bool down, double now_s, bool locked) {
        _now = now_s;
        if (down) {
            _held = true;
            _press_t = now_s;
            _press_locked = locked;
            _marked = false;
            _marked_mask = 0;
            for (float& a : _mark_accum) a = 0.f;
            _undo_armed = false;
            _lock_fired = false;
            return;
        }
        if (!_held) return;           // no matching press: ignore (defensive)
        _held = false;
        if (_lock_fired) return;      // already delivered mid-hold; release
                                       // adds nothing -- and must NOT clobber
                                       // an unpolled LOCK_TOGGLE still queued.
        const double dur = now_s - _press_t;
        GestureOut::Op op; uint8_t mask = 0;
        if (_press_locked) {
            // Rule 6: while locked, every press but the clean 5 s unlock
            // hold (which took the _lock_fired branch above) refuses --
            // tap, dead band, marked hold, and a clean hold that never
            // reached kLockS all land here.
            op = GestureOut::REFUSED;
            _refuse_t = now_s;
        } else if (_marked) {
            op = GestureOut::NEW_PARTIAL;
            mask = _marked_mask;
        } else if (dur < kTapMaxS) {
            op = GestureOut::NEW_FULL;
        } else if (_undo_armed) {
            op = GestureOut::UNDO;
        } else {
            op = GestureOut::NONE;    // the dead band: >= kTapMaxS, < kUndoArmS
        }
        _pending.op = op;
        _pending.mask = mask;
    }

    // Physical travel while held; ignored entirely while the button is up.
    void knob_delta(int macro, float delta, double now_s) {
        _now = now_s;
        if (!_held || _lock_fired) return;   // inert once resolved this hold
        if (macro < 0 || macro >= MACRO_COUNT) return;
        _mark_accum[macro] += delta < 0.f ? -delta : delta;
        if (_mark_accum[macro] >= kMarkDelta) {
            // Rule 5: turning a knob wins, permanently for this hold --
            // cancels armed undo now, and (via the _marked guard in tick())
            // the lock timer for the rest of the hold too.
            _marked = true;
            _marked_mask = uint8_t(_marked_mask | (1u << macro));
            _undo_armed = false;
        }
    }

    // For LED state only; also advances the undo-arm/lock-fire timers.
    //
    // can_undo is Flow::can_undo(), handed in the same way `locked` is -- the
    // decoder stays pure and never learns what a Flow is. It is a parameter
    // rather than something this class could infer because only Flow knows
    // whether the one undo slot is occupied.
    void tick(double now_s, bool can_undo) {
        _now = now_s;
        if (!_held || _marked || _lock_fired) return;
        const double dur = now_s - _press_t;
        if (dur >= kLockS) {
            // Fires immediately (not on release) whether this hold started
            // locked (an unlock) or not (a lock) -- same op either way.
            _lock_fired = true;
            _pending.op = GestureOut::LOCK_TOGGLE;
            _pending.mask = 0;
        } else if (dur >= kUndoArmS && !_press_locked && can_undo) {
            // Undo never arms under a locked press: that hold can only ever
            // end in LOCK_TOGGLE (if it reaches kLockS) or REFUSED, so an
            // "armed" state here would just be a misleading LED.
            //
            // Same reasoning for can_undo, which is why it is a condition and
            // not just an LED filter: on a freshly-woken instrument there is
            // nothing to undo, so a hold past kUndoArmS used to double-pulse
            // "undo armed" and then do nothing at all on release. Now it never
            // arms, the LED never claims it did, and the release falls into
            // the dead band (NONE) exactly like a hold that ended too early.
            _undo_armed = true;
        }
    }

    GestureOut poll() {
        GestureOut out = _pending;
        _pending = GestureOut{};
        return out;
    }

    // LED signature for the host to render (spec §5 table + lock blink).
    enum Led { LED_IDLE, LED_BLEND, LED_MARKED, LED_UNDO_ARMED,
               LED_LOCKED, LED_REFUSE };

    Led led(float blend_phase, bool locked) const {
        // Precedence, highest first: REFUSE (transient feedback) >
        // UNDO_ARMED > MARKED (mutually exclusive with UNDO_ARMED by rule 5,
        // ordered anyway so the function is total) > BLEND > LOCKED > IDLE.
        // LED_UNDO_ARMED is reachable only when tick() was told an undo
        // exists, so the light cannot promise an op that would be refused.
        if (_now - _refuse_t < kRefuseFlashS) return LED_REFUSE;
        if (_held && _undo_armed) return LED_UNDO_ARMED;
        if (_held && _marked) return LED_MARKED;
        if (blend_phase < 1.f) return LED_BLEND;
        if (locked) return LED_LOCKED;
        return LED_IDLE;
    }

private:
    bool   _held = false;
    double _press_t = 0.0;
    bool   _press_locked = false;
    bool   _marked = false;
    uint8_t _marked_mask = 0;
    float  _mark_accum[MACRO_COUNT] = {};
    bool   _undo_armed = false;
    bool   _lock_fired = false;

    double _now = 0.0;
    // Last REFUSED release, for the flash window. Kept far enough in the
    // past that a fresh Gesture (both fields still at their defaults) does
    // NOT read as "just refused" -- _now and _refuse_t must never start
    // equal, or led() reports LED_REFUSE before any event ever arrives.
    double _refuse_t = -1e18;
    GestureOut _pending;
};

} } // namespace spky::flow
