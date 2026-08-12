// tests/test_flow_gesture.cpp
//
// The gesture decoder (spec §5): raw NEW-button + macro-knob events -> the
// four ops Flow's NEW family understands. Pure -- no Flow, no host headers.
#include "doctest/doctest.h"
#include "flow/gesture.h"
#include "flow/taste.h"
using namespace spky::flow;

static GestureOut run_press(Gesture& g, double t0, double dt,
                            int mark_macro = -1, double mark_at = 0.0,
                            bool locked = false, bool can_undo = true) {
    g.button(true, t0, locked);
    if (mark_macro >= 0) g.knob_delta(mark_macro, 0.05f, t0 + mark_at);
    g.tick(t0 + dt, can_undo);
    g.button(false, t0 + dt, locked);
    return g.poll();
}

TEST_CASE("flow gesture: the §5 table") {
    Gesture g;
    CHECK(run_press(g, 0.0, 0.2).op == GestureOut::NEW_FULL);          // tap
    auto pr = run_press(g, 10.0, 2.0, M_BRIGHT, 0.5);                   // mark
    CHECK(pr.op == GestureOut::NEW_PARTIAL);
    CHECK(pr.mask == (1u << M_BRIGHT));
    CHECK(run_press(g, 20.0, 2.0).op == GestureOut::UNDO);             // hold
    // Mark AFTER undo armed: mark wins, undo cancelled.
    auto late = run_press(g, 30.0, 3.0, M_SPACE, 2.5);
    CHECK(late.op == GestureOut::NEW_PARTIAL);
    // Clean 5 s hold: LOCK fires during the hold, release adds nothing.
    g.button(true, 40.0, false);
    g.tick(40.0 + kLockS + 0.1, true);
    CHECK(g.poll().op == GestureOut::LOCK_TOGGLE);
    g.button(false, 46.0, true);
    CHECK(g.poll().op == GestureOut::NONE);
    // While locked: tap refuses.
    CHECK(run_press(g, 50.0, 0.2, -1, 0.0, true).op == GestureOut::REFUSED);
    // Marked hold past 5 s: NO lock (knob turned) - partial on release.
    auto held = run_press(g, 60.0, 6.0, M_PACE, 0.5);
    CHECK(held.op == GestureOut::NEW_PARTIAL);
}

TEST_CASE("flow gesture: the dead band between tap and undo-arm is NONE") {
    // Resolution #1: a release at or after kTapMaxS but before kUndoArmS is
    // neither a tap nor a hold -- it must do nothing, not fall through to
    // either neighbor.
    Gesture g;
    CHECK(run_press(g, 0.0, kTapMaxS).op == GestureOut::NONE);          // boundary: at kTapMaxS, not a tap
    CHECK(run_press(g, 10.0, 0.7).op == GestureOut::NONE);              // mid dead band
    CHECK(run_press(g, 20.0, kUndoArmS - 0.01).op == GestureOut::NONE); // just short of arming
    CHECK(run_press(g, 30.0, kUndoArmS).op == GestureOut::UNDO);        // boundary: arms right at kUndoArmS
}

TEST_CASE("flow gesture: locked marked hold past kLockS still refuses") {
    // Resolution #6: a knob turn during an unlock hold cancels the lock
    // timer like any other hold, so that press ends as REFUSED -- not
    // LOCK_TOGGLE, and not NEW_PARTIAL (marks never fire while locked).
    Gesture g;
    auto out = run_press(g, 0.0, kLockS + 1.0, M_MOTION, 0.5, /*locked=*/true);
    CHECK(out.op == GestureOut::REFUSED);
    CHECK(out.mask == 0);
}

TEST_CASE("flow gesture: a mark AFTER undo is already armed (via tick) still cancels it") {
    // The brief's run_press() helper always calls knob_delta() BEFORE
    // tick(), so its "mark after undo armed" case never actually exercises
    // arming undo first and marking second. Do that ordering explicitly:
    // undo_armed becomes true via tick(), THEN a knob turn arrives -- rule 5
    // says the mark must still cancel it, however long the hold runs after.
    Gesture g;
    g.button(true, 0.0, false);
    g.tick(kUndoArmS + 0.1, true);        // undo arms
    CHECK(g.led(1.f, false) == Gesture::LED_UNDO_ARMED);
    g.knob_delta(M_WANDER, 0.05f, kUndoArmS + 0.2);   // then it gets marked
    // The cancellation is not just about the eventual op -- the LED must
    // stop claiming undo is armed the instant the mark lands, still mid-hold.
    CHECK(g.led(1.f, false) == Gesture::LED_MARKED);
    g.tick(kUndoArmS + 0.3, true);
    g.button(false, kUndoArmS + 0.3, false);
    auto out = g.poll();
    CHECK(out.op == GestureOut::NEW_PARTIAL);
    CHECK(out.mask == (1u << M_WANDER));
}

TEST_CASE("flow gesture: poll() consumes -- a second poll with no new event is NONE") {
    Gesture g;
    g.button(true, 0.0, false);
    g.button(false, 0.2, false);
    CHECK(g.poll().op == GestureOut::NEW_FULL);
    CHECK(g.poll().op == GestureOut::NONE);
    CHECK(g.poll().op == GestureOut::NONE);
}

TEST_CASE("flow gesture: led() covers every value it can produce") {
    SUBCASE("idle: nothing pending, settled, unlocked") {
        Gesture g;
        CHECK(g.led(1.f, false) == Gesture::LED_IDLE);
    }
    SUBCASE("blend: a ramp still running, no hold, unlocked") {
        Gesture g;
        CHECK(g.led(0.5f, false) == Gesture::LED_BLEND);
    }
    SUBCASE("locked: settled, no hold, locked") {
        Gesture g;
        CHECK(g.led(1.f, true) == Gesture::LED_LOCKED);
    }
    SUBCASE("marked: held with a mark, before release") {
        Gesture g;
        g.button(true, 0.0, false);
        g.knob_delta(M_PACE, 0.05f, 0.1);
        CHECK(g.led(1.f, false) == Gesture::LED_MARKED);
    }
    SUBCASE("undo_armed: held past kUndoArmS clean, before release") {
        Gesture g;
        g.button(true, 0.0, false);
        g.tick(kUndoArmS + 0.1, true);
        CHECK(g.led(1.f, false) == Gesture::LED_UNDO_ARMED);
    }
    SUBCASE("refuse: right after a REFUSED release, then it expires") {
        Gesture g;
        g.button(true, 0.0, true);
        g.button(false, 0.1, true);
        CHECK(g.poll().op == GestureOut::REFUSED);
        CHECK(g.led(1.f, true) == Gesture::LED_REFUSE);
        g.tick(0.1 + kRefuseFlashS + 0.01, true);
        CHECK(g.led(1.f, true) == Gesture::LED_LOCKED);
    }
}

TEST_CASE("flow gesture: LED precedence -- refuse and undo_armed beat blend/locked") {
    // Rule 3's ordering must actually be load-bearing, not just documented.
    Gesture g;
    g.button(true, 0.0, true);
    g.button(false, 0.1, true);
    g.poll();
    // Even though blend_phase < 1 and locked == true would otherwise pick
    // BLEND or LOCKED, the fresh refusal must win.
    CHECK(g.led(0.3f, true) == Gesture::LED_REFUSE);

    Gesture h;
    h.button(true, 0.0, false);
    h.tick(kUndoArmS + 0.1, true);
    // undo_armed must beat a concurrently-running blend.
    CHECK(h.led(0.3f, false) == Gesture::LED_UNDO_ARMED);
}

TEST_CASE("flow gesture: undo does not arm when there is nothing to undo") {
    // The LED must not promise an op that Flow would refuse. On a freshly
    // woken instrument the single undo slot is empty, so a hold past
    // kUndoArmS used to light LED_UNDO_ARMED and then do nothing at all on
    // release -- with no refusal blink either, because the decoder had no way
    // to know. can_undo is handed to tick() the way `locked` is handed to
    // button(): the decoder stays pure and never learns what a Flow is.
    Gesture g;
    g.button(true, 0.0, false);
    g.tick(kUndoArmS + 0.1, /*can_undo=*/false);
    CHECK(g.led(1.f, false) == Gesture::LED_IDLE);   // NOT LED_UNDO_ARMED
    g.tick(kUndoArmS + 0.2, false);
    g.button(false, kUndoArmS + 0.2, false);
    CHECK(g.poll().op == GestureOut::NONE);          // dead band, not UNDO

    // ...and the identical press with a slot to return to still arms and
    // still delivers UNDO, so the two CHECKs above are the flag doing the
    // work, not the timing.
    Gesture h;
    h.button(true, 0.0, false);
    h.tick(kUndoArmS + 0.1, /*can_undo=*/true);
    CHECK(h.led(1.f, false) == Gesture::LED_UNDO_ARMED);
    h.tick(kUndoArmS + 0.2, true);
    h.button(false, kUndoArmS + 0.2, false);
    CHECK(h.poll().op == GestureOut::UNDO);
}
