#include "persist_record.h"
#include <cstddef>
#include <cstring>

namespace shell {

namespace {

bool blank(const uint8_t* p, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        if (p[i] != 0xFF) return false;
    return true;
}

// Reads the slot through a copy: flash bytes carry no alignment promise.
bool slot_valid(const uint8_t* sec, int slot, const PersistLayout& want, uint32_t& seq)
{
    PersistRecord r;
    std::memcpy(&r, sec + slot * kPersistStride, sizeof r);
    if (!record_valid(r, want)) return false;
    seq = r.seq;
    return true;
}

} // namespace

uint32_t crc32(const uint8_t* p, uint32_t n)
{
    // Bitwise, table-less: about 530 B once per save, size beats speed here.
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int b = 0; b < 8; ++b)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

void record_seal(PersistRecord& r)
{
    r.magic = kPersistMagic;
    r.crc   = crc32(reinterpret_cast<const uint8_t*>(&r), offsetof(PersistRecord, crc));
}

bool record_valid(const PersistRecord& r, const PersistLayout& want)
{
    if (r.magic != kPersistMagic) return false;
    if (r.layout.num_params != want.num_params
        || r.layout.hash != want.hash
        || r.layout.version != want.version) return false;
    return r.crc == crc32(reinterpret_cast<const uint8_t*>(&r), offsetof(PersistRecord, crc));
}

SlotPick pick_latest(const uint8_t* sec0, const uint8_t* sec1, const PersistLayout& want)
{
    SlotPick best{-1, 0, 0};
    const uint8_t* const secs[2] = {sec0, sec1};
    for (int s = 0; s < 2; ++s)
        for (int i = 0; i < kPersistSlots; ++i) {
            uint32_t seq = 0;
            if (slot_valid(secs[s], i, want, seq) && (best.sector < 0 || seq > best.seq))
                best = SlotPick{s, i, seq};
        }
    return best;
}

SlotNext next_slot(const uint8_t* sec0, const uint8_t* sec1, const SlotPick& latest)
{
    const uint8_t* const secs[2] = {sec0, sec1};
    if (latest.sector < 0)
        return SlotNext{0, 0, !blank(sec0, kPersistStride)};

    const uint8_t* cur = secs[latest.sector];
    for (int i = latest.slot + 1; i < kPersistSlots; ++i)
        if (blank(cur + i * kPersistStride, kPersistStride))
            return SlotNext{latest.sector, i, false};

    // Full: roll to the other sector. This is the only place an erase is asked
    // for, and it hits the sector that does not hold the newest record.
    const int other = 1 - latest.sector;
    return SlotNext{other, 0, !blank(secs[other], kPersistStride)};
}

PersistLayout current_layout()
{
    return PersistLayout{ffctl::NUM_PARAMS, ffctl::kParamLayoutHash, kSnapshotVersion};
}

} // namespace shell
