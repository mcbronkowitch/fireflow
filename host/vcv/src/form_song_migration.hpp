#pragma once

#include <cstdint>

namespace spkyvcv {

struct FormSongMigration {
    int form = 2;
    int song = 0;
};

inline bool is_modern_form_song_version(bool marker_is_integer,
                                        std::int64_t marker_value) {
    return marker_is_integer && marker_value >= 1;
}

inline FormSongMigration migrate_legacy_form_song(
    bool last_basis_source_present,
    bool last_basis_is_integer,
    std::int64_t last_basis,
    bool principle_is_integer,
    std::int64_t principle) {
    std::int64_t form = 2;
    if (last_basis_source_present) {
        if (last_basis_is_integer)
            form = last_basis;
    } else if (principle_is_integer) {
        form = principle;
    }

    if (form < 0) form = 0;
    if (form > 4) form = 4;
    return {static_cast<int>(form), 0};
}

} // namespace spkyvcv
