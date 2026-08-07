// engine/flow/terrain.cpp
//
// Stages 0-4 of the terrain generator (spec §4): archetype -> roles ->
// tonality -> base patch -> macro mappings, then the weather layer and the
// named hard constraints. Everything is drawn from per-purpose RNG streams
// (flow_rng.h) so a partial reroll bumps exactly one domain's counters and
// every other draw stays bit-identical. All tuning data lives in taste.h;
// this file is only the plumbing that walks those tables.
//
// THE ADVENTURE LEVELS ARE PER DOMAIN FOR EXACTLY THAT REASON (spec §7,
// corrected 2026-08-06). A first version drew ONE level per terrain keyed on
// reroll_weather_counter(), copying the weather. That broke the guarantee
// above and it was measured breaking it: the weather is an additive layer over
// a finished terrain, but a risk level is an INPUT to every span draw, so
// rerolling one macro re-narrowed the spans every other value came from and
// moved the entire terrain (test_flow_new.cpp's two isolation cases, ~87
// assertions red). Keying it on 0 instead made those green and made "a reroll
// redraws the domain's nerve" false -- the two properties are mutually
// exclusive under a single level. The owner's ruling: one level per macro
// domain keyed on that domain's counter, plus one for the base patch keyed on
// the master alone. Both properties then hold. See Terrain::adventure.
#include "flow/terrain.h"
#include "flow/taste.h"
#include "flow/flow_rng.h"
#include "mod/divisions.h"
#include <cmath>

namespace spky { namespace flow {

// One value inside a span, narrowed toward the middle by the terrain's
// adventure level (spec 2026-08-06 §7). At adv == 1 this is the IDENTITY --
// the whole span, uniform, which is what the taste tables mean on their own --
// and it only ever narrows from there, symmetrically about the span's centre.
//
// That one-sidedness is the load-bearing property, not an implementation
// detail: taste.h's spans are proven at build time to sit inside every veto
// band (tests/test_flow_veto.cpp), so a draw that can never leave its span can
// never breach a veto either, at any adventure level. If this ever grows a
// branch that widens past s, the vetoes lose their build-time proof and need a
// runtime clamp instead.
//
// Consumes exactly one next_unipolar(), like the plain uniform draw it
// replaced, so routing a call site through it shifts no other stream.
float draw_span(Rng& r, const Span& s, float adv) {
    const float w = kAdventureNarrow + (1.f - kAdventureNarrow) * adv;
    const float c = 0.5f * (s.lo + s.hi);
    const float half = 0.5f * (s.hi - s.lo) * w;
    return (c - half) + r.next_unipolar() * (2.f * half);
}

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

// w^(1 - a^kAdventureExp): the taste table's weights as written at a = 0,
// uniform at a = 1. This is what lets an adventurous terrain draw the rung the
// tables call unlikely -- a triplet rate, an 11-step phrase -- without any
// weight ever becoming a veto. Weights only; the SHUFFLE skew is NOT one (see
// stage 3).
//
// The exponent itself is a tuning value and lives in taste.h as
// kAdventureExp, with the owner's ruling and the measured alternatives
// (exponents 1, 3 and 4) recorded there. It used to be an `adv * adv` literal
// here, which put a lever outside the taste tables; moved 2026-08-06 (Task 7).
//
// DISCONTINUOUS AT w == 0 (comment, not a clamp -- flagged in final review,
// left as-is): std::pow(0.f, e) is 0 for any e > 0 but 1 for e == 0, and the
// exponent here is exactly 0 only when adv == 1 (full adventure, the no-op
// case). No current table entry is 0 or 1 -- kModeW's four values and every
// weight table sit strictly inside (0, 1) -- so this is unreachable today. It
// is a live trap for a FUTURE table edit, though: the mode coin computes
// temper(1.f - kModeW[arch], adv), so a hypothetical kModeW of 1.0 would make
// temper(0.f, adv) return 1.0 at adv == 1 instead of 0.0, turning a
// fully-adventurous terrain's mode coin into a 50/50 draw instead of the
// deterministic STEP the table intends. Not clamped here because no value in
// the tables triggers it and a clamp added for an unreached case is an
// untested behavior change; if a future kModeW (or any other temper() input)
// ever reaches 0 or 1, this is where to look first.
float temper(float w, float adv) {
    return std::pow(w, 1.f - std::pow(adv, kAdventureExp));
}

// One adventure level: a = 1 - u^(1/kAdventureShape), so P(a > x) =
// (1-x)^kAdventureShape (spec §7). Consumes exactly one next_unipolar(), like
// the two inline draws it replaces, so factoring this out shifts no stream.
//
// The shape itself is a tuning value and lives in taste.h as
// kAdventureShape (spec §7: "a first guess, tunable later"), the same
// reasoning that moved kAdventureExp there. This helper replaces two
// identical `1.f - std::pow(r.next_unipolar(), 1.f / 3.f)` literals (the base
// patch's draw, and each macro domain's), which is the bare-literal
// duplication the same rule catches -- a future tuning edit to one site could
// otherwise miss the other.
float draw_adventure(Rng& r) {
    return 1.f - std::pow(r.next_unipolar(), 1.f / kAdventureShape);
}

// Snap a normalized rate to a weighted rung of the divisions.h ladder, chosen
// among the rungs that fall inside the drawn span. Free-mode terrains skip
// this entirely -- there is no ladder to snap to.
float snap_rate(Rng& r, float lo, float hi, float adv) {
    int idx[kDivisionCount], n = 0;
    float w[kDivisionCount];
    for (int i = 0; i < kDivisionCount; ++i) {
        const float norm = float(i) / float(kDivisionCount - 1);
        if (norm < lo || norm > hi) continue;
        idx[n] = i; w[n] = temper(kRateRungW[i], adv); ++n;
    }
    if (n == 0) return lo;                  // span narrower than one rung
    return float(idx[pick_weighted(r, w, n)]) / float(kDivisionCount - 1);
}

// Weighted integer step count inside a span. The 1e-4 slack lets a span
// whose endpoint is an integer written as a float still include it.
float snap_steps(Rng& r, float lo, float hi, float adv) {
    int val[kStepsWCount], n = 0;
    float w[kStepsWCount];
    for (int i = 0; i < kStepsWCount; ++i) {
        const int s = i + 2;                        // kStepsW indexes 2..16
        if (float(s) < lo - 1e-4f || float(s) > hi + 1e-4f) continue;
        val[n] = s; w[n] = temper(kStepsW[i], adv); ++n;
    }
    // Span narrower than one step, the mirror of snap_rate's fallback. This
    // hands back a possibly NON-INTEGER lo into a discrete param, which is
    // sane only because apply_param() rounds at the engine boundary (every
    // other discrete base draw relies on that too) -- it is not a step count
    // until it gets there.
    if (n == 0) return lo;
    return float(val[pick_weighted(r, w, n)]);
}

// Draw one story-curve target: each breakpoint uniform inside ITS OWN span,
// then the five values sorted monotone in the story's direction. Direction
// = sign of (bp4 span lo - bp0 span lo); a flat story counts as ascending.
// Sorting (not rejection) enforces monotonicity because neighboring spans
// in taste.h may overlap -- an inversion there is a draw artifact, not a
// story feature.
Curve draw_curve(Rng& r, const CurveRule& cr, float adv) {
    Curve c;
    c.param = cr.param;
    for (int b = 0; b < 5; ++b) {
        c.bp[b] = draw_span(r, cr.bp[b], adv);
        // STEPS is storied (DENSITY owns it), so the weight can only reach the
        // five drawn breakpoints, and only as far as each breakpoint's own
        // span allows: the TOP endpoint prefers 16 and the CENTRE prefers 8,
        // while the bottom endpoint prefers 4 -- the best its {2,4} span
        // holds, not a bug. A knob position between two breakpoints still
        // interpolates between them and can land anywhere. That is the honest
        // limit of weighting a storied discrete; snapping at runtime instead
        // would fight the hysteresis.
        //
        // Same span-narrowing exception as the base-rule redraws (spec §7.1):
        // snap_steps here is handed cr.bp[b].lo/.hi, the RAW breakpoint span,
        // not the draw_span() result computed just above -- adventure reaches
        // the step-count weighting, never the breakpoint span itself.
        if (cr.param == P_STEPS_A)
            c.bp[b] = snap_steps(r, cr.bp[b].lo, cr.bp[b].hi, adv);
    }
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
    // engines may dive). max(bp, floor) preserves the curve's monotone
    // order in either direction.
    //
    // THIS CLAMP ALONE IS NOT ENOUGH, and an earlier version of this comment
    // wrongly said it was. It is keyed on THIS terrain's engine assignment,
    // so it only makes the floor true of one terrain evaluated on its own --
    // and a NEW blend interpolates FILT between two terrains clamped under
    // DIFFERENT assignments, which put a BODY deck as far as -0.5485 for up
    // to 5.99 s of a 6 s ramp. The floor that actually holds at every tick is
    // the runtime one in Flow::recompute_and_push (engine/flow/flow.cpp),
    // keyed on the deck's currently-pushed engine. Keep both: this one keeps
    // a settled terrain honest at generation time (where the runtime guard
    // would silently rewrite curve data instead of catching a bad table).
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
        t.a_carries = a_carries;          // published for §5's staggered switch
        const int carrier = kCarrierEngine[pick_weighted(r, kCarrierW[t.arch], 3)];
        const int texture = kTextureEngine[pick_weighted(r, kTextureW[t.arch], 5)];
        engine_a = a_carries ? carrier : texture;
        engine_b = a_carries ? texture : carrier;
    }

    // The base patch's adventure level (spec 2026-08-06 §7). Keyed on the
    // master ALONE, counter fixed at 0: base parameters belong to no macro
    // domain, so nothing a partial reroll can bump may move them. Each macro
    // domain draws its own level in stage 4, from its own counter.
    //
    // a = 1 - u^(1/kAdventureShape) gives P(a > x) = (1-x)^kAdventureShape:
    // above 0.5 in 12.5% of draws, above 0.8 in 0.8% at the shipped shape
    // (3). draw_adventure() holds the arithmetic; the tunables it needs
    // (kAdventureShape, kAdventureNarrow) live in taste.h with the rest.
    //
    // Drawn HERE, before stage 2, because every draw from stage 2 onward reads
    // it: the scale weights are tempered, the mode coin's weights are tempered,
    // and so is every base span. Its own stream means the position costs no
    // other stage a value -- moving it up past stage 2 (2026-08-07) left every
    // other draw bit-identical for that reason.
    {
        Rng r = make_stream(st.master, kStreamAdventure, 0);
        t.adventure_base = draw_adventure(r);
    }

    // Stage 2: tonality. Scale and root are one draw each -- the scale
    // weighted by kScaleW and tempered by adventure, the root still
    // uniform -- both decks share the single P_SCALE/P_ROOT, so "one scale
    // for both decks" is structural. TUNE/RANGE stay ordinary
    // archetype-conditioned base rules (stage 3, their own streams): kParams
    // gives them no tonality coupling to draw here, and inventing one is
    // listening-loop work, not plumbing.
    static_assert(SCALE_LIST_COUNT == 13,
                  "kScaleW and kParams[P_SCALE].steps must cover the same list");
    int scale, root;
    {
        Rng r = make_stream(st.master, kStreamTonality, 0);
        // Weighted, not uniform (spec 2026-08-07 §3). pick_weighted and
        // pick_index each consume exactly ONE next_unipolar(), so the stream
        // position after this line is unchanged and the ROOT draw below stays
        // bit-identical to the uniform version.
        float w[SCALE_LIST_COUNT];
        for (int i = 0; i < SCALE_LIST_COUNT; ++i)
            w[i] = temper(kScaleW[i], t.adventure_base);
        scale = pick_weighted(r, w, SCALE_LIST_COUNT);   // 0..12
        root  = pick_index(r, kParams[P_ROOT].steps);    // 0..11
    }

    // Stage 3a: operating mode (spec 2026-08-06 §5). Its base-rule row is a
    // placeholder like the ENGINE rows -- the real draw is this weighted coin,
    // taken from the param's OWN stream so it rerolls exactly when a full
    // terrain does. Written as a clean 0/1 so nothing downstream has to guess
    // where the rounding boundary is.
    //
    // Drawn BEFORE the stage-3 loop, not after, because the loop's RATE weight
    // reads it: in free mode there is no ladder to snap to. Its own stream is
    // kStreamParamBase + P_MODE, which the loop never touches (each row seeds
    // its own stream from the master), so the move consumes nothing the loop
    // wanted and leaves every other draw bit-identical.
    {
        Rng r = make_stream(st.master, kStreamParamBase + uint32_t(P_MODE), 0);
        const float w[2] = { temper(1.f - kModeW[t.arch], t.adventure_base),
                             temper(kModeW[t.arch], t.adventure_base) };
        t.base[P_MODE] = float(pick_weighted(r, w, 2));
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
        // P_MODE's placeholder row would overwrite the stage-3a coin, which
        // used to be safe only because stage 3a ran after this loop. It draws
        // from its own stream, so skipping it consumes nothing.
        if (br.param == P_MODE) continue;
        Rng r = make_stream(st.master, kStreamParamBase + uint32_t(br.param), 0);
        const Span& s = br.per_arch[t.arch];
        t.base[br.param] = draw_span(r, s, t.adventure_base);
        // Weighted redraws (taste.h §6). The uniform draw above stands for
        // every other row; these three have a preferred SET, not a range.
        //
        // NONE OF THE THREE BELOW SEE THE ADVENTURE-NARROWED SPAN (spec §7.1's
        // exception, documented there): draw_span() above still runs and its
        // result is thrown away for P_RATE_A/B, P_STEPS_B and P_SHUFFLE --
        // snap_rate/snap_steps and the shuffle skew draw are handed s.lo/s.hi,
        // the table's RAW span, not a narrowed one. So for these three (plus
        // P_STEPS_A below, via draw_curve for DENSITY's "rate" story --
        // five params in total) adventure still reaches the WEIGHT tempering,
        // never the span itself. Defensible for the two discrete sets
        // (RATE/STEPS): a per-rung weight already does what narrowing would.
        // NOT defensible for P_SHUFFLE, which is continuous and gets no
        // narrowing at all -- left unchanged pending the owner's ruling (spec
        // §7.1).
        if (br.param == P_RATE_A || br.param == P_RATE_B) {
            // Only meaningful synced: in free mode RATE is free_hz's continuous
            // curve and there is no ladder. base[P_MODE] is already drawn.
            if (t.base[P_MODE] > 0.5f)
                t.base[br.param] = snap_rate(r, s.lo, s.hi, t.adventure_base);
        } else if (br.param == P_STEPS_B) {
            t.base[br.param] = snap_steps(r, s.lo, s.hi, t.adventure_base);
        } else if (br.param == P_SHUFFLE) {
            // The skew tempers too: kShuffleSkew^(1 - a^kAdventureExp) is the
            // table's skew at a = 0 and decays to 1.0 -- a uniform draw across
            // the span -- at full adventure, which is the same "tables as
            // written, then flat" arc the weights follow. (An earlier version
            // of this comment and the one below still named the SUPERSEDED law
            // kShuffleSkew^(1-a); the code has implemented the squared form
            // since 4624822/3e7944f. Corrected 2026-08-06, Task 7.)
            //
            // Written out rather than as temper(kShuffleSkew, adv), which
            // computes the IDENTICAL number today. The two are not the same
            // claim: temper() is defined on a WEIGHT in a table of weights,
            // and kShuffleSkew is an exponent on the draw itself. They agree
            // only because w^(1 - a^kAdventureExp) happens to be the right law
            // for both, and the moment temper() adopts any other flattening law
            // (a lerp toward the mean weight, a floor under the small ones) it
            // would silently take the skew somewhere meaningless with it.
            const float u = r.next_unipolar();
            const float a = t.adventure_base;
            const float skew = std::pow(kShuffleSkew,
                                        1.f - std::pow(a, kAdventureExp));
            t.base[br.param] = s.lo + (s.hi - s.lo) * std::pow(u, skew);
        }
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
        // This domain's adventure level, from its OWN stream and its OWN
        // counter (spec §7). Rerolling DENSITY redraws DENSITY's nerve and
        // nothing else's; the base patch's level (drawn above, master-keyed)
        // cannot move at all. Its own stream id block, not this macro's story
        // stream, so the nerve does not depend on how many values the curves
        // below happen to draw.
        {
            Rng ra = make_stream(st.master,
                                 kStreamAdventureMacro + uint32_t(m),
                                 st.reroll[m]);
            t.adventure[m] = draw_adventure(ra);
        }
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
            if (picked) {
                mm.story = s;                    // global kStories index
                t.window[m] = sv.arch_window[t.arch];
            }
            for (int tg = 0; tg < sv.n_targets; ++tg) {
                Curve c = draw_curve(r, sv.targets[tg], t.adventure[m]);
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
// the archetypes differ. The archetype term exists because two terrains can
// land close in every base value yet still read as structurally different
// places (different carrier/texture roles, different weights), which the
// base-patch mean alone cannot see.
//
// WHAT THIS ACTUALLY DECIDES, measured rather than assumed: the flat 0.25
// is not a tie-breaker on top of the base-patch term, it OUTWEIGHS it
// outright.
//
// RE-MEASURED 2026-08-06, after P_MODE joined the parameter table (spec
// 2026-08-06 §5.1). The mean is taken over P_COUNT params, so one more param
// changes the denominator (P_COUNT is now 63), and a mode mismatch
// contributes a full 1.0/P_COUNT of its own -- both terms had to be measured
// again rather than carried over. Measurement: 20 000 random terrain pairs
// (masters 2i-1 vs 2i), and 3 000 chained draw_new() calls off an Rng seeded
// 12345. The SAME harness was built and run in a worktree at 651ee2c, the
// commit before P_MODE existed, so the before/after below is one measurement
// pair rather than a comparison against a remembered number:
//
//                     pre-mode 651ee2c     HEAD (with P_MODE)
//   P_COUNT                   62                   63
//   base-patch min        0.0597               0.0588
//   base-patch mean       0.1514               0.1569
//   base-patch max        0.2462               0.2582
//   same-arch pairs        6 603                6 603
//   ...of which clear          1                    2
//   draw_new same-arch    0/3 000              0/3 000
//
// Every one of those movements is arithmetic, and the constants that did NOT
// move are the load-bearing check:
//
//  - The same-archetype pair count is IDENTICAL, and had to be: arch comes
//    from make_stream(master, kStreamArch, 0), a pure function of the master,
//    which appending a param cannot touch. If this number had moved, the
//    measurement would have been wrong.
//  - base[0..61] are likewise untouched -- each param draws from its own
//    kStreamParamBase + id stream (flow_params.h), so appending P_MODE LAST
//    re-seeds nothing earlier. Hence every pair's new value is
//    (sum_62 + mode_term)/63 with mode_term in {0, 1}, and each end of the
//    spread lands exactly where that predicts: min 0.0597*62/63 = 0.0588
//    (the closest pair agrees on mode, so it only felt the denominator),
//    max (0.2462*62 + 1)/63 = 0.2582 (the farthest pair disagrees, so it
//    took the full 1/63).
//  - P_MODE disagrees on 50.1 % of the 20 000 pairs (measured, same run),
//    predicting a mean of (0.1514*62 + 0.501)/63 = 0.1570 against 0.1569
//    measured.
//
// So P_MODE moved the mean by +0.0055 and pushed exactly ONE more
// same-archetype pair over kDistanceMin (1 -> 2 out of 6 603). It did not
// come close to making the base term decide anything, and the archetype term
// still does.
//
// (An earlier version of this comment quoted 0.0711 / 0.1509 / 0.2491 and
// 2 of 6 777 as the "before". Those figures predate the pre-mode baseline --
// they cannot be a valid before, since the pair count is master-determined
// and provably 6 603 at 651ee2c. They are superseded by the table above.)
//
// RE-MEASURED AGAIN 2026-08-06 (Task 7), after the taste tables. Same harness,
// same 20 000 pairs and 3 000 chained draw_new() calls, built and run at EVERY
// commit of the glow-taste-tables branch from a worktree at its branch point
// 4ec5be0 -- so this before/after is a measurement pair, not a remembered
// number:
//
//                    4ec5be0 (branch pt)   89eb461 (HEAD)
//   P_COUNT                  63                 63
//   base-patch min       0.0588             0.0352
//   base-patch mean      0.1569             0.1229
//   base-patch max       0.2582             0.2193
//   same-arch pairs       6 603              6 603
//   ...of which clear         2                  0
//   draw_new same-arch   0/3 000            0/3 000
//
// THE BASE-PATCH TERM SHRANK BY ROUGHLY A FIFTH, and the per-commit sweep says
// where all of it came from: the mean sits at 0.1541-0.1569 at every commit
// from 4ec5be0 through 46cd3e8 (no single table edit moves it by more than
// 0.003) and drops to 0.1230 at c945866 -- THE PER-DOMAIN ADVENTURE DRAW. That
// is what draw_span() does by construction: at adventure a a span is sampled
// only over the fraction kAdventureNarrow + (1-kAdventureNarrow)*a of its
// width, so both draws in a pair are pulled toward their spans' centres and
// |delta| shrinks with them. E[a] is 0.25.
//
// The same-archetype pair count is 6 603 at every one of the thirteen commits,
// as it must be: arch is make_stream(master, kStreamArch, 0), a pure function
// of the master, and no commit on this branch touched kArchWeight. Any table
// measurement that moves this number is wrong before it is interesting.
//
// THE CONCLUSION DID NOT MOVE -- IT GOT STRONGER. Same-archetype pairs
// clearing kDistanceMin went 2 -> 1 at 4624822 (the musical weights) -> 0 at
// 46cd3e8 (the COMP ceiling). NO same-archetype pair in 6 603 now reaches
// kDistanceMin on its base patch alone, so "far enough away" does not merely
// mostly mean "a different archetype", it means exactly that: the flat 0.25
// is the whole decision. Note what this is NOT a claim about -- the base-patch
// distribution still runs to 0.2193, well past kDistanceMin's 0.18, so the
// threshold is comfortably reachable in principle. What was measured is that
// no SAME-ARCHETYPE pair happens to reach it, not that none could.
//
// That may be exactly right for an explore-the-instrument gesture, or it
// may be why a drone never persists across a NEW press on an instrument
// weighted 0.5 toward drone. It is an ear question, logged as an open one
// in docs/superpowers/specs/2026-08-05-flow-listening-notes.md; the two
// knobs that would change it are kDistanceMin and this 0.25. Do not "fix"
// either from the code side.
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
