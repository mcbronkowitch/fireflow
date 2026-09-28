// The wait-sweep probe's grid, its arms and its microsecond-to-cycle
// arithmetic. Spec section 8's first bullet, one TEST_CASE per claim.
#include <cstdlib>
#include <doctest/doctest.h>
#include "../shell/wait_plan.h"
#include "../shell/settle_plan.h"
#include "../shell/pot_plan.h"

TEST_CASE("wait grid: starts back to back, strictly increasing, fifteen points") {
    static_assert(shell::kWaitPoints == 15, "spec section 3 names fifteen points");
    CHECK(shell::kWaitGridUs[0] == 0u);
    for(int i = 1; i < shell::kWaitPoints; ++i) {
        CAPTURE(i);
        CHECK(shell::kWaitGridUs[i] > shell::kWaitGridUs[i - 1]);
    }
}

TEST_CASE("wait grid: arm C's reference and round two's three cadences are on it") {
    // Arm C is compared against arm A at identical commanded waits; a codec
    // point that is off the grid would compare against an interpolation.
    for(int k = 0; k < shell::kWaitCodecPoints; ++k) {
        CAPTURE(k);
        const int i = shell::wait_codec_grid_index(k);
        REQUIRE(i >= 0);
        CHECK(shell::kWaitGridUs[i] == shell::kWaitCodecGridUs[k]);
    }
    // Its own back-to-back reference first, then the three cadences round
    // two measured: 5 kHz, 1 kHz, 100 Hz.
    CHECK(shell::kWaitCodecGridUs[0] == 0u);
    CHECK(shell::kWaitCodecGridUs[1] == 200u);
    CHECK(shell::kWaitCodecGridUs[2] == 1000u);
    CHECK(shell::kWaitCodecGridUs[3] == 10000u);
    CHECK(shell::wait_codec_grid_index(-1) == -1);
    CHECK(shell::wait_codec_grid_index(shell::kWaitCodecPoints) == -1);
}

TEST_CASE("wait: us_to_cycles is exact across the whole grid, top included") {
    // 480 cycles per microsecond, exactly, at every grid point. The top point
    // is the one a 32-bit nanosecond route gets wrong.
    for(int i = 0; i < shell::kWaitPoints; ++i) {
        CAPTURE(i);
        CHECK(static_cast<uint64_t>(shell::us_to_cycles(shell::kWaitGridUs[i]))
              == static_cast<uint64_t>(shell::kWaitGridUs[i]) * 480u);
    }
    CHECK(shell::us_to_cycles(50000u) == 24000000u);
}

TEST_CASE("wait: the 32-bit nanosecond route really does wrap at 10 ms") {
    // Not a test of this file's code: a pinned demonstration that the trap
    // wait_plan.h's comment names is real, so nobody "simplifies"
    // us_to_cycles() into ns_to_cycles(us * 1000). This is cycles.h's
    // ns_to_cycles() body, transcribed, because cycles.h cannot be compiled
    // on the host (it includes daisy_seed.h).
    auto ns_to_cycles_32 = [](uint32_t ns) -> uint32_t { return ns * 480u / 1000u; };
    CHECK(ns_to_cycles_32(1000000u) == 480000u);          // 1 ms: still right
    CHECK(ns_to_cycles_32(10000000u) != 4800000u);        // 10 ms: wrapped
    CHECK(ns_to_cycles_32(10000000u) != shell::us_to_cycles(10000u));
}

TEST_CASE("wait: the sweep order is a permutation in both directions") {
    for(int dir = 0; dir < 2; ++dir) {
        CAPTURE(dir);
        bool seen[shell::kWaitPoints] = {};
        for(int k = 0; k < shell::kWaitPoints; ++k) {
            const int i = shell::wait_grid_order(k, shell::kWaitPoints, dir);
            REQUIRE(i >= 0);
            REQUIRE(i < shell::kWaitPoints);
            CHECK_FALSE(seen[i]);
            seen[i] = true;
        }
    }
    CHECK(shell::wait_grid_order(0, shell::kWaitPoints, 0) == 0);
    CHECK(shell::wait_grid_order(0, shell::kWaitPoints, 1) == shell::kWaitPoints - 1);
}

TEST_CASE("wait: the long rung is 387.5 cycles") {
    CHECK(shell::kSamplingLadderTenths[shell::kWaitLongRung] == 3875);
}

TEST_CASE("wait: the block estimate stays under two minutes") {
    // Spec section 6 estimates ~90 s. The bound is 120 s: loose enough that
    // the generous per-conversion costs do not trip it, tight enough that a
    // grid edit adding a 100 ms point (+ ~96 s over three arms and five
    // victims) does.
    const uint32_t ms = shell::wait_block_estimate_ms();
    CAPTURE(ms);
    CHECK(ms > 60000u);
    CHECK(ms < 120000u);
}

// --- Round four: the pot victims (spec 2026-09-28-coupon-pot-round-design.md
// section 5) ---

TEST_CASE("wait: the pot round appends three victims and keeps round one's") {
    CHECK(shell::kWaitVictimsMax == 8);
    CHECK(shell::wait_victim_count(false) == 5);
    CHECK(shell::wait_victim_count(true) == 8);
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        CAPTURE(v);
        const shell::XtalkVictim w = shell::wait_victim(v);
        CHECK(w.group == shell::kXtalkVictimTable[v].group);
        CHECK(w.channel == shell::kXtalkVictimTable[v].channel);
        CHECK(w.r_src_ohm == shell::kXtalkVictimTable[v].r_src_ohm);
    }
    for(int i = 0; i < shell::kPotCount; ++i) {
        CAPTURE(i);
        const shell::XtalkVictim w = shell::wait_victim(shell::kXtalkVictims + i);
        CHECK(w.group == shell::kPots[i].group);
        CHECK(w.channel == shell::kPots[i].channel);
        CHECK(w.r_src_ohm == shell::pot_r_src_mid(shell::kPots[i].r_track_ohm));
    }
}

TEST_CASE("wait: the block estimate scales with the victim count") {
    // Spec section 5: ~89.7 s at five victims, ~143.5 s at eight. The
    // estimate is per-victim cost times the count, floored to ms once, so
    // the two differ from an exact 8:5 by less than 8 ms.
    const uint32_t five  = shell::wait_block_estimate_ms(5);
    const uint32_t eight = shell::wait_block_estimate_ms(8);
    CAPTURE(five);
    CAPTURE(eight);
    CHECK(five == shell::wait_block_estimate_ms());
    CHECK(std::llabs(static_cast<long long>(eight) * 5 - static_cast<long long>(five) * 8) < 8);
    CHECK(eight > 120000u);
    CHECK(eight < 180000u);
}
