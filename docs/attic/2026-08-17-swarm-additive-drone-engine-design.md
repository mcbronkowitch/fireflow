# SWARM — additive partial-swarm drone engine

**Date:** 2026-08-17
**Status:** design approved in brainstorming; implementation plan not yet written
**Working title:** SWARM (final caption decided in the VCV round)

## 1. Motivation and gap

All five part engines are event-based: a trigger, an envelope, a note — even
the FLOW drone is "one held note". What none of them produces is a sound whose
**motion comes from within**: beating partials, a spectrum that breathes and
wanders below the lane timescale, alive while every lane stands still. That
inner life is what dedicated drone instruments (Lyra-8, additive drone boxes)
have and FireFlow lacks; the modulation lanes should *shape* that life, not be
its only source.

The 2026-08-17 brainstorming surfaced three candidate directions for it:

- **SWARM** (this spec) — additive partial swarm: beating and spectral bloom.
- **FEED** — coupled feedback-FM at the edge of chaos (instability).
- **AIR** — noise through a resonant bank (breath, wind, vowels).

All three were accepted; SWARM goes first. FEED and AIR are queued in
`docs/roadmap.md` ahead of M5k (ZAP), each needing its own brainstorming
round.

## 2. Core decisions

1. **Note engine.** SWARM plays notes like SYNTH/WAVE/BODY — PITCH lane sets
   the root, the chord layer reaches it, FORM/SONG/DENSE stay meaningful.
2. **One swarm per deck, retuned.** Exactly one bank of N sine partials per
   deck. A chord does not spawn voices; it redistributes the partials over the
   chord tones' overtone series. CPU is constant regardless of played density,
   and every note/chord change is a glide of the whole spectrum, never on/off.
3. **Triggers bloom.** A fire injects energy: partials swell above a sustain
   floor and relax back. ATTACK/DECAY shape the bloom; the floor carries the
   FLOW drone.
4. **Full character arc.** One control morphs the overtone map harmonic →
   stretched (piano/bell) → clustered/metallic. The pretty range is the lower
   half of the knob; the extreme is deliberately reachable, like BODY's
   material axis.

## 3. Swarm core

A fixed bank of **N sine partials** (working figure 32; the real N is a
**measured decision** — see §8 — and a compile-time constant so scaling it
down is a rebuild, not a redesign).

Each partial holds a current (frequency, amplitude) pair that moves per sample
along **slopes**, and a target pair recomputed only at control rate (the
`Part::kCtrlInterval` pattern). Target computation has three layers:

1. **Allocation.** The chord tones from `set_chord` (the root when no chord is
   active) receive the partials; each partial sits on one overtone of one
   chord tone. Low overtones get more weight; a spectral tilt shapes
   amplitude over overtone index. A partial whose target lands above a fixed
   fraction of the sample rate is **muted, never re-allocated** — the loop
   length must not depend on the played note, or "CPU is constant regardless
   of played density" (§2) stops being true (ruled 2026-08-17, plan review).
2. **The arc.** One value warps overtone positions: harmonic (f·n) →
   stretched (f·n^(1+β)) → clustered (positions pulled toward a seeded
   pseudo-random spread). Deterministic per partial: the same knob position
   always yields the same metal.
3. **Drift.** Per partial, a slow random walk on detune (cents range) and an
   independent slow amplitude undulation — its own `Rng` stream per partial,
   derived from one base seed. Drift *rate* has no control of its own; it
   scales gently with drift depth (deeper = slightly faster). Drift is
   free-running on purpose: motion below the lane timescale.

**Bloom.** One `Env` instance (`engine/synth/env.h`) gates the whole swarm —
see §7 for why it fits unmodified: sustain = FLOOR, trigger-from-current-level
= click-free re-bloom, idle snap = a silent swarm is nearly free. The bloom
reaches the partials with a small per-partial stagger (a few milliseconds,
low to high) so it blooms rather than switches. The STEP accent applies on
both halves, like SYNTH/WAVE/BODY: velocity on bloom height, the ring half on
FALL once DEC is up.

## 4. Lanes and voice row

Rule: no knob goes dead, and every slot keeps the function class it has on
every engine.

| Lane (class) | SWARM meaning |
|---|---|
| SOURCE (position/timbre, ×2) | **TILT** — spectral tilt: energy walks up/down the overtone stacks, dark ↔ glittering |
| SIZE (size/filter, ×1/2) | **FOCUS** — spectral aperture, read centre-open: the value's *distance* from 0.5 is the narrowness, its *side* the position. 0.5 (the lane's boot base) = fully open; toward 0 a narrow low window, toward 1 a narrow high one — so one number is aperture and position at once, and the lane's bipolar excursion sweeps the formant through the spectrum (ruled 2026-08-17, plan review) |
| PITCH (master, ×1) | Root/melody as everywhere; the chord layer pulls the swarm |
| MOTION (shape/motion, ×3/4) | **DRIFT** — depth of the inner life: detune-walk range and amplitude undulation together |
| LEVEL (×3/2) | Level, as everywhere |

Voice row (captions via the `DYNAMIC_CAPTIONS` generator, like BODY's
HIT/DAMP/CHAR):

- **SOURCE contextual knob → HARM** — the harmonic → stretched → clustered
  arc. The engine's main character control, on the same slot as BODY's MATL.
- **ATTACK → RISE / DECAY → FALL** — the bloom envelope, with a deliberately
  long range (seconds); FALL carries the accent ring half.
- **RESO → FLOOR** — the sustain floor: 0 = blooms only (a pulsing swell
  swarm in the groove), 1 = solid endless drone. In FLOW a minimum floor is
  enforced so the drone promise holds at FLOOR 0; CHOKE hold still releases
  the drone via `set_hold`.
- **SUB → SUB** — one or two dedicated partials an octave below the root; the
  foundation under the swarm.
- **FILT (bipolar) → even/odd balance** — negative = hollow (odd partials
  only), positive = even-emphasized (organ-full), center = natural.
  Deliberately *not* another tilt; the SOURCE lane owns tilt.

Detune A/B stays what it is everywhere: a swarm-wide offset per part.

## 5. Chord, FORM/SONG, STEP/FLOW

SWARM registers as a **melodic engine** (the `_melodic` engine class), so
FORM and SONG reach it in both modes through the phrase machinery shipped in
2.21.2.

- **Chord.** `set_chord` arrives once per control tick; SWARM retargets the
  allocation live, so a COLOR move re-voices the swarm as a glissando without
  retrigger. `trigger_chord` is **overridden**: the default fires `trigger` n
  times, which would mean n blooms per chord — SWARM fires **one** bloom and
  allocates to all n pitches. Retargeting is nearest-neighbor: partials on
  common tones hold still, only moving voices glide (mirroring the chord
  layer's voice-leading), and no RNG draw happens on a chord change.
- **STEP.** Each fire: accent push → set allocation → bloom. DENSE reveals
  the ranked slots; the accent contour lands on bloom height and (with DEC)
  on FALL. `set_gate` is ignored, like the synth: RISE/FALL define the bloom
  completely.
- **FLOW.** The melodic lane walks its 8-slot phrase; retunes are pure
  glides; blooms come from the auto-retrigger at accent 0; the floor carries
  the drone in between (minimum floor per §4). CHOKE hold: floor decays out
  click-free, auto-retrigger stops, release re-arms.
- **FORM/SONG.** Nothing SWARM-specific — it consumes the composed phrase
  like any melodic engine. An A/B switch on a phrase boundary is heard as the
  whole spectrum gliding into the new phrase.
- **NEW** additionally redraws the swarm's base seed (drift streams and
  cluster map). Determinism ("same knob position, same metal") stays intact
  because NEW is an explicit user gesture — it gives the deck a new
  individual, the way it spawns a grain on the sampler. The reseed takes
  effect **immediately**, not at the next phrase wrap: every target change
  arrives as a glide anyway, so there is nothing a deferral would protect
  (ruled 2026-08-17, plan review).

## 6. Integration

- **`ENGINE_SWARM = 6`**, appended before the `ENGINE_COUNT` sentinel in
  `engine/parts/engine_iface.h`. The sentinel deliberately breaks every
  hand-written "all engines" list (e.g. `tests/test_deck_bus.cpp`'s
  bit-identity sweep) until it knows SWARM.
- **Own `IPartEngine` implementation in `engine/swarm/`** (`swarm_engine.h`,
  partial bank possibly separate) — *not* the `SynthEngineT<Voice>` pattern:
  SWARM has no per-note voices.
- **Overrides:** `trigger_chord` (one bloom, §5), `set_chord`, `set_hold`,
  `set_flow`, `set_accent`, and `set_width` gets a real meaning: per-partial
  constant-power stereo spread (the COLOR-driven push already exists).
  `consumes_input` stays false.
- **Metering.** `Part::voice_env`/`active_voices` return 0 for an engine they
  have no arm for, which would leave the VCV LED and `Instrument`'s meter
  dead on a SWARM deck. A swarm is one sound, not n voices: the deck reports
  the bloom envelope on voice slot 0 and `active_voices() == 1` while audible
  (ruled 2026-08-17, plan review).
- **Hosts.** Render host gets a listening scenario (`swarm_drone.json`) as a
  sanity render — no hash gate; renders are spot checks, not checksums. VCV
  gets the sixth ENG state plus a `DYNAMIC_CAPTIONS` row (HARM, RISE, FALL,
  FLOOR, SUB, BAL).

## 7. Reuse and CPU strategy

**Reused as-is:**

- **`Env` (engine/synth/env.h) is the bloom, unmodified.** Its semantics match
  §2–§4 point for point: `sustain` *is* FLOOR (ADS hold = endless drone),
  `trigger()` rises from the current level = click-free re-bloom, "sustain to
  0 while holding" is exactly the CHOKE demotion, and below −80 dB it snaps
  idle so a silent swarm costs almost nothing. One instance for the whole
  swarm; no new envelope code.
- **`Rng`** (xorshift32, deterministic, bit-reproducible): one base seed,
  per-partial streams derived from it; NEW redraws only the base seed.
- **`fast_sin`**: polynomial, no lookup table, so no memory either.
- **`OnePole`** on the control side only — its branches and `fabs` stay out of
  the per-sample partial loop.
- **Part plumbing is free:** control tick, `set_chord`, accent, hold, width,
  voice-row routing, phrase machinery, bench and probe infrastructure.

**CPU levers, largest first:**

1. **One sine per partial; stereo via pan gains.** Never two oscillators for
   L/R — `set_width` becomes a constant-power pan per partial (2 MACs).
   Halves the naive cost outright.
2. **Slopes, not smoothers, in the hot loop.** The control tick computes
   per-partial slopes (phase-increment slope, amplitude slope); the inner
   loop is a branch-free multiply-add chain — auto-vectorizable on desktop,
   pipeline-friendly on the M7. Glides fall out for free because slopes run
   toward targets.
3. **Amortized retargeting.** Each control tick redistributes only a slice of
   the partials (round-robin) instead of all at once. The budget gate
   measures the *worst* block, so flattening spikes buys budget directly.
4. **Idle gating.** Env idle → swarm silent → near-zero cost (the reverb's
   clear-on-sleep pattern). Honest footnote: FOCUS culling of inaudible
   partials only helps the *average*, not the worst case — it does not count
   toward the N decision.
5. **Plan B if the early bench says no: recursive sine oscillators**
   (s[n] = 2cos(ω)·s[n−1] − s[n−2]). Fiddly under glides (coefficient
   updates, renormalization), so not the starting point — a measured
   fallback, decided by the bench, not by taste.
6. **DTCM/ITCM ladder exists.** Swarm state is tiny (~32 partials × ~8 floats
   ≈ 1 KB) — a DTCM candidate; the inner loop is an ITCM-hotset candidate
   (with the known caveat that ITCM placement currently does not link at the
   shipping optimization level).

**Memory.** SWARM is a memory dwarf: ~1–2 KB of state, no tables, no delay
lines. The memory budget stays owned by the sample-based engines (sampler
buffers, WAVE bank, BBD lines); FEED and AIR are in the same weight class as
SWARM. A hybrid "static spectrum body as wavetable + few free partials on
top" was considered and rejected: it would freeze phase relations and kill
the inner life the engine exists for.

## 8. CPU probes and budget

Per the probe rule, no runtime number in this spec is a promise; every one
below is either cited from a measured source or explicitly marked as
to-be-measured.

Two-stage, the BODY/BBD pattern:

1. **Early — kernel bench decides N.** A bench workload (`swarm_bank`): N
   partials with `fast_sin`, slope glide, control-tick retargeting, measured
   on the **Patch Submodule** at `-O3` over USB-CDC. Result: cycles per
   partial → N. Runs before the engine is built, because N drives design
   room (whether amplitude undulation per partial is affordable, 32 vs 24).
   New rows are verified via `bench.map` (the stale-object trap), and no Seed
   figure may be quoted for the decision (2.17-point reserve).
2. **Late — whole-engine gate.** `inst_swarm_engine_worst`, the
   `inst_bbd_engine_worst` pattern: the built engine with blooms, chord
   retargeting and deck FX must not create a new `instrument_worst` above
   today's (102.27 % avg / 108.62 % max on the submodule, measured — see
   `docs/roadmap.md`). Orientation for a voice-engine deck: BODY's measured
   ~30.7 % of the block.

The early bench says what we can afford; the late gate proves we afforded it.
Because N is compile-time, a failing late gate costs a rebuild at lower N,
not an architecture change.

Behavioral runtime claims (overtone limits, glide times, floor levels) go
through desktop probes (`docs/engine-map.md` §6 recipe) before they enter the
plan; measured facts land in the engine map.

## 9. Tests

Doctest, each with its RED proven once:

- **Neutrality:** with SWARM in the tree, the five existing engines stay
  bit-identical (the existing sweep pattern).
- **Determinism:** same knob state → same output; NEW changes it, and only
  NEW.
- **`trigger_chord` = one bloom,** not n (red against the default behavior).
- **Click-free retargeting:** a chord change produces no discontinuity above
  a derivative threshold.
- **DRIFT 0 = static / DRIFT > 0 = moving:** spectrum at t₀ vs. later,
  asserted in both directions.
- **FLOW minimum floor audible;** CHOKE decays out and stops retriggering;
  accent shapes bloom height.

No render hash gates (renders are sanity checks, not checksums).

## 10. By-ear candidates

Reserved for Bastian's ears, marked as such in the plan: drift depth range,
minimum-floor level, bloom stagger spread, the cluster zone of HARM. First-try
values ship flagged, like the STEP-accent depth floors.

## 11. Open points

- **N** — unknown until the early bench (§8); the working figure 32 is not a
  claim.
- **Caption wording** (HARM/RISE/FALL/FLOOR/BAL) — finalized in the VCV
  round with the `DYNAMIC_CAPTIONS` change.
- **Minimum-floor value in FLOW** — by ear (§10).
