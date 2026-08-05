// engine/flow/terrain.h
//
// The terrain generator's public surface (spec §4). A terrain's full
// identity is (master seed, override vector) -- NOT a bare seed -- because
// partial reroll replaces parts of it: every drawn value gets its RNG
// stream from (master, stream id, override counter), so bumping one
// macro's counter rerolls only that macro's domain and never shifts a
// neighboring stream. generate() is pure table arithmetic over taste.h --
// no audio, no heap, cheap enough to call from anywhere except the audio
// callback's inner loop.
#pragma once
#include <cstdint>
#include "flow/flow_ids.h"
#include "flow/flow_params.h"
#include "mod/rng.h"

namespace spky { namespace flow {

// Terrain::base[P_ENGINE_*] holds EngineId values (engine_iface.h). The
// ids live in the enclosing spky namespace; re-export them here so flow
// consumers (and the tests, which open only spky::flow) can name them
// without reaching up a namespace.
using spky::EngineId;
using spky::ENGINE_TEST_TONE;
using spky::ENGINE_SYNTH;
using spky::ENGINE_SAMPLER;
using spky::ENGINE_WAVE;
using spky::ENGINE_BODY;
using spky::ENGINE_BBD;
using spky::ENGINE_COUNT;
// Same re-export reasoning for Rng (mod/rng.h): it lives in the enclosing
// spky namespace, and draw_new()'s signature below needs flow:: callers
// (and the tests, which open only spky::flow) to be able to name it.
using spky::Rng;

struct TerrainState {
    uint32_t master = 1;
    uint16_t reroll[MACRO_COUNT] = {};   // override counters per macro domain

    // Weather rerolls with the whole terrain: its counter is the sum of all
    // six macro counters, so ANY partial reroll refreshes the weather too.
    // That is §4's "a new sub-seed like any other stage" reading -- weather
    // is not player-addressable, it just follows whichever domain moved.
    uint32_t reroll_weather_counter() const {
        uint32_t s = 0;
        for (int m = 0; m < MACRO_COUNT; ++m) s += reroll[m];
        return s;
    }
};

struct Curve { int param; float bp[5]; };          // drawn story curve
struct MacroMap { int story; int n_targets; Curve targets[6]; };

struct Terrain {
    Archetype arch;
    float     base[P_COUNT];              // engine units; storied params too
    bool      storied[P_COUNT];           // owned by some macro's curve
    MacroMap  map[MACRO_COUNT];
    int       weather_n;                  // 2..4
    float     weather_period_s[4], weather_depth[4];
    Macro     weather_target[4];
};

Terrain generate(const TerrainState& st);

// Distance between two terrains (spec 7.4): mean |Δ normalized base| over
// every P_COUNT param (normalized by that param's kParams span), plus a
// flat 0.25f if the archetypes differ. Implemented in terrain.cpp.
float distance(const Terrain& a, const Terrain& b);

// Draw a new terrain state that reads as a different place from cur (spec
// 7.4's NEW gesture): a fresh master with every reroll counter zero, at
// least kDistanceMin away from cur by distance() above. seq is the
// caller-held sequence Rng -- passing the same seeded Rng twice reproduces
// the same draw chain, so NEW is deterministic given a fixed seed even
// though it never repeats cur.master. See terrain.cpp for the retry
// policy (up to 16 tries, falls back to the farthest candidate seen).
TerrainState draw_new(const TerrainState& cur, Rng& seq);

} } // namespace spky::flow
