#pragma once
#include <cstdint>
#include "mod/rng.h"

namespace spky { namespace flow {

// Stream id blocks. Params and macros get one id each; the stage-level
// draws get fixed ids above both blocks.
enum : uint32_t {
    kStreamParamBase = 0,          // + ParamId
    kStreamMacroBase = 1000,       // + Macro
    // + Macro: that domain's adventure level, keyed on that macro's OWN reroll
    // counter so it rerolls with the domain and with nothing else. Its own
    // block rather than kStreamMacroBase + m, which is already the domain's
    // story/curve stream -- sharing it would make the nerve a function of how
    // many values the curves happened to draw.
    kStreamAdventureMacro = 3000,
    kStreamArch      = 2000,
    kStreamRoles     = 2001,
    kStreamTonality  = 2002,
    kStreamWeather   = 2003,
    kStreamDistance  = 2004,
    kStreamNewSeq    = 2005,       // Flow's NEW press-chain sequence Rng
    kStreamAdventure = 2006,       // the BASE PATCH's risk level; master-keyed
};

// splitmix32-style avalanche of the (master, stream, counter) triple.
// One multiply-xor round per word is enough: Rng itself keeps mixing.
inline uint32_t stream_seed(uint32_t master, uint32_t stream_id,
                            uint32_t counter) {
    uint32_t h = master;
    h ^= stream_id + 0x9E3779B9u + (h << 6) + (h >> 2);
    h *= 0x85EBCA6Bu; h ^= h >> 13;
    h ^= counter + 0x9E3779B9u + (h << 6) + (h >> 2);
    h *= 0xC2B2AE35u; h ^= h >> 16;
    return h ? h : 0x1u;           // Rng::seed treats 0 as 1 anyway
}

inline Rng make_stream(uint32_t master, uint32_t stream_id,
                       uint32_t counter) {
    Rng r;
    r.seed(stream_seed(master, stream_id, counter));
    return r;
}

} } // namespace spky::flow
