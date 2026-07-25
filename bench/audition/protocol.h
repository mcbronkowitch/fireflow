#pragma once

#include <cstddef>
#include <cstdint>


namespace audition {

enum class State : std::uint32_t
{
    Booting     = 0x41550001u,
    Waiting     = 0x41550002u,
    UploadReady = 0x41550003u,
    Running     = 0x41550004u,
    Error       = 0x4155ffffu,
};

struct UploadExpectation
{
    std::size_t frames;
    std::uint32_t fnv1a;
    float first_l;
    float first_r;
    float last_l;
    float last_r;
};

std::uint32_t fnv1a32(const std::uint8_t* bytes, std::size_t size);

bool validate_upload(
    const float* planar,
    std::size_t uploaded_frames,
    const UploadExpectation& expected);

}  // namespace audition
