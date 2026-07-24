#include "doctest/doctest.h"

#include <cstddef>
#include <cstdint>

#include "../bench/serial_arena.h"

namespace {

struct LifetimeProbe {
    LifetimeProbe(int id, int* destroyed)
    : id(id), destroyed(destroyed)
    {
    }
    ~LifetimeProbe() { *destroyed = id; }

    int id;
    int* destroyed;
};

struct alignas(32) WideProbe {
    explicit WideProbe(int value) : value(value) {}
    uint8_t bytes[97] = {};
    int value;
};

} // namespace

TEST_CASE("serial arena destroys the old row group before reusing its address") {
    bench::SerialArena<LifetimeProbe, WideProbe> arena;
    int destroyed = 0;
    LifetimeProbe& first = arena.emplace<LifetimeProbe>(7, &destroyed);
    const void* first_address = &first;

    WideProbe& second = arena.emplace<WideProbe>(11);
    CHECK(destroyed == 7);
    CHECK(static_cast<const void*>(&second) == first_address);
    CHECK(reinterpret_cast<std::uintptr_t>(&second) % alignof(WideProbe) == 0);
    CHECK(arena.get<WideProbe>().value == 11);
}

TEST_CASE("serial arena storage is max group size and alignment, not a sum") {
    using Arena = bench::SerialArena<LifetimeProbe, WideProbe>;
    static_assert(Arena::capacity == sizeof(WideProbe));
    static_assert(Arena::alignment == alignof(WideProbe));
    CHECK(Arena::capacity < sizeof(LifetimeProbe) + sizeof(WideProbe));
}
