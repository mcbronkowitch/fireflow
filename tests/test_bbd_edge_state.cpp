#include <doctest/doctest.h>

#include "vcv/src/bbd_edge_state.hpp"


// Covers all three restore paths a review round found broken in turn (spec
// 2026-07-31 bbd-part-engine, Task 10): a genuine player-driven transition
// must still fire; a fresh module/whole-patch-open/duplicate landing already
// on BBD must not; and an already-live preset Load/paste landing on BBD must
// not either, once rearm() is called from dataFromJson()'s curSr>0.f branch.

TEST_CASE("BbdEdgeState: genuine player transition into BBD fires exactly once")
{
    spkyvcv::BbdEdgeState edge;

    CHECK(edge.tick(false) == false);   // ENG=Synth
    CHECK(edge.tick(false) == false);   // ENG=Synth, still
    CHECK(edge.tick(true)  == true);    // player flips to BBD -- must fire
    CHECK(edge.tick(true)  == false);   // still BBD -- must NOT re-fire
}

TEST_CASE("BbdEdgeState: leaving BBD and genuinely returning fires again")
{
    spkyvcv::BbdEdgeState edge;

    CHECK(edge.tick(false) == false);   // baseline (first-ever call): ENG=Synth
    CHECK(edge.tick(true)  == true);    // player enters BBD -- must fire
    CHECK(edge.tick(false) == false);   // player leaves BBD
    CHECK(edge.tick(true)  == true);    // a real second entry -- must fire again
}

TEST_CASE("BbdEdgeState: fresh construction already on BBD never fires (fresh add / "
          "whole-patch open / Ctrl+D duplicate)")
{
    // Models dataFromJson() restoring a freshly-constructed Module whose ENG
    // was already 4 when the JSON landed -- the exact case Critical 2's
    // first fix round addressed. The very first tick() call establishes the
    // baseline instead of a transition.
    spkyvcv::BbdEdgeState edge;

    CHECK(edge.tick(true) == false);    // first-ever observation: baseline, not a fire
    CHECK(edge.tick(true) == false);    // still BBD
}

TEST_CASE("BbdEdgeState: already-live restore (preset Load/paste) landing on BBD "
          "does not fire, but a later genuine transition still does")
{
    // Models the case Critical 2's SECOND fix round addressed: a module
    // already live and ticking on a non-BBD engine receives dataFromJson()
    // on an already-live instance (Spotymod's curSr>0.f branch -- right-click
    // Load preset / module paste), landing ENG on BBD. Without rearm(), the
    // instance is already seeded from before the restore and the stale
    // wasBbd==false would make this look like a transition and fire.
    spkyvcv::BbdEdgeState edge;

    CHECK(edge.tick(false) == false);   // live module, ENG=Synth
    CHECK(edge.tick(false) == false);   // still Synth

    edge.rearm();                        // dataFromJson()'s curSr>0.f branch
    // The JSON just set ENG=BBD on the param the caller reads; the next
    // tick() must treat that as a baseline, not a transition, because it
    // matches what the preset saved (including whatever FLUX/exciteOtherDeck
    // that preset saved -- BbdEdgeState itself doesn't own those, but a
    // false return here is what keeps the caller from touching them).
    CHECK(edge.tick(true) == false);    // post-restore: must NOT fire
    CHECK(edge.tick(true) == false);    // still BBD

    // A later GENUINE player-driven leave-and-return must still work after
    // a rearm() -- the fix must not have disabled real edge detection.
    CHECK(edge.tick(false) == false);   // player leaves BBD
    CHECK(edge.tick(true)  == true);    // player returns -- must fire
}
