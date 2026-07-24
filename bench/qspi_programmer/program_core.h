#pragma once

#include <cstddef>
#include <cstdint>

namespace bench {

constexpr uint32_t kQspiPayloadOffset = 0x00040000u;

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
    std::size_t size,
    const volatile uint8_t* mapped)
{
    if(!device.erase_block(kQspiPayloadOffset))
        return QspiProgramResult::erase_failed;
    if(!device.write(kQspiPayloadOffset, size, source))
        return QspiProgramResult::write_failed;

    device.invalidate(mapped, size);
    for(std::size_t i = 0; i < size; ++i)
    {
        if(mapped[i] != source[i])
            return QspiProgramResult::compare_failed;
    }
    return QspiProgramResult::ok;
}

} // namespace bench
