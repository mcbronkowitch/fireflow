#include <doctest/doctest.h>
#include "vcv/src/form_song_migration.hpp"
#include "vcv/src/link_migration.hpp"

using namespace spkyvcv;

TEST_CASE("VCV LINK migration preserves old THIN and drops old DRAG") {
    CHECK(migrate_legacy_link(-1.f) == doctest::Approx(1.f));
    CHECK(migrate_legacy_link(-0.37f) == doctest::Approx(0.37f));
    CHECK(migrate_legacy_link(0.f) == 0.f);
    CHECK(migrate_legacy_link(0.42f) == 0.f);
    CHECK(migrate_legacy_link(2.f) == 0.f);
}

TEST_CASE("VCV LINK has an independent schema marker") {
    CHECK(is_modern_link_version(true, 1));
    CHECK(is_modern_link_version(true, 2));
    CHECK_FALSE(is_modern_link_version(false, 1));
    CHECK_FALSE(is_modern_link_version(true, 0));
}

TEST_CASE("a patch with no version keys requests both independent migrations") {
    CHECK_FALSE(is_modern_form_song_version(false, 0));
    CHECK_FALSE(is_modern_link_version(false, 0));
    const auto form_song = migrate_legacy_form_song(false, false, 0, false, 0);
    CHECK(form_song.form == 2);

    CHECK(form_song.song == 0);
    CHECK(migrate_legacy_link(-0.6f) == doctest::Approx(0.6f));
}
