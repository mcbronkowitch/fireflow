#include "protocol.h"

#include <cmath>


namespace audition {

std::uint32_t fnv1a32(const std::uint8_t* bytes, std::size_t size)
{
    std::uint32_t value = 0x811c9dc5u;
    for(std::size_t index = 0; index < size; ++index)
    {
        value ^= bytes[index];
        value *= 0x01000193u;
    }
    return value;
}


bool validate_upload(
    const float* planar,
    std::size_t uploaded_frames,
    const UploadExpectation& expected)
{
    if(planar == nullptr || uploaded_frames == 0
       || uploaded_frames != expected.frames)
        return false;

    const std::size_t bytes = uploaded_frames * 2u * sizeof(float);
    if(fnv1a32(reinterpret_cast<const std::uint8_t*>(planar), bytes)
       != expected.fnv1a)
        return false;

    const float first_l = planar[0];
    const float last_l = planar[uploaded_frames - 1u];
    const float first_r = planar[uploaded_frames];
    const float last_r = planar[uploaded_frames * 2u - 1u];
    if(!std::isfinite(first_l) || !std::isfinite(first_r)
       || !std::isfinite(last_l) || !std::isfinite(last_r))
        return false;

    return first_l == expected.first_l && first_r == expected.first_r
           && last_l == expected.last_l && last_r == expected.last_r;
}

}  // namespace audition
