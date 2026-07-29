#include <cassert>
#include "workload.h"
#include "families.h"
#include "mem.h"
#include "serial_arena.h"
#include "instrument.h"
#include "parts/part.h"
#include "mod/super_modulator.h"
#include "mod/lane_id.h"

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
// The row it corrects is mod_plane_2x_center, which runs two modulators at
// RATE 0.5 and 0.6 and DENSITY 0.7 (bench/workloads_system.cpp:75-76), never
// calls set_tempo_bpm, and does no settle. setup_inst_worst runs RATE 0.8 and
// DENSITY 1.0 on both decks, and Instrument::set_tempo_bpm pushes 120 BPM
// into every part's modulator (engine/instrument.cpp:70). All three
// differences are deliberate here and all three are part of what the
// subtraction measures -- see the design spec section 3.
//
// The Center is deliberately absent. mod_plane_2x_center includes it, so
// charging each deck half of that row double-counts an instrument-level
// object that no bare Part runs and that the measured 4.04-point glue term
// already contains.
struct DeckModGroup {
    SuperModulator mod;
};

SerialArena<InstrNoVerbGroup, InstrPartGroup, DeckModGroup> g_instr_arena;

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
    // with seed_base = 0x1234abcd for PART_A (engine/parts/part.cpp:16,
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
}

float proc_deck_mod_hot()
{
    auto& g = g_instr_arena.get<DeckModGroup>();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) {
        g.mod.process();
        acc += g.mod.lane_output(LANE_PITCH);
    }
    return acc;
}

} // namespace

const Workload kInstrWorkloads[] = {
    { "instr", "instr_part_1", setup_instr_part_1, proc_instr_part_1 },
    { "instr", "instr_part_2", setup_instr_part_2, proc_instr_part_2 },
    { "instr", "instr_noverb", setup_instr_noverb, proc_instr_noverb },
    { "instr", "deck_mod_hot",  setup_deck_mod_hot,  proc_deck_mod_hot  },
};
const int kInstrCount = sizeof(kInstrWorkloads) / sizeof(kInstrWorkloads[0]);

} // namespace bench
