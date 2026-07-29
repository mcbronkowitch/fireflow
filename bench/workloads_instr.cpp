#include <cassert>
#include <cmath>
#include "workload.h"
#include "families.h"
#include "mem.h"
#include "serial_arena.h"
#include "instrument.h"
#include "parts/part.h"
#include "parts/engine_iface.h"
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
//      set_decay(1.0) at THIS row's derived cycle (~0.144 s, not the old
//      set_cycle(2.f)'s 16 s), decay_s is ~1.15 s
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

SerialArena<InstrNoVerbGroup, InstrPartGroup, DeckModGroup, DeckEngineGroup> g_instr_arena;

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
    // mistakenly left at 0.5 -- the value mod_plane_2x_center's mod_a uses,
    // two rows up in this same file's history -- would be free_hz(0.5f) =~
    // 0.775 Hz, which a loose band around 0.5 Hz would have let through.
    //
    // DENSITY is NOT covered, and cannot cheaply be: set_density() only
    // writes ModLane::_density (lane.h:23), read solely by _groove_k()
    // (lane.cpp:422), which _effective_gate() only consults when _step_mode
    // is true (lane.cpp:449). This row never calls set_step(), so it runs in
    // FLOW, where _on_boundary() hardcodes `gated = true` regardless of
    // DENSITY (lane.cpp:449) -- the same operating point the real gate runs
    // at (setup_inst_worst never calls set_step either, design spec section
    // 2.4). DENSITY is therefore inert here, not merely untested: nothing
    // this row can read -- lane_fired() included, since a wrap fires
    // unconditionally in FLOW -- would move if DENSITY were silently wrong.
    const float expected_hz = free_hz(0.8f);
    g.master_hz = g.mod.master_hz();
    assert(g.master_hz > 0.f);
    assert(std::fabs(g.master_hz - expected_hz) < 1e-4f);
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

    // Mirrors Part::init for PART_A (engine/parts/part.cpp:16-45).
    g.mod.init(kSampleRate, 0x1234abcdu);
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
    // (Voice::process, engine/synth/voice.cpp:114, `if (!_env.active())
    // return;`) at this point, ~0.4 s after the trigger loop above ran --
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
        // to it rather than replacing it, landing as 4 genuinely sustaining
        // voices instead of self-cannibalizing to 1 (see difference #5).
        // Through the base pointer, same as process_in/process above --
        // trigger_chord is virtual on IPartEngine (engine_iface.h).
        if (--g.fire_ctr <= 0) {
            g.fire_ctr = g.fire_period;
            g.engine->trigger_chord(kDeckEnginePitches, 4);
        }
    }
    acc += static_cast<float>(g.synth.active_voices());
    acc += g.master_hz + static_cast<float>(g.voices)
         + static_cast<float>(g.fire_period);
    return acc;
}

} // namespace

const Workload kInstrWorkloads[] = {
    { "instr", "instr_part_1", setup_instr_part_1, proc_instr_part_1 },
    { "instr", "instr_part_2", setup_instr_part_2, proc_instr_part_2 },
    { "instr", "instr_noverb", setup_instr_noverb, proc_instr_noverb },
    { "instr", "deck_mod_hot", setup_deck_mod_hot, proc_deck_mod_hot },
    { "instr", "deck_engine_hot", setup_deck_engine_hot, proc_deck_engine_hot },
};
const int kInstrCount = sizeof(kInstrWorkloads) / sizeof(kInstrWorkloads[0]);

} // namespace bench
