# Flow Machine — compact macro module (design)

**Date:** 2026-08-05
**Status:** approved design, pre-plan
**Scope:** a portable macro/terrain layer over the engine plus a new compact
VCV module, FireFlow Glow. The hardware expansion, FireFlow Forge, and the M6
panel itself are out of scope (doors kept open, nothing more).

## 1. Idea

A "flow machine": endless, always-evolving drones and pads from a module with
six knobs and one button. Every knob position sounds pleasantly ambient by
construction. **NEW** rolls a fresh "terrain" — a new generated instrument to
explore — while the knobs keep their fixed meanings. The existing full
Spotymod panel remains untouched as the full-control view — FireFlow Forge's
ancestor; the later hardware expansion is a future product, not part of this
design.

Decisions fixed during brainstorming:

- VCV prototype first; the macro layer must be portable so M6 firmware reuses
  it unchanged.
- 6 knobs + NEW. Fixed macro semantics; NEW rerolls the terrain underneath.
- The A/B duo is invisible: macros drive both decks; NEW also rolls the deck
  roles. The player only ever sees one instrument.
- I/O: stereo out, clock in, CV over five of the six macros (all but
  WANDER).
- Panel drawn at true hardware dimensions (see §6) so the VCV faceplate
  doubles as the 1:1 draft for the M6 panel.
- An independent creative review (2026-08-05) reshaped the first draft: it
  found the terrains would converge on "mid-density pleasant mush" because
  every parameter was an independent draw from one safe distribution, and it
  named PACE a dead set-and-forget knob. Folded in below: terrain archetypes
  (§4 stage 0), partial reroll and blend retargeting (§5), WANDER replacing
  PACE (§3), bounded risk zones and deep mappings (§3), and a minimum
  terrain distance for NEW (§4).
- A second, musician-perspective "polisher" review (2026-08-05, spec read
  cold) found the instrument undefined at its two most important moments:
  power-on and minute forty of unattended running. It also caught that
  "hold a knob + NEW" is physically impossible (pots don't sense touch; a
  mouse can't hold two things). Folded in below: the weather layer (§4
  stage 5), the house seed (§5), the reworked NEW gesture family (§5),
  RST cut in favor of CV SPACE (§6), terrain lock as a panel gesture (§5),
  and the rename VARY → WANDER.
- A third round (2026-08-05, owner-driven) generalized the macro math:
  mappings became quadrant **story curves** with a curated story library,
  and the **calm corner** became a named, tested guarantee — the module can
  always be ridden down into polite background behind other instruments
  (§3). The former ad-hoc rules (risk zones, the MASTER_DRIVE threshold
  stage, the calm floor) are now ordinary cells of the story table.
- A final neutral review (2026-08-05, whole-spec coherence pass) found the
  seams the rounds had left: partial reroll broke the single-seed terrain
  identity, reroll domains were undefined, discrete setters could chatter
  under weather/CV, the NEW state machine had open states, and persistence
  contradicted §8. All fixed below; it also set the implementation split
  (§2, two plans, flow layer first).

## 2. Architecture

New portable component **`engine/flow/`** — pure logic on the public setters
of `engine/instrument.h`. No hardware or Rack type crosses in. (The setter
surface is *mostly* normalized `0..1`, but not uniformly: `set_variation`,
`set_choke` and the voice FILT are bipolar, and engine/scale/root/FORM/SONG/
STEPS are discrete — `taste.h`'s schema carries real ranges and types, and
nothing in this spec may assume uniform `0..1`.)

```
engine/flow/
  terrain.h/.cpp   seed → full patch + macro mapping table (deterministic)
  taste.h          taste rules: allowed ranges & named constraints per param
  flow.h/.cpp      runtime: holds terrain + 6 macro values, computes final
                   params, runs the NEW crossfade, calls the setters
```

Consumers:

- **VCV module FireFlow Glow** — second module in the Spotymod plugin. Embeds
  the same `Instrument`, talks only to `flow.h`.
- **Render host** — drives the flow layer from `scenario.json` (seed + macro
  rides), so terrains are testable and auditionable without Rack.
- **M6 firmware (later)** — reads 6 pots + 1 button, calls the same `flow.h`.

The flow layer encapsulates nothing away: it calls the same public setters as
any host. A full-control expansion (FireFlow Forge) later just sets
parameters directly.

**Implementation split (final-review recommendation): one spec, two plans,
strict order.** Plan A builds `engine/flow/` + render-host integration + the
§7 tests — the listening loop happens there, without Rack. Plan B builds the
VCV module (panel, wiring, gestures, persistence). Plan B does not start
until Plan A's calm corner and house seed have survived a listening pass:
the panel is trivial by comparison and the tuning data will churn.

**Determinism is owned, not borrowed.** "Same seed → same terrain on every
host" only holds with a portable PRNG and portable transcendentals — libm's
`sinf` differs across MSVC, glibc and the ARM toolchain. `engine/flow/`
ships its own PRNG and its own sine/interpolation approximations.

## 3. The six macros

Six macros. Each has a fixed one-word meaning; *what* it touches in detail
is the terrain's choice — always inside that meaning, always monotone (more
knob = more of it). Five carry a CV jack (all but WANDER, §6).

| Macro | Meaning | Typical target pool (terrain picks subset + spans) |
|---|---|---|
| **MOTION** | how much everything moves | TIDE, DRIFT, mod-lane depths, REV SMEAR/WOBL, FLUX amount |
| **DENSITY** | how much happens | deck DENSITY, STEPS activity, COLOR (chord density) |
| **BRIGHT** | spectral center | FILT of both decks, REV TONE, SUB balance, engine-specific timbre params |
| **DIRT** | clean ↔ driven | GRIT + COMP per deck, MASTER_DRIVE (threshold-aware, see below) |
| **WANDER** | predictable ↔ wandering | melody variation (MELODY), FORM/SONG movement, sequence drift, STEPS pattern churn |
| **SPACE** | close ↔ vast | reverb sends of both decks + SIZE/DECAY on one coupled path |

Notes on individual macros:

- **WANDER replaces the first draft's PACE.** Tempo is not a ridden gesture
  in ambient; it is rolled by the terrain (archetype-conditioned, §4) and the
  CLK input overrides it. WANDER is the "hold this pattern… now let it
  wander" knob — and it is the one place rhythm and melody surface on the
  panel: subordinate, but reachable. (The polisher review renamed it from
  VARY: next to MOTION, "vary" and "move" read as the same sentence;
  WANDER points at the notes going elsewhere.)
- **DENSITY has two story variants, and the terrain picks one** — a
  rate-led story (events/steps carry the sweep) and a thickness-led story
  (chord/pad density carries it). "One dominant interpretation per terrain"
  and the story library are the same rule seen from two sides: the variant
  *is* the interpretation. The two are different musical intentions and are
  never blended inside one terrain.
- **SPACE stays safe to whip:** the send portion follows the knob directly;
  SIZE/DECAY follow through a lazy slew, so fast rides throw the sends
  without lurching the room.

**Macro math — replace, not add, and every knob tells a story.** A macro's
mapping is a set of target parameters, each with a **story curve**: a
piecewise-linear curve over a shared quadrant grid (breakpoints at 0, ¼, ½,
¾, 1). Piecewise-linear is already continuous — there is nothing to
crossfade at the seams; quadrant character comes from targets *entering and
leaving* at breakpoints (a target's segment can be flat), never from any
mode switch. Per target the curve is monotone and stays inside the taste
limits at every breakpoint; unmapped parameters sit at their terrain base
value. Every knob position is therefore inside the allowed region by
construction — the "always pleasant" guarantee is arithmetic, not hope.

**Discrete targets quantize with hysteresis.** FORM, SONG, STEPS count,
COLOR chord count and their kin are integer setters living inside continuous
story curves fed by knob + CV + weather. Each discrete target quantizes its
curve value with a hysteresis band of half a step, so a sum hovering at a
threshold switches once, not every control tick. (Named constraint; band
width per target is tuning-phase data.)

What the quadrants buy: different targets *enter and leave* in different
quarters of the travel, so a knob does something recognizably different at
its bottom than at its top without any parameter ever reversing direction.
BRIGHT's bottom quarter can dip the level and bloom the reverb decay while
the filters darken — the instrument recedes into a wash — and none of that
leaks into the upper three quarters of the sweep. ("Level" names its
mechanism: there is no per-deck level setter, so the dip rides the dry leg
of the per-deck reverb send — equal-power, so receding *is* washing out.)

**Story library, not free-form dice.** The terrain generator does not invent
quadrant compositions; it picks per macro from the hand-written story
variants in `taste.h` and varies only intensities. That keeps the tuning
load tractable and the knobs learnable — "down is always calmer" is true on
every terrain. **v1 ships the table below: one variant per macro, except
DENSITY's two.** Growing each macro to 2–3 variants is a named
listening-phase deliverable, not silent future work. The starting library:

| Knob | Q1 "background" | Q2 | Q3 | Q4 "edge" |
|---|---|---|---|---|
| **BRIGHT** | ember: dark, level dips, decay blooms — recede into wash | classic cutoff sweep | open | air: SUB/timbre sheen joins |
| **DENSITY** (rate-led) | held drone only, long gaps | events/steps wake | steps quicken | overflow, busy weave |
| **DENSITY** (thickness-led) | held drone only, thin | pad thickens | voices stack | chord density, bloom |
| **MOTION** | still photo: weather becalmed, LFOs near-frozen | slow breathing | audible wobble | risk zone: seasick edge |
| **DIRT** | clinically clean, COMP as glue | warmth/saturation | grit | risk zone + the MASTER_DRIVE threshold |
| **WANDER** | pattern frozen, exact loop | fine variation | melody wanders in scale | FORM/SONG churn: the sequence reinvents itself |
| **SPACE** | intimate, near-dry | room | hall | dissolve: SIZE/DECAY bloom, dry ducks slightly |

Three rules that used to be ad-hoc are now just cells of this table:

- **The calm corner (named constraint, tested).** All six knobs at full CCW
  — the Q1 column combined — is a defined background state on *every*
  terrain: quiet, dark, sparse, near-static. The generator must choose all
  mapping floors so the CCW corner lands there; §7 pins it with an RMS
  ceiling in the render smoke. This is the "gas pedal": pull down any
  subset of knobs and the instrument recedes toward that corner; every knob
  upward adds a specific kind of energy. The *tested* guarantee is the full
  corner (all six at 0, weather becalmed by MOTION at 0) — with MOTION up,
  weather may sit up to its small offset above the other macros' floors,
  which is the intended trade. (Externally, one CV fanned into CV DEN +
  CV BRT rides the same pedal under voltage.)
- **Edges are Q4 behavior.** The top quarter of DIRT's and MOTION's travel
  may exceed the taste-range centers into a bounded risk zone — still
  clamped, still safe from genuine breakage. "Always pleasant" becomes
  "always recoverable"; pulling back from almost-too-much is where ambient
  play lives. Zone widths are tuning-phase data.
- **Depth mandate.** Every macro keeps its minimum audible span, but the
  terrain must map at least one or two macros *deep* (wide spans, many
  parameters). Six uniformly shallow knobs are the samey-nice failure in
  knob form.

Guard rails inherited from project memory:

- **By-ear values become range centers, never get overwritten.** The tuned
  resonance cap, reverb gains and CHOKE states define the safe zones the
  terrain and macros move inside.
- **DIRT respects that MASTER_DRIVE is a threshold:** the macro primarily
  rides the deck GRITs; MASTER_DRIVE only joins in DIRT's top quarter (its
  Q4 story cell), because beyond the limiter's knee it stops controlling
  dirt.
- **BRIGHT never walks a BODY deck off the FILT cliff:** BODY dies below
  FILT −0.5, so a BODY deck's BRIGHT mapping floor sits clearly *above* the
  cliff, not merely near it — a knob that silences a deck at one end is a
  broken knob in disguise.

**CV:** MOTION, DENSITY, BRIGHT, DIRT and SPACE each get one CV input,
additive onto the knob value, hard-clamped to `0..1` — the guarantee
survives any input. A hot CV source can legitimately hold a macro in its Q4
risk zone with the knob at 0; then "always recoverable" lives in the patch,
not on the panel — the patcher's choice, accepted. SPACE earned its jack in
the polisher review: an envelope into the reverb send is the first patch a
modular ambient player tries, a *performance* move where drive-under-CV is
merely a sound-design one.

## 4. Terrain generator

**Terrain identity (final-review fix).** A terrain's full state is
**(master seed, override vector)** — not a bare 32-bit seed, because partial
reroll (§5) replaces parts of it. Every drawn value gets its RNG stream
derived from `(master seed, parameter id, override counter)`, so rerolling
one domain bumps only its own counters and never shifts any other stream.
A fresh NEW is the special case of an empty override vector. What the
features reference:

- **Sharing/menu:** a compact **terrain code** string encodes master seed +
  override vector; the context menu shows and accepts codes, not bare seeds.
- **Undo:** stores the full previous state (code), not a seed.
- **House seed:** a plain code with an empty override vector.
- **Partial-reroll domain:** the terrain-chosen targets of the turned macro
  — their stage-3 base values, their story curve, and that macro's stage-4
  variant pick. A target shared between macros (STEPS sits under both
  DENSITY and WANDER) belongs, for the reroll, to the macro being turned;
  the other macro's curve simply applies to the new base afterwards.
- **That macro's adventure level** (`2026-08-06-glow-taste-structure-design.md`
  §7), added 2026-08-06. It is part of the domain, not a terrain-wide value:
  each macro draws its own risk level from its own reroll counter, and the base
  patch draws one from the master alone. Written down here because the
  isolation guarantee above is what forced it — a single per-terrain level
  keyed on the sum of the counters was built first and broke this section, since
  a risk level is an *input to* every span draw rather than a layer over the
  finished terrain, so rerolling one macro re-narrowed the spans every other
  value came from. Anything later that reads "per terrain" must not become a
  draw input without re-checking this rule.

Fully deterministic; same state → same terrain on every host (see §2's
owned-PRNG rule). Generation order:

0. **Archetype.** Drone / pulse / arp / fragment, weighted toward drone.
   This is the correlation structure that keeps terrains from converging on
   mid-density mush: the archetype conditions the engine weights, the taste
   distributions of stage 3, and the macro-pool selection of stage 4.
   Whether a terrain *is* a static drone, a pulsing swell or a slow arp is a
   decision, not an accident of independent draws. (Archetype weights and
   per-archetype ranges are tuning-phase data.)
1. **Roles.** Which deck carries, which textures. Engine choice per role with
   archetype-conditioned weights (carrier prefers Synth/Body/Wave; texture
   may also take Sampler/BBD). Never two "loud" engine combinations.
2. **Tonality.** Scale, root, TUNE/RANGE — both decks always share one scale.
3. **Base patch.** Every remaining parameter drawn from the (archetype-
   conditioned) taste ranges, honoring the named hard constraints (the BODY
   FILT cliff; the resonance cap; no double high density on both decks).
   Tempo base is drawn here too — pace belongs to the terrain, not to a
   knob; the CLK input overrides it. CLK follows the big Spotymod module's
   clock convention (same pulses-per-beat and tempo derivation); when the
   clock stops, tempo falls back to the terrain's own after a short timeout.
   WANDER's sequence targets are tempo-independent — an external clock
   changes *when* steps fall, never how much they wander.
4. **Macro mappings.** Per macro one story variant from the §3 library plus
   its intensities: which targets participate, and the breakpoint values of
   each target's story curve. The variant record and the per-breakpoint draw
   ranges live in `taste.h` — sketching that schema is the implementation
   plan's first task. Three rules: every macro must be audible — a minimum
   span per mapping, so no knob is ever dead; at least one or two macros per
   terrain are mapped deep (§3 depth mandate); and the mapping floors must
   jointly satisfy the calm corner (§3).
5. **Weather.** The polisher review's one-hour finding: LFO-class motion at
   constant depth is how ambient becomes wallpaper — after fifteen minutes
   the ear has mapped the orbit and files it under air conditioning. So each
   terrain also rolls 2–4 very slow deterministic wander functions (periods
   in the 5–20 minute range: summed incommensurate slow sines or a seeded
   low-rate random walk — no storage, pure functions of sub-seed and time)
   that feed small offsets (±0.05–0.10) into the macro summing points,
   exactly like internal CV cables. Because CV is additive and clamped
   (§3), the pleasant-guarantee and the player's knob dominance survive
   untouched. Weather depth scales with MOTION — read at the *pre-weather*
   sum (knob + external CV), so weather never modulates its own depth and
   no feedback loop exists. Weather's time origin is the moment its
   sub-seed came alive (the terrain roll); across a NEW blend, old and new
   weather offsets crossfade with the same blend ramp. Weather never
   changes the terrain state — distance metric, partial reroll and undo
   are unaffected. Depths and periods are tuning-phase data.

Generation is a one-shot draw of ~100 values — cheap, but it runs at
control rate spread over a few blocks (or off the audio thread where one
exists), never as a burst inside the audio callback.

**NEW must land audibly elsewhere.** A cheap terrain-distance metric
(weighted normalized-parameter + archetype distance) rejects candidate seeds
too close to the current terrain, draw-and-retry, deterministic given the
seed sequence. The rejection rule is spec; the threshold and the shaping
behind it are listening-loop data. A 6-second blend into a near-identical
terrain is the most deflating thing this instrument could do.

**Taste rules are data, not code.** `taste.h` is a table: parameter →
min/max/distribution, plus named constraints. The prototype phase's
listening loop consists of tightening or widening this table, not of
rebuilding generator code.

## 5. NEW behavior

**Power-on — the house seed.** The wake-up state is a curated terrain code
(or tiny fixed pool), chosen during the listening phase to be the best first
impression the instrument can make; NEW starts the journey from there. For
an instrument whose stated goal is "switch it on and be inspired", the first
sound is the most important terrain of all — it is not left to a random
draw. (Polisher-review finding: the first draft never defined it.)

**Three kinds of "power-on" (final-review fix):** *first insert* (VCV) and
*first hardware boot* wake on the house seed. *Patch reload* (VCV
`dataFromJson`) and later hardware boots restore the full saved state —
current terrain code, lock, and the undo slot; a live set built around a
locked terrain must survive a restart. §8's storage line means preset
*systems*, not this baseline persistence.

**Tap — a transition, not a cut:**

- Continuous parameters ramp to their new targets over a blend time of
  roughly 6 s (a tuning-phase value, not a constant); the flow layer rides
  the normalized setters and the engine declicks itself (the v2.18.2
  declick round carries this).
- Discrete switches (engine, scale, root) switch once, at the start of the
  blend, under a short duck of the affected deck. The decks switch staggered
  (texture first, then carrier) so both never jump at once.
- The six knobs keep their physical positions and act on the new terrain
  immediately. No value jumps, because macros map absolutely (§3).
- **Re-press during the blend** retargets from the current interpolated
  state — mashing NEW is a legitimate wander gesture, not undefined
  behavior.

**One button, one gesture family.** The first draft's "hold a knob + NEW"
was physically impossible — pots don't sense touch, and in VCV one mouse
cannot hold two things (polisher-review catch). The whole family now lives
on the NEW button itself; all thresholds are tuning-phase data:

| Gesture | Action | LED |
|---|---|---|
| tap (release below ~0.4 s, no knob turned) | full NEW | breathes through the blend |
| hold + *turn* a macro knob | marks that macro's domain; release fires a **partial reroll** of the marked domain(s) via its RNG streams (§4) — hold NEW and turn BRIGHT: new timbre, same tonality, roles and pace | flickers on mark |
| hold past ~1.5 s, no knob turned | arms **undo**; release blends back to the one remembered previous terrain state (no deeper history) | double-pulse, reversed feel |
| hold past ~5 s, no knob turned | toggles **terrain lock** — tap/hold gestures ignored until unlocked the same way; protects live use, exists on the panel because hardware has no context menu | solid while locked |

State-machine rules that close the corners (final-review fix):

- **Turning a knob wins.** The moment a knob turns during a hold, armed
  undo and the lock timer are cancelled — the hold is a partial-reroll
  gesture from then on, however long it lasts. Undo and lock only ever fire
  from a *clean* hold.
- **Marking moves the knob, and that is accepted.** Macros map absolutely,
  so the marking wiggle simply lands wherever it lands — the reroll fires
  from the knob's new position. The "turned" threshold is a small travel
  delta (tuning-phase data), large enough that a resting finger doesn't
  mark.
- **Any reroll during a running blend** (tap or partial) retargets from the
  current interpolated state — same rule as re-press.
- **While locked,** tap and hold gestures produce a short refusal blink;
  only the clean ~5 s hold (unlock) does anything.

**VCV context menu** (VCV-only niceties, nothing essential): show the
current terrain code, enter a code manually (share terrains), and the same
terrain lock as a convenience mirror of the panel gesture.

## 6. VCV module FireFlow Glow — panel

Own compact module in the Spotymod plugin, **12 HP (61 mm)**, drawn at true
hardware dimensions so the faceplate doubles as the 1:1 M6 panel draft.
Generated like the big panel: an own `gen_flow_panel.py` emits the SVG + a
generated header; neither is ever hand-edited.

- Logo on top: silkscreen "FireFlow" in a light weight, "GLOW" bold, set on
  one line (12 HP is 61 mm — three text lines do not fit; `ton-k` is the
  brand but does not appear on the panel).
- Six **16 mm** macro knobs in two rows of three (~20 mm pitch):
  row 1 MOTION · DENSITY · BRIGHT, row 2 DIRT · WANDER · SPACE.
- Large **NEW** button below, with the LED signatures of §5's gesture table.
- Jack block at the bottom, two rows of four at ~14 mm pitch:
  `CV MOT · CV DEN · CV BRT · CV DRT` over `CV SPC · CLK · OUT L · OUT R`.
- There is deliberately **no RST jack** (polisher review): its behavior was
  specified nowhere and "reset" has no obvious meaning in a generative
  drone box — it was a jack that existed only because Eurorack modules
  usually have one. Its slot went to CV SPACE. If clock-phase reset turns
  out to matter for the pulse/arp archetypes, that argument can earn the
  jack back.
- WANDER deliberately has no CV: the jack budget stays at eight, and CV
  over it is a Forge candidate, not a loss here.

The contrast to the big panel is intentional: there the "big" knobs render at
8.4 mm because ~56 controls had to fit; here six knobs render at real 16 mm.

## 7. Testing

doctest units in `tests/`, render host for audio smoke. Project convention:
sanity checks, no byte-identity gates. Every test proves RED once before it
counts.

1. **Property tests over many seeds** (~10 000): every story curve inside
   the taste limits at every breakpoint (risk zones included in the limits
   they widen); all named constraints hold; every macro mapping meets the
   minimum span and each terrain meets the depth mandate; determinism (same
   seed twice → identical terrain).
2. **Monotonicity:** riding a macro 0→1 moves every mapped parameter
   monotonically in its declared direction.
3. **Partial reroll:** rerolling one macro's domain (§4 definition,
   shared-target rule included) changes only that domain — every parameter
   outside it is identical to before, proving the per-parameter RNG streams
   don't shift. Since 2026-08-06 this also covers the adventure levels, in
   both directions: the rerolled domain's level must change, and every other
   level — including the base patch's — must be bit-identical. Asserting only
   the "unchanged" half would pass for a level that was never drawn at all.
4. **Terrain distance:** the NEW rejection rule is deterministic for a given
   seed sequence, and every accepted terrain clears the distance threshold.
5. **Weather:** offsets stay inside their declared bounds at every sampled
   time, are pure functions of (sub-seed, time since terrain roll) — two
   runs agree — and scale to zero as pre-weather MOTION reaches its becalmed
   end.
6. **Discrete hysteresis:** sweeping a knob+CV+weather sum slowly across a
   discrete target's threshold switches the setter exactly once per
   crossing, never repeatedly.
7. **Archetype conditioning:** the archetypes are distinguishable in the
   generated data (e.g. a drone terrain's event-rate distribution sits
   measurably below a pulse terrain's) — the stage-0 draw provably reaches
   stage 3. A fixed-seed-set statistical assertion with generous margins,
   not a fresh-draw hypothesis test — no flaky CI.
8. **Render smoke:** a handful of fixed seeds through `render.exe` — no NaN,
   RMS inside plausible bounds, a NEW transition without a level jump beyond
   a fixed dB threshold, and the **calm corner** (all macros at 0) under a
   fixed RMS ceiling on every rendered seed.

## 8. Out of scope

- The full-control hardware expansion, FireFlow Forge (mentioned as future
  option only; §2 keeps it architecturally trivial).
- The M6 panel itself (though §6 is its 1:1 draft).
- Preset *systems* (banks, slots, favorites). Baseline persistence is in
  scope: the house seed, the single undo slot, and the patch-persisted
  current terrain state + lock (§5).
- CV over WANDER (Forge candidate).
- A PACE knob: tempo is terrain state plus CLK override, by review decision.
- An RST jack: cut by the polisher review (see §6 for the earn-back rule).
