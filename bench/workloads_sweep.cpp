#include "workload.h"
#include "mem.h"
#include "serial_arena.h"
#include "instrument.h"
#include "fx/bbd.h"
#include "fx/flux.h"
#include "fx/grit.h"
#include "fx/part_fx.h"

namespace bench {
namespace {

using namespace spky;

// This family's own arena. workloads_system.cpp's g_system_arena is a static
// in an ANONYMOUS namespace -- unreachable from here by design, and exporting
// it would mean editing workloads_system.cpp, which perturbs exactly the code
// layout this round is investigating (spec S4.6).
//
// SerialArena overlays its groups: `capacity` is the max sizeof, not the sum.
// So the cost of this second arena is one more max-sized .bss block, and
// SweepInstrumentGroup is what sets that maximum. Task 1 measured the delta
// before the rows were written -- see the plan.
struct SweepInstrumentGroup {
    Instrument instrument;
    float out_l[kBlock], out_r[kBlock];
};

// --- Sweep A: cost against the FLUX division ladder --------------------------
// engine/mod/divisions.h: kFluxRateCount == 12. At bpm=120 (set below, mirroring
// setup_fx) and the boot stage count (8192 -- kBootStagesNorm=0.8 in flux.cpp,
// 512*32^0.8 == 512*16 == 8192, and this row never calls set_stages so it stays
// there), each rate index's target repeat rate is (bpm/60)*kDivisions[5+idx].cpb
// and the BBD clock that produces is stages/(2*t) = 4096/t Hz, clamped to
// kClockMaxHz (32000). The full ladder (all 12 indices, unclamped clock):
//   0: 4096 Hz    1: 5461.3 Hz   2: 6144 Hz    3: 8192 Hz    4: 10922.7 Hz
//   5: 12288 Hz   6: 16384 Hz    7: 21845.3 Hz 8: 24576 Hz
//   9: 32768 Hz -> clamped     10: 49152 Hz -> clamped     11: 65536 Hz -> clamped
// Indices 9, 10 and 11 all clamp to the SAME 32000 Hz -- rate 8 (24576 Hz) is
// the highest rung whose unclamped clock still sits strictly below the
// ceiling, i.e. the last rung before it engages. The five sampled points:
//   rate 0  (cpb=1/2): t=1.0s    -> clock 4096 Hz
//   rate 3  (cpb=1  ): t=0.5s    -> clock 8192 Hz
//   rate 6  (cpb=2  ): t=0.25s   -> clock 16384 Hz
//   rate 8  (cpb=3  ): t=0.16667s-> clock 24576 Hz (last unclamped rung)
//   rate 11 (cpb=8  ): t=0.0625s -> clock 65536 Hz -> clamped to 32000 Hz
// rate 8 replaces an earlier choice of rate 9: rate 9's unclamped clock
// (32768 Hz) already clamps to the same 32000 Hz as rate 11, so the two rows
// would have returned the same clock, same cost and the same number twice --
// four distinct points instead of five, with the knee between rate 6 (last
// point below the clamp region) and the ceiling left unmeasured. rate 8 (1.33
// ticks per audio sample only at rate 11; 1.024 at rate 8) pins exactly where
// the ceiling starts biting instead. A prior draft of this row also zeroed
// values[FXT_FLUX_TIME], which bbd_time_mult() maps to x0.25 (engine/fx/bbd.h),
// quartering every row's real clock and hiding this ceiling entirely; fixed by
// mirroring setup_fx's values[] array exactly (see setup_flux_rate below).
struct SweepFxGroup {
    PartFx fx;
    float  values[FXT_COUNT];
    // The integer stage count Flux actually settled to, read once after each
    // setup's settle loop (flux().stages(), engine/fx/flux.h) and folded into
    // proc_sweep_fx's returned accumulator once per call -- see the comment
    // there. Every setup that uses this group must set it: the five
    // sweep_flux_rate_* rows never call set_stages, so for them it reads
    // back whatever Flux::init's boot default (8192) settled to, which is
    // well-defined and just as worth checksumming as the four sweep_stages_*
    // rows' explicit settings.
    int stages_achieved = 0;
};

// --- Ablation F: where did fx_grit's 2.9 points come from? -------------------
// fx_grit rose 4.78 -> 7.70 % max between 518f639 and 1f7671d at an IDENTICAL
// checksum, with no commits to engine/fx/grit.{cpp,h} in range and fx_none
// unmoved. Three candidates: GRIT itself, the PartFx shell, or cache pressure
// from the BBD buffers merely being resident.
//
// fx_grit - fx_none is 5.14 today against a historical 2.22.
//   sweep_grit_bare ~ 5.14                 -> GRIT costs that; the old figure
//                                             was a smaller image, i.e. layout
//   sweep_grit_bare ~ 2.2, no_bbd_mem ~5.14 -> the shell is the suspect
//   sweep_grit_no_bbd_mem ~ 2.2             -> consistent with cache pressure
//                                             from the BBD buffers, but NOT
//                                             a clean isolation of it: the
//                                             same _buf_ok guard that keeps
//                                             the memory unresident also
//                                             elides a per-sample std::pow
//                                             in set_feedback/apply_feedback
//                                             (see the comment on
//                                             setup_grit_no_bbd_mem below) --
//                                             a low reading is ambiguous
//                                             between the two and cannot be
//                                             split further without an
//                                             engine/ change this task does
//                                             not permit
struct SweepGritGroup {
    Grit grit;
};

// --- Ablation E: the Flux wrapper's own cost ---------------------------------
// Two bare BbdEcho, at the stage count and clock a default-initialised Flux
// computes, with no Flux around them. Then
//   fx_flux_sdram - sweep_flux_lines_2ch - fx_none
// is the wrapper's own per-sample work: two fonepole slews, two std::fabs
// snaps, a clampf and bbd_clock_hz's division -- all of which run every
// sample although their inputs only move on the 96-sample control tick.
//
// That is NOT the whole of what this row isolates, though. PartFx::process
// calls _flux.set_feedback(v[FXT_FLUX_FB]) unconditionally, every sample
// (part_fx.cpp:38), and that reaches Flux::apply_feedback (flux.cpp:132),
// which calls bbd_drive_gain -- a std::pow every sample (bbd.h:192), priced
// by this repo's own bench at 198 cycles (fx_pow, workloads_system.cpp).
// It is the LARGEST single item inside the number this row makes
// measurable, not a footnote next to the slews -- and it is left exactly as
// it is: fixing it is out of scope for this task, however tempting a
// one-line dirty check would be. This task makes no engine/ change at all --
// Flux::clock_hz() (engine/fx/flux.h) already existed before this plan and
// already does what a test-only clock accessor would, so the plan's
// conditional permission to add one never fires.
//
// clock_hz and the stage count come from a throwaway probe Flux rather than
// being hard-coded, so this row cannot silently drift from what
// fx_flux_sdram (and setup_flux_rate/setup_stages above) actually run.
// fx_flux_sdram (bench/workloads_system.cpp's setup_fx(SEL_FLUX)) never
// calls set_stages or set_flux_rate away from Flux::init's boot defaults --
// bpm 120, _rate_idx 3 ("1/4"), kBootStagesNorm 0.8 -> 512*32^0.8 == 8192
// stages (flux.cpp) -- so a freshly-init'd probe, left untouched, lands on
// exactly those numbers: clock 8192 Hz (the rate-3 row of the Sweep A
// ladder table above already derives this) and 8192 stages. Neither
// set_bpm(120) nor set_flux_rate(3) needs to be called on the probe: those
// are already init()'s defaults.
//
// clock_hz() (engine/fx/flux.h -- pre-existing, already used throughout
// tests/test_flux.cpp) only reads back _clock_hz, which Flux computes once
// per process() call (flux.cpp, after the delay-time/stage slews) from
// _dt_current and the stage count -- both of which init() already snaps
// straight to their targets (immediate = true), with no slew left to run.
// So a single process() call is enough for the probe's clock to be
// meaningful; no settling loop is needed on the probe itself.
//
// BbdEcho::Init leaves cells_ at its full capacity (Flux::kMaxSamples ==
// 8192 cells, i.e. a 16384-stage buffer) unless SetStages narrows it --
// DOUBLE the boot-default Flux's actual footprint (8192 stages == 4096
// cells). Sweep B above exists because stage count is not free on this
// part; left uncorrected here, that mismatch would leak a buffer-size
// confound into the very subtraction this row exists to keep clean. The
// explicit SetStages(stages) calls below, using the probe's own achieved
// stage count, remove it.
//
// Two SEPARATE echo buffer pairs: the probe takes fx_mem()'s PART_B pair
// (m.echo[1][*]) for its one throwaway process() call, and the two measured
// bare lines take PART_A's pair (m.echo[0][*], the same pair
// setup_flux_rate/setup_stages above use for their PartFx) -- so the probe
// cannot disturb the memory the measured lines settle into and then read
// from.
//
// Settle: _rate_idx 3 at bpm 120 is the SAME division Sweep B holds fixed
// (see the comment on setup_stages above), for the same reason -- the clock
// stays unclamped (8192 Hz, comfortably under kClockMaxHz's 32000), so a
// full line-fill takes exactly the division's own period regardless of
// stage count: stages/(2*(stages/(2t))) = t = 0.5 s = 24000 samples at
// 48 kHz. Four fills (this file's standing settle-before-measuring
// precedent -- kSweepFluxSettleSamples and kSweepStagesSettleSamples above,
// setup_bbd_ceiling in workloads_bbd.cpp) is 96000 samples -- the same
// number as kSweepStagesSettleSamples, and for the identical reason, but
// named separately here since it is this row's own derivation and not a
// reuse of Sweep B's group.
constexpr int kSweepFluxLinesSettleSamples = 96000;

struct SweepLineGroup {
    BbdEcho l, r;
    float   clock_hz;
};

SerialArena<SweepInstrumentGroup, SweepFxGroup, SweepGritGroup, SweepLineGroup> g_sweep_arena;

// A trivial row whose only job is to prove the family links and registers.
// It is replaced by real rows in later tasks and must not survive to the
// hardware run.
void setup_sweep_probe()
{
    auto& group = g_sweep_arena.emplace<SweepInstrumentGroup>();
    group.instrument.init(kSampleRate, fx_mem());
}

float proc_sweep_probe()
{
    auto& group = g_sweep_arena.get<SweepInstrumentGroup>();
    const float* in = test_input();
    group.instrument.process(in, in, group.out_l, group.out_r, kBlock);
    return group.out_l[0] + group.out_r[0];
}

// Four line-fills of the SLOWEST of the five sampled divisions (rate 0), the
// same "settle before measuring" precedent workloads_bbd.cpp's
// setup_bbd_ceiling documents and uses (49152 = 4 * 16384/(2*32000)*48000).
// Here stages=8192 and the actual (post-ceiling-clamp) BBD clock differs per
// rate -- see the derivation on SweepFxGroup above -- so the fill time
// stages/(2*clock_actual) differs per row and only rate 0 (the longest delay,
// hence the longest fill) sets the bound all five rows share:
//   rate 0:  clock 4096  Hz -> fill 8192/(2*4096)  = 1.000 s   = 48000 samples
//   rate 3:  clock 8192  Hz -> fill 8192/(2*8192)  = 0.500 s   = 24000 samples
//   rate 6:  clock 16384 Hz -> fill 8192/(2*16384) = 0.250 s   = 12000 samples
//   rate 8:  clock 24576 Hz -> fill 8192/(2*24576) = 0.16667 s =  8000 samples
//   rate 11: clock 32000 Hz -> fill 8192/(2*32000) = 0.128 s   =  6144 samples
// Four times the largest (rate 0's 48000): 192000 samples, used for every row
// so all five settle at least as long as their own four-fill requirement.
constexpr int kSweepFluxSettleSamples = 192000;

void setup_flux_rate(int rate_index)
{
    auto& group = g_sweep_arena.emplace<SweepFxGroup>();
    const FxMem& m = fx_mem();
    group.fx.init(kSampleRate, m.echo[0][0], m.echo[0][1]);
    group.fx.set_fx_on(FxBlock::Grit, false, true);
    group.fx.set_fx_on(FxBlock::Flux, true,  true);
    group.fx.set_comp(0.f);
    group.fx.set_grit_mix(1.f);
    group.fx.set_flux_mix(1.f);
    group.fx.set_bpm(120.f);
    group.fx.set_flux_rate(rate_index);

    // Mirrors setup_fx's values[] exactly (bench/workloads_system.cpp) -- same
    // already-modulated target values a real Part::fx_target_value() would
    // hand over. FXT_FLUX_TIME = 0.5f is the neutral point of bbd_time_mult()
    // (engine/fx/bbd.h: 0 -> x0.25, 0.5 -> x1, 1 -> x4) -- 0.f, used by an
    // earlier draft of this row, quarters the real clock instead.
    group.values[FXT_GRIT_INT]  = 0.8f;
    group.values[FXT_FLUX_TIME] = 0.5f;
    group.values[FXT_FX_MIX]    = 1.f;
    group.values[FXT_REV_SEND]  = 0.5f;
    group.values[FXT_FLUX_FB]   = 0.7f;

    // Settle OUTSIDE the measured window: an unsettled BBD line measures an
    // empty machine, consistently across both runs, so the checksum gate
    // cannot catch it. See kSweepFluxSettleSamples above for the derivation.
    const float* in = test_input();
    for (int i = 0; i < kSweepFluxSettleSamples; ++i) {
        float l = in[i % kBlock], r = l * 0.9f, sl = 0.f, sr = 0.f;
        group.fx.process(l, r, sl, sr, group.values);
    }
    // This row never calls set_stages -- stages_achieved reads back whatever
    // the boot default (8192, kBootStagesNorm in flux.cpp) settled to.
    group.stages_achieved = group.fx.flux().stages();
}

void setup_flux_rate_0()  { setup_flux_rate(0);  }
void setup_flux_rate_3()  { setup_flux_rate(3);  }
void setup_flux_rate_6()  { setup_flux_rate(6);  }
void setup_flux_rate_8()  { setup_flux_rate(8);  }
void setup_flux_rate_11() { setup_flux_rate(11); }

// --- Sweep B: cost against STAGES --------------------------------------------
// The hypothesis under test is CACHE, not arithmetic: stage count does not
// change the tick rate at a fixed clock, but it does change how much SDRAM
// the line walks against a 16 KB D-cache. That only holds if the clock is
// the SAME (and unclamped) across all four rows -- otherwise a clamped row's
// cost carries a clock effect on top of the memory effect and the curve
// cannot separate the two.
//
// Division held at 3, NOT 6 (the brief's number, wrong -- corrected here).
// Flux::recompute_time (engine/fx/flux.cpp) computes hz = division_hz(idx,
// bpm) and t = 1/hz; bbd_clock_hz (engine/fx/bbd.h) then computes
// stages/(2*t), clamped to kClockMaxHz = 32000. At bpm=120 (set below):
//   division 3 ("1/4", cpb=1):  hz = 2,   t = 0.5 s   -> clock = stages/1.0
//   division 6 ("1/8", cpb=2):  hz = 4,   t = 0.25 s  -> clock = stages/0.5
// (kFluxRateOffset=5, so slice index 6 resolves to kDivisions[11] = "1/8" --
// the name only, the cpb=2 used above is unaffected.)
// At division 6 the 16384-stage row computes 16384/(2*0.25) = 32768 Hz and
// CLAMPS to 32000 -- exactly the confound this row exists to avoid. At
// division 3 the same row computes 16384/(2*0.5) = 16384 Hz, comfortably
// under the ceiling, so all four points sit on the linear (unclamped) part
// of bbd_clock_hz and stages is the only variable:
//   512 stages   -> clock   512 Hz
//   2048 stages  -> clock  2048 Hz
//   8192 stages  -> clock  8192 Hz
//   16384 stages -> clock 16384 Hz
// (all four strictly below 32000 -- none clamp.)
//
// Flux::set_stages is geometric, 512 * 32^n (engine/fx/flux.cpp:156); the
// four norms below are its exact solutions for the requested stage counts
// (32^0.4 == 4, 32^0.8 == 16). tests/test_flux.cpp's "STAGES is geometric,
// 512 to 16384" case exercises this same mapping on a DIFFERENT Flux
// instance that happens to share the same boot defaults (division 3, bpm
// 120): settled_stages(0.f) == 512, (0.4f) ~= 2048, (0.8f) ~= 8192,
// (1.f) == 16384, and passed at this commit
// (`spky_tests.exe --test-case="flux: STAGES is geometric*"`, 4/4
// assertions). THIS row's own instance is checked directly: setup_stages
// reads flux().stages() after its own settle and stores it in
// SweepFxGroup::stages_achieved, and proc_sweep_fx folds that into its
// returned accumulator once per call -- so if this row's own achieved stage
// count ever drifted from what its name claims, the per-row checksum the
// bench compares across hardware runs would move and the run would be
// rejected, instead of the curve quietly acquiring a knee that isn't there.
//
// Settle length: at division 3 the clock is unclamped for every row, so a
// full line-fill takes exactly the division's own period regardless of
// stage count -- stages/(2*(stages/(2t))) = t = 0.5 s = 24000 samples at
// 48 kHz. Four fills (the same "settle before measuring" precedent
// kSweepFluxSettleSamples above and workloads_bbd.cpp's setup_bbd_ceiling
// use) is 4 * 24000 = 96000 samples, comfortably longer than the ~30 ms
// (1440-sample) slew Flux::process applies to both the delay time and the
// stage count, and used for all four rows since none of them clamp (unlike
// Sweep A, there is no "slowest row" to derive from -- every row's fill time
// is the same 0.5 s).
constexpr int kSweepStagesSettleSamples = 96000;

void setup_stages(float norm)
{
    auto& group = g_sweep_arena.emplace<SweepFxGroup>();
    const FxMem& m = fx_mem();
    group.fx.init(kSampleRate, m.echo[0][0], m.echo[0][1]);
    group.fx.set_fx_on(FxBlock::Grit, false, true);
    group.fx.set_fx_on(FxBlock::Flux, true,  true);
    group.fx.set_comp(0.f);
    group.fx.set_grit_mix(1.f);
    group.fx.set_flux_mix(1.f);
    group.fx.set_bpm(120.f);
    group.fx.set_flux_rate(3);      // division 3 ("1/4") -- see derivation above
    group.fx.set_stages(norm);

    // Mirrors setup_fx's values[] exactly (bench/workloads_system.cpp), same
    // as setup_flux_rate above: comparability with the sibling row
    // fx_flux_sdram is the reason a divergence here would silently break
    // later arithmetic.
    group.values[FXT_GRIT_INT]  = 0.8f;
    group.values[FXT_FLUX_TIME] = 0.5f;
    group.values[FXT_FX_MIX]    = 1.f;
    group.values[FXT_REV_SEND]  = 0.5f;
    group.values[FXT_FLUX_FB]   = 0.7f;

    // Settle OUTSIDE the measured window -- see kSweepStagesSettleSamples.
    const float* in = test_input();
    for (int i = 0; i < kSweepStagesSettleSamples; ++i) {
        float l = in[i % kBlock], r = l * 0.9f, sl = 0.f, sr = 0.f;
        group.fx.process(l, r, sl, sr, group.values);
    }
    // Read back what this row's own Flux instance actually settled to -- see
    // the comment above SweepFxGroup and the one on this row's block above.
    group.stages_achieved = group.fx.flux().stages();
}

void setup_stages_512()   { setup_stages(0.0f); }
void setup_stages_2048()  { setup_stages(0.4f); }
void setup_stages_8192()  { setup_stages(0.8f); }
void setup_stages_16384() { setup_stages(1.0f); }

float proc_sweep_fx()
{
    auto& group = g_sweep_arena.get<SweepFxGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float l = in[i], r = in[i] * 0.9f, sl = 0.f, sr = 0.f;
        group.fx.process(l, r, sl, sr, group.values);
        acc += l + r + sl + sr;
    }
    // Fold the achieved stage count into the checksum, once per call (not
    // per sample) so the added work is negligible and constant: a row whose
    // Flux settled to a different stage count than its name claims moves
    // the checksum the bench compares across hardware runs, instead of
    // silently measuring something other than what it says it measures.
    // Every SweepFxGroup setup sets stages_achieved (including the
    // sweep_flux_rate_* rows, which never call set_stages and so read back
    // Flux's boot default) -- see the comment on the struct.
    acc += static_cast<float>(group.stages_achieved);
    return acc;
}

// sweep_grit_bare: a bare Grit, no PartFx shell at all. Mirrors setup_fx
// (SEL_GRIT)'s Grit-facing calls exactly (bench/workloads_system.cpp) --
// set_grit_mix(1.f), intensity = values[FXT_GRIT_INT] = 0.8f, and the
// immediate on-switch -- so this measures GRIT alone, fully engaged, nothing
// else running.
void setup_grit_bare()
{
    auto& group = g_sweep_arena.emplace<SweepGritGroup>();
    group.grit.init(kSampleRate);
    group.grit.set_mix(1.f);
    group.grit.set_intensity(0.8f);
    group.grit.set_on(true, true);
}

float proc_grit_bare()
{
    auto& group = g_sweep_arena.get<SweepGritGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float l = in[i], r = in[i] * 0.9f;
        group.grit.process(l, r);
        acc += l + r;
    }
    return acc;
}

// sweep_grit_no_bbd_mem: fx_grit's exact shell (setup_fx(SEL_GRIT), mirrored
// verbatim below) with one change -- PartFx::init receives null echo memory
// instead of fx_mem()'s buffers. Flux::init (engine/fx/flux.cpp:11-17) sets
// _buf_ok = (buf_l && buf_r) and returns BEFORE calling _echo_l/_echo_r.Init
// when either pointer is null -- no BbdLine::Init runs, no dereference, no
// UB.
//
// IMPORTANT: this does NOT isolate memory residency alone. _buf_ok also
// gates real per-sample arithmetic that fx_grit pays for and this row does
// not. PartFx::process calls _flux.set_feedback(v[FXT_FLUX_FB])
// unconditionally, every sample, whenever GRIT or FLUX is engaged
// (part_fx.cpp:38) -- true here since GRIT is on. In fx_grit (_buf_ok
// true), Flux::set_feedback (flux.cpp:99) runs through to apply_feedback
// (flux.cpp:132), which calls bbd_drive_gain -- a std::pow every sample,
// with no dirty-check unlike the set_intensity call three lines above it in
// part_fx.cpp. In this row (_buf_ok false), set_feedback returns at its
// guard and none of that runs. Flux::process's idle-path _sw.process()
// call is skipped the same way -- trivial next to the pow, but the same
// mechanism. set_time_mod is the one exception: it only ever writes a
// scalar (_time_mult) and has no guard because it needs none, so it runs
// identically either way. engaged() is also gated on _buf_ok, so it reads
// false for Flux, but GRIT alone still keeps PartFx::process's outer
// `_grit.engaged() || _flux.engaged()` branch live, so the rest of the
// shell -- the smoothers, the tape-tap bookkeeping, the FX-MIX blend, COMP,
// the send calc -- still runs exactly as it does in fx_grit.
//
// Net effect: relative to fx_grit, this row removes BOTH (a) the BBD's
// ~128 KB of echo memory ever being resident, AND (b) the per-sample
// set_feedback/apply_feedback/std::pow work (plus the trivial idle-path
// _sw.process() call) that _buf_ok also happens to gate. A low reading
// here is therefore ambiguous between "cache pressure from residency" and
// "the elided pow cost" -- it cannot, without an engine/ change this task
// does not permit, separate the two. Read it as an upper bound on the
// memory-residency hypothesis alone, not a clean isolation of it.
//
// No settle loop: unlike setup_flux_rate/setup_stages, this PartFx has no
// delay-line memory to fill (Flux::init returned before allocating any), and
// setup_fx(SEL_GRIT) -- the row this mirrors -- does not settle either.
void setup_grit_no_bbd_mem()
{
    auto& group = g_sweep_arena.emplace<SweepFxGroup>();
    group.fx.init(kSampleRate, nullptr, nullptr);
    group.fx.set_fx_on(FxBlock::Grit, true, true);
    group.fx.set_fx_on(FxBlock::Flux, false, true);
    group.fx.set_comp(0.f);
    group.fx.set_grit_mix(1.f);
    group.fx.set_flux_mix(1.f);
    group.fx.set_bpm(120.f);

    // Mirrors setup_fx(SEL_GRIT)'s values[] exactly (bench/workloads_system.cpp).
    group.values[FXT_GRIT_INT]  = 0.8f;
    group.values[FXT_FLUX_TIME] = 0.5f;
    group.values[FXT_FX_MIX]    = 1.f;
    group.values[FXT_REV_SEND]  = 0.5f;
    group.values[FXT_FLUX_FB]   = 0.7f;

    // Flux::process never reaches the stage-tracking code below its _buf_ok
    // guard, so _stages_now stays at Flux's boot default (8192) for the life
    // of this instance -- deterministic, and folded into the checksum like
    // every other SweepFxGroup row (see the comment on the struct).
    group.stages_achieved = group.fx.flux().stages();
}

void setup_flux_lines_2ch()
{
    // Derive the clock AND the stage count from a real Flux rather than
    // hard-coding either, so this row cannot silently drift away from what
    // fx_flux_sdram measures -- see the derivation above SweepLineGroup.
    static Flux probe;
    const FxMem& m = fx_mem();
    // PART_B's buffers (m.echo[1][*]) -- a genuinely separate pair from
    // PART_A's (m.echo[0][*]) below, so this one throwaway call cannot
    // disturb the memory the measured lines settle into.
    probe.init(kSampleRate, m.echo[1][0], m.echo[1][1]);
    {
        // One call is enough: init() already snapped the delay-time and
        // stage slews to their targets, so _clock_hz is fully settled the
        // instant process() computes it once.
        float pl = 0.f, pr = 0.f;
        probe.process(pl, pr);
    }
    const float hz     = probe.clock_hz();
    const int   stages = probe.stages();

    auto& group = g_sweep_arena.emplace<SweepLineGroup>();
    group.clock_hz = hz;
    group.l.Init(kSampleRate, m.echo[0][0], Flux::kMaxSamples);
    group.r.Init(kSampleRate, m.echo[0][1], Flux::kMaxSamples);
    // Match the probe's stage count -- see the SweepLineGroup comment on why
    // leaving BbdEcho::Init's full-capacity default in place would confound
    // the subtraction this row exists to keep clean.
    group.l.SetStages(stages);
    group.r.SetStages(stages);

    // Settle OUTSIDE the measured window -- see kSweepFluxLinesSettleSamples.
    const float* in = test_input();
    for (int i = 0; i < kSweepFluxLinesSettleSamples; ++i) {
        const float x = in[i % kBlock];
        group.l.Process(x, hz);
        group.r.Process(x * 0.9f, hz);
    }
}

float proc_flux_lines_2ch()
{
    auto& group = g_sweep_arena.get<SweepLineGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        acc += group.l.Process(in[i], group.clock_hz);
        acc += group.r.Process(in[i] * 0.9f, group.clock_hz);
    }
    return acc;
}

// --- Sweep C: cost against voice count ---------------------------------------
// setup_inst_worst (bench/workloads_system.cpp) with COLOR as the only
// variable. Everything else -- density, depth, rate, both FX blocks, comp,
// voice decay, the reverb settings, master drive -- stays at its worst-case
// value, so this curve is measured on the same instrument the CPU gate is
// set on. Eight voices (2 parts x SynthEngineT::kVoices == 4, synth_engine.h)
// are 35.8 points, the largest single item in the whole instrument; this
// asks whether the last voice costs more than the first.
//
// COLOR norms for 1..4 notes, from engine/pitch/chord.h. Instrument::set_color
// (engine/instrument.h) -> Part::set_color (engine/parts/part.h) only stores
// the raw knob value; the CHORD layer only sees it through Part::_control_tick
// (engine/parts/part.cpp:274-275, "_chord.set_color(_color_eff)"), which
// calls ChordBuilder::set_color (chord.h). That function's zone edges are
// its own constants -- kEdge2 = 0.125, kEdge3 = 0.375, kEdge4 = 0.625, kHyst
// = 0.02 -- NOT evenly spaced fractions of 0-1 (0.125/0.375/0.625 are, but
// the fourth "zone" is everything from kEdge4 up, i.e. unbounded above). On
// the very first call (the state this row's single set_color(p, ...) call
// produces -- _above2/_above3/_above4 all start false, chord.h:38-46),
// ChordBuilder::_hyst(false, edge) resolves to `color >= edge + kHyst`, so
// the four zones this row's four norms target are:
//   1 note  -> color <  0.145                     (norm 0.0)
//   2 notes -> 0.145 <= color < 0.395              (norm 0.25, zone centre)
//   3 notes -> 0.395 <= color < 0.645              (norm 0.52, zone centre)
//   4 notes -> color >= 0.645                      (norm 1.0 -- the exact
//                                                    value setup_inst_worst
//                                                    already uses)
//
// That is NOT the whole story. This row's mandated DEPTH = 1 also modulates
// COLOR every control tick (Part::_control_tick, part.cpp:264-274): cmod =
// mod_lane_output(LANE_MOTION) * depth * kColorMod(0.2, part.h) * cgate,
// where cgate = clampf(base_color / kColorGate(0.01), 0, 1) saturates to 1
// for any base color above 0.01. So the EFFECTIVE color swings by roughly
// +/-0.2 around three of these four norms -- wider than the ~0.25-wide 2-
// and 3-note zones -- for as long as DEPTH stays at its mandated worst-case
// value. Only the 1-note norm (0.0, where cgate == 0 exactly, so the swing
// is gated fully off) and the 4-note norm (1.0, where the swing can only
// push color DOWN and 0.8 still clears kEdge4 + kHyst with margin) are
// immune to it.
//
// A second, independent effect compounds this: DENSITY = 1 and RATE = 0.8
// (also mandated) make the PITCH lane fire often, and each fire's
// trigger_chord() (synth_engine.cpp:142) claims the next FREE voice for
// every note in the chord before it ever steals a sounding one
// (SynthEngineT::_do_trigger, synth_engine.cpp:178-183) -- and VOICE_DECAY
// = 1 (also mandated) means a struck voice does not free up on any
// timescale this bench measures. So repeated fires accumulate DISTINCT
// struck voices across time, independent of how many notes any single fire
// contains. Verified empirically with a scratch instrument mirroring this
// setup exactly (engine-only init, same set_color/density/depth/rate/
// voice_decay, same trigger_manual): active_voices(PART_A) +
// active_voices(PART_B) reaches 8 (every voice on both parts) by block
// ~150 (14 400 samples, ~0.3 s) for ALL FOUR norms above, including 0.0,
// and stays at 8 for at least 1200 blocks -- well past this row's own
// settle (below) plus the runner's fixed 1000-block measured window. See
// the report's "concerns" section: under these mandated worst-case
// settings, this sweep's four rows are expected to reach the SAME achieved
// voice count once settled, not a graduated 1/2/3/4 (or 2/4/6/8) curve.
// proc_voices() below folds the achieved active_voices() into the checksum
// for exactly this reason -- so that fact is visible in the numbers
// instead of hidden behind a plausible-looking row name.
//
// Settle: setup_inst_worst leaves this entirely to the runner's fixed
// 100-block warm-up, which is NOT enough for what this row actually
// contains -- the empirical trace above shows the slowest norm (0.0) still
// climbing at block 100 (only 6 of 8 voices) and not yet flat. Settling
// inside setup(), before the runner's own warm-up and its 1000-block
// measured window even start, means every measured block sees the same
// (saturated) voice count instead of blending a rising transient into the
// average. 600 blocks (57 600 samples, 1.2 s) is four times the observed
// ~150-block saturation point -- this file's standing "settle four times
// over" margin (kSweepFluxSettleSamples, kSweepStagesSettleSamples,
// kSweepFluxLinesSettleSamples above) -- and it is also six times longer
// than the reverb's own precedent elsewhere in this bench: several other
// families (workloads_abl.cpp, workloads_body.cpp, workloads_sampler.cpp)
// run the identical decay = 0.95 AmbientReverb behind only the runner's
// bare 100-block warm-up and are accepted rows, so 600 blocks covers the
// reverb's settle needs as a side effect of covering the voice-saturation
// need that actually drives this number.
constexpr int kSweepVoicesSettleBlocks = 600;

void setup_voices(float color_norm)
{
    auto& group = g_sweep_arena.emplace<SweepInstrumentGroup>();
    auto& inst = group.instrument;
    inst.init(kSampleRate, fx_mem());
    inst.set_tempo_bpm(120.f);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_color(p, color_norm);   // <-- the one variable; see derivation above
        inst.set_density(p, 1.f);
        inst.set_depth(p, 1.f);
        inst.set_rate(p, 0.8f);
        inst.set_fx_on(p, FxBlock::Grit, true);
        inst.set_fx_on(p, FxBlock::Flux, true);
        inst.set_grit_mix(p, 1.f);
        inst.set_flux_mix(p, 1.f);
        inst.set_comp(p, 1.f);
        inst.set_voice_decay(p, 1.f);
        inst.trigger_manual(p);
    }
    inst.set_reverb_mix(0.5f);
    inst.set_reverb_size(1.f);
    inst.set_reverb_decay(0.95f);
    inst.set_reverb_diffusion(0.9f);
    inst.set_reverb_smear(1.f);
    inst.set_reverb_mod(1.f);
    inst.set_master_drive(1.f);

    // Settle OUTSIDE the measured window -- see kSweepVoicesSettleBlocks above.
    const float* in = test_input();
    for (int b = 0; b < kSweepVoicesSettleBlocks; ++b)
        inst.process(in, in, group.out_l, group.out_r, kBlock);
}

void setup_voices_1() { setup_voices(0.0f);  }
void setup_voices_2() { setup_voices(0.25f); }
void setup_voices_3() { setup_voices(0.52f); }
void setup_voices_4() { setup_voices(1.0f);  }

float proc_voices()
{
    auto& group = g_sweep_arena.get<SweepInstrumentGroup>();
    auto& inst = group.instrument;
    const float* in = test_input();
    inst.process(in, in, group.out_l, group.out_r, kBlock);
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += group.out_l[i] + group.out_r[i];
    // Fold the achieved voice count into the checksum, once per call (not
    // per sample) -- same idiom as proc_inst (workloads_system.cpp) and
    // proc_sweep_fx's stages_achieved fold above. If this row's actual
    // active_voices() ever differs from what its name claims -- whether
    // from a bad COLOR-zone mapping or from the voice-stealing saturation
    // documented on setup_voices above -- this moves the checksum the bench
    // compares across hardware runs, instead of the curve quietly
    // reporting a knee (or a flat line) that isn't explained.
    acc += static_cast<float>(inst.active_voices(PART_A));
    acc += static_cast<float>(inst.active_voices(PART_B));
    return acc;
}

} // namespace

const Workload kSweepWorkloads[] = {
    { "sweep", "sweep_probe",       setup_sweep_probe,  proc_sweep_probe },
    { "sweep", "sweep_flux_rate_0",  setup_flux_rate_0,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_3",  setup_flux_rate_3,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_6",  setup_flux_rate_6,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_8",  setup_flux_rate_8,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_11", setup_flux_rate_11, proc_sweep_fx },
    { "sweep", "sweep_stages_512",   setup_stages_512,   proc_sweep_fx },
    { "sweep", "sweep_stages_2048",  setup_stages_2048,  proc_sweep_fx },
    { "sweep", "sweep_stages_8192",  setup_stages_8192,  proc_sweep_fx },
    { "sweep", "sweep_stages_16384", setup_stages_16384, proc_sweep_fx },
    { "sweep", "sweep_grit_bare",       setup_grit_bare,       proc_grit_bare },
    { "sweep", "sweep_grit_no_bbd_mem", setup_grit_no_bbd_mem, proc_sweep_fx  },
    { "sweep", "sweep_flux_lines_2ch",  setup_flux_lines_2ch,  proc_flux_lines_2ch },
    { "sweep", "sweep_voices_1", setup_voices_1, proc_voices },
    { "sweep", "sweep_voices_2", setup_voices_2, proc_voices },
    { "sweep", "sweep_voices_3", setup_voices_3, proc_voices },
    { "sweep", "sweep_voices_4", setup_voices_4, proc_voices },
};
const int kSweepCount = sizeof(kSweepWorkloads) / sizeof(kSweepWorkloads[0]);

} // namespace bench
