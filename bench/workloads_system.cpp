#include "workload.h"
#include "mem.h"
#include "serial_arena.h"
#include "instrument.h"
#include "mod/super_modulator.h"
#include "mod/lane_id.h"
#include "center/center.h"
#include "parts/part.h"
#include "synth/synth_engine.h"
#include "fx/part_fx.h"
#include "engine_2x4.h"
#include "parts/bbd_music.h"
#include <cassert>
#include <cstddef>
#include <new>

#if defined(__ARM_EABI__)
#define BENCH_DTCM_BSS __attribute__((section(".dtcmram_bss")))
#else
#define BENCH_DTCM_BSS
#endif

namespace bench {
namespace {

using namespace spky;

struct ModGroup {
    SuperModulator mod_a, mod_b;
    Center center;
    Part hook_a, hook_b;
};

struct SynthGroup {
    SynthEngine synth;
    int voices;
};

struct SynthPairGroup {
    SynthEngine a, b;
};

struct WavePairGroup {
    WaveEngine a, b;
};

struct FxGroup {
    PartFx fx;
    float values[FXT_COUNT];
};

struct InstrumentHarness {
    int counter;
    float out_l[kBlock], out_r[kBlock];
};

struct InstrumentGroup {
    Instrument instrument;
};

using SystemArena = SerialArena<
    ModGroup,
    SynthGroup,
    SynthPairGroup,
    WavePairGroup,
    FxGroup,
    InstrumentGroup>;

SystemArena g_system_arena;

// DTCM is NOLOAD in alt_sram.lds and therefore survives a debug reset.
// Keep raw storage here and begin the Instrument lifetime explicitly in
// setup_inst_worst_bbd_dtcm() before reading any retained byte.
struct DtcmInstrumentStorage {
    alignas(Instrument)
        unsigned char bytes[sizeof(Instrument)];
};

DtcmInstrumentStorage BENCH_DTCM_BSS g_dtcm_instrument_storage;
Instrument* g_dtcm_instrument = nullptr;
Instrument* g_active_instrument = nullptr;
InstrumentHarness g_instrument_harness;

// --- 1. baseline ------------------------------------------------------------
void setup_empty() {}
float proc_empty() { return 0.f; }

// --- 2. modulation plane only ----------------------------------------------
// Two SuperModulators plus the Center, no voices, no FX: the lanes budget the
// design spec estimates at 4-6 %.
//
// Center::update needs two Parts to write its hooks into, so two live here --
// but they are never process()ed. What this row measures is the mod plane and
// the control tick, not the parts.
void setup_mod()
{
    auto& group = g_system_arena.emplace<ModGroup>();
    group.mod_a.init(kSampleRate, 1u);
    group.mod_b.init(kSampleRate, 2u);
    group.center.init(kSampleRate, 11u);
    group.hook_a.init(kSampleRate, 1u);
    group.hook_b.init(kSampleRate, 2u);
    group.mod_a.set_rate(0.5f); group.mod_b.set_rate(0.6f);
    group.mod_a.set_density(0.7f); group.mod_b.set_density(0.7f);
}
float proc_mod()
{
    auto& group = g_system_arena.get<ModGroup>();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        group.mod_a.process();
        group.mod_b.process();
        acc += group.mod_a.lane_output(LANE_PITCH)
             + group.mod_b.lane_output(LANE_PITCH);
    }
    // Control rate: one tick per Center::kCtrlInterval (96) samples, which is
    // exactly one per block. Running it per sample would measure a cadence the
    // firmware never has.
    group.center.update(
        group.mod_a, group.mod_b, group.hook_a, group.hook_b);
    return acc;
}

// --- 3-5. synth voices, 1 / 2 / 4 -------------------------------------------
// Does polyphony scale linearly? One SynthEngine, triggered exactly once in
// setup for the row's intended voice count and never topped up.
//
// SynthEngine::_do_trigger allocates round-robin over *inactive* voices
// (synth_engine.cpp), so retriggering the full set mid-measurement -- as an
// earlier version of this row did -- doesn't refresh voices, it ADDS them:
// with this decay/cycle pairing giving ~16 s of envelope, none of the
// original voices had freed up by the next retrigger, and the "1 voice" row
// silently grew to 2, 3, then 4 voices over the measured window. The fix is
// structural: trigger the intended count once, and rely on the same long
// decay (now a feature, not a bug) to keep exactly those voices sounding,
// unrefreshed, for the whole ~2.2 s measured window.
void setup_synth_n(int n)
{
    auto& group = g_system_arena.emplace<SynthGroup>();
    group.voices = n;
    group.synth.set_seed(3u);
    group.synth.init(kSampleRate);
    group.synth.set_decay(1.f); // 8x cycle: ~16s decay outlives measured window
    group.synth.set_cycle(2.f);
    group.synth.set_flow(false);
    for (int v = 0; v < n; ++v) group.synth.trigger(0.3f + 0.1f * v);
}
void setup_synth_1() { setup_synth_n(1); }
void setup_synth_2() { setup_synth_n(2); }
void setup_synth_4() { setup_synth_n(4); }

float proc_synth()
{
    auto& group = g_system_arena.get<SynthGroup>();
    float acc = 0.f, l, r;
    for (size_t i = 0; i < kBlock; ++i) {
        group.synth.process(l, r);
        acc += l + r;
    }
    // Cheap regression guard: fold the actual active voice count into the
    // returned value so it reaches the checksum. If a future change makes
    // this row hold the wrong number of voices, the checksum moves -- no
    // print inside the measured loop, no silent pass.
    acc += static_cast<float>(group.synth.active_voices());
    return acc;
}

// --- 6-7. matched two-engine, four-voice SYNTH / WAVE -----------------------
// Direct-engine A/B comparison: every setup and process operation is shared
// by construction, so the oscillator type is the only measured difference.
// setup_engine_2x4 / proc_engine_2x4 themselves now live in bench/engine_2x4.h
// (Task 13), hoisted out so bench/workloads_body.cpp can reuse them for
// BodyEngine without copying them.

void setup_synth_2x4()
{
    auto& pair = g_system_arena.emplace<SynthPairGroup>();
    setup_engine_2x4(pair.a, pair.b);
}
void setup_wave_2x4()
{
    auto& pair = g_system_arena.emplace<WavePairGroup>();
    setup_engine_2x4(pair.a, pair.b);
}

float proc_synth_2x4()
{
    auto& pair = g_system_arena.get<SynthPairGroup>();
    return proc_engine_2x4(pair.a, pair.b);
}
float proc_wave_2x4()
{
    auto& pair = g_system_arena.get<WavePairGroup>();
    return proc_engine_2x4(pair.a, pair.b);
}

// --- 8. FX blocks, one at a time -------------------------------------------
// PartFx carries GRIT, FLUX and COMP; each row turns on exactly one so the
// 8-10 % FX estimate decomposes. `FxBlock` is an enum class with only Flux and
// Grit -- COMP is not a block, it is set_comp(amount) and bypasses bit-exactly
// at 0, so the selector here is a plain int, not an FxBlock.
//
// SEL_NONE runs the identical PartFx::process shell with every block
// disabled (GRIT off, FLUX off, set_comp(0.f), which the engine bypasses
// bit-exactly). Without this row, fx_grit/fx_flux_sdram/fx_comp each measure
// "shell + one block" and there is no way to isolate a block's own cost --
// this is the row that makes fx_X - fx_none the block's isolated cost.
enum FxSel { SEL_GRIT = 0, SEL_FLUX = 1, SEL_COMP = 2, SEL_NONE = 3 };

void setup_fx(int sel)
{
    auto& group = g_system_arena.emplace<FxGroup>();
    const FxMem& m = fx_mem();
    group.fx.init(kSampleRate, m.echo[0][0], m.echo[0][1]);
    // immediate = true: the soft switches would otherwise fade in over the
    // warm-up and the measured window would see a partly-engaged chain.
    group.fx.set_fx_on(FxBlock::Grit, sel == SEL_GRIT, true);
    group.fx.set_fx_on(FxBlock::Flux, sel == SEL_FLUX, true);
    group.fx.set_comp(sel == SEL_COMP ? 0.8f : 0.f);
    group.fx.set_grit_mix(1.f);
    group.fx.set_flux_mix(1.f);
    group.fx.set_bpm(120.f);

    // Already-modulated target values, as Part::fx_target_value() would hand
    // them over. Fixed here: this row measures the FX, not the modulation.
    group.values[FXT_GRIT_INT]  = 0.8f;
    group.values[FXT_FLUX_TIME] = 0.5f;
    group.values[FXT_FX_MIX]    = 1.f;
    group.values[FXT_REV_SEND]  = 0.5f;
    group.values[FXT_FLUX_FB]   = 0.7f;
}
void setup_fx_none() { setup_fx(SEL_NONE); }
void setup_fx_grit() { setup_fx(SEL_GRIT); }
void setup_fx_flux() { setup_fx(SEL_FLUX); }
void setup_fx_comp() { setup_fx(SEL_COMP); }

float proc_fx()
{
    auto& group = g_system_arena.get<FxGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float l = in[i], r = in[i] * 0.9f, sl = 0.f, sr = 0.f;
        group.fx.process(l, r, sl, sr, group.values);
        acc += l + r + sl + sr;
    }
    return acc;
}

// --- 7. Oliverb solo --------------------------------------------------------
// The reverb question (estimate 15-25 %), and the first METER measurement the
// firmware-shell spec has been carrying as a TODO. Worst case: big room,
// blooming decay, dense diffusion, both LFOs up.
void setup_reverb()
{
    g_system_arena.reset();
    AmbientReverb& v = reverb_sram();
    v.init(kSampleRate);
    v.clear();
    v.set_size(0.9f);
    v.set_decay(0.95f);        // above the 1.0 loop-gain crossing: bloom
    v.set_tone(0.8f);
    v.set_diffusion(0.9f);
    v.set_diffuser_mod_depth(1.f);
    v.set_mod_depth(1.f);
}
float proc_reverb()
{
    const float* in = test_input();
    AmbientReverb& v = reverb_sram();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        float l, r;
        v.process(in[i], in[i] * 0.9f, l, r);
        acc += l + r;
    }
    return acc;
}

// --- 8-9. the whole instrument ---------------------------------------------
InstrumentGroup& construct_axi_instrument_group()
{
    if (g_dtcm_instrument) {
        g_dtcm_instrument->~Instrument();
        g_dtcm_instrument = nullptr;
    }
    return g_system_arena.emplace<InstrumentGroup>();
}

Instrument& construct_dtcm_instrument()
{
    g_system_arena.reset();
    if (g_dtcm_instrument) {
        g_dtcm_instrument->~Instrument();
        g_dtcm_instrument = nullptr;
    }
    g_dtcm_instrument = ::new (
        static_cast<void*>(g_dtcm_instrument_storage.bytes)) Instrument{};
    return *g_dtcm_instrument;
}

void configure_inst_common(Instrument& inst)
{
    g_active_instrument = &inst;
    inst.init(kSampleRate, fx_mem());
    inst.set_tempo_bpm(120.f);
    // Reset the retrigger phase here, not just at file-scope initialization:
    // without this, instrument_worst's phase silently inherits whatever
    // instrument_init's process() loop left it at. Each row now constructs a
    // fresh InstrumentGroup, and this explicit assignment documents the
    // intended retrigger phase rather than relying on value-initialization.
    g_instrument_harness.counter = 0;
}

// Init patch: the typical load.
void setup_inst_init()
{
    auto& group = construct_axi_instrument_group();
    configure_inst_common(group.instrument);
}

// Worst case: 8 voices, 4-note COLOR on both parts, every FX block on, high
// diffusion, echo at maximum. THE number.
void configure_inst_worst(Instrument& inst)
{
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_color(p, 1.f);          // 4-note chords -> 4 voices per part
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
}

void setup_inst_worst()
{
    auto& group = construct_axi_instrument_group();
    configure_inst_common(group.instrument);
    configure_inst_worst(group.instrument);
}

// instrument_worst, plus the cross-deck bus (spec 2026-07-31
// cross-deck-audio-bus, movement 1). configure_inst_worst() never calls
// set_engine, so a naive copy of it stays on the boot default, ENGINE_SYNTH,
// on both decks -- and SynthEngine does not override
// IPartEngine::consumes_input() (engine_iface.h; SamplerEngine is the ONLY
// engine that does, sampler_engine.h). Part::process's
// `if (_engine_wants_in)` guard is keyed off that cached override, and the
// _src_deck branch (and process_in() itself) live INSIDE it -- so on
// ENGINE_SYNTH the branch is not merely a false guard, it is never reached
// at all, regardless of other_deck or SPKY_DECK_BUS. A row that left the
// engine untouched would measure nothing.
//
// Both decks are switched to ENGINE_SAMPLER with the monitor on instead --
// closely modelled on Task 4's mutual-routing test (test_deck_bus.cpp,
// "sampler <-> sampler mutual routing stays finite": same engine pair, same
// monitor, same other_deck), which proved THAT loop finite and bounded on
// desktop over a 10 s run. This row is not identical to Task 4's, though,
// and the differences matter enough to name: audio_in is false here (Task 4
// passes true), the drive signal is bench's fixed test_input() noise (Task 4
// drives a constant +-0.5), and this is a hardware bench row, not a desktop
// doctest. Safety here rests on fast_tanh's own hard-clamp contract (the
// mechanism Task 4 exercised), not on this exact configuration having been
// separately proven finite -- it has not. This does mean the row is no
// longer a same-source A/B against instrument_worst (the engine swap
// dominates any diff there) -- the real A/B is this same row,
// inst_worst_deck_bus, measured once with SPKY_DECK_BUS at its default 1 and
// once rebuilt with it forced to 0; instrument_worst stays in this build
// only as one of several controls used to gauge cross-build drift separately
// from the bus (see docs/bench/2026-07-31-20eafed-deck-bus.md).
void setup_inst_worst_deck_bus()
{
    auto& group = construct_axi_instrument_group();
    configure_inst_common(group.instrument);
    configure_inst_worst(group.instrument);
    for (int p = 0; p < PART_COUNT; ++p) {
        group.instrument.set_engine(p, ENGINE_SAMPLER);
        group.instrument.sampler_monitor(p, true);
        group.instrument.set_excitation_sources(p, true, /*other_deck=*/true, false);
    }
    // Settle every envelope and slew -- including the engine swap's 4 ms
    // SoftSwitch fade -- before the runner's measured window opens. Same
    // 200-block depth the neighbouring instrument_worst_bbd row above uses,
    // and that workloads_instr.cpp's kInstrSettleBlocks names for its own
    // rows. NOTE this is NOT shown to be enough for the mutual loop's own
    // slower dynamic: a desktop check of an ANALOGOUS configuration -- same
    // engine/monitor/other_deck settings as this row, but driven with a
    // fixed sine-plus-DC signal rather than this row's test_input() noise,
    // run on desktop (not hardware), and deleted uncommitted after use, so
    // it is not this exact configuration and is not reproducible from the
    // tree -- found the sibling tap still moving in a bounded band out to
    // several thousand blocks (dipping ~3% around block 500, recovering by
    // ~block 2000) before settling. The reported bench figure is conditioned
    // on this 200-block depth and has not been shown to equal the
    // steady-state cost -- see docs/bench/2026-07-31-20eafed-deck-bus.md's
    // settle-sensitivity section, which describes that desktop check in the
    // same corrected terms.
    const float* in = test_input();
    for (int b = 0; b < 200; ++b)
        group.instrument.process(in, in, g_instrument_harness.out_l,
                                  g_instrument_harness.out_r, kBlock);
}

// --- 10. the whole instrument, both decks on the BBD PART ENGINE ------------
// The legacy-named AXI/DTCM pair and the explicit movement-2 row all use this
// one setup. That makes them the harness's checksum-equal placement pair while
// fx_flux_sdram remains the independent stereo tape-FLUX price.
//
// Two deliberate departures from a naive "instrument_worst plus set_engine":
//
//   * Tape FLUX is switched OFF on both decks so this row isolates the BBD
//     engine pair instead of adding four large tape lines to the measurement.
//   * The quantizer is put in Free mode. LANE_PITCH reaches the engine as
//     _pitch_q (part.cpp:233), so under the boot Dorian scale a lane at 1.0
//     arrives at the engine as the nearest scale degree BELOW 1.0 and the
//     clock lands short of its ceiling. Free is what makes "PITCH at the
//     clock ceiling" true rather than approximately true, and the assert at
//     the end of the setup is what holds it there.
void configure_inst_bbd_engine_worst(Instrument& inst)
{
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_engine(p, ENGINE_BBD);
        // Freeze is unreachable in FLOW. Enter STEP before any settling so
        // the freshly swapped-in engine receives STEP through _engine_swap's
        // state replay, then keep its gate high throughout the settle.
        inst.set_step(p, true, 16);
        inst.set_color(p, 1.f);              // maximum stereo clock spread
        inst.set_voice_attack(p, 0.f);       // 2 ms freeze ramp
        inst.set_voice_decay(p, 1.f);        // no trim below freeze gain
        // A voiceless engine has nothing of its own to make a sound with:
        // with no source selected the lines see silence and the row measures
        // an idling delay. Both sources on, which is also the mutual routing
        // (inst_worst_deck_bus's shape) rather than a one-way feed.
        inst.set_excitation_sources(p, /*tape=*/false, /*other_deck=*/true,
                                    /*audio_in=*/true);
        inst.set_fx_on(p, FxBlock::Flux, false);   // see the note above
        inst.set_quant_mode(p, QuantMode::Free);

        // PITCH at the top of its travel and SIZE at the shortest division
        // (kDivs[0] == 1/32) put the clock on kClockMaxHz -- and the clock is
        // what the per-sample cost is proportional to, because BbdLine::
        // Process runs f_clk/f_s cell ticks per sample (fx/bbd.h:376-383).
        // Both lanes are DEACTIVATED rather than merely based high: an active
        // lane would spend most of its time below the ceiling, i.e. below the
        // worst case. Deactivating a lane does not remove any modulation-plane
        // work -- the plane runs regardless (part.cpp:107) -- it only pins the
        // value the plane's output is not added to.
        inst.set_target_active(p, LANE_PITCH, false);
        inst.set_target_base(p, LANE_PITCH, 1.f);
        inst.set_target_active(p, LANE_SIZE, false);
        inst.set_target_base(p, LANE_SIZE, 0.f);
        // MIX (LANE_LEVEL) fully wet and FEEDBACK (LANE_MOTION) at the top of
        // its travel, both pinned for the same reason.
        inst.set_target_active(p, LANE_LEVEL, false);
        inst.set_target_base(p, LANE_LEVEL, 1.f);
        inst.set_target_active(p, LANE_MOTION, false);
        inst.set_target_base(p, LANE_MOTION, 1.f);
        // LANE_SOURCE is left ACTIVE on purpose: it is DRIVE, and a moving
        // DRIVE defeats BbdEcho::SetDrive's unchanged-value guard every
        // control block (bbd_engine.cpp:185-190), which is a std::pow per line
        // per block. That is the worst case, and it is what a plane-driven
        // patch actually does.
        //
        // RESONANCE off centre so the feedback-path tilt one-pole is LIVE
        // (bbd.h:679-683 is identity at tilt_ == 0). SUB at 1 so the input
        // actually arrives at full level. DETUNE at 0 -> the fastest clock
        // slew, so process()'s glide is doing arithmetic rather than sitting
        // on its target.
        inst.set_voice_resonance(p, 1.f);
        inst.set_voice_sub(p, 1.f);
        inst.set_voice_detune(p, 0.f);
        inst.set_voice_filt(p, 1.f);
    }
    // Fill both stereo pairs and settle every envelope, slew, freeze ramp and
    // the engine swap's own SoftSwitch fade before the runner measures. Both
    // gates are retriggered on every block, exactly like the measured process.
    const float* in = test_input();
    for (int b = 0; b < 200; ++b) {
        inst.trigger_manual(PART_A);
        inst.trigger_manual(PART_B);
        inst.process(in, in, g_instrument_harness.out_l,
                     g_instrument_harness.out_r, kBlock);
    }

    // The self-check, and the reason it is here rather than in a comment.
    // Movement 1's Task 5 shipped a row that stayed on ENGINE_SYNTH and
    // therefore never reached process_in at all -- it measured a SYNTH deck
    // under a BBD row's name, and nothing in the harness noticed, because a
    // wrong-but-stable configuration returns a wrong-but-stable checksum and
    // run.py only compares run against run. asserts are live here: the bench
    // builds -O2 with NDEBUG undefined (see workloads_instr.cpp:70).
    for (int p = 0; p < PART_COUNT; ++p) {
        assert(inst.engine_id(p) == ENGINE_BBD);
        assert(inst.bbd_div(p) == 0);                       // 1/32, the shortest
        assert(inst.bbd_clock_hz(p) >= bbd_tuning::kClockMaxHz);
        assert(inst.bbd_frozen(p));
    }
}

void setup_inst_worst_bbd()
{
    auto& group = construct_axi_instrument_group();
    configure_inst_common(group.instrument);
    configure_inst_worst(group.instrument);
    configure_inst_bbd_engine_worst(group.instrument);
}

void setup_inst_worst_bbd_dtcm()
{
    auto& inst = construct_dtcm_instrument();
    configure_inst_common(inst);
    configure_inst_worst(inst);
    configure_inst_bbd_engine_worst(inst);
}

void setup_inst_bbd_engine_worst()
{
    setup_inst_worst_bbd();
}

// FEED on both decks, at the ring's worst case (spec 2026-08-18). Modelled
// line for line on configure_inst_bbd_engine_worst above, and for the same
// reason: the kernel row (feed_pairs, workloads_feed.cpp) prices ONE bank in
// isolation, and SWARM's own history is that a kernel figure alone sized a
// bank too generously and the whole-engine row corrected the reading.
void configure_inst_feed_engine_worst(Instrument& inst)
{
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_engine(p, ENGINE_FEED);
        // STEP with a short pattern, so envelope hits and chord retargeting
        // are actually happening rather than a settled drone coasting.
        inst.set_step(p, true, 16);
        inst.set_color(p, 1.f);              // the largest chord COLOR reaches
        inst.set_quant_mode(p, QuantMode::Free);
        // FEED has no input path, so the excitation bus is left alone; what it
        // does have is a ring whose per-sample cost is flat in every knob
        // except the ones below.
        inst.set_fx_on(p, FxBlock::Flux, false);   // as the BBD row does

        // RISE short and FALL long with FLOOR up: the envelope is always
        // moving and never idles, so Env::process is never on its free path.
        inst.set_voice_attack(p, 0.f);
        inst.set_voice_decay(p, 1.f);
        // RATIO into the irrational upper half -- no magnet flattening, and
        // the modulator phase advances at a rate the compiler cannot fold.
        inst.set_voice_resonance(p, 1.f);
        inst.set_voice_sub(p, 1.f);          // the SUB oscillator live
        // FILT at -0.4: the darkest cutoff that does NOT fade the deck out.
        // The SVF costs the same at every cutoff, so the point of this value
        // is not the filter -- it is that the deck still emits. At -1 the
        // gain fade reaches zero and every stage downstream of the part
        // (Grit, Comp, the center reverb, the master limiter) would be
        // priced on silence instead of on signal.
        inst.set_voice_filt(p, -0.4f);
        // BOND high so the ring taps are live rather than a self-feedback
        // path the compiler could narrow, and SPREAD full so every pair sits
        // at its own frequency. Both lanes DEACTIVATED and based at the
        // ceiling, the same reasoning the BBD row states: an active lane
        // spends most of its time below the worst case.
        inst.set_target_active(p, LANE_SOURCE, false);
        inst.set_target_base(p, LANE_SOURCE, 1.f);
        inst.set_target_active(p, LANE_SIZE, false);
        inst.set_target_base(p, LANE_SIZE, 1.f);
        inst.set_target_active(p, LANE_MOTION, false);
        inst.set_target_base(p, LANE_MOTION, 1.f);   // DEPTH at the top
        inst.set_target_active(p, LANE_LEVEL, false);
        inst.set_target_base(p, LANE_LEVEL, 1.f);
    }
    const float* in = test_input();
    for (int b = 0; b < 200; ++b) {
        inst.trigger_manual(PART_A);
        inst.trigger_manual(PART_B);
        inst.process(in, in, g_instrument_harness.out_l,
                     g_instrument_harness.out_r, kBlock);
    }
    // The same self-check the BBD row carries, and for the same reason: a
    // row that stayed on the wrong engine would return a wrong-but-stable
    // checksum and nothing in the harness would notice.
    for (int p = 0; p < PART_COUNT; ++p) {
        assert(inst.engine_id(p) == ENGINE_FEED);
        assert(inst.active_voices(p) == 1);   // the ring is audible
    }
}

void setup_inst_feed_engine_worst()
{
    auto& group = construct_axi_instrument_group();
    configure_inst_common(group.instrument);
    configure_inst_worst(group.instrument);
    configure_inst_feed_engine_worst(group.instrument);
}

float proc_inst()
{
    auto& inst = *g_active_instrument;
    auto& harness = g_instrument_harness;
    const float* in = test_input();
    inst.process(in, in, harness.out_l, harness.out_r, kBlock);
    // Keep the voices busy: a fire every ~half second on both parts.
    if (++harness.counter >= 250) {
        harness.counter = 0;
        inst.trigger_manual(PART_A);
        inst.trigger_manual(PART_B);
    }
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += harness.out_l[i] + harness.out_r[i];
    // Same guard proc_synth uses: fold both parts' active voice counts into
    // the returned value so a wrong voice count -- the exact failure that
    // shipped undetected as a "1 voice" row measuring 2.8 voices -- moves
    // the checksum instead of passing silently. No printing inside the
    // measured loop.
    acc += static_cast<float>(inst.active_voices(PART_A));
    acc += static_cast<float>(inst.active_voices(PART_B));
    return acc;
}

float proc_inst_bbd_frozen()
{
    auto& inst = *g_active_instrument;
    inst.trigger_manual(PART_A);
    inst.trigger_manual(PART_B);
    return proc_inst();
}

// FEED's other worst case, and the one no other row in this file reaches:
// SILENCE. A deck between notes is not idle work -- FeedBank's ring runs at
// full rate whatever the amplitude is -- and until 2026-08-19 that state cost
// 1.65x a SOUNDING deck on the desktop, because the amplitude glide converged
// on zero without arriving and the tail then ran entirely on subnormals.
// Fixed in the engine (FeedBankT::kArriveEps, SvfLp::FlushDenormals); this row
// exists so a regression is measured instead of argued.
//
// proc_inst fires both decks every ~half second on purpose, so it can never
// see this. That is why the row needs its own proc as well as its own setup.
void configure_inst_feed_engine_idle(Instrument& inst)
{
    for (int p = 0; p < PART_COUNT; ++p) {
        // FALL at 0 is the shortest decay and FLOOR sits in the top quarter of
        // the same control, so 0 is FLOOR 0 too and the envelope actually
        // reaches zero. The worst-case row uses 1.f, which never goes quiet.
        inst.set_voice_decay(p, 0.f);
        // Getting to silence took all three of these, measured rather than
        // assumed. DENS 0 alone leaves a note firing every 2.58 s -- k =
        // round(0 * L) picks no groove cells, but the phrase boundary fires
        // anyway -- which would have made this row a 93/7 mixture with the
        // sounding case landing in max_cyc. RATE 0 stretches that past any
        // measurement window; VARY 0 is "loop", so the cell cannot re-draw
        // itself into one. Verified: 70 s of exactly zero after the single
        // manual hit below, with active_voices 0 throughout.
        inst.set_density(p, 0.f);
        inst.set_rate(p, 0.f);
        inst.set_variation(p, 0.f);
    }
    const float* in = test_input();
    inst.trigger_manual(PART_A);
    inst.trigger_manual(PART_B);
    // WAIT for the envelope to land rather than guessing a warm-up length. A
    // guessed 3000 blocks (6 s) was short by 333 and hung the board on the
    // assert below -- twice, because the failure mode of an assert here is a
    // silent halt with no row emitted, not a message. FEED's FALL at 0 still
    // takes 6.67 s to cross Env's 1e-4 idle threshold at 120 BPM: the decay is
    // exponential, so the last two orders of magnitude cost as much time as
    // the first two. The bound is a backstop, not a duration.
    int settled = 0;
    for (int b = 0; b < 20000 && settled < 500; ++b) {
        inst.process(in, in, g_instrument_harness.out_l,
                     g_instrument_harness.out_r, kBlock);
        // 500 further blocks after both parts go idle, so everything
        // downstream of the decks has reached the state being priced too.
        // Measured: the instrument output is then EXACTLY zero, reverb
        // included, which is the point of the row.
        settled = (inst.active_voices(PART_A) == 0 &&
                   inst.active_voices(PART_B) == 0) ? settled + 1 : 0;
    }
    for (int p = 0; p < PART_COUNT; ++p) {
        assert(inst.engine_id(p) == ENGINE_FEED);
        // Silent, which is the whole subject. A row that kept ringing would
        // return a plausible number for the wrong state.
        assert(inst.active_voices(p) == 0);
    }
}

void setup_inst_feed_engine_idle()
{
    auto& group = construct_axi_instrument_group();
    configure_inst_common(group.instrument);
    configure_inst_worst(group.instrument);
    configure_inst_feed_engine_worst(group.instrument);
    configure_inst_feed_engine_idle(group.instrument);
}

float proc_inst_feed_idle()
{
    auto& inst = *g_active_instrument;
    auto& harness = g_instrument_harness;
    const float* in = test_input();
    inst.process(in, in, harness.out_l, harness.out_r, kBlock);
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += harness.out_l[i] + harness.out_r[i];
    // Fold the voice counts in as proc_inst does -- here they must stay 0, so
    // a row that started ringing mid-measurement moves the checksum.
    acc += static_cast<float>(inst.active_voices(PART_A));
    acc += static_cast<float>(inst.active_voices(PART_B));
    return acc;
}

// --- 11. EDGE's own filter cost: SYNTH deck + BBD deck, EDGE off-neutral ---
// This branch (voice-knobs-dpth-edge) added an OnePoleHp pair to the summed
// stereo output of SynthEngineT (SYNTH, WAVE) and a second pair to BbdEngine::
// process_in, ahead of SUB's _in_gain. Both are skipped by an explicit branch
// -- `if (_edge != 0.f)` -- at EDGE's boot/neutral value (synth_engine.cpp,
// bbd_engine.cpp), so a row that leaves EDGE at 0 measures the branch, not
// the filter (task-10 brief). This row exists to put a real, non-bypassed
// SynthEngineT filter and a real, non-bypassed BBD process_in filter into one
// system-family measurement.
//
// PART_A is left on the boot-default ENGINE_SYNTH (configure_inst_worst
// never calls set_engine); only PART_B is switched to ENGINE_BBD here, reusing
// configure_inst_bbd_engine_worst's clock-ceiling recipe for that one part so
// process_in actually runs at kClockMaxHz rather than on an idling line --
// see that function's comments for why each of these setters is the worst
// case, not just a non-default one.
void configure_inst_edge_synth_bbd(Instrument& inst)
{
    inst.set_engine(PART_B, ENGINE_BBD);
    inst.set_step(PART_B, true, 16);
    inst.set_color(PART_B, 1.f);               // maximum stereo clock spread
    inst.set_voice_attack(PART_B, 0.f);
    inst.set_voice_decay(PART_B, 1.f);
    inst.set_excitation_sources(PART_B, /*tape=*/false, /*other_deck=*/true,
                                /*audio_in=*/true);
    inst.set_fx_on(PART_B, FxBlock::Flux, false);
    inst.set_quant_mode(PART_B, QuantMode::Free);
    inst.set_target_active(PART_B, LANE_PITCH, false);
    inst.set_target_base(PART_B, LANE_PITCH, 1.f);
    inst.set_target_active(PART_B, LANE_SIZE, false);
    inst.set_target_base(PART_B, LANE_SIZE, 0.f);
    inst.set_target_active(PART_B, LANE_LEVEL, false);
    inst.set_target_base(PART_B, LANE_LEVEL, 1.f);
    inst.set_target_active(PART_B, LANE_MOTION, false);
    inst.set_target_base(PART_B, LANE_MOTION, 1.f);
    inst.set_voice_resonance(PART_B, 1.f);
    inst.set_voice_sub(PART_B, 1.f);
    inst.set_voice_detune(PART_B, 0.f);
    inst.set_voice_filt(PART_B, 1.f);

    // EDGE off-neutral on both decks -- the entire point of this row. 1.f is
    // the top of the bipolar (-1..1) travel BbdEngine::set_edge and
    // SynthEngineT::set_edge both clamp to, as far from the t==0 bypass
    // branch as the knob goes.
    for (int p = 0; p < PART_COUNT; ++p)
        inst.set_voice_edge(p, 1.f);

    // Settle every envelope, slew and the engine swap's own SoftSwitch fade
    // before the runner's measured window opens -- same 200-block depth
    // configure_inst_bbd_engine_worst uses, and for the same reason.
    const float* in = test_input();
    for (int b = 0; b < 200; ++b) {
        inst.trigger_manual(PART_A);
        inst.trigger_manual(PART_B);
        inst.process(in, in, g_instrument_harness.out_l,
                     g_instrument_harness.out_r, kBlock);
    }

    // The same self-check the BBD and FEED rows carry: a row that silently
    // stayed on the wrong engine, or whose EDGE write did not land, would
    // return a plausible-looking but wrong-basis checksum and nothing in the
    // harness would notice otherwise.
    assert(inst.engine_id(PART_A) == ENGINE_SYNTH);
    assert(inst.engine_id(PART_B) == ENGINE_BBD);
    assert(inst.bbd_div(PART_B) == 0);                    // 1/32, the shortest
    assert(inst.bbd_clock_hz(PART_B) >= bbd_tuning::kClockMaxHz);
    assert(inst.bbd_frozen(PART_B));
}

void setup_inst_edge_synth_bbd()
{
    auto& group = construct_axi_instrument_group();
    configure_inst_common(group.instrument);
    configure_inst_worst(group.instrument);
    configure_inst_edge_synth_bbd(group.instrument);
}

// Not proc_inst_bbd_frozen: that helper retriggers BOTH parts every block,
// which is right for the all-BBD rows it was written for (freeze needs
// re-arming every block) but wrong here -- PART_A is SYNTH, and retriggering
// it every ~2 ms leaves no voice ever idle long enough to free up, so
// SynthEngineT::_do_trigger's steal path (synth_engine.cpp) fires on every
// single measured call instead of the occasional bloom/retrigger proc_inst's
// own ~250-block cadence models. That would price constant voice-stealing,
// not EDGE. So: re-arm PART_B's freeze gate every block (the actual
// requirement), and leave PART_A to proc_inst's normal cadence.
float proc_inst_edge_synth_bbd()
{
    auto& inst = *g_active_instrument;
    inst.trigger_manual(PART_B);
    return proc_inst();
}

} // namespace

const Workload kCoreWorkloads[] = {
    { "system", "empty_callback",     setup_empty,     proc_empty   },
    { "system", "mod_plane_2x_center",setup_mod,       proc_mod     },
    { "system", "synth_1_voice",      setup_synth_1,   proc_synth   },
    { "system", "synth_2_voices",     setup_synth_2,   proc_synth   },
    { "system", "synth_4_voices",     setup_synth_4,   proc_synth   },
    { "system", "synth_2x4",          setup_synth_2x4, proc_synth_2x4 },
    { "system", "wave_2x4",           setup_wave_2x4,  proc_wave_2x4  },
    { "system", "fx_none",            setup_fx_none,   proc_fx      },
    { "system", "fx_grit",            setup_fx_grit,   proc_fx      },
    { "system", "fx_flux_sdram",      setup_fx_flux,   proc_fx      },
    { "system", "fx_comp",            setup_fx_comp,   proc_fx      },
    { "system", "oliverb_solo_sram",  setup_reverb,    proc_reverb  },
    { "system", "instrument_init",    setup_inst_init, proc_inst    },
    { "system", "instrument_worst",   setup_inst_worst,proc_inst    },
    { "system", "inst_worst_deck_bus", setup_inst_worst_deck_bus, proc_inst },
    { "system", "instrument_worst_bbd",
      setup_inst_worst_bbd, proc_inst_bbd_frozen },
    { "system", "instrument_worst_bbd_dtcm",
      setup_inst_worst_bbd_dtcm, proc_inst_bbd_frozen },
    { "system", "inst_bbd_engine_worst",
      setup_inst_bbd_engine_worst, proc_inst_bbd_frozen },
    // proc_inst, not proc_inst_bbd_frozen: FEED has no freeze to re-arm every
    // block, and proc_inst already fires both decks every ~half second and
    // folds active_voices into the checksum -- which on FEED is 1 while the
    // ring is audible and 0 when it is not, so a deck that fell silent moves
    // the checksum instead of passing quietly.
    { "system", "inst_feed_engine_worst",
      setup_inst_feed_engine_worst, proc_inst },
    { "system", "inst_feed_engine_idle",
      setup_inst_feed_engine_idle, proc_inst_feed_idle },
    { "system", "inst_edge_synth_bbd",
      setup_inst_edge_synth_bbd, proc_inst_edge_synth_bbd },
};
const int kCoreCount = sizeof(kCoreWorkloads) / sizeof(kCoreWorkloads[0]);

} // namespace bench
