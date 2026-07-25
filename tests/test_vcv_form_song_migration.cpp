#include <doctest/doctest.h>
#include "vcv/src/form_song_migration.hpp"

using namespace spkyvcv;

TEST_CASE("VCV FORM SONG marker accepts only integer versions at least one") {
    CHECK_FALSE(is_modern_form_song_version(false, 1));
    CHECK_FALSE(is_modern_form_song_version(true, -1));
    CHECK_FALSE(is_modern_form_song_version(true, 0));
    CHECK(is_modern_form_song_version(true, 1));
    CHECK(is_modern_form_song_version(true, 99));
}

TEST_CASE("VCV legacy FORM SONG migration preserves basis priority and clamps") {
    auto migrated = migrate_legacy_form_song(true, true, 3, true, 1);
    CHECK(migrated.form == 3);
    CHECK(migrated.song == 0);

    migrated = migrate_legacy_form_song(false, false, 0, true, 1);
    CHECK(migrated.form == 1);
    CHECK(migrated.song == 0);

    migrated = migrate_legacy_form_song(true, true, -20, true, 1);
    CHECK(migrated.form == 0);

    migrated = migrate_legacy_form_song(true, true, 5000000000LL, false, 0);
    CHECK(migrated.form == 4);
}

TEST_CASE("VCV malformed or missing legacy FORM values use HIERARCHICAL") {
    auto migrated = migrate_legacy_form_song(false, false, 4, false, 0);
    CHECK(migrated.form == 2);
    CHECK(migrated.song == 0);

    migrated = migrate_legacy_form_song(true, false, 0, true, 1);
    CHECK(migrated.form == 2);
    CHECK(migrated.song == 0);
}
