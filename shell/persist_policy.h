#pragma once

// Panel-state persistence (spec 2026-10-28): when a save is due, and how the
// boot vector is built from the init patch plus the stored record.
//
// Pure data logic, no hardware type, host tested (tests/test_persist_policy.cpp).
//
// Spec: ../docs/superpowers/specs/2026-10-28-panel-state-persistence-design.md
#include <cstdint>
#include "persist_record.h"

namespace shell {

inline constexpr uint32_t kSaveQuietMs = 15000;   // no change for this long -> save
inline constexpr uint32_t kSaveFloorMs = 300000;  // at most one save per this

// True when the two vectors differ in anything that is persisted: REC_A, REC_B
// and MODBTN are never restored (spec 4.3), so a change there is not one.
bool persist_differs(const float* a, const float* b);

struct SavePolicy {
    uint32_t last_change_ms = 0;
    uint32_t last_save_ms   = 0;
    bool     saved_once     = false;
    bool     dirty          = false;
};

// Called once per foreground pass. `changed` = the published vector differs
// (persist_differs) from the previous pass's vector. True = write now.
bool save_due(SavePolicy& s, bool changed, uint32_t now_ms);
void save_done(SavePolicy& s, uint32_t now_ms);
// A save that did not verify: the 5-min floor applies from now as after a
// good one (no slot burnt every pass), but the vector stays dirty, so the
// same state is tried again once the floor is up -- without a new change.
void save_failed(SavePolicy& s, uint32_t now_ms);

// Fills knobs from the init patch, overlays rec (if non-null) and applies the
// spec 4.3 overrides. excite_out gets the record's excite bits (false without one).
void boot_overlay(const PersistRecord* rec, float* knobs, bool excite_out[2]);

// After the boot take: excite only where the deck's ENGINE is BBD. A deck
// that was on BBD before the take (the record's engine) keeps the recorded
// bit; a deck the take turned onto BBD (the pot moved while off) is excited,
// as a live turn onto BBD would excite it -- FLUX still stays at its pot,
// since the take fires no bbd_edge. was_bbd[p]: deck p's ENGINE knob was on
// BBD before the take wrote.
void excite_after_take(const bool rec_excite[2], const bool was_bbd[2], const float* knobs,
                       bool excite_out[2]);

} // namespace shell
