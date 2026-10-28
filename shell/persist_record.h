#pragma once

// Panel-state persistence (spec 2026-10-28): the record that is written to
// the QSPI parameter sectors, its CRC-32, and the A/B slot scan that finds the
// newest valid record after power-up and the slot the next save goes to.
//
// Two 4 KiB sectors, seven 576-byte slots each, appended in order. A save is a
// program of one fresh slot; the other sector is only erased when the next
// roll comes back to it, so a valid record always exists across a power cut.
//
// Pure data logic, no hardware type, host tested (tests/test_persist_record.cpp).
//
// Spec: ../docs/superpowers/specs/2026-10-28-panel-state-persistence-design.md
#include <cstdint>
#include "control/params.hpp"

namespace shell {

inline constexpr uint32_t kPersistMagic      = 0x46465053u; // "SPFF"
inline constexpr uint32_t kSnapshotVersion   = 1;           // bump on a meaning change the hash cannot see
inline constexpr uint32_t kPersistSectorSize = 4096;
inline constexpr uint32_t kPersistStride     = 576;         // 7 slots per sector
inline constexpr int      kPersistSlots      = 7;

static_assert(kPersistSlots * kPersistStride <= kPersistSectorSize,
              "the slots must fit in one sector");

struct PersistLayout { uint32_t num_params, hash, version; };

struct PersistRecord {          // POD, the exact bytes written to flash
    uint32_t magic;
    PersistLayout layout;
    uint32_t seq;
    float    knobs[ffctl::NUM_PARAMS];
    uint8_t  excite[2];
    uint8_t  pad[2];
    uint32_t crc;               // CRC-32 (poly 0xEDB88320) over all bytes before it
};
static_assert(sizeof(PersistRecord) <= kPersistStride);

uint32_t crc32(const uint8_t* p, uint32_t n);

// Sets magic and crc; the caller fills everything else first.
void record_seal(PersistRecord& r);

// Magic, CRC and layout all match.
bool record_valid(const PersistRecord& r, const PersistLayout& want);

// Newest valid record across both sectors; sector -1 = none.
struct SlotPick { int sector; int slot; uint32_t seq; };
SlotPick pick_latest(const uint8_t* sec0, const uint8_t* sec1, const PersistLayout& want);

// Where the next save goes. Appends to the first all-0xFF slot after `latest`
// in its sector; a full sector rolls to slot 0 of the other one (erase_first
// when that slot is not blank); with no record at all it is sector 0, slot 0.
struct SlotNext { int sector; int slot; bool erase_first; };
SlotNext next_slot(const uint8_t* sec0, const uint8_t* sec1, const SlotPick& latest);

// FNV-1a style fold of the three count constants onto the generated hash.
// kParamLayoutHash hashes their symbolic upper bounds (e.g.
// "float(spky::SCALE_LIST_COUNT - 1)"), not their values; this adds the values.
constexpr uint32_t layout_hash_mix(uint32_t base, uint32_t scale_count,
                                   uint32_t song_count, uint32_t fluxrate_count)
{
    uint32_t h = base;
    const uint32_t v[3] = {scale_count, song_count, fluxrate_count};
    for (uint32_t x : v)
        for (int i = 0; i < 4; ++i) {
            h ^= (x >> (8 * i)) & 0xFFu;
            h *= 0x01000193u;
        }
    return h;
}

// {NUM_PARAMS, layout_hash_mix(kParamLayoutHash, counts), kSnapshotVersion}
PersistLayout current_layout();

} // namespace shell
