#include "workload.h"
#include "body/ks_string.h"
#include "body/mode_bank.h"
#include "PhysicalModeling/KarplusString.h"
#include "mem.h"
#include "serial_arena.h"
#include "engine_2x4.h"
#include "instrument.h"

namespace bench {
namespace {

using namespace spky;

// Rows in this family run strictly serially (in table order below), so
// their state shares one max-sized, max-aligned allocation instead of each
// holding its own permanent globals -- see bench/serial_arena.h and
// bench/workloads_system.cpp for the worked pattern this follows.
struct ModeBankGroup {
    spky::ModeBank bank;
    bool           wobble_up = false;   // only used by the varying row below
};

struct KsGroup {
    daisysp::String a, b;
};

struct KsPortGroup {
    spky::KsString a, b;
    bool           wobble_up = false;
};

// Task 13: the engine-level rows. A matched BodyEngine pair (mirrors
// SynthPairGroup / WavePairGroup, bench/workloads_system.cpp) and a whole
// Instrument (mirrors InstrumentGroup) -- both defined here, not reached from
// workloads_system.cpp's anonymous namespace, because that namespace gives
// them internal linkage private to that translation unit. Keeping the body
// family's own group types also means this row can never perturb
// instrument_worst's arena slot: the two live in entirely separate arenas
// (g_system_arena vs g_body_arena).
struct BodyPairGroup {
    BodyEngine a, b;
};

struct BodyInstGroup {
    Instrument instrument;
    int        counter;
    float      out_l[kBlock], out_r[kBlock];
};

SerialArena<ModeBankGroup, KsGroup, KsPortGroup, BodyPairGroup, BodyInstGroup> g_body_arena;

// +-3 cents = 2^(+-3/1200), precomputed so no pow() call sits in the
// control-tick path measured below -- the row prices ModeBank::set_params,
// not a ratio generator.
constexpr float kWobbleUpRatio   = 1.001734f;   // +3 cents
constexpr float kWobbleDownRatio = 0.998269f;   // -3 cents

// --- body/mode_bank_24: WORST CASE -------------------------------------
// f0 alternates by a few deterministic cents every block (up, down, up, ...
// -- never random, so the harness's checksum stays run-to-run comparable).
// That guarantees ModeBank::set_params's early-return dirty check
// (f0_hz == _f0 && ... && !_dirty) never matches two blocks running, so
// _recompute() genuinely executes once per block -- the same worst case a
// real BodyVoice hits, since its per-voice drift LFO and the mod lanes keep
// DETUNE/MATL/FILTER moving at nearly every control tick. This is the
// number that should decide voice count.
void setup_mode_bank()
{
    auto& group = g_body_arena.emplace<ModeBankGroup>();
    group.bank.init(kSampleRate);
    group.bank.set_params(220.f, 0.6f, 0.8f, 0.7f);
    group.wobble_up = false;
}

float proc_mode_bank()
{
    auto& group = g_body_arena.get<ModeBankGroup>();
    const float* in = test_input();
    group.wobble_up = !group.wobble_up;
    const float f0 = 220.f * (group.wobble_up ? kWobbleUpRatio : kWobbleDownRatio);
    group.bank.set_params(f0, 0.6f, 0.8f, 0.7f);
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) acc += group.bank.process(in[i]);
    return acc;
}

// --- body/mode_bank_24_static: FLOOR -------------------------------------
// Same four literal parameters on every call, so set_params's dirty check
// short-circuits from the second call onward and _recompute() runs zero
// times inside the measured window: only the per-sample SVF bank sum
// (ModeBank::process) is priced. This is the cost of a held/static voice,
// not a moving one -- keep both rows; the gap between this and
// mode_bank_24 above IS the coefficient math's real price.
void setup_mode_bank_static()
{
    auto& group = g_body_arena.emplace<ModeBankGroup>();
    group.bank.init(kSampleRate);
    group.bank.set_params(220.f, 0.6f, 0.8f, 0.7f);
}

float proc_mode_bank_static()
{
    auto& group = g_body_arena.get<ModeBankGroup>();
    const float* in = test_input();
    group.bank.set_params(220.f, 0.6f, 0.8f, 0.7f);
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) acc += group.bank.process(in[i]);
    return acc;
}

// --- body/ks_string_pair --------------------------------------------------
void setup_ks_pair()
{
    auto& group = g_body_arena.emplace<KsGroup>();
    group.a.Init(kSampleRate);
    group.b.Init(kSampleRate);
    group.a.SetFreq(220.f);
    group.b.SetFreq(220.f * 1.008f);   // the DETUNE spread a voice runs with
    group.a.SetBrightness(0.7f);
    group.b.SetBrightness(0.7f);
    group.a.SetDamping(0.7f);
    group.b.SetDamping(0.7f);
    group.a.SetNonLinearity(0.4f);
    group.b.SetNonLinearity(0.4f);
}

float proc_ks_pair()
{
    auto& group = g_body_arena.get<KsGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += group.a.Process(in[i]) + group.b.Process(in[i]);
    return acc;
}

// --- body/ks_string_pair_nolin: the same pair with dispersion OFF ---------
// SetNonLinearity(0) sends String::Process down the CURVED_BRIDGE branch with
// a zero amount: no allpass stretch line, no rand()/fonepole dispersion noise,
// bridge_curving and ap_gain both zero. Everything the nonlinearity buys is
// gone; a plain ReadHermite plus the damping filter remains.
//
// What it does NOT remove: the per-sample parameter block at the top of
// ProcessInternal (two powf, one atanf, one SetFrequency), which runs
// unconditionally in both branches. That is the point of this row. Against
// ks_string_pair it splits the string's cost in two -- what the nonlinearity
// costs, and what the recomputation costs -- and only the second is a defect
// this fork already knows how to fix (see engine/body/mode_bank.h, and the
// same shape in daisysp::Resonator).
void setup_ks_pair_nolin()
{
    auto& group = g_body_arena.emplace<KsGroup>();
    group.a.Init(kSampleRate);
    group.b.Init(kSampleRate);
    group.a.SetFreq(220.f);
    group.b.SetFreq(220.f * 1.008f);
    group.a.SetBrightness(0.7f);
    group.b.SetBrightness(0.7f);
    group.a.SetDamping(0.7f);
    group.b.SetDamping(0.7f);
    group.a.SetNonLinearity(0.f);       // the only difference from setup_ks_pair
    group.b.SetNonLinearity(0.f);
}

float proc_ks_pair_nolin()
{
    auto& group = g_body_arena.get<KsGroup>();
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += group.a.Process(in[i]) + group.b.Process(in[i]);
    return acc;
}

// --- body/ks_string_pair_port: the same pair, coefficients at control rate --
// spky::KsString is daisysp::String with the parameter block moved out of the
// per-sample path (engine/body/ks_string.h). Same four parameters as
// setup_ks_pair, same nonlinearity, and set_params called once per block --
// the 96-sample control tick a real BodyVoice runs on. Against ks_string_pair
// this is the whole question: what the string costs once it stops recomputing
// two powf, an atanf and a tanf every sample.
//
// The parameters wobble by +-3 cents per block for the same reason
// body/mode_bank_24 does: a held parameter would let the dirty check
// short-circuit recompute() away entirely and price a voice nobody is
// modulating. This is the moving case.
void setup_ks_pair_port()
{
    auto& group = g_body_arena.emplace<KsPortGroup>();
    group.a.init(kSampleRate, 0x9E3779B9u);
    group.b.init(kSampleRate, 0x85EBCA6Bu);
    group.a.set_params(220.f, 0.7f, 0.7f, 0.4f);
    group.b.set_params(220.f * 1.008f, 0.7f, 0.7f, 0.4f);
    group.wobble_up = false;
}

float proc_ks_pair_port()
{
    auto& group = g_body_arena.get<KsPortGroup>();
    const float* in = test_input();
    group.wobble_up = !group.wobble_up;
    const float r = group.wobble_up ? kWobbleUpRatio : kWobbleDownRatio;
    group.a.set_params(220.f * r, 0.7f, 0.7f, 0.4f);
    group.b.set_params(220.f * 1.008f * r, 0.7f, 0.7f, 0.4f);
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += group.a.process(in[i]) + group.b.process(in[i]);
    return acc;
}

// --- body/body_2x4 & body/body_2x4_string: engine-level worst case ---------
// Task 13: mirrors Task 8's matched-pair pattern (bench/engine_2x4.h) with
// BodyEngine standing in for SynthEngine/WaveEngine -- kVoices == 1 rather
// than 4, but setup_engine_2x4/proc_engine_2x4 only call set_seed, init,
// set_decay, set_cycle, set_flow, trigger and active_voices, every one of
// which SynthEngineT<BodyVoice> provides.
//
// MATL (LANE_SOURCE) must reach the engine BEFORE trigger(): BodyVoice::
// trigger() calls _apply_params() itself (body_voice.cpp), which reads
// _matl as pushed by the LAST set_morph() call -- and set_morph() is only
// ever called from SynthEngineT::_update_control(), the control tick.
// set_targets() below runs before setup_engine_2x4() (which owns both
// init() and trigger()); init() calls _update_control() once at its own end
// (synth_engine.cpp), so by the time setup_engine_2x4()'s trigger() calls
// land, the mode bank and the string/modal blend are already primed at the
// row's intended MATL, not the engine's boot default. Every lane but
// LANE_SOURCE stays at the engine's own boot default (synth_engine.h);
// LANE_SOURCE is the only difference between the two rows below.
void setup_body_pair(float matl)
{
    auto& pair = g_body_arena.emplace<BodyPairGroup>();
    float t[LANE_COUNT] = { matl, 0.5f, 0.5f, 0.f, 0.8f };
    pair.a.set_targets(t, 0.f);
    pair.b.set_targets(t, 0.f);
    setup_engine_2x4(pair.a, pair.b);
}

float proc_body_2x4_pair()
{
    auto& pair = g_body_arena.get<BodyPairGroup>();
    return proc_engine_2x4(pair.a, pair.b);
}

// body/body_2x4: MATL = 1, the modal end. BodyVoice::process() computes the
// string pair AND the mode bank unconditionally every sample -- MATL only
// weights their blend, spec §3 -- but MATL also reaches KsString::set_params
// as the dispersion amount (body_voice.cpp _apply_params): at MATL = 1 the
// string pair runs its full nonlinear dispersion branch, the more expensive
// of the two, so this is the worst case, not merely a representative one.
void setup_body_2x4() { setup_body_pair(1.f); }

// body/body_2x4_string: the same pair at MATL = 0, the ablation that prices
// the mode bank in context -- body_2x4 minus body_2x4_string is what the
// bank costs a running BODY voice on top of the (cheaper, non-dispersing)
// string pair, the same way ks_string_pair_nolin isolates the string's own
// nonlinearity above.
void setup_body_2x4_string() { setup_body_pair(0.f); }

// --- body/inst_body_worst: BODY on both decks, excitation bus hot ----------
// Mirrors setup_inst_worst / proc_inst (bench/workloads_system.cpp:306,332)
// -- same instrument-worst configuration (8 voices via 4-note COLOR, every
// FX block on, high diffusion, echo at maximum) -- with BODY on both parts
// and the excitation bus actually reaching the voices. Not reusable
// literally: InstrumentGroup and proc_inst are internal-linkage names in
// workloads_system.cpp's anonymous namespace, invisible from this
// translation unit, so this row carries its own group (BodyInstGroup above)
// and its own process function, duplicating proc_inst's shape exactly
// (process the block, retrigger both parts every ~250 blocks, fold both
// parts' active_voices() into the checksum) rather than inventing a
// different one.
void setup_inst_body_worst()
{
    auto& group = g_body_arena.emplace<BodyInstGroup>();
    Instrument& inst = group.instrument;
    inst.init(kSampleRate, fx_mem());
    inst.set_tempo_bpm(120.f);
    group.counter = 0;

    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_engine(p, ENGINE_BODY);
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
        // Excitation bus (spec §6, Tasks 9+10): all three sources enabled
        // and SUB > 0. BodyVoice hard-gates the whole bus at SUB == 0
        // (body_voice.cpp process()); leaving SUB at whatever
        // setup_inst_worst never touches would price a BODY instrument with
        // the bus switched OFF -- the opposite of a worst case.
        inst.set_excitation_sources(p, true, true, true);
        inst.set_voice_sub(p, 1.f);
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

float proc_inst_body_worst()
{
    auto& group = g_body_arena.get<BodyInstGroup>();
    Instrument& inst = group.instrument;
    const float* in = test_input();
    inst.process(in, in, group.out_l, group.out_r, kBlock);
    // Keep the voices busy: a fire every ~half second on both parts, same
    // cadence proc_inst uses.
    if (++group.counter >= 250) {
        group.counter = 0;
        inst.trigger_manual(PART_A);
        inst.trigger_manual(PART_B);
    }
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += group.out_l[i] + group.out_r[i];
    // Same guard proc_inst uses: fold both parts' active voice counts into
    // the returned value so a wrong voice count moves the checksum instead
    // of passing silently.
    acc += static_cast<float>(inst.active_voices(PART_A));
    acc += static_cast<float>(inst.active_voices(PART_B));
    return acc;
}

} // namespace

const Workload kBodyWorkloads[] = {
    { "body", "mode_bank_24",        setup_mode_bank,        proc_mode_bank        },
    { "body", "mode_bank_24_static", setup_mode_bank_static, proc_mode_bank_static },
    { "body", "ks_string_pair",      setup_ks_pair,          proc_ks_pair          },
    { "body", "ks_string_pair_nolin", setup_ks_pair_nolin,   proc_ks_pair_nolin    },
    { "body", "ks_string_pair_port",  setup_ks_pair_port,    proc_ks_pair_port     },
    { "body", "body_2x4",             setup_body_2x4,        proc_body_2x4_pair    },
    { "body", "body_2x4_string",      setup_body_2x4_string, proc_body_2x4_pair    },
    { "body", "inst_body_worst",      setup_inst_body_worst, proc_inst_body_worst  },
};
const int kBodyCount = sizeof(kBodyWorkloads) / sizeof(kBodyWorkloads[0]);

} // namespace bench
