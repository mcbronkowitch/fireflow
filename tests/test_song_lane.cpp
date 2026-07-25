#include <doctest/doctest.h>
#include "mod/lane.h"
#include <cstring>
#include <vector>

using namespace spky;

namespace {

ModLane make_song_lane(uint32_t seed, int steps = 8) {
    ModLane lane;
    lane.set_melodic(true);
    lane.set_step(true, steps);
    lane.set_last_basis(Principle::Hierarchical);
    lane.set_form(FormMode::SongAAAB);
    lane.init(48000.f, seed);
    lane.set_rate_hz(120.f);
    lane.set_shape(1.f);
    lane.set_density(1.f);
    lane.set_variation(0.f);
    return lane;
}

void drive_to_wrap(ModLane& lane) {
    for (int sample = 0; sample < 10000; ++sample) {
        lane.process();
        if (lane.wrapped()) return;
    }
    FAIL("lane did not wrap within the safety bound");
}

bool same_pattern(const MelodyPattern& first, const MelodyPattern& second) {
    return std::memcmp(&first, &second, sizeof(first)) == 0;
}

bool same_normal_content(const MelodyPattern& first,
                         const MelodyPattern& second) {
    return std::memcmp(first.pitch, second.pitch, sizeof(first.pitch)) == 0 &&
           std::memcmp(first.gate, second.gate, sizeof(first.gate)) == 0 &&
           std::memcmp(first.motif_id, second.motif_id,
                       sizeof(first.motif_id)) == 0 &&
           std::memcmp(&first.layout, &second.layout,
                       sizeof(first.layout)) == 0 &&
           std::memcmp(&first.cell_groove, &second.cell_groove,
                       sizeof(first.cell_groove)) == 0;
}

bool same_pitch_except(const MelodyPattern& first,
                       const MelodyPattern& second,
                       int except_slot) {
    for (int slot = 0; slot < 32; ++slot)
        if (slot != except_slot &&
            first.pitch[slot] != second.pitch[slot])
            return false;
    return true;
}

void drive_to_step(ModLane& lane, int wanted_step) {
    for (int sample = 0; sample < 10000; ++sample) {
        lane.process();
        if (lane.cur_step() == wanted_step) return;
    }
    FAIL("lane did not reach the requested step");
}

void check_song_groove(const PatternGroove& groove, int expected_length) {
    REQUIRE(groove.len == expected_length);
    bool seen[32] = {};
    CHECK(groove.rank_of_slot[0] == 0);
    for (int slot = 0; slot < expected_length; ++slot) {
        REQUIRE(groove.rank_of_slot[slot] < expected_length);
        CHECK_FALSE(seen[groove.rank_of_slot[slot]]);
        seen[groove.rank_of_slot[slot]] = true;
        CHECK(groove.note_len[slot] >= 1);
        CHECK(groove.note_len[slot] <= 4);
    }
}

} // namespace

TEST_CASE("song LOOP advances AAAB and persists A and B") {
    ModLane lane = make_song_lane(0xA11CEu);
    const MelodyPattern initial_a = lane.pattern_for_test(0);
    const MelodyPattern initial_b = lane.pattern_for_test(1);
    REQUIRE_FALSE(same_pattern(initial_a, initial_b));

    std::vector<uint8_t> symbols;
    symbols.push_back(lane.active_pattern());
    for (int cycle = 1; cycle < 8; ++cycle) {
        drive_to_wrap(lane);
        symbols.push_back(lane.active_pattern());
    }

    CHECK(symbols == std::vector<uint8_t>({0, 0, 0, 1, 0, 0, 0, 1}));
    CHECK(same_pattern(lane.pattern_for_test(0), initial_a));
    CHECK(same_pattern(lane.pattern_for_test(1), initial_b));
}

TEST_CASE("song pending FORM NEW and STEPS changes coalesce at the wrap") {
    ModLane with_intermediate_write = make_song_lane(0xCAFEu);
    ModLane final_write_only = make_song_lane(0xCAFEu);
    drive_to_step(with_intermediate_write, 3);
    drive_to_step(final_write_only, 3);
    const MelodyPattern before_a =
        with_intermediate_write.pattern_for_test(0);
    const MelodyPattern before_b =
        with_intermediate_write.pattern_for_test(1);
    REQUIRE(same_pattern(before_a, final_write_only.pattern_for_test(0)));
    REQUIRE(same_pattern(before_b, final_write_only.pattern_for_test(1)));
    REQUIRE(with_intermediate_write.phase() == final_write_only.phase());

    with_intermediate_write.set_form(FormMode::CallResponse);
    with_intermediate_write.set_form(FormMode::Hierarchical);
    with_intermediate_write.new_phrase();
    with_intermediate_write.set_step(true, 12);

    final_write_only.set_form(FormMode::Hierarchical);
    final_write_only.new_phrase();
    final_write_only.set_step(true, 12);

    CHECK(with_intermediate_write.form() == FormMode::SongAAAB);
    CHECK(same_pattern(with_intermediate_write.pattern_for_test(0), before_a));
    CHECK(same_pattern(with_intermediate_write.pattern_for_test(1), before_b));
    CHECK(same_pattern(with_intermediate_write.pattern_for_test(0),
                       final_write_only.pattern_for_test(0)));
    CHECK(same_pattern(with_intermediate_write.pattern_for_test(1),
                       final_write_only.pattern_for_test(1)));

    drive_to_wrap(with_intermediate_write);
    drive_to_wrap(final_write_only);
    CHECK(with_intermediate_write.form() == FormMode::Hierarchical);
    CHECK(with_intermediate_write.last_basis() == Principle::Hierarchical);
    CHECK(with_intermediate_write.active_pattern() == 0);
    CHECK(with_intermediate_write.song_position() == 0);
    CHECK(same_pattern(with_intermediate_write.pattern_for_test(0),
                       final_write_only.pattern_for_test(0)));
    CHECK(same_pattern(with_intermediate_write.pattern_for_test(1),
                       final_write_only.pattern_for_test(1)));
}

TEST_CASE("normal to SONG captures the outgoing phrase as A") {
    ModLane lane;
    lane.set_melodic(true);
    lane.set_principle(Principle::CallResponse);
    lane.set_step(true, 8);
    lane.init(48000.f, 0xF00Du);
    lane.set_rate_hz(120.f);
    lane.set_shape(1.f);
    lane.set_density(1.f);
    const MelodyPattern normal = lane.pattern_for_test(0);
    drive_to_step(lane, 3);

    lane.set_form(FormMode::SongAAAB);
    CHECK(lane.form() == FormMode::CallResponse);
    drive_to_wrap(lane);

    CHECK(lane.form() == FormMode::SongAAAB);
    CHECK(lane.active_pattern() == 0);
    CHECK(lane.song_position() == 0);
    CHECK(same_normal_content(lane.pattern_for_test(0), normal));
    CHECK_FALSE(same_pattern(lane.pattern_for_test(0),
                             lane.pattern_for_test(1)));
    check_song_groove(lane.pattern_for_test(0).pattern_groove, 8);
    check_song_groove(lane.pattern_for_test(1).pattern_groove, 8);
}

TEST_CASE("SONG NEW rebuilds A and B from the remembered basis") {
    ModLane lane = make_song_lane(0x5151u);
    const MelodyPattern old_a = lane.pattern_for_test(0);
    const MelodyPattern old_b = lane.pattern_for_test(1);
    drive_to_step(lane, 2);

    lane.set_last_basis(Principle::Ostinato);
    lane.new_phrase();
    drive_to_wrap(lane);

    CHECK(lane.form() == FormMode::SongAAAB);
    CHECK(lane.last_basis() == Principle::Ostinato);
    CHECK(lane.song_position() == 0);
    CHECK(lane.active_pattern() == 0);
    CHECK_FALSE(same_pattern(lane.pattern_for_test(0), old_a));
    CHECK_FALSE(same_pattern(lane.pattern_for_test(1), old_b));
}

TEST_CASE("leaving SONG generates the selected normal form at the wrap") {
    ModLane lane = make_song_lane(0x7777u);
    const MelodyPattern song_a = lane.pattern_for_test(0);
    drive_to_step(lane, 4);

    lane.set_form(FormMode::Ostinato);
    CHECK(lane.form() == FormMode::SongAAAB);
    drive_to_wrap(lane);

    CHECK(lane.form() == FormMode::Ostinato);
    CHECK(lane.last_basis() == Principle::Ostinato);
    CHECK(lane.active_pattern() == 0);
    CHECK(lane.song_position() == 0);
    CHECK_FALSE(same_pattern(lane.pattern_for_test(0), song_a));
}

TEST_CASE("FLOW pauses SONG position and both snapshots") {
    ModLane lane = make_song_lane(0xD00Du);
    drive_to_wrap(lane);
    const uint8_t paused_position = lane.song_position();
    const MelodyPattern paused_a = lane.pattern_for_test(0);
    const MelodyPattern paused_b = lane.pattern_for_test(1);

    lane.set_variation(1.f);
    lane.set_step(false, 8);
    for (int cycle = 0; cycle < 3; ++cycle)
        drive_to_wrap(lane);

    CHECK(lane.song_position() == paused_position);
    CHECK(same_pattern(lane.pattern_for_test(0), paused_a));
    CHECK(same_pattern(lane.pattern_for_test(1), paused_b));

    lane.set_step(true, 8);
    CHECK(lane.song_position() == paused_position);
    drive_to_wrap(lane);
    CHECK(lane.song_position() ==
          static_cast<uint8_t>((paused_position + 1u) & 3u));
}

TEST_CASE("song supports every short and portable engine length") {
    const int lengths[] = {1, 2, 3, 12, 32};
    for (const int length : lengths) {
        ModLane lane = make_song_lane(0x1000u + length, length);
        check_song_groove(lane.pattern_for_test(0).pattern_groove, length);
        check_song_groove(lane.pattern_for_test(1).pattern_groove, length);
        CHECK(lane.cadence_slot_for_test() == length - 1);
        CHECK(lane.pattern_for_test(1).pattern_groove.rank_of_slot[
                  lane.cadence_slot_for_test()] == (length > 1 ? 1 : 0));
        for (int cycle = 0; cycle < 4; ++cycle)
            drive_to_wrap(lane);
    }
}

TEST_CASE("pre-roll FORM selection applies before the first audible STEP") {
    ModLane lane;
    lane.set_melodic(true);
    lane.init(48000.f, 0xABCDu);
    lane.set_rate_hz(120.f);
    lane.set_form(FormMode::CallResponse);
    lane.set_step(true, 8);

    CHECK(lane.form() == FormMode::SongAAAB);
    lane.process();
    CHECK(lane.form() == FormMode::CallResponse);
    CHECK(lane.cur_step() == 0);
    CHECK(lane.fired());
}

TEST_CASE("steps changes above 32 do not rebuild SONG snapshots") {
    ModLane lane = make_song_lane(0x3232u, 40);
    const MelodyPattern initial_a = lane.pattern_for_test(0);
    const MelodyPattern initial_b = lane.pattern_for_test(1);
    lane.set_step(true, 64);
    drive_to_wrap(lane);
    CHECK(same_pattern(lane.pattern_for_test(0), initial_a));
    CHECK(same_pattern(lane.pattern_for_test(1), initial_b));
}

TEST_CASE("song GROW mutates only the outgoing snapshot") {
    ModLane lane = make_song_lane(0x600Du);
    lane.set_variation(1.f);
    const MelodyPattern initial_a = lane.pattern_for_test(0);
    const MelodyPattern initial_b = lane.pattern_for_test(1);

    drive_to_wrap(lane);
    CHECK_FALSE(same_pattern(lane.pattern_for_test(0), initial_a));
    CHECK(same_pattern(lane.pattern_for_test(1), initial_b));

    drive_to_wrap(lane);
    drive_to_wrap(lane);
    REQUIRE(lane.active_pattern() == 1);
    const MelodyPattern a_before_b = lane.pattern_for_test(0);
    const MelodyPattern b_before_play = lane.pattern_for_test(1);
    drive_to_wrap(lane);
    CHECK(same_pattern(lane.pattern_for_test(0), a_before_b));
    CHECK_FALSE(same_pattern(lane.pattern_for_test(1), b_before_play));
}

TEST_CASE("song RENEW mutates only the outgoing snapshot and never rederives B") {
    ModLane lane = make_song_lane(0x700Du);
    lane.set_variation(-1.f);
    const MelodyPattern initial_a = lane.pattern_for_test(0);
    const MelodyPattern initial_b = lane.pattern_for_test(1);

    drive_to_wrap(lane);
    CHECK_FALSE(same_pattern(lane.pattern_for_test(0), initial_a));
    CHECK(same_pattern(lane.pattern_for_test(1), initial_b));

    drive_to_wrap(lane);
    drive_to_wrap(lane);
    REQUIRE(lane.active_pattern() == 1);
    const int cadence = lane.cadence_slot_for_test();
    CHECK(same_pitch_except(lane.pattern_for_test(1), initial_b, cadence));
    const MelodyPattern a_before_b = lane.pattern_for_test(0);
    const MelodyPattern b_before_play = lane.pattern_for_test(1);
    drive_to_wrap(lane);
    CHECK(same_pattern(lane.pattern_for_test(0), a_before_b));
    CHECK_FALSE(same_pattern(lane.pattern_for_test(1), b_before_play));
    CHECK_FALSE(same_pattern(lane.pattern_for_test(0),
                             lane.pattern_for_test(1)));
}

TEST_CASE("song cadence moves exactly halfway once before B") {
    ModLane lane = make_song_lane(0xCADAu);
    const MelodyPattern initial_b = lane.pattern_for_test(1);
    const int cadence = lane.cadence_slot_for_test();
    lane.set_variation(1.f);

    drive_to_wrap(lane);
    drive_to_wrap(lane);
    // Freeze the third A pass so selecting B performs only the cadence bind;
    // otherwise GROW legitimately mutates incoming B slot 0 on its boundary.
    lane.set_variation(0.f);
    drive_to_wrap(lane);
    REQUIRE(lane.active_pattern() == 1);
    const MelodyPattern bound_b = lane.pattern_for_test(1);
    const float a_opening = lane.pattern_for_test(0).pitch[0];
    CHECK(bound_b.pitch[cadence] == doctest::Approx(
        0.5f * (initial_b.pitch[cadence] + a_opening)));
    CHECK(same_pitch_except(bound_b, initial_b, cadence));
    CHECK(lane.bound_a_opening_for_test() == a_opening);

    const MelodyPattern before_extra_samples = lane.pattern_for_test(1);
    for (int sample = 0; sample < 25; ++sample)
        lane.process();
    CHECK(same_pattern(lane.pattern_for_test(1), before_extra_samples));
}

TEST_CASE("song LOOP playback consumes no RNG before future GROW") {
    ModLane playback = make_song_lane(0x123456u);
    ModLane paused = make_song_lane(0x123456u);
    paused.set_step(false, 8);

    for (int cycle = 0; cycle < 16; ++cycle) {
        drive_to_wrap(playback);
        drive_to_wrap(paused);
    }
    REQUIRE(playback.song_position() == 0);
    REQUIRE(paused.song_position() == 0);
    REQUIRE(same_pattern(playback.pattern_for_test(0),
                         paused.pattern_for_test(0)));
    REQUIRE(same_pattern(playback.pattern_for_test(1),
                         paused.pattern_for_test(1)));

    paused.set_step(true, 8);
    playback.reset(0.f);
    paused.reset(0.f);
    playback.set_variation(1.f);
    paused.set_variation(1.f);
    drive_to_wrap(playback);
    drive_to_wrap(paused);

    CHECK(same_pattern(playback.pattern_for_test(0),
                       paused.pattern_for_test(0)));
    CHECK(same_pattern(playback.pattern_for_test(1),
                       paused.pattern_for_test(1)));
}
