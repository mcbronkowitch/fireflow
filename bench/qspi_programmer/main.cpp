#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <daisy_seed.h>

#include "../qspi_digest.h"
#include "program_core.h"

namespace {

constexpr uintptr_t kStagingAddress = 0x24040000u;
constexpr uintptr_t kMappedAddress = 0x90040000u;

daisy::DaisySeed g_hw;
char g_message[160];

void semihost_write0(const char* message)
{
    register int r0 asm("r0") = 0x04;
    register const char* r1 asm("r1") = message;
    asm volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
}

[[noreturn]] void halt_after_report()
{
    asm volatile("bkpt 0");
    while(true) {}
}

struct DaisyQspiDevice {
    bool erase_block(uint32_t offset)
    {
        return g_hw.qspi.EraseBlock(offset, false)
               == daisy::QSPIHandle::Result::OK;
    }

    bool write(uint32_t offset, std::size_t size, const uint8_t* source)
    {
        return g_hw.qspi.Write(
                   offset,
                   static_cast<uint32_t>(size),
                   const_cast<uint8_t*>(source))
               == daisy::QSPIHandle::Result::OK;
    }

    void invalidate(const volatile uint8_t* mapped, std::size_t size)
    {
        dsy_dma_invalidate_cache_for_buffer(
            const_cast<uint8_t*>(mapped),
            size);
    }
};

const char* error_stage(bench::QspiProgramResult result)
{
    switch(result)
    {
        case bench::QspiProgramResult::erase_failed: return "erase";
        case bench::QspiProgramResult::write_failed: return "write";
        case bench::QspiProgramResult::compare_failed: return "compare";
        default: return "unknown";
    }
}

} // namespace

int main()
{
    daisy::System::InitBackupSram();
    daisy::boot_info.version = daisy::System::BootInfo::Version::v6_1;
    g_hw.Init(true);

    auto* source = reinterpret_cast<const uint8_t*>(kStagingAddress);
    auto* mapped = reinterpret_cast<const volatile uint8_t*>(kMappedAddress);
    DaisyQspiDevice device;
    const auto result =
        bench::program_qspi_payload(device, source, mapped);

    if(result != bench::QspiProgramResult::ok)
    {
        std::snprintf(
            g_message,
            sizeof(g_message),
            "QSPI_PROGRAM_ERROR,%s\n",
            error_stage(result));
        semihost_write0(g_message);
        halt_after_report();
    }

    char digest[65] = {};
    bench::sha256_hex(mapped, bench::kQspiPayloadSize, digest);
    const auto* uid = reinterpret_cast<const uint32_t*>(UID_BASE);
    std::snprintf(
        g_message,
        sizeof(g_message),
        "QSPI_PROGRAM_OK,90040000,65024,%s,%08lx%08lx%08lx\n",
        digest,
        static_cast<unsigned long>(uid[0]),
        static_cast<unsigned long>(uid[1]),
        static_cast<unsigned long>(uid[2]));
    semihost_write0(g_message);
    halt_after_report();
}
