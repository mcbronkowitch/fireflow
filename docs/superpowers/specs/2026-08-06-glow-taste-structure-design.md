# Glow taste structure — hard rules before listening data

**Date:** 2026-08-06
**Status:** design, approved for planning
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
(§8).

## 2. Rules being encoded

Collected in session, with the veto/bias distinction applied to each. A **veto**
holds under every archetype, every macro position, every weather offset. A
**bias** shifts probability and leaves the value reachable.

| Rule | Kind | Param |
|---|---|---|
| WOBBLE never above 0.25 | veto | `P_REV_MOD` |
| PUSH never above 0.4 | veto | `P_DRIVE` (= `MASTER_DRIVE`, panel label "PUSH") |
| COMP never 0, stays 0.1–0.5 | veto | `P_COMP_A/B` |
| Reverb never fully dry | veto | `P_REVMIX_A/B` |
| Drone LFOs round, never angular | archetype | `P_SHAPE_A/B` |
| Drone density low | archetype | `P_DENSITY_A/B` (storied) |
| Ambient attack/decay long | archetype | already correct in table |
| DIFF mostly 0.6–0.8 | bias | `P_REV_DIFF` |
| SHUFFLE mostly low | bias | `P_SHUFFLE` |
| RATE prefers straight rungs | bias | `P_RATE_A/B` |
| STEPS mostly 8 or 16 | bias | `P_STEPS_A/B` |
| Drones normally have STEP off | archetype | new `P_MODE` |

Two findings changed the shape of the work:

**SHAPE is engine-independent.** `Instrument::set_shape` routes to `ModLane`,
not to any engine. The axis is a waveform morph (`mod/waveforms.h:22`):
sine 0.0 → triangle 0.25 → ramp 0.5 → pulse 0.75 → S&H 1.0. "Round, not
angular" is therefore exactly `0 … 0.25`, and it is mechanical, not taste:
from the ramp upward the lane emits a discontinuity per cycle, which is the
rhythmisation being heard. The table currently draws `{0 … 1}` for all four
archetypes and calls it a wildcard, so three quarters of every drone draw gets
an angular LFO. This is likely the single largest dud source and costs one line
to fix.

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
clamp because it is engine-conditional and this table is engine-independent.
Both get a cross-reference.

**Enforcement is a build-time test, not a runtime clamp** (§7 test 1). A table
that violates a veto goes red; it is not silently corrected. A runtime clamp
stays in place at the end of `recompute_and_push`, after stories, base draws,
weather and blend, but only as the net for what no single table row can show:
`P_REVMIX_A` is targeted by two stories (BRIGHT "dawn" and SPACE "bloom") and
the weather offset moves macros by up to ±0.10.

**Four curves are redrawn rather than clipped**, so no macro loses live travel:

| Story | Target | Was | Becomes |
|---|---|---|---|
| MOTION "orbit" | `P_REV_MOD` | bp3 `.25–.45`, bp4 `.45–.85` | flattens out at `.10–.25`; the seasick end is carried by `P_TIDE` and `P_REV_SMEAR` instead |
| MOTION "orbit" | `P_REV_SMEAR` | bp4 `.5–.8` | raised — smear washes the reverb rather than tearing it |
| DIRT "heat" | `P_DRIVE` | bp4 `.3–.7` | `.25–.40` |
| DIRT "heat" | `P_COMP_A` | bp4 `.5–.75` | `.35–.50` |
| SPACE "bloom" | `P_REVMIX_A/B` | bp0 `.02–.1` | `.08–.15` — intimate means small room, not dry |

`DIRT "heat"`'s `P_COMP_A` breaches the COMP veto at bp2 (`.35–.55`) and bp3
(`.4–.6`) as well, not only bp4; all three are pulled under 0.50 keeping their
relative shape.

### 3.1 Base rule edits

Rules from §2 that are plain span changes, no new mechanism:

| Param | Was | Becomes | Why |
|---|---|---|---|
| `P_SHAPE_A/B` drone | `{0, 1}` | `{0, .25}` | sine…triangle only; from the ramp up the lane emits a per-cycle discontinuity, which is the rhythmisation. Pulse/arp/fragment keep `{0, 1}` — nothing collected says otherwise |
| `P_COMP_B` | `{.3, .6}` | `{.3, .5}` | breaches the COMP veto at the top |
| `P_REV_DIFF` (all four) | `{.4, .8}` | `{.6, .8}` | bias, stated directly |
| `P_SHUFFLE` fragment | `{.1, .5}` | `{.05, .35}` | "mostly low" was given globally; fragment was the only span reaching 0.5. First guess, to confirm by ear |
| `P_STEPS_B` drone | `{2, 6}` | `{2, 16}` | so the 8/16 weight of §6 has something to bite on |

## 4. Archetype window on story curves

`CurveRule` carries only `bp[5]`, so a macro behaves identically under every
archetype and "drone sits sparser" is not expressible. Writing each curve out
four times would quadruple the table with mostly duplicated rows.

Instead each archetype gets a **window onto the story**, not its own curves:

```cpp
struct StoryVariant {
    Macro macro; const char* name;
    Span arch_window[ARCH_COUNT];   // default {0,1} = the whole curve
    int n_targets; CurveRule targets[6];
};
```

The knob still sweeps its full physical travel; only the sampling position is
remapped, `pos = lo + knob * (hi - lo)`.

Initial data: DENSITY "rate" gets drone `{0.0, 0.45}`, every other entry stays
`{0.0, 1.0}`. A drone at full DENSITY lands where an arp sits at half, and
`P_STEPS_A` follows down with it because it lives in the same story — sparse
and fewer steps, which is the musically correct coupling.

Chosen over scaling the output value for two reasons: a window can only produce
values that already appear in the curve's breakpoint spans, so the veto test of
§7 stays valid unchanged (an output scaler above 1.0 could push past a veto and
would have to be folded into that test); and every story except DENSITY keeps
its default, so nothing else changes audibly.

**Accepted consequence:** under drone the DENSITY knob can no longer reach the
story's Q4 risk zone. Dense chaos on a drone can then only come from the
adventure draw (§6), never from the knob. That is intended — the knob should be
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
never meet. This explains the reported restlessness far better than a few
excess triplets.

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

`P_MODE` is a probability, not a range, so it cannot be a normal base rule. It
gets a full-range placeholder row carrying the same comment `P_ENGINE_A/B`
already carries — the row exists so the completeness test has no hole, the real
numbers live in `kModeW` beside it.

## 6. Rung and step weights, and the adventure draw

**Weights, not new value sets.** Once synced, biasing RATE toward straight rungs
needs one weight per ladder rung, archetype-independent:

```cpp
inline const float kRateRungW[kDivisionCount] = {
//  8bar 4bar 2bar 1bar  1/2.  1/2  1/4.  1/2T  1/4  1/8.  1/4T  1/8
    1.0f,1.0f,1.0f,1.0f, .20f,1.0f, .20f, .15f,1.0f, .20f, .15f,1.0f,
//  1/16. 1/8T 1/16  1/16T 1/32
     .20f,.15f,1.0f,  .15f, 1.0f,
};
```

Drawing stays inside the archetype's span; it is weighted rather than uniform,
so crooked rungs stay reachable and become rare. The same pattern applies to
STEPS as a weight vector over 2..16 with 8 and 16 heavy.

For reference, what the current spans hit once synced:

| Archetype | span | rungs reached | crooked |
|---|---|---|---|
| drone | `{0, .25}` | 8/4/2/1 bar, 1/2. | 1 of 5 |
| pulse | `{.3, .6}` | 1/2, 1/4., 1/2T, 1/4, 1/8., 1/4T | 4 of 6 |
| arp | `{.55, .9}` | 1/8., 1/4T, 1/8, 1/16., 1/8T, 1/16 | 4 of 6 |
| fragment | `{.3, .7}` | 1/2 … 1/8 | 4 of 7 |

Drone's `P_STEPS_B {2 … 6}` widens to reach 8 and 16: drones normally have STEP
off, but a drone that does draw the step mode gets the same preferred counts as
everything else.

**The adventure draw is not a control.** It is a property of the draw, not of
the panel — which also keeps the faceplate reducible to hardware. Each terrain
rolls its own from its own RNG stream:

```
a = u³        // u uniform 0..1
```

so `a` sits near 0 most of the time, above 0.5 in roughly 12% of draws and
above 0.8 in under 1%. It comes from the master seed, so it rides inside the
terrain code and adds no state. `u³` is a first guess, tunable later.

`a` acts in two places, neither of which needs new table data:

- **Spans widen.** At `a = 0` only the middle 40% of a span is drawn from; at
  `a = 1` the whole span. Table edges become the outermost permitted value
  rather than the normal case.
- **Weights flatten.** Rung and mode weights are tempered as `w^(1−a)`: as
  written at `a = 0`, uniform at `a = 1`. An adventurous terrain may draw a
  triplet, or a drone in rhythm mode — chaos from places where chaos means
  something musically, not from parameter noise.

**`a` never touches the vetos.** The wildest terrain still gets no WOBBLE above
0.25.

## 7. Tests

Each must be proven RED once before being accepted (project rule: a test that
cannot fail gets fixed).

1. **Veto vs. table** — every base span (all four archetypes) and every story
   breakpoint lies inside `kVetos`.
2. **Veto at runtime** — macro grid × N seeds × weather phases; every pushed
   value inside. This is the one that catches two stories summing on
   `P_REVMIX_A`.
3. **Mode invariant** — no reachable state is "steps without grid";
   `P_MODE` drives both `set_step` and `set_sync` from one value.
4. **Archetype window** — drone DENSITY draws never exceed the story's bp2
   value; every story left at default `{0,1}` draws exactly as today.
5. **Weights** — fixed-seed distribution test with generous bounds (crooked
   rungs below a stated share at `a = 0`), shown non-vacuous in both
   directions.
6. **Adventure** — at `a = 0` all draws fall in the middle 40%; `P(a > 0.5)`
   meets ~12.5% within tolerance.
7. **Completeness** — `test_flow_taste.cpp`'s existing "every param is owned"
   check still passes with `P_MODE` added.

**Gates that will legitimately move**, and must be *re-measured* rather than
nudged until green — this is a real part of the work, not a side effect:

- `test_flow_audio.cpp`'s fixed-seed RMS band (`kFixedSeedRmsMin/Max`), the
  differential NEW-blend level gate (`kBlendSpikeDb`, whose comment records
  only ~1.14 dB of real headroom) and the discrete-churn gate were all measured
  against today's terrain distribution. The mode change alone alters what a
  terrain plays. Their comments state which measurement backed each number;
  the replacements need the same treatment.
- `kHouseCode` is already flagged in-source as a placeholder to be re-chosen by
  ear once Glow could be played. After these changes the current code sounds
  different. It gets re-chosen — by ear, which is now possible.

`tests/check_render_hash.cmake` is unaffected: those gates hang off the engine
core, not the flow layer.

## 8. Out of scope — the second spec

Genre correlations ("ambient drone" vs "dub rhythm" as coupled parameter sets)
are **not** hand-written here. Hand-writing them is exactly where an author's
own habits would narrow the instrument to music already imagined.

They come from a tuning log, specified separately:

- Gestures are the labels, no rating UI. `new_partial(macro_mask)` already
  means "this domain was wrong"; LOCK already means "keeping this".
- Macro adjustments after a NEW are logged as deltas — direction and amount,
  which a thumbs-down cannot give.
- Dwell time before the next NEW is the continuous positive signal, capped
  around ten minutes. A burst of fast NEWs is a rejection streak and labels
  every terrain in it, which covers the "found nothing" case without a button.
- An explicit keeper lives in the VCV **right-click menu**, never on the panel —
  host-side tooling, invisible to firmware, and no violation of the
  reducible-to-hardware constraint.
- The log must store **resolved parameter values, not terrain codes.** A code is
  `(master seed, reroll counters)`; the sound only exists through the tables, so
  every code captured today means something else after any table edit.

`P_MODE` is the natural root for those genres: a terrain is an ambient flow or
a rhythm piece, and most other correlations hang off that choice.
