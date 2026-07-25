#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <daisy_seed.h>

#include "factory_meta.h"
#include "init_patch.h"
#include "instrument.h"
#include "memory.h"
#include "protocol.h"
#include "../rand_shim.h"

extern "C" volatile std::uint32_t g_audition_state;

namespace {

constexpr std::size_t kBlockSize = 96;
constexpr float kSampleRate = 48000.f;
constexpr float kOutputTrim = 0.25f;

daisy::DaisySeed g_seed;
spky::Instrument g_instrument;

void silence(daisy::AudioHandle::OutputBuffer out, std::size_t size)
{
    std::fill_n(out[0], size, 0.f);
    std::fill_n(out[1], size, 0.f);
}


void audio_callback(
    daisy::AudioHandle::InputBuffer,
    daisy::AudioHandle::OutputBuffer out,
    std::size_t size)
{
    if(g_audition_state
           != static_cast<std::uint32_t>(audition::State::Running)
       || size > kBlockSize)
    {
        silence(out, size);
        return;
    }

    float zero[kBlockSize] = {};
    float left[kBlockSize];
    float right[kBlockSize];
    g_instrument.process(zero, zero, left, right, size);
    for(std::size_t index = 0; index < size; ++index)
    {
        out[0][index] = left[index] * kOutputTrim;
        out[1][index] = right[index] * kOutputTrim;
    }
}


[[noreturn]] void fail_silent()
{
    g_audition_state
        = static_cast<std::uint32_t>(audition::State::Error);
    while(true)
    {
    }
}

}  // namespace


extern "C" {
volatile std::uint32_t g_audition_state
    = static_cast<std::uint32_t>(audition::State::Booting);
}


int main()
{
    daisy::System::InitBackupSram();
    daisy::boot_info.version
        = daisy::System::BootInfo::Version::v6_1;

    g_seed.Init(true);
    g_seed.SetAudioBlockSize(kBlockSize);
    g_seed.SetAudioSampleRate(
        daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
    audition::init_memory();

    g_audition_state
        = static_cast<std::uint32_t>(audition::State::Waiting);
    __asm volatile("bkpt #0");

    if(g_audition_state
       != static_cast<std::uint32_t>(audition::State::UploadReady))
        fail_silent();

    const audition::UploadExpectation expected{
        audition::kFactoryFrames,
        audition::kFactoryFnv1a,
        audition::kFactoryFirstL,
        audition::kFactoryFirstR,
        audition::kFactoryLastL,
        audition::kFactoryLastR,
    };
    if(!audition::validate_upload(
           audition::factory_left(), audition::kFactoryFrames, expected))
        fail_silent();

    bench::srand(1u);
    g_instrument.init(kSampleRate, audition::fx_memory());
    g_instrument.load_sample(
        spky::PART_B,
        audition::factory_left(),
        audition::factory_right(),
        audition::kFactoryFrames);
    audition::apply_init_patch(g_instrument);

    g_audition_state
        = static_cast<std::uint32_t>(audition::State::Running);
    g_seed.StartAudio(audio_callback);
    while(true)
    {
    }
}
