# The melody becomes reachable

**Status:** design. **Scope: one repair, in `engine/mod/` only.** Split out of the
SHAPE/SMOOTH rework (`2026-08-13-shape-smooth-rework-design.md`), which spent five
drafts trying to fix three unrelated things at once. Two independent reviews of
that spec's fifth draft recommended the same seam; this is the piece both agreed
was sound, and the only one measured state-by-state on a patched build.

**Goal:** FORM, SONG and the composed phrase are audible where the instrument
actually plays, instead of on 0.01 % of terrain draws.

Every number states its probe setup. **Construction order matters**:
`set_melodic()` before `init()` (`docs/engine-map.md` §6).

---

## 0. Invariants

| # | Invariant | Owner | Respected how |
|---|---|---|---|
| I1 | ENTROPY 0 = LOOP: nothing mutates, the buffer repeats exactly. | `…entropy-sequencer-design.md:61` | Neither change touches mutation, RNG, or the buffer. |
| I2 | The drone SHAPE cap `{0, 0.25}` keeps the calm-corner gate green. | `taste.h:998-999`; evidence log `:101-113` | Nothing here touches `taste.h`. This spec makes the melody independent of SHAPE, so the cap stops mattering for it. |
| I3 | **The BBD PITCH lane is not a note.** It is the delay clock, "a bend, not a keyboard", and `kBbdFlowRangeMax = kBbdFlowSemis / (2·60)` derives its 2 **from `apply_range` itself**. The owner ruled for that trade on 2026-08-07. | `taste.h:578-597` | Both changes are gated on `_melodic && _flow_melody`, which is false for a BBD deck **in either mode**. Measured: a BBD/SAMPLER PITCH lane is bit-unchanged in FLOW and in STEP. |
| I4 | `fireflow-dev-alpha-no-patch-compat` — saved patches may break. | memory | No migration designed. |

A note on a **retired** invariant. Draft 5 carried
`2026-07-25-mod-lane-step-grid-lock-design.md:12-14` — *"sweeping it left of the
S&H end blends the composed pitch with the lane waveform … That behaviour is not
under review"* — as a constraint. **It is wrong about the code.** In
`shape_value` (`waveforms.h:22-33`) `sh_hold` appears in exactly one arm,
`default:`, i.e. `shape ≥ 0.75`. Left of the S&H end the phrase's weight is
**zero**: it is computed and discarded. The sentence describes a behaviour the
instrument does not have, so it constrains nothing.

---

## 1. The problem, measured

### 1.1 At the bottom of SHAPE the note deck plays a decoy

Melodic lane, STEP 8, rate 0.5 Hz, VARY 0, SMOOTH 0, 20 s:

| SHAPE | what the five `Principle`s emit |
|---|---|
| **0.0** | all five **identical** — p2p 2.000, **5 distinct values**, and identical across seeds 999 / 12345 / 7 / 4242 |
| 1.0 | three of the four differ from `TwoMotif`; 12–13 distinct |

At SHAPE 0 the note deck plays `sin(2πk/8)` sampled at the step boundaries: the
same five values on every seed, every FORM, every SONG, every terrain. It spans
the **whole** 36-semitone pitch axis and clears 3–4 scale degrees — louder, in
pitch terms, than the real phrase — while carrying no melodic information at all.

### 1.2 The phrase is reachable almost nowhere

The phrase only appears in the top quarter of SHAPE, and only on the paths that
take `shape_value`. Measured with the real `generate()` over 20 000 terrains:

| | |
|---|---|
| mean drawn SHAPE | 0.316 |
| either deck above SHAPE 0.75 | 4.65 % |
| **either deck above SHAPE 0.95** | **0.01 %** |
| drone archetype (SHAPE capped at 0.25) | 49.2 % of terrains, **0.00 %** in that zone |

`P_SHAPE_A/B` appears in `kBaseRules` and in **no** story curve, so no Glow macro
can move it; only the in-lane offsets can, and they total ±0.40 on the melodic lane.

### 1.3 Where the phrase *is* already playing, RANGE flattens it

On a FLOW deck with a note engine the phrase is emitted directly at
`lane.cpp:551`, at **every** SHAPE — `shape_value` is never called. That is not a
corner case: `kArchWeight` drone = 0.50 (`taste.h:656`), and `mode_of` puts
**82.2 %** of drones in FLOW (`terrain.cpp:295-304`; the raw `kModeW` 0.15 is
tempered toward uniform), with carrier engines restricted to SYNTH / WAVE / BODY
(`:666-667`) — none of which disables FLOW melody (`part.cpp:43,441`). So on
**≈40.7 % of all terrains** the phrase already plays at every knob position.

There it is gated by RANGE alone. The pitch axis is 36 semitones
(`Quantizer::SPAN_SEMIS`, `quantizer.h:67`) and `LANE_PITCH` is handed depth 1.0
unconditionally (`part.cpp:98`). Measured through the real path
(`lane → apply_range → +(_tune−0.5) → clampf(0,1) at part.cpp:132 → Quantizer`),
STEP 8, SHAPE 1.0, TUNE 0.5, mean of 6 seeds:

| RANGE | 0.10 | 0.20 | 0.35 | 0.40 | 0.50 | 0.75 | 1.00 |
|---|---|---|---|---|---|---|---|
| semitones | 1.45 | 2.89 | 5.06 | 5.79 | **7.23** | 5.51 | 2.97 |
| scale degrees | 1.8 | 2.5 | 3.0 | 3.3 | **3.8** | 3.2 | 2.5 |
| samples clamped | 0 | 0 | 0 | 0 | 0 | **29.2 %** | **54.2 %** |

Two things to read off. The drone's RANGE band is `{0.10, 0.40}`
(`taste.h:991-992`) → **1.8 to 3.3 degrees**. And **the curve peaks at RANGE ≈ 0.5
and falls for the entire top half of the knob**, because `pitch_pre_quant`'s
`clampf(…, 0.f, 1.f)` starts discarding samples at r ≈ 0.55. At full RANGE more
than half of every phrase is clipped against the axis ends.

⚠️ **An earlier draft blamed a "unipolar/bipolar seam" in `apply_range` at
r = 0.5. That seam does not exist** — `apply_range` is provably continuous there
(`uni·(0.5·2) = uni`, and `lerpf(uni, v, 0) = uni`), and a direct sweep is
strictly monotonic. The residue was real, the mechanism was mislabelled
(`fireflow-ablation-verdict-discipline`). The clip is the cause, and its position
moves with TUNE, since `part.cpp:132` adds `(_tune − 0.5)` before clamping.

---

## 2. The design

### 2.1 The melody system stops going through the waveform bank

`lane.cpp:551`'s guard changes from `_flow_melody_on()` to **`_melodic && _flow_melody`**,
so a lane on a **note engine** emits its phrase directly, in STEP as in FLOW, at
every SHAPE.

**Not `_melody_engine_on()`.** That predicate (`_melodic && (_step_mode || _flow_melody)`,
`lane.h:200`) also catches a SAMPLER and a BBD deck in STEP, and a BBD's PITCH
lane is the delay clock, not a note (I3). Gating on `_flow_melody` instead
excludes both engine classes in **both** modes, because `part.cpp:43,441` pushes
that flag from the engine id: `_engine_id != ENGINE_SAMPLER && != ENGINE_BBD`.

⚠️ **Naming trap.** `_flow_melody` reads as "FLOW melody is on"; it actually means
"this deck has a note engine", and it is pushed in both modes. This is the shape
`fireflow-gotchas` records for `set_depth`. **Rename it `_note_engine` in the same
commit**, or the next reader will misjudge the gate exactly as draft 5 did.

**Measured on a patched build**, all six reachable states, 20 s at 0.5 Hz, SHAPE 0.5:

| state | stock | after |
|---|---|---|
| non-melodic, FLOW / STEP | 2.000 / 7734 · 1.750 / 9 | **unchanged** |
| melodic, FLOW, note engine | 0.351 / 8 | **unchanged** |
| **melodic, STEP, note engine** | **1.750 / 9** | **0.351 / 8** |
| melodic, FLOW, SAMPLER / BBD | 2.000 / 7734 | **unchanged** |
| melodic, STEP, SAMPLER / BBD | 1.750 / 9 | **unchanged** |

**Exactly one state changes.** SHAPE becomes inert on the note deck in both
modes — the consistency `roadmap.md:2519-2523` names as what the rework exists to
decide — and SAMPLER and BBD decks are bit-unchanged everywhere, so I3 needs no
exception and the `kBbdFlowRangeMax` chain is untouched.

**The gain is undiminished by the narrower gate.** FORM's audibility, STEP 8,
four seeds:

| | SHAPE 0.00 | SHAPE 0.50 | SHAPE 1.00 |
|---|---|---|---|
| stock: `Principle`s differing from `TwoMotif` | **0 / 4** | **0 / 4** | 3 / 4 |
| after | **3 / 4** | **3 / 4** | 3 / 4 |

Note the middle column: today the decoy reaches the **centre** of the knob, not
just its bottom. After the change every SHAPE position gives what only 1.00 gave
before, with per-seed value sets of 10–13.

What is given up: the crossfade in the top quarter, where the emitted value is
`f·pitch[slot] + (1−f)·wave_pulse(ph)`. `wave_pulse` is ±1, so at half weight the
note deck plays the phrase riding a hard square wave of half the axis — **18 of
36 semitones**. A shipped spec called that "a good melodic tool"; it is an
octave-and-a-half square trill, and this spec removes it without regret.
Redefining SHAPE for the melodic lane is a separate question and belongs with the
SHAPE work.

### 2.2 `LANE_PITCH` gets a pitch-shaped RANGE law

`apply_range` (`range.h:12-19`) is an **amplitude** law: unipolar below r = 0.5,
scaled by `2r`. Applied to a quantized pitch axis it produces §1.3's two defects —
too little ambitus at the bottom of the band, and clipping over the whole top half.

The note-engine lane gets its own law, expressed in **scale degrees**, with a
floor. The floor value is a by-ear call; G3 measures it and the owner rules.

**The gate is the same `_melodic && _flow_melody` as §2.1 — not `slot == LANE_PITCH`.**
This is what keeps the law out of the BBD's clock: `kBbdFlowRangeMax = kBbdFlowSemis / (2·60) = 0.00833`
derives its `2` **from `apply_range`'s unipolar behaviour**, stated verbatim at
`taste.h:586-588`, so any floor applied to a BBD deck would void that constant and
the owner's 2026-08-07 ruling.

An earlier version of this spec left the BBD-in-STEP case open, because
`_melody_engine_on()` is true there. Measurement closed it: the cap at
`flow.cpp:581-585` is FLOW-only (`&& !_mode_now`), and a BBD in STEP *is*
quantized onto scale steps (`part.cpp:241-242`, "STEP puts the clock on scale
steps so the bend is in the key") — so a degree floor would not be *meaningless*
there, but it would be a large unasked-for change: a drone-band BBD deck at
RANGE 0.10 travels ~12 semitones of clock today and would travel ~42 under a
floor at 0.35. Gating on the engine class avoids the question entirely rather
than answering it.

---

## 3. Gates

Each must be shown RED once (`fireflow-tests-must-be-able-to-fail`).

| # | Gate | Red when |
|---|---|---|
| G1 | **FORM changes the emitted sequence at SHAPE 0.0.** With five `Principle`s, STEP 8, seed 999: at least three must differ from `TwoMotif`, and the result must differ across seeds | §2.1 did not land. *Proven RED against stock: today all five are identical, 5 distinct values, identical across four seeds.* |
| G2 | A melodic lane's output is **identical at SHAPE 0.00 / 0.25 / 0.50 / 0.75 / 1.00**, sample for sample | the bank still reaches the melody |
| G3 | On a drone-band terrain (RANGE ≤ 0.4) the phrase clears **≥ 3 scale degrees** | the pitch RANGE floor is absent or too low |
| G4 | At RANGE 1.0 fewer than **5 %** of emitted samples are clamped at `part.cpp:132` | the clip §1.3 measures at 54.2 % survived |
| G5 | A **non-melodic** lane's output is unchanged by both changes, sample for sample, in both modes | the change leaked off the melody |
| G6 | A **SAMPLER** and a **BBD** deck's PITCH lane is unchanged, sample for sample, in **both** modes | the gate was written as `_melody_engine_on()` instead of `_melodic && _flow_melody` (I3). *Proven RED against that variant: it moves the STEP case from 1.750/9 to 0.351/8.* |

---

## 4. Blast radius

- `engine/mod/lane.cpp:551` — the guard (§2.1).
- `engine/mod/range.h:12-19` — the melodic branch (§2.2). Called from
  `lane.cpp:501, 777, 1024` — all three, not one.
- **`host/render/scenarios/ctrl_identity.json` — its hash WILL move.** Verified:
  `_range` defaults to `1.f` (`lane.h:243`), the scenario never calls
  `set_range`, and both melodic PITCH lanes are audible through
  `apply_range(v, 1.0)`. An earlier draft asserted this hash must not move; that
  check covered SHAPE, SMOOTH and STEP and missed RANGE. `wave_formant_sweep`
  moves too. These are the only two hash-gated scenarios
  (`CMakeLists.txt:227,249`).
- `tests/test_range.cpp` — the only direct test of `apply_range`.
- `tests/test_param_impact.cpp:310-340, 371-390` — encodes the exact FLOW/STEP
  asymmetry §2.1 deletes, and cites the SHAPE/SMOOTH spec by date.
- `tests/test_flow_runtime.cpp:344, 345, 419` — the BBD RANGE cap gates. Expected
  **unchanged** under the engine-class gate; they are G6's existing half and
  should be run as a regression, not edited.
- `engine/mod/lane.h` / `lane.cpp` / `super_modulator.h` — the `_flow_melody` →
  `_note_engine` rename (§2.1), plus `part.cpp:43,441` and the `set_flow_melody`
  setter chain.
- `tests/test_flow_melody.cpp` — the FLOW-melody cases; §2.1 makes STEP agree
  with them, which several may already assert.
- `docs/engine-map.md` §1 (row 5/6 of the state table) and §7 — both describe the
  behaviour this changes; update in the same commit.
- `shell/` and `bench/` compile `engine/` and must be rebuilt
  (`fireflow-bench-stale-object-trap`).

**Not touched:** `engine/flow/taste.h`, `engine/center/`, `host/vcv/src/init_patch.hpp`,
`gen_panel.py`. No stored value changes meaning, so there is no
`fireflow-control-merge-init-trap` exposure and the factory patch is unaffected.

---

## 5. What this does not do

- **SHAPE is left inert on the melodic lane.** That is an improvement over a knob
  whose bottom half plays a fixed sine, and it is not an answer. Giving SHAPE a
  new melodic meaning belongs to the SHAPE spec.
- **SHAPE's own reachability** (mean drawn 0.316, top quarter in 4.65 %) is
  untouched. After this spec it no longer gates the melody.
- **SMOOTH** is a separate spec.
- **The Marbles round** is unscheduled (`roadmap.md:2534`, "⬜ unscheduled",
  "Ordering is open") and unaffected by this work.
