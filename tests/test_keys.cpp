// The panel's keys, debounced on the host: on the board a key that bounces
// into two presses, or never lets go, is a gesture that does the wrong thing.
// Spec: docs/superpowers/specs/2026-10-02-rev-a-p6a-panel-scan-design.md
// section 3.4.
#include <doctest/doctest.h>
#include "../shell/keys.h"
#include "../shell/mux_plan.h"

namespace {
constexpr uint32_t kUp = 0xFFFFFFFFu;   // every input high: every key released
constexpr uint32_t down(int bit) { return kUp & ~(1u << bit); }
constexpr shell::KeyPad kTwo{2, {7, 6}};

void feed(shell::KeyState& s, const shell::KeyPad& p, uint32_t ret, int n)
{
    for(int i = 0; i < n; ++i) shell::key_update(s, p, ret);
}
}

TEST_CASE("keys: fewer equal reads than the debounce are not a press") {
    shell::KeyState s;
    feed(s, shell::kCouponKeys, down(7), shell::kKeyDebounce - 1);
    CHECK(s.pressed == 0);
    feed(s, shell::kCouponKeys, down(7), 1);
    CHECK(s.pressed == 1);
    CHECK(s.presses[0] == 1);
}

TEST_CASE("keys: a bouncing contact never registers") {
    shell::KeyState s;
    for(int i = 0; i < 20; ++i)
        shell::key_update(s, shell::kCouponKeys, (i & 1) ? kUp : down(7));
    CHECK(s.pressed == 0);
    CHECK(s.presses[0] == 0);
}

TEST_CASE("keys: a held key counts once, and release is debounced too") {
    shell::KeyState s;
    feed(s, shell::kCouponKeys, down(7), 50);
    CHECK(s.presses[0] == 1);
    feed(s, shell::kCouponKeys, kUp, shell::kKeyDebounce - 1);
    CHECK(s.pressed == 1);
    feed(s, shell::kCouponKeys, kUp, 1);
    CHECK(s.pressed == 0);
    CHECK(s.presses[0] == 1);
}

TEST_CASE("keys: held from the first read counts as one press") {
    // A key held while the board powers up (Review Focus 3).
    shell::KeyState s;
    feed(s, shell::kCouponKeys, down(7), 200);
    CHECK(s.pressed == 1);
    CHECK(s.presses[0] == 1);
}

TEST_CASE("keys: five clean presses count five") {
    shell::KeyState s;
    for(int i = 0; i < 5; ++i)
    {
        feed(s, shell::kCouponKeys, down(7), 5);
        feed(s, shell::kCouponKeys, kUp, 5);
    }
    CHECK(s.presses[0] == 5);
    CHECK(s.pressed == 0);
}

TEST_CASE("keys: active low -- a high input is a released key") {
    shell::KeyState s;
    feed(s, shell::kCouponKeys, kUp, 10);
    CHECK(s.pressed == 0);
    feed(s, shell::kCouponKeys, 0u, shell::kKeyDebounce);
    CHECK(s.pressed == 1);
}

TEST_CASE("keys: two keys debounce independently") {
    shell::KeyState s;
    feed(s, kTwo, down(7), 2);
    feed(s, kTwo, down(7) & down(6), 1);   // key 1 starts while key 0 is mid-count
    CHECK(s.pressed == 0x1);
    feed(s, kTwo, down(7) & down(6), 2);
    CHECK(s.pressed == 0x3);
    CHECK(s.presses[0] == 1);
    CHECK(s.presses[1] == 1);
}

TEST_CASE("keys: the coupon key is the bit kCouponChain names") {
    CHECK(shell::kCouponKeys.count == 1);
    CHECK(shell::kCouponKeys.bit[0] == shell::button_bit(shell::kCouponChain));
}
