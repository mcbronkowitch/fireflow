// Panel-state persistence: record format, CRC-32 and the A/B slot scan.
// Pure data logic, no hardware type.
// Spec: docs/superpowers/specs/2026-10-28-panel-state-persistence-design.md
// sections 4.1-4.2, 5.2.
#include <doctest/doctest.h>
#include <cstdint>
#include <cstring>
#include "../shell/persist_record.h"

using namespace shell;

namespace {

struct Flash {
    uint8_t sec[2][kPersistSectorSize];
    Flash() { std::memset(sec, 0xFF, sizeof sec); }
};

PersistLayout lay() { return PersistLayout{ffctl::NUM_PARAMS, 0x1234ABCDu, 1}; }

PersistRecord make(uint32_t seq, const PersistLayout& l, float fill = 0.25f) {
    PersistRecord r;
    std::memset(&r, 0, sizeof r);
    r.layout = l;
    r.seq    = seq;
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) r.knobs[i] = fill + float(i) * 0.001f;
    r.excite[0] = 3;
    r.excite[1] = 4;
    record_seal(r);
    return r;
}

void put(Flash& f, int sector, int slot, const PersistRecord& r) {
    std::memcpy(&f.sec[sector][slot * kPersistStride], &r, sizeof r);
}

} // namespace

TEST_CASE("crc32: check value") {
    const char* s = "123456789";
    CHECK(crc32(reinterpret_cast<const uint8_t*>(s), 9) == 0xCBF43926u);
}

TEST_CASE("record: sealed record is valid") {
    PersistRecord r = make(1, lay());
    CHECK(r.magic == kPersistMagic);
    CHECK(record_valid(r, lay()));
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) {
        PersistRecord b = r;
        reinterpret_cast<uint8_t*>(&b.knobs[i])[1] ^= 0x01;
        CHECK_FALSE(record_valid(b, lay()));
    }
}

TEST_CASE("record: layout mismatch is invalid") {
    const PersistRecord r = make(1, lay());
    PersistLayout w = lay();
    w.num_params += 1;
    CHECK_FALSE(record_valid(r, w));
    w = lay();
    w.hash += 1;
    CHECK_FALSE(record_valid(r, w));
    w = lay();
    w.version += 1;
    CHECK_FALSE(record_valid(r, w));
}

TEST_CASE("record: all-0xFF and random bytes yield no record") {
    Flash f;
    CHECK(pick_latest(f.sec[0], f.sec[1], lay()).sector == -1);

    uint32_t x = 0x9E3779B9u;
    for (auto& s : f.sec)
        for (auto& b : s) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            b = uint8_t(x);
        }
    CHECK(pick_latest(f.sec[0], f.sec[1], lay()).sector == -1);
}

TEST_CASE("record: highest seq wins across sectors") {
    Flash f;
    put(f, 0, 6, make(5, lay()));
    put(f, 1, 0, make(6, lay()));
    const SlotPick p = pick_latest(f.sec[0], f.sec[1], lay());
    CHECK(p.sector == 1);
    CHECK(p.slot == 0);
    CHECK(p.seq == 6);
}

TEST_CASE("record: torn last record falls back to the previous one") {
    Flash f;
    put(f, 0, 0, make(3, lay()));
    put(f, 0, 1, make(4, lay()));
    PersistRecord torn = make(5, lay());
    torn.crc ^= 1u;
    put(f, 0, 2, torn);
    const SlotPick p = pick_latest(f.sec[0], f.sec[1], lay());
    CHECK(p.sector == 0);
    CHECK(p.slot == 1);
    CHECK(p.seq == 4);
}

TEST_CASE("record: erased new sector with old sector intact") {
    Flash f;
    for (int i = 0; i < kPersistSlots; ++i) put(f, 0, i, make(1 + i, lay()));
    const SlotPick p = pick_latest(f.sec[0], f.sec[1], lay());
    CHECK(p.sector == 0);
    CHECK(p.slot == 6);
    CHECK(p.seq == 7);
    const SlotNext n = next_slot(f.sec[0], f.sec[1], p);
    CHECK(n.sector == 1);
    CHECK(n.slot == 0);
    CHECK_FALSE(n.erase_first);
}

TEST_CASE("next_slot: appends in the active sector") {
    Flash f;
    for (int i = 0; i <= 2; ++i) put(f, 0, i, make(1 + i, lay()));
    const SlotNext n = next_slot(f.sec[0], f.sec[1], pick_latest(f.sec[0], f.sec[1], lay()));
    CHECK(n.sector == 0);
    CHECK(n.slot == 3);
    CHECK_FALSE(n.erase_first);
}

TEST_CASE("next_slot: full sector rolls with erase") {
    Flash f;
    for (int i = 0; i < kPersistSlots; ++i) put(f, 0, i, make(1 + i, lay()));
    for (int i = 0; i < kPersistSlots; ++i) put(f, 1, i, make(8 + i, lay()));
    const SlotPick p = pick_latest(f.sec[0], f.sec[1], lay());
    CHECK(p.sector == 1);
    CHECK(p.slot == 6);
    const SlotNext n = next_slot(f.sec[0], f.sec[1], p);
    CHECK(n.sector == 0);
    CHECK(n.slot == 0);
    CHECK(n.erase_first);
}

TEST_CASE("layout: count constants are part of the hash") {
    const uint32_t base = ffctl::kParamLayoutHash;
    const uint32_t sc = static_cast<uint32_t>(spky::SCALE_LIST_COUNT);
    const uint32_t so = static_cast<uint32_t>(spky::kSongLadderCount);
    const uint32_t fl = static_cast<uint32_t>(spky::kFluxRateCount);
    const uint32_t ref = shell::layout_hash_mix(base, sc, so, fl);
    CHECK(shell::layout_hash_mix(base, sc + 1, so, fl) != ref);
    CHECK(shell::layout_hash_mix(base, sc, so + 1, fl) != ref);
    CHECK(shell::layout_hash_mix(base, sc, so, fl + 1) != ref);
    CHECK(shell::current_layout().hash == ref);
}

TEST_CASE("next_slot: no record at all writes sector 0 slot 0") {
    Flash f;
    SlotNext n = next_slot(f.sec[0], f.sec[1], SlotPick{-1, 0, 0});
    CHECK(n.sector == 0);
    CHECK(n.slot == 0);
    CHECK_FALSE(n.erase_first);
    f.sec[0][5] = 0x00;   // debris in the slot: it must be erased first
    n = next_slot(f.sec[0], f.sec[1], SlotPick{-1, 0, 0});
    CHECK(n.erase_first);
}
