#include "workload.h"
#include "body/mode_bank.h"
#include "PhysicalModeling/KarplusString.h"
#include "mem.h"

namespace bench {
namespace {

spky::ModeBank g_bank;
int            g_bank_ctr = 0;

void setup_mode_bank()
{
    g_bank.init(kSampleRate);
    g_bank.set_params(220.f, 0.6f, 0.8f, 0.7f);
    g_bank_ctr = 0;
}

// One control tick per block, exactly as BodyVoice will drive it: the point
// of the row is that the coefficient math is NOT in the per-sample loop.
float proc_mode_bank()
{
    const float* in = test_input();
    g_bank.set_params(220.f, 0.6f, 0.8f, 0.7f);
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) acc += g_bank.process(in[i]);
    return acc;
}

daisysp::String g_str_a, g_str_b;

void setup_ks_pair()
{
    g_str_a.Init(kSampleRate);
    g_str_b.Init(kSampleRate);
    g_str_a.SetFreq(220.f);
    g_str_b.SetFreq(220.f * 1.008f);   // the DETUNE spread a voice runs with
    g_str_a.SetBrightness(0.7f);
    g_str_b.SetBrightness(0.7f);
    g_str_a.SetDamping(0.7f);
    g_str_b.SetDamping(0.7f);
    g_str_a.SetNonLinearity(0.4f);
    g_str_b.SetNonLinearity(0.4f);
}

float proc_ks_pair()
{
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += g_str_a.Process(in[i]) + g_str_b.Process(in[i]);
    return acc;
}

} // namespace

const Workload kBodyWorkloads[] = {
    { "body", "mode_bank_24",  setup_mode_bank, proc_mode_bank },
    { "body", "ks_string_pair", setup_ks_pair,  proc_ks_pair   },
};
const int kBodyCount = sizeof(kBodyWorkloads) / sizeof(kBodyWorkloads[0]);

} // namespace bench
