#include "workload.h"
#include "body/mode_bank.h"
#include "PhysicalModeling/KarplusString.h"
#include "mem.h"
#include "serial_arena.h"

namespace bench {
namespace {

// Rows in this family run strictly serially (body/mode_bank_24,
// body/mode_bank_24_static, body/ks_string_pair, in table order below), so
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

SerialArena<ModeBankGroup, KsGroup> g_body_arena;

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

} // namespace

const Workload kBodyWorkloads[] = {
    { "body", "mode_bank_24",        setup_mode_bank,        proc_mode_bank        },
    { "body", "mode_bank_24_static", setup_mode_bank_static, proc_mode_bank_static },
    { "body", "ks_string_pair",      setup_ks_pair,          proc_ks_pair          },
    { "body", "ks_string_pair_nolin", setup_ks_pair_nolin,   proc_ks_pair_nolin    },
};
const int kBodyCount = sizeof(kBodyWorkloads) / sizeof(kBodyWorkloads[0]);

} // namespace bench
