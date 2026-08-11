// engine/flow/terrain.h
//
// The terrain generator's public surface (spec §4). A terrain's full
// identity is (master seed, override vector) -- NOT a bare seed -- because
// partial reroll replaces parts of it: every drawn value gets its RNG
// stream from (master, stream id, override counter), so bumping one
// macro's counter rerolls only that macro's domain and never shifts a
// neighboring stream. That holds for VALUES too, and the adventure levels
// (spec §7) are per domain precisely so it keeps holding -- see
// Terrain::adventure below. generate() is pure table arithmetic over taste.h --
// no audio, no heap, cheap enough to call from anywhere except the audio
// callback's inner loop.
#pragma once
#include <cstdint>
#include <type_traits>
#include "flow/flow_ids.h"
#include "flow/flow_params.h"       // Span lives here, not taste.h
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

// A hand-authored base patch riding alongside a seed (spec 2026-08-11 §4.1).
// Indexed by ParamId rather than packed to the 38 base-rule slots: a packed
// form needs a second index table that can drift from kBaseRules, and 315
// bytes is not worth that risk.
//
// Trivially copyable on purpose -- host/vcv/src/touch_pads.hpp's Place holds
// one, and Glow.cpp copies the whole Place array to the AUDIO thread as one
// staged handover. A heap-owning member would put a malloc in that copy.
struct BaseOverlay {
    float v[P_COUNT]   = {};
    bool  has[P_COUNT] = {};
};

static_assert(std::is_trivially_copyable<BaseOverlay>::value,
              "BaseOverlay is copied on the audio thread (Glow.cpp, "
              "UiOp::SET_PLACES); it must not own heap memory");

// True if `param` is set by a kBaseRules row -- i.e. if an overlay entry for
// it is honoured. Reads the table; never a transcribed list, because taste.h
// owns this partition and moves it.
bool is_base_rule(int param);

struct Curve { int param; float bp[5]; };          // drawn story curve
struct MacroMap { int story; int n_targets; Curve targets[6]; };

struct Terrain {
    Archetype arch;
    // Stage-1 role, published because the runtime needs it: NEW switches the
    // two decks' discrete params staggered (texture first, carrier a quarter
    // of the blend later, spec §5), and "which deck is the carrier" is not
    // recoverable from base[P_ENGINE_*] alone -- both decks can hold the
    // same engine id. Written by generate(); costs no RNG draw.
    bool      a_carries;                  // true -> deck A carries, B textures
    float     base[P_COUNT];              // engine units; storied params too
    bool      storied[P_COUNT];           // owned by some macro's curve
    MacroMap  map[MACRO_COUNT];
    // The picked variant's window for THIS terrain's archetype, per macro.
    // Copied at generate() time so the runtime never re-reads kStories. Has
    // NO default member initialiser (unlike StoryVariant::arch_window), so
    // a `Terrain t{}` zero-inits every entry to {0,0}, not the {0,1}
    // identity every other macro relies on -- if generate() ever left a
    // macro's window uncopied, that macro's story would silently sample
    // only x=0 (its bp[0] floor) at every knob position. Currently
    // unreachable: "every macro has at least one story"
    // (test_flow_taste.cpp) guarantees stage 4's `if (picked)` branch fires
    // for every m. Worth knowing at this site if that invariant ever moves.
    Span      window[MACRO_COUNT];
    int       weather_n;                  // 2..4
    float     weather_period_s[4], weather_depth[4];
    Macro     weather_target[4];
    // Risk levels, 0..1 (spec §7). SEVEN of them, not one, and the split is
    // what lets §7 and 7.3 both hold:
    //
    //   adventure[m]    drawn from that macro's OWN reroll counter, and used
    //                   for every curve that macro's stories draw. Rerolling
    //                   DENSITY redraws DENSITY's nerve -- a wild DENSITY does
    //                   not survive the player asking for a new one (§7) --
    //                   and touches no other domain's (7.3).
    //   adventure_base  drawn from the master ALONE, counter fixed at 0, and
    //                   used for the base patch and the mode coin. Keyed on
    //                   nothing a partial reroll can move, so a partial reroll
    //                   cannot shift a base parameter at all.
    //
    // A single per-terrain level keyed on reroll_weather_counter() was built
    // first and does NOT work: unlike the weather, which is an additive layer
    // over a finished terrain, the level is an INPUT to every span draw, so
    // rerolling one macro re-narrowed the spans every other value came from
    // and moved the whole terrain. Measured, then replaced by the owner's
    // ruling. Do not collapse these back into one.
    float     adventure[MACRO_COUNT];
    float     adventure_base;
};

// The archetype alone, without building a terrain. Stage 0 is a pure function
// of the master -- make_stream(master, kStreamArch, 0), counter pinned at 0,
// no dependence on adventure, roles or anything drawn later -- and draw_new's
// genre filter needs to reject candidates before paying for a full generate()
// on the audio thread. generate() itself now calls this, so the two cannot
// drift apart; test_flow_terrain.cpp pins that they agree anyway.
Archetype arch_of(uint32_t master);

Terrain generate(const TerrainState& st, const BaseOverlay* ov = nullptr);

// One value inside a span, narrowed toward the middle by the terrain's
// adventure level: full span at adv == 1, the middle kAdventureNarrow at 0.
// Declared here rather than left in terrain.cpp's anonymous namespace so the
// tests can assert the narrowing directly instead of inferring it from the
// terrains it produces.
float draw_span(Rng& r, const Span& s, float adv);

// Distance between two terrains (spec 7.4): mean |Δ normalized base| over
// every P_COUNT param (normalized by that param's kParams span), plus a
// flat 0.25f if the archetypes differ. Implemented in terrain.cpp.
float distance(const Terrain& a, const Terrain& b);

// Draw a new terrain state that reads as a different place from cur (spec
// 7.4's NEW gesture, extended by spec 2026-08-07 §2.2). Every candidate is a
// fresh master with all reroll counters zero. seq is the caller-held sequence
// Rng -- passing the same seeded Rng twice reproduces the same draw chain --
// and cur.master is never returned.
//
// TWO BRANCHES, and they use different rules on purpose:
//
//   want == ARCH_ANY   the original: retry until a candidate clears
//                      kDistanceMin, or give up after 16 tries and take the
//                      farthest seen. Unchanged, down to the RNG draw count.
//   want == archetype  candidates whose arch_of() does not match are skipped
//                      without being generated; once kGenreCandidates of them
//                      match, the farthest by distance() wins. NO threshold --
//                      see taste.h at kGenreCandidates for why one would be
//                      decorative here. Bounded by kGenreDrawCap draws.
TerrainState draw_new(const TerrainState& cur, Rng& seq, int want = ARCH_ANY);

} } // namespace spky::flow
