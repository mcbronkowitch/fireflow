# By-ear values harvested from `engine/flow/taste.h`

`engine/flow/taste.h` and the whole `engine/flow/` layer it belongs to are
being deleted (see `docs/superpowers/plans/2026-08-14-flow-glow-removal`).
Formally the values below are terrain-draw rules for the Glow macro layer.
In substance several of them are claims about where FireFlow sounds good,
arrived at by the owner listening rather than by reasoning from the code —
and two pieces of future work want them back: the M6 hardware panel needs
ranges, and the planned "Marbles round" needs to know which axes were
deliberately constrained and why.

**None of this is in the boot patch, and none of it should be promoted
there without its own listening round.** These are terrain-generator draw
spans and weights, not init values on any control. The repo's control-merge
init trap (`fireflow-gotchas`) has already cost four regressions from
exactly this move — merging or promoting a control silently changes the
factory sound unless the stored init value is converted to match. Treat
every span below as "what the generator was told to reach for," not as
"what to set anything to by default."

This note does not restate FLOW-the-lane-run-mode (`_flow_melody`,
`set_flow_melody`, `Instrument::set_flow`) anywhere — that is instrument
core and is not going away. Everything here is the terrain/Glow layer.

---

## 1. Wants a hardware panel range

Values that assert "this parameter's *usable* travel is narrower than its
engine range," which is exactly the shape of fact an M6 panel needs.

### 1.1 Hard by-ear vetoes (`kVetos`)

Spec 2026-08-06 §3: "these hold under EVERY archetype, every macro
position, every weather offset and every adventure level. A row here is a
claim that no music in this box ever wants that value."

| param | band | what it asserts |
|---|---|---|
| `P_REV_MOD` | 0.00–0.25 | above this the reverb tail comes apart |
| `P_DRIVE` | 0.00–0.40 | above this the limiter rides and DRIVE stops controlling dirt — it only gets louder |
| `P_COMP_A` / `P_COMP_B` | 0.40–0.60 | see §1.2, the COMP band move |
| `P_REVMIX_A` / `P_REVMIX_B` | 0.08–1.00 | never fully dry |

Two more hard limits live outside this table on purpose and are *not*
duplicated here, per taste.h's own note: `P_RES`'s 0.75 ceiling is in
`flow_params.h` (it also normalises the terrain distance metric), and
`kBodyFiltFloor` / `kBbdFlowRangeMax` are runtime clamps in `flow.cpp`
because they're conditional on which engine a deck is running. All three
still express the same kind of claim and are equally worth carrying to a
panel design; they are just not in this file.

### 1.2 The COMP band move

`P_COMP_A`/`P_COMP_B` started life in Glow's table at 0.10–0.50. The owner
plays per-deck COMP at ≥ 0.5 by ear, and that band never let it draw that
high — COMP carries the makeup gain, so a table that undershoots where the
owner actually sets the knob is why Glow read quiet. It was first moved to
0.40–0.70 (same width discipline: never uncompressed, never squashed), then
the owner **listened to 0.70 and ruled it back down to 0.60** ("Ja passt
eher 0.6" — 0.70 read as over-compressed). The 0.40 low end and the
"COMP carries makeup gain" reasoning both still hold; 0.60 is a by-ear
ceiling on top of them, not a relocation of the band. A later session
measured that about 3.4 dB of the level the owner still wants is
unaccounted for after this move — the file explicitly says not to chase it
by raising the ceiling back to 0.70; that has already been ruled out by
ear.

### 1.3 The drone SHAPE cap, and the coupling finding (carry verbatim)

`kBaseRules`: `P_SHAPE_A`/`P_SHAPE_B` draw `{0, .25}` for the drone
archetype (all other archetypes keep the full `{0,1}`). SHAPE morphs
sine(0) → tri(.25) → ramp(.5) → pulse(.75) → S&H(1); from the ramp up, the
lane emits a per-cycle discontinuity and a drone reading that reads as
rhythmic rather than sustained — the cap keeps a drone a drone.

**Carried verbatim, as the brief requires:** the drone SHAPE cap is what
retired the `kCalmCornerRmsMax` ceiling breach at master 0x707 *and* what
mutes master 0x404. Anyone reinstating one gets the other. (Full isolation
data for this finding is in §3.1 below, where the coupled constants live.)

### 1.4 Archetype-shaped base rules with real musical assertions

These are ordinary `kBaseRules` rows, not vetoes, but each one encodes an
explicit archetype-vs-parameter claim that a panel or a Marbles axis would
want to know about:

- **`P_COLOR_A`/`P_COLOR_B` (chord size, not timbre).** `ChordBuilder::set_color`
  counts tones over fixed zone edges, so these spans are read as tone
  counts, not a continuous tint: drone .45–.80 → 3 tones reaching 4 ("pads
  want the weight"), pulse .20–.55 → 2 reaching 3, arp .10–.45 → 1 to 3
  ("an arpeggio may legitimately be a single line"), fragment .05–.50, the
  widest, "because broken material may be anything." A single tone stays
  reachable for arp/fragment but is meant to be *rare*, not the standing
  state — an earlier design let COLOR fall permanently into "one note" on
  over half of all terrains, which the file treats as the defect these
  rows fixed.
- **`P_DRIVE` archetype spread**, alongside the veto in §1.1: drone stays
  clean (0–.10), pulse/arp move up to .20/.25 (arp reaches exactly the
  point `limiter.h` calls the end of "clean"), fragment reaches .30 — one
  archetype step into the "gentle" dirt range, deliberately, because
  fragment is the archetype that's supposed to sound broken.
- **`P_TEMPO_BPM`**: drone 55–75, pulse 80–110, arp 90–130, fragment
  70–110 — slow drones, faster arps, by archetype.
- **`P_REV_DIFF`** (reverb density, the one reverb-character axis that
  still has a Fireflow knob per the existing reverb-mod-split record):
  base rule narrows every archetype to 0.6–0.8. The comment is explicit
  that 0.4–0.6 "is simply not wanted" — a hole in the middle of the range,
  not a weight. This is new detail on top of the already-recorded
  reverb-mod-split finding (DIFF is the one with a knob; SMEAR and MOD/WOBL
  are constants) — it says *where in DIFF's own range* the owner doesn't
  want to land.

### 1.5 First-pass, explicitly *not yet* by ear — do not treat as settled

`P_TIDE`'s base-rule spans (the four texture lanes' rate scale, x1/4..x4,
geometric) carry their own warning in the file: "FIRST-PASS VALUES, set by
arithmetic and not yet by ear... worth checking against the ear before
these are treated as settled." If a hardware panel or Marbles round wants
a TIDE range, it should re-derive it, not inherit this one as if it had
been auditioned.

`kCarrierW` / `kTextureW` (the per-archetype engine-role weights) are
labelled "listening-phase first guesses" in their own header comment —
same caveat: not yet confirmed by ear, don't inherit as settled fact.

`kHouseCode` (the terrain the instrument wakes on) is explicitly a
PLACEHOLDER, not a curated choice — the listening pass that would have
chosen it by ear was stopped because a bounced render file can only judge
level, not the played result. It is not a by-ear value at all yet; it's
a note that one is still owed.

---

## 2. Input to the Marbles round

Values that describe *which axes were deliberately constrained and how*,
rather than a single number a panel would show — the shape a "Marbles"
axis-selection round would want to read before deciding what to expose.

### 2.1 Scale draw weights (`kScaleW`)

Spec 2026-08-07 §3. A uniform draw over all thirteen scales put whole
tone, hijaz, phrygian and harmonic minor together at 31% of terrains —
stated in the file as "most of why Glow read dissonant." The fix grades
scales by friction, measured off `SCALE_MASKS` rather than by feel: minor
and major pentatonic contain neither a minor second nor a tritone; every
seven-note mode contains both (a property of seven notes in twelve, not a
choice among modes); whole tone has no minor second but three tritones.
True shares after normalisation: modes 40.9%, clean pentatonic 31.8%, the
pygmy/hirajoshi/kumoi bucket 18.2%, exotic 9.1%. The owner ruled
(2026-08-07) to keep the weights exactly as shipped rather than
renormalise to sum to 1 — mathematically a no-op either way, so there was
nothing to gain by moving the numbers. This is a genuine musical claim
(these scales are less mutually dissonant when two sustained voices land
on them at once) and is exactly the kind of axis-friction knowledge the
Marbles round would want as a starting prior.

### 2.2 Musical weights (rungs, steps, shuffle)

Spec 2026-08-06 §6. Weights, not vetoes — the unlikely values stay
reachable, just rare:

- **Rate rungs** (`kRateRungW`): straight rungs weigh 1.0, dotted 0.20,
  triplet 0.15.
- **Step counts** (`kStepsW`, counts 2–16): 8 and 16 are the ones actually
  played (weight 1.0); a second, separately-named rule in the same table
  is that even counts beat odd ones across the board — odd counts sit at
  .05 uniformly while even leftovers (2/6/10/14) sit at .15/.20/.15/.10, a
  2–4× preference. Stated reasoning: "a phrase whose length does not
  halve reads as a mistake against everything else on the clock."
- **Shuffle skew** (`kShuffleSkew = 2.5`): SHUFFLE has no rungs, so its
  bias is a power-law skew toward the low end of its drawn span, applied
  so a heavily-shuffled fragment stays reachable (a narrowed span would
  have killed it instead).

These are all tempered per-domain by the adventure draw (§2.3) — at
adventure 1 the table reads uniform, so none of these are hard limits, just
a shape at rest.

### 2.3 The adventure draw shape (`kAdventureNarrow`, `kAdventureExp`, `kAdventureShape`)

Spec 2026-08-06 §7: not a control, a property of the draw itself, so NEW
occasionally surprises and the panel gains no dedicated knob.
`kAdventureNarrow = 0.40` sets how tight the middle-of-span sampling is at
adventure 0. `kAdventureExp = 2` is an explicit owner ruling (2026-08-06):
exponent 1 would put the *typical* terrain (mean adventure 0.25) at w^0.75,
lifting a 0.15 triplet weight to 0.24 — too much routine flattening.
Squaring instead puts the mean terrain at w^0.94 and the median at w^0.96
(essentially the tables as written), and only lets flattening bite on a
rare, brave draw. Quoted by the owner as "chaos when NEW is pressed, aber
eben seltener." `kAdventureShape = 3` sets the draw distribution itself,
`a = 1 − u^(1/3)`, called out in the spec as a first guess, tunable later.
The applied mechanism (which of these numbers governs which domain) lives
in `terrain.cpp` and dies with it; the numbers and the owner's reasoning
for them are what this note preserves.

### 2.4 Archetype draw weights and the archetype window mechanism

`kArchWeight = {0.5 drone, 0.2 pulse, 0.15 arp, 0.15 fragment}` — drone-
heavy, stated as following directly from the spec ("this is an ambient
box"), not from a separate listening pass on the weights themselves.

The archetype-window mechanism (`StoryVariant::arch_window`, default the
full `{0,1}` per archetype) is the general form of "some archetypes only
read part of a macro's story." The one live example: `M_DENSITY`'s "rate"
story narrows the drone archetype's window to `{0, .45}` — "a drone at
full DENSITY lands where an arp sits at half," i.e. a drone never reaches
the story's own busy end. This mechanism, more than the one number, is
what the Marbles round would want: it is the general shape of "which
archetypes get a smaller slice of which axis," independent of Glow's
specific macros.

---

## 3. Closed, recorded only — so it is not re-litigated

Values where the owner already ruled, the ruling is final, and the risk is
a future session re-deriving a "fix" from first principles.

### 3.1 The calm-corner level (`kCalmCornerRmsMin` / `kCalmCornerRmsMax`)

**`kCalmCornerRmsMax = 0.06`** (§7.8 ceiling, lin FS) is a spec number, not
a measured one — "it says how loud the calm corner is allowed to be, so it
is never fitted to what the generator happens to produce." A breach at
master 0x707 was retired without moving this constant. Per-commit
isolation (calm-corner render, macros 0, first 3s skipped, run at every
commit of the branch from worktree point `4ec5be0`):

```
4ec5be0 .. ab76a97   0x707 rms 0.0824 -> 0.0787   OVER
3435c31 (base rules) 0x707 rms 0.00665            under
89eb461              0x707 rms 4.25e-04           under
```

Isolated by reverting each of commit `3435c31`'s three table edits in turn
and re-rendering, with 0x404 carried in the same run (see the coupling
finding, §1.3):

```
3435c31 as shipped            0x707 6.645e-03  0x404 7.00e-08
drone SHAPE cap reverted      0x707 9.920e-02  0x404 1.35e-03
DIFF narrowing reverted       0x707 6.689e-03  0x404 6.93e-08
drone STEPS_B widening rev.   0x707 2.561e-02  0x404 7.00e-08
```

Reverting the SHAPE cap alone puts 0x707 back over the ceiling; reverting
either other edit leaves it well under. **One span, isolated by reversion —
and the same one span is what silences 0x404.** (This is the coupling
finding, carried verbatim per the brief; see §1.3 for the drone SHAPE cap
itself.) The remaining population-level breach rate (0.51% of 1,566
non-Sampler terrains at HEAD) was ruled ACCEPTED by the owner 2026-08-07,
not eliminated — the ceiling is a property the generator holds for
~99.5% of terrains, not a guarantee.

**`kCalmCornerRmsMin = 1e-5`** (§7.8 floor) is explicitly a silence
*detector*, not a musical target — it exists only to catch a calm corner
that has gone functionally mute by accident, not to state where quiet
should sit. The musical question it sits next to *is* answered, though,
and is the by-ear content worth carrying: the owner signed off
(2026-08-07) that the measured spread of calm-corner levels across
terrains — roughly −105 to −31 dBFS, a ~74 dB range driven by archetype
and per-terrain draws — is **character, not error**: "a sparse drone
terrain legitimately recedes further than a busy one." The ruling that
carries: **the calm corner is not a level the generator normalises to,**
and no normalisation stage should be added on the grounds that the corner
reads as "inconsistent" — the inconsistency is the signed-off behaviour.
Separately, roughly 1 terrain in 15 currently renders functionally mute
at its calm corner; that fraction is also ruled ACCEPTED (owner,
2026-08-07) as a known property of the generator, not a tracked defect.

### 3.2 The BBD flow bend budget (`kBbdFlowSemis = 1`)

Spec 2026-08-07 §2. In FLOW (the lane run mode, not this layer), a BBD
deck's PITCH lane isn't a note, it's the delay clock; a full lane travel
would be 5 octaves (60 semitones) against a scale-locked second deck. This
constant caps that travel to 1 semitone under Glow specifically — at that
cap the clock is effectively static, and the wobble that lane used to
contribute has to come from DRIFT/MOTION/FLUX instead. That trade-off —
static clock vs. off-key travel — was stated explicitly and **the owner
ruled for it, 2026-08-07.** This is a Glow-layer-specific constraint,
distinct from the already-recorded `fireflow-bbd-range-cap-is-flow-only`
finding (that one is about STEP mode legitimately running 87× over this
same kind of cap at the engine level, by design). Both are about the same
mechanism family (BBD range/pitch travel limits) but are separate rulings
at separate layers; if the layer is reinstated, this specific 1-semitone
number and its rationale should come back with it, not be re-derived.

### 3.3 Reverb character restated from elsewhere

MOTION story (`P_DRIFT`/`P_REV_SMEAR`/`P_REV_MOD`): SMEAR "carries the
seasick end now that WOBL is capped" — WOBL here is `P_REV_MOD`, capped at
its veto (§1.1, 0–0.25) and flattening rather than stopping dead as the
MOTION macro rises. This restates and slightly extends the existing
`fireflow-reverb-mod-split` record (DIFF is the one control with a
Fireflow knob; SMEAR and MOD/WOBL are constants "confirmed by ear") — the
new detail here is *how* MOD is used within Glow specifically (as a
capped, flattening tail inside the MOTION story) rather than a new
decision.

### 3.4 CHOKE and PACE are structural, not by-ear

`P_CHOKE`'s base rule is a degenerate `{0,0}` span (draws nothing; the row
exists only so the overlay walk has a destination) because CHOKE's inhibit
is binary at every stage in the engine — magnitude is never consulted,
only sign and `amt > 0` — so there is no gradient for a draw to sit on at
all. This is a structural fact about the parameter, not a listening
decision, and it should not be read as one. The actual by-ear CHOKE states
are already recorded elsewhere (`fireflow-by-ear-decisions`,
`fireflow-choke-silences-a-deck`) and are not restated or extended here.
`P_PACE`'s base rule (`{0.5,0.5}`, "exactly x1") is the same shape of
non-value for the same structural reason (a story-owned PACE would throw
away a transferred patch's own speed) and carries no separate by-ear
content either.

---

## Inventory check

Every scalar/table in `taste.h` with an explicit "by ear," "owner ruled,"
or "owner listened" marker is accounted for above. Constants that are
gesture/UI timing (`kBlendS`, `kMinSpan`, `kTapMaxS`, `kUndoArmS`,
`kLockS`, `kMarkDelta`, `kRefuseFlashS`), weather config
(`kWeatherDepthMin/Max`, `kWeatherPeriodMinS/MaxS`, `kWeatherOscMin/Max`),
or test-gate calibration (`kCalmMuteFracMax`, `kCalmLoudFracMax`,
`kBlendSpikeDb`, `kBlendDropDb`, `kBlendSpikeBreachFracMax`,
`kBlendDropBreachFracMax`, `kBlendGateWindowS`, `kFixedSeedRmsMin/Max`,
`kDiscreteChurnMax`, `kSpaceSlewS`, `kHysteresisFrac`,
`kCarrierStaggerFrac`, `kDuckWindowS/WetTarget/Depth`) are deliberately
left out of this note: they are measured, spec-derived, or test-tolerance
numbers, not claims about where the instrument sounds good, and dressing
them up as by-ear content here would be inventing a rationale the file
doesn't give them.
