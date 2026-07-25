#pragma once

#include <cstdint>
#include "mod/phrase_gen.h"

namespace spky {

enum class FormMode : uint8_t {
    SongAAAB = 0,
    TwoMotifs,
    OnePlusVar,
    Hierarchical,
    CallResponse,
    Ostinato,
    kCount
};

inline FormMode clamp_form(int value) {
    if (value < 0) value = 0;
    const int last = static_cast<int>(FormMode::kCount) - 1;
    if (value > last) value = last;
    return static_cast<FormMode>(value);
}

inline Principle form_basis(FormMode form, Principle fallback) {
    switch (form) {
    case FormMode::TwoMotifs:    return Principle::TwoMotif;
    case FormMode::OnePlusVar:   return Principle::OneMotif;
    case FormMode::Hierarchical: return Principle::Hierarchical;
    case FormMode::CallResponse: return Principle::CallResponse;
    case FormMode::Ostinato:     return Principle::Ostinato;
    default:                     return fallback;
    }
}

inline FormMode form_for_principle(Principle principle) {
    switch (principle) {
    case Principle::TwoMotif:     return FormMode::TwoMotifs;
    case Principle::OneMotif:     return FormMode::OnePlusVar;
    case Principle::Hierarchical: return FormMode::Hierarchical;
    case Principle::CallResponse: return FormMode::CallResponse;
    case Principle::Ostinato:     return FormMode::Ostinato;
    default:                      return FormMode::Ostinato;
    }
}

struct PatternGroove {
    uint8_t rank_of_slot[32] = {};
    uint8_t note_len[32] = {};
    uint8_t len = 0;
};

struct MelodyPattern {
    float pitch[32] = {};
    bool gate[32] = {};
    uint8_t motif_id[32] = {};
    PhraseLayout layout{};
    GrooveCell cell_groove{};
    PatternGroove pattern_groove{};
};

struct TurnaroundZones {
    int related_end;
    int turn_start;
    int length;
};

struct SongForm {
    MelodyPattern patterns[2] = {};
    uint8_t form_position = 0;
    uint8_t active_pattern = 0;
    FormMode selected_form = FormMode::SongAAAB;
    FormMode pending_form = FormMode::SongAAAB;
    Principle last_basis = Principle::Hierarchical;
    bool form_pending = false;
    bool new_pending = false;
    bool length_pending = false;
    uint8_t cadence_slot = 0;
    float bound_a_opening = 0.f;
};

inline TurnaroundZones song_zones(int steps) {
    int length = steps;
    if (length < 1) length = 1;
    if (length > 32) length = 32;
    int related_end = length / 2;
    if (related_end < 1) related_end = 1;
    int turn_start = (3 * length) / 4;
    if (turn_start < related_end) turn_start = related_end;
    return {related_end, turn_start, length};
}

inline uint8_t song_symbol_at(uint8_t form_position) {
    static constexpr uint8_t symbols[4] = {0, 0, 0, 1};
    return symbols[form_position & 3u];
}

inline void expand_pattern_groove(const GrooveCell& cell, int steps,
                                  PatternGroove& out) {
    int length = steps;
    if (length < 1) length = 1;
    if (length > 32) length = 32;
    int cell_length = static_cast<int>(cell.len);
    if (cell_length < 1) cell_length = 1;
    if (cell_length > 32) cell_length = 32;

    out = {};
    out.len = static_cast<uint8_t>(length);

    uint8_t order[32];
    for (int slot = 0; slot < length; ++slot) {
        order[slot] = static_cast<uint8_t>(slot);
        int note_length = static_cast<int>(cell.note_len[slot % cell_length]);
        if (note_length < 1) note_length = 1;
        if (note_length > 4) note_length = 4;
        out.note_len[slot] = static_cast<uint8_t>(note_length);
    }

    for (int i = 1; i < length; ++i) {
        const uint8_t candidate = order[i];
        const int candidate_cell = candidate % cell_length;
        const int candidate_occurrence = candidate / cell_length;
        int j = i - 1;
        while (j >= 0) {
            const int prior_cell = order[j] % cell_length;
            const int prior_occurrence = order[j] / cell_length;
            const uint8_t candidate_rank = cell.rank_of_slot[candidate_cell];
            const uint8_t prior_rank = cell.rank_of_slot[prior_cell];
            const bool candidate_first =
                candidate_rank < prior_rank ||
                (candidate_rank == prior_rank &&
                 candidate_occurrence < prior_occurrence);
            if (!candidate_first) break;
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = candidate;
    }

    for (int rank = 0; rank < length; ++rank)
        out.rank_of_slot[order[rank]] = static_cast<uint8_t>(rank);

    if (out.rank_of_slot[0] != 0) {
        const uint8_t old_rank = out.rank_of_slot[0];
        for (int slot = 1; slot < length; ++slot) {
            if (out.rank_of_slot[slot] == 0) {
                out.rank_of_slot[slot] = old_rank;
                break;
            }
        }
        out.rank_of_slot[0] = 0;
    }
}

inline void sf_clamp_note_length(uint8_t& note_length, int delta) {
    int value = static_cast<int>(note_length) + delta;
    if (value < 1) value = 1;
    if (value > 4) value = 4;
    note_length = static_cast<uint8_t>(value);
}

inline void sf_put_rank_at_slot(PatternGroove& groove, int slot,
                                uint8_t wanted_rank) {
    const uint8_t displaced_rank = groove.rank_of_slot[slot];
    if (displaced_rank == wanted_rank) return;
    for (int i = 0; i < groove.len; ++i) {
        if (groove.rank_of_slot[i] == wanted_rank) {
            groove.rank_of_slot[i] = displaced_rank;
            groove.rank_of_slot[slot] = wanted_rank;
            return;
        }
    }
}

inline void bind_song_cadence(const MelodyPattern& a, MelodyPattern& b,
                              uint8_t cadence_slot,
                              float& bound_a_opening) {
    if (bound_a_opening == a.pitch[0]) return;
    const int slot = cadence_slot < 32 ? cadence_slot : 31;
    b.pitch[slot] = 0.5f * (b.pitch[slot] + a.pitch[0]);
    bound_a_opening = a.pitch[0];
}

inline void derive_turnaround(const MelodyPattern& a, int steps, Rng& rng,
                              MelodyPattern& b, uint8_t& cadence_slot,
                              float& bound_a_opening) {
    b = a;
    const TurnaroundZones zones = song_zones(steps);
    const int length = zones.length;
    bool related_rank_swapped = false;

    for (int slot = 0; slot < zones.turn_start; ++slot) {
        const float pitch_decision = rng.next_unipolar();
        const float pitch_amount = rng.next_bipolar();
        const float rank_decision = rng.next_unipolar();
        const float length_decision = rng.next_unipolar();
        if (slot == 0) continue;

        const bool related = slot < zones.related_end;
        const float pitch_probability = related ? 0.20f : 0.60f;
        const float pitch_width = related ? 0.12f : 0.35f;
        if (pitch_decision < pitch_probability)
            b.pitch[slot] = pg_clampf(
                b.pitch[slot] + pitch_amount * pitch_width, -1.f, 1.f);

        const float rank_probability = related ? 0.20f : 0.45f;
        const int zone_end = related ? zones.related_end : zones.turn_start;
        if (rank_decision < rank_probability && slot + 1 < zone_end &&
            (!related || !related_rank_swapped)) {
            const uint8_t rank = b.pattern_groove.rank_of_slot[slot];
            b.pattern_groove.rank_of_slot[slot] =
                b.pattern_groove.rank_of_slot[slot + 1];
            b.pattern_groove.rank_of_slot[slot + 1] = rank;
            if (related) related_rank_swapped = true;
        }

        const float length_probability = related ? 0.15f : 0.40f;
        if (length_decision < length_probability) {
            const int delta =
                length_decision < 0.5f * length_probability ? -1 : 1;
            sf_clamp_note_length(
                b.pattern_groove.note_len[slot], delta);
        }
    }

    const int turnaround_length = length - zones.turn_start;
    float contour[32] = {};
    const float contour_start = zones.turn_start > 0
        ? b.pitch[zones.turn_start - 1] : a.pitch[0];
    pg_contour_walk(rng, contour, turnaround_length,
                    contour_start, 0.85f, 0.08f);
    for (int i = 0; i < turnaround_length; ++i)
        b.pitch[zones.turn_start + i] = contour[i];

    uint8_t available_ranks[32] = {};
    uint8_t random_order[32] = {};
    float rank_score[32] = {};
    for (int i = 0; i < turnaround_length; ++i) {
        const int slot = zones.turn_start + i;
        available_ranks[i] = b.pattern_groove.rank_of_slot[slot];
        random_order[i] = static_cast<uint8_t>(i);
        rank_score[i] = rng.next_unipolar();
    }
    for (int i = 1; i < turnaround_length; ++i) {
        const uint8_t rank = available_ranks[i];
        int j = i - 1;
        while (j >= 0 && available_ranks[j] > rank) {
            available_ranks[j + 1] = available_ranks[j];
            --j;
        }
        available_ranks[j + 1] = rank;
    }
    for (int i = 1; i < turnaround_length; ++i) {
        const uint8_t candidate = random_order[i];
        int j = i - 1;
        while (j >= 0 &&
               rank_score[random_order[j]] < rank_score[candidate]) {
            random_order[j + 1] = random_order[j];
            --j;
        }
        random_order[j + 1] = candidate;
    }
    for (int rank_index = 0; rank_index < turnaround_length; ++rank_index) {
        const int local_slot = random_order[rank_index];
        b.pattern_groove.rank_of_slot[zones.turn_start + local_slot] =
            available_ranks[rank_index];
    }

    for (int i = 0; i < turnaround_length; ++i) {
        const float draw = rng.next_unipolar();
        int note_length = 1 + static_cast<int>(draw * 4.f);
        if (note_length > 4) note_length = 4;
        b.pattern_groove.note_len[zones.turn_start + i] =
            static_cast<uint8_t>(note_length);
    }

    cadence_slot = static_cast<uint8_t>(length - 1);
    sf_put_rank_at_slot(b.pattern_groove, 0, 0);
    if (length > 1)
        sf_put_rank_at_slot(b.pattern_groove, cadence_slot, 1);

    b.pitch[cadence_slot] =
        0.5f * (b.pitch[cadence_slot] + a.pitch[0]);
    bound_a_opening = a.pitch[0];

    bool turnaround_matches = true;
    for (int slot = zones.turn_start; slot < length; ++slot) {
        if (b.pitch[slot] != a.pitch[slot] ||
            b.pattern_groove.rank_of_slot[slot] !=
                a.pattern_groove.rank_of_slot[slot] ||
            b.pattern_groove.note_len[slot] !=
                a.pattern_groove.note_len[slot]) {
            turnaround_matches = false;
            break;
        }
    }
    if (length > 1 && turnaround_matches) {
        const float direction = a.pitch[cadence_slot] <= 0.f ? 0.125f : -0.125f;
        b.pitch[cadence_slot] =
            pg_clampf(a.pitch[cadence_slot] + direction, -1.f, 1.f);
    }
}

inline void sf_pattern_nudge_length(Rng& rng, PatternGroove& groove) {
    const int length = groove.len < 1 ? 1 : groove.len;
    int slot = static_cast<int>(
        rng.next_unipolar() * static_cast<float>(length));
    if (slot >= length) slot = length - 1;
    const int delta = rng.next_unipolar() < 0.5f ? -1 : 1;
    sf_clamp_note_length(groove.note_len[slot], delta);
}

inline void sf_pattern_mutate_grow(Rng& rng, PatternGroove& groove) {
    const int length = groove.len;
    if (rng.next_unipolar() < 0.5f) {
        if (length < 3) return;
        int rank = 1 + static_cast<int>(
            rng.next_unipolar() * static_cast<float>(length - 2));
        if (rank > length - 2) rank = length - 2;
        int first = -1;
        int second = -1;
        for (int slot = 0; slot < length; ++slot) {
            if (groove.rank_of_slot[slot] == rank) first = slot;
            if (groove.rank_of_slot[slot] == rank + 1) second = slot;
        }
        if (first >= 0 && second >= 0) {
            groove.rank_of_slot[first] = static_cast<uint8_t>(rank + 1);
            groove.rank_of_slot[second] = static_cast<uint8_t>(rank);
        }
    } else {
        sf_pattern_nudge_length(rng, groove);
    }
}

inline void sf_pattern_reroll(Rng& rng, PatternGroove& groove) {
    const int length = groove.len;
    for (int slot = 0; slot < length; ++slot) {
        groove.rank_of_slot[slot] = static_cast<uint8_t>(slot);
        const float draw = rng.next_unipolar();
        int note_length = 1 + static_cast<int>(draw * 4.f);
        if (note_length > 4) note_length = 4;
        groove.note_len[slot] = static_cast<uint8_t>(note_length);
    }
    for (int rank = length - 1; rank > 1; --rank) {
        int other = 1 + static_cast<int>(
            rng.next_unipolar() * static_cast<float>(rank));
        if (other > rank) other = rank;
        const uint8_t tmp = groove.rank_of_slot[rank];
        groove.rank_of_slot[rank] = groove.rank_of_slot[other];
        groove.rank_of_slot[other] = tmp;
    }
    sf_put_rank_at_slot(groove, 0, 0);
}

inline void sf_pattern_mutate_renew(Rng& rng, PatternGroove& groove,
                                    bool reroll) {
    if (reroll) {
        sf_pattern_reroll(rng, groove);
        return;
    }
    const int length = groove.len;
    if (rng.next_unipolar() < 0.7f) {
        const int candidate_count = (length - 1) / 2;
        if (candidate_count < 1) return;
        int candidate = static_cast<int>(
            rng.next_unipolar() * static_cast<float>(candidate_count));
        if (candidate >= candidate_count) candidate = candidate_count - 1;
        const int even_slot = 2 + 2 * candidate;
        const uint8_t rank = groove.rank_of_slot[even_slot - 1];
        groove.rank_of_slot[even_slot - 1] =
            groove.rank_of_slot[even_slot];
        groove.rank_of_slot[even_slot] = rank;
    } else {
        sf_pattern_nudge_length(rng, groove);
    }
}

inline void mutate_pattern_groove(Rng& rng, PatternGroove& groove,
                                  float variation, bool renew_side) {
    float amount = variation < 0.f ? -variation : variation;
    if (amount > 1.f) amount = 1.f;
    float zone = (amount - 0.25f) / 0.75f;
    if (zone < 0.f) zone = 0.f;
    if (rng.next_unipolar() >= zone * zone) return;

    if (renew_side) {
        const bool reroll =
            amount >= 0.9f && rng.next_unipolar() < 0.25f;
        sf_pattern_mutate_renew(rng, groove, reroll);
    } else {
        sf_pattern_mutate_grow(rng, groove);
    }
    sf_put_rank_at_slot(groove, 0, 0);
}

} // namespace spky
