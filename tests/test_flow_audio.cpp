// tests/test_flow_audio.cpp
//
// The audio gates (spec §7.8) -- Task 10, last engineering task on the flow
// layer: NaN, RMS bounds, level-jump-on-blend, the calm corner, plus a
// discrete-churn gate that Task 7's watch item asked for (scale/root/form/
// song reharmonizing many times a second under static macros). These are
// SANITY bounds, not render-hash/byte-identity gates (none of that kind is
// added here): they exist to catch NaN, silence, a level jump or a calm
// corner that has gone mute, not to pin today's exact numbers.
//
// Control rate: every case ticks Flow once every Center::kCtrlInterval
// samples (96 @ 48 kHz = 500 Hz), the SAME raster the render host drives it
// on (host/render/main.cpp, Task 9) -- not an independently chosen number,
// so a gate here means the same thing a render-host audition would show.
//
// FxMem: echo + BBD line memory is provided (static, no heap), matching the
// pattern tests/test_bbd_engine.cpp already uses for a two-deck Instrument.
// Sampler record buffers are deliberately left null -- this rig cannot
// afford two 42 s/part heap buffers just to prove an audio gate -- which
// means any deck that rolls ENGINE_SAMPLER runs silent (FxMem's own
// contract, engine/instrument.h). A silent Sampler deck would trip the RMS
// floor for a reason that has nothing to do with the flow layer, so every
// case here filters its candidate master seeds by generating and
// inspecting the terrain BEFORE rendering, and asserts the filtered set is
// non-empty (a filter that quietly skips everything would be a gate that
// tests nothing).
#include "doctest/doctest.h"
#include "flow/flow.h"
#include "center/center.h"
#include "fx/flux.h"
#include "parts/bbd_engine.h"
#include <cmath>
#include <cstdint>
#include <cstdio>

using namespace spky;
using namespace spky::flow;

namespace {

// ---------------------------------------------------------------------------
// Shared FX memory (static, no heap in engine/ -- the buffers themselves are
// static storage here in the test rig, following tests/test_instrument.cpp
// and tests/test_bbd_engine.cpp's TapeMem/s_inst_bbd idiom). Two slots: the
// single-Instrument cases below (fixed-seed RMS, calm corner, churn) only
// ever have one Instrument alive at a time and reuse slot 0 sequentially;
// the differential NEW-blend gate runs a control and a press Instrument
// CONCURRENTLY and needs two independent buffer sets so neither run's echo/
// BBD memory bleeds into the other's.
constexpr int kFxSlots = 2;
float s_fa_echo[kFxSlots][PART_COUNT][2][Flux::kMaxSamples];
float s_fa_bbd[kFxSlots][PART_COUNT][2][BbdEngine::kCells];
AmbientReverb s_fa_reverb[kFxSlots];

FxMem flow_audio_fx_mem(int slot = 0) {
    FxMem m;
    for (int p = 0; p < PART_COUNT; ++p) {
        m.echo[p][0] = s_fa_echo[slot][p][0];
        m.echo[p][1] = s_fa_echo[slot][p][1];
        m.bbd[p][0]  = s_fa_bbd[slot][p][0];
        m.bbd[p][1]  = s_fa_bbd[slot][p][1];
    }
    m.reverb = &s_fa_reverb[slot];
    // sampler_buf intentionally left null -- see file header.
    return m;
}

// Control raster: Center::kCtrlInterval samples/tick at 48 kHz -- the same
// 500 Hz the render host drives Flow on (host/render/main.cpp). The task
// brief's own pseudocode ticks every 64 samples (750 Hz); 500 Hz is used
// here instead so a gate failure here means the same thing a render-host
// audition would show, and because a control-rate-sensitive gate would be a
// fragile gate (task-10-brief's resolutions).
constexpr int   kBlock = Center::kCtrlInterval;   // 96
constexpr float kSr    = 48000.f;
constexpr float kCtrlHz = kSr / float(kBlock);    // 500 Hz

struct RenderStats {
    double rms  = 0.0;
    float  peak = 0.f;
    bool   has_nan = false;
};

// Render duration_s seconds through Instrument::process one sample at a
// time, ticking Flow every kBlock samples. NaN is checked over the WHOLE
// render; RMS/peak accumulate only once skip_s seconds have elapsed, so a
// case can discard a settle/attack tail without hiding a NaN that happens
// to land inside it.
RenderStats render_flow(Flow& fl, Instrument& inst,
                         double duration_s, double skip_s = 0.0) {
    RenderStats st;
    double sum_sq = 0.0;
    size_t counted = 0;
    const size_t total = size_t(duration_s * kSr);
    const size_t skip  = size_t(skip_s * kSr);
    for (size_t i = 0; i < total; ++i) {
        if (i % size_t(kBlock) == 0) fl.tick();
        float l = 0.f, r = 0.f;
        inst.process(nullptr, nullptr, &l, &r, 1);
        if (l != l || r != r) st.has_nan = true;   // NaN check, whole render
        if (i >= skip) {
            sum_sq += double(l) * l + double(r) * r;
            counted += 2;
            const float al = std::fabs(l), ar = std::fabs(r);
            if (al > st.peak) st.peak = al;
            if (ar > st.peak) st.peak = ar;
        }
    }
    st.rms = counted ? std::sqrt(sum_sq / double(counted)) : 0.0;
    return st;
}

bool terrain_has_sampler(const Terrain& t) {
    return int(t.base[P_ENGINE_A] + 0.5f) == ENGINE_SAMPLER ||
           int(t.base[P_ENGINE_B] + 0.5f) == ENGINE_SAMPLER;
}

// Candidate master seeds -- the brief's own {0x101, 0x202, 0x303, 0x404}
// plus six more so a filtered-out master never leaves the tested set
// empty. Verified (test-start filter below, not by hand): none of these
// ten actually rolls a Sampler deck, but the filter stays in place so that
// fact is provable at test time, not merely asserted in a comment.
// 0xD0Du/0xC0C0u (review I-4): the NEW-blend gate needs seeds whose
// new_full() does NOT switch a deck's engine, and the original eight
// candidates yielded only two (0x404, 0x808) -- a bare-step mutation was
// caught by only one of them (0x808; see task-10-report.md round 5). These
// two were added after scanning wider master ranges specifically to find
// more non-switching, non-Sampler seeds -- see task-10-report.md for the
// scan and the resulting per-seed mutation results.
constexpr uint32_t kCandidateMasters[] = {
    0x101u, 0x202u, 0x303u, 0x404u, 0x505u, 0x606u, 0x707u, 0x808u,
    0xD0Du, 0xC0C0u,
};
constexpr int kMaxKept = int(sizeof(kCandidateMasters) / sizeof(uint32_t));

// Fixed-size (no heap): fills `kept` with every candidate whose terrain
// avoids ENGINE_SAMPLER on both decks, returns the count. A filter that
// silently accepted an empty result would be a gate that tests nothing
// (task-10-brief resolution #4) -- callers REQUIRE the count > 0.
int filtered_masters(uint32_t kept[kMaxKept]) {
    int n = 0;
    for (uint32_t m : kCandidateMasters) {
        TerrainState st; st.master = m;
        Terrain t = generate(st);
        if (!terrain_has_sampler(t)) kept[n++] = m;
    }
    return n;
}

// dBFS with a floor (kBlendLevelFloorDb), so a near-silent window produces a
// bounded number instead of -inf (rms == 0) or a value whose ratio against
// another near-silent window can blow up arbitrarily.
double level_db(double rms) {
    static const double kFloorLin = std::pow(10.0, double(kBlendLevelFloorDb) / 20.0);
    const double clamped = rms > kFloorLin ? rms : kFloorLin;
    return 20.0 * std::log10(clamped);
}

// Whether master's new_full() draw would switch either deck's engine
// mid-blend. new_full()'s target is a pure function of the woken master
// (Flow::wake seeds _seq from s.master alone, and nothing but a NEW-family
// call consumes from it), so calling it immediately after a throwaway
// wake() reproduces exactly the same draw a real 6 s-settled press would
// make -- no need to replicate the settle here. Uses FX slot 0's buffers
// only to construct a valid Instrument; renders no audio.
bool new_full_switches_engine(uint32_t master) {
    TerrainState st; st.master = master;
    const Terrain before = generate(st);
    Instrument probe; probe.init(kSr);
    Flow fl; fl.init(&probe, kCtrlHz);
    fl.wake(st);
    fl.new_full();
    const Terrain& after = terrain_of(fl);
    return int(before.base[P_ENGINE_A] + 0.5f) != int(after.base[P_ENGINE_A] + 0.5f) ||
           int(before.base[P_ENGINE_B] + 0.5f) != int(after.base[P_ENGINE_B] + 0.5f);
}

} // namespace

TEST_CASE("flow audio: fixed seeds render clean and inside RMS bounds (7.8)") {
    uint32_t kept[kMaxKept];
    const int n = filtered_masters(kept);
    REQUIRE(n > 0);   // the filter must actually have tested something

    for (int i = 0; i < n; ++i) {
        CAPTURE(kept[i]);
        Instrument inst;
        FxMem mem = flow_audio_fx_mem();
        inst.init(kSr, mem);
        Flow fl; fl.init(&inst, kCtrlHz);
        TerrainState st; st.master = kept[i];
        fl.wake(st);
        for (int m = 0; m < MACRO_COUNT; ++m) fl.set_macro(m, 0.5f);

        const RenderStats rs = render_flow(fl, inst, 10.0);
        CHECK_FALSE(rs.has_nan);
        CHECK(rs.rms > kFixedSeedRmsMin);
        CHECK(rs.rms < kFixedSeedRmsMax);
    }
}

TEST_CASE("flow audio: calm corner sits under the ceiling, and above the silence floor (7.8)") {
    uint32_t kept[kMaxKept];
    const int n = filtered_masters(kept);
    REQUIRE(n > 0);

    for (int i = 0; i < n; ++i) {
        CAPTURE(kept[i]);
        Instrument inst;
        FxMem mem = flow_audio_fx_mem();
        inst.init(kSr, mem);
        Flow fl; fl.init(&inst, kCtrlHz);
        TerrainState st; st.master = kept[i];
        fl.wake(st);
        for (int m = 0; m < MACRO_COUNT; ++m) fl.set_macro(m, 0.f);

        // Skip the first 3 s (reverb/envelope tails from wake's boot state)
        // per the brief; 10 s total.
        const RenderStats rs = render_flow(fl, inst, 10.0, 3.0);
        CHECK_FALSE(rs.has_nan);
        CHECK(rs.rms <= kCalmCornerRmsMax);
        // kCalmCornerRmsMin is a silence detector, not a musical target --
        // see its comment in taste.h. It catches a calm corner that went
        // mute, nothing more.
        CHECK(rs.rms > kCalmCornerRmsMin);
    }
}

// NEW blend vs. no-press control (spec §5/§7.8) -- Task 10, round 5 (review
// round 1 found the round-3/4 comment materially overstated what this gate
// covers; corrected here -- see task-10-report.md for the full history).
//
// The original single-run window-to-window ratio design conflated the
// instrument's own note-envelope/retrigger dynamics with anything the
// blend itself did, and failed on every seed tested (see task-10-report.md
// §4). Round 2 fixed that with a DIFFERENTIAL design: run a CONTROL
// Flow+Instrument (same terrain, same macros, never presses NEW) alongside
// the PRESS run and compare the two at the same window index in dBFS
// (floored, kBlendLevelFloorDb) -- native dynamics appear in both runs and
// cancel, so what survives is what the blend actually changed. That design
// still failed on two seeds with no engine switch at all, and round 3
// found why: the control keeps playing the OUTGOING terrain for the full
// 6 s while the press run settles onto a completely different terrain, so
// comparing them once the blend is mostly/fully settled compares two
// terrains' natural loudness, not anything the blend did.
//
// The gate now asserts ONLY inside the first kBlendGateWindowS seconds
// after the press -- kBlendSpikeDb/kBlendDropDb themselves are unchanged;
// this narrows WHAT THE GATE CLAIMS, not how much it tolerates. taste.h's
// kBlendGateWindowS comment has the full, corrected accounting: 1.0 s is a
// CONSERVATIVE CHOICE (checked green out to 1.75 s, red at 2.0 s), not a
// derivation; real measured in-window headroom is ~1.14 dB against the
// 6 dB spike bound, not the ~4.3 dB an earlier (wrong) phase-math argument
// implied; and THE GATE'S BLIND SPOT IS STRUCTURAL, NOT INCIDENTAL -- the
// carrier deck's discrete switch (at kCarrierStaggerFrac * kBlendS = 1.5 s)
// and its duck (~1.25-1.75 s) sit entirely outside kBlendGateWindowS =
// 1.0 s, for every seed, permanently. This gate covers the TEXTURE deck's
// switch and the initial retarget. It does NOT cover the carrier deck's
// switch or duck -- read taste.h before assuming it does.
//
// The full 6 s is still RENDERED and MEASURED for every seed (see
// task-10-report.md's plain-measurement table) -- only the CHECK()s are
// scoped, and (review Minor 3) ONLY the level CHECK()s: NaN is still
// asserted for every window of every seed, including excluded ones, below.
//
// EXCLUSION (measured, not carved around -- see task-10-report.md round 2
// for the full per-seed numbers): seeds whose new_full() switches a deck's
// engine mid-blend dip well below this bound -- worst measured case
// (master 0x101) is 65.16 dB below the no-press control, 4.00 s of the 6 s
// blend spent more than 20 dB below it. This is KNOWN AND ACCEPTED FOR NOW,
// pending a listening-loop decision (Bastian, 2026-08-05): a brief
// near-silence when a NEW press switches engines is not being called a
// defect here, and the fix direction is not obvious for free -- an
// engine-switch crossfade would mean running two part engines at once,
// which is expensive on the Daisy target. So the LEVEL comparison is
// scoped to seeds new_full() does NOT switch an engine on, honestly (no
// clause inside the gate pretends to cover the switch case); NaN checks
// still run on excluded seeds too (they have nothing to do with the level
// comparison). The exclusion is asserted non-vacuous, and (review I-4)
// asserted to leave at least kMinNoSwitchSeeds qualifying seeds, not just
// a non-empty set: under the RED-proof bare-step mutation only ONE of the
// original two candidates (0x808) actually caught it (0x404 stayed within
// ±1.6 dB) -- REQUIRE(tested > 0) cannot detect that kind of degradation,
// only REQUIRE(tested >= N) can.
constexpr int kMinNoSwitchSeeds = 3;

TEST_CASE("flow audio: a NEW blend never jumps the level vs. a no-press control (7.8)") {
    uint32_t kept[kMaxKept];
    const int n = filtered_masters(kept);
    REQUIRE(n > 0);

    constexpr double kWindowS = 0.25;
    const int windows = int(kBlendS / kWindowS + 0.5);   // 24, full blend
    const int gated_windows = int(double(kBlendGateWindowS) / kWindowS + 0.5); // 4

    int tested = 0;
    for (int i = 0; i < n; ++i) {
        const uint32_t master = kept[i];
        const bool excluded = new_full_switches_engine(master);   // see EXCLUSION above
        if (!excluded) ++tested;
        CAPTURE(master);
        CAPTURE(excluded);

        TerrainState seed_state; seed_state.master = master;

        Instrument c_inst;
        FxMem c_mem = flow_audio_fx_mem(0);
        c_inst.init(kSr, c_mem);
        Flow c_fl; c_fl.init(&c_inst, kCtrlHz);
        c_fl.wake(seed_state);
        for (int m = 0; m < MACRO_COUNT; ++m) c_fl.set_macro(m, 0.5f);

        Instrument p_inst;
        FxMem p_mem = flow_audio_fx_mem(1);
        p_inst.init(kSr, p_mem);
        Flow p_fl; p_fl.init(&p_inst, kCtrlHz);
        p_fl.wake(seed_state);
        for (int m = 0; m < MACRO_COUNT; ++m) p_fl.set_macro(m, 0.5f);

        // 6 s identical settle: both instances share identical inputs up to
        // this point (same master, same macro pushes, same tick clock), so
        // this renders bit-for-bit identically -- only the press run
        // diverges, and only once new_full() actually fires below.
        render_flow(c_fl, c_inst, 6.0, 6.0);
        render_flow(p_fl, p_inst, 6.0, 6.0);

        p_fl.new_full();
        REQUIRE(p_fl.blend_phase() < 1.f);

        // The FULL 6 s blend is rendered and NaN-checked here for EVERY
        // seed, excluded or not (review Minor 3) -- kept as a plain
        // measurement (task-10-report.md), not asserted on past
        // gated_windows. Only the first kBlendGateWindowS seconds (see the
        // comment above and taste.h) get the spike/drop CHECK()s, and only
        // for non-excluded seeds.
        for (int w = 0; w < windows; ++w) {
            const RenderStats cs = render_flow(c_fl, c_inst, kWindowS);
            const RenderStats ps = render_flow(p_fl, p_inst, kWindowS);
            CHECK_FALSE(cs.has_nan);
            CHECK_FALSE(ps.has_nan);
            if (excluded || w >= gated_windows) continue;   // measured, not asserted
            const double c_db = level_db(cs.rms);
            const double p_db = level_db(ps.rms);
            const double diff_db = p_db - c_db;
            CAPTURE(w);
            CAPTURE(c_db);
            CAPTURE(p_db);
            CHECK(diff_db <= double(kBlendSpikeDb));
            CHECK(diff_db >= -double(kBlendDropDb));
        }
    }
    // review I-4: REQUIRE(tested > 0) alone cannot detect the gate quietly
    // degrading to a single seed that happens not to catch anything --
    // require the qualifying set stays at least kMinNoSwitchSeeds wide.
    REQUIRE(tested >= kMinNoSwitchSeeds);
}

// ---------------------------------------------------------------------------
// Discrete-churn gate (Task 7 watch item / task-10-brief resolution #2): a
// scale or root (or engine/form/song) that reharmonizes several times a
// second is musically wrong for an ambient drone. Every macro is held
// STATIC here -- weather is the only mover -- so this is a different
// scenario from Task 7's randomized-macro-sweep measurement (258 changes in
// 6 s, 256 with no button presses): that number came from macros actually
// MOVING; this gate isolates weather alone.
//
// The bound, decided BEFORE measuring:
//
//  - Params with no story owner (Terrain::storied[p] == false for this
//    terrain) sit at their terrain's fixed base value (flow.cpp's
//    eval_terrain: `out[p] = t.base[p]` unless a story overrides it) and
//    nothing here fires a NEW gesture. So a non-storied discrete's pushed
//    value cannot move at all -- the bound is EXACTLY 0 changes, a
//    structural guarantee, not a measured one.
//
//  - Params a story DOES own (which ones depends on the terrain -- e.g.
//    DENSITY's "rate" vs. "thick" variant decides whether P_STEPS_A is
//    storied at all -- read live from Terrain::storied) move only through
//    weather (macros are static). taste.h's weather periods run
//    kWeatherPeriodMinS..kWeatherPeriodMaxS (300..1200 s), so every
//    oscillator is still inside its first quarter-cycle throughout this
//    60 s window (quarter of the FASTEST period is 75 s > 60 s) and hence
//    monotonic for the whole window (fast_sin(ts/period) rises from 0
//    without turning over before ts = period/4). taste.h's story
//    breakpoint ranges are contiguous and non-decreasing by construction
//    (each bp's Span touches, never overlaps backward, the next), so the
//    curve itself is monotonic non-decreasing in its governing macro. A
//    monotonic input can cross a hysteresis-guarded step seam only moving
//    forward, never back-and-forth ("chatter"); the worst-case swing this
//    analysis allows (~0.05 macro-units of weather offset at MOTION=0.5,
//    against curve slopes up to ~12 units/macro-unit for the steepest
//    table row) is under two step widths. The bound (taste.h
//    kDiscreteChurnMax) is set at 2 changes per storied discrete param
//    over 60 s -- comfortably inside that analysis, nowhere near Task 7's
//    256-with-macros-moving figure, and still a real, falsifiable bound
//    (see the RED proof in the report).
TEST_CASE("flow audio: discrete params stay stable under static macros (Task 10 churn gate)") {
    uint32_t kept[kMaxKept];
    const int n = filtered_masters(kept);
    REQUIRE(n > 0);

    constexpr double kWindowS = 60.0;
    const int ticks = int(kWindowS * kCtrlHz + 0.5);   // 30000 @ 500 Hz

    for (int i = 0; i < n; ++i) {
        CAPTURE(kept[i]);
        Instrument inst; inst.init(kSr);   // no FX chain needed: this gate
                                            // only watches param_now(), it
                                            // never calls process().
        Flow fl; fl.init(&inst, kCtrlHz);
        TerrainState st; st.master = kept[i];
        fl.wake(st);
        for (int m = 0; m < MACRO_COUNT; ++m) fl.set_macro(m, 0.5f);

        float prev[P_COUNT];
        for (int p = 0; p < P_COUNT; ++p) prev[p] = fl.param_now(p);
        int changes[P_COUNT] = {};
        for (int t = 0; t < ticks; ++t) {
            fl.tick();
            for (int p = 0; p < P_COUNT; ++p) {
                if (kParams[p].steps <= 0) continue;   // continuous, not this gate
                const float v = fl.param_now(p);
                if (v != prev[p]) { ++changes[p]; prev[p] = v; }
            }
        }

        const Terrain& t = terrain_of(fl);
        for (int p = 0; p < P_COUNT; ++p) {
            if (kParams[p].steps <= 0) continue;
            CAPTURE(kParams[p].name);
            CAPTURE(changes[p]);
            if (t.storied[p]) CHECK(changes[p] <= kDiscreteChurnMax);
            else               CHECK(changes[p] == 0);
        }
    }
}
