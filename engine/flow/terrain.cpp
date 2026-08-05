// engine/flow/terrain.cpp
//
// Stages 0-4 of the terrain generator (spec §4): archetype -> roles ->
// tonality -> base patch -> macro mappings, then the weather layer and the
// named hard constraints. Everything is drawn from per-purpose RNG streams
// (flow_rng.h) so a partial reroll bumps exactly one domain's counters and
// every other draw stays bit-identical. All tuning data lives in taste.h;
// this file is only the plumbing that walks those tables.
#include "flow/terrain.h"
#include "flow/taste.h"
#include "flow/flow_rng.h"
#include <cmath>

namespace spky { namespace flow {
namespace {

// Cumulative-weight scan: r.next_unipolar()*total walked against the
// running sum. The trailing return covers the float edge where x lands
// exactly on total (next_unipolar is [0,1), but weights are floats).
int pick_weighted(Rng& r, const float* w, int n) {
    float total = 0.f;
    for (int i = 0; i < n; ++i) total += w[i];
    float x = r.next_unipolar() * total;
    float acc = 0.f;
    for (int i = 0; i < n; ++i) {
        acc += w[i];
        if (x < acc) return i;
    }
    return n - 1;
}

// Uniform integer 0..n-1. next_unipolar() is [0,1) so int() alone stays
// in range; the clamp is belt-and-braces against float rounding at 1.0f.
int pick_index(Rng& r, int n) {
    int i = int(r.next_unipolar() * float(n));
    return i < n ? i : n - 1;
}

// Draw one story-curve target: each breakpoint uniform inside ITS OWN span,
// then the five values sorted monotone in the story's direction. Direction
// = sign of (bp4 span lo - bp0 span lo); a flat story counts as ascending.
// Sorting (not rejection) enforces monotonicity because neighboring spans
// in taste.h may overlap -- an inversion there is a draw artifact, not a
// story feature.
Curve draw_curve(Rng& r, const CurveRule& cr) {
    Curve c;
    c.param = cr.param;
    for (int b = 0; b < 5; ++b)
        c.bp[b] = cr.bp[b].lo + r.next_unipolar() * (cr.bp[b].hi - cr.bp[b].lo);
    const bool descending = cr.bp[4].lo < cr.bp[0].lo;
    for (int i = 1; i < 5; ++i) {              // insertion sort, n=5
        float v = c.bp[i];
        int j = i;
        while (j > 0 && (descending ? c.bp[j-1] < v : c.bp[j-1] > v)) {
            c.bp[j] = c.bp[j-1];
            --j;
        }
        c.bp[j] = v;
    }
    return c;
}

// Named hard constraints, enforced after every draw (spec §4 stage 3; the
// RES cap is already structural -- kParams caps the range at 0.75).
void apply_constraints(Terrain& t) {
    // BODY FILT cliff (ledger watch item). BODY's FILTER is a timbre
    // parameter, not an attenuating filter -- below about -0.5 the deck
    // just dies. The floor must hold on BOTH the base value AND every
    // story-curve breakpoint that targets a BODY deck's FILT: taste.h's
    // BRIGHT dawn story deliberately draws FILT bp0 down to -0.55 (other
    // engines may dive), so this clamp is the only thing keeping a BODY
    // deck off the cliff. max(bp, floor) preserves the curve's monotone
    // order in either direction.
    const bool body[2] = {
        int(t.base[P_ENGINE_A] + 0.5f) == ENGINE_BODY,
        int(t.base[P_ENGINE_B] + 0.5f) == ENGINE_BODY,
    };
    const int filt[2] = { P_FILT_A, P_FILT_B };
    for (int d = 0; d < 2; ++d) {
        if (!body[d]) continue;
        if (t.base[filt[d]] < kBodyFiltFloor) t.base[filt[d]] = kBodyFiltFloor;
        for (int m = 0; m < MACRO_COUNT; ++m)
            for (int i = 0; i < t.map[m].n_targets; ++i) {
                Curve& c = t.map[m].targets[i];
                if (c.param != filt[d]) continue;
                for (int b = 0; b < 5; ++b)
                    if (c.bp[b] < kBodyFiltFloor) c.bp[b] = kBodyFiltFloor;
            }
    }
    // No double high density: min(base A, base B) <= 0.5. With the current
    // taste tables the density bases are story bp0 draws (calm floors well
    // under 0.5) so this rarely fires -- it exists so a future table edit
    // cannot silently produce two hot decks.
    float& da = t.base[P_DENSITY_A];
    float& db = t.base[P_DENSITY_B];
    if (da > 0.5f && db > 0.5f) {
        if (da <= db) da = 0.5f; else db = 0.5f;
    }
}

} // namespace

Terrain generate(const TerrainState& st) {
    Terrain t{};

    // Stage 0: archetype -- the correlation structure that keeps terrains
    // from converging on mid-density mush. Everything downstream reads it.
    {
        Rng r = make_stream(st.master, kStreamArch, 0);
        t.arch = Archetype(pick_weighted(r, kArchWeight, ARCH_COUNT));
    }

    // Stage 1: roles. A coin picks which deck carries; each role then draws
    // its engine from the archetype's weights. Held in locals and written
    // AFTER the stage-3 loop: kBaseRules keeps placeholder ENGINE rows so
    // the table has no hole, and taste.h documents that stage 1 overrides
    // those draws.
    int engine_a, engine_b;
    {
        Rng r = make_stream(st.master, kStreamRoles, 0);
        const bool a_carries = r.next_unipolar() < 0.5f;
        const int carrier = kCarrierEngine[pick_weighted(r, kCarrierW[t.arch], 3)];
        const int texture = kTextureEngine[pick_weighted(r, kTextureW[t.arch], 5)];
        engine_a = a_carries ? carrier : texture;
        engine_b = a_carries ? texture : carrier;
    }

    // Stage 2: tonality. Scale and root are one draw each -- both decks
    // share the single P_SCALE/P_ROOT, so "one scale for both decks" is
    // structural. TUNE/RANGE stay ordinary archetype-conditioned base rules
    // (stage 3, their own streams): kParams gives them no tonality coupling
    // to draw here, and inventing one is listening-loop work, not plumbing.
    int scale, root;
    {
        Rng r = make_stream(st.master, kStreamTonality, 0);
        scale = pick_index(r, kParams[P_SCALE].steps);   // 0..12
        root  = pick_index(r, kParams[P_ROOT].steps);    // 0..11
    }

    // Stage 3: base patch. Every kBaseRules row from its OWN param stream,
    // uniform inside the archetype's span. Counter is 0 for all of them:
    // "owned by a macro" means "targeted by a story", and taste.h's
    // coverage test guarantees base rules and story targets never share a
    // param -- so no base-rule param has an owning macro. Discrete params
    // (FORM/SONG/STEPS) draw continuous here; apply_param() rounds at the
    // engine boundary.
    for (int i = 0; i < kBaseRuleCount; ++i) {
        const BaseRule& br = kBaseRules[i];
        Rng r = make_stream(st.master, kStreamParamBase + uint32_t(br.param), 0);
        const Span& s = br.per_arch[t.arch];
        t.base[br.param] = s.lo + r.next_unipolar() * (s.hi - s.lo);
    }
    // Stages 1-2 override their placeholder rows.
    t.base[P_ENGINE_A] = float(engine_a);
    t.base[P_ENGINE_B] = float(engine_b);
    t.base[P_SCALE]    = float(scale);
    t.base[P_ROOT]     = float(root);

    // Stage 4: macro mappings. One stream per macro, keyed by that macro's
    // own reroll counter. The variant pick is uniform among this macro's
    // kStories entries; then EVERY variant's curves are drawn in table
    // order (constant stream consumption whichever variant wins). The
    // picked variant becomes the mapping: base[p] = its bp[0] (the calm
    // floor), storied[p] = true. Unpicked variants' targets still need a
    // base -- no kBaseRules row covers a story-owned param (taste.h's
    // coverage test allows story-only coverage), so they too take their
    // drawn bp[0] as a calm-floor base, just without a mapping. When two
    // macros share a target (BRIGHT and SPACE both curve REVMIX_A), the
    // later macro's bp[0] wins the base; both curves stay mapped, and the
    // runtime pushes whichever candidate lands FARTHEST from that base
    // (the rule lives in Flow::recompute_and_push, engine/flow/flow.cpp).
    for (int m = 0; m < MACRO_COUNT; ++m) {
        Rng r = make_stream(st.master, kStreamMacroBase + uint32_t(m),
                            st.reroll[m]);
        int n_var = 0;
        for (int s = 0; s < kStoryCount; ++s)
            if (kStories[s].macro == m) ++n_var;
        const int pick = pick_index(r, n_var);

        MacroMap& mm = t.map[m];
        mm.n_targets = 0;
        int vi = 0;
        for (int s = 0; s < kStoryCount; ++s) {
            if (kStories[s].macro != m) continue;
            const StoryVariant& sv = kStories[s];
            const bool picked = (vi == pick);
            if (picked) mm.story = s;            // global kStories index
            for (int tg = 0; tg < sv.n_targets; ++tg) {
                Curve c = draw_curve(r, sv.targets[tg]);
                if (picked) {
                    mm.targets[mm.n_targets++] = c;
                    t.base[c.param]    = c.bp[0];
                    t.storied[c.param] = true;
                } else if (!t.storied[c.param]) {
                    t.base[c.param] = c.bp[0];   // calm floor, unmapped
                }
            }
            ++vi;
        }
    }

    // Stage 5: weather -- 2..4 very slow offsets into the macro sums, so a
    // terrain never becomes wallpaper. Rerolls with any partial reroll (the
    // counter is the sum of the six macro counters -- see terrain.h).
    {
        Rng r = make_stream(st.master, kStreamWeather,
                            st.reroll_weather_counter());
        t.weather_n = kWeatherOscMin
                    + pick_index(r, kWeatherOscMax - kWeatherOscMin + 1);
        for (int i = 0; i < t.weather_n; ++i) {
            t.weather_period_s[i] = kWeatherPeriodMinS
                + r.next_unipolar() * (kWeatherPeriodMaxS - kWeatherPeriodMinS);
            t.weather_depth[i] = kWeatherDepthMin
                + r.next_unipolar() * (kWeatherDepthMax - kWeatherDepthMin);
            t.weather_target[i] = Macro(pick_index(r, MACRO_COUNT));
        }
    }

    apply_constraints(t);
    return t;
}

// Distance (spec 7.4): mean of |Δbase[p]| normalized by that param's
// kParams span, averaged over every P_COUNT param, plus a flat 0.25f if
// the archetypes differ. The archetype bonus matters because two terrains
// can land close in every base value yet still read as structurally
// different places (different carrier/texture roles, different weights) --
// the flat add is a cheap proxy for that without re-deriving stage 1.
float distance(const Terrain& a, const Terrain& b) {
    float sum = 0.f;
    for (int p = 0; p < P_COUNT; ++p) {
        const float span = kParams[p].hi - kParams[p].lo;
        sum += std::fabs(a.base[p] - b.base[p]) / span;
    }
    float d = sum / float(P_COUNT);
    if (a.arch != b.arch) d += 0.25f;
    return d;
}

// NEW draw (spec 7.4): retry fresh masters off the caller-held sequence
// Rng until a candidate clears kDistanceMin against cur, or give up after
// 16 tries and take the farthest-seen candidate -- NEW must always
// terminate even if the taste tables happen to make kDistanceMin hard to
// clear from some cur. Every candidate is a bare master with ALL reroll
// counters zero (NEW replaces the whole terrain; partial reroll is a
// separate gesture that owns the counter vector). generate(cur) is
// computed once and reused for every candidate's distance check.
//
// A drawn master equal to cur.master is skipped (redrawn) but still
// spends one of the 16 tries, so draw_new can never trivially return cur
// unchanged. (If, by a chance short of 1 in 2^32 per try, all 16 draws
// hit cur.master, best stays its default-constructed TerrainState --
// master 1, every reroll 0 -- which is a real but unreachable-in-practice
// edge left uncorrected rather than adding retry-count-inflating logic
// for it.)
TerrainState draw_new(const TerrainState& cur, Rng& seq) {
    const Terrain cur_terrain = generate(cur);
    TerrainState best;
    float best_dist = -1.f;
    for (int try_i = 0; try_i < 16; ++try_i) {
        const uint32_t master = seq.next_u32();
        if (master == cur.master) continue;        // redraw, still a spent try
        TerrainState cand;
        cand.master = master;                       // reroll[] already zero
        const float d = distance(cur_terrain, generate(cand));
        if (d >= kDistanceMin) return cand;
        if (d > best_dist) { best_dist = d; best = cand; }
    }
    return best;
}

} } // namespace spky::flow
