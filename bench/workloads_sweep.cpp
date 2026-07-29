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
// kClockMaxHz (32000):
//   rate 0  (cpb=1/2): t=1.0s    -> clock 4096 Hz
//   rate 3  (cpb=1  ): t=0.5s    -> clock 8192 Hz
//   rate 6  (cpb=2  ): t=0.25s   -> clock 16384 Hz
//   rate 9  (cpb=4  ): t=0.125s  -> clock 32768 Hz -> clamped to 32000 Hz
//   rate 11 (cpb=8  ): t=0.0625s -> clock 65536 Hz -> clamped to 32000 Hz
// So BOTH rate 9 and rate 11 land on the 32 kHz ceiling (1.33 ticks per audio
// sample at 48 kHz) and should cost the same -- the knee sits between rate 6
// and rate 9, not only at the last point. A prior draft of this row zeroed
// values[FXT_FLUX_TIME], which bbd_time_mult() maps to x0.25 (engine/fx/bbd.h),
// quartering every row's real clock and hiding this ceiling entirely; fixed by
// mirroring setup_fx's values[] array exactly (see setup_flux_rate below).
struct SweepFxGroup {
    PartFx fx;
    float  values[FXT_COUNT];
};

SerialArena<SweepInstrumentGroup, SweepFxGroup> g_sweep_arena;

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
//   rate 0:  clock 4096  Hz -> fill 8192/(2*4096)  = 1.000 s = 48000 samples
//   rate 3:  clock 8192  Hz -> fill 8192/(2*8192)  = 0.500 s = 24000 samples
//   rate 6:  clock 16384 Hz -> fill 8192/(2*16384) = 0.250 s = 12000 samples
//   rate 9:  clock 32000 Hz -> fill 8192/(2*32000) = 0.128 s =  6144 samples
//   rate 11: clock 32000 Hz -> fill 8192/(2*32000) = 0.128 s =  6144 samples
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
}

void setup_flux_rate_0()  { setup_flux_rate(0);  }
void setup_flux_rate_3()  { setup_flux_rate(3);  }
void setup_flux_rate_6()  { setup_flux_rate(6);  }
void setup_flux_rate_9()  { setup_flux_rate(9);  }
void setup_flux_rate_11() { setup_flux_rate(11); }

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
    return acc;
}

} // namespace

const Workload kSweepWorkloads[] = {
    { "sweep", "sweep_probe",       setup_sweep_probe,  proc_sweep_probe },
    { "sweep", "sweep_flux_rate_0",  setup_flux_rate_0,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_3",  setup_flux_rate_3,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_6",  setup_flux_rate_6,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_9",  setup_flux_rate_9,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_11", setup_flux_rate_11, proc_sweep_fx },
};
const int kSweepCount = sizeof(kSweepWorkloads) / sizeof(kSweepWorkloads[0]);

} // namespace bench
