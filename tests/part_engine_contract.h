#pragma once
#include <doctest/doctest.h>
#include "parts/engine_iface.h"
#include <algorithm>
#include <cmath>

namespace spky {

// The contract every part engine owes, voiced or not. tests/synth_engine_
// contract.h covers SynthEngineT specifically -- voices, envelopes, note
// allocation -- and a voiceless input-consuming engine cannot satisfy it.
// This is the part that is genuinely universal.
//
// Why each of the three is universal rather than BBD-specific:
//
// 1. Silence in must not grow, and the output must be finite forever. Every
//    engine in this repo has state that decays (envelopes, filter poles,
//    delay lines, resonator modes); "silence in stays bounded" is the one
//    claim that does not depend on WHAT that state is. It is also the claim
//    that catches the two failures this repo has actually shipped: a loop
//    whose gain crept above unity, and a denormal-parked state that never
//    reached zero. Note the loop runs the engine WITHOUT ever calling
//    set_targets(): a construction-time default that blooms is exactly as
//    much of a defect as a knob position that does.
// 2. consumes_input() and process_in() are overridden TOGETHER. Nothing in
//    the language enforces this and the failure is silent: an engine that
//    implements process_in and forgets the flag never hears its input,
//    because Part caches the flag once per swap and skips the call
//    (engine_iface.h:78-85, part.h). The check is written so an engine that
//    does not consume input passes vacuously -- which is correct, since for
//    such an engine there is nothing to pair. CALLER'S RESPONSIBILITY: the
//    setup must leave the engine in a state where process_in's effect is
//    OBSERVABLE at process(). For BbdEngine that is automatic (the input is
//    always in the signal path); SamplerEngine, the only other engine that
//    consumes input today, would need set_monitor(true) or recording armed,
//    because otherwise process_in only writes to the record buffer and the
//    two runs really are identical for a reason that is not a defect.
// 3. Every no-op setter is safe to call in any order, at any time. The
//    IPartEngine base declares set_flow/set_gate/set_hold/set_cycle/set_width
//    as defaulted no-ops, so an engine that overrides one inherits no ordering
//    contract at all -- and Part does not push them in a fixed order relative
//    to set_targets() (part.cpp:303 pushes set_width BEFORE set_targets in the
//    same tick; part.cpp:425-427 pushes hold/gate/cycle on an engine swap
//    before any target has arrived). A host CAN and DOES push absurd values:
//    set_cycle(0) when the master lane has not started, set_cycle(1e6) when it
//    has been slowed to a crawl.
template <typename E, typename Setup>
inline void check_part_engine_contract(Setup setup) {
    {   // Silence in, no output growth, and finite forever.
        E e; setup(e);
        float peak = 0.f;
        for (int i = 0; i < 48000 * 5; ++i) {
            float l, r;
            if (e.consumes_input()) e.process_in(0.f, 0.f);
            e.process(l, r);
            REQUIRE(std::isfinite(l));
            REQUIRE(std::isfinite(r));
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        }
        CHECK(peak <= 1.f);
    }
    {   // consumes_input() and process_in() are overridden TOGETHER.
        E e; setup(e);
        if (e.consumes_input()) {
            float l0 = 0.f, r0 = 0.f, l1 = 0.f, r1 = 0.f;
            for (int i = 0; i < 4800; ++i) {
                e.process_in(0.f, 0.f);
                e.process(l0, r0);
            }
            E f; setup(f);
            for (int i = 0; i < 4800; ++i) {
                f.process_in(std::sin(i * 0.05f), std::sin(i * 0.05f));
                f.process(l1, r1);
            }
            // If process_in were unreachable the two runs would be identical.
            CHECK(l0 != l1);
        }
    }
    {   // Every no-op setter is safe to call in any order, at any time.
        E e; setup(e);
        e.set_flow(true); e.set_flow(false);
        e.set_gate(true); e.set_gate(false);
        e.set_hold(true); e.set_hold(false);
        e.set_cycle(0.f); e.set_cycle(1e6f); e.set_cycle(0.25f);
        e.set_width(0.f); e.set_width(1.f);
        float l, r;
        for (int i = 0; i < 4800; ++i) {
            if (e.consumes_input()) e.process_in(0.f, 0.f);
            e.process(l, r);
            REQUIRE(std::isfinite(l));
            REQUIRE(std::isfinite(r));
        }
    }
}

}  // namespace spky
