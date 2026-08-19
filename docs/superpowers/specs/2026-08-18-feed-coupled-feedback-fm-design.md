# FEED — coupled feedback-FM drone engine

**Date:** 2026-08-18
**Revised:** 2026-08-19 — deck-compatibility review round (§2.6, §9.9, SPREAD/RATIO bounds)
**Status:** design approved in brainstorming; implementation plan not yet written
**Working title:** FEED (captions finalized in the VCV round)

## 1. Motivation and gap

FEED is the second of the three directions the 2026-08-17 ambient-engine
brainstorming produced. The first, the additive partial swarm SWARM, was built
and withdrawn on a listening decision on 2026-08-18
([`docs/attic/2026-08-18-swarm-withdrawn.md`](../../attic/2026-08-18-swarm-withdrawn.md));
the third, AIR, is still unqueued for design. FEED was never touched by the
SWARM work and inherits none of its mechanism — but it inherits its lessons,
and this spec names them where they apply.

The gap is the one SWARM was also aimed at and did not close: every part engine
here is event-based. A trigger, an envelope, a note. What none of them produces
is a sound whose **motion comes from within** — alive while every modulation
lane stands still. FEED answers it from the opposite side of SWARM: not many
independent partials that are individually made to drift, but **few operators
that are coupled to each other**, where the motion is a consequence of the
coupling rather than an addition to it.

Two further facts make FEED worth building now:

- **It would be the first FM anywhere in the built engine.** M5k's ZAP spec
  (`2026-07-18-zap-percussion-engine-design.md`) designs a two-oscillator
  FM/AM percussion voice, but nothing implements it. There is no operator
  primitive to inherit and none to conflict with.
- **The prior art is MIT and this repo has already adapted it twice.** See §11.

## 2. Core decisions

Settled in the brainstorming round, 2026-08-18:

1. **Melodic engine, free-running network.** FEED registers as a melodic
   engine, so PITCH, the scale layer, the chord layer, FORM/SONG and DENSE all
   reach it. The network **runs continuously**: a trigger retunes it and
   injects energy, it does not start it. The inner life therefore does not
   depend on being triggered.
2. **A fixed bank of P operator pairs per deck**, allocated over the chord
   tones. CPU is independent of the played density and every note or chord
   change is a glide of the whole network, never a voice on/off. P is a
   compile-time constant decided by the bench (§8), not by taste.
3. **One knob is the cliff.** BOND morphs each modulator's phase-modulation
   input from its own feedback (well-behaved, tonal FM) to the neighbouring
   pair's output (a coupled system that beats, escalates and finally breaks).
4. **The motion is the coupling.** No LFO, no random walk, no drift constant in
   the audio path. Detuned pairs under coupling interfere aperiodically; that
   is the mechanism, and §9's gate is two-sided so it proves the mechanism and
   not merely that something moved.
5. **A trigger drives amplitude and index from one envelope.** In FM the attack
   lives in the index envelope, not in the level. Bright and rough on the
   attack, darker and calmer in the tail. The sustain level is the drone.

Added 2026-08-19, in the spec review round:

6. **FEED blends; it does not detune the mix.** The engine plays alongside
   other decks, so the perceived pitch centre must hold: over the whole SPREAD
   travel and up to a defined BOND threshold, the estimated fundamental stays
   within a small tolerance of the played pitch (the gate is §9.9). Beyond the
   threshold the network may break — the cliff stays, but it becomes a *place*
   on the knob rather than an accident. The variety this spec promises comes
   from motion (BOND, SPREAD, the index), never from parking the deck out of
   tune. SWARM's withdrawal calibrates the scale: a +420-cent overtone
   excursion earned the complaint "HARM almost always sounds detuned", and
   +4.5 cents was accepted
   ([`docs/attic/2026-08-18-swarm-withdrawn.md`](../../attic/2026-08-18-swarm-withdrawn.md)).

## 3. The pair bank

A fixed bank of **P pairs** per deck (working figure 4 pairs = 8 operators;
the real P is a **measured decision**, §8). Each pair holds two phase
accumulators, two feedback history slots and its current/target frequency and
amplitude, moved per sample along slopes toward targets recomputed at control
rate (the `Part::kCtrlInterval` = 96 pattern).

### 3.1 One pair

For pair *i*, with κ = BOND and all `fast_sin` on normalized phase
(`fast_sin(p) == sin(2π·p)`):

```
fb_i    = 0.5 * (m_i[n-1] + m_i[n-2])        // own modulator, 2-sample average
nb_i    = 0.5 * (o_j[n-1] + o_j[n-2])        // neighbour carrier, j = (i+1) % P
pm_i    = fb_amount_i * ((1 - κ) * fb_i + κ * nb_i)

m_i[n]  = fast_sin(phase_m_i + pm_i)          // modulator
o_i[n]  = fast_sin(phase_c_i + index_i * m_i[n])   // carrier
```

where `index_i = DEPTH · env` and the pair's amplitude is `env` as well — one
`Env` instance per deck feeds both, which is decision §2.5 in one line. FLOOR is
that envelope's sustain, so at FLOOR 1 the drone stands at full DEPTH and at
FLOOR 0 the index collapses with the level after every hit.

The deck output is the pan-summed `o_i` plus SUB, through the ceiling of §3.3.

**The coupling enters the modulator, not the carrier.** That is the deeper and
faster-tipping point of attack: at κ > 0 the modulator of pair *i* is being
driven by a signal at a different fundamental, so the pair's own spectrum stops
being a function of its own pitch alone. Carrier-side coupling was considered
and rejected: it reads as a mix, not as an infection.

**Ring, not chain.** `j = (i+1) % P` closes the ring, so every pair is both a
source and a destination and there is no privileged first pair. The ring order
is fixed at compile time; it is not a control.

### 3.2 Two borrowed stabilizers

Both are recipes ported into float, not vendored code (§11):

1. **Every feedback tap is the average of the last two samples** — the DX7
   trick, as implemented in Plaits `plaits/dsp/fm/operator.h`
   (`pm = (previous_0 + previous_1) * fb_scale`). One add, one multiply, and
   the feedback path is low-passed. This is simultaneously the anti-aliasing
   and the anti-blowup measure, and it is why round 1 runs **without
   oversampling**.
2. **`fb_amount_i` falls with played pitch** — the mechanism of Braids'
   `RenderFeedbackFm`, which derives an attenuation from the pitch offset so
   the top octaves stay clean while the bottom stays capable of destruction.
   Its curve is a by-ear candidate (§10); that it is wired at all is a gate
   (§9.4).

**One attenuation, both terms — deliberate.** `fb_amount_i` multiplies the
blended input in §3.1, so the pitch-dependent attenuation dampens the
neighbour term exactly as much as the self-feedback: high chord tones are not
only more stable, they are also less *infected* — BOND audibly weakens toward
the top of a chord. This is a decision, not a side effect: the top staying
clean is part of §2.6. A review that files "coupling doesn't reach high notes"
as a defect should be pointed here (the `fireflow-bbd-range-cap-is-flow-only`
precedent: name the intent before it gets reported as a bug).

### 3.3 The ceiling

A `tanh` ceiling on the deck sum, the `BodyVoice::kFlowSatCeil` pattern and for
the same stated reason: *where opening a path lets a value diverge, add the
bounding nonlinearity the instrument already has rather than re-imposing a
limit downstream*. Its constant is by-ear (§10); its existence is a gate
(§9.3).

### 3.4 SPREAD, and why there is no drift

The pairs are detuned against each other by SPREAD, distributed
deterministically around each allocated pitch (symmetric, so the perceived
centre pitch does not move with SPREAD). Under coupling, two nearly-equal
mutually-modulating systems interfere aperiodically — the beat pattern never
repeats, at no cost and with no state to freeze.

**SPREAD is bounded by the spec, not only by ear.** The usable region is
"beating audible, detune not": the beat tempo grows with the pair offset long
before the offset reads as out-of-tune, and where that boundary sits is a
desktop probe, not a taste question. The lower half of the knob stays in
single-digit cents; only the top end is allowed to reach §4's "dense
roughness". §10 still owns the curve inside that frame — and the
symmetric-centre claim above stops being only a claim: §9.9 gates it.

This is the deliberate opposite of SWARM's mechanism, and the reason is
recorded in that engine's withdrawal: a drift implemented as a per-tick step
**froze** at low settings (93.9 % of control ticks left the frequency exactly
unchanged at MOTION 0.15; MOTION 0.02–0.05 was completely inert), and the
correlated-walk rebuild that fixed it never reached its slow-end target.
FEED has no per-tick drift step, so the float32 cliff described in
`fireflow-float32-modulation-freeze` has nothing to bite.

**`NEW` redraws the detune signature** of the bank — one `Rng` draw at the
control tick, the deck's "individual", exactly as `NEW` reseeds elsewhere. It
is the only randomness in the engine and it is not in the audio path.

**`NEW` also draws small per-pair `fb_amount` offsets.** With one shared
`fb_amount` the only per-pair individuality is the detune signature — thin, if
SWARM's "it always sounds the same" is the bar. Small deterministic offsets,
applied at the control tick, make each pair tip at a slightly different BOND
position: the cliff becomes a gradient the ear can ride instead of an edge.
Per-pair *ratio* offsets were considered for the same job and rejected — they
push the sidebands inharmonic, which is exactly the detune §2.6 forbids. The
offset range is a by-ear candidate (§10).

## 4. Lanes and voice row

Rule as everywhere: no knob goes dead, and every slot keeps its function class.

| Lane (class) | FEED meaning |
|---|---|
| SOURCE (position/timbre, ×2) | **BOND** — the coupling: self-feedback → neighbour. The character axis belongs on the lane with the largest excursion, so a swinging SOURCE lane drives audibly through the instability |
| SIZE (size/filter, ×1/2) | **SPREAD** — detune width between the pairs. The "size" of the cluster, and because the motion is beating, also its tempo: narrow = slow breathing, wide = dense roughness |
| PITCH (master, ×1) | Root/melody as everywhere; the chord layer reaches the allocation |
| MOTION (shape/motion, ×3/4) | **DEPTH** — the FM index. The index *is* the waveform's motion; the most FM-native reading of this class |
| LEVEL (×3/2) | Level, as everywhere |

Voice row, captions via the `DYNAMIC_CAPTIONS` generator (the BODY
HIT/DAMP/CHAR pattern):

- **SOURCE contextual knob → RATIO** — modulator-to-carrier ratio, arcing from
  1:1 through integer ratios into irrational territory: tonal → bell-like →
  clangorous. Same slot as BODY's MATL, same role: the pretty range is the
  lower half, the extreme is deliberately reachable. **The lower half
  gravitates to integer ratios.** A continuous knob stands *between* the
  integers almost everywhere, and near-integer ratios (2.03…) read as
  chorus/detune — motion, but from the wrong source: §2.6 wants the motion to
  come from BOND and SPREAD, not from a knob that happens to sit crooked. So
  the lower half locks onto 1:1…4:1 (magnet curve vs. zones with hysteresis is
  a plan decision; SWARM's zone reader survives in the attic tag as a
  reference recipe), and only the upper half runs continuously into the
  irrational.
- **ATTACK → RISE / DECAY → FALL** — the one envelope, driving amplitude and
  index together. FALL carries the ring half of the STEP accent, gated by the
  DEC knob exactly as SYNTH/WAVE/BODY do it.
- **RESO → FLOOR** — the envelope's sustain: 0 = blooms only, 1 = an endless
  drone. RESO is free because FEED has no filter resonance, the same argument
  by which SWARM took the slot.
- **SUB → SUB** — one sine an octave below the root, the foundation under the
  network. Not part of the ring and not coupled.
- **FILT (bipolar) → DAMP** — a one-pole low-pass **inside the feedback
  path**. Precisely, because "bright" is ambiguous on a low-pass: the centre
  detent is a by-ear neutral cutoff (§10); left sweeps the cutoff down — dark
  and tame, the feedback loses the highs that feed escalation; right sweeps it
  up toward effectively open — bright and wild, unfiltered feedback carries
  its full spectrum back into the phase input. It is honestly a filter; it
  sits at the place where a filter means something in FM.

DETUNE A/B stays what it is everywhere: a deck-wide offset.

**The unwritten-base trap, named before it bites.** The VCV host never writes
the base values of `LANE_MOTION` and `LANE_LEVEL` (memory
`fireflow-unwritten-lane-bases`). DEPTH would therefore sit at `Part`'s default
of 0.5 and be unreachable while playing — the defect found on the day SWARM was
withdrawn, and a plausible part of why nobody was convinced at the panel. Two
consequences, both binding on the plan:

1. **A host task: the MOTION lane base gets written.** This fixes the class of
   defect for every future engine, not only for FEED.
2. **Defensively regardless: DEPTH at 0.5 must be a good sound**, not a dead
   one. Unfixed, the deck is then merely not fully exploitable rather than
   broken.

## 5. Chord, FORM/SONG, STEP/FLOW

FEED registers as a **melodic engine** (the `_melodic` engine class), so FORM
and SONG reach it in both modes through the phrase machinery shipped in 2.21.2.

- **Chord.** `set_chord` arrives once per control tick; FEED reallocates the P
  pairs over the chord tones live, so a COLOR move re-voices the network as a
  glissando without retrigger. Allocation is nearest-neighbour: pairs on
  common tones hold still, only moving ones glide. `trigger_chord` is
  **overridden** — the default fires `trigger` n times, which would mean n
  envelope hits per chord; FEED fires **one**.
- **STEP.** Each fire: accent push → retune → one envelope hit on amplitude and
  index. `set_gate` is ignored, like the synth: RISE/FALL define the hit
  completely. The accent spends itself twice, on hit height and (with DEC up)
  on FALL.
- **FLOW.** The melodic lane walks its 8-slot phrase; retunes are pure glides;
  hits come from the auto-retrigger at accent 0; FLOOR carries the drone
  between them. A **minimum floor** is enforced in FLOW so the drone promise
  holds at FLOOR 0 (SWARM's rule, kept). CHOKE hold releases the drone via
  `set_hold`: the floor decays out click-free, auto-retrigger stops, release
  re-arms.
- **FORM/SONG.** Nothing FEED-specific — it consumes the composed phrase like
  any melodic engine.

## 6. Integration

- **`ENGINE_FEED = 6`**, appended before the `ENGINE_COUNT` sentinel in
  `engine/parts/engine_iface.h`. The sentinel deliberately breaks every
  hand-written "all engines" list (e.g. `tests/test_deck_bus.cpp`'s
  bit-identity sweep) until it knows FEED.
- **Own `IPartEngine` implementation in `engine/feed/`** (`feed_engine.h/.cpp`,
  `feed_pair.h`, `feed_config.h`) — *not* the `SynthEngineT<Voice>` pattern:
  FEED has no per-note voices and no allocator.
- **Overrides:** `trigger_chord` (one hit, §5), `set_chord`, `set_hold`,
  `set_flow`, `set_accent`, and `set_width` as a constant-power pan per pair.
  `consumes_input` stays false.
- **Metering.** `Part::voice_env`/`active_voices` would otherwise leave the VCV
  LED and `Instrument`'s meter dead on a FEED deck. A coupled network is one
  sound, not n voices: the deck reports the envelope on voice slot 0 and
  `active_voices() == 1` while audible (the SWARM ruling, kept).
- **Hosts.** Render host gets a listening scenario `feed_drone.json` as a
  sanity render — no hash gate; renders are spot checks, not checksums. VCV
  gets the sixth ENG state plus a `DYNAMIC_CAPTIONS` row (BOND, SPREAD, DEPTH,
  RATIO, RISE, FALL, FLOOR, SUB, DAMP).
- **`THIRD_PARTY.md` gains a row** in its "Ported" section (§11).

## 7. Reuse and CPU strategy

**Reused as-is:**

- **`Env` (`engine/synth/env.h`) is the envelope, unmodified.** `sustain` *is*
  FLOOR (ADS hold = endless drone), `trigger()` rises from the current level =
  click-free re-hit, "sustain to 0 while holding" is exactly the CHOKE
  demotion, and below −80 dB it snaps idle so a silent deck costs almost
  nothing. One instance per deck, driving both amplitude and index.
- **`fast_sin`**: polynomial, no lookup table, so no memory either — and one
  shared implementation, so desktop renders and firmware agree. That matters
  more here than anywhere else: a chaotic system amplifies any difference
  between two sine implementations, and there is only one.
- **`Rng`** for the `NEW` draws only — the detune signature and the per-pair
  `fb_amount` offsets (§3.4).
- **`OnePole`** for DAMP, on the control side for coefficients; the per-sample
  path is a multiply-add.
- **Part plumbing is free:** control tick, `set_chord`, accent, hold, width,
  voice-row routing, phrase machinery, bench and probe infrastructure.

**CPU levers, largest first:**

1. **One sine per operator; stereo via pan gains.** Never two oscillators for
   L/R — `set_width` is a constant-power pan per pair (2 MACs).
2. **Slopes, not smoothers, in the hot loop.** The control tick computes
   per-pair slopes; the inner loop is branch-free. Glides fall out for free.
3. **Idle gating.** Env idle at FLOOR 0 → deck silent → near-zero cost.
4. **P is compile-time**, so a failing late gate costs a rebuild at lower P,
   not an architecture change.
5. **No oversampling in round 1.** §3.2's two stabilizers are the plan; if the
   ear reports aliasing, 2× is a rebuild, and the decision is then informed by
   a measurement instead of a fear.

**What does not vectorize.** The feedback taps are a per-sample serial
dependency: pair *i* reads its own and its neighbour's previous outputs. The
loop is therefore scalar by construction. This is priced in, not a defect —
SWARM's measured loop was scalar too.

**Memory.** FEED is a memory dwarf: a few hundred bytes of state, no tables, no
delay lines. The memory budget stays owned by the sample-based engines.

## 8. CPU probes and budget

Per the probe rule, no runtime number in this spec is a promise. Every figure
below is either cited from a measured source or explicitly marked
to-be-measured.

**The only anchor that exists** is SWARM's, and it is an anchor for the shape
of the loop, **not a FEED number**: a sine partial with per-sample glide and
control-tick retargeting measured **7405 cycles per partial per 96-sample
block** (77.1 cycles per sample) at `-O3` on the Patch Submodule, layout `axi`,
transport USB
([`docs/attic/2026-08-17-swarm-n-decision.md`](../../attic/2026-08-17-swarm-n-decision.md)).
An FM operator is that class of loop. Nothing follows from this except that the
round is worth opening.

Two-stage, the BODY/BBD/SWARM pattern:

1. **Early — a kernel bench decides P.** A bench workload (`feed_pairs`): P
   pairs with `fast_sin`, both feedback taps, slope glide and control-tick
   retargeting, measured on the **Patch Submodule** at `-O3` over USB-CDC.
   Result: cycles per pair → P. Runs before the engine is built. New rows are
   verified via `bench.map` (`fireflow-bench-stale-object-trap`), the tree must
   be clean per run (`fireflow-bench-clean-tree-guard`), and **no Seed figure
   may be quoted for the decision**.
2. **Late — the whole-engine gate.** `inst_feed_engine_worst`, the
   `inst_bbd_engine_worst` pattern: the built engine with hits, chord
   retargeting and deck FX must not exceed the **same image's**
   `instrument_worst`. SWARM's own history is the warning here: its kernel row
   alone sized the bank too generously and the whole-engine row corrected the
   reading.

**Denormals get measured, not assumed.** Decaying feedback tails are exactly
the shape that pays the denormal tax, and nothing in this repo sets
flush-to-zero (verified 2026-08-18 across `shell/`, `bench/`, `host/render/`,
`engine/`, `src/`; see `docs/roadmap.md`, "Two threads carried out of the SWARM
withdrawal"). The plan runs one probe with and without FTZ+DAZ and records the
delta. **Enabling the flag is a separate decision** — it changes existing
behaviour instrument-wide — and this spec does not make it.

Behavioural runtime claims (where the cliff sits, how long a beat cycle is,
what SPREAD does to it) go through desktop probes
([`docs/engine-map.md` §6](../../engine-map.md)) before they enter the plan;
measured facts land in the engine map.

One of those probes is named here because three spec decisions hang on it: a
**regime map** over BOND × DEPTH × RATIO — where the output is tonal, where it
beats, where it escalates, where it breaks — measured on the desktop build and
recorded in the engine map before the plan is written. The BOND knob curve
(§10) is laid onto that map instead of searched blind, and §9.9's BOND
threshold and cent tolerance are read off it rather than invented. The same
probe finds SPREAD's "audibly beating but not yet detuned" boundary (§3.4).

## 9. Tests

Doctest, each with its RED proven once (`fireflow-tests-must-be-able-to-fail`),
and none of the four vacuous shapes (`fireflow-vacuous-test-gates`).

1. **Neutrality.** With FEED in the tree, the five existing engines stay
   bit-identical (the existing sweep pattern).
2. **The inner life, two-sided.** All lanes static, FLOOR 1: at mid BOND the
   spectrum after 30 s differs beyond a threshold; **at BOND 0 it does not**,
   within a tolerance. The second half is the point — it proves the coupling is
   what moves, rather than merely asserting that something moved. This is the
   gate SWARM never had.

   The measure must be the **magnitude spectrum over a window**, and the
   implementer needs to know why: at BOND 0 the detuned pairs still beat
   audibly, but that beating is amplitude interference between a *fixed* set of
   frequencies, so the windowed magnitude spectrum is stationary. Only coupling
   makes the sidebands themselves wander. A time-domain measure would see
   motion in both cases and the gate would be vacuous.

   One implementation constraint, so the negative side cannot fail spuriously:
   the analysis window must be long enough to **resolve the SPREAD detune**
   (window length > 1/Δf for the smallest pair offset). An unresolved pair
   merges into one bin whose magnitude pulses at the beat rate — the BOND 0
   side would then move for a reason that has nothing to do with coupling.
3. **Boundedness.** A sweep over BOND × DEPTH × RATIO × played pitch: the
   output is never NaN, never inf, and never exceeds the ceiling. This is
   feedback FM's real failure mode and the gate is cheap.
4. **High notes are attenuated.** The effective feedback amount at a high
   played pitch is measurably lower than at a low one — otherwise §3.2's
   second stabilizer is written but not wired.
5. **`trigger_chord` fires one envelope hit,** not n (RED against the
   interface's default implementation).
6. **Click-free retune.** A chord change produces no discontinuity above a
   derivative threshold.
7. **FLOW minimum floor audible; CHOKE decays out and stops retriggering; the
   accent lands on both halves.**
8. **Determinism.** Same knob state → same output; `NEW` changes it, and only
   `NEW`.
9. **The pitch centre holds (two-sided).** Up to the BOND threshold read off
   §8's regime map, and over the full SPREAD travel, the estimated fundamental
   (autocorrelation on the deck output) stays within the tolerance of the
   played pitch; **beyond the threshold it is allowed to break**, and the test
   asserts nothing there. This is §2.6 made falsifiable — and it also proves
   §3.4's symmetric-centre claim, which until this gate is only a claim.

No render hash gates (`fireflow-bit-exactness-not-required`).

## 10. By-ear candidates

Reserved for Bastian's ears and marked as such in the plan, the way the STEP
accent's depth floors were:

- where in BOND's travel the cliff sits (the curve, laid onto §8's regime
  map — the endpoint of the tonal region is the map's, not the ear's),
- the SPREAD range in cents, inside §3.4's frame (lower half single-digit),
- the per-pair `fb_amount` offset range of §3.4,
- the minimum floor in FLOW,
- RATIO's irrational end,
- the DAMP range and its neutral centre cutoff (§4),
- the pitch-attenuation curve of §3.2.2 and the ceiling constant of §3.3.

Not by-ear, recorded to keep the boundary clean: §9.9's BOND threshold and
cent tolerance come from the regime map (§8), not from listening.

First-try values ship flagged.

## 11. Prior art, and what is borrowed

Both sources are MIT, as is this repository, and both have been adapted here
before: `third_party/oliverb/` vendors Émilie Gillet's code with an
`stmlib_shim.h`, and `THIRD_PARTY.md` already carries a "Ported" section for
recipes reimplemented rather than copied (the stmlib limiter, DaisySP's
`ResonatorSvf` recurrence).

- **Plaits `plaits/dsp/fm/operator.h`** (Émilie Gillet, MIT) — the two-sample
  feedback average of §3.2.1.
- **Braids `braids/digital_oscillator.cc`** (Émilie Gillet, MIT),
  `RenderFeedbackFm` — the pitch-dependent feedback attenuation of §3.2.2.
  Its sibling `RenderChaoticFeedbackFm` (the WTFM shape: the output modulates
  the modulator's phase *increment*) is the acknowledged ancestor of §3.1's
  topology, but is not the mechanism used: FEED couples pairs to each other,
  which that function does not do.

**Nothing is vendored.** Both upstreams are fixed-point (`int16`/`int32` with
`Interpolate824` over a sine table); this engine is float with `fast_sin`, so
what crosses over is the recipe. `THIRD_PARTY.md` gets a "Ported" row with both
attributions regardless — the same courtesy standard the limiter entry sets.

Rejected sources, recorded so they are not re-evaluated: **PolyFM**
(Daisy, 4-op) has no license file; **Dexed** and **Surge** are GPL and
incompatible with this repository's MIT license; **`daisysp::Fm2`** is 73 lines
with no feedback at all and has nothing FEED needs.

## 12. Out of scope for round 1

Deliberately, and each is a second round if the first earns one:

- coupling **across** decks over the excitation bus,
- oversampling,
- DX7 SysEx import or any preset bank,
- a per-pair algorithm matrix (Plaits' six-operator machinery),
- ZAP sharing FEED's operator primitive — ZAP's spec wants triangle-core
  morphing oscillators, and merging the two is a design decision that belongs
  to whichever engine is built second.

## 13. Step 0, before the plan

The direction can still be heard for zero lines of code, and after SWARM it
should be: **Audible Instruments "Macro Oscillator" (the Braids port) in WTFM
mode, next to FireFlow in Rack** — or through `IN L`/`IN R` into a BBD deck, so
it runs through this instrument's own FX chain and reverb. Ten minutes, and the
outcome is allowed to be "no".

This is a listening step, not a gate: it informs the plan, and the plan is
written afterwards.

## 14. Open points

- **P** — unknown until the early bench (§8); the working figure 4 pairs is not
  a claim.
- **Caption wording** (BOND/SPREAD/DEPTH/RATIO/DAMP) — finalized in the VCV
  round with the `DYNAMIC_CAPTIONS` change.
- **Whether SPREAD's distribution is symmetric in cents or in ratio** — a
  desktop probe decides, before the plan.
- **§9.9's BOND threshold and cent tolerance** — read off the §8 regime map,
  fixed in the plan.
- **RATIO's lower-half mechanism** — magnet curve vs. zones with hysteresis; a
  plan decision (§4).
- **FTZ** — measured in this round (§8), decided in its own.
