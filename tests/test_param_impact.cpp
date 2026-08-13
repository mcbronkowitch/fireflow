// tests/test_param_impact.cpp
//
// The gate that would have caught the DIRT macro. DIRT aimed two of its four
// targets at GRIT, the wet/dry of a block nothing ever switches on, and nobody
// noticed until someone rendered it: the flow layer pushed the values, the
// setters accepted them, every unit test stayed green, and the knob did
// nothing. Nine more parameters were in the same state when this file was
// written (docs/2026-08-13-glow-macro-audit.md).
//
// So this is not a unit test of any one component. It renders audio and asks
// the only question those components cannot answer about themselves: does
// moving this parameter change what comes out?
//
// TWO GATES, both exact-set comparisons rather than one-way checks, so neither
// list can rot:
//
//   1. Every ParamId moves audio on at least one terrain, except a named set
//      that provably cannot. A parameter that goes dead reddens this; so does
//      one that comes back to life while still on the list.
//   2. Every ParamId that moves audio moves it in BOTH operating modes, except
//      a named set of mode-exclusive ones. This is the gate the FLOW melody
//      engine will change on purpose -- when it lands, the expected set here
//      shrinks and this test tells you exactly by how much.
//
// FxMem: echo + BBD line memory is static here, the idiom
// tests/test_flow_audio.cpp and tests/test_bbd_engine.cpp already use. Sampler
// record buffers are deliberately left null (two 42 s/part heap buffers is not
// a price worth paying to prove a gate), so a Sampler deck runs silent -- every
// candidate terrain is inspected and rejected before it is rendered.
#include "doctest/doctest.h"
#include "flow/flow.h"
#include "center/center.h"
#include "fx/flux.h"
#include "parts/bbd_engine.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace spky;
using namespace spky::flow;

namespace {

// Two slots: the two Instruments of a comparison run CONCURRENTLY and must not
// share echo/BBD memory, or each would hear the other's tail.
constexpr int kSlots = 2;
float s_pi_echo[kSlots][PART_COUNT][2][Flux::kMaxSamples];
float s_pi_bbd[kSlots][PART_COUNT][2][BbdEngine::kCells];
AmbientReverb s_pi_reverb[kSlots];

FxMem pi_fx_mem(int slot) {
    FxMem m;
    for (int p = 0; p < PART_COUNT; ++p) {
        m.echo[p][0] = s_pi_echo[slot][p][0];
        m.echo[p][1] = s_pi_echo[slot][p][1];
        m.bbd[p][0]  = s_pi_bbd[slot][p][0];
        m.bbd[p][1]  = s_pi_bbd[slot][p][1];
    }
    m.reverb = &s_pi_reverb[slot];
    return m;
}

constexpr float  kSr    = 48000.f;
constexpr int    kBlock = Center::kCtrlInterval;      // 96
constexpr float  kCtrlHz = kSr / float(kBlock);       // 500 Hz, as the hosts run it
constexpr double kDur   = 4.0;   // render window, seconds
constexpr double kSkip  = 0.5;   // discarded attack/settle head
constexpr int    kPer   = 2;     // terrains per operating mode

// A parameter counts as moving audio at this much relative difference. The
// measured population is bimodal by orders of magnitude -- dead parameters read
// exactly 0.0 (bit-identical renders, not "small"), live ones read 0.03 and up
// -- so this threshold sits in an empty region and is not a tuning knob. It is
// deliberately NOT zero: a parameter whose only effect is denormal-level dither
// would pass a `> 0` test while being inaudible, and that is the failure this
// file exists to catch.
constexpr double kMoved = 1e-6;

// How much of the mix a deck must carry before its parameters may be judged on
// that terrain. See ReferencePatch below for why this check exists at all.
constexpr double kDeckAudible = 0.05;

bool has_sampler(const Terrain& t) {
    return int(t.base[P_ENGINE_A] + 0.5f) == ENGINE_SAMPLER ||
           int(t.base[P_ENGINE_B] + 0.5f) == ENGINE_SAMPLER;
}

// Excluded from both gates, each for its own reason.
//
// P_MODE, and not because it is hard to measure: it IS the axis the second gate
// splits on. Sweeping it would compare a FLOW render against a STEP one, which
// says nothing about whether the parameter is wired -- the two modes are
// different instruments by design. Covered by tests/test_flow_mode.cpp.
//
// FORM and SONG because this rig cannot give a STABLE answer about them, and a
// gate that flips with its sample set teaches the reader to ignore it. They
// reach the audio only through shape_value()'s S&H segment (waveforms.h, weight
// (shape-0.75)*4), so whether they move anything depends on a SHAPE value the
// terrain draws -- capped at {0,.25} for drone, uniform elsewhere, so roughly a
// quarter of non-drone decks. Measured: dead on 40 of 40 terrains in the first
// sample; SONG_A alone came alive when the CHOKE fix changed which terrains
// this rig picks. Widening the sample until the answer stabilised would cost
// more render time than the whole suite. The SHAPE/SMOOTH rework (roadmap) owns
// this; when it lands, delete this exclusion and let the gate judge them.
bool is_excluded(int p) {
    return p == P_MODE ||
           p == P_FORM_A || p == P_FORM_B || p == P_SONG_A || p == P_SONG_B;
}

// The operating point every comparison runs at: every value the flow layer
// actually pushes with all six macros at their CENTRE, settled.
//
// NOT Terrain::base[]. That was this rig's first version and it measured the
// wrong instrument: for a story-owned parameter stage 4 writes base[p] =
// bp[0], the curve's calm floor, so rendering from base[] puts every macro at
// zero at once. Measured at that corner, BRIGHT's floor holds FILT_A/B at
// about -0.5 on EVERY terrain, both decks sit near silence, and one of them
// usually falls off the filter cliff entirely and renders exact zeros -- which
// then reports every _B parameter as dead. The bug was in the rig, not the
// instrument. A centred macro vector is an operating point somebody might
// actually listen to.
struct ReferencePatch {
    float v[P_COUNT];
    bool  step;
    int   steps_a, steps_b;
};

ReferencePatch reference_patch(uint32_t master) {
    TerrainState st; st.master = master;
    Instrument probe;
    probe.init(kSr, pi_fx_mem(0));
    Flow fl;
    fl.init(&probe, kCtrlHz);
    for (int m = 0; m < MACRO_COUNT; ++m) fl.set_macro(m, 0.5f);
    fl.wake(st);
    // Long enough for the SPACE slew and the discrete hysteresis to land; the
    // wake() itself forces them, so this only has to outlast the blend that
    // wake() does not start. Cheap: no audio is rendered here.
    for (int i = 0; i < 1000; ++i) fl.tick();

    ReferencePatch rp{};
    for (int p = 0; p < P_COUNT; ++p) rp.v[p] = fl.param_now(p);
    rp.step    = rp.v[P_MODE] > 0.5f;
    rp.steps_a = int(clamp_to(kParams[P_STEPS_A], rp.v[P_STEPS_A]) + 0.5f);
    rp.steps_b = int(clamp_to(kParams[P_STEPS_B], rp.v[P_STEPS_B]) + 0.5f);
    return rp;
}

// Push a reference patch onto an Instrument, optionally overriding one
// parameter. `param < 0` means "no override".
void apply_patch(Instrument& in, const ReferencePatch& rp, int param, float v) {
    for (int p = 0; p < P_COUNT; ++p) {
        // set_step() takes mode and count together and set_sync() is global, so
        // these three cannot go through the per-parameter apply_param() -- the
        // same reason Flow::push_mode_and_steps exists. Issued as one unit
        // below.
        if (p == P_MODE || p == P_STEPS_A || p == P_STEPS_B) continue;
        apply_param(in, p, p == param ? v : rp.v[p]);
    }
    const int sa = param == P_STEPS_A
        ? int(clamp_to(kParams[P_STEPS_A], v) + 0.5f) : rp.steps_a;
    const int sb = param == P_STEPS_B
        ? int(clamp_to(kParams[P_STEPS_B], v) + 0.5f) : rp.steps_b;
    in.set_sync(rp.step);
    in.set_step(PART_A, rp.step, sa);
    in.set_step(PART_B, rp.step, sb);
}

// Render two Instruments in lockstep from the same reference patch, differing
// in exactly one thing, and return the relative RMS difference. 0.0 means the
// two renders are bit-identical.
//
// Deliberately NOT driven through Flow during the render: Flow re-pushes every
// parameter each control tick from its own terrain evaluation, so an override
// would be erased on the next tick. Flow establishes the operating point; the
// render then holds it still so ONE parameter can differ.
double compare(const ReferencePatch& rp, int param, float v_lo, float v_hi,
               int mute_deck = -1) {
    Instrument a, b;
    a.init(kSr, pi_fx_mem(0));
    b.init(kSr, pi_fx_mem(1));
    apply_patch(a, rp, param, v_lo);
    apply_patch(b, rp, param, v_hi);
    if (mute_deck >= 0) b.set_part_level(mute_deck, 0.f);

    const size_t total = size_t(kDur * kSr), skip = size_t(kSkip * kSr);
    double sum_d = 0.0, sum_a = 0.0;
    for (size_t i = 0; i < total; ++i) {
        float al = 0.f, ar = 0.f, bl = 0.f, br = 0.f;
        a.process(nullptr, nullptr, &al, &ar, 1);
        b.process(nullptr, nullptr, &bl, &br, 1);
        if (i < skip) continue;
        const double dl = double(al) - double(bl), dr = double(ar) - double(br);
        sum_d += dl * dl + dr * dr;
        sum_a += double(al) * al + double(ar) * ar;
    }
    // A silent reference cannot answer the question either way. Reported as
    // "did not move" rather than as a pass, so a rig that accidentally renders
    // silence fails loudly instead of passing everything.
    if (sum_a <= 0.0) return 0.0;
    return std::sqrt(sum_d / sum_a);
}

// Does `deck` carry enough of the mix for its parameters to be judgeable here?
// A terrain can still leave one deck near-silent even at a centred macro vector
// -- an engine whose excitation never arrives, an envelope that never opens --
// and on such a terrain every parameter of that deck renders bit-identical.
// Without this check the gate reports those as defects.
bool deck_audible(const ReferencePatch& rp, int deck) {
    return compare(rp, -1, 0.f, 0.f, deck) > kDeckAudible;
}

struct Terrains { ReferencePatch flow[kPer], step[kPer]; };

Terrains pick_terrains() {
    Terrains out{};
    int nf = 0, ns = 0;
    for (uint32_t m = 1; m < 400 && (nf < kPer || ns < kPer); ++m) {
        TerrainState st; st.master = m;
        Terrain t = generate(st);
        if (has_sampler(t)) continue;
        const ReferencePatch rp = reference_patch(m);
        if (!deck_audible(rp, PART_A) || !deck_audible(rp, PART_B)) continue;
        if (rp.step && ns < kPer)  out.step[ns++] = rp;
        if (!rp.step && nf < kPer) out.flow[nf++] = rp;
    }
    // A filter that silently kept nothing would be a gate that tests nothing.
    REQUIRE(nf == kPer);
    REQUIRE(ns == kPer);
    return out;
}

// Does `param` move audio on any of these terrains? Stops at the first terrain
// that says yes -- the gate asks a yes/no question, and paying for the
// remaining renders would only refine a number nobody reads.
bool moves_audio(const ReferencePatch* rps, int param) {
    for (int i = 0; i < kPer; ++i)
        if (compare(rps[i], param, kParams[param].lo, kParams[param].hi) > kMoved)
            return true;
    return false;
}

std::string name_of(int p) { return std::string(kParams[p].name + 2); }

// Sorted, comma-joined, so a failure names the difference instead of making the
// reader diff two unordered dumps.
std::string join(const bool* set) {
    std::string s;
    for (int p = 0; p < P_COUNT; ++p)
        if (set[p]) { if (!s.empty()) s += ", "; s += name_of(p); }
    return s.empty() ? "(none)" : s;
}

void report(const bool* actual, const bool* expected, const char* what) {
    for (int p = 0; p < P_COUNT; ++p) {
        if (is_excluded(p)) continue;
        INFO("parameter " << name_of(p));
        CHECK(actual[p] == expected[p]);
    }
    if (join(actual) != join(expected))
        std::printf("  %s now:      %s\n  %s expected: %s\n",
                    what, join(actual).c_str(), what, join(expected).c_str());
}

} // namespace

TEST_CASE("param impact: every parameter moves audio somewhere") {
    // The parameters that provably cannot, each with the mechanism that stops
    // them. Measured 2026-08-13; full method in
    // docs/2026-08-13-glow-macro-audit.md.
    //
    // GRIT / FLUXMIX / LINK: apply_param() has no set_fx_on(), so neither FX
    //   block is ever switched on and all six are wet/dry controls of silence.
    //   Unlike FORM/SONG (excluded above), this does not depend on what a
    //   terrain happens to draw -- no terrain can switch a block on -- so the
    //   answer is the same for every sample set.
    //
    // REMOVE an entry when its cause is fixed; this case fails on a listed
    // parameter coming back to life just as it does on a new death, so the list
    // cannot silently outlive the defect.
    bool expected[P_COUNT] = {};
    for (int p : { P_GRIT_A, P_GRIT_B, P_FLUXMIX_A, P_FLUXMIX_B,
                   P_LINK_A, P_LINK_B })
        expected[p] = true;

    const Terrains ter = pick_terrains();
    bool actual[P_COUNT] = {};
    for (int p = 0; p < P_COUNT; ++p) {
        if (is_excluded(p)) continue;
        actual[p] = !moves_audio(ter.flow, p) && !moves_audio(ter.step, p);
    }
    report(actual, expected, "dead");
}

TEST_CASE("param impact: a live parameter works in both operating modes") {
    // Mode-exclusive by construction, not by accident:
    //
    // STEPS / SHUFFLE / TEMPO_BPM are step-grid concepts with nothing to act on
    //   in the free mode.
    // COUPLE is the mirror image: it corrects a phase error between the decks
    //   that the step grid does not leave, so it is free-mode-only.
    //
    // DENSITY used to be on this list too -- in the free mode the melodic lane
    //   was a continuous LFO (one slot, no gate) that DENSITY had nothing to
    //   act on. Spec 2026-08-13 flow-melody-engine task 8 wires the FLOW
    //   melody engine into every note engine's PITCH lane (Part::init /
    //   Part::_engine_swap push set_flow_melody(true)), which walks phrase
    //   slots and consults DENSITY's k-of-8 groove ranking in FLOW exactly as
    //   STEP already did -- so DENSITY now moves audio in both modes and drops
    //   off this list, per the comment above that predicted exactly this.
    //
    // Anything NOT listed here that works in one mode only is a defect: half of
    // every terrain population cannot reach it.
    bool expected[P_COUNT] = {};
    for (int p : { P_STEPS_A, P_STEPS_B, P_SHUFFLE, P_TEMPO_BPM, P_COUPLE })
        expected[p] = true;

    const Terrains ter = pick_terrains();
    bool actual[P_COUNT] = {};
    for (int p = 0; p < P_COUNT; ++p) {
        if (is_excluded(p)) continue;
        actual[p] = moves_audio(ter.flow, p) != moves_audio(ter.step, p);
    }
    report(actual, expected, "mode-exclusive");
}

