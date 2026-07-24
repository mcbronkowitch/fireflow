#include "doctest/doctest.h"

#include "../bench/rand_shim.h"

TEST_CASE("bench rand shim repeats a fixed full-scale sequence")
{
    bench::srand(7u);
    CHECK(bench::rand() == 1025555898);
    CHECK(bench::rand() == 1775940049);
    CHECK(bench::rand() == 483148028);
    CHECK(bench::rand() == 1833871403);
    CHECK(bench::rand() == 211918734);

    bench::srand(7u);
    CHECK(bench::rand() == 1025555898);
    CHECK(bench::rand() == 1775940049);
}
