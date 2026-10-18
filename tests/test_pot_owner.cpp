// Pot ownership: a pot takes its parameter only by being moved past a
// threshold from a reference reading. Pure data logic, no hardware type.
// Spec: docs/superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md
// section 2.
#include <doctest/doctest.h>
#include "../shell/pot_owner.h"
#include "../shell/scan_value.h"

namespace {
constexpr int kT = shell::kPotTouch;
}

TEST_CASE("owner: boot takes a reference and writes nothing") {
    shell::PotOwner o{};
    CHECK(shell::pot_own(o, 30000, 5, true, kT) == shell::Own::Idle);
    CHECK(o.has_ref);
    CHECK(o.ref == 30000);
    CHECK_FALSE(o.owned);
    CHECK(o.target == 5);
}

TEST_CASE("owner: an invalid span takes no reference") {
    shell::PotOwner o{};
    CHECK(shell::pot_own(o, 100, 5, false, kT) == shell::Own::Idle);
    CHECK_FALSE(o.has_ref);
    CHECK(shell::pot_own(o, 20000, 5, true, kT) == shell::Own::Idle);
    CHECK(o.has_ref);
    CHECK(o.ref == 20000);
}

TEST_CASE("owner: jitter inside the threshold never takes") {
    shell::PotOwner o{};
    constexpr int ref = 30000;
    CHECK(shell::pot_own(o, ref, 5, true, kT) == shell::Own::Idle);
    for (int i = 0; i < 1000; ++i) {
        const int off = (i % 2 == 0) ? (kT - 1) : -(kT - 1);
        CHECK(shell::pot_own(o, static_cast<uint16_t>(ref + off), 5, true, kT)
              == shell::Own::Idle);
    }
    CHECK_FALSE(o.owned);
}

TEST_CASE("owner: a move past the threshold takes, then owns") {
    shell::PotOwner o{};
    constexpr int ref = 30000;
    shell::pot_own(o, ref, 5, true, kT);
    CHECK(shell::pot_own(o, static_cast<uint16_t>(ref + kT + 1), 5, true, kT)
          == shell::Own::Took);
    CHECK(o.owned);
    // Owned stays owned, wherever the pot goes -- including back to the ref.
    CHECK(shell::pot_own(o, static_cast<uint16_t>(ref + kT + 2), 5, true, kT)
          == shell::Own::Owned);
    CHECK(shell::pot_own(o, static_cast<uint16_t>(ref), 5, true, kT)
          == shell::Own::Owned);
    CHECK(shell::pot_own(o, 0, 5, true, kT) == shell::Own::Owned);
}

TEST_CASE("owner: a move downward past the threshold takes too") {
    shell::PotOwner o{};
    constexpr int ref = 30000;
    shell::pot_own(o, ref, 5, true, kT);
    CHECK(shell::pot_own(o, static_cast<uint16_t>(ref - kT - 1), 5, true, kT)
          == shell::Own::Took);
}

TEST_CASE("owner: exactly the threshold is not yet a move") {
    shell::PotOwner o{};
    constexpr int ref = 30000;
    shell::pot_own(o, ref, 5, true, kT);
    CHECK(shell::pot_own(o, static_cast<uint16_t>(ref + kT), 5, true, kT)
          == shell::Own::Idle);
}

TEST_CASE("owner: a snap region is not a move") {
    shell::PotOwner o{};
    // The reference sits inside the zero snap region (raw <= zero + hysteresis).
    shell::pot_own(o, 10, 5, true, kT);
    CHECK(shell::pot_own(o, static_cast<uint16_t>(10 + shell::kPotHysteresis + 1),
                         5, true, kT) == shell::Own::Idle);
    CHECK_FALSE(o.owned);
}

TEST_CASE("owner: the difference is computed without unsigned wrap") {
    shell::PotOwner o{};
    shell::pot_own(o, 65535, 5, true, kT);
    CHECK(shell::pot_own(o, 0, 5, true, kT) == shell::Own::Took);
    shell::PotOwner p{};
    shell::pot_own(p, 0, 5, true, kT);
    CHECK(shell::pot_own(p, 65535, 5, true, kT) == shell::Own::Took);
}

TEST_CASE("owner: a new target releases and re-references") {
    shell::PotOwner o{};
    shell::pot_own(o, 30000, 5, true, kT);
    shell::pot_own(o, static_cast<uint16_t>(30000 + kT + 1), 5, true, kT);
    REQUIRE(o.owned);
    constexpr uint16_t r = 12000;
    CHECK(shell::pot_own(o, r, 6, true, kT) == shell::Own::Idle);
    CHECK(o.target == 6);
    CHECK(o.has_ref);
    CHECK_FALSE(o.owned);
    CHECK(o.ref == r);
    CHECK(shell::pot_own(o, static_cast<uint16_t>(r + kT + 1), 6, true, kT)
          == shell::Own::Took);
}

TEST_CASE("owner: release re-references at the next reading") {
    shell::PotOwner o{};
    shell::pot_own(o, 30000, 5, true, kT);
    shell::pot_own(o, static_cast<uint16_t>(30000 + kT + 1), 5, true, kT);
    REQUIRE(o.owned);
    shell::pot_release(o);
    CHECK_FALSE(o.owned);
    CHECK_FALSE(o.has_ref);
    CHECK(shell::pot_own(o, 45000, 5, true, kT) == shell::Own::Idle);
    CHECK(o.has_ref);
    CHECK(o.ref == 45000);
}

TEST_CASE("owner: an invalid span after release keeps waiting for a reference") {
    shell::PotOwner o{};
    shell::pot_own(o, 30000, 5, true, kT);
    shell::pot_release(o);
    CHECK(shell::pot_own(o, 45000, 5, false, kT) == shell::Own::Idle);
    CHECK_FALSE(o.has_ref);
}
