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

SerialArena<SweepInstrumentGroup, SweepFxGroup, SweepGritGroup> g_sweep_arena;

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
};
const int kSweepCount = sizeof(kSweepWorkloads) / sizeof(kSweepWorkloads[0]);

} // namespace bench
