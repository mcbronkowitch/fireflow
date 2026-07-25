#include <doctest/doctest.h>
#include "mod/lane.h"
#include "mod/song_form.h"
#include <cmath>
#include <cstring>

using namespace spky;

namespace {

MelodyPattern generated_pattern(int steps, uint32_t seed) {
    MelodyPattern pattern{};
    Rng rng;
    rng.seed(seed);
    generate_phrase(Principle::Hierarchical, rng, steps,
                    pattern.pitch, pattern.gate, pattern.motif_id,
                    pattern.layout);
    pg_gen_groove(rng, pattern.layout.motif_len, pattern.cell_groove);
    expand_pattern_groove(pattern.cell_groove, steps, pattern.pattern_groove);
    return pattern;
}

float zone_distance(const MelodyPattern& a, const MelodyPattern& b,
                    int begin, int end) {
    float distance = 0.f;
    for (int i = begin; i < end; ++i) {
        distance += std::fabs(a.pitch[i] - b.pitch[i]);
        distance += a.pattern_groove.rank_of_slot[i] ==
                            b.pattern_groove.rank_of_slot[i]
                        ? 0.f : 0.25f;
        distance += 0.25f * std::fabs(
            static_cast<float>(a.pattern_groove.note_len[i]) -
            static_cast<float>(b.pattern_groove.note_len[i]));
    }
    return distance;
}

int turnaround_difference(const MelodyPattern& a, const MelodyPattern& b,
                          int begin, int end) {
    int differences = 0;
    for (int i = begin; i < end; ++i) {
        if (std::fabs(a.pitch[i] - b.pitch[i]) > 1e-6f ||
            a.pattern_groove.rank_of_slot[i] !=
                b.pattern_groove.rank_of_slot[i] ||
            a.pattern_groove.note_len[i] !=
                b.pattern_groove.note_len[i])
            ++differences;
    }
    return differences;
}

void check_pattern_groove(const PatternGroove& groove) {
    REQUIRE(groove.len >= 1);
    REQUIRE(groove.len <= 32);
    bool seen[32] = {};
    CHECK(groove.rank_of_slot[0] == 0);
    for (int i = 0; i < groove.len; ++i) {
        REQUIRE(groove.rank_of_slot[i] < groove.len);
        CHECK_FALSE(seen[groove.rank_of_slot[i]]);
        seen[groove.rank_of_slot[i]] = true;
        CHECK(groove.note_len[i] >= 1);
        CHECK(groove.note_len[i] <= 4);
    }
}

void fnv_feed(uint64_t& hash, const void* data, size_t length) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

} // namespace

TEST_CASE("form values clamp and normal forms map bijectively to principles") {
    CHECK(clamp_form(-7) == FormMode::SongAAAB);
    CHECK(clamp_form(99) == FormMode::Ostinato);
    CHECK(form_basis(FormMode::SongAAAB, Principle::CallResponse)
          == Principle::CallResponse);

    const Principle principles[] = {
        Principle::TwoMotif,
        Principle::OneMotif,
        Principle::Hierarchical,
        Principle::CallResponse,
        Principle::Ostinato
    };
    for (const auto principle : principles) {
        const FormMode form = form_for_principle(principle);
        CHECK(form != FormMode::SongAAAB);
        CHECK(form_basis(form, Principle::TwoMotif) == principle);
    }
}

TEST_CASE("song form is exactly AAAB") {
    const uint8_t expected[] = {0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 8; ++i)
        CHECK(song_symbol_at(static_cast<uint8_t>(i)) == expected[i]);
}

TEST_CASE("song zones scale safely from 1 through 32 steps") {
    struct Case { int n, related, turn; };
    const Case cases[] = {
        {1, 1, 1}, {2, 1, 1}, {3, 1, 2},
        {8, 4, 6}, {12, 6, 9}, {16, 8, 12}, {32, 16, 24}
    };
    for (const auto& c : cases) {
        const auto z = song_zones(c.n);
        CHECK(z.length == c.n);
        CHECK(z.related_end == c.related);
        CHECK(z.turn_start == c.turn);
        CHECK(z.related_end >= 1);
        CHECK(z.related_end <= z.turn_start);
        CHECK(z.turn_start <= z.length);
    }
}

TEST_CASE("cell groove expands to unique absolute ranks at every supported length") {
    GrooveCell cell{};
    cell.len = 4;
    cell.rank_of_slot[0] = 0; cell.rank_of_slot[1] = 2;
    cell.rank_of_slot[2] = 1; cell.rank_of_slot[3] = 3;
    cell.note_len[0] = 1; cell.note_len[1] = 2;
    cell.note_len[2] = 3; cell.note_len[3] = 4;
    for (int n = 1; n <= 32; ++n) {
        PatternGroove groove{};
        expand_pattern_groove(cell, n, groove);
        bool seen[32] = {};
        REQUIRE(groove.len == n);
        CHECK(groove.rank_of_slot[0] == 0);
        for (int i = 0; i < n; ++i) {
            REQUIRE(groove.rank_of_slot[i] < n);
            CHECK_FALSE(seen[groove.rank_of_slot[i]]);
            seen[groove.rank_of_slot[i]] = true;
            CHECK(groove.note_len[i] >= 1);
            CHECK(groove.note_len[i] <= 4);
        }
    }
}

TEST_CASE("song turnaround derivation is deterministic and stronger at the ending") {
    const int lengths[] = {2, 3, 8, 12, 16, 32};
    for (const int length : lengths) {
        for (uint32_t seed = 1; seed <= 12; ++seed) {
            const MelodyPattern a =
                generated_pattern(length, 0xA11CEu + seed * 17u);
            MelodyPattern first{};
            MelodyPattern second{};
            uint8_t first_cadence = 0;
            uint8_t second_cadence = 0;
            float first_bound = 0.f;
            float second_bound = 0.f;
            Rng first_rng;
            Rng second_rng;
            first_rng.seed(0xBEEFu + seed * 31u);
            second_rng.seed(0xBEEFu + seed * 31u);

            derive_turnaround(a, length, first_rng, first,
                              first_cadence, first_bound);
            derive_turnaround(a, length, second_rng, second,
                              second_cadence, second_bound);

            CHECK(std::memcmp(&first, &second, sizeof(first)) == 0);
            CHECK(first_cadence == second_cadence);
            CHECK(first_bound == second_bound);
            const TurnaroundZones zones = song_zones(length);
            CHECK(turnaround_difference(a, first, zones.turn_start,
                                        zones.length) > 0);
            CHECK(zone_distance(a, first, 0, zones.related_end) <
                  zone_distance(a, first, zones.turn_start, zones.length));
            check_pattern_groove(first.pattern_groove);
            CHECK(first.pattern_groove.rank_of_slot[first_cadence] == 1);
            for (int i = 0; i < length; ++i) {
                CHECK(first.pitch[i] >= -1.f);
                CHECK(first.pitch[i] <= 1.f);
            }
        }
    }
}

TEST_CASE("song cadence binding moves only the cadence pitch once without RNG") {
    MelodyPattern a = generated_pattern(16, 0xCAFEu);
    MelodyPattern b{};
    uint8_t cadence = 0;
    float bound = 0.f;
    Rng derive_rng;
    derive_rng.seed(0x1234u);
    derive_turnaround(a, 16, derive_rng, b, cadence, bound);

    const MelodyPattern before = b;
    a.pitch[0] = a.pitch[0] < 0.5f ? 0.75f : -0.75f;
    bind_song_cadence(a, b, cadence, bound);
    CHECK(b.pitch[cadence] == doctest::Approx(
        0.5f * (before.pitch[cadence] + a.pitch[0])));
    for (int i = 0; i < 32; ++i)
        if (i != cadence) CHECK(b.pitch[i] == before.pitch[i]);
    CHECK(bound == a.pitch[0]);

    const MelodyPattern after_first_bind = b;
    bind_song_cadence(a, b, cadence, bound);
    CHECK(std::memcmp(&b, &after_first_bind, sizeof(b)) == 0);

    Rng untouched_a;
    Rng untouched_b;
    untouched_a.seed(0xFEEDu);
    untouched_b.seed(0xFEEDu);
    bind_song_cadence(a, b, cadence, bound);
    CHECK(untouched_a.next_u32() == untouched_b.next_u32());
}

TEST_CASE("pattern groove mutation is deterministic and preserves invariants") {
    const MelodyPattern base = generated_pattern(32, 0x4242u);
    PatternGroove first = base.pattern_groove;
    PatternGroove second = base.pattern_groove;
    Rng first_rng;
    Rng second_rng;
    first_rng.seed(0x5151u);
    second_rng.seed(0x5151u);

    for (int i = 0; i < 64; ++i) {
        const bool renew_side = (i & 1) != 0;
        mutate_pattern_groove(first_rng, first, 1.f, renew_side);
        mutate_pattern_groove(second_rng, second, 1.f, renew_side);
        CHECK(std::memcmp(&first, &second, sizeof(first)) == 0);
        check_pattern_groove(first);
    }
}

TEST_CASE("normal forms survive pattern snapshot migration") {
    const uint32_t seeds[] = {1u, 0xBEEFu};
    const int step_counts[] = {1, 8, 16, 32};
    const float variations[] = {-1.f, 0.f, 1.f};
    uint64_t hash = 1469598103934665603ull;

    for (int principle = 0;
         principle < static_cast<int>(Principle::kCount);
         ++principle) {
        for (const uint32_t seed : seeds) {
            for (const int steps : step_counts) {
                for (const float variation : variations) {
                    ModLane lane;
                    lane.set_melodic(true);
                    lane.set_principle(static_cast<Principle>(principle));
                    lane.init(48000.f, seed);
                    lane.set_rate_hz(120.f);
                    lane.set_shape(1.f);
                    lane.set_density(0.73f);
                    lane.set_step(true, steps);
                    lane.set_variation(variation);

                    for (int sample = 0; sample < 1600; ++sample) {
                        const float output = lane.process();
                        const float target = lane.target();
                        const uint8_t state = static_cast<uint8_t>(
                            (lane.fired() ? 1 : 0) |
                            (lane.wrapped() ? 2 : 0) |
                            (lane.gate_state() ? 4 : 0));
                        fnv_feed(hash, &output, sizeof(output));
                        fnv_feed(hash, &target, sizeof(target));
                        fnv_feed(hash, &state, sizeof(state));
                    }
                }
            }
        }
    }

    CHECK(hash == 0x68396762664c16f8ull);
}

TEST_CASE("normal FORM selection becomes observable only at a STEP wrap") {
    ModLane lane;
    lane.set_melodic(true);
    lane.set_principle(Principle::TwoMotif);
    lane.init(48000.f, 0xBEEFu);
    lane.set_rate_hz(120.f);
    lane.set_step(true, 8);
    lane.process();
    REQUIRE(lane.form() == FormMode::TwoMotifs);

    lane.set_principle(Principle::CallResponse);
    CHECK(lane.form() == FormMode::TwoMotifs);
    do {
        lane.process();
    } while (!lane.wrapped());
    CHECK(lane.form() == FormMode::CallResponse);
    CHECK(lane.last_basis() == Principle::CallResponse);
}

TEST_CASE("song storage budget") {
    constexpr size_t kLaneBytesBefore = 412u; // ARM/Daisy ABI at c3c3c70
    static_assert(sizeof(ModLane) >= kLaneBytesBefore,
                  "song-form migration unexpectedly shrank ModLane");
    constexpr size_t kAddedAcrossTwoParts =
        (sizeof(ModLane) - kLaneBytesBefore) * 10u;
    INFO("ModLane bytes after migration: " << sizeof(ModLane));
    INFO("added bytes across ten uniform lanes: " << kAddedAcrossTwoParts);
    CHECK(kAddedAcrossTwoParts < 5u * 1024u);
}
