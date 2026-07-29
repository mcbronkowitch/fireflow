#include <cassert>
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
// Two bare BbdEcho, at the stage count and clock an ENGAGED Flux computes,
// with no Flux around them. Then
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
// A hardware run of an earlier version of this row (before the "ENGAGED"
// above) came back with checksum ea306fb5 -- byte-identical to
// empty_callback's. Flux::process (flux.cpp) returns before the ONE line
// that assigns _clock_hz (line 320) at two guards: `if (!_buf_ok) return;`
// and `if (_sw.is_idle()) return;`. Flux::init never turns the soft switch
// on, so a merely-init'd probe never reaches the assignment, clock_hz()
// reads back its declared initialiser (0.f, flux.h), BbdEcho::Process(x,
// 0.f) sets ticks_ = 0, and the two bare lines never advanced a single tick
// -- silently, because the input/output filter poles the tick loop
// surrounds still advance every sample regardless, so the row still
// returned a plausible-looking nonzero number (6.70 % on that run) despite
// measuring nothing of what it claims to. Recomputing bbd_clock_hz(0.5,
// 8192) = 8192 from the formula could not catch this: the formula is
// correct arithmetic about a value the row never actually used. Confirmed
// against the running object instead, not the formula: a standalone build
// of engine/fx/flux.{h,cpp} against this exact sequence prints
// clock_hz()=0.000000 for an un-engaged probe and clock_hz()=8192.000000,
// stable across repeated process() calls, once probe.set_on(true, true) is
// called first -- see setup_flux_lines_2ch below and its assert.
//
// clock_hz and the stage count come from a throwaway probe Flux rather than
// being hard-coded, so this row cannot silently drift from what
// fx_flux_sdram (and setup_flux_rate/setup_stages above) actually run.
// fx_flux_sdram (bench/workloads_system.cpp's setup_fx(SEL_FLUX)) never
// calls set_stages or set_flux_rate away from Flux::init's boot defaults --
// bpm 120, _rate_idx 3 ("1/4"), kBootStagesNorm 0.8 -> 512*32^0.8 == 8192
// stages (flux.cpp) -- and, like fx_flux_sdram (via PartFx::set_fx_on),
// this row's probe is explicitly engaged (set_on(true, true) -- see
// setup_flux_lines_2ch). Left at those untouched defaults plus engaged, the
// probe lands on exactly those numbers: clock 8192 Hz (the rate-3 row of
// the Sweep A ladder table above already derives this arithmetically, and
// the standalone run above confirms it from the object) and 8192 stages.
// Neither set_bpm(120) nor set_flux_rate(3) needs to be called on the
// probe: those are already init()'s defaults.
//
// clock_hz() (engine/fx/flux.h -- pre-existing, already used throughout
// tests/test_flux.cpp) only reads back _clock_hz, which Flux computes once
// per process() call (flux.cpp, after the delay-time/stage slews) from
// _dt_current and the stage count -- both of which init() already snaps
// straight to their targets (immediate = true), with no slew left to run.
// So a single process() call is enough for the ENGAGED probe's clock to be
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
    // MUST engage the probe. Flux::process (flux.cpp) returns before line 320
    // (where _clock_hz is assigned) at TWO guards: `if (!_buf_ok) return;`
    // and `if (_sw.is_idle()) return;`. init() never turns the soft switch
    // on, so an un-engaged probe's clock_hz() reads back its declared
    // initialiser, 0.f, forever -- exactly the bug a hardware run caught
    // here: this row's checksum came back byte-identical to empty_callback's
    // because BbdEcho::Process(x, 0.f) sets ticks_ = 0 and the lines never
    // advanced. set_on's immediate=true snaps the switch straight to
    // Stage::hold (fx_util.h), so is_idle() is already false the moment the
    // very first process() call below runs -- a switch merely fading in
    // (immediate=false) is still idle and would reproduce the same bug.
    probe.set_on(true, /*immediate=*/true);
    {
        // One call is enough: init() already snapped the delay-time and
        // stage slews to their targets, so _clock_hz is fully settled the
        // instant process() reaches its assignment.
        float pl = 0.f, pr = 0.f;
        probe.process(pl, pr);
    }
    const float hz = probe.clock_hz();
    // Guard, not a formality: this exact silent-zero failure already reached
    // hardware once (checksum ea306fb5, identical to empty_callback's). If
    // clock_hz() is ever 0 again -- a future Flux::init/process change, a
    // reordered call above, anything -- this row must fail loudly rather
    // than quietly measure two stopped BBD lines a second time.
    assert(hz > 0.f && "sweep_flux_lines_2ch: probe Flux produced a stopped "
                        "(0 Hz) clock -- Flux::process returned before "
                        "assigning _clock_hz; is the probe engaged?");
    const int stages = probe.stages();

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
    // Fold the achieved clock into the checksum, once per call (not per
    // sample), exactly as proc_sweep_fx folds stages_achieved above. This
    // does NOT guard the silent-zero failure the comment on the assert in
    // setup_flux_lines_2ch describes: acc += 0.f changes nothing, and in
    // the run that actually hit that failure the accumulator was already
    // exactly 0.0, checksum ea306fb5 -- folding a zero clock into a
    // zero-valued sum moves nothing. The assert above is what catches a
    // zero clock. What this fold DOES guard is a WRONG NONZERO clock: if a
    // future change ever made clock_hz read back some other incorrect but
    // nonzero value (the assert only checks `> 0.f`), that value changes
    // acc and the per-row checksum moves, instead of the row silently
    // measuring the wrong configuration.
    acc += group.clock_hz;
    return acc;
}

// --- Sweep C: cost against voice count -- BUILT, THEN REMOVED ---------------
// A four-row sweep (sweep_voices_1..4) was built here: setup_inst_worst
// (bench/workloads_system.cpp) with COLOR as the only variable, on the
// theory that COLOR's chord size (1-4 notes/part, engine/pitch/chord.h)
// would step the instrument's active voice count and let the curve answer
// whether the last of the instrument's eight voices (2 parts x
// SynthEngineT::kVoices == 4, synth_engine.h; 35.8 points, the largest
// single item in the whole instrument) costs more than the first.
//
// The COLOR norms, kept here so this research does not have to be redone:
// from engine/pitch/chord.h, ChordBuilder::set_color and its private
// _hyst() helper. Instrument::set_color (engine/instrument.h) ->
// Part::set_color (engine/parts/part.h) only stores the raw knob value;
// the chord layer only sees it through Part::_control_tick
// (engine/parts/part.cpp:274-275, "_chord.set_color(_color_eff)"), which
// calls ChordBuilder::set_color (chord.h). That function's zone edges are
// its own constants -- kEdge2 = 0.125, kEdge3 = 0.375, kEdge4 = 0.625,
// kHyst = 0.02 -- NOT evenly spaced fractions of 0-1 (0.125/0.375/0.625
// are, but the fourth "zone" is everything from kEdge4 up, unbounded
// above). On a freshly-initialised builder (_above2/_above3/_above4 all
// start false, chord.h:38-46), _hyst(false, edge) resolves to
// `color >= edge + kHyst`, giving:
//   1 note  -> color <  0.145                     (norm 0.0)
//   2 notes -> 0.145 <= color < 0.395              (norm 0.25, zone centre)
//   3 notes -> 0.395 <= color < 0.645              (norm 0.52, zone centre)
//   4 notes -> color >= 0.645                      (norm 1.0 -- the exact
//                                                    value setup_inst_worst
//                                                    already uses)
//
// Why the rows were removed, not shipped: under setup_inst_worst's other
// mandated settings -- DEPTH=1, DENSITY=1, RATE=0.8, VOICE_DECAY=1, all
// required to stay at worst-case so the curve would measure the same
// instrument the CPU gate is set on -- COLOR does not end up controlling
// the achieved voice count. Two compounding mechanisms:
//   1. DEPTH=1 modulates COLOR every control tick (part.cpp:264-274):
//      cmod = mod_lane_output(LANE_MOTION) * depth * kColorMod(0.2,
//      part.h) * cgate, and cgate saturates to 1 for any base color above
//      kColorGate (0.01, part.h). The resulting +/-0.2 swing is wider than
//      the ~0.25-wide 2- and 3-note zones above.
//   2. Independently, and more decisively: DENSITY=1/RATE=0.8 make the
//      PITCH lane fire often, and SynthEngineT::_do_trigger
//      (synth_engine.cpp:178-183) always claims the next FREE voice for
//      every note before ever stealing a sounding one. With VOICE_DECAY=1
//      meaning a struck voice never frees up on any timescale this bench
//      measures, repeated fires accumulate distinct struck voices over
//      time regardless of how many notes any single fire contains.
// A scratch instrument mirroring setup_inst_worst exactly (engine-only
// init, same set_color/density/depth/rate/voice_decay, same
// trigger_manual; not part of any commit, so this is an observation, not
// committed evidence) showed active_voices(PART_A) + active_voices(PART_B)
// reaching 8 -- every voice on both parts -- within ~150 blocks (14 400
// samples, ~0.3 s) for ALL FOUR norms above, including 0.0, and staying at
// 8 for at least 1200 blocks. Four rows that all converge on the same
// number are not a curve.
//
// The question this sweep was meant to answer is already answered by rows
// that exist: the system family's synth_1_voice / synth_2_voices /
// synth_4_voices (bench/workloads_system.cpp), measured on hardware
// 2026-07-29 at 5.67 / 9.91 / 17.83 % max -- a fixed overhead of about
// 1.45 points plus roughly 4.2 points per additional voice, linear above
// the first. Those are clean isolated-engine rows (one SynthEngine,
// triggered once, no FX/reverb/modulation confound); this instrument-level
// sweep would have been a worse instrument for the same question even if
// COLOR had controlled the voice count. See the commit that removed these
// rows (e525084, "fix(bench/sweep): remove sweep_voices_1..4, the mapping
// cannot hold") and S9.4 of the design spec for the full account.
//
// SweepInstrumentGroup itself is NOT removed -- Task 7's reverb sweep
// (sweep_room_lo/mid/hi below) needs it.

// --- Sweep D: cost against the room controls ---------------------------------
// DIFF, SMEAR and MOD move together: setup_inst_worst (bench/workloads_system.cpp)
// puts all three near maximum and in practice they are one gesture, so this
// family sweeps them as a single knob rather than three independent rows.
//
// Are they reachable as modulation destinations under setup_inst_worst's
// settings (DEPTH=1, DENSITY=1, RATE=0.8, all lanes live)? Checked before
// writing a single row, because Task 6's sweep_voices_1..4 shipped-then-died
// on exactly this trap (see the comment on Sweep C above). Answer: NO.
//   - Instrument::set_reverb_diffusion/_smear/_mod (engine/instrument.h:108-110)
//     call straight through to AmbientReverb::set_diffusion /
//     set_diffuser_mod_depth / set_mod_depth (engine/fx/reverb.cpp) -- plain
//     setters, no lane/target indirection, called ONLY from here and from
//     AmbientReverb::init's boot defaults (grep across engine/ confirms no
//     other call site).
//   - The only per-Part modulation-destination enum touching FX is FxTargetId
//     (engine/fx/part_fx.h): FXT_GRIT_INT, FXT_FLUX_TIME, FXT_FX_MIX,
//     FXT_REV_SEND, FXT_FLUX_FB (FXT_COUNT == 5). FXT_REV_SEND is the
//     per-part SEND LEVEL into the room -- a different control from the
//     room's own diffusion/smear/mod, which have no slot in this enum at all.
//   - The two functions that ever combine a base value with lane/DEPTH
//     modulation are Part::target_raw (engine/parts/part.cpp:80, LANE_*
//     slots) and Part::fx_target_value (part.cpp:126, FXT_* slots). Both
//     index into Part-owned per-slot arrays (_base/_active/_tdepth or
//     _fx_base/_fx_active/_fx_depth); neither array has a slot for the room.
// So unlike COLOR (Sweep C), the knob position here fully determines the
// state DIFF/SMEAR/MOD end up in -- DEPTH/DENSITY/RATE being live does not
// touch them, and three distinct amounts give three distinct, controlled
// instrument states.
//
// Read setup_inst_worst (workloads_system.cpp:274-298) for the setter names
// and the exact top values -- diffusion 0.9, smear 1.0, mod 1.0 -- and mirror
// it line for line, changing only those three calls. lo/mid/hi = 0.0/0.45/0.9
// for diffusion; smear and mod scale off the SAME amount, divided by 0.9 so
// hi reproduces setup_inst_worst's smear=1.0/mod=1.0 exactly:
//   lo:  diffusion 0.00, smear 0.00/0.9 = 0.000, mod 0.000
//   mid: diffusion 0.45, smear 0.45/0.9 = 0.500, mod 0.500
//   hi:  diffusion 0.90, smear 0.90/0.9 = 1.000, mod 1.000
// sweep_room_hi therefore reproduces setup_inst_worst's room exactly -- a
// cross-check worth having, but a weaker one than a like-for-like comparison:
// proc_sweep_room (this row) never re-triggers, while proc_inst
// (workloads_system.cpp), which setup_inst_worst measures through, fires
// both parts every 250 blocks -- so the two readings (sweep_room_hi 120.81
// vs instrument_worst 120.14, a 0.67-point gap) compare two different proc
// functions, not the same one twice. That 0.67-point gap is itself larger
// than the 0.64-point span S9.3 found across the room's ENTIRE travel, so it
// cannot be read as confirming instrument-level parity on its own. The room
// controls' "flat / leave it" disposition does not depend on this check,
// though: the weakness here is common-mode across lo/mid/hi alike, not
// specific to hi.
//
// Checksum fold: Task 3's stages_achieved reads back Flux's REAL settled
// state (it can legitimately differ from the requested norm -- see the
// comment on SweepFxGroup) and folds that into the checksum, so a drift
// between "requested" and "achieved" moves the number instead of passing
// silently. No such readback exists here: AmbientReverb (engine/fx/reverb.h)
// exposes set_diffusion/set_diffuser_mod_depth/set_mod_depth but no getters,
// and the values they compute (the allpass coefficient and the two LFO
// amounts) live as private members of third_party/oliverb/oliverb.h's
// clouds::Oliverb with no accessor either. Both setters apply their norm
// synchronously with no slew and no clamping that would ever engage at these
// amounts, so there is no async-settling step to read back in the first
// place -- but confirming that requires exactly the getter that does not
// exist, and adding one would be an engine/ or third_party/ change, which
// Task 7 has no exception for. Folding the locally-known requested amount
// back into the checksum would be circular (it would just restate a
// compile-time constant this file already wrote down) and would misrepresent
// itself as the readback guard Task 3 has -- so it is deliberately NOT done.
// What IS folded, because it is a real existing accessor and a real guard:
// Instrument::reverb_asleep() (engine/instrument.h:263). setup_inst_worst's
// set_reverb_mix(0.5f) keeps both decks' wet target above zero, so the room
// never sleeps here (Instrument::process, instrument.cpp:154-189, only sets
// _rev_asleep when BOTH wet targets are zero) -- reverb_asleep() should read
// false for the life of every row below. It does not confirm the diffusion/
// smear/mod VALUES took effect, only that the reverb is actually being
// Process()ed at all rather than silently bypassed -- if a future change to
// the mix/scaling above ever put the room to sleep, this is what would move
// the checksum instead of the sweep quietly measuring an idle reverb.
//
// Settle: derived, not copied from setup_inst_worst_bbd's 200 blocks (that
// row settles BBD lines at a much shorter clock period; this row is a
// different device with its own period). The core loop (excluding the four
// INPUT diffusers ap1-ap4, which are not part of the feedback path) is a
// single ring: del1 -> dap2a -> dap2b -> del2 -> dap1a -> dap1b -> back to
// del1 (third_party/oliverb/oliverb.h:111-219). At SIZE 1.0 -- set_reverb_size
// isn't part of this sweep but setup_inst_worst sets it to 1.f, and every row
// here mirrors that -- smooth_size_ settles (one-pole, coeff 0.0002/sample,
// i.e. ~5000-sample/~104 ms time constant, fully negligible against what
// follows) to 0.99, and each delay read sits at (length-1)*smooth_size, i.e.
// essentially the full reserved length. Summing the six loop-member lengths
// (the Reserve<> sizes at oliverb.h:111-120, already the parasites values
// x1.5): dap1a 1880 + dap1b 2607 + del1 5117 + dap2a 2270 + dap2b 2045 +
// del2 7173 = 21092 samples -- one full trip around the ring, ~439 ms at
// 48 kHz, ~220 blocks. DECAY 0.95 (set_reverb_decay, held at setup_inst_worst's
// own value, not swept) maps through AmbientReverb::set_decay to loop gain
// 0.95*(1/0.9) = 1.0556, clamped to 1.05 -- ABOVE unity, the same "blooms,
// self-sustains, stays bounded" configuration tests/test_reverb.cpp's decay-
// past-100% case exercises (its set_decay(1.f) maps to the identical clamped
// 1.05: 1.f*(1/0.9) = 1.111, also clamped). That test is this row's evidence
// the growth is bounded (SoftLimit, stmlib_shim.h) rather than runaway, so
// extending the settle window is safe, not a race against divergence.
// decay_ is applied once per branch, twice per full ring trip, so gain per
// trip is 1.05^2 = 1.1025 -- a modest ~10%/trip excess over unity. Four trips
// (this file's standing "settle before measuring" multiple -- see
// kSweepFluxSettleSamples/kSweepStagesSettleSamples/kSweepFluxLinesSettleSamples
// above) is 4*21092 = 84368 samples = 878.83 blocks, rounded up to 879 --
// comfortably past the point where a sample has had the chance to travel the
// entire feedback ring four times over, so the measured window sees the
// room's actual running state rather than a partially-filled one. The
// runner's fixed 100-block warm-up (9600 samples) covers under HALF of one
// single trip around this particular ring -- nowhere near enough on its
// own -- which is exactly why this setup extends it explicitly instead of
// relying on kWarmupBlocks, the same move setup_inst_worst_bbd (workloads_
// system.cpp) already makes for its own, much shorter-period BBD lines.
constexpr int kSweepRoomSettleBlocks = 879;   // 4 * 21092 samples / kBlock, see above

void setup_room(float amount)
{
    auto& group = g_sweep_arena.emplace<SweepInstrumentGroup>();
    group.instrument.init(kSampleRate, fx_mem());
    group.instrument.set_tempo_bpm(120.f);
    // Mirrors setup_inst_worst (workloads_system.cpp) line for line -- the
    // three room calls at the end are the only change.
    for (int p = 0; p < PART_COUNT; ++p) {
        group.instrument.set_color(p, 1.f);          // 4-note chords -> 4 voices per part
        group.instrument.set_density(p, 1.f);
        group.instrument.set_depth(p, 1.f);
        group.instrument.set_rate(p, 0.8f);
        group.instrument.set_fx_on(p, FxBlock::Grit, true);
        group.instrument.set_fx_on(p, FxBlock::Flux, true);
        group.instrument.set_grit_mix(p, 1.f);
        group.instrument.set_flux_mix(p, 1.f);
        group.instrument.set_comp(p, 1.f);
        group.instrument.set_voice_decay(p, 1.f);
        group.instrument.trigger_manual(p);
    }
    group.instrument.set_reverb_mix(0.5f);
    group.instrument.set_reverb_size(1.f);
    group.instrument.set_reverb_decay(0.95f);
    // The three swept controls -- see the derivation above the struct comment
    // for why amount/0.9 makes sweep_room_hi (amount=0.9) land on exactly
    // setup_inst_worst's smear=1.0/mod=1.0.
    group.instrument.set_reverb_diffusion(amount);
    group.instrument.set_reverb_smear(amount / 0.9f);
    group.instrument.set_reverb_mod(amount / 0.9f);
    group.instrument.set_master_drive(1.f);

    // Settle OUTSIDE the measured window -- see kSweepRoomSettleBlocks above.
    // voice_decay=1.f (set on both parts just above) is the same knob
    // workloads_system.cpp's setup_synth_n documents as giving ~16 s of
    // envelope at its own decay/cycle pairing; Task 6's scratch probe of this
    // exact instrument-worst configuration found voices staying fully
    // sounding for at least 1200 blocks with no re-trigger. This row's total
    // window (879 settle + 1000 measured = 1879 blocks, ~3.76 s) is well
    // inside both figures, so -- unlike proc_inst (workloads_system.cpp),
    // which re-triggers every 250 blocks to counter a MUCH longer measured
    // window -- no periodic re-trigger is needed here, and SweepInstrumentGroup
    // (Task 1) has no counter field to drive one with anyway.
    const float* in = test_input();
    for (int b = 0; b < kSweepRoomSettleBlocks; ++b)
        group.instrument.process(in, in, group.out_l, group.out_r, kBlock);
}

void setup_room_lo()  { setup_room(0.0f); }
void setup_room_mid() { setup_room(0.45f); }
void setup_room_hi()  { setup_room(0.9f); }

float proc_sweep_room()
{
    auto& group = g_sweep_arena.get<SweepInstrumentGroup>();
    const float* in = test_input();
    group.instrument.process(in, in, group.out_l, group.out_r, kBlock);
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += group.out_l[i] + group.out_r[i];
    // Fold the closest thing to an "achieved setting" this row has access to
    // without an engine/ or third_party/ change -- see the long comment
    // above setup_room for why a true diffusion/smear/mod readback does not
    // exist. reverb_asleep() is a real, pre-existing accessor
    // (engine/instrument.h) and should read false for the life of this row;
    // if it ever reads true the checksum moves, catching "the room silently
    // stopped running" even though it cannot catch "the room is running with
    // the wrong diffusion/smear/mod".
    acc += group.instrument.reverb_asleep() ? 1.f : 0.f;
    return acc;
}

} // namespace

const Workload kSweepWorkloads[] = {
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
    { "sweep", "sweep_room_lo",  setup_room_lo,  proc_sweep_room },
    { "sweep", "sweep_room_mid", setup_room_mid, proc_sweep_room },
    { "sweep", "sweep_room_hi",  setup_room_hi,  proc_sweep_room },
};
const int kSweepCount = sizeof(kSweepWorkloads) / sizeof(kSweepWorkloads[0]);

} // namespace bench
