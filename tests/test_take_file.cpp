// Sampler takes on SD: slot file layout, CRC-32 and slot order.
// Pure data logic, no hardware type.
// Spec: docs/superpowers/specs/2026-11-06-sampler-takes-on-sd-design.md
// sections 4.1-4.2.
#include <doctest/doctest.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../shell/take_file.h"
#include "sampler/sampler_config.h"

using namespace shell;

static_assert(kTakeMaxMarkers == spky::sampler_cfg::kMaxSlices,
              "the file carries every marker the map can hold");
static_assert(kTakeMaxFrames == static_cast<uint32_t>(spky::sampler_cfg::kSizeCeilS * 48000.f),
              "one slot holds the whole buffer");

namespace {

uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint16_t rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }

TakeMeta sample_meta(int n_markers = 3)
{
    TakeMeta m;
    m.seq = 7;
    m.frames = 96000;
    m.deck = 1;
    m.n_markers = n_markers;
    for (int i = 0; i < n_markers; ++i) {
        m.marker_frame[i] = static_cast<uint32_t>(i) * 150u + 10u;
        m.marker_strength[i] = static_cast<uint8_t>(i * 37 + 1);
    }
    return m;
}

// An independent RIFF walker: every chunk from offset 12, sizes even, the
// last one ending exactly at `file_bytes`. Fills in where fmt/fact/data/JUNK sit.
struct Walk { bool ok = false; uint32_t fmt = 0, fact = 0, ffsm = 0, data = 0, junk = 0, end = 0; };

Walk walk(const uint8_t* hdr, const uint8_t junk_hdr[8], uint32_t frames, uint32_t file_bytes)
{
    Walk w;
    if (std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "WAVE", 4) != 0) return w;
    if (rd32(hdr + 4) != file_bytes - 8) return w;
    uint32_t off = 12;
    while (off < kTakeHeaderBytes) {
        const uint32_t size = rd32(hdr + off + 4);
        if (size % 2 != 0) return w;
        if (std::memcmp(hdr + off, "fmt ", 4) == 0) w.fmt = off;
        else if (std::memcmp(hdr + off, "fact", 4) == 0) w.fact = off;
        else if (std::memcmp(hdr + off, "ffsm", 4) == 0) w.ffsm = off;
        else if (std::memcmp(hdr + off, "data", 4) == 0) { w.data = off; off = off + 8 + size; break; }
        off += 8 + size;
    }
    if (w.data == 0 || off != take_junk_offset(frames)) return w;
    if (std::memcmp(junk_hdr, "JUNK", 4) != 0) return w;
    w.junk = off;
    w.end = off + 8 + rd32(junk_hdr + 4);
    w.ok = w.end == file_bytes;
    return w;
}

} // namespace

TEST_CASE("crc32: check value") {
    const char* s = "123456789";
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    CHECK(crc32_end(crc32_update(crc32_begin(), p, 9)) == 0xCBF43926u);
    uint32_t c = crc32_begin();
    c = crc32_update(c, p, 2);
    c = crc32_update(c, p + 2, 5);
    c = crc32_update(c, p + 7, 2);
    CHECK(crc32_end(c) == 0xCBF43926u);
}

TEST_CASE("take_file: header round trip") {
    for (int n : {0, 3, kTakeMaxMarkers}) {
        TakeMeta m = sample_meta(0);
        m.frames = 600000;
        m.n_markers = n;
        for (int i = 0; i < n; ++i) {
            m.marker_frame[i] = static_cast<uint32_t>(i) * 1000u;
            m.marker_strength[i] = static_cast<uint8_t>(255 - i % 256);
        }
        m.crc = 0xDEADBEEFu;
        uint8_t hdr[kTakeHeaderBytes];
        take_header_encode(m, hdr);
        TakeMeta d;
        REQUIRE(take_header_decode(hdr, 1, d));
        CHECK(d.seq == m.seq);
        CHECK(d.frames == m.frames);
        CHECK(d.deck == m.deck);
        CHECK(d.crc == m.crc);
        REQUIRE(d.n_markers == n);
        for (int i = 0; i < n; ++i) {
            CHECK(d.marker_frame[i] == m.marker_frame[i]);
            CHECK(d.marker_strength[i] == m.marker_strength[i]);
        }
    }
    // An empty take (a clear) is a valid take.
    TakeMeta e = sample_meta(0);
    e.frames = 0;
    uint8_t hdr[kTakeHeaderBytes];
    take_header_encode(e, hdr);
    TakeMeta d;
    CHECK(take_header_decode(hdr, 1, d));
    CHECK(d.frames == 0);
}

TEST_CASE("take_file: the header is a valid WAV") {
    for (uint32_t frames : {0u, 1u, 96000u, kTakeMaxFrames}) {
        TakeMeta m = sample_meta(0);
        m.frames = frames;
        uint8_t hdr[kTakeHeaderBytes];
        uint8_t junk[8];
        take_header_encode(m, hdr);
        take_junk_header(frames, junk);
        const Walk w = walk(hdr, junk, frames, kTakeFileBytes);
        INFO("frames " << frames);
        REQUIRE(w.ok);
        CHECK(w.data == kTakeDataChunk);
        CHECK(rd32(hdr + w.data + 4) == frames * kTakeFrameBytes);
        REQUIRE(w.fmt != 0);
        const uint8_t* f = hdr + w.fmt + 8;
        CHECK(rd16(f + 0) == 3);                    // IEEE float
        CHECK(rd16(f + 2) == 2);                    // stereo
        CHECK(rd32(f + 4) == kTakeSampleRate);
        CHECK(rd32(f + 8) == kTakeSampleRate * 8);  // byte rate
        CHECK(rd16(f + 12) == 8);                   // block align
        CHECK(rd16(f + 14) == 32);                  // bits
        REQUIRE(w.fact != 0);
        CHECK(rd32(hdr + w.fact + 8) == frames);
        CHECK(w.ffsm != 0);
    }
}

TEST_CASE("take_file: seq 0 header is invalid") {
    TakeMeta m = sample_meta();
    m.seq = 0;
    uint8_t hdr[kTakeHeaderBytes];
    take_header_encode(m, hdr);
    TakeMeta d;
    CHECK_FALSE(take_header_decode(hdr, 1, d));
}

TEST_CASE("take_file: every other rejection") {
    uint8_t hdr[kTakeHeaderBytes];
    TakeMeta d;
    const TakeMeta good = sample_meta();
    take_header_encode(good, hdr);
    REQUIRE(take_header_decode(hdr, 1, d));

    SUBCASE("wrong deck") { CHECK_FALSE(take_header_decode(hdr, 0, d)); }
    SUBCASE("all 0xFF") {
        std::memset(hdr, 0xFF, sizeof hdr);
        CHECK_FALSE(take_header_decode(hdr, 1, d));
    }
    SUBCASE("all zero") {
        std::memset(hdr, 0, sizeof hdr);
        CHECK_FALSE(take_header_decode(hdr, 1, d));
    }
    SUBCASE("too many frames") {
        TakeMeta m = good; m.frames = kTakeMaxFrames + 1;
        take_header_encode(m, hdr);
        CHECK_FALSE(take_header_decode(hdr, 1, d));
    }
    SUBCASE("unsorted markers") {
        TakeMeta m = good; m.marker_frame[2] = m.marker_frame[1];
        take_header_encode(m, hdr);
        CHECK_FALSE(take_header_decode(hdr, 1, d));
    }
    SUBCASE("a marker past the take") {
        TakeMeta m = good; m.marker_frame[2] = m.frames;
        take_header_encode(m, hdr);
        CHECK_FALSE(take_header_decode(hdr, 1, d));
    }
    SUBCASE("too many markers") {
        TakeMeta m = good; m.n_markers = kTakeMaxMarkers + 1;
        take_header_encode(m, hdr);
        CHECK_FALSE(take_header_decode(hdr, 1, d));
    }
    SUBCASE("a byte flipped in each fixed field") {
        // Magic, version and the RIFF scaffolding: one byte off anywhere in
        // the first 70 bytes must not decode. (The meta values themselves are
        // the CRC's job.)
        for (int i = 0; i < 70; ++i) {
            take_header_encode(good, hdr);
            hdr[i] ^= 0x5A;
            TakeMeta t;
            const bool still = take_header_decode(hdr, 1, t);
            // seq, frames, n_markers are data, not scaffolding: a flip there
            // can still decode (the CRC catches it). Everything else must not.
            const bool value_field = still && (t.seq != good.seq || t.frames != good.frames
                                               || t.n_markers != good.n_markers);
            INFO("byte " << i);
            CHECK((!still || value_field));
        }
    }
}

TEST_CASE("take_file: CRC covers data and meta") {
    std::vector<uint8_t> data(4096);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 7 + 3);
    TakeMeta m = sample_meta();
    uint32_t c = crc32_update(crc32_begin(), data.data(), data.size());
    m.crc = crc32_end(take_meta_crc(c, m));
    CHECK(take_crc_ok(c, m));

    data[1000] ^= 1;   // a data bit
    CHECK_FALSE(take_crc_ok(crc32_update(crc32_begin(), data.data(), data.size()), m));
    data[1000] ^= 1;
    TakeMeta m2 = m;
    m2.marker_strength[1] ^= 1;   // a meta bit
    CHECK_FALSE(take_crc_ok(c, m2));
    m2 = m;
    m2.seq += 1;
    CHECK_FALSE(take_crc_ok(c, m2));
}

TEST_CASE("take_file: CRC mismatch falls back to the other slot") {
    // The loader's sequence: try first; on a CRC failure try second.
    const bool valid[2] = {true, true};
    const uint32_t seq[2] = {4, 5};
    const SlotOrder o = take_slot_order(valid, seq);
    CHECK(o.first == 1);
    CHECK(o.second == 0);
    int loaded = -1;
    for (int slot : {o.first, o.second}) {
        if (slot < 0) break;
        const bool crc_good = slot == 0;   // the newer slot is torn
        if (crc_good) { loaded = slot; break; }
    }
    CHECK(loaded == 0);
    CHECK(take_next_slot(loaded) == 1);   // and the next save overwrites the torn one
}

TEST_CASE("take_file: slot order") {
    {
        const bool v[2] = {false, false}; const uint32_t s[2] = {0, 0};
        const SlotOrder o = take_slot_order(v, s);
        CHECK(o.first == -1); CHECK(o.second == -1);
    }
    {
        const bool v[2] = {true, false}; const uint32_t s[2] = {3, 9};
        const SlotOrder o = take_slot_order(v, s);
        CHECK(o.first == 0); CHECK(o.second == -1);
    }
    {
        const bool v[2] = {true, true}; const uint32_t s[2] = {9, 3};
        const SlotOrder o = take_slot_order(v, s);
        CHECK(o.first == 0); CHECK(o.second == 1);
    }
    CHECK(take_next_slot(-1) == 0);
    CHECK(take_next_slot(0) == 1);
    CHECK(take_next_slot(1) == 0);
}
