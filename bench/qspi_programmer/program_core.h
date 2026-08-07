#pragma once

#include <cstddef>
#include <cstdint>

namespace bench {

constexpr uint32_t kQspiPayloadOffset = 0x00100000u;
constexpr std::size_t kQspiPayloadSize = 0x0000fe00u;

enum class QspiProgramResult {
    ok,
    erase_failed,
    write_failed,
    compare_failed,
};

template <typename Device>
QspiProgramResult program_qspi_payload(
    Device& device,
    const uint8_t* source,
    const volatile uint8_t* mapped)
{
    if(!device.erase_block(kQspiPayloadOffset))
        return QspiProgramResult::erase_failed;
    if(!device.write(kQspiPayloadOffset, kQspiPayloadSize, source))
        return QspiProgramResult::write_failed;

    device.invalidate(mapped, kQspiPayloadSize);
    for(std::size_t i = 0; i < kQspiPayloadSize; ++i)
    {
        if(mapped[i] != source[i])
            return QspiProgramResult::compare_failed;
    }
    return QspiProgramResult::ok;
}

} // namespace bench
