#include "take_file.h"

#include <cstring>

namespace shell {

namespace {

// Byte layout of the header block. Little-endian throughout, written byte by
// byte: no struct is punned onto these bytes.
constexpr uint32_t kOffRiff     = 0;     // "RIFF" size "WAVE"
constexpr uint32_t kOffFmt      = 12;    // "fmt " 18, then 18 bytes
constexpr uint32_t kFmtSize     = 18;    // WAVEFORMATEX with cbSize = 0, as format 3 wants
constexpr uint32_t kOffFact     = kOffFmt + 8 + kFmtSize;    // 38: "fact" 4, frames
constexpr uint32_t kOffFfsm     = kOffFact + 8 + 4;          // 50: "ffsm" size
constexpr uint32_t kOffMeta     = kOffFfsm + 8;              // 58: the meta fields
constexpr uint32_t kFfsmSize    = kTakeDataChunk - kOffMeta; // pads up to the data chunk
// Meta fields, from kOffMeta.
constexpr uint32_t kMetaMagic   = 0;
constexpr uint32_t kMetaVersion = 4;
constexpr uint32_t kMetaDeck    = 8;
constexpr uint32_t kMetaSeq     = 12;
constexpr uint32_t kMetaFrames  = 16;
constexpr uint32_t kMetaCount   = 20;
constexpr uint32_t kMetaFrame0  = 24;
constexpr uint32_t kMetaStrength0 = kMetaFrame0 + 4u * kTakeMaxMarkers;
constexpr uint32_t kMetaCrc     = kMetaStrength0 + kTakeMaxMarkers;   // the field take_meta_crc stops before
constexpr uint32_t kMetaBytes   = kMetaCrc + 4;

static_assert(kFfsmSize % 2 == 0, "RIFF chunks are even");
static_assert(kMetaBytes <= kFfsmSize, "the meta fits its chunk");
static_assert(kTakeHeaderBytes % 512 == 0, "the data starts on a sector");
static_assert(kTakeDataChunk + 8 == kTakeHeaderBytes, "the data chunk header ends the block");

void put32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}
void put16(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
uint32_t get32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
           | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t get16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
bool tag(const uint8_t* p, const char* t) { return std::memcmp(p, t, 4) == 0; }

// The meta fields as they are encoded, without the crc: what take_meta_crc
// folds. One function, so the encoder and the CRC cannot drift apart.
void encode_meta(const TakeMeta& m, uint8_t* p)
{
    put32(p + kMetaMagic, kTakeMagic);
    put32(p + kMetaVersion, kTakeVersion);
    put32(p + kMetaDeck, m.deck);
    put32(p + kMetaSeq, m.seq);
    put32(p + kMetaFrames, m.frames);
    put32(p + kMetaCount, static_cast<uint32_t>(m.n_markers));
    const int n = m.n_markers < 0 ? 0 : (m.n_markers > kTakeMaxMarkers ? kTakeMaxMarkers : m.n_markers);
    for (int i = 0; i < kTakeMaxMarkers; ++i) {
        put32(p + kMetaFrame0 + 4u * static_cast<uint32_t>(i), i < n ? m.marker_frame[i] : 0u);
        p[kMetaStrength0 + static_cast<uint32_t>(i)] = i < n ? m.marker_strength[i] : 0u;
    }
}

struct CrcTable
{
    uint32_t t[256];
    CrcTable()
    {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
    }
};
const CrcTable kCrc;

} // namespace

uint32_t crc32_begin() { return 0xFFFFFFFFu; }

uint32_t crc32_update(uint32_t c, const uint8_t* p, size_t n)
{
    for (size_t i = 0; i < n; ++i) c = kCrc.t[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c;
}

uint32_t crc32_end(uint32_t c) { return c ^ 0xFFFFFFFFu; }

uint32_t take_meta_crc(uint32_t c, const TakeMeta& m)
{
    uint8_t meta[kMetaCrc];
    encode_meta(m, meta);
    return crc32_update(c, meta, kMetaCrc);
}

bool take_crc_ok(uint32_t data_state, const TakeMeta& m)
{
    return crc32_end(take_meta_crc(data_state, m)) == m.crc;
}

void take_header_encode(const TakeMeta& m, uint8_t out[kTakeHeaderBytes])
{
    std::memset(out, 0, kTakeHeaderBytes);
    std::memcpy(out + kOffRiff, "RIFF", 4);
    put32(out + kOffRiff + 4, kTakeFileBytes - 8);
    std::memcpy(out + kOffRiff + 8, "WAVE", 4);

    uint8_t* f = out + kOffFmt;
    std::memcpy(f, "fmt ", 4);
    put32(f + 4, kFmtSize);
    put16(f + 8, 3);                           // WAVE_FORMAT_IEEE_FLOAT
    put16(f + 10, 2);
    put32(f + 12, kTakeSampleRate);
    put32(f + 16, kTakeSampleRate * kTakeFrameBytes);
    put16(f + 20, static_cast<uint16_t>(kTakeFrameBytes));
    put16(f + 22, 32);
    put16(f + 24, 0);                          // cbSize

    std::memcpy(out + kOffFact, "fact", 4);
    put32(out + kOffFact + 4, 4);
    put32(out + kOffFact + 8, m.frames);

    std::memcpy(out + kOffFfsm, "ffsm", 4);
    put32(out + kOffFfsm + 4, kFfsmSize);
    encode_meta(m, out + kOffMeta);
    put32(out + kOffMeta + kMetaCrc, m.crc);

    std::memcpy(out + kTakeDataChunk, "data", 4);
    put32(out + kTakeDataChunk + 4, m.frames * kTakeFrameBytes);
}

bool take_header_decode(const uint8_t in[kTakeHeaderBytes], uint8_t deck, TakeMeta& m)
{
    if (!tag(in + kOffRiff, "RIFF") || get32(in + kOffRiff + 4) != kTakeFileBytes - 8
        || !tag(in + kOffRiff + 8, "WAVE"))
        return false;
    const uint8_t* f = in + kOffFmt;
    if (!tag(f, "fmt ") || get32(f + 4) != kFmtSize || get16(f + 8) != 3 || get16(f + 10) != 2
        || get32(f + 12) != kTakeSampleRate || get32(f + 16) != kTakeSampleRate * kTakeFrameBytes
        || get16(f + 20) != kTakeFrameBytes || get16(f + 22) != 32 || get16(f + 24) != 0)
        return false;
    if (!tag(in + kOffFact, "fact") || get32(in + kOffFact + 4) != 4) return false;
    if (!tag(in + kOffFfsm, "ffsm") || get32(in + kOffFfsm + 4) != kFfsmSize) return false;
    if (!tag(in + kTakeDataChunk, "data")) return false;

    const uint8_t* p = in + kOffMeta;
    if (get32(p + kMetaMagic) != kTakeMagic || get32(p + kMetaVersion) != kTakeVersion) return false;
    if (get32(p + kMetaDeck) != deck) return false;
    const uint32_t seq    = get32(p + kMetaSeq);
    const uint32_t frames = get32(p + kMetaFrames);
    const uint32_t count  = get32(p + kMetaCount);
    if (seq == 0 || frames > kTakeMaxFrames || count > static_cast<uint32_t>(kTakeMaxMarkers))
        return false;
    if (get32(in + kOffFact + 8) != frames) return false;
    if (get32(in + kTakeDataChunk + 4) != frames * kTakeFrameBytes) return false;

    uint32_t prev = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t fr = get32(p + kMetaFrame0 + 4u * i);
        if (fr >= frames) return false;
        if (i > 0 && fr <= prev) return false;
        prev = fr;
    }

    m.seq = seq;
    m.frames = frames;
    m.deck = deck;
    m.n_markers = static_cast<int>(count);
    for (int i = 0; i < kTakeMaxMarkers; ++i) {
        const bool in_use = i < m.n_markers;
        m.marker_frame[i]    = in_use ? get32(p + kMetaFrame0 + 4u * static_cast<uint32_t>(i)) : 0u;
        m.marker_strength[i] = in_use ? p[kMetaStrength0 + static_cast<uint32_t>(i)] : 0u;
    }
    m.crc = get32(p + kMetaCrc);
    return true;
}

void take_junk_header(uint32_t frames, uint8_t out[8])
{
    std::memcpy(out, "JUNK", 4);
    put32(out + 4, (kTakeMaxFrames - frames) * kTakeFrameBytes);
}

SlotOrder take_slot_order(const bool valid[2], const uint32_t seq[2])
{
    if (valid[0] && valid[1]) return seq[1] > seq[0] ? SlotOrder{1, 0} : SlotOrder{0, 1};
    if (valid[0]) return {0, -1};
    if (valid[1]) return {1, -1};
    return {-1, -1};
}

} // namespace shell
