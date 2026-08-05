// engine/flow/terrain_code.h
//
// Human-shareable terrain codes: "F1-DEADBEEF-000100020000" -- format
// version 1, 8 hex master, then one 2-hex counter per macro in table order
// (MACRO_COUNT == 6 today: M_MOTION..M_SPACE, flow_ids.h). Header-only: one
// stateless encode + one stateless decode, snprintf into a caller-owned
// buffer with an explicit cap -- no heap, no unbounded writes.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "flow/flow_ids.h"
#include "flow/terrain.h"

namespace spky { namespace flow {

// Fixed wire length in characters (NOT counting the trailing NUL):
// "F1-" (3) + 8 hex master digits + "-" (1) + MACRO_COUNT*2 hex counter
// digits. Ties itself to MACRO_COUNT so a future macro-count change can't
// silently desync the format without this constant (and every buffer size
// derived from it) moving too.
constexpr int kTerrainCodeLen = 3 + 8 + 1 + MACRO_COUNT * 2;

// Encodes st into out as e.g. "F1-DEADBEEF-000100020000" (kTerrainCodeLen
// chars + NUL). Returns the length written (== kTerrainCodeLen) on success,
// or -1 if cap is too small to hold the code and its NUL -- out is left
// untouched in that case.
//
// reroll[] counters are uint16_t but the wire format is one hex BYTE
// (%02X) per counter: values above 255 print modulo 256 (wrap, not
// saturate -- e.g. 260 prints as "04"). That is an accepted lossy
// encoding: reroll counts that high are far beyond any real play session,
// and decode_code() reads the same low byte back, so a round trip through
// a printed code is stable even though it is not a full uint16_t round
// trip for pathologically large counters.
inline int encode_code(const TerrainState& st, char* out, int cap) {
    if (cap < kTerrainCodeLen + 1) return -1;
    int n = std::snprintf(out, size_t(cap), "F1-%08X-", unsigned(st.master));
    for (int m = 0; m < MACRO_COUNT; ++m) {
        n += std::snprintf(out + n, size_t(cap - n), "%02X",
                            unsigned(st.reroll[m] & 0xFFu));
    }
    return n;
}

namespace detail {
inline bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}
inline unsigned hex_val(char c) {
    if (c >= '0' && c <= '9') return unsigned(c - '0');
    if (c >= 'a' && c <= 'f') return unsigned(c - 'a') + 10u;
    return unsigned(c - 'A') + 10u;                     // already validated
}
} // namespace detail

// Strict decode: exact "F1-" prefix, exact overall length (kTerrainCodeLen,
// no trailing junk), hex digits only in the master and counter fields, '-'
// exactly at the one separator position. Anything else -- wrong version,
// truncated, extended, non-hex characters -- returns false and leaves out
// untouched.
inline bool decode_code(const char* code, TerrainState& out) {
    if (!code) return false;
    if (std::strlen(code) != size_t(kTerrainCodeLen)) return false;
    if (code[0] != 'F' || code[1] != '1' || code[2] != '-') return false;
    for (int i = 3; i < 11; ++i)
        if (!detail::is_hex(code[i])) return false;
    if (code[11] != '-') return false;
    for (int i = 12; i < kTerrainCodeLen; ++i)
        if (!detail::is_hex(code[i])) return false;

    TerrainState st;
    uint32_t master = 0;
    for (int i = 3; i < 11; ++i) master = (master << 4) | detail::hex_val(code[i]);
    st.master = master;
    for (int m = 0; m < MACRO_COUNT; ++m) {
        const unsigned v = (detail::hex_val(code[12 + m * 2]) << 4) |
                            detail::hex_val(code[12 + m * 2 + 1]);
        st.reroll[m] = uint16_t(v);
    }
    out = st;
    return true;
}

} } // namespace spky::flow
