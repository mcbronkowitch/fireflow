#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

#include "../bench/audition/protocol.h"


TEST_CASE("Seed audition accepts an exact planar factory upload")
{
    const float samples[] = {0.f, -1.f, 0.75f, 0.5f};
    const audition::UploadExpectation expected{
        2,
        audition::fnv1a32(
            reinterpret_cast<const std::uint8_t*>(samples), sizeof(samples)),
        0.f,
        0.75f,
        -1.f,
        0.5f,
    };

    CHECK(audition::validate_upload(samples, 2, expected));
}


TEST_CASE("Seed audition rejects corrupted data before audio starts")
{
    const float samples[] = {0.f, -1.f, 0.75f, 0.5f};
    audition::UploadExpectation expected{
        2,
        audition::fnv1a32(
            reinterpret_cast<const std::uint8_t*>(samples), sizeof(samples)),
        0.f,
        0.75f,
        -1.f,
        0.5f,
    };

    SUBCASE("hash mismatch")
    {
        expected.fnv1a ^= 1u;
        CHECK_FALSE(audition::validate_upload(samples, 2, expected));
    }
    SUBCASE("sentinel mismatch")
    {
        expected.last_r = 0.25f;
        CHECK_FALSE(audition::validate_upload(samples, 2, expected));
    }
    SUBCASE("frame-count mismatch")
    {
        expected.frames = 3;
        CHECK_FALSE(audition::validate_upload(samples, 2, expected));
    }
}
