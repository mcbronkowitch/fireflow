#pragma once

// Sampler takes on the SD card (spec 2026-11-06-sampler-takes-on-sd): the
// slot file's layout, its CRC-32 and the choice between a deck's two slots.
//
// A slot is a RIFF/WAVE file of 32-bit float stereo at 48 kHz, preallocated
// once at its full size (kTakeFileBytes) and from then on overwritten in
// place. Its first kTakeHeaderBytes are one block -- RIFF header, fmt, fact,
// the FireFlow meta chunk `ffsm`, and the `data` chunk header -- so the data
// starts on a sector boundary and moves straight between the SDRAM buffer and
// the card. A `JUNK` chunk after the data pads the file to its fixed size, so
// it stays valid RIFF at every take length.
//
// Pure data logic, no hardware type, host tested (tests/test_take_file.cpp).
//
// Spec: ../docs/superpowers/specs/2026-11-06-sampler-takes-on-sd-design.md
// sections 4.1-4.2.
#include <cstddef>
#include <cstdint>

namespace shell {

inline constexpr uint32_t kTakeMagic       = 0x4D534646u;  // "FFSM" little-endian
inline constexpr uint32_t kTakeVersion     = 1;
inline constexpr uint32_t kTakeHeaderBytes = 4096;
inline constexpr uint32_t kTakeDataChunk   = kTakeHeaderBytes - 8;  // the `data` chunk header
inline constexpr uint32_t kTakeMaxFrames   = 2016000;  // 42 s at 48 kHz; == kSamplerFrames (asserted in take_card.cpp)
inline constexpr uint32_t kTakeFrameBytes  = 8;        // {float l, float r}
inline constexpr int      kTakeMaxMarkers  = 512;      // == sampler_cfg::kMaxSlices (asserted in the test)
inline constexpr uint32_t kTakeFileBytes   = kTakeHeaderBytes + kTakeMaxFrames * kTakeFrameBytes + 8;
inline constexpr uint32_t kTakeSampleRate  = 48000;

struct TakeMeta
{
    uint32_t seq       = 0;   // per deck, from 1; 0 marks the slot invalid (a save in progress)
    uint32_t frames    = 0;
    uint8_t  deck      = 0;
    int      n_markers = 0;
    uint32_t marker_frame[kTakeMaxMarkers]    = {};
    uint8_t  marker_strength[kTakeMaxMarkers] = {};
    uint32_t crc       = 0;   // over the data bytes, then the meta fields (take_meta_crc)
};

// CRC-32, polynomial 0xEDB88320 (the zlib one), incremental and table-driven:
// the boot load runs it over up to 16 MB.
uint32_t crc32_begin();
uint32_t crc32_update(uint32_t c, const uint8_t* p, size_t n);
uint32_t crc32_end(uint32_t c);

// Folds the meta fields before `crc` into c, in their encoded byte order.
// The take's CRC is: crc32_begin(), every data byte, take_meta_crc(), crc32_end().
uint32_t take_meta_crc(uint32_t c, const TakeMeta& m);

// The header block. encode writes every byte of `out` (padding zeroed).
// decode is false when the block is not a take of this layout for `deck`:
// RIFF / WAVE / fmt / fact / ffsm / data wrong, magic, version or deck off,
// seq 0, frames > kTakeMaxFrames, or the markers not strictly ascending,
// not < frames, or more than kTakeMaxMarkers. The CRC is not checked here --
// it needs the data (take_crc_ok).
void take_header_encode(const TakeMeta& m, uint8_t out[kTakeHeaderBytes]);
bool take_header_decode(const uint8_t in[kTakeHeaderBytes], uint8_t deck, TakeMeta& m);

// The 8-byte `JUNK` chunk header that goes right after the data.
void     take_junk_header(uint32_t frames, uint8_t out[8]);
inline constexpr uint32_t take_junk_offset(uint32_t frames)
{
    return kTakeHeaderBytes + frames * kTakeFrameBytes;
}

// data_state: crc32_begin() folded over every data byte. True when the meta's
// crc matches.
bool take_crc_ok(uint32_t data_state, const TakeMeta& m);

// A deck's two slots, newest valid first; -1 where there is none.
struct SlotOrder { int first; int second; };
SlotOrder take_slot_order(const bool valid[2], const uint32_t seq[2]);

// Where the next save goes: the slot the deck did NOT load from (0 if none).
inline constexpr int take_next_slot(int loaded_slot) { return loaded_slot == 0 ? 1 : 0; }

} // namespace shell
