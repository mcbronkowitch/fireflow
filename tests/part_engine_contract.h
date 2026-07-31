#pragma once
#include <doctest/doctest.h>
#include "parts/engine_iface.h"
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <vector>

namespace spky {

namespace part_contract_detail {

// The class in which a pointed-to member function was DECLARED. [expr.unary.op]
// forms `&X::f` with the class that declares f, not with X -- so for an engine
// that does not override process_in, `&E::process_in` has type
// `void (IPartEngine::*)(float, float)`, and for one that does it has type
// `void (E::*)(float, float)`. That difference is a type, visible at compile
// time, and it is the only handle the language gives on "did this class
// override that virtual".
//
// Deliberately NOT `&E::process_in != &IPartEngine::process_in`: comparing
// pointers to VIRTUAL member functions has an unspecified result
// ([expr.eq]), so that formulation would be reading a value the standard
// declines to define. This one reads a type.
template <typename T> struct declaring_class;
template <typename C, typename R, typename... A>
struct declaring_class<R (C::*)(A...)> { using type = C; };
template <typename C, typename R, typename... A>
struct declaring_class<R (C::*)(A...) const> { using type = C; };

template <typename T>
using declaring_class_t = typename declaring_class<T>::type;

// "Someone below IPartEngine declared this." Compared against IPartEngine
// rather than against E on purpose: an engine may inherit its override from an
// intermediate base (SynthEngineT does exactly this for other members), and
// such an engine HAS overridden the virtual even though the declaring class is
// not E itself.
template <typename E>
inline constexpr bool declares_process_in =
    !std::is_same_v<declaring_class_t<decltype(&E::process_in)>, IPartEngine>;

template <typename E>
inline constexpr bool declares_consumes_input =
    !std::is_same_v<declaring_class_t<decltype(&E::consumes_input)>, IPartEngine>;

}  // namespace part_contract_detail

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
//
//    "Must not grow" is measured, not merely implied by the bound: a peak
//    taken over the whole window is satisfied by an engine creeping from
//    1e-6 to 0.99, which is precisely the failure the sentence is about. The
//    last second's peak is therefore compared against the first 100 ms', with
//    a stated allowance -- see kGrowthFactor below for what it does and does
//    not permit.
//
// 2. consumes_input() and process_in() are overridden TOGETHER. Nothing in
//    the language enforces this and the failure is silent: an engine that
//    implements process_in and forgets the flag never hears its input,
//    because Part caches the flag once per swap and skips the call
//    (engine_iface.h:78-85, part.h).
//
//    BOTH directions are checked, and they are checked by different means
//    because they fail differently:
//
//      * "process_in overridden, flag forgotten" -- the likely one, and the
//        one a runtime check CANNOT see, because the engine answers false and
//        any `if (consumes_input())` block skips itself. It is a static_assert
//        on the two declarations, so it fails at COMPILE time. (Until
//        2026-07-31 this block opened with `if (e.consumes_input())` and this
//        header's own comment claimed the pairing was enforced -- in exactly
//        the failure it named, the block was skipped and the contract passed.)
//      * "flag returned, process_in inert" -- an engine that says it consumes
//        input and then does not hear it. That one needs audio, so it is a
//        runtime CHECK on the difference between a silent run and a driven
//        one.
//
//    CALLER'S RESPONSIBILITIES, both of which a caller can get wrong and be
//    rewarded with a green block:
//      (a) The setup must leave the engine in a state where process_in's
//          effect is OBSERVABLE at process(). For BbdEngine that is automatic
//          (the input is always in the signal path); SamplerEngine, the only
//          other engine that consumes input today, would need
//          set_monitor(true) or recording armed, because otherwise process_in
//          only writes to the record buffer and the two runs really are
//          identical for a reason that is not a defect.
//      (b) The setup must give EACH constructed instance usable storage. This
//          block constructs two engines and runs them one after the other; a
//          setup that hands both the same file-scope buffers (which
//          test_bbd_engine.cpp's does) is benign only because BbdLine::Init
//          ends in Reset(), i.e. the second instance clears what the first
//          left. An engine without that property would compare a fresh run
//          against a run over the other's residue, and the difference below
//          would be nonzero for the wrong reason.
//
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
    // Block 2's first direction, and the reason it is here rather than inside
    // the block: a compile-time claim has no run time. See the comment above.
    static_assert(
        part_contract_detail::declares_process_in<E> ==
            part_contract_detail::declares_consumes_input<E>,
        "IPartEngine::process_in and IPartEngine::consumes_input must be "
        "overridden TOGETHER: an engine that implements process_in without "
        "the flag never hears its input, because Part caches the flag once "
        "per engine swap and skips the call entirely.");

    {   // Silence in, no growth, and finite forever.
        // What "no growth" allows: the last second's peak may exceed the
        // first 100 ms' by this factor. Not 1.0, because an engine whose
        // state is still settling (a filter charging, an envelope's attack)
        // legitimately rises early; not open-ended, because the failure this
        // is named for is a slow climb.
        constexpr float kGrowthFactor = 2.f;
        // And a floor, so an engine that is exactly silent at the start
        // (BbdEngine's construction default is MIX 0, i.e. output 0) is not
        // held to a ratio against zero.
        constexpr float kGrowthFloor = 1e-3f;
        constexpr int kN = 48000 * 5;
        E e; setup(e);
        float peak = 0.f, peak_early = 0.f, peak_late = 0.f;
        for (int i = 0; i < kN; ++i) {
            float l, r;
            if (e.consumes_input()) e.process_in(0.f, 0.f);
            e.process(l, r);
            REQUIRE(std::isfinite(l));
            REQUIRE(std::isfinite(r));
            const float a = std::max(std::fabs(l), std::fabs(r));
            peak = std::max(peak, a);
            if (i < 4800) peak_early = std::max(peak_early, a);
            if (i >= kN - 48000) peak_late = std::max(peak_late, a);
        }
        CAPTURE(peak_early);
        CAPTURE(peak_late);
        CHECK(peak <= 1.f);
        CHECK(peak_late <=
              std::max(peak_early * kGrowthFactor, kGrowthFloor));
    }
    {   // The runtime half of the pairing: flag returned, process_in inert.
        E e; setup(e);
        if (e.consumes_input()) {
            constexpr int kN = 4800;
            // Both channels, every sample -- not one sample of one channel.
            // A single-sample comparison happens to discriminate for the BBD
            // and would fail for no reason on a future engine whose two runs
            // coincide at exactly that index.
            std::vector<float> ref(2 * kN);
            for (int i = 0; i < kN; ++i) {
                float l, r;
                e.process_in(0.f, 0.f);
                e.process(l, r);
                ref[2 * i] = l;
                ref[2 * i + 1] = r;
            }
            E f; setup(f);
            double diff = 0.0;
            for (int i = 0; i < kN; ++i) {
                float l, r;
                const float x = std::sin(i * 0.05f);
                f.process_in(x, x);
                f.process(l, r);
                diff += std::fabs(l - ref[2 * i]);
                diff += std::fabs(r - ref[2 * i + 1]);
            }
            CAPTURE(diff);
            // If process_in were inert the two runs would be identical.
            CHECK(diff > 0.0);
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
