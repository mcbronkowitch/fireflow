# Glow taste structure — hard rules before listening data

**Date:** 2026-08-06
**Status:** design, approved for planning (revised after independent review)
**Scope:** `engine/flow/taste.h`, `engine/flow/flow_params.h`, `engine/flow/terrain.cpp`,
`engine/flow/flow.cpp`, `tests/test_flow_taste.cpp`, `tests/test_flow_audio.cpp`

## 1. Why

Glow ships (v3.0.0) but its taste tables are, by their own header comment,
"listening-phase first guesses". The render-based listening pass was stopped on
2026-08-05 because a bounced WAV can only be judged for level, not for music.
The instrument can now be played, so tuning can start — but a rating loop is
worth nothing while a large share of NEW presses are broken for reasons that
are already known and stateable.

This spec covers only the stateable part: the by-ear rules Bastian can name,
plus the table mechanisms needed to express them. Learning the correlations he
*cannot* name — the genre presets — is deliberately deferred to a second spec
(§9).

## 2. Rules being encoded

Collected in session. Three kinds, and the distinction is load-bearing:

- **Veto** — holds under every archetype, every macro position, every weather
  offset, every adventure level. Lives in `kVetos` (§3), enforced by test.
- **Weight** — shifts probability, leaves the value reachable (§6).
- **Span** — an archetype's draw range. Narrowing one makes values
  *unreachable*; it is a veto in everything but name, so it is labelled as a
  span here and never as a "bias".

| Rule | Kind | Param |
|---|---|---|
| WOBBLE never above 0.25 | veto | `P_REV_MOD` (panel label "WOBL") |
| PUSH never above 0.4 | veto | `P_DRIVE` (= `MASTER_DRIVE`, panel label "PUSH") |
| COMP never 0, stays 0.1–0.5 | veto | `P_COMP_A/B` |
| Reverb never fully dry | veto | `P_REVMIX_A/B` |
| Drone LFOs round, never angular | span | `P_SHAPE_A/B` |
| Drone density low | span (via window) | `P_DENSITY_A/B` (storied) |
| Ambient attack/decay long | — | already correct in table |
| DIFF mostly 0.6–0.8 | span | `P_REV_DIFF` |
| SHUFFLE mostly low | weight | `P_SHUFFLE` |
| RATE prefers straight rungs | weight | `P_RATE_A/B` |
| STEPS mostly 8 or 16 | weight | `P_STEPS_A/B` |
| Drones normally have STEP off | weight | new `P_MODE` |

SHUFFLE takes the weight treatment rather than a narrowed span: a fragment with
heavy shuffle is plausibly chaos worth keeping, so it stays reachable and
becomes rare. DIFF is a plain span narrowing — 0.4–0.6 is simply not wanted.

Two findings reshaped the work:

**SHAPE is engine-independent.** `Instrument::set_shape` routes to `ModLane`,
not to any engine. The axis is a waveform morph (`mod/waveforms.h:22`):
sine 0.0 → triangle 0.25 → ramp 0.5 → pulse 0.75 → S&H 1.0. "Round, not
angular" is therefore exactly `0 … 0.25`, and it is mechanical rather than
taste: from the ramp upward the lane emits a discontinuity per cycle, which is
the rhythmisation being heard. The table currently draws `{0 … 1}` for all four
archetypes and calls it a wildcard, so three quarters of every drone draw gets
an angular LFO. This is likely the single largest dud source and costs one line.

Because no collected rule needs it, **no per-engine span layer is specified
here.** It remains a hypothesis for spec 2 to confirm or drop.

**Glow is stuck in the one mode combination nobody wants.** See §5.

## 3. Veto layer

A new table in `taste.h`:

```cpp
struct Veto { int param; float lo, hi; };
inline const Veto kVetos[] = {
    { P_REV_MOD,  0.00f, 0.25f },
    { P_DRIVE,    0.00f, 0.40f },
    { P_COMP_A,   0.10f, 0.50f },
    { P_COMP_B,   0.10f, 0.50f },
    { P_REVMIX_A, 0.08f, 1.00f },
    { P_REVMIX_B, 0.08f, 1.00f },
};
```

This table is **not** the complete list of hard by-ear limits, and its comment
must say so: `P_RES`'s 0.75 ceiling stays in `kParams` because it also
normalises the terrain distance metric, and `kBodyFiltFloor` stays a runtime
clamp because it is engine-conditional while this table is engine-independent.
Both get a cross-reference.

**Enforcement is a build-time test** (§8 test 1). A table that violates a veto
goes red; it is not silently corrected.

**The runtime clamp exists for exactly one mechanism**, and the earlier draft
named the wrong one. Shared story targets do **not** sum: `Flow::eval_terrain`
(`flow.cpp:266–280`) picks the single candidate farthest from the terrain base,
winner-takes-all. Weather only shifts `_eff`, which is clamped to 0..1
(`flow.cpp:343–344`) before curve evaluation, so it still samples inside the
drawn spans. Neither can breach a veto if test 1 passes.

The blend residual can. `flow.cpp:394–396` computes

```cpp
v = clamp_to(kParams[p], prv[p] + (cur[p] - prv[p]) * ph + _resid[p] * (1.f - ph));
```

— clamped to the **kParams** range, not the veto band, and the comment directly
above states that the sum can exceed a parameter's range even when both
terrains' candidates sit inside it. The clamp is applied after this line, at
the end of `recompute_and_push`, before the change guard and before `_pushed`
is written — `param_now()` is a public observer and must never show a vetoed
value.

> **Corrected 2026-08-06, during task 2 implementation.** This section
> originally said a macro moved *during a blend* can push REVMIX under 0.08
> with both terrains legal, full stop. That is false as a general claim, and
> the task 2 build proved it two ways before writing the clamp's test: on a
> fresh press from a settled terrain, `_resid` is computed as
> `_cont_now[p] - _cand_cur[p]`, and both are the *same value from the same
> tick* — so `_resid` is exactly zero, and the combine line above reduces to
> `prv[p] + (cur[p] - prv[p]) * ph`, a plain convex combination of `prv[p]`
> and `cur[p]`. Both are always inside the veto band (that is what test 1
> enforces at build time), and a convex combination of two in-band points
> cannot leave the band — so a *single* press, however fast or extreme the
> macro sweep, cannot breach a veto. This was checked empirically too: 300
> masters, a full 6 s sweep of every macro at full travel, zero breaches.
> `_resid` only goes nonzero when NEW is pressed **again mid-flight** — "a
> re-press lands mid-flight" is `begin_blend`'s own description of the case.
> With a nonzero residual, a macro moved during that second ramp *can* push
> the sum outside the veto band even though both terrains are legal, because
> the residual term is clamped to `kParams` only. That re-press is the one
> and only mechanism the runtime clamp exists for; a plan or reviewer that
> reaches for a single-press repro to justify simplifying the clamp away is
> working from the error this note replaces.

**Four curves are redrawn rather than clipped**, so no macro loses live travel:

| Story | Target | Was | Becomes |
|---|---|---|---|
| MOTION "orbit" | `P_REV_MOD` | bp3 `.25–.45`, bp4 `.45–.85` | flattens out at `.10–.25`; the seasick end is carried by `P_TIDE` and `P_REV_SMEAR` instead |
| MOTION "orbit" | `P_REV_SMEAR` | bp4 `.5–.8` | raised — smear washes the reverb rather than tearing it |
| DIRT "heat" | `P_DRIVE` | bp4 `.3–.7` | `.25–.40` |
| DIRT "heat" | `P_COMP_A` | bp2 `.35–.55`, bp3 `.4–.6`, bp4 `.5–.75` | all three pulled under `.50`, relative shape kept |
| SPACE "bloom" | `P_REVMIX_A/B` | bp0 `.02–.1` | `.08–.15` — intimate means small room, not dry |

### 3.1 Base rule edits

| Param | Was | Becomes | Why |
|---|---|---|---|
| `P_SHAPE_A/B` drone | `{0, 1}` | `{0, .25}` | sine…triangle only. Pulse/arp/fragment keep `{0, 1}` — nothing collected says otherwise |
| `P_COMP_B` | `{.3, .6}` | `{.3, .5}` | breaches the COMP veto at the top |
| `P_REV_DIFF` (all four) | `{.4, .8}` | `{.6, .8}` | span narrowing, 0.4–0.6 not wanted |
| `P_STEPS_B` drone | `{2, 6}` | `{2, 16}` | so the 8/16 weight of §6 has something to bite on |

## 4. Archetype window on story curves

`CurveRule` carries only `bp[5]`, so a macro behaves identically under every
archetype and "drone sits sparser" is not expressible. Writing each curve out
four times would quadruple the table with mostly duplicated rows.

Instead each archetype gets a **window onto the story**, not its own curves:

```cpp
struct StoryVariant {
    Macro macro; const char* name;
    int n_targets; CurveRule targets[6];
    // Where each archetype reads this story. Default = the whole curve.
    Span arch_window[ARCH_COUNT] = {{0.f,1.f},{0.f,1.f},{0.f,1.f},{0.f,1.f}};
};
```

The new member goes **last, with a default initialiser**, so every existing
positional entry in `kStories` keeps compiling untouched and only the rows that
need a window state one.

The knob still sweeps its full physical travel; only the sampling position is
remapped. The runtime samples `_eff[m]` (knob + CV + weather, clamped 0..1 at
`flow.cpp:343–344`), so the mapping is

```
pos = lo + eff * (hi - lo)
```

— CV and weather ride inside the window like the knob, rather than bypassing it.

Initial data: DENSITY "rate" gets drone `{0.0, 0.45}`, every other entry keeps
the default. A drone at full DENSITY lands where an arp sits at half, and
`P_STEPS_A` follows down with it because it lives in the same story — sparse
and fewer steps, which is the musically correct coupling.

Chosen over scaling the output value because a window can only produce values
that already appear in the curve's breakpoint spans (the breakpoints are walked
in ascending order in `draw_curve`), so the veto test of §8 stays valid
unchanged. An output scaler above 1.0 could push past a veto and would have to
be folded into that test.

**Accepted consequence:** under drone the DENSITY knob can no longer reach the
story's Q4 risk zone. Dense chaos on a drone can then only come from the
adventure draw (§7), never from the knob. That is intended — the knob should be
dependable, the dice should surprise.

## 5. Operating mode

`RATE` has been quantised all along, and Glow never uses it.

`divisions.h` holds a 17-rung musical ladder for SYNC mode (`8 bars … 1/32`,
speed-sorted so dotted and triplet rungs sit *between* the straight ones), with
`division_index(norm) = round(norm × 16)` — "1 bar" is `norm 0.1875`, "2 bars"
`0.125`. But `SuperModulator::_synced` defaults to `false` and **Glow never
calls `set_sync`**; only `Fireflow.cpp:734` does, from the big panel's switch.
Glow therefore runs `free_hz(norm)`, a continuous 0.02–30 Hz exponential with
no ladder at all. "1 bar" is not rare in Glow, it is unreachable.

Second hardcode: `flow_params.h:107` passes a literal `true` —

```cpp
case P_STEPS_A:  in.set_step(PART_A, true, i); break;
```

— so **both decks are permanently in STEP mode.** The big module has a per-deck
STEP toggle (`Fireflow.cpp:714`); Flow does not. The two modes are musically
distinct (`mod/lane.cpp:162`): in FLOW, RATE is the cycle rate and the texture
lanes run their own `kLaneRatio` relationships; in STEP, the PITCH lane is the
deck's only clock and the texture lanes are followers.

Glow thus gets the one combination neither side wants: **a step sequencer with
no grid** — two decks stepping stubbornly at unrelated free-running rates that
never meet.

**Design: one drawn mode per terrain**, two coupled states.

```
FLOW / free    lanes breathe in their own ratios, no grid
STEP / synced  step sequencer on the musical ladder
```

The coupling is not only taste — it is forced. `instrument.h:274`'s `set_sync`
is **global**, applying to `_center` and both parts at once, so a per-deck mode
would need SYNC simultaneously on and off. One global `P_MODE` (2 steps) is the
result, replacing any separate `P_STEP_A/B` and sync parameter.

Archetype weights, `kModeW[ARCH_COUNT]` = probability of STEP/synced:

| drone | pulse | arp | fragment |
|---|---|---|---|
| 0.15 | 0.90 | 0.95 | 0.75 |

**Accepted cost:** in the rhythm mode there is no freely breathing deck left. A
drone-like deck beside a beat gets a very slow step clock (8 bars, long attack)
rather than free-running lanes — similar, not identical, since its texture
lanes become followers. The escape hatch, if this proves wrong by ear, is
per-deck STEP with global SYNC, which is three states instead of two.

### 5.1 Where P_MODE sits in the enum — load-bearing

Base draws are keyed `kStreamParamBase + uint32_t(param)` (`terrain.cpp:160`).
`P_MODE` must therefore be **appended at the tail** of `SPKY_FLOW_PARAMS`.
Inserting it anywhere else shifts the RNG stream of every parameter after it,
and every existing terrain code — including `kHouseCode` and every saved
patch — resolves to a different draw.

`P_MODE` is a probability, not a range, so it cannot be a normal base rule. It
gets a full-range placeholder row carrying the same comment `P_ENGINE_A/B`
already carries — the row exists so the completeness test has no hole, the real
numbers live in `kModeW` beside it.

### 5.2 P_MODE during a blend

`P_MODE` belongs to no deck, so the existing stagger (`flow.cpp:288–292`,
texture at phase 0, carrier at `kCarrierStaggerFrac`) does not describe it: a
global `set_sync` flips **both** decks' rate mapping at whatever instant it
fires. Riding the carrier would mean the texture deck's entire clocking jumps
1.5 s after its own duck had closed — precisely what the stagger exists to
prevent.

> **Corrected 2026-08-06, after implementation.** The bullets below originally
> said a mode change *collapses* the stagger — that both decks duck together at
> phase 0 "instead of the carrier's duck firing later", and that both ducks then
> sit inside `kBlendGateWindowS`. That design was built, escalated as **Critical**
> during review, and replaced: collapsing the stagger leaves the carrier deck's
> `P_ENGINE` switch — which stays at `kCarrierStaggerFrac`, because the stagger is
> a by-ear decision the project owner re-affirmed — unducked in the open at
> `press + 1.5 s`, where `duck()` computes `u = 6` and hands the send back
> untouched. What ships is a *second* carrier duck, not a moved one. The gate
> claim was wrong in the same breath and is corrected below. This section now
> describes `flow.cpp`'s `begin_blend`; do not "restore spec compliance" from an
> older copy.

A mode change is a whole-terrain event, not a per-deck one — but it is **two**
events on two different schedules, so it adds a duck rather than moving one:

- `P_MODE` switches at **phase 0**, with the texture deck.
- The clocking flip is global. `set_sync` hits both decks at the press, and the
  texture deck's only duck is already there — so the **carrier deck gets a
  second duck at the press** to cover it. Its first duck stays where it was.
- The **stagger survives**. The carrier's own engine/scale switch still lands at
  `kCarrierStaggerFrac * kBlendS` = 1.5 s, still under the duck already
  scheduled there. Collapsing the stagger would trade the clocking flip's
  exposure for the carrier's *engine* change happening in the open, which is the
  louder of the two.
- So a mode-changing press schedules three ducks in total: one on the texture
  deck, two on the carrier. Two per deck is therefore the exact maximum, not a
  guess (`kDucksPerDeck`).
- `duck()` combines a deck's slots by **maximum**. Overlapping ducks can never
  dig deeper than a single duck, so the depth `kDuckDepth` was tuned to still
  bounds the gesture. (Today the two carrier windows are 0.5 s wide and 1.5 s
  apart and do not in fact overlap; the maximum is the guarantee, not the
  observation.)
- **Gate coverage, precisely:** the carrier's *press-instant* duck falls inside
  `kBlendGateWindowS` (1.0 s), so for the first time the level gate sees a
  carrier-deck event on this path. The carrier's *stagger* duck does not — it
  spans roughly 1.25–1.75 s and is still structurally outside the window, for
  every seed. The authority on that, with the measurements, is the
  `kBlendGateWindowS` comment in `engine/flow/taste.h`.

`ModLane::set_step` on entering step mode can set `_song.new_pending`
(`lane.cpp:156–158`), so a mode flip regenerates phrase material. That happens
under the press-instant ducks by design, and the plan must confirm it by ear.

### 5.3 Routing — apply_param cannot express this

`apply_param` (`flow_params.h:95`) is stateless per parameter. `P_MODE` needs
the current step counts (`set_step` takes mode *and* count) and `P_STEPS_A/B`
need the current mode, so neither can stay a generic case.

The three move out of `apply_param` into a small helper owned by `Flow`, which
holds `_pushed[]` and therefore has all three values at once. It pushes
`set_sync` and both `set_step` calls together, so no tick can ever observe
"steps without grid". `apply_param` keeps handling everything else unchanged.

## 6. Rung, step, shuffle and mode weights

Once synced, biasing RATE toward straight rungs needs one weight per ladder
rung, archetype-independent:

```cpp
inline const float kRateRungW[kDivisionCount] = {
//  8bar 4bar 2bar 1bar  1/2.  1/2  1/4.  1/2T  1/4  1/8.  1/4T  1/8
    1.0f,1.0f,1.0f,1.0f, .20f,1.0f, .20f, .15f,1.0f, .20f, .15f,1.0f,
//  1/16. 1/8T 1/16  1/16T 1/32
     .20f,.15f,1.0f,  .15f, 1.0f,
};
```

Drawing stays inside the archetype's span; it is weighted rather than uniform,
so crooked rungs stay reachable and become rare. The same pattern covers STEPS
(weights over 2..16, 8 and 16 heavy), SHUFFLE (weights favouring the low end,
so a heavy-shuffle fragment stays possible) and `P_MODE` (`kModeW`).

For reference, what the current spans hit once synced:

| Archetype | span | rungs reached | crooked |
|---|---|---|---|
| drone | `{0, .25}` | 8/4/2/1 bar, 1/2. | 1 of 5 |
| pulse | `{.3, .6}` | 1/2, 1/4., 1/2T, 1/4, 1/8., 1/4T | 4 of 6 |
| arp | `{.55, .9}` | 1/8., 1/4T, 1/8, 1/16., 1/8T, 1/16 | 4 of 6 |
| fragment | `{.3, .7}` | 1/2 … 1/8 | 4 of 7 |

## 7. The adventure draw

Not a control. It is a property of the draw, not of the panel — which also
keeps the faceplate reducible to hardware. Each terrain rolls its own:

```
a = 1 - u^(1/3)        // u uniform 0..1
```

`P(a > x) = (1 - x)³`, so `a` sits above 0.5 in 12.5% of draws and above 0.8 in
0.8%. Braver terrain is the rule, outliers the exception. (The earlier draft
wrote `u³` alongside these percentages; that is a different variable — `u³`
exceeds 0.5 in 20.6% of draws. The formula above is the one that matches.) The
`(1-x)³` shape is a first guess, tunable later.

> **Corrected 2026-08-06, after implementation.** This section originally said
> `a` was drawn **once per terrain**, from a stream keyed by
> `reroll_weather_counter()` — the sum of all six macro counters — "exactly as
> the weather already is". That was built, and it is wrong: it contradicts the
> per-domain isolation the terrain-state design guarantees (spec 7.3 of
> `2026-08-05-flow-machine-design.md`, and `terrain.h`'s own opening comment),
> and the two cannot both hold under a single level. The analogy to the weather
> is what misled: the weather is an **additive layer over** a finished terrain,
> so rerolling it moves only the weather, whereas a risk level is an **input
> to** every span draw and every tempered weight. Keyed on the counter sum, a
> partial reroll re-narrowed the spans that every *other* value had been drawn
> from and moved the whole terrain — measured, ~87 assertions red across
> `test_flow_new.cpp`'s two isolation cases. Keying it on nothing instead made
> those green and made this section's own reroll promise false. The project
> owner ruled on the split below; do not restore the single-level version.

**Stream and reroll.** A terrain draws **seven** adventure levels, not one.

- `adventure[m]`, one per macro domain, from that macro's own stream keyed on
  **that macro's own reroll counter**. It applies to every curve that macro's
  stories draw. Rerolling DENSITY therefore redraws DENSITY's nerve — a wild
  DENSITY does not stay wild in the domain the player explicitly asked to be
  redone — and redraws no other domain's.
- `adventure_base`, from the master **alone** with a fixed counter, applying to
  the base patch and the mode coin. Base parameters belong to no macro domain,
  so nothing a partial reroll can bump is allowed to move them at all.

Both properties then hold at once: §7's "a reroll refreshes that domain's
nerve", and 7.3's "a partial reroll touches only its domain". The per-macro
levels take their own stream id block rather than reusing `kStreamMacroBase +
m`, which is already the domain's story stream — sharing it would make the
nerve a function of how many values the curves happened to draw.

**Scope.** `a` applies to base-rule spans and story breakpoint spans (§7.1) and
to every weight table of §6 (§7.2). It does **not** apply to the archetype
window of §4, which is a musical range statement rather than a risk setting.

### 7.1 Spans narrow

At `a = 0` a span is sampled only in its middle 40%; at `a = 1` in full. Table
edges are the outermost permitted value rather than the normal case.

### 7.2 Weights flatten

> **Corrected 2026-08-06, after implementation.** This section originally
> specified `w^(1−a)`. That was built and measured, and it flattens far too
> eagerly to mean what §6 says: `E[a]` is 0.25, so the *typical* terrain already
> draws at `w^0.75`, which lifts a 0.15 triplet weight to 0.24 and took the
> crooked-rung share from 0.189 to **0.255** — past §6's "straight rungs win
> more than four draws out of five", and past the test that encodes it. The
> owner ruled for `w^(1−a²)`: the flattening should bite only on genuinely
> brave terrains, which is what "chaos when you press NEW, aber eben seltener"
> asked for in the first place.

Rung, step, shuffle and mode weights are tempered as `w^(1−a²)`: as written at
`a = 0`, uniform at `a = 1`, and — because of the square — still essentially as
written for the median terrain, which sits at `w^0.96`. The flattening arrives
with the rare brave draw: `a = 0.5` gives `w^0.75`, `a = 0.9` gives `w^0.19`. An
adventurous terrain may draw a triplet, or a drone in rhythm mode — chaos from
places where chaos means something musically, rather than parameter noise.

The shuffle skew tempers on the same schedule, `kShuffleSkew^(1−a²)`, reaching
1.0 (a uniform draw across the span) at full adventure. It is written out rather
than routed through the weight helper: the skew is an exponent on the draw, not
a weight in a table, and the two agree only because `w^(1−a²)` happens to suit
both.

**Measured after the correction** (4000 masters, the Task 5 fixed-seed sets):
crooked-rung share **0.213**, step counts on 8/16 **0.475**, shuffle mean
position **0.303**. The step and shuffle gates hold. The crooked-rung gate at
`< 0.20` does **not**, and it is a genuine tension rather than a weak exponent:
the untempered tables already sit at 0.189, so that bound leaves any tempering
at all only +0.011 of room, while `a²` costs +0.024. Measured alternatives —
`a³` gives 0.203, `a⁴` gives 0.196 — so nothing short of a fourth power fits
under it. Whether the bound moves or the exponent does is an ear question and is
open; it is recorded here rather than settled by widening the test.

**`a` never touches the vetos.** The wildest terrain still gets no WOBBLE above
0.25.

## 8. Tests

Each must be proven RED once before being accepted (project rule: a test that
cannot fail gets fixed).

1. **Veto vs. table** — every base span (all four archetypes) and every story
   breakpoint lies inside `kVetos`.
2. **Veto at runtime, through a blend** — the recipe must include what the
   clamp is actually for: press NEW, then move macros *during* the ramp, across
   seeds and weather phases, asserting every `param_now()` inside the veto band.
   A static macro grid alone would never exercise the residual path of §3 and
   would be a test that cannot fail.
3. **Mode invariant** — no observable tick has steps without grid; `P_MODE`
   drives `set_sync` and both `set_step` calls from one push (§5.3).
4. **Archetype window** — drone DENSITY draws never exceed the story's bp2
   value; with `a` forced to its no-op value, every story left at the default
   window draws exactly as today. (Without that clamp the statement is false —
   §7.1 narrows every span.)
5. **Weights** — fixed-seed distribution test with generous bounds (crooked
   rungs below a stated share at `a = 0`), shown non-vacuous in both directions.
6. **Adventure** — at `a = 0` all draws fall in the middle 40%, and at `a = 1`
   the whole span (the no-op), asserted from *both* sides so "narrowed to the
   middle 40%" is distinguishable from "narrowed to nothing"; the drawn
   distribution meets `P(a > 0.5) ≈ 12.5%` and `P(a > 0.8) ≈ 0.8%` within
   tolerance, measured on a macro domain's level as well as the base level so a
   per-domain level that was never drawn cannot pass. Plus the reroll rule of
   §7 in both directions: rerolling a domain changes that domain's level and
   leaves every other level, including `adventure_base`, bit-identical.
7. **Enum tail** — `P_MODE` is the last entry of `SPKY_FLOW_PARAMS`, asserted
   statically, with the stream-key reason in the message (§5.1).
8. **Completeness** — `test_flow_taste.cpp`'s existing "every param is owned"
   check still passes with `P_MODE` added.

**Gates that will legitimately move**, and must be *re-measured* rather than
nudged until green — this is a real part of the work, not a side effect:

- `test_flow_audio.cpp`'s fixed-seed RMS band (`kFixedSeedRmsMin/Max`), the
  differential NEW-blend level gate (`kBlendSpikeDb`, whose comment records only
  ~1.14 dB of real headroom) and the discrete-churn gate were all measured
  against today's terrain distribution. The mode change alone alters what a
  terrain plays. Their comments state which measurement backed each number; the
  replacements need the same treatment.
- `distance()` and `kDistanceMin`. Adding `P_MODE` changes the mean's
  denominator, a mode mismatch contributes its own term, and §7.1's narrowing
  shrinks the typical base spread. The measured commentary at
  `terrain.cpp:241–257` (base-patch mean 0.1509; "same-archetype accepted 0
  times in 3000") goes stale and must be re-measured, or NEW's rejection
  behaviour changes silently.
- `kHouseCode` is already flagged in-source as a placeholder to be re-chosen by
  ear once Glow could be played. After these changes the current code sounds
  different. It gets re-chosen — by ear, which is now possible.

`tests/check_render_hash.cmake` is unaffected: those gates drive the render host
over engine scenarios, not the flow layer.

### 8.1 Saved patches change — not a constraint

Every saved Glow patch, locked ones included, plays a different piece after this
work: `Glow::dataToJson` persists a terrain code, and the sound exists only
through the tables. FireFlow is in dev alpha and patch compatibility is
explicitly not a concern yet, so this needs no migration, no versioning and no
further design attention.

What *does* still matter is determinism within a version — hence the enum-tail
rule of §5.1.

## 9. Out of scope — the second spec

Genre correlations ("ambient drone" vs "dub rhythm" as coupled parameter sets)
are **not** hand-written here. Hand-writing them is exactly where an author's
own habits would narrow the instrument to music already imagined.

They come from a tuning log, specified separately:

- Gestures are the labels, no rating UI. `new_partial(macro_mask)` already means
  "this domain was wrong"; LOCK already means "keeping this".
- Macro adjustments after a NEW are logged as deltas — direction and amount,
  which a thumbs-down cannot give.
- Dwell time before the next NEW is the continuous positive signal, capped
  around ten minutes. A burst of fast NEWs is a rejection streak and labels
  every terrain in it, which covers the "found nothing" case without a button.
- An explicit keeper lives in the VCV **right-click menu**, never on the panel —
  host-side tooling, invisible to firmware, and no violation of the
  reducible-to-hardware constraint.
- The log must store **resolved parameter values, not terrain codes**, for the
  same reason §8.1 breaks saved patches.

`P_MODE` is the natural root for those genres: a terrain is an ambient flow or a
rhythm piece, and most other correlations hang off that choice.
