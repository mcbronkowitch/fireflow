#include "doctest/doctest.h"
#include "flow/flow_rng.h"
using namespace spky::flow;

TEST_CASE("flow rng: streams are deterministic and independent") {
    // Same triple -> same sequence.
    auto a = make_stream(0xDEADBEEF, kStreamParamBase + 3, 0);
    auto b = make_stream(0xDEADBEEF, kStreamParamBase + 3, 0);
    for (int i = 0; i < 16; ++i) CHECK(a.next_u32() == b.next_u32());
    // Bumping ONE counter changes only that stream: id 3's sequence moves,
    // id 4's is untouched.
    auto c0 = make_stream(0xDEADBEEF, kStreamParamBase + 4, 0);
    auto c1 = make_stream(0xDEADBEEF, kStreamParamBase + 4, 0);
    auto d  = make_stream(0xDEADBEEF, kStreamParamBase + 3, 1);
    CHECK(c0.next_u32() == c1.next_u32());          // neighbor unmoved
    auto e = make_stream(0xDEADBEEF, kStreamParamBase + 3, 0);
    CHECK(d.next_u32() != e.next_u32());            // own stream moved
    // Different masters diverge.
    auto f = make_stream(0xDEADBEEF, kStreamArch, 0);
    auto g = make_stream(0xDEADBEF0, kStreamArch, 0);
    CHECK(f.next_u32() != g.next_u32());
}
