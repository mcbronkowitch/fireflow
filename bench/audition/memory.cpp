#include "memory.h"

#include <daisy_seed.h>
#include <new>


extern "C" {
float DSY_SDRAM_BSS g_factory_upload[2][audition::kFactoryFrames];
}

namespace audition {
namespace {

spky::SampleBuffer::Frame DSY_SDRAM_BSS
    g_sampler[spky::PART_COUNT][kFactoryFrames];
float DSY_SDRAM_BSS
    g_echo[spky::PART_COUNT][spky::Flux::kMaxSamples];

alignas(alignof(spky::AmbientReverb))
    unsigned char DSY_SDRAM_BSS
        g_reverb_storage[sizeof(spky::AmbientReverb)];

spky::FxMem g_fx_memory;
bool g_memory_ready = false;

constexpr std::size_t kSdramBytes
    = sizeof(g_factory_upload) + sizeof(g_sampler) + sizeof(g_echo)
      + sizeof(g_reverb_storage);
static_assert(kSdramBytes < 64u * 1024u * 1024u,
              "audition memory must fit the Seed's 64 MiB SDRAM");

}  // namespace


void init_memory()
{
    if(g_memory_ready)
        return;

    auto* reverb = new(g_reverb_storage) spky::AmbientReverb();
    for(int part = 0; part < spky::PART_COUNT; ++part)
    {
        g_fx_memory.echo[part] = g_echo[part];
        g_fx_memory.sampler_buf[part] = g_sampler[part];
    }
    g_fx_memory.reverb = reverb;
    g_fx_memory.sampler_frames = kFactoryFrames;
    g_memory_ready = true;
}


const spky::FxMem& fx_memory()
{
    return g_fx_memory;
}


float* factory_left()
{
    return g_factory_upload[0];
}


float* factory_right()
{
    return g_factory_upload[1];
}

}  // namespace audition
