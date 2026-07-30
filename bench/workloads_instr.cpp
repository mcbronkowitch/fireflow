#include <cassert>
#include <cmath>
#include "workload.h"
#include "families.h"
#include "mem.h"
#include "serial_arena.h"
#include "instrument.h"
#include "parts/part.h"
#include "parts/engine_iface.h"
#include "fx/part_fx.h"
#include "synth/synth_engine.h"
#include "mod/super_modulator.h"
#include "mod/lane_id.h"
#include "mod/divisions.h"

using namespace spky;

namespace bench {
namespace {

// Mirrors setup_inst_worst_bbd's own settle (bench/workloads_system.cpp):
// fill the BBD line of every Part the row actually runs -- two for
// instr_part_2 and instr_noverb, one for instr_part_1 -- and let every
// envelope and slew arrive before the runner's measured window opens.
constexpr int kInstrSettleBlocks = 200;

// The full instrument at the gate row's configuration, with the reverb
// removed. Instrument::process gates its whole reverb section behind
// `if (_reverb)`, and FxMem::reverb is a host-supplied pointer, so a null
// pointer removes the algorithm, its four per-deck gain smoothers AND the
// send/return mixing in one move -- without reimplementing any instrument
// logic here. Rebuilt logic drifts from the original, and a drifted copy
// silently measuring the wrong thing is the exact failure class this round
// exists to detect (design spec section 3.1).
//
// The MORPH blend is NOT removed with it: `l = al*ga + bl*gb` runs
// unconditionally above the guard, so it correctly stays on the glue side of
// instrument_worst_bbd - instr_noverb.
struct InstrNoVerbGroup {
    Instrument instrument;
    float out_l[kBlock], out_r[kBlock];
    int   counter = 0;
};

// Bare Parts, driven directly. The point is that NOTHING wraps them -- no
// Instrument, so no Center, no CHOKE framing, no MORPH, no dry taps, no
// cross-deck rhythm exchange, no limiter.
//
// Both bare rows share this struct, and they do not use it identically:
// instr_part_2 initialises and runs `a` and `b`, instr_part_1 initialises and
// runs `a` only (`b` stays default-constructed and is never touched, so its
// clock_b/stages_b remain 0). The glue subtraction is therefore
// instr_noverb - instr_part_2 specifically; instr_part_1 is the single-deck
// rung, and instr_part_2 - 2*instr_part_1 estimates inter-deck contention,
// not glue.
struct InstrPartGroup {
    Part  a, b;
    int   counter = 0;
    // Read back after the settle for the asserts at the end of
    // setup_instr_part_common, and folded into the returned value so they are
    // not dead stores.
    //
    // The fold is NOT itself a detector, and must not be read as one: nothing
    // compares these against a stored expectation (run.py only compares run
    // against run within one measurement), and they are constants once setup
    // has finished, so a row that silently configured its Parts differently
    // from the Instrument would just return a different-but-perfectly-stable
    // checksum and pass. What actually catches a mis-mirrored row is the
    // assert pair below, which is live: the bench builds -O2 with NDEBUG
    // undefined.
    float clock_a = 0.f, clock_b = 0.f;
    int   stages_a = 0, stages_b = 0;
};

// Mirrors setup_inst_worst + setup_inst_worst_bbd, deck for deck.
//
// This is a CHECKLIST, not a reinterpretation: every Instrument setter used
// there is a one-line forward declared in engine/instrument.h -- e.g.
// `set_color(p,n)` IS `_parts[p].set_color(n)` and `set_rate(p,n)` IS
// `_parts[p].mod().set_rate(n)`. Verify each line against
// bench/workloads_system.cpp's setup_inst_worst/setup_inst_worst_bbd and
// against engine/instrument.h's forward, in that order.
//
// Three things Instrument does that this deliberately does NOT (design spec
// section 4.1), because they have no Part equivalent and therefore belong on
// the glue side of the subtraction:
//   - set_master_drive, which reaches Instrument's own _limiter;
//   - set_other_deck_tap, supplied at control rate by Instrument;
//   - fx().set_rhythm(), likewise -- and harmless as well as correct, since
//     setup_inst_worst_bbd never touches LINK, so _link stays 0 and both
//     DRAG and THIN are inert on either side of the comparison.
void configure_worst_bbd(Part& part)
{
    part.mod().set_tempo_bpm(120.f);
    part.fx().set_bpm(120.f);
    part.set_color(1.f);
    part.mod().set_density(1.f);
    part.set_depth(1.f);
    part.mod().set_rate(0.8f);
    part.fx().set_fx_on(FxBlock::Grit, true);
    part.fx().set_fx_on(FxBlock::Flux, true);
    part.fx().set_grit_mix(1.f);
    part.fx().set_flux_mix(1.f);
    part.fx().set_comp(1.f);
    part.set_voice_decay(1.f);
    part.trigger_manual();
    part.fx().set_stages(1.f);
    part.fx().set_drive(0.85f);
    part.fx().set_flux_rate(kFluxRateCount - 1);
    part.set_fx_target_base(FXT_FLUX_FB, 0.9f);
}

// One SuperModulator at the gate's operating point, with no Center.
//
// The row it corrects is mod_plane_2x_center, which seeds its two modulators
// 1u and 2u (bench/workloads_system.cpp:70-71), runs them at RATE 0.5 and 0.6
// and DENSITY 0.7 (bench/workloads_system.cpp:75-76), never calls
// set_tempo_bpm, and does no settle. This row seeds from PART_A's real
// 0x1234abcd instead (engine/parts/part.cpp:17, engine/instrument.cpp:22),
// runs RATE 0.8 and DENSITY 1.0 -- setup_inst_worst's operating point on both
// decks -- and settles to the same depth the Instrument rows do. The seed,
// RATE and settle-depth differences are all deliberate here and all part of
// what the subtraction measures -- see the design spec section 3.
//
// DENSITY 1.0 is configured for the same reason: faithfulness to
// setup_inst_worst. But the difference from mod_plane_2x_center's DENSITY 0.7
// contributes nothing to the subtraction -- DENSITY is a no-op on BOTH sides
// of it. set_density() only writes ModLane::_density (lane.h:23), read solely
// by _groove_k() (lane.cpp:422), which _effective_gate() only consults when
// _step_mode is true (lane.cpp:449: `gated = _step_mode ? _effective_gate(...)
// : true`). Neither this row, nor mod_plane_2x_center, nor the gate itself
// (setup_inst_worst never calls set_step, design spec section 2.4) ever calls
// set_step() -- all three run in FLOW, where gated is hardcoded true. The
// value differs between the rows; the difference is not measured.
//
// set_tempo_bpm(120.f) is also called below, mirroring what
// Instrument::set_tempo_bpm pushes into every part's modulator
// (engine/instrument.cpp:70) and what configure_worst_bbd already does for
// the bare-Part rows. Unlike the other three, this one is NOT part of what
// the subtraction measures: _synced defaults to false (super_modulator.h:187)
// and nothing on this path calls set_synced(), so _update_rate() takes the FREE
// branch -- _base_hz = free_hz(_rate_norm), never _bpm
// (engine/mod/super_modulator.cpp:28-29). The call is kept for faithfulness
// to the real Part init sequence, not because it changes behaviour or cost.
//
// The Center is deliberately absent. mod_plane_2x_center includes it, so
// charging each deck half of that row double-counts an instrument-level
// object that no bare Part runs and that the measured 4.04-point glue term
// already contains.
struct DeckModGroup {
    SuperModulator mod;
    // Read back after the settle for the self-check in setup_deck_mod_hot,
    // and folded into the returned checksum so it is not a dead store -- same
    // shape as InstrPartGroup's clock_a/stages_a above.
    float master_hz = 0.f;
};

// One SynthEngine, driven exactly as Part::process drives it.
//
// Differences from synth_2x4 (bench/engine_2x4.h), each deliberate and each
// part of what this row corrects:
//
//   1. Called through an IPartEngine*, not a concrete SynthEngine&. Part
//      holds `IPartEngine* _engine` (engine/parts/part.h:209) and every
//      method on that interface is virtual (engine/parts/engine_iface.h),
//      so Part pays two virtual dispatches per sample and can inline
//      neither. proc_engine_2x4 holds a concrete reference and the compiler
//      inlines both calls. Calling the concrete type here would push that
//      dispatch cost silently into the round's remainder -- the one way to
//      get this row wrong (design spec section 2.3).
//   2. FLOW, not STEP. _step_on initialises to false (part.cpp:35), flow()
//      is !_step_on (part.h:98), and setup_inst_worst never calls set_step,
//      so the gate runs both decks as a drone. setup_engine_2x4 calls
//      set_flow(false).
//   3. The cycle comes from a real modulator's master_hz(), as
//      Part::process derives it (part.cpp:403-407), not from the constant
//      set_cycle(2.f) the old row uses.
//   4. process_in() is called every sample, before process()
//      (part.cpp:482). proc_engine_2x4 never calls it, so the 35.80 points
//      contain none of it -- but on THIS row's engine, that call costs
//      exactly one virtual dispatch to an empty body, nothing more.
//      SynthEngineT never overrides process_in(); it inherits IPartEngine's
//      empty default (engine/parts/engine_iface.h:57-59, "Only the sampler
//      implements it"). So difference #1's "two virtual dispatches per
//      sample" already covers this call's cost -- #4 is not a second,
//      additive charge on top of #1's (a correction to this row's own
//      original comment and to design spec section 2.2, found in review).
//      The distinction section 2.2 draws is real only on a SAMPLER deck:
//      SamplerEngine::process_in (engine/sampler/sampler_engine.cpp:158)
//      actually records/monitors from the call. Not this row's engine.
//   5. A periodic re-fire (added in review; see task-2-report.md's fix
//      round). Once FLOW is engaged, holding a chord needs trigger_chord(),
//      not repeated trigger() calls: trigger()'s chord_slot is hardcoded 0
//      (synth_engine.cpp:139, `_do_trigger(pitch_norm, 1.f, 0)`), and in
//      FLOW, chord_slot 0 always demotes whichever voice currently holds
//      the surface before picking a new one (synth_engine.cpp:206, `if
//      (chord_slot == 0) _demote_all();` -- "a new fire demotes [the
//      sustaining voice] and takes over", synth_engine.h class comment). A
//      real deck re-strikes its chord on every LANE_PITCH fire -- roughly
//      once every 1/master_hz seconds -- via _engine->trigger_chord(chord,
//      nch) (part.cpp:447-460), which demotes only on the chord's OWN root
//      and adds the rest without demoting, so a real deck holds 4 sustained
//      voices permanently. This row's setup below fires once; with
//      set_decay(1.0) at THIS row's derived cycle (~0.144 s -- the old
//      row's constant set_cycle(2.f) is a 2 s cycle, whose own decay_s
//      would be 8 x 2 = 16 s), decay_s here is ~1.15 s
//      (synth_engine.cpp:390,276) and the -80 dB Idle threshold arrives
//      around decay_s * 80/60 ~= 1.54 s (env.h:12-13,17's 60 dB decay-time
//      definition and -80 dB Idle threshold) -- far short of the ~2.6 s a
//      full setup-settle + warmup + measurement run spans. Left unfixed,
//      occupancy collapses well inside the measured window (found in
//      review: see task-2-report.md). proc_deck_engine_hot below re-issues
//      ONE trigger_chord(kDeckEnginePitches, 4) call -- not four trigger()
//      calls, for the reason above -- through the base pointer, every
//      fire_period samples (derived from master_hz in setup, stored below),
//      matching cadence for cadence what a real deck does.
//   6. Different seed: 0x1234abcd ^ 0x5eedC0DE (PART_A's own mirror,
//      part.cpp:19) vs. setup_engine_2x4's set_seed(3u)/(4u). Cost-neutral
//      -- the RNG draws it feeds run once, at init (engine/synth/
//      voice.cpp:25-30) -- listed here for completeness, the same way
//      deck_mod_hot's own comment lists its seed difference.
//
// Not included, by design: the chord builder, the quantizer, _control_tick's
// target pushes and the _engine_fade multiply. Those are Part-level and stay
// in the round's remainder (design spec section 4). The periodic
// trigger_chord() call in #5 is not the chord builder: it re-strikes the
// same fixed four pitches every cycle -- the set this row's setup already
// triggers once -- rather than building a chord from COLOR or the quantizer.
struct DeckEngineGroup {
    SynthEngine    synth;
    SuperModulator mod;          // setup only -- see setup_deck_engine_hot
    IPartEngine*   engine = nullptr;
    float          master_hz = 0.f;
    int            voices = 0;
    // Re-fire cadence (post-review fix, difference #5 above): samples
    // between trigger_chord() calls in proc_deck_engine_hot, derived once in
    // setup from master_hz and held fixed. fire_ctr is the running
    // countdown; it lives in the group so it survives across process()
    // calls, the same reason InstrNoVerbGroup and InstrPartGroup keep their
    // own retrigger counters here rather than as function-local statics.
    int            fire_period = 0;
    int            fire_ctr = 0;
};

// One PartFx with FLUX on, at the operating point a deck actually runs it at.
// FxGroup's shape (bench/workloads_system.cpp:37-40) plus four readbacks.
//
// The row this exists to correct is fx_flux_sdram (setup_fx(SEL_FLUX) +
// proc_fx, bench/workloads_system.cpp:182-220), which prices FLUX at STAGES
// 8192, rate index 3, FEEDBACK 0.7 and DRIVE 0 -- none of which a deck runs.
// configure_worst_bbd above pushes set_stages(1.f), set_drive(0.85f),
// set_flux_rate(kFluxRateCount - 1) and FXT_FLUX_FB 0.9 into every Part
// (bench/workloads_instr.cpp:107-110). Round 1 could only price the STAGES
// and RATE axes separately and add them (+2.29 points, never measured
// together), and it never priced DRIVE at all; that estimate is the slop
// design spec section 2 calls load-bearing, and this row replaces it with a
// measurement.
//
// EVERY difference from fx_flux_sdram, read off setup_fx/proc_fx line by
// line. Four are the operating point. There is a FIFTH, and it is named
// here rather than left implicit:
//
//   1. STAGES. set_stages(1.f) -> _stage_target 16384 (Flux::set_stages,
//      engine/fx/flux.cpp:203-214). setup_fx never calls set_stages, so
//      fx_flux_sdram stays on Flux::init's boot value -- kBootStagesNorm
//      0.8, which is exactly 8192 (engine/fx/flux.cpp:8, 74-77).
//   2. RATE. set_flux_rate(kFluxRateCount - 1) -> slice 11, i.e.
//      kDivisions[kFluxRateOffset + 11] == kDivisions[16] == "1/32", cpb 8,
//      so 16 Hz at 120 BPM and a 0.0625 s delay time (engine/mod/
//      divisions.h:12-19, 48-49; Flux::recompute_time, engine/fx/
//      flux.cpp:96-110). setup_fx never calls set_flux_rate, so
//      fx_flux_sdram stays on init's _rate_idx 3 (engine/fx/flux.cpp:19) --
//      kDivisions[8] "1/4", 2 Hz, a 0.5 s delay.
//   3. FEEDBACK. values[FXT_FLUX_FB] 0.9 rather than setup_fx's 0.7. It is
//      the values[] entry and not a setter that carries this: PartFx pushes
//      the smoothed target into Flux::set_feedback once per sample
//      (engine/fx/part_fx.cpp:38), which is the same path
//      Part::set_fx_target_base(FXT_FLUX_FB, 0.9f) reaches on a real deck.
//   4. DRIVE. set_drive(0.85f) rather than setup_fx's nothing, which leaves
//      Flux::init's own set_drive(0.f) (engine/fx/flux.cpp:81) standing. This
//      axis is NOT independent of axis 3: Flux::set_drive rewrites
//      _fb_scale = 1.2 / bbd_drive_gain(d) and calls apply_feedback
//      (engine/fx/flux.cpp:189-201), so the coefficient the echo actually
//      receives is _fb_norm * _fb_scale (engine/fx/flux.cpp:180) and moving
//      DRIVE moves it. It also moves the saturator's input scale, which
//      BbdEcho sets to bbd_drive_gain(norm) / kSatCeil
//      (engine/fx/bbd.h:560-564) and which decides how often fast_tanh takes
//      its magnitude early return (engine/util/fast_tanh.h:36-37). See the
//      comment in setup_fx_flux_hot for what that does and does not settle.
//   5. A settle in setup, which setup_fx has none of. THIS IS A FIFTH
//      DIFFERENCE. It is not an operating point, and it is not optional:
//      differences 1 and 2 both ride Flux's 30 ms slew, and STAGES'
//      8192 -> 16384 leg needs 12972 samples to close to within the
//      one-stage snap at _dt_coef == 1/1440 -- 8192 * (1 - 1/1440)^n < 1
//      first holds at n == 12972 (engine/fx/flux.cpp:18, 348-357) -- which
//      is 135.1 blocks, MORE than the runner's 100-block warmup
//      (kWarmupBlocks, bench/workload.h:11, applied at bench/runner.cpp:28).
//      Without a settle the first ~35 MEASURED blocks would price a BBD
//      whose line length was still moving and whose _echo.SetStages() was
//      firing per sample. The two readbacks below would also be untrustworthy
//      -- but they fail DIFFERENTLY, and the difference is worth stating
//      because only one of them is actually unreadable. _clock_hz has no
//      writer outside Flux::process: its only assignment in the class is
//      engine/fx/flux.cpp:370, so clock_hz() would still be the 0.f member
//      initialiser (engine/fx/flux.h:144) and its assert would compare
//      against nothing. _stages_now DOES have an init writer
//      (engine/fx/flux.cpp:76; the other assignment is flux.cpp:361, inside
//      process), so stages() would return a stale-but-VALID 8192 -- which is
//      exactly fx_flux_sdram's own value, i.e. a number that looks like a
//      reading and is not one. Neither of those is why the settle is
//      mandatory: the 135.1-blocks-against-100-of-warm-up arithmetic above
//      carries that on its own.
//      fx_flux_sdram needs no settle because it moves neither axis -- init
//      snaps _dt_current (recompute_time(true), engine/fx/flux.cpp:78) and
//      _stage_current (engine/fx/flux.cpp:75-77) -- so the settle does not
//      make the two rows less comparable; it is what makes BOTH of them
//      measured in a settled state. Depth is kInstrSettleBlocks (200), this
//      file's existing settle depth, not a number chosen for this row.
//
// Nothing else differs in configuration. The remaining differences are
// structural and unavoidable for a row added in this translation unit; none
// of them is an operating point:
//   - it is emplaced in g_instr_arena, not g_system_arena, so its PartFx
//     sits at a different address. Both arenas are plain globals -- neither
//     carries BENCH_SRAM_EXEC_BSS, which only bench/mem.cpp's g_sram uses --
//     so this is a different address in the same section, not different
//     memory. The BBD line itself is the identical pointer in both rows:
//     fx_mem().echo[0].
//   - the group carries stages/clock/drive/fb_coef after the PartFx, which
//     FxGroup does not, and proc_fx_flux_hot folds those four into its return
//     value, which proc_fx does not. Same idiom as InstrPartGroup's
//     clock_a/stages_a above, and for the same reason: the asserts' readbacks
//     must not be dead stores. The fold is NOT a detector -- see
//     InstrPartGroup's comment.
struct FxFluxHotGroup {
    PartFx fx;
    float  values[FXT_COUNT];
    int    stages   = 0;
    float  clock_hz = 0.f;
    float  drive    = 0.f;
    float  fb_coef  = 0.f;
};

SerialArena<InstrNoVerbGroup, InstrPartGroup, DeckModGroup, DeckEngineGroup,
            FxFluxHotGroup> g_instr_arena;

void setup_instr_noverb()
{
    auto& group = g_instr_arena.emplace<InstrNoVerbGroup>();
    auto& inst = group.instrument;

    // fx_mem() hands out echo and sampler storage with the SRAM reverb
    // attached; copy it and drop only the reverb. Everything else must stay
    // identical to what instrument_worst_bbd gets, or the subtraction
    // measures the difference in memory rather than the reverb.
    FxMem mem = fx_mem();
    mem.reverb = nullptr;
    inst.init(kSampleRate, mem);
    inst.set_tempo_bpm(120.f);
    group.counter = 0;

    // Mirrors setup_inst_worst + setup_inst_worst_bbd exactly, minus the
    // reverb calls. Omitting them is correct and costs nothing, but not for
    // the obvious reason: set_reverb_size/decay/tone/diffusion/smear/mod are
    // each `if (_reverb)`-guarded (engine/instrument.h) and genuinely have
    // nothing to act on, while set_reverb_mix is NOT guarded
    // (engine/instrument.cpp) -- it would still write _rev_dry_target and
    // _rev_wet_target. Skipping it is still free and still faithful because
    // those targets are only ever read inside Instrument::process's own
    // `if (_reverb)` block, which never runs with a null reverb.
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_color(p, 1.f);
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
    inst.set_master_drive(1.f);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_stages(p, 1.f);
        inst.set_drive(p, 0.85f);
        inst.set_flux_rate(p, kFluxRateCount - 1);
        inst.set_fx_target_base(p, FXT_FLUX_FB, 0.9f);
    }

    const float* in = test_input();
    for (int b = 0; b < kInstrSettleBlocks; ++b)
        inst.process(in, in, group.out_l, group.out_r, kBlock);
}

float proc_instr_noverb()
{
    auto& group = g_instr_arena.get<InstrNoVerbGroup>();
    auto& inst = group.instrument;
    const float* in = test_input();
    inst.process(in, in, group.out_l, group.out_r, kBlock);
    // Same retrigger cadence as proc_inst (bench/workloads_system.cpp): voice
    // occupancy has to match the row this one is subtracted from, or the
    // difference measures voices instead of the reverb.
    if (++group.counter >= 250) {
        group.counter = 0;
        inst.trigger_manual(PART_A);
        inst.trigger_manual(PART_B);
    }
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += group.out_l[i] + group.out_r[i];
    acc += static_cast<float>(inst.active_voices(PART_A));
    acc += static_cast<float>(inst.active_voices(PART_B));
    return acc;
}

void setup_instr_part_common(InstrPartGroup& g, int n_parts)
{
    // Seeds must match Instrument::init's, or the modulation streams differ
    // and so does voice timing: PART_A 0x1234abcd, PART_B 0x9e3779b9
    // (engine/instrument.cpp).
    // Draw every buffer from the same FxMem the Instrument rows get, so the
    // subtraction cannot be measuring different memory. Going through fx_mem()
    // rather than sampler_arena()/kSamplerFrames directly keeps this row on
    // the one accessor whose contents are guaranteed to match.
    const FxMem& mem = fx_mem();
    g.a.init(kSampleRate, 0x1234abcdu, mem.echo[PART_A],
             mem.sampler_buf[PART_A], mem.sampler_frames);
    configure_worst_bbd(g.a);
    if (n_parts == 2) {
        g.b.init(kSampleRate, 0x9e3779b9u, mem.echo[PART_B],
                 mem.sampler_buf[PART_B], mem.sampler_frames);
        configure_worst_bbd(g.b);
    }
    g.counter = 0;

    for (int b = 0; b < kInstrSettleBlocks; ++b) {
        const float* in = test_input();
        for (size_t i = 0; i < kBlock; ++i) {
            float ol, orr, sl, sr;
            g.a.process(in[i], in[i], ol, orr, sl, sr);
            if (n_parts == 2) g.b.process(in[i], in[i], ol, orr, sl, sr);
        }
    }

    // The self-check. STAGES at 1.0 must have settled to kMaxStages, and the
    // "1/32" rate at 120 BPM must have driven the clock onto its ceiling
    // (16384 / (2 * 0.0625) = 131072 Hz, clamped to kClockMaxHz). A row that
    // mirrored the configuration wrongly fails here instead of returning a
    // plausible number.
    g.stages_a = g.a.fx().flux().stages();
    g.clock_a  = g.a.fx().flux().clock_hz();
    assert(g.stages_a == bbd_tuning::kMaxStages);
    assert(g.clock_a >= bbd_tuning::kClockMaxHz);
    if (n_parts == 2) {
        g.stages_b = g.b.fx().flux().stages();
        g.clock_b  = g.b.fx().flux().clock_hz();
        assert(g.stages_b == bbd_tuning::kMaxStages);
        assert(g.clock_b >= bbd_tuning::kClockMaxHz);
    }
}

void setup_instr_part_1()
{
    setup_instr_part_common(g_instr_arena.emplace<InstrPartGroup>(), 1);
}

void setup_instr_part_2()
{
    setup_instr_part_common(g_instr_arena.emplace<InstrPartGroup>(), 2);
}

float proc_instr_part_1()
{
    auto& g = g_instr_arena.get<InstrPartGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float ol, orr, sl, sr;
        g.a.process(in[i], in[i], ol, orr, sl, sr);
        acc += ol + orr + sl + sr;
    }
    if (++g.counter >= 250) { g.counter = 0; g.a.trigger_manual(); }
    acc += static_cast<float>(g.a.active_voices());
    acc += g.clock_a + static_cast<float>(g.stages_a);
    return acc;
}

float proc_instr_part_2()
{
    auto& g = g_instr_arena.get<InstrPartGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float ol, orr, sl, sr;
        g.a.process(in[i], in[i], ol, orr, sl, sr);
        acc += ol + orr + sl + sr;
        g.b.process(in[i], in[i], ol, orr, sl, sr);
        acc += ol + orr + sl + sr;
    }
    if (++g.counter >= 250) {
        g.counter = 0;
        g.a.trigger_manual();
        g.b.trigger_manual();
    }
    acc += static_cast<float>(g.a.active_voices());
    acc += static_cast<float>(g.b.active_voices());
    acc += g.clock_a + static_cast<float>(g.stages_a);
    acc += g.clock_b + static_cast<float>(g.stages_b);
    return acc;
}

void setup_deck_mod_hot()
{
    auto& g = g_instr_arena.emplace<DeckModGroup>();
    // PART_A's seed base, as Part::init passes it: _mod.init(sr, seed_base)
    // with seed_base = 0x1234abcd for PART_A (engine/parts/part.cpp:17,
    // engine/instrument.cpp:22).
    g.mod.init(kSampleRate, 0x1234abcdu);
    g.mod.set_tempo_bpm(120.f);
    g.mod.set_rate(0.8f);
    g.mod.set_density(1.f);

    // Settle to the same depth the Instrument rows settle to, so the row is
    // measured in the state the gate is measured in. mod_plane_2x_center has
    // no settle at all; that difference is part of what this row corrects.
    for (int b = 0; b < kInstrSettleBlocks; ++b)
        for (size_t i = 0; i < kBlock; ++i) g.mod.process();

    // The self-check, same spirit as setup_instr_part_common's stages_a/
    // clock_a asserts above: without it, a silently wrong RATE would just
    // return a plausible-looking float and pass. master_hz() is the readback
    // Task 2's deck_engine_hot row will also take from its own modulator, for
    // a different purpose (deriving a cycle).
    //
    // This covers RATE and the _pitch_scale default, nothing else: _synced is
    // false here (super_modulator.h:187) and nothing on this path calls
    // set_synced(), so _update_rate() takes the FREE branch
    // (super_modulator.cpp:28-29) and _apply_rate() sets
    // master_hz() = free_hz(_rate_norm) * _pitch_scale (super_modulator.cpp:34).
    // _pitch_scale defaults 1.0 and nothing here calls set_rate_scale(), so
    // with RATE 0.8 master_hz() should equal free_hz(0.8f) exactly, less
    // float noise -- checked against the live call rather than a hardcoded
    // ~6.95 Hz so this cannot silently pass if the FREE curve is ever
    // retuned. It also excludes the legacy set_cycle(2.f)'s 0.5 Hz by
    // construction: free_hz(0.8f) is nowhere near 0.5, whereas RATE
    // mistakenly left at 0.5 -- the value mod_plane_2x_center's mod_a uses
    // (bench/workloads_system.cpp:75, not this file) -- would be
    // free_hz(0.5f) =~ 0.775 Hz, which a loose band around 0.5 Hz would have
    // let through.
    //
    // DENSITY is NOT covered, and cannot cheaply be: set_density() only
    // writes ModLane::_density (lane.h:23), read solely by _groove_k()
    // (lane.cpp:422). _effective_gate() calls _groove_k() in both of its
    // melodic branches (lane.cpp:437 and :442); what is step-gated is the call
    // to _effective_gate() itself (lane.cpp:449). This row never calls
    // set_step(), so it runs in
    // FLOW, where _on_boundary() hardcodes `gated = true` regardless of
    // DENSITY (lane.cpp:449) -- the same operating point the real gate runs
    // at (setup_inst_worst never calls set_step either, design spec section
    // 2.4). DENSITY is therefore inert here, not merely untested: nothing
    // this row can read -- lane_fired() included, since a wrap fires
    // unconditionally in FLOW -- would move if DENSITY were silently wrong.
    // free_hz(0.8f) inlined into the assert rather than held in a local, so
    // this does not warn as an unused variable if NDEBUG is ever defined --
    // setup_deck_engine_hot below already uses that form.
    g.master_hz = g.mod.master_hz();
    assert(g.master_hz > 0.f);
    assert(std::fabs(g.master_hz - free_hz(0.8f)) < 1e-4f);
}

float proc_deck_mod_hot()
{
    auto& g = g_instr_arena.get<DeckModGroup>();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        g.mod.process();
        acc += g.mod.lane_output(LANE_PITCH);
    }
    acc += g.master_hz;
    return acc;
}

// Four fixed pitches, the same set setup_engine_2x4 uses (bench/engine_2x4.h,
// kEngine2x4Pitches). setup_deck_engine_hot's own trigger loop below fires
// them once, the same way setup_engine_2x4 does; proc_deck_engine_hot's
// periodic re-fire (DeckEngineGroup's difference #5 above) re-strikes this
// SAME set every fire_period samples. This row and synth_2x4 hold the same
// voice occupancy for the whole measured window BECAUSE of that cadence --
// not merely because the same four pitches are named here (that claim,
// unqualified, was wrong: found in review, see task-2-report.md). Without
// the cadence, the setup's own one-shot quad-trigger below does not even
// establish 4 sustained voices to begin with: each trigger() call's
// chord_slot is hardcoded 0, and in FLOW every chord_slot-0 fire demotes
// whatever the previous one left sustaining (difference #5) -- so by the
// time FLOW's own auto-drone promise fires on the first process() call
// below (sustain_count() == 0 when set_flow(true) ran, so synth_engine.cpp
// :120-122 arms it; it fires at synth_engine.cpp:360-363, retriggering
// SynthEngineT::init's default chord, _chord[0] = _targets[LANE_PITCH] =
// 0.5, synth_engine.cpp:39), the sounding set is actually {0.5, 0.35, 0.45,
// 0.55} from 5 total triggers, and only the LAST of those five is genuinely
// FLOW-sustaining -- the other three are one-shot releases already headed
// for Idle (see the self-check comment below). The chord builder that
// Part::trigger_manual would normally run (part.cpp:149-162) is Part-level
// and belongs in the remainder; what this row needs from it is only the
// number of voices it lands.
constexpr float kDeckEnginePitches[] = { 0.25f, 0.35f, 0.45f, 0.55f };

void setup_deck_engine_hot()
{
    auto& g = g_instr_arena.emplace<DeckEngineGroup>();

    // Two provenances, not one. The modulator and engine init below mirror
    // Part::init for PART_A (engine/parts/part.cpp:17, 19-20); the three
    // modulator settings do NOT appear in Part::init at all -- BPM arrives via
    // Instrument::set_tempo_bpm's fan-out to every part (engine/
    // instrument.cpp:70), and RATE/DENSITY come from the gate's own setup
    // (setup_inst_worst). deck_mod_hot's header comment above draws the same
    // distinction; this one used to claim all six mirrored Part::init.
    g.mod.init(kSampleRate, 0x1234abcdu);
    // Both of these are inert here, for the reasons setup_deck_mod_hot's
    // comment above sets out in full (BPM: _synced is false, so _update_rate()
    // takes the FREE branch and never reads _bpm; DENSITY: only reaches
    // _effective_gate(), which _on_boundary() calls only in STEP). Doubly moot
    // in this row, which never process()es this modulator inside the measured
    // loop -- it reads master_hz() from it and nothing else. Mirrored anyway,
    // so the derived cycle comes from the state the gate actually runs in.
    g.mod.set_tempo_bpm(120.f);
    g.mod.set_rate(0.8f);
    g.mod.set_density(1.f);
    g.synth.set_seed(0x1234abcdu ^ 0x5eedC0DEu);
    g.synth.init(kSampleRate);

    // From here on the engine is reached ONLY through the base pointer.
    g.engine = &g.synth;
    g.engine->set_flow(true);        // boot: lanes boot in FLOW -> drone
    g.synth.set_decay(1.f);          // Part::set_voice_decay(1.0), part.h:139

    // Derive the cycle the way Part::process does: run the modulator to a
    // settled state, read master_hz(), push 1/hz. The modulator is then left
    // alone -- driving it inside the measured loop would pay deck_mod_hot's
    // cost a second time and corrupt both rows (design spec section 3.2).
    for (int b = 0; b < kInstrSettleBlocks; ++b)
        for (size_t i = 0; i < kBlock; ++i) g.mod.process();
    g.master_hz = g.mod.master_hz();
    assert(g.master_hz > 0.f);
    // Checked against the live free_hz(0.8f) call -- same idiom
    // setup_deck_mod_hot uses (this file, above) -- rather than a second
    // hardcoded expectation, so this cannot silently pass if the FREE curve
    // is ever retuned. This single tight-banded check already excludes the
    // legacy set_cycle(2.f)'s 0.5 Hz by construction: free_hz(0.8f) is
    // nowhere near 0.5, so a RATE that silently fell back to 0.5 (the
    // operating point this row exists to move away from) fails this assert.
    // A second, separate `fabs(master_hz - 0.5f) > ...` band used to sit
    // here as well; dropped in review because this equality-strength check
    // makes it unreachable -- fabs(master_hz - free_hz(0.8f)) < 1e-4f
    // already implies fabs(master_hz - 0.5f) > 1e-3f whenever it holds, so
    // the second assert could never fire and its comment ("banded rather
    // than compared for equality") no longer described the code above it.
    assert(std::fabs(g.master_hz - free_hz(0.8f)) < 1e-4f);
    g.engine->set_cycle(1.f / g.master_hz);

    // Re-fire cadence (post-review fix; DeckEngineGroup's difference #5
    // above explains why one is needed at all). A real deck re-strikes its
    // chord roughly once every 1/master_hz seconds -- the same quantity
    // set_cycle just derived (part.cpp:403-407 derives the cycle from it;
    // the fire itself is the `fired` check at part.cpp:409-460). Rounded to
    // the nearest sample and held as a fixed interval: Part re-derives its
    // fire from a live per-sample lane_fired() edge (ModLane/SuperModulator,
    // already priced by deck_mod_hot), which this row does not reproduce --
    // what this row needs to match is the cadence, not its exact phase, so
    // fixed-interval is faithful here. Folded into proc_deck_engine_hot's
    // checksum so it is not a dead store.
    g.fire_period = static_cast<int>(std::lround(kSampleRate / g.master_hz));
    assert(g.fire_period > 0);
    // The fix only works because fire_period lands inside the warmup window
    // and well short of the Idle horizon -- neither is written down anywhere
    // else, and both are consequences of RATE 0.8 (this row's master_hz),
    // not of anything asserted directly. At RATE 0.8, fire_period is 6908
    // samples (~0.144 s) against a 9600-sample (kWarmupBlocks * kBlock,
    // ~0.2 s) warmup and a ~1.54 s Idle horizon (difference #5's
    // decay_s * 80/60), so the first correcting fire lands inside warmup
    // with ~56 ms to spare, and every later fire is ~10x sooner than a
    // demoted voice's Idle time. A future RATE change could push
    // fire_period past the warmup window, silently moving the first
    // corrective fire into the MEASURED window instead of before it --
    // this assert makes that fail loudly (an assert failure) instead of
    // quietly reintroducing the collapse the fix round found. It checks
    // only the warmup half of the margin, not the Idle half, because
    // Idle's ~1.54 s is over ten times looser and the warmup window is the
    // tighter constraint by a wide margin at every RATE this bench reaches.
    assert(g.fire_period < kWarmupBlocks * static_cast<int>(kBlock));
    g.fire_ctr = g.fire_period;

    for (float p : kDeckEnginePitches) g.engine->trigger(p);

    const float* in = test_input();
    for (int b = 0; b < kInstrSettleBlocks; ++b)
        for (size_t i = 0; i < kBlock; ++i) {
            float ol, orr;
            g.engine->process_in(in[i], in[i]);
            g.engine->process(ol, orr);
        }

    // Self-check, same shape as InstrPartGroup's stages_a/clock_a asserts and
    // DeckModGroup's master_hz assert. What it actually covers is narrower
    // than the original comment here claimed (corrected in review): it
    // confirms only that 4 Env instances are non-Idle
    // (active_voices(), engine/synth/synth_engine.cpp:406-411, counts
    // Voice::active() -- engine/synth/voice.h:51 -- which is the same flag
    // Voice::process's cost early-return checks, engine/synth/voice.cpp:114)
    // at this point, ~0.4 s after the trigger loop above ran --
    // NOT that all 4 are FLOW-sustaining, and not that FLOW is even
    // engaged. Only the LAST of the five triggers this row has made by now
    // (four explicit plus FLOW's auto-drone; see the comment on
    // kDeckEnginePitches above) is genuinely sustaining; the other three are
    // one-shot releases already headed for Idle around 1.54 s
    // (difference #5, DeckEngineGroup's comment) if nothing re-strikes them
    // first. The assert also cannot over-report -- SynthEngine::kVoices is 4
    // (engine/synth/synth_engine.h:35), so 4 is the hard ceiling as well as
    // the count asked for -- and it would pass identically even if
    // set_flow(true) above had never been called: STEP-mode trigger() calls
    // populate the same 4 envelopes on the same decay clock, with no notion
    // of "sustaining" to tell FLOW and STEP apart at this single readback.
    // What actually holds occupancy at 4 for the WHOLE measured window is
    // proc_deck_engine_hot's periodic trigger_chord() cadence below
    // (fire_period/fire_ctr), not this assert -- this assert only catches a
    // setup that silently failed to trigger anything at all.
    g.voices = g.synth.active_voices();
    assert(g.voices == 4);
}

float proc_deck_engine_hot()
{
    auto& g = g_instr_arena.get<DeckEngineGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float ol, orr;
        // Both calls through the base pointer, in Part::process's order:
        // process_in first, then process (part.cpp:482-483).
        g.engine->process_in(in[i], in[i]);
        g.engine->process(ol, orr);
        acc += ol + orr;
        // Re-fire cadence (post-review fix, DeckEngineGroup's difference #5
        // above): a real deck never lets its FLOW voices sit un-restruck
        // long enough to release past Idle (part.cpp:409-460's `fired`
        // check, roughly once every 1/master_hz seconds). ONE
        // trigger_chord() call, not four trigger() calls -- chord_slot 0
        // only demotes the current surface on a chord's OWN root note
        // (synth_engine.cpp:206), so the other three notes in this call add
        // to it rather than replacing it, landing 4 voices as non-Idle
        // (Voice::process branches on `_env.active()`, never on
        // `_sustaining`, voice.cpp:113-114) -- non-Idle is the quantity this
        // row's cost actually tracks, and the cadence holds it at 4.
        //
        // The FLOW *surface* does NOT stay at 4, and that is expected, not a
        // second collapse to fix: nothing here ever calls set_chord(), so
        // `_chord_n` stays at init()'s value of 1 (synth_engine.cpp:38-39)
        // -- trigger_chord() never writes `_chord_n` either. Once this
        // fire's three pending notes land (~8 ms, kStabSpreadS,
        // synth_engine.h:42), `_update_control()`'s next few ticks call
        // `_adjust_surface()` (gated on `_pending_n == 0`, synth_engine.cpp
        // :263) with `want = 1`, `m = 4` (synth_engine.cpp:244), and take
        // the collapse branch (synth_engine.cpp:251-254), demoting the
        // highest-chord_slot voice one per tick until only slot 0 remains
        // sustaining (~3 ticks, ~6 ms) -- and that survivor's pitch gets
        // pulled to `_chord[0]` (0.5, synth_engine.cpp:39) by the same
        // per-tick pitch-follow loop (synth_engine.cpp:339-344), regardless
        // of what pitch it was struck at. set_chord() is a `_control_tick`
        // push design spec section 4 deliberately excludes from this row
        // (the remainder); this collapse is that exclusion's visible
        // consequence, not an omission to fix here. It costs nothing extra:
        // the three demoted voices stay non-Idle, releasing toward 0 over
        // ~1.54 s (difference #5's timing) same as before, and the NEXT
        // fire -- ~0.144 s later, far sooner -- steals them again before
        // any of them gets there.
        // Through the base pointer, same as process_in/process above --
        // trigger_chord is virtual on IPartEngine (engine_iface.h).
        if (--g.fire_ctr <= 0) {
            g.fire_ctr = g.fire_period;
            g.engine->trigger_chord(kDeckEnginePitches, 4);
        }
    }
    acc += static_cast<float>(g.synth.active_voices());
    // The cadence's invariant, checked live rather than inferred. The value is
    // already computed one line up and folded into the checksum -- but this
    // file's own header comment (above, on the checksum fold) says plainly
    // that the fold is not a detector: a collapse changes the checksum, and a
    // changed checksum is indistinguishable from any other change. Until this
    // assert, occupancy across the measured window was held only by the
    // reasoning in setup's fire_period comment, and a collapse is exactly the
    // failure the review round found once already -- a future RATE, decay or
    // cadence change would reintroduce it and still return a plausible
    // number. It holds for every block: the four triggers at setup leave 4
    // Envs non-Idle with an Idle horizon of ~1.54 s (difference #5), the
    // first corrective fire lands at ~0.544 s, and every later fire is ~10x
    // sooner than that horizon, so nothing ever reaches Idle. kVoices is 4
    // (synth_engine.h:35), so 4 is the ceiling as well as the expectation.
    // Cost is one compare per block against this row's ~172000 cycles.
    assert(g.synth.active_voices() == 4);
    acc += g.master_hz + static_cast<float>(g.voices)
         + static_cast<float>(g.fire_period);
    return acc;
}

void setup_fx_flux_hot()
{
    auto& g = g_instr_arena.emplace<FxFluxHotGroup>();

    // --- setup_fx(SEL_FLUX) verbatim (bench/workloads_system.cpp:182-203).
    // Same accessor, same echo buffer index, same order. sel == SEL_FLUX
    // resolves the three booleans/amounts below; they are written out rather
    // than parameterised because this row has exactly one selector value.
    const FxMem& m = fx_mem();
    g.fx.init(kSampleRate, m.echo[0]);
    g.fx.set_fx_on(FxBlock::Grit, false, true);   // sel != SEL_GRIT
    g.fx.set_fx_on(FxBlock::Flux, true,  true);   // sel == SEL_FLUX
    g.fx.set_comp(0.f);                           // sel != SEL_COMP
    g.fx.set_grit_mix(1.f);
    g.fx.set_flux_mix(1.f);
    g.fx.set_bpm(120.f);

    g.values[FXT_GRIT_INT]  = 0.8f;
    g.values[FXT_FLUX_TIME] = 0.5f;
    g.values[FXT_FX_MIX]    = 1.f;
    g.values[FXT_REV_SEND]  = 0.5f;
    g.values[FXT_FLUX_FB]   = 0.7f;

    // --- and now the deck's own operating point, differences 1-4 in
    // FxFluxHotGroup's comment above, in configure_worst_bbd's own call order
    // (bench/workloads_instr.cpp:107-110) minus the lines there that do not
    // address FLUX.
    //
    // DRIVE is one of the four axes, not a footnote left for another row.
    // There is no other row: bench/workloads_sweep.cpp registers only
    // sweep_flux_rate_*, sweep_stages_*, sweep_grit_*, sweep_flux_lines_2ch
    // and sweep_room_* (its table, bench/workloads_sweep.cpp:880-894), so the
    // sweep family has no DRIVE row at all, and the only set_drive rows
    // anywhere in the bench are lim_clean/lim_driven
    // (bench/workloads_abl.cpp:186-187), which move the master Limiter's
    // pre-gain (engine/fx/limiter.h:29) and never touch Flux. DRIVE's cost is
    // this row's question or nobody's.
    //
    // What reading DOES establish is that DRIVE cannot be omitted without
    // changing the regime, because it is coupled to FEEDBACK and to the
    // saturator at once: at DRIVE 0 the coefficient handed to the echo is
    // 0.9 * 1.2 == 1.08 and the saturator sees x * (1 / kSatCeil) == x *
    // 1.111; at the deck's 0.85 the coefficient is 0.9 * 1.2 / 3.236 ==
    // 0.334 and the saturator sees x * (3.236 / kSatCeil) == x * 3.596. What
    // reading does NOT establish is which of the two costs more. The echo's
    // small-signal loop gain is feedback * g either way -- ~1.08 at both
    // settings, which is precisely the invariant Flux::apply_feedback's
    // division exists to hold (engine/fx/flux.cpp:148-182,
    // engine/fx/bbd.h:531-546) -- so the two differ in where fast_tanh's
    // magnitude branch falls, not in whether the loop blooms. Design spec
    // section 6.6 argues a direction for that; this comment deliberately does
    // not restate it as fact. The row's justification needs neither: a deck
    // runs 0.85, so this row runs 0.85.
    //
    // Ordering. Against the values[] write below, set_drive's position is
    // immaterial: values[] is this file's array and reaches Flux only through
    // PartFx::process, which pushes FXT_FLUX_FB into Flux::set_feedback
    // (engine/fx/part_fx.cpp:38) before calling _flux.process
    // (engine/fx/part_fx.cpp:60), so nothing in Flux has seen 0.9 until the
    // settle's first sample -- by which time _fb_scale is already 0.85's.
    // set_drive's own claim of order-independence (engine/fx/flux.cpp:196-198)
    // therefore holds on this path, and holds even for the coefficient if
    // set_drive were moved AFTER the settle, since apply_feedback re-derives
    // from _fb_norm rather than from the coefficient in force. What would NOT
    // survive that move is the STATE: 200 blocks of settle would run at
    // coefficient 1.08 and saturator scale 1.111, i.e. entirely in the regime
    // this row exists not to measure, and all four asserts below would still
    // pass. Hence: before the settle.
    g.fx.set_stages(1.f);                      // -> kMaxStages, 16384
    g.fx.set_drive(0.85f);                     // the deck's DRIVE, line 108
    g.fx.set_flux_rate(kFluxRateCount - 1);    // -> "1/32", clock ceiling
    g.values[FXT_FLUX_FB] = 0.9f;              // overrides setup_fx's 0.7

    // --- difference 5: the settle. See FxFluxHotGroup's comment for why it
    // is required and why it does not cost comparability.
    const float* in = test_input();
    for (int b = 0; b < kInstrSettleBlocks; ++b)
        for (size_t i = 0; i < kBlock; ++i) {
            float l = in[i], r = in[i] * 0.9f, sl = 0.f, sr = 0.f;
            g.fx.process(l, r, sl, sr, g.values);
        }

    // The self-check, one assert per axis, four in all -- these two, then
    // DRIVE and FEEDBACK below. The failure this pair guards is the one that
    // produces a plausible, worthless number: set_stages or set_flux_rate
    // silently not taking, leaving this row measuring fx_flux_sdram a second
    // time under a new name. The two are complementary and neither is
    // redundant --
    //   - stages() is independent of RATE, so the first assert alone would
    //     pass with the rate stuck at init's index 3;
    //   - the clock is clamped to kClockMaxHz, so the second assert alone
    //     would pass with STAGES stuck at 8192: 8192 / (2 * 0.0625) is
    //     65536 Hz, still over the 32000 Hz ceiling (bbd_clock_hz,
    //     engine/fx/bbd.h:201-206, bbd_tuning::kClockMaxHz, bbd.h:88).
    // Values under the mistakes: STAGES stuck -> stages() == 8192 against
    // kMaxStages 16384, first assert fires. RATE stuck -> _delay_time 0.5 s,
    // so clock_hz() == 16384 / (2 * 0.5) == 16384 Hz, under the ceiling, and
    // the second assert fires. Both stuck (the row that IS fx_flux_sdram) ->
    // 8192 stages and an 8192 Hz clock, and both fire.
    //
    // Settled, the clock sits exactly ON its ceiling rather than merely near
    // it: bbd_clock_hz(0.0625, 16384) is 131072 Hz before the clamp, and
    // FXT_FLUX_TIME 0.5 contributes _time_mult == 1 exactly (bbd_time_mult's
    // 65-row LUT: the table is built at engine/fx/bbd.h:218-225 with
    // t[i] == 2^(4*(i/64 - 0.5)), and the indexing and lerp at
    // engine/fx/bbd.h:226-230 turn 0.5 into p == 32.f exactly, so i == 32,
    // the fraction is 0 and the return is table[32] == 2^0 untouched by the
    // interpolation), so >= is an equality test here in every non-broken
    // case.
    g.stages   = g.fx.flux().stages();
    g.clock_hz = g.fx.flux().clock_hz();
    assert(g.stages   == bbd_tuning::kMaxStages);
    assert(g.clock_hz >= bbd_tuning::kClockMaxHz);

    // DRIVE's own readback -- difference 4, which neither assert above can
    // see. It is a genuine readback and not a restated law, because Flux
    // already exposes one: drive_norm_for_test() at engine/fx/flux.h:74
    // returns _drive_norm, the value set_drive stored after its own clampf
    // (engine/fx/flux.cpp:191-193). No getter had to be added to engine/,
    // which this round has locked. The compare is exact rather than banded on
    // purpose: clampf(0.85f, 0.f, 1.f) returns its argument bit-for-bit, so
    // there is no arithmetic between the push and the readback that could
    // round, and an exact compare is the strongest form available here.
    // Under the mistake it guards -- set_drive silently not taking, e.g. this
    // line deleted or moved behind a guard that swallowed it -- it sees
    // Flux::init's set_drive(0.f) value, 0.f (engine/fx/flux.cpp:81), and
    // fires.
    g.drive = g.fx.flux().drive_norm_for_test();
    assert(g.drive == 0.85f);

    // FEEDBACK's own readback, which none of the three asserts above can see.
    // This is the ONE check in this row that restates an engine law instead of
    // reading a derived quantity back, so it is also the one that would need
    // updating if FLUX's feedback law moved: Flux::apply_feedback hands the
    // echo _fb_norm * _fb_scale (engine/fx/flux.cpp:180), and _fb_scale is
    // 1.2 / bbd_drive_gain(_drive_norm), rewritten by set_drive
    // (engine/fx/flux.cpp:199). This row DOES call set_drive, so _drive_norm
    // is 0.85 and bbd_drive_gain(0.85f) is 10^(0.85 * 12 * 0.05) == 10^0.51
    // == 3.2359 (engine/fx/bbd.h:176-177, 188-193): the expected coefficient
    // is 0.9 * 1.2 / 3.2359 == 0.3337. _fb_norm is exactly 0.9 because
    // PartFx's 2 ms OnePole was reset() to the first pushed value on the first
    // process() call (engine/fx/part_fx.cpp:26-29) and then returns it
    // unchanged (engine/util/onepole.h, the !_smoothing early return).
    //
    // Two distinct mistakes reach it, and it fires on both:
    //   - values[FXT_FLUX_FB] left at setup_fx's 0.7 -> 0.7 * 1.2 / 3.2359
    //     == 0.2596 against 0.3337, a miss of 0.074;
    //   - set_drive not taking -> _fb_scale stays init's 1.2 / 1 and the
    //     coefficient is 0.9 * 1.2 == 1.08 against 0.3337, a miss of 0.746.
    // The second overlaps the DRIVE assert above deliberately: that one shows
    // the knob was STORED, this one shows the coupling actually reached the
    // echo, and only the pair distinguishes a failed FEEDBACK push from a
    // failed DRIVE push. The 1e-3 tolerance is 74x smaller than the nearer of
    // the two misses; it is sized only to absorb the difference in multiply
    // order between the engine's _fb_norm * (1.2 / g) and this line's
    // (0.9 * 1.2) / g, which is a last-bit effect.
    g.fb_coef = g.fx.flux().feedback_coef_for_test();
    assert(std::fabs(g.fb_coef - 0.9f * 1.2f / bbd_drive_gain(0.85f)) < 1e-3f);
}

// proc_fx's shape (bench/workloads_system.cpp:209-220), line for line: the
// same r = in[i] * 0.9f stereo skew, the same zeroed sends, the same
// accumulation of all four. The only addition is the readback fold, which
// keeps setup's four asserted values from being dead stores.
float proc_fx_flux_hot()
{
    auto& g = g_instr_arena.get<FxFluxHotGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float l = in[i], r = in[i] * 0.9f, sl = 0.f, sr = 0.f;
        g.fx.process(l, r, sl, sr, g.values);
        acc += l + r + sl + sr;
    }
    acc += static_cast<float>(g.stages) + g.clock_hz + g.drive + g.fb_coef;
    return acc;
}

} // namespace

const Workload kInstrWorkloads[] = {
    { "instr", "instr_part_1", setup_instr_part_1, proc_instr_part_1 },
    { "instr", "instr_part_2", setup_instr_part_2, proc_instr_part_2 },
    { "instr", "instr_noverb", setup_instr_noverb, proc_instr_noverb },
    { "instr", "deck_mod_hot", setup_deck_mod_hot, proc_deck_mod_hot },
    { "instr", "deck_engine_hot", setup_deck_engine_hot, proc_deck_engine_hot },
    { "instr", "fx_flux_hot", setup_fx_flux_hot, proc_fx_flux_hot },
};
const int kInstrCount = sizeof(kInstrWorkloads) / sizeof(kInstrWorkloads[0]);

} // namespace bench
