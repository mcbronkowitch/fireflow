#include <cassert>
#include <cmath>
#include "workload.h"
#include "families.h"
#include "mem.h"
#include "serial_arena.h"
#include "instrument.h"
#include "parts/part.h"
#include "parts/engine_iface.h"
#include "parts/test_tone_engine.h"
#include "fx/part_fx.h"
#include "synth/synth_engine.h"
#include "mod/super_modulator.h"
#include "mod/lane_id.h"
#include "mod/lane.h"
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
// delay_target_b remains 0). The glue subtraction is therefore
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
    float delay_target_a = 0.f, delay_target_b = 0.f;
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
    //     setup_inst_worst_bbd never touches LINK, so _link stays 0 and THIN
    //     is inert on either side of the comparison.
void configure_worst_tape_flux(Part& part)
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
// (engine/instrument.cpp:70) and what configure_worst_tape_flux does for
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
    // shape as InstrPartGroup's delay-target readback above.
    float master_hz = 0.f;
};

// One bare ModLane, driven exactly as SuperModulator drives LANE_PITCH.
//
// Task 12 (spec 2026-08-12-modulation-pace): lane.h:196-205 moved _phase and
// _phase_inc from float to double, because a float increment below half an
// ulp of the current binade rounds away entirely and the lane freezes --
// measured stalls at PACE's slow end, where free_hz's floor divided by 32
// leaves as little as ~0.000625 Hz. -mfpu=fpv5-d16 makes that double add a
// real hardware instruction, not a software-emulated one, but it is still
// roughly twice float's latency, and ModLane::process() pays it once per
// sample on the PITCH lane regardless of rate. deck_mod_hot above prices a
// whole SuperModulator, but only LANE_PITCH calls process() per sample there
// (super_modulator.cpp:109); the other four lanes call tick() once per
// 96-sample raster instead, so the per-sample double add's cost is diluted
// across deck_mod_hot's row. This row calls process() alone, every sample,
// so that cost is visible on its own line rather than averaged down.
struct ModLaneGroup {
    ModLane lane;
};

// One SynthEngine, driven exactly as Part::process drives it.
//
// Differences from synth_2x4 (bench/engine_2x4.h), each deliberate and each
// part of what this row corrects:
//
//   1. Called through an IPartEngine*, not a concrete SynthEngine&. Part
//      holds `IPartEngine* _engine` (engine/parts/part.h:383) and every
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
//      Part::process derives it (part.h:255-256, whose body is
//      Part::_push_master_cycle, part.cpp:421-424), not from the constant
//      set_cycle(2.f) the old row uses.
//   4. process_in() is called every sample, before process()
//      (Part::process, part.h:315). proc_engine_2x4 never calls it, so the
//      35.80 points
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
//      nch) (Part::_fire_trigger, part.cpp:429-442, whose guard is at
//      part.h:296), which demotes only on the chord's OWN root
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

// One PartFx with stereo tape FLUX at the deck's hot operating point: the
// shortest RATE division and feedback 0.9. The setup settles the shared time
// slew, then asserts and folds the achieved delay target so the row cannot
// silently duplicate the boot-rate context. BBD stage/clock/drive state is
// deliberately absent.
struct FxFluxHotGroup {
    PartFx fx;
    float  values[FXT_COUNT];
    float  delay_target_s = 0.f;
};

// One TestToneEngine, driven exactly as Part::process drives its engine.
//
// Why the row exists at all: a shell pays Part-level code PLUS the FX shell
// PLUS whatever engine it runs PLUS the modulation plane, so Part-level code
// is only readable as
//   deck_shell - fx_none - tone_solo - deck_mod_hot
// (design spec sections 3.3 and 4.1). This row is the tone term, and without
// it deck_shell is uninterpretable. The modulation term was missing from the
// formula this comment used to state: Part::process runs _mod.process() every
// sample (Part::process, engine/parts/part.h:246), so deck_shell contains the
// plane that
// deck_mod_hot prices, and the three-term form charged it twice -- once inside
// Part-level code and once beside it. See section 9.2, which traces all three
// of the round's prediction misses to that one omission.
//
// What it reproduces from Part::process, and what it does not:
//
//   1. The engine is reached ONLY through an IPartEngine*, never through the
//      concrete TestToneEngine. Part holds `IPartEngine* _engine`
//      (engine/parts/part.h:383), pointing at its own TestToneEngine member
//      (part.h:382) once ENGINE_TEST_TONE is selected, and every IPartEngine
//      method is virtual (engine/parts/engine_iface.h:25-28), so a Part can
//      inline none of them. A concrete `TestToneEngine&` here would let the
//      compiler inline process() outright, and the body it would inline is,
//      read off the emitted code rather than off the C++ (corrected in
//      review, which found the divide missing and the multiply count one
//      short): a single-precision DIVIDE for `_freq / _sr` (`vdiv.f32`), the
//      phase increment, a BRANCHLESS wrap (`it ge` / `vsubge.f32`, not a
//      taken branch), one `sinf`, and THREE multiplies -- `_phase * TWO_PI`,
//      `* _amp`, `* 0.3f`. The divide is named because it is the
//      second-costliest operation in the body after `sinf` (design spec
//      section 3.1, corrected for the same omission). Nothing stops that
//      inlining, and the row would then price something no Part ever pays.
//      Same trap and same
//      resolution as DeckEngineGroup's difference #1 above; on THIS engine
//      the trap is sharper, because the body is small enough to disappear
//      into the loop entirely.
//   2. process_in(in, in) then process(ol, orr), per sample, in that order
//      (Part::process, engine/parts/part.h:315-316). TestToneEngine does NOT
//      override process_in: it inherits IPartEngine's empty body
//      (engine/parts/engine_iface.h:59). So on this row that call is ONE
//      VIRTUAL DISPATCH AND NO COMPUTE -- it is one of the two dispatches per
//      sample that point 1 already charges for, not a second cost stacked on
//      top of them. Round 2's design section 2.2 conflated the two and had to
//      be corrected; the compute/dispatch distinction it drew is real only on
//      a SAMPLER deck, where SamplerEngine::process_in
//      (engine/sampler/sampler_engine.cpp:158) genuinely records and monitors.
//      Not this row's engine, and not deck_engine_hot's either (difference #4
//      above carries the same correction).
//   3. set_targets() once per SynthEngine::kCtrlInterval samples
//      (engine/synth/synth_engine.h:36, == 96, == kBlock), which is the raster
//      Part::_control_tick runs on. Read off part.h directly:
//      `if (_ctrl_ctr == 0) { _ctrl_ctr = SynthEngine::kCtrlInterval;
//      _control_tick(); } else if (fired) { _control_tick(); } --_ctrl_ctr;`
//      (Part::process, part.h:288-294), and _control_tick's own push into the
//      engine is `_engine->set_targets(_tg, _tune)` (part.cpp:348). Two cadence
//      facts follow and both are reproduced here exactly: _ctrl_ctr initialises
//      to 0 (part.h:376, re-zeroed in Part::init at part.cpp:74), so the tick
//      lands on the FIRST sample of each 96-sample group, not the last; and the
//      raster block sits ABOVE the engine calls in the same process() body
//      (part.h:288-294 against 315-316), so the push precedes that sample's
//      process_in/process rather than following it. This row's ctrl_ctr
//      mirrors that counter for counter and lives in the group for the same
//      reason Part's lives in the object: it has to survive across process()
//      calls.
//
//      What the row does NOT reproduce is the fire refresh at part.h:291-293
//      -- a SECOND _control_tick() on any sample where LANE_PITCH fired, i.e.
//      one extra set_targets() roughly every 1/master_hz seconds, which
//      setup_deck_engine_hot's fire_period puts at ~6908 samples (~72 blocks)
//      at the gate's RATE 0.8. A deck therefore makes ~1.4% more set_targets
//      calls than this row does, and that fraction of one push per 72 blocks
//      stays on deck_shell's side of the subtraction. Reproducing it would
//      need a live lane_fired() edge, i.e. deck_mod_hot's modulator running
//      inside the measured loop, which would pay that row's cost twice and
//      corrupt both (design spec section 3.2).
//   4. Nothing else, deliberately. _control_tick's target build, the
//      quantizer, the chord builder and the FX target cache are Part-level and
//      belong to deck_shell, as does the _engine_fade multiply
//      (part.h:317-318). This row pushes a fixed target array, so it prices
//      the engine's set_targets and nothing upstream of it.
//
// The two targets that matter are the ones a deck actually produces:
//   - LANE_LEVEL 0.8. That is Part's own `_base[LANE_LEVEL]` (part.h:570) and
//     its boot `_tg[LANE_LEVEL]` (part.h:526) -- the value a deck at MOD 0
//     pushes verbatim. At the gate's MOD 1.0 the lane moves it, floored at
//     kLevelFloor * base == 0.4 * 0.8 == 0.32 (part.h:585, part.cpp:107-108);
//     0.8 is the base that swing is taken around.
//   - LANE_PITCH 0.5, likewise `_base[LANE_PITCH]` (part.h:570) and the boot
//     `_tg[LANE_PITCH]` (part.h:526). On a deck this slot arrives already
//     quantized and tuned (part.cpp:222-223), which is exactly what
//     TestToneEngine::set_targets assumes of it (test_tone_engine.h:12-15), so
//     handing it a plain 0.5 is the right shape rather than a shortcut. It
//     maps to 110 * 8^0.5 == 311.13 Hz (test_tone_engine.h:22).
//   - The tune argument is 0.5, Part's `_tune` default (part.h:610).
//     TestToneEngine::set_targets ignores it entirely (test_tone_engine.h:20,
//     `float /*tune*/`), so it is there for faithfulness and cannot move cost.
//   - The other three slots stay 0. TestToneEngine reads only LANE_PITCH and
//     LANE_LEVEL (test_tone_engine.h:21-23), so nothing reads them; the array
//     is LANE_COUNT wide because that is set_targets' contract
//     (`const float* targets /*[LANE_COUNT]*/`, engine_iface.h:26).
//
// One approximation, named rather than buried: the pitch target is CONSTANT
// here, where a deck's walks the quantizer's staircase from tick to tick.
// set_targets calls std::pow(8.f, p) (test_tone_engine.h:22); whether powf's
// cost depends on its argument is NOT something reading settles, so if it
// does, this row prices that call at one fixed argument. Either way it is one
// call per 96 samples, not per sample.
struct ToneSoloGroup {
    TestToneEngine tone;
    IPartEngine*   engine = nullptr;
    float          targets[LANE_COUNT] = { 0.f, 0.f, 0.f, 0.f, 0.f };
    // Part's _ctrl_ctr, mirrored (point 3 above). In the group, not a
    // function-local static, because it must survive across process() calls --
    // the same reason DeckEngineGroup keeps fire_ctr here.
    int            ctrl_ctr = 0;
    // Self-check readback: the largest |outL| seen in setup's check window,
    // asserted there and folded into proc_tone_solo's return value so it is
    // not a dead store. Same idiom as InstrPartGroup's delay-target readback, and
    // subject to the same non-claim: the fold is not a detector.
    float          peak = 0.f;
};

// One whole Part at the gate's operating point, running the cheapest engine
// there is, with every FX block off. This is the shell row: it prices
// Part-level code -- the per-sample loop in Part::process, the control raster
// and its two entry paths, _control_tick's target build, the quantizer, the
// chord builder, the FX target-cache fill, the excitation bus and the
// _engine_fade multiply -- but it prices them WITH the FX shell, the tone AND
// the modulation plane riding along, so the quantity this round wants is
//   deck_shell - fx_none - tone_solo - deck_mod_hot
// (design spec sections 3.3 and 4.1, and section 6.10 on why that subtraction
// removes the harness two times too many). The modulation term is the one this
// comment used to omit: Part::process calls _mod.process() every sample
// (engine/parts/part.h:246), so a whole Part contains the plane deck_mod_hot
// prices, and subtracting it only in the remainder' line charged it twice
// (design spec sections 3.3 and 9.2).
//
// The operating point is setup_instr_part_common's, i.e. configure_worst_tape_flux
// above: PART_A's 0x1234abcd seed base, the same FxMem buffers, BPM 120,
// COLOR 1.0, DENSITY 1.0, MOD 1.0, RATE 0.8, VOICE DECAY 1.0, one
// trigger_manual(), FLOW (nothing calls set_step, on either row). It departs
// in exactly TWO ways, both deliberate, both required, and nothing else
// differs in configuration.
//
// DEPARTURE 1 -- set_engine(ENGINE_TEST_TONE), run to completion.
//
// The switch is faded, not immediate (design spec section 3.2). set_engine
// only arms it: _switching goes true and _engine_fade.set_on(false) is called
// WITHOUT the immediate flag (engine/parts/part.cpp:132-137), so the swap
// happens later, inside process(), at the fade's idle point
// (Part::_engine_swap, engine/parts/part.cpp:401-417) -- where the freshly
// selected engine also
// gets set_flow, set_hold, set_gate and set_cycle re-pushed into it, and
// _ctrl_ctr is re-armed to 0 so the same sample runs a control tick.
//
// HOW LONG THAT TAKES, counted off SoftSwitch (engine/fx/fx_util.h:66-116),
// one _engine_fade.process() call per Part::process call
// (engine/parts/part.h:251). Part::init leaves the switch at Stage::hold via
// set_on(true, true) (engine/parts/part.cpp:34):
//   - call 1: the `hold` case emits 1.0, forces _iterator to 191 and, seeing
//     !_on, moves to `fall` (fx_util.h:94-98). Because `hold` writes 191
//     itself, this count does not depend on what _iterator held before.
//   - calls 2-192: the `fall` case, 191 of them; --_iterator walks 190 down to
//     0 and the stage becomes `idle` when it hits 0 (fx_util.h:99-103), i.e.
//     ON call 192.
//   - the SWAP lands on that same call 192, because process() tests
//     is_idle() AFTER _engine_fade.process() has returned (part.h:251-252).
//     set_on(true) re-arms from `idle`.
//   - call 193: the `idle` case emits 0.0 and moves to `rise` (fx_util.h:84-88).
//   - calls 194-384: the `rise` case, 191 of them; ++_iterator reaches 191 on
//     call 384 and the stage becomes `hold` (fx_util.h:89-93).
//   - call 385: `hold` again, so the multiplier is exactly 1.0 for the first
//     time since the switch.
// So: 192 samples to the swap, 385 samples until the fade multiplier is back
// at 1.0. The 191 is the counter's own literal, not a sample-rate derivation
// -- _kof = 1/(0.004*sr) (fx_util.h:72) only scales the Hann LOOKUP
// (fx_util.h:91, 101), so the leg is 191 samples long at any sample rate.
// 385 samples is 4.01 blocks of 96. The settle below is kInstrSettleBlocks
// (200) = 19200 samples, 49.9x that, which is where the number comes from: it
// is this file's existing settle depth, chosen so this row settles as deep as
// instr_part_1 does, and it clears the fade by nearly two orders of magnitude
// rather than being sized for it.
//
// DEPARTURE 2 -- every FX block off at fx_none's EXACT operating point.
//
// Not merely "off": off the way setup_fx(SEL_NONE) has it off
// (bench/workloads_system.cpp:182-203), including the values[] entries, which
// are part of that operating point. deck_shell - fx_none is the whole reason
// this row exists, and any mismatch here turns that subtraction into a fourth
// operating-point discrepancy instead of a clean difference. The mirror,
// call by call, is written out in setup_deck_shell below and asserted there.
//
// What is NOT mirrored, because it cannot be, and why none of it is an
// operating point:
//   - The FX shell's INPUT SIGNAL. fx_none feeds PartFx test_input() with a
//     r = in[i] * 0.9f stereo skew; on a Part, PartFx is fed the engine's
//     output times the fade (part.h:317-320), i.e. the tone. This is
//     structural -- a Part cannot hand its own FX anything else -- and it is
//     cost-neutral, because with both blocks off PartFx::process does no
//     input-dependent work: the outer `_grit.engaged() || _flux.engaged()`
//     branch is skipped entirely (part_fx.cpp:33, 80-86), Comp::process takes
//     its bit-exact bypass return on `!engaged()` at amount 0
//     (engine/fx/comp.cpp:75-83) with _gain already 1 so not even the re-arm
//     runs, and what remains is five OnePole::process calls (part_fx.cpp:31,
//     FXT_COUNT == 5), the else-arm's single `_tape_tap = 0.f` store
//     (part_fx.cpp:85), one fast_sin and THREE multiplies -- the 0.25f scale
//     of the send argument and the two send gains (part_fx.cpp:96-98). The
//     earlier "two multiplies" here undercounted and omitted the store; the
//     claim they support -- that the branch is input-independent, so the two
//     rows' different input signals are cost-neutral -- is unaffected and was
//     confirmed in review. Those five smoothers see a
//     CONSTANT target on both rows -- fx_none's fixed values[], and here
//     Part's _fxv, filled from fx_target_value() with every _fx_active false
//     (part.h:579, part.cpp:349) so no lane reaches them -- so both rows take
//     OnePole's `!_smoothing` early return every sample
//     (engine/util/onepole.h:24-26).
//   - The arena. This group sits in g_instr_arena, so its Part's PartFx is at
//     a different address from fx_none's. Both arenas are plain globals
//     (neither carries BENCH_SRAM_EXEC_BSS, which only bench/mem.cpp's g_sram
//     uses), and the BBD line is the identical pointer either way:
//     fx_mem().echo[PART_A] is fx_mem().echo[0], since PART_A == 0
//     (engine/instrument.h:14). Same section, different address.
//
// THE READER'S SANITY CHECK. deck_shell must come out STRICTLY BELOW
// instr_part_1's 46.00, and below it by roughly the engine and FX difference
// (design spec section 5.2's secondary check). A figure anywhere near 46 means
// the switch never took and the row is still running a SynthEngine with GRIT,
// FLUX and COMP on. engine_id()'s assert below should catch that first; if the
// assert passes and the number is still high, believe the number.
//
// One thing this row does NOT hold constant against tone_solo, named here
// rather than discovered later: Part has a SECOND set_targets path. The raster
// is `if (_ctrl_ctr == 0) {...} else if (fired) { _control_tick(); }`
// (part.h:288-294), so a LANE_PITCH fire runs an extra whole tick -- an
// extra engine push and an extra FX-cache fill -- outside the 96-sample
// raster, roughly every 6908 samples at RATE 0.8. tone_solo does not reproduce
// it, so it lands on this row's side of the subtraction. Design spec section
// 6.8 bounds it at order 7e-4 points and states plainly that it is harmless
// because of its SIZE, not because it is correctly attributed.
struct DeckShellGroup {
    Part  part;
    int   counter = 0;
    // Readbacks for the asserts at the end of setup_deck_shell, folded into
    // proc_deck_shell's return value so they are not dead stores. Same idiom
    // as InstrPartGroup's delay-target readback above, and subject to the same
    // non-claim: the fold is NOT a detector -- run.py compares run against run
    // within one measurement, never against a stored expectation, so a row
    // that configured itself differently would simply return a different and
    // perfectly stable checksum. The asserts are what is live.
    int   engine_id = -1;
    float peak      = 0.f;
};

SerialArena<InstrNoVerbGroup, InstrPartGroup, DeckModGroup, ModLaneGroup,
            DeckEngineGroup, FxFluxHotGroup, ToneSoloGroup, DeckShellGroup>
    g_instr_arena;

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
    g.a.init(kSampleRate, 0x1234abcdu,
             mem.echo[PART_A][0], mem.echo[PART_A][1],
             mem.sampler_buf[PART_A], mem.sampler_frames);
    configure_worst_tape_flux(g.a);
    if (n_parts == 2) {
        g.b.init(kSampleRate, 0x9e3779b9u,
                 mem.echo[PART_B][0], mem.echo[PART_B][1],
                 mem.sampler_buf[PART_B], mem.sampler_frames);
        configure_worst_tape_flux(g.b);
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

    g.delay_target_a = g.a.fx().flux().delay_target_for_test();
    assert(g.delay_target_a > 0.f);
    if (n_parts == 2) {
        g.delay_target_b = g.b.fx().flux().delay_target_for_test();
        assert(g.delay_target_b > 0.f);
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
    acc += g.delay_target_a;
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
    acc += g.delay_target_a + g.delay_target_b;
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

    // The self-check, same spirit as setup_instr_part_common's delay-target
    // asserts above: without it, a silently wrong RATE would just
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

// PART_A's seed base and RATE 0.8 -- the same operating point
// setup_deck_mod_hot uses above, for the same reason: faithfulness to
// setup_inst_worst's gate. set_melodic(true) is LANE_PITCH's own setting
// (super_modulator.cpp:14, `_lanes[i].set_melodic(i == LANE_PITCH)`), which
// is what the double phase accumulator's comment (lane.h:196-205) calls "the
// melodic lane" -- the lane driven by process() every sample rather than by
// tick() once per raster.
void setup_mod_lane_hot()
{
    auto& g = g_instr_arena.emplace<ModLaneGroup>();
    g.lane.init(kSampleRate, 0x1234abcdu);
    g.lane.set_melodic(true);
    g.lane.set_rate_hz(free_hz(0.8f));

    for (int b = 0; b < kInstrSettleBlocks; ++b)
        for (size_t i = 0; i < kBlock; ++i) g.lane.process();
}

float proc_mod_lane_hot()
{
    auto& g = g_instr_arena.get<ModLaneGroup>();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) acc += g.lane.process();
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
    // set_cycle just derived (part.h:255-256 with part.cpp:421-424 derives the
    // cycle from it; the fire itself is the `fired` check at part.h:258 and the
    // Part::_fire_trigger call it guards at part.h:296). Rounded to
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

    // Self-check, same shape as InstrPartGroup's delay-target asserts and
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
        // process_in first, then process (part.h:315-316).
        g.engine->process_in(in[i], in[i]);
        g.engine->process(ol, orr);
        acc += ol + orr;
        // Re-fire cadence (post-review fix, DeckEngineGroup's difference #5
        // above): a real deck never lets its FLOW voices sit un-restruck
        // long enough to release past Idle (part.h:258's `fired`
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
    const FxMem& m = fx_mem();
    g.fx.init(kSampleRate, m.echo[PART_A][0], m.echo[PART_A][1]);
    g.fx.set_fx_on(FxBlock::Grit, false, true);
    g.fx.set_fx_on(FxBlock::Flux, true, true);
    g.fx.set_comp(0.f);
    g.fx.set_grit_mix(1.f);
    g.fx.set_flux_mix(1.f);
    g.fx.set_bpm(120.f);
    g.fx.set_flux_rate(kFluxRateCount - 1);

    g.values[FXT_GRIT_INT] = 0.8f;
    g.values[FXT_FLUX_TIME] = 0.5f;
    g.values[FXT_FX_MIX] = 1.f;
    g.values[FXT_REV_SEND] = 0.5f;
    g.values[FXT_FLUX_FB] = 0.9f;

    const float* in = test_input();
    for (int b = 0; b < kInstrSettleBlocks; ++b)
        for (size_t i = 0; i < kBlock; ++i) {
            float l = in[i], r = in[i] * 0.9f, sl = 0.f, sr = 0.f;
            g.fx.process(l, r, sl, sr, g.values);
        }

    g.delay_target_s = g.fx.flux().delay_target_for_test();
    assert(g.delay_target_s > 0.f);
}
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
    acc += g.delay_target_s;
    return acc;
}

// The target values, and the tune argument, a deck produces -- see
// ToneSoloGroup's comment for where each one comes from in Part.
constexpr float kToneLevel = 0.8f;
constexpr float kTonePitch = 0.5f;
constexpr float kToneTune  = 0.5f;

// Length of setup's self-check window. NOT a settle: see setup_tone_solo.
constexpr int   kToneCheckBlocks = 2;

void setup_tone_solo()
{
    auto& g = g_instr_arena.emplace<ToneSoloGroup>();

    g.targets[LANE_PITCH] = kTonePitch;
    g.targets[LANE_LEVEL] = kToneLevel;

    // From here on the engine is reached ONLY through the base pointer, init()
    // included -- IPartEngine::init is pure virtual (engine/parts/
    // engine_iface.h:25) and Part reaches it through `_engine` too. Keeping
    // even this call on the pointer is not decoration: it is the one place a
    // concrete-type slip would be invisible, because init() is outside the
    // measured loop and a devirtualised init() would still leave the loop
    // looking right.
    g.engine = &g.tone;
    g.engine->init(kSampleRate);

    // This row has NO settle, and it is the only row in this file that needs
    // none. TestToneEngine's whole state is four floats -- _sr, _phase, _freq,
    // _amp (test_tone_engine.h:37-40; line 36 is the `private:` label) -- with
    // no slew, no envelope, no delay line and nothing that converges; init()
    // and the first set_targets() put all four at their final values. The
    // window below is a SELF-CHECK window
    // and nothing else. Its length is set by the assert's arithmetic (below),
    // not by anything arriving.
    //
    // The window drives the engine exactly as proc_tone_solo does, including
    // the raster, so the first set_targets() push is made by the raster itself
    // rather than by a separate hand-written call -- which is what Part does
    // too: _ctrl_ctr starts at 0 (part.h:376, part.cpp:74), so a Part's first
    // process() call ticks before its engine ever runs a sample. The body is
    // written out again rather than shared with proc_tone_solo because the peak
    // tracking must NOT appear in the measured loop.
    const float* in = test_input();
    for (int b = 0; b < kToneCheckBlocks; ++b)
        for (size_t i = 0; i < kBlock; ++i) {
            if (g.ctrl_ctr == 0) {
                g.ctrl_ctr = SynthEngine::kCtrlInterval;
                g.engine->set_targets(g.targets, kToneTune);
            }
            --g.ctrl_ctr;
            float ol, orr;
            g.engine->process_in(in[i], in[i]);
            g.engine->process(ol, orr);
            const float a = std::fabs(ol);
            if (a > g.peak) g.peak = a;
        }
    // The window ends on a block boundary with ctrl_ctr back at 0 (the counter
    // is reloaded to 96 and then decremented on the block's first sample, so it
    // reaches 0 on the block's last one), so proc_tone_solo's first sample
    // ticks -- the same phase every later block gets.

    // The self-check. The mistake it guards is a setup that pushes a target
    // array whose LANE_LEVEL is zero -- the natural way to get this row wrong,
    // because a zero-filled `float targets[LANE_COUNT]` looks complete and the
    // row would still run every dispatch, every sinf and every multiply and
    // return a real, cheap, entirely meaningless number.
    //
    // Banded rather than merely non-zero, and the band is arithmetic. Settled,
    // TestToneEngine::process emits sin(2*pi*phase) * _amp * 0.3f
    // (test_tone_engine.h:31) with _amp == targets[LANE_LEVEL] == 0.8, so the
    // waveform's true peak is 0.8 * 0.3 == 0.24 exactly. The window is 2
    // blocks == 192 samples at 311.127 Hz (110 * 8^0.5), i.e. 1.2445 cycles.
    //
    // TWO extrema fall inside it, not one -- the loop below tracks
    // std::fabs(ol), so a TROUGH counts exactly as much as a crest, and 1.2445
    // cycles spans phase 0.25 AND phase 0.75 (only 1.25 falls outside). The
    // original version of this comment said "one crest" and was corrected in
    // review; the band and the figure below were right, the reason was not.
    // Whichever sample lands nearest an extremum can miss it by at most half a
    // phase increment, 0.5 * 311.127/48000 == 0.00324 cycles, where |sin| is
    // still cos(2*pi*0.00324) == 0.99979 -- the worst case for either
    // extremum. On this grid the nearer of the two is the SECOND one: the
    // 116th phase increment sits at 0.751890, 0.001890 off, and in float32
    // gives 0.24 * 0.9999295 == 0.2399831, which is the value observed. The
    // first, the 39th increment at 0.252791, is 0.002791 off and would give
    // 0.2399631 -- so had the window held only that one, the peak would sit
    // further BELOW 0.24, and still 0.00096 above the lower bound asserted
    // here. The band holds either way. Both values are inside
    // [0.23995, 0.24000], and (0.239, 0.241) admits them with ~1e-3 of room on
    // either side.
    //
    // What each mistake sees here:
    //   - LANE_LEVEL 0 pushed -> _amp == 0, every sample exactly 0.f, peak
    //     0.f: the lower assert fires. This is the one the row exists to
    //     catch.
    //   - set_targets never reaching the engine at all -> _amp and _freq keep
    //     their member initialisers, 0.5f and 220.f
    //     (test_tone_engine.h:39-40), so the peak is 0.15 (220 Hz crests at
    //     sample ~55, well inside the window): the lower assert fires too.
    //     Worth stating because a bare `> 0.f` check would NOT have caught
    //     this one -- the default amplitude is audible, not silent.
    //   - LANE_LEVEL pushed as 1.0 instead of the deck's 0.8 -> peak 0.30: the
    //     UPPER assert fires. That is what the upper bound is for; it is the
    //     difference between checking that the tone sounds and checking that it
    //     sounds at the level a deck runs it at.
    assert(g.peak > 0.239f);
    assert(g.peak < 0.241f);
}

// proc_deck_engine_hot's shape, minus the re-fire cadence (a continuous tone
// has nothing to re-strike: TestToneEngine::trigger is empty,
// test_tone_engine.h:26) and plus the control-tick raster.
float proc_tone_solo()
{
    auto& g = g_instr_arena.get<ToneSoloGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        // Part::process's raster, counter for counter (part.h:288-294):
        // push when the counter has reached 0, reload it, decrement after --
        // so the push lands on the block's first sample and before that
        // sample's engine calls, which is where part.h puts it (288-294 sit
        // above 315-316). Part's `else if (fired)` refresh is deliberately
        // absent; ToneSoloGroup's point 3 sizes what that leaves out.
        if (g.ctrl_ctr == 0) {
            g.ctrl_ctr = SynthEngine::kCtrlInterval;
            g.engine->set_targets(g.targets, kToneTune);
        }
        --g.ctrl_ctr;
        float ol, orr;
        // Both calls through the base pointer, in Part::process's order:
        // process_in first, then process (part.h:315-316). process_in is one
        // dispatch into IPartEngine's empty inherited body here
        // (engine_iface.h:59) -- a dispatch, not a second unit of work.
        g.engine->process_in(in[i], in[i]);
        g.engine->process(ol, orr);
        acc += ol + orr;
    }
    // Setup's asserted readback, folded so it is not a dead store.
    acc += g.peak;
    return acc;
}

// setup_fx(SEL_NONE)'s five values[] entries (bench/workloads_system.cpp:
// 198-202), by FxTargetId, in that function's own write order. Named here
// because they are pushed in one place and asserted in another, and both must
// stay the same five numbers: the pushes below are written out one per line
// against fx_none's literals so a reader can diff them by eye, and the assert
// loop then compares the readback against THIS array -- so a mistyped push
// fires rather than agreeing with itself.
//
// Three of the five are live detectors and two are documentation, which is
// worth knowing before trusting the assert: Part's own boot _fx_base is
// { 0.3, 0.5, 1.0, 0.25, 0.45 } (engine/parts/part.h:580), so a missing push
// on GRIT_INT, REV_SEND or FLUX_FB fires (0.3 != 0.8, 0.25 != 0.5,
// 0.45 != 0.7), while FLUX_TIME and FX_MIX already agree with fx_none by
// default and a missing push on either would pass unseen. They are pushed
// anyway, because "identical to fx_none by coincidence of two defaults" is not
// something a later reader should have to rediscover.
constexpr float kFxNoneValues[FXT_COUNT] = { 0.8f, 0.5f, 1.f, 0.5f, 0.7f };

// Total length of the engine fade, in samples, from set_engine to the first
// sample at which the fade multiplier is exactly 1.0 again. Derived in full in
// DeckShellGroup's comment above: 1 (hold -> fall) + 191 (fall, swap on the
// last of them) + 1 (idle -> rise) + 191 (rise) + 1 (hold) == 385.
constexpr int kEngineFadeSamples = 385;

// The fade-finished guarantee, and it is a compile-time one because Part
// exposes no runtime readback of the SoftSwitch -- see the assert block at the
// end of setup_deck_shell for the full account of what was looked for and what
// is there instead. This is what fails if the settle is ever shortened below
// the fade, which is the one way this row could end up measuring a crossfade.
static_assert(kInstrSettleBlocks * static_cast<int>(kBlock) > kEngineFadeSamples,
              "the settle must outlast the engine fade, or deck_shell prices "
              "two engines and a crossfade instead of a shell");

// Length of setup's self-check window, in blocks. NOT a settle -- the settle
// above it is what settles. Its length is set by the peak assert's arithmetic:
// see that assert for why 288 samples is the smallest window whose bound is
// clean at every pitch this row can reach.
constexpr int kShellCheckBlocks = 3;

void setup_deck_shell()
{
    auto& g = g_instr_arena.emplace<DeckShellGroup>();

    // --- the gate's operating point, exactly as setup_instr_part_common
    //     establishes it for PART_A: the same seed base, and every buffer
    //     drawn from the same FxMem the Instrument rows get, so no subtraction
    //     in this round can be measuring different memory
    //     (bench/workloads_instr.cpp's setup_instr_part_common).
    const FxMem& mem = fx_mem();
    g.part.init(kSampleRate, 0x1234abcdu,
                mem.echo[PART_A][0], mem.echo[PART_A][1],
                mem.sampler_buf[PART_A], mem.sampler_frames);

    // configure_worst_tape_flux above, line for line, MINUS its FX lines --
    // the two that engage a block, the one that sets COMP, and the two that
    // voice tape FLUX (RATE and FEEDBACK). Those belong to departure 2 below,
    // which replaces the first three with fx_none's values and drops the last
    // two, because setup_fx(SEL_NONE) calls none of them. Order
    // preserved so this reads as a checklist against that function.
    g.part.mod().set_tempo_bpm(120.f);   // configure_worst_tape_flux line 1
    g.part.fx().set_bpm(120.f);          // line 2 -- and fx_none's own set_bpm
    g.part.set_color(1.f);               // line 3: 4-note chords, i.e. the
                                         //   chord path this row exists to price
    g.part.mod().set_density(1.f);       // line 4
    g.part.set_depth(1.f);               // line 5: MOD 1.0
    g.part.mod().set_rate(0.8f);         // line 6
    g.part.set_voice_decay(1.f);         // line 12 -- reaches _synth/_wave/
                                         //   _body/_sampler, none of them this
                                         //   row's engine; kept for faithfulness
    g.part.trigger_manual();             // line 13
    // Lines 7, 8 and 11 (set_fx_on Grit/Flux, set_comp 1.0) are REPLACED by
    // departure 2; lines 9 and 10 (set_grit_mix/set_flux_mix 1.0) are pushed
    // there instead, because fx_none pushes the identical two values and the
    // mirror reads better in one block; the hot RATE and FXT_FLUX_FB 0.9 are
    // DROPPED, because
    // setup_fx(SEL_NONE) calls none of them.
    //
    // Nothing calls set_step(), on this row or on the gate, so both run in
    // FLOW -- asserted below via flow().

    // --- DEPARTURE 1: the engine. Armed here, ahead of the settle, so the
    //     settle runs the fade to completion; see DeckShellGroup's comment for
    //     the 385-sample arithmetic and kEngineFadeSamples above for the
    //     static_assert that keeps the settle longer than it.
    g.part.set_engine(ENGINE_TEST_TONE);

    // --- DEPARTURE 2: every FX block off at setup_fx(SEL_NONE)'s exact
    //     operating point (bench/workloads_system.cpp:186-202), call for call
    //     and value for value. The init() is already done, above, by
    //     Part::init's own _fx.init(sample_rate, echo) (part.cpp:48) on the
    //     same echo buffer.
    g.part.fx().set_fx_on(FxBlock::Grit, false, true);   // sel != SEL_GRIT
    g.part.fx().set_fx_on(FxBlock::Flux, false, true);   // sel != SEL_FLUX
    g.part.fx().set_comp(0.f);                           // sel != SEL_COMP
    g.part.fx().set_grit_mix(1.f);
    g.part.fx().set_flux_mix(1.f);
    // set_bpm(120.f) is above, with the operating-point block: fx_none and
    // configure_worst_tape_flux pushes the identical value, so it is one line serving
    // both mirrors rather than a difference.
    //
    // fx_none's values[] reach a bare PartFx as a caller-owned array; on a
    // Part the same five numbers have to arrive as target BASES, which
    // _control_tick turns into _fxv via fx_target_value() (part.cpp:349). With
    // every _fx_active false -- Part's boot state (part.h:579), and nothing
    // here calls set_fx_target_active -- fx_target_value returns
    // clampf(_fx_base[i] + 0, 0, 1), i.e. the base unchanged, so _fxv holds
    // fx_none's array exactly. Asserted below.
    g.part.set_fx_target_base(FXT_GRIT_INT,  0.8f);   // fx_none: values[0] 0.8
    g.part.set_fx_target_base(FXT_FLUX_TIME, 0.5f);   // fx_none: values[1] 0.5
    g.part.set_fx_target_base(FXT_FX_MIX,    1.f);    // fx_none: values[2] 1.0
    g.part.set_fx_target_base(FXT_REV_SEND,  0.5f);   // fx_none: values[3] 0.5
    g.part.set_fx_target_base(FXT_FLUX_FB,   0.7f);   // fx_none: values[4] 0.7

    g.counter = 0;

    // The settle, at this file's own kInstrSettleBlocks depth so this row is
    // measured as deep into its state as instr_part_1 is. It also runs the
    // engine fade to completion 49.9x over -- see DeckShellGroup's comment.
    // Written in setup_instr_part_common's own loop shape.
    for (int b = 0; b < kInstrSettleBlocks; ++b) {
        const float* in = test_input();
        for (size_t i = 0; i < kBlock; ++i) {
            float ol, orr, sl, sr;
            g.part.process(in[i], in[i], ol, orr, sl, sr);
        }
    }

    // The self-check window. Separate from the settle, and the peak tracking
    // lives here rather than in proc_deck_shell for the same reason
    // setup_tone_solo writes its window out twice: a compare-and-store per
    // sample must not appear in the measured loop.
    {
        const float* in = test_input();
        for (int b = 0; b < kShellCheckBlocks; ++b)
            for (size_t i = 0; i < kBlock; ++i) {
                float ol, orr, sl, sr;
                g.part.process(in[i], in[i], ol, orr, sl, sr);
                const float a = std::fabs(ol);
                if (a > g.peak) g.peak = a;
            }
    }

    // --- the self-checks.

    // Departure 1 took. This is the assert design spec section 3.2 asks for
    // and section 5.2's secondary check leans on. Under the mistake it guards
    // -- set_engine never called, or a settle shorter than the fade's first
    // 192 samples -- engine_id() returns Part::init's boot default
    // ENGINE_SYNTH (engine/parts/part.cpp:29, ENGINE_SYNTH == 1,
    // engine/parts/engine_iface.h:11-17), and the row would be pricing a
    // SynthEngine at COLOR 1.0 with four voices: a figure near instr_part_1's
    // 46.00, plausible and worthless. It fires on that.
    g.engine_id = static_cast<int>(g.part.engine_id());
    assert(g.part.engine_id() == ENGINE_TEST_TONE);

    // FLOW, which the gate also runs (nothing calls set_step on either side).
    // Under a stray set_step(true, n) this reads false and fires.
    assert(g.part.flow());

    // Departure 2 took, read back off the three blocks themselves rather than
    // inferred from the setters having been called. Grit::engaged() and
    // Flux::engaged() are `_sw.is_on() || !_sw.is_idle()` (engine/fx/grit.h:62,
    // engine/fx/flux.h:42, the latter also gated on _buf_ok) and
    // Comp::amount() returns _amount_target (engine/fx/comp.h:18) -- all three
    // already public, so no getter had to be added to engine/, which this
    // round has locked. Under the mistake they guard -- configure_worst_tape_flux's
    // FX lines copied across with the rest of it -- they see true, true and
    // 1.0 against false, false and 0.0, and all three fire. That mistake is
    // the expensive one: it would leave GRIT, FLUX and COMP inside deck_shell
    // while fx_none has none of them, so deck_shell - fx_none would count
    // three whole FX blocks as "Part-level code".
    assert(!g.part.fx().grit().engaged());
    assert(!g.part.fx().flux().engaged());
    assert(g.part.fx().comp().amount() == 0.f);

    // ...and so did the values[] half of it, which is the half a reader is
    // most likely to skip. Exact compares, not banded: fx_target_value is
    // clampf(base + 0, 0, 1) with every _fx_active false, and clampf returns an
    // in-range argument bit-for-bit, so there is no arithmetic between push and
    // readback that could round. See kFxNoneValues above for which three of the
    // five are live detectors and which two only document.
    for (int i = 0; i < FXT_COUNT; ++i)
        assert(g.part.fx_target_value(i) == kFxNoneValues[i]);

    // The fade. Part exposes NO readback of _engine_fade, and this is not for
    // want of looking: every public observer on Part was checked
    // (engine/parts/part.h) -- engine_id(), flow(), active_voices(),
    // voice_env(), max_voice_env(), color_eff(), overlap_eff(),
    // excitation_eff(), gate(), pitch_cv(), chord_size(), target_value(),
    // target_raw(), pitch_pre_quant(), lane_output(), lane_fired(), and the
    // sub-object accessors mod()/quant()/fx()/synth()/wave()/body()/sampler()
    // -- and none of them reads the SoftSwitch or anything downstream of it.
    // The fade multiplies outL/outR between the engine and the FX
    // (part.h:317-320), and PartFx's one level-ish getter, tape_tap(), is
    // forced to exactly 0.f whenever FLUX is not engaged (part_fx.cpp:85), so
    // on this row it carries no information about the fade at all. Adding a
    // getter would mean editing engine/, which is locked.
    //
    // So the fade-finished guarantee is the settle arithmetic, and it is a
    // static_assert (above kShellCheckBlocks) rather than a runtime one: 19200
    // settle samples against 385, checked at compile time so a shortened
    // settle cannot compile. engine_id() above covers the first 192 of those
    // 385 at runtime and says nothing about the remaining 193.
    //
    // What the peak below adds is a genuine but WEAK runtime lower bound on
    // the fade, and it is worth stating exactly how weak. With every FX block
    // off, PartFx returns l untouched (the outer branch is skipped and Comp
    // bypasses), so outL is exactly sin(2*pi*phase) * _amp * 0.3f * fade.
    // _amp is _tg[LANE_LEVEL], which target_raw clamps to [0, 1] and floors at
    // kLevelFloor * _base[LANE_LEVEL] == 0.4 * 0.8 == 0.32
    // (engine/parts/part.cpp:102-108, part.h:570, 585), so _amp is in
    // [0.32, 1.0] and the tone's own peak is in [0.096, 0.300].
    //
    // The window is kShellCheckBlocks (3) blocks == 288 samples, and 3 rather
    // than 2 for a reason that is arithmetic, not taste. _freq is
    // 110 * 8^clamp(pitch) (test_tone_engine.h:22), so it is in [110, 880] Hz
    // whatever the quantizer does. At the low end 288 samples spans
    // 288 * 110/48000 == 0.66 cycles, and |sin|'s extrema are 0.5 cycles
    // apart, so at least one falls inside the window from ANY starting phase.
    // 192 samples would span only 0.44 cycles, which can miss both. The
    // nearest sample to that extremum misses it by at most half a phase
    // increment, worst at the HIGH end: 0.5 * 880/48000 == 0.00917 cycles,
    // where |sin| is still cos(2*pi*0.00917) == 0.99834. So with the fade at
    // 1.0 the observed peak is in [0.0958, 0.3000].
    //
    // The band asserted is (0.095, 0.301). Since _amp <= 1 and |sin| <= 1, a
    // peak above 0.095 requires fade > 0.095/0.3 == 0.317 on at least one
    // sample in the window -- that, and no more, is what the lower bound
    // proves about the fade. It cannot tell a fade of 0.5 from an _amp of 0.5.
    // What it does catch outright is a fade sitting at or near 0, i.e. a
    // settle that ended inside `fall` or at `idle`, where every sample is
    // scaled to nothing and the peak collapses. The upper bound catches a
    // shell whose FX are contributing wet signal on top of the tone, which is
    // the same mistake the three engaged() asserts above catch directly and
    // more reliably; it is here because 0.3f is a hard ceiling on this
    // expression and a free bound is worth taking.
    assert(g.peak > 0.095f);
    assert(g.peak < 0.301f);
}

// proc_instr_part_1's shape, line for line -- the same per-sample
// Part::process, the same accumulation of all four outputs, the same
// 250-block trigger_manual() cadence, the same active_voices() fold -- because
// instr_part_1 is the row this one is read against (design spec section 5.2's
// secondary check). Two notes on that identity:
//   - the retrigger is kept rather than dropped, so the comparison does not
//     acquire a third difference. On this row it costs the chord build inside
//     trigger_manual (part.cpp:149-162), which IS Part-level code and belongs
//     here, plus the engine dispatch that follows it. That dispatch is not
//     one call: TestToneEngine does NOT override trigger_chord, so
//     _engine->trigger_chord (part.cpp:161) lands in the IPartEngine default
//     (engine/parts/engine_iface.h:49-51), which then makes n further virtual
//     trigger() calls -- n = 4 at this row's COLOR 1.0 (ChordBuilder::_count,
//     engine/pitch/chord.h:54, kEdge4 = 0.625) since _flatten_for_sampler
//     returns n unchanged off a sampler (part.h:438-442). Each of those four
//     bodies is empty (test_tone_engine.h:26). The cost is unchanged either
//     way -- once per 250 blocks -- but the earlier description of a single
//     dispatch into an empty body was wrong about the path.
//   - active_voices() returns 0 here, not a voice count: it is
//     engine-qualified and _engine_id is none of SYNTH/WAVE/BODY
//     (engine/parts/part.h:162-167), so it is three compares per block, not a
//     scan. Kept for the same shape-identity reason.
float proc_deck_shell()
{
    auto& g = g_instr_arena.get<DeckShellGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float ol, orr, sl, sr;
        g.part.process(in[i], in[i], ol, orr, sl, sr);
        acc += ol + orr + sl + sr;
    }
    if (++g.counter >= 250) { g.counter = 0; g.part.trigger_manual(); }
    acc += static_cast<float>(g.part.active_voices());
    // Setup's asserted readbacks, folded so they are not dead stores.
    acc += static_cast<float>(g.engine_id) + g.peak;
    return acc;
}

} // namespace

const Workload kInstrWorkloads[] = {
    { "instr", "instr_part_1", setup_instr_part_1, proc_instr_part_1 },
    { "instr", "instr_part_2", setup_instr_part_2, proc_instr_part_2 },
    { "instr", "instr_noverb", setup_instr_noverb, proc_instr_noverb },
    { "instr", "deck_mod_hot", setup_deck_mod_hot, proc_deck_mod_hot },
    { "instr", "mod_lane_hot", setup_mod_lane_hot, proc_mod_lane_hot },
    { "instr", "deck_engine_hot", setup_deck_engine_hot, proc_deck_engine_hot },
    { "instr", "fx_flux_hot", setup_fx_flux_hot, proc_fx_flux_hot },
    { "instr", "tone_solo", setup_tone_solo, proc_tone_solo },
    { "instr", "deck_shell", setup_deck_shell, proc_deck_shell },
};
const int kInstrCount = sizeof(kInstrWorkloads) / sizeof(kInstrWorkloads[0]);

} // namespace bench
