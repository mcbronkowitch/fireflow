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
#include <algorithm>
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

// "The calm corner" is every macro at its calm end -- with ONE macro that has
// no calm end. M_PACE is a time-stretch, not an amount: 0.5 is exactly x1 (the
// pace the terrain, or a transferred patch, actually specifies) and 0 is the
// x1/32 EXTREME, five octaves of tempo below what the terrain asked for. It is
// also the value Flow::init writes into _knob[M_PACE] for exactly this reason.
//
// Parking it at 0 with the other five is not a quieter instrument, it is a
// DIFFERENT one, and it moved this gate's own subject when PACE took DIRT's
// slot on 2026-08-12: the subsample's mute fraction read 32.8% (45 of 137)
// against kCalmMuteFracMax = 0.10 -- more than three times the bound, and
// roughly five times the 6.58% the FULL population measured at the time
// (taste.h states both; the 6.58% is a measured rate, not the bound). A lane
// clocked 32x slow emits almost nothing inside a 7 s measurement window.
// Nothing about the level mechanism had changed. Neither kCalmMuteFracMax nor
// kCalmCornerRmsMin was touched to accommodate it -- the parking was corrected
// instead, which is the only move here that does not amount to fitting a gate
// to a knob position it was never measuring.
//
// WHAT THIS PARKING COSTS, stated so it is not silently lost: no calm-corner
// render now exercises PACE anywhere but neutral. The x1/32 and x4 ends are
// covered by "flow audio: an extreme PACE still renders finite audio" below,
// which asserts FINITENESS ONLY -- see the reasoning there.
void park_calm(Flow& fl) {
    for (int m = 0; m < MACRO_COUNT; ++m)
        fl.set_macro(m, m == M_PACE ? 0.5f : 0.f);
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

// THE doctest::should_fail() MARKER THIS CASE CARRIED FROM 2026-08-05 IS
// RETIRED (2026-08-06, Task 7). It was self-expiring by design -- should_fail
// turns a case red the moment it starts PASSING -- and it fired, so it came
// off. Every assertion here now passes on all ten seeds, with no constant
// moved and no seed dropped.
//
// WHAT CHANGED, measured commit by commit out of a worktree at the branch
// point 4ec5be0 rather than reasoned about (the figures live in taste.h,
// beside kCalmCornerRmsMax, so the two copies of this finding cannot drift):
// the 0x707 ceiling breach the marker was written for stopped reproducing at
// 3435c31, and inside that commit it is the drone SHAPE cap alone, isolated
// by reverting that one span while leaving the other two edits standing.
//
// THE TRAP THE MARKER CREATED, RECORDED BECAUSE IT SPRUNG: should_fail is
// satisfied by ANY failing assertion, so while it was on, this case was free
// to fail for a reason its own prose did not describe -- and for eight commits
// it did. From 3435c31 the ceiling breach was gone and the case was failing on
// the SILENCE FLOOR instead (master 0x404 at rms 7.0e-08, later 0x707 too),
// which nobody would have seen from the suite. A marker that declares one
// failure and accepts any is a marker that hides the next one; if this case
// ever needs declaring again, declare the specific assertion, not the case.
//
// NEITHER SIDE OF THIS GATE IS A GUARANTEE THE GENERATOR MAKES. Over masters
// 1..2000 (1 566 non-Sampler terrains) 0.51 % breach the ceiling and 6.58 %
// render at or below the silence floor, and this case was green only because
// none of the ten fixed seeds sat in either fraction -- the identical trap the
// NEW-blend level gate fell into when the taste tables moved two of its seeds.
//
// RULED 2026-08-07 (owner), BOTH FRACTIONS ACCEPTED, and §7.8 now states them.
// The population case below asserts the rates; what remains HERE is the
// asymmetric half of that ruling, and the asymmetry is the point:
//
//  - THE CEILING CHECK STAYS PER SEED, as a canary. A fixed seed crossing
//    0.06 is worth a look even under an accepted 0.51 % rate, and the rate
//    check alone cannot see it (at ~131 sampled terrains, 0.51 % is under one
//    expected breach -- see kCalmLoudFracMax).
//  - THE FLOOR CHECK IS GONE from this case. Once the mute fraction is
//    accepted, a fixed seed drifting into it is the accepted event occurring,
//    not news; a red test for it would be noise, and noise is what makes a
//    suite stop being read. The floor's whole claim is now the rate.
//
// See taste.h's kCalmCornerRmsMax / kCalmCornerRmsMin for the ruling and every
// number behind it, which is why none of them are restated here.
TEST_CASE("flow audio: calm corner sits under the ceiling on every fixed seed (7.8)") {
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
        park_calm(fl);

        // Skip the first 3 s (reverb/envelope tails from wake's boot state)
        // per the brief; 10 s total.
        const RenderStats rs = render_flow(fl, inst, 10.0, 3.0);
        CHECK_FALSE(rs.has_nan);
        CHECK(rs.rms <= kCalmCornerRmsMax);
    }
}

// ---------------------------------------------------------------------------
// The coverage park_calm() costs back (2026-08-12). Parking M_PACE on its
// neutral is right for every gate above -- they measure LEVEL, and a
// time-stretch has no calm end -- but the consequence is that no audio case
// renders PACE anywhere except x1. The two ends of pace_mult are five octaves
// of tempo apart and the slow one is where the arithmetic is uncomfortable: at
// x1/32 a lane's base_hz falls to ~1.4e-3 Hz, which is the exact reason Task 3
// moved the phase accumulator to double, and the transport's grid servo is
// aiming at a target 32x away from where it started.
//
// THIS ASSERTS FINITENESS AND NOTHING ELSE, deliberately. A level or mute bound
// on one fixed seed at the slowest setting in the range would be a number
// fitted to that seed, and the first taste-table edit that moved it would be
// answered by widening the bound -- the shape this file has already refused
// three times (kBlendSpikeDb, kCalmCornerRmsMax, kBlendGateWindowS all carry
// the refusal). "The audio is finite" is a claim that is true or false, never
// nearly true, so it can never be tuned.
//
// The reference render is what keeps it from being vacuous: finiteness passes
// trivially on an all-zero buffer, so the same terrain at the same macros with
// PACE neutral must first be shown to make sound at all. Macros sit at 0.5,
// not 0 -- that is the "busy" setting kFixedSeedRmsMin/Max bound to
// 0.0306..0.1211 across these candidates, so the reference is audible by a
// measurement this file already carries, not by luck.
//
// WHERE THIS GATE'S TEETH ACTUALLY ARE, established by sabotage rather than
// assumed, because the answer was not the expected one. It was PROVEN RED --
// a NaN injected into the master limiter on the slow half of the curve trips
// `has_nan` and `isfinite(rms)` on the x1/32 render alone, leaving x1 and x4
// clean, so the wiring observes what it claims and is scoped to the extreme.
// But three sabotages that looked like plausible pace regressions did NOT
// redden it, and that is worth knowing:
//
//   - an unguarded reciprocal in the pace curve (inf `_pace`)      -> finite
//   - a rate/period inversion in ModLane::_update_inc (inf phase)  -> finite
//   - a NaN `_bpm` pushed from set_pace                            -> finite
//
// Three guards absorb all three: Instrument::set_pace rejects non-finite
// input, Transport::clock_pulse rejects a non-positive or non-finite pace, and
// ModLane::set_rate_hz maps anything not > 0 (NaN included) to 0. The third
// one also explains the last row -- that terrain runs FLOW, where free_hz is
// the clock and BPM never reaches a lane at all. So this case is not really
// asserting that the pace arithmetic is safe; it is asserting that those
// guards are still standing. Read a failure here as "one of them went",
// not as "the curve is wrong".
TEST_CASE("flow audio: an extreme PACE still renders finite audio") {
    uint32_t kept[kMaxKept];
    const int n = filtered_masters(kept);
    REQUIRE(n > 0);
    const uint32_t master = kept[0];
    CAPTURE(master);

    // skip_s = 0: NaN is checked over the whole render anyway, but peak and rms
    // are wanted over all of it too -- at x1/32 the interesting part may well
    // be the first seconds, before anything has had time to move.
    auto render_at = [&](float pace) {
        Instrument inst;
        FxMem mem = flow_audio_fx_mem();
        inst.init(kSr, mem);
        Flow fl; fl.init(&inst, kCtrlHz);
        TerrainState st; st.master = master;
        fl.wake(st);
        for (int m = 0; m < MACRO_COUNT; ++m)
            fl.set_macro(m, m == M_PACE ? pace : 0.5f);
        return render_flow(fl, inst, 10.0);
    };

    const RenderStats ref = render_at(0.5f);          // x1, the reference
    REQUIRE(std::isfinite(ref.peak));
    REQUIRE(ref.peak > 0.f);                          // non-vacuity, see above

    const RenderStats slow = render_at(0.f);          // x1/32
    const RenderStats fast = render_at(1.f);          // x4
    MESSAGE("extreme PACE, master " << master
            << ": x1 peak=" << ref.peak << " rms=" << ref.rms
            << " | x1/32 peak=" << slow.peak << " rms=" << slow.rms
            << " | x4 peak=" << fast.peak << " rms=" << fast.rms);

    CHECK_FALSE(slow.has_nan);
    CHECK(std::isfinite(slow.peak));
    CHECK(std::isfinite(slow.rms));
    CHECK_FALSE(fast.has_nan);
    CHECK(std::isfinite(fast.peak));
    CHECK(std::isfinite(fast.rms));
}

// ---------------------------------------------------------------------------
// §7.8 calm corner as a rate over a population (owner ruling, 2026-08-07 --
// taste.h's kCalmMuteFracMax / kCalmLoudFracMax carry the ruling and every
// number behind it).
//
// Same two-claims-of-different-kind shape as the NEW-blend level gate:
//
//  - THE MEDIAN TERRAIN'S CALM CORNER IS AUDIBLE AND QUIET -- strictly inside
//    both bounds. This is where "receding but present" (§3) is enforced as a
//    property rather than tolerated as a rate, and it goes red if the typical
//    terrain drifts either way, whatever the tails do.
//  - THE TWO TAILS DO NOT GROW past the accepted fractions.
//
// THE POPULATION IS A STRIDE SUBSAMPLE and that is a real compromise, recorded
// rather than hidden: rendering all 1 566 the way this measures takes 115 s.
// The accepted fractions were set from that FULL measurement (taste.h), and
// the case reports both its own rate and what the full population read, so a
// subsample that stops representing it is visible in the log rather than
// silently reassuring.
//
// RED PROVEN, ALL FOUR CHECKS, 2026-08-07 (repo rule). The mute rate is shown
// red BY A MECHANISM, not only by walking bounds: reverting the drone SHAPE
// cap (P_SHAPE_A/B drone span {0,.25} -> {0,1}) -- the one edit taste.h
// isolates as what moved the mute population in the first place --
//
//   as shipped                 median 1.51e-03  mute  5.84 %  loud 1.46 %
//   drone SHAPE cap reverted   median 1.37e-03  mute 18.98 % RED  loud 2.92 %
//
// so the mute check catches the exact change that created the finding it now
// tolerates. The loud rate moved with it (1.46 -> 2.92 %) without crossing
// 5 %, which is the looseness kCalmLoudFracMax documents rather than a gap.
// The two MEDIAN checks track the mechanism (1.51e-03 -> 1.37e-03) without
// crossing bounds two orders of magnitude away, so they were separately shown
// live by walking the bounds inward to 2e-03 / 1e-03 against unmutated audio:
// both went red on the real median. Everything was restored; only this table
// and the numbers in taste.h survive.
TEST_CASE("flow audio: calm corner holds as a rate over the population (7.8)") {
    constexpr int kPopCap = 512;
    static double rms[kPopCap];
    int pop = 0;

    for (uint32_t master = 1; master <= kBlendPopScanMax && pop < kPopCap; ++master) {
        if ((master - 1u) % kCalmPopStride != 0u) continue;      // even subsample
        TerrainState st; st.master = master;
        if (terrain_has_sampler(generate(st))) continue;         // see file header

        Instrument inst;
        FxMem mem = flow_audio_fx_mem();
        inst.init(kSr, mem);
        Flow fl; fl.init(&inst, kCtrlHz);
        fl.wake(st);
        park_calm(fl);

        // Identical render shape to the per-seed case above -- 10 s, first 3 s
        // skipped -- so the rate and the canary measure the same quantity.
        const RenderStats rs = render_flow(fl, inst, 10.0, 3.0);
        CHECK_FALSE(rs.has_nan);
        rms[pop++] = rs.rms;
    }

    REQUIRE(pop >= kCalmPopMin);
    REQUIRE(pop < kPopCap);

    int mute = 0, loud = 0;
    for (int i = 0; i < pop; ++i) {
        if (rms[i] <= double(kCalmCornerRmsMin)) ++mute;
        if (rms[i] >= double(kCalmCornerRmsMax)) ++loud;
    }
    const double mute_frac = double(mute) / double(pop);
    const double loud_frac = double(loud) / double(pop);

    static double sorted[kPopCap];
    for (int i = 0; i < pop; ++i) sorted[i] = rms[i];
    std::sort(sorted, sorted + pop);
    const double median = (pop & 1) ? sorted[pop / 2]
                                    : 0.5 * (sorted[pop / 2 - 1] + sorted[pop / 2]);

    MESSAGE("calm corner population: n=" << pop << " (stride " << kCalmPopStride
            << ") median=" << median
            << " mute=" << mute << " (" << 100.0 * mute_frac << "%)"
            << " loud=" << loud << " (" << 100.0 * loud_frac << "%)"
            << " -- full population at HEAD reads 6.58% mute, 0.51% loud");

    // The typical terrain, held to both bounds as a property.
    CHECK(median > double(kCalmCornerRmsMin));
    CHECK(median < double(kCalmCornerRmsMax));
    // The accepted tails, held as regression bounds.
    CHECK(mute_frac <= double(kCalmMuteFracMax));
    CHECK(loud_frac <= double(kCalmLoudFracMax));
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
// this narrows WHAT THE GATE CLAIMS, not how much it tolerates.
//
// THE PER-SEED LEVEL GATE THAT LIVED HERE IS GONE (owner ruling, 2026-08-07).
// It went red at Task 7 on masters 0xD0D and 0xC0C0 and could not be made
// green by any honest measurement: the 6 dB ceiling is a §5 spec claim about
// what a crossfade may do, not a property the generator has -- more than a
// quarter of eligible terrains breach it, at the branch point as well as at
// HEAD. Four fixed seeds cannot assert a claim like that; they only sample it,
// and the taste tables moved two of them into the breaching third.
//
// So §7.8's level comparison is now a DISTRIBUTION check over a computed
// population (the case below this one), exactly the second of the two exits
// taste.h's kBlendSpikeDb comment has named since Task 7. No bound, no window
// and no seed was moved to get there: kBlendSpikeDb and kBlendDropDb are
// unchanged at 6 and 10 dB, and 0xD0D/0xC0C0 are still in kCandidateMasters,
// still measured, still breaching -- they are simply no longer mistaken for
// the population. THE FIGURES AND THE FULL ARGUMENT LIVE IN ONE PLACE,
// taste.h, and are deliberately not restated here so the two copies cannot
// drift.
//
// WHAT STAYS PER-SEED, AND WHY, is this case: NaN over the FULL 6 s blend, on
// every candidate seed including the Sampler-filtered and engine-switching
// ones. NaN is not a distribution question -- one is a defect -- and the
// exclusion that scoped the level comparison never applied to it.
//
// A should_fail() MARKER WAS NOT ADDED, then or now. The calm-corner case
// above carried one and it was retired for having become a catch-all: it
// accepted any failing assertion and so hid a different defect for eight
// commits. Declaring this case that way would have repeated it exactly.
//
// THE GATE WINDOW, unchanged and still the scope of the level comparison in
// the distribution case below. RE-MEASURED 2026-08-06 after the operating-mode
// work (spec 2026-08-06 §5). The headlines, qualitatively -- THE FIGURES
// BEHIND THEM LIVE IN ONE PLACE, taste.h's kBlendGateWindowS / kBlendSpikeDb /
// kBlendDropDb comments, and are deliberately not restated here so the two
// copies cannot drift:
//
//  - kBlendGateWindowS is still a CONSERVATIVE CHOICE, not a derivation, but
//    the failure boundary has now reached the window itself.
//  - THE BLIND SPOT NARROWED but did not close. On a mode-changing press the
//    carrier deck is now ducked AT THE PRESS as well (set_sync is global, so
//    the clocking flip lands at phase 0), and that duck IS inside this
//    window -- two of the four asserted seeds take that path. The carrier's
//    own engine/scale switch at kCarrierStaggerFrac * kBlendS and its stagger
//    duck are still entirely outside it, for every seed, by construction.
//
// So this gate covers the texture deck's switch, the initial retarget, and
// (new) the carrier's clocking flip on a mode change. It does NOT cover the
// carrier deck's engine switch or stagger duck -- read taste.h before
// assuming it does.
//
// The full 6 s is still RENDERED and NaN-checked for every seed by the case
// immediately below, including the Sampler-filtered and engine-switching
// ones; the level comparison, scoped to this window, moved to the population
// case after it.
//
// EXCLUSION (measured, not carved around -- see task-10-report.md round 2
// for the full per-seed numbers): seeds whose new_full() switches a deck's
// engine mid-blend dip well below this bound. RE-MEASURED 2026-08-06: worst
// case is now master 0x303 at 31.73 dB below the no-press control with 1.50 s
// of the 6 s blend spent more than 20 dB below it (0x101, the pre-mode worst,
// is 31.58 dB / 1.50 s). The dip got markedly shallower and shorter than the
// pre-mode figures this paragraph used to quote (65.16 dB, 4.00 s) -- the
// mode routing fix and the second carrier duck both land here -- but it is
// still far outside the bound, so the exclusion stands.
//
// RULED 2026-08-07 (owner): ACCEPTED, not pending. This carried "KNOWN AND
// ACCEPTED FOR NOW, pending a listening-loop decision (Bastian, 2026-08-05)"
// for two days; the decision is made and the qualifier is gone. A brief
// near-silence when a NEW press switches engines is a PROPERTY of the
// instrument, not a defect on a list -- the fix direction was never obvious
// for free, since an engine-switch crossfade means running two part engines
// at once, which is expensive on the Daisy target, and the measured dip has
// been getting shallower and shorter on its own (65.16 dB / 4.00 s before the
// mode work, 31.73 dB / 1.50 s now). Nothing here is waiting on a listen.
//
// So the LEVEL comparison is
// scoped to terrains new_full() does NOT switch an engine on, honestly (no
// clause inside the gate pretends to cover the switch case); NaN checks
// still run on excluded seeds too (they have nothing to do with the level
// comparison).
//
// UNDER THE DISTRIBUTION RULING THIS EXCLUSION BECAME THE POPULATION
// DEFINITION rather than a per-seed skip, and that is a strict improvement in
// honesty: it is now applied to every master in 1..kBlendPopScanMax by the
// same rule, instead of thinning a hand-picked list of ten down to whichever
// four survived. kMinNoSwitchSeeds and its review-I-4 argument retired with
// the per-seed case -- kBlendPopMin is the same guard at population scale, and
// a stronger one, because a filter that starved the set trips it long before
// the set gets small enough to assert nothing.
TEST_CASE("flow audio: a NEW blend renders without NaN, every candidate seed (7.8)") {
    uint32_t kept[kMaxKept];
    const int n = filtered_masters(kept);
    REQUIRE(n > 0);

    constexpr double kWindowS = 0.25;
    const int windows = int(kBlendS / kWindowS + 0.5);   // 24, full blend

    for (int i = 0; i < n; ++i) {
        const uint32_t master = kept[i];
        CAPTURE(master);
        // No exclusion here: NaN is not a distribution question, and the
        // engine-switch exclusion was only ever about the level comparison.
        TerrainState seed_state; seed_state.master = master;

        // The no-press control run is NOT rendered here. It exists to cancel
        // native dynamics out of a LEVEL comparison, and there is none in this
        // case; as a NaN subject it is a plain settled render with macros at
        // 0.5, which the fixed-seed case above already covers on these same
        // ten seeds. Rendering it again would double this case's cost to
        // re-assert that.
        Instrument p_inst;
        FxMem p_mem = flow_audio_fx_mem(1);
        p_inst.init(kSr, p_mem);
        Flow p_fl; p_fl.init(&p_inst, kCtrlHz);
        p_fl.wake(seed_state);
        for (int m = 0; m < MACRO_COUNT; ++m) p_fl.set_macro(m, 0.5f);

        const RenderStats settle = render_flow(p_fl, p_inst, 6.0, 6.0);
        CHECK_FALSE(settle.has_nan);

        p_fl.new_full();
        REQUIRE(p_fl.blend_phase() < 1.f);

        for (int w = 0; w < windows; ++w) {
            const RenderStats ps = render_flow(p_fl, p_inst, kWindowS);
            CAPTURE(w);
            CHECK_FALSE(ps.has_nan);
        }
    }
}

// ---------------------------------------------------------------------------
// §7.8 NEW-blend LEVEL gate, as a rate over a population (owner ruling,
// 2026-08-07 -- see taste.h's kBlendSpikeBreachFracMax for the ruling itself
// and every number behind it, which is why none of them are restated here).
//
// TWO CLAIMS, DELIBERATELY DIFFERENT IN KIND, because one distribution figure
// alone would hide the other's failure:
//
//  - THE MEDIAN TERRAIN STILL HOLDS THE SPEC BOUND. kBlendSpikeDb /
//    kBlendDropDb are unchanged and still mean what §5 says a crossfade may
//    do; the median is where that claim is enforced. If the typical press
//    starts jumping the level, this goes red even while the breaching
//    fraction sits still.
//  - THE TAIL MUST NOT GROW. The fraction past each bound is compared to the
//    tolerated fraction. If the tail widens without moving the median -- which
//    is exactly what the taste tables did to the old fixed seeds -- this goes
//    red even while the median sits still.
//
// The population is COMPUTED, not enumerated (kBlendPopScanMax, the same range
// every figure in taste.h was measured over), so it cannot be trimmed to dodge
// a failure the way a seed list can, and REQUIRE(pop >= kBlendPopMin) plus the
// cap assertion below make a starved or truncated population a failure rather
// than a trivially green rate.
//
// COST: this renders ~85 terrains twice through a 6 s settle plus the gate
// window, about 10 s -- the most expensive case in the suite by a wide margin,
// and knowingly so. A rate is the thing being asserted; four seeds could not
// assert it, which is how the gate came to be wrong in the first place.
//
// RED PROVEN, ALL FOUR CHECKS, 2026-08-07 (repo rule: a test that cannot go
// red gets fixed). Measured, not argued:
//
//   mutation                       spike med / frac    drop med / frac
//   (none, HEAD)                     2.18 / 28.2 %       2.06 /  7.1 %
//   kBlendS 6 -> 0.4  (near-instant) 4.05 / 37.6 % RED   4.82 / 16.5 % RED
//   kBlendS 6 -> 0.02                3.49 / 37.6 % RED   5.07 / 27.1 % RED
//   kDuckDepth 0.8 -> 0              2.14 / 28.2 %       0.90 /  5.9 %
//
// THE TWO RATE CHECKS CATCH A MECHANISM CHANGE; the two MEDIAN checks moved
// under it (2.18 -> 4.05, 2.06 -> 4.82: they track the mechanism, they are not
// dead reads) but did not cross their bounds, so they were separately shown to
// fire by walking the bounds down to 2 dB / 1 dB against unmutated audio --
// both went red on the real medians. Bounds and kDuckDepth were restored; only
// this table survives.
//
// kDuckDepth -> 0 is recorded because it made the gate GREENER (drops 7.1 % ->
// 5.9 %), which is the expected direction -- the duck is what digs the drops --
// and is exactly why the drop side alone would be a poor mutation target.
TEST_CASE("flow audio: NEW-blend level holds as a rate over the population (7.8)") {
    constexpr double kWindowS = 0.25;
    const int gated_windows = int(double(kBlendGateWindowS) / kWindowS + 0.5); // 4

    // Rig detail, not a tuning number: storage for the per-terrain extremes.
    // Asserted below to have NOT been reached, so a population that outgrows
    // it fails loudly instead of silently measuring a truncated prefix.
    constexpr int kPopCap = 256;
    static double spikes[kPopCap], drops[kPopCap];
    int pop = 0;

    for (uint32_t master = 1; master <= kBlendPopScanMax && pop < kPopCap; ++master) {
        TerrainState st; st.master = master;
        if (terrain_has_sampler(generate(st))) continue;      // silent deck, see header
        if (new_full_switches_engine(master)) continue;       // see EXCLUSION above

        Instrument c_inst;
        FxMem c_mem = flow_audio_fx_mem(0);
        c_inst.init(kSr, c_mem);
        Flow c_fl; c_fl.init(&c_inst, kCtrlHz);
        c_fl.wake(st);
        for (int m = 0; m < MACRO_COUNT; ++m) c_fl.set_macro(m, 0.5f);

        Instrument p_inst;
        FxMem p_mem = flow_audio_fx_mem(1);
        p_inst.init(kSr, p_mem);
        Flow p_fl; p_fl.init(&p_inst, kCtrlHz);
        p_fl.wake(st);
        for (int m = 0; m < MACRO_COUNT; ++m) p_fl.set_macro(m, 0.5f);

        // 6 s identical settle: both instances share identical inputs up to
        // this point (same master, same macro pushes, same tick clock), so
        // this renders bit-for-bit identically -- only the press run
        // diverges, and only once new_full() actually fires below.
        render_flow(c_fl, c_inst, 6.0, 6.0);
        render_flow(p_fl, p_inst, 6.0, 6.0);

        p_fl.new_full();
        REQUIRE(p_fl.blend_phase() < 1.f);

        double worst_spike = -1e9, worst_drop = -1e9;
        for (int w = 0; w < gated_windows; ++w) {
            const RenderStats cs = render_flow(c_fl, c_inst, kWindowS);
            const RenderStats ps = render_flow(p_fl, p_inst, kWindowS);
            const double diff_db = level_db(ps.rms) - level_db(cs.rms);
            if (diff_db  > worst_spike) worst_spike = diff_db;
            if (-diff_db > worst_drop)  worst_drop  = -diff_db;
        }
        spikes[pop] = worst_spike;
        drops[pop]  = worst_drop;
        ++pop;
    }

    // Non-vacuity, both directions: the population must be wide enough for a
    // rate to mean anything, and must not have been truncated by the rig.
    REQUIRE(pop >= kBlendPopMin);
    REQUIRE(pop < kPopCap);

    int spike_breach = 0, drop_breach = 0;
    for (int i = 0; i < pop; ++i) {
        if (spikes[i] > double(kBlendSpikeDb)) ++spike_breach;
        if (drops[i]  > double(kBlendDropDb))  ++drop_breach;
    }
    const double spike_frac = double(spike_breach) / double(pop);
    const double drop_frac  = double(drop_breach)  / double(pop);

    static double sorted[kPopCap];
    auto median_of = [&](const double* src) {
        for (int i = 0; i < pop; ++i) sorted[i] = src[i];
        std::sort(sorted, sorted + pop);
        return (pop & 1) ? sorted[pop / 2]
                         : 0.5 * (sorted[pop / 2 - 1] + sorted[pop / 2]);
    };
    const double spike_median = median_of(spikes);
    const double drop_median  = median_of(drops);

    MESSAGE("blend level population: n=" << pop
            << " spike median=" << spike_median << " breach=" << spike_breach
            << " (" << 100.0 * spike_frac << "%)"
            << " drop median=" << drop_median << " breach=" << drop_breach
            << " (" << 100.0 * drop_frac << "%)");

    // The spec bound, enforced where it is a true claim.
    CHECK(spike_median <= double(kBlendSpikeDb));
    CHECK(drop_median  <= double(kBlendDropDb));
    // The tolerated tail, enforced as a regression bound.
    CHECK(spike_frac <= double(kBlendSpikeBreachFracMax));
    CHECK(drop_frac  <= double(kBlendDropBreachFracMax));
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
