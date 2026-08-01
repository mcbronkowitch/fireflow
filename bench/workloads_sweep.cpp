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

// --- Sweep A: cost against the FLUX tape-time division ladder ----------------
// Keep the five historical RATE samples so reports remain comparable. Each
// row now observes and folds the achieved tape delay target; there is no BBD
// clock ceiling or stage axis left to interpret.
struct SweepFxGroup {
    PartFx fx;
    float values[FXT_COUNT];
    float delay_target_s = 0.f;
};

// --- Ablation F: GRIT with and without FLUX tape storage ---------------------
// Retain the protocol row name for historical result files. The null-memory
// row now removes FLUX's stereo tape arena and idle processing guard; it is a
// storage/residency comparison, not a BBD or libm ablation.
struct SweepGritGroup {
    Grit grit;
};

// --- Ablation E: historical raw BBD reference --------------------------------
// The protocol name remains stable for comparison with existing captures.
// It is no longer derived from FLUX: two dedicated BbdEcho lines use the
// historical 8192-stage, 8192-Hz point in fx_mem().bbd[PART_A]. The new tape
// arena is intentionally untouched. Four half-second fills settle the lines.
constexpr int kSweepFluxLinesSettleSamples = 96000;

struct SweepLineGroup {
    BbdEcho l, r;
    float   clock_hz;
};

SerialArena<SweepInstrumentGroup, SweepFxGroup, SweepGritGroup, SweepLineGroup> g_sweep_arena;

// Four fills of the slowest tape RATE sample (rate 0, one second at 120 BPM)
// settle all five rows beyond both buffer fill and the shared 30 ms slew.
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
    // hand over. FXT_FLUX_TIME = 0.5f is the neutral point of tape_time_mult()
    // (engine/fx/tape_echo.h: 0 -> x0.25, 0.5 -> x1, 1 -> x4) -- 0.f, used by an
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
    group.delay_target_s = group.fx.flux().delay_target_for_test();
    assert(group.delay_target_s > 0.f);
}

void setup_flux_rate_0()  { setup_flux_rate(0);  }
void setup_flux_rate_3()  { setup_flux_rate(3);  }
void setup_flux_rate_6()  { setup_flux_rate(6);  }
void setup_flux_rate_8()  { setup_flux_rate(8);  }
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
    // Fold the achieved tape target once per block so a row stuck on the
    // boot rate cannot return an apparently valid duplicate checksum.
    acc += group.delay_target_s;
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

// Historical protocol name, now the exact GRIT PartFx shell with both tape
// pointers null. Flux stays disengaged behind its two-pointer _buf_ok guard,
// while GRIT and the surrounding shell remain active. This compares tape
// storage/residency plus the guarded idle path; it makes no old BBD/pow claim.
// No settle is needed because no delay line is initialized.
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

    // Null stereo tape memory leaves Flux disengaged behind its _buf_ok guard.
    group.delay_target_s = 0.f;
}

void setup_flux_lines_2ch()
{
    // Historical protocol name: this remains the same-build raw second-BBD-
    // line row and never borrows movement 3's tape arena.
    constexpr float kBbdClockHz = 8192.f;
    constexpr int kBbdStages = 8192;
    const FxMem& m = fx_mem();
    auto& group = g_sweep_arena.emplace<SweepLineGroup>();
    group.clock_hz = kBbdClockHz;
    group.l.Init(kSampleRate, m.bbd[PART_A][0], BbdEngine::kCells);
    group.r.Init(kSampleRate, m.bbd[PART_A][1], BbdEngine::kCells);
    group.l.SetStages(kBbdStages);
    group.r.SetStages(kBbdStages);

    const float* in = test_input();
    for (int i = 0; i < kSweepFluxLinesSettleSamples; ++i) {
        const float x = in[i % kBlock];
        group.l.Process(x, kBbdClockHz);
        group.r.Process(x * 0.9f, kBbdClockHz);
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
    // sample), exactly as proc_sweep_fx folds its delay target above. This
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
// (engine/parts/part.cpp:287-288, "_chord.set_color(_color_eff)"), which
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
//   1. DEPTH=1 modulates COLOR every control tick (part.cpp:277-287):
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
// Checksum fold: the setup readback captures Flux's real settled target.
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
    // reverb_asleep() catches "the room stopped running"; this catches "the
    // voices feeding it stopped". The three room rows sweep the reverb at a
    // FIXED voice load, so a drifting voice count would change what the room
    // is being fed while the row's name still claimed a controlled
    // comparison. Same fold bench/workloads_abl.cpp already uses.
    acc += static_cast<float>(group.instrument.active_voices(PART_A)
                            + group.instrument.active_voices(PART_B));
    return acc;
}

} // namespace

const Workload kSweepWorkloads[] = {
    { "sweep", "sweep_flux_rate_0",  setup_flux_rate_0,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_3",  setup_flux_rate_3,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_6",  setup_flux_rate_6,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_8",  setup_flux_rate_8,  proc_sweep_fx },
    { "sweep", "sweep_flux_rate_11", setup_flux_rate_11, proc_sweep_fx },
    { "sweep", "sweep_grit_bare",       setup_grit_bare,       proc_grit_bare },
    { "sweep", "sweep_grit_no_bbd_mem", setup_grit_no_bbd_mem, proc_sweep_fx  },
    { "sweep", "sweep_flux_lines_2ch",  setup_flux_lines_2ch,  proc_flux_lines_2ch },
    { "sweep", "sweep_room_lo",  setup_room_lo,  proc_sweep_room },
    { "sweep", "sweep_room_mid", setup_room_mid, proc_sweep_room },
    { "sweep", "sweep_room_hi",  setup_room_hi,  proc_sweep_room },
};
const int kSweepCount = sizeof(kSweepWorkloads) / sizeof(kSweepWorkloads[0]);

} // namespace bench
