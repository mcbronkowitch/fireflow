#pragma once

#include <cstddef>
#include <cstdint>

namespace bench {

// The volatile source is intentional: on target this must read the bytes from
// the live memory-mapped QSPI device, not an optimizer-synthesized copy.
void sha256_hex(
    const volatile uint8_t* data,
    std::size_t size,
    char output[65]);

} // namespace bench
