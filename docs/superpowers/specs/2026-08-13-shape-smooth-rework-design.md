# SHAPE, SMOOTH and the melody — three axes that mean the wrong thing

**Status:** design, fifth draft. Supersedes drafts 1–4 (`1af9747`, `0714429`,
`1c3e6cc`, `3ed752d`). Draft 4 was rejected by two independent reviews; one built
a patched `lane.cpp` and measured the proposed change rather than the proxy the
spec published. Both rejections are folded in below and named where they land.

**Every number states its probe setup — rate, SHAPE, seed, window.** Draft 4's
header promised this and its headline table broke it, which is how a proxy
measurement reached a design decision.

---

## 0. Invariants this work must not break

| # | Invariant | Owner | How this spec respects it |
|---|---|---|---|
| I1 | **ENTROPY 0 = LOOP: nothing mutates, the buffer repeats exactly.** | `…entropy-sequencer-design.md:61` | §3.1 advances a *read* index. Verified on a patched build: at VARY 0 no RNG is consumed, no slot is written, and the wrap-aligned sequence is exactly period-32 over 100 wraps, 0 mismatches. |
| I2 | "The SHAPE knob is a good melodic tool and stays as it is: sweeping it **left of the S&H end blends the composed pitch with the lane waveform**… That behaviour is not under review." | `…mod-lane-step-grid-lock-design.md:12-14` | **Draft 4 misapplied this.** It describes the `shape_value` blend — the path FLOW melody mode *never takes* (`lane.cpp:551` returns before it). I2 protects the blend on the STEP note deck; it says nothing about FLOW melody, which `roadmap.md:2504-2508` records as "its own open question for the rework". §3.4 decides it, and states what is given up. |
| I3 | The drone SHAPE cap `{0, 0.25}` is load-bearing for the calm-corner gate: reverting it alone moves 0x707 RMS 6.6e-03 → 9.9e-02. | `taste.h:101-113` | Nothing here touches `taste.h`. **The mechanism is narrower than draft 4 assumed:** the cap matters because reverting it lets a drone draw into the *ramp/pulse* region, whose per-cycle discontinuity is the loudness source (`test_flow_taste.cpp:100-102`) — not because of the S&H end. §3.1 therefore cannot reach it (§4, G7). |
| I4 | `kFlowNoteMinS = 0.060` and `kFlowSlewFrac = 0.35` are set by arithmetic, then **confirmed by ear**. | `lane.h:269,275` | §3.2 preserves the note floor and adopts 0.35 as the *melody* top rather than re-deriving it. |
| I5 | FLOW melody mode is off by default; a missing push must be a silent revert to the old sound. | `lane.h:32-38` | No new defaulted-on state. |
| I6 | No bit-exactness gates. | `fireflow-bit-exactness-not-required` | Gates are behavioural; §5 names which hashes may move. |

---

## 1. Diagnosis

### 1.1 SHAPE's top quarter is an amplitude fade onto a fixed offset

Texture lane, FLOW, **rate 0.5 Hz**, 30 s, seed 999, VARY 0, SMOOTH 0:

| SHAPE | 0.70 | 0.80 | 0.90 | 0.95 | 1.00 |
|---|---|---|---|---|---|
| p2p | 1.600 | 1.600 | 0.800 | 0.400 | **0.000** |
| distinct | 797 | 2 | 2 | 2 | **1** |

Law: `p2p = 2·(1 − 4·(sh − 0.75))`, which falls out of `waveforms.h:32`
(`lerpf(pulse, sh_hold, f)`, pulse p2p = 2) analytically for `sh ≥ 0.75`. The
cause is `_sh_slot()`'s **early return** at `lane.cpp:564`, taken before
`_cur_step` is read. It does not fade to silence: the lane parks on the held
value, **−0.53 … +0.27** over ten seeds. *(Pinned: `tests/test_engine_map.cpp`.)*

SHAPE is also the only axis whose played value is a **sum** of four sources
(`lane.cpp:554`): knob, `_ev_shape` ±0.25, `_shape_offset` (DRIFT), `_kick_shape`
(SPOT ±0.35/draw). SPOT skips `LANE_PITCH` (`super_modulator.cpp:182`), so the
hidden range is ±0.40 sustained on the melodic lane, ±0.75 peak on a texture lane.

### 1.2 SMOOTH fails at **both** ends, in opposite directions

The law is absolute seconds — `t = 0.00002 · 25000^smooth` (`lane.cpp:360`),
i.e. 20 µs … 0.5 s — while the lane rate spans four decades. Measured over
20 000 real terrains, composing `free_hz(P_RATE) × pace_mult × tide_free ×
kLaneRatio[i]` for the four texture lanes:

```
texture-lane cycle: min 0.025 s   max 168 s
  < 4 s: 43.9 %     4..100 s: 55.5 %     > 100 s: 0.7 %
```

**Draft 4 claimed "4–100 s" and diagnosed only the slow half.** Both halves are
broken, oppositely:

- **Slow half (56 %).** Against a 100 s cycle the top of the knob is 0.5 %
  smoothing — inaudible. This is the reported "SMOOTH in the middle doesn't
  exist; only all the way up".
- **Fast half (44 %).** At a 0.025 s cycle the top of the knob is **20× the
  cycle** — it does not do nothing, it annihilates the lane.

`_smooth` has exactly one writer and one reader (`lane.cpp:349` → `:360`), so
nothing hides this; it is the law itself. An interval-relative law fixes both
ends with one change, which is the strongest argument in this spec.

### 1.3 The melody plays a decoy loudly and itself almost never

Measured, melodic lane, STEP 8, VARY 0, rate 0.5 Hz, 20 s:

- At **SHAPE 1.0**, three of the four other `Principle`s emit a different value
  set from `TwoMotif` — FORM works.
- At **SHAPE 0.0**, **none** do. All five collapse onto the same 5-value sine
  staircase, spanning the whole pitch axis and clearing 3–4 scale degrees.

So at the bottom of the knob the note deck plays a seed-independent,
FORM-independent sine — audible, and carrying no melodic information. Draft 4
called this a decoy and then left it in place.

**Draft 4's reason for leaving it is refuted.** It argued the melody is gated by
the terrain's SHAPE draw, which lives in `engine/flow/`. But `kArchWeight`
drone = 0.50 (`taste.h:656`), `kModeW` drone = 0.15 (`:689`) → **85 % of drone
terrains are FLOW**, and the carrier engine may only be SYNTH/WAVE/BODY
(`:666-667`), none of which disables FLOW melody (`part.cpp:43,441`). On
**≈42 % of all terrains** the carrier deck therefore emits the phrase *directly*
at `lane.cpp:551`, **at every SHAPE value** — the SHAPE cap gates nothing there.

What gates it on that dominant case is **RANGE alone**, and `apply_range` is
`engine/mod/range.h:12-20`, called from `lane.cpp:501,777,1024`. Measured
(SHAPE 1.0, STEP 8, mean of 6 seeds, `Quantizer::SPAN_SEMIS = 36`,
`quantizer.h:67`):

| RANGE | 0.10 | 0.20 | 0.35 | 0.40 | 0.50 | 0.75 | 1.00 |
|---|---|---|---|---|---|---|---|
| semitones | 1.24 | 2.47 | 4.33 | **4.83** | **4.69** | 9.27 | **8.43** |
| scale degrees | 1.5 | 2.3 | 3.5 | 3.2 | 3.0 | 4.5 | 4.7 |

The drone band is `{0.10, 0.40}` (`taste.h:991-992`) → **1.5 to 3.2 degrees**.
And the law itself is wrong-shaped for pitch: the span **falls** from 0.40 to
0.50 (the seam where `apply_range` switches unipolar → bipolar) and **falls
again** at 1.00 (the bipolar result clipping the 0..1 axis). Harmless for an
amplitude destination; backwards for a quantized pitch axis.

---

## 2. Rejected approaches

**Merge SHAPE and SMOOTH** (drafts 1–2). Different fan-in, different failure
modes, and on a FLOW note deck one knob would have to mean two things.

**Re-space the waveform bank** (draft 3 §4.4). Moves the dead zone rather than
removing it, and perturbs the drone band I3 protects.

**Move the melody's reachability to an `engine/flow/` spec** (draft 4 §6).
Refuted by §1.3: on the dominant case the gate is in `engine/mod/range.h`.

---

## 3. The design

Four repairs. §3.1–§3.3 are independent; §3.4 depends on nothing.

### 3.1 The S&H end walks the whole buffer **per cycle**

`_sh_slot()` advances for a non-melodic lane in FLOW — **32 slots per lane
cycle**, so the S&H quarter is one complete waveform per cycle, exactly like the
three quarters below it. The advance is gated on `!_melodic`, **not** on the
early-return condition (see G3 below and §3.4).

**Draft 4 proposed one slot per cycle. Measured on a patched build, that is dead
where the instrument lives** (SHAPE 1.0, VARY 0, 40 s, seed 999):

| rate | 0.5 Hz | 0.25 | 0.1 | 0.05 | **0.02** |
|---|---|---|---|---|---|
| p2p | 0.918 | 0.475 | 0.064 | 0.043 | **0.000** |
| distinct | 20 | 10 | 4 | 2 | **1** |

`kRateFreeMin = 0.02 Hz` (`divisions.h:57`) is exactly what a drone draws at
RATE 0. There the repair is **bit-identical to today's frozen constant**, and the
32-slot pattern would need 27 minutes. One-slot-per-cycle also makes SHAPE
rate-dependent, which is the very defect §3.2 exists to remove from SMOOTH.

**Whole-buffer-per-cycle removes both problems.** The emitted ambitus is a
property of the buffer, not the rate: `_fill_walk()` → `pg_contour_walk`
(`lane.cpp:662-665`) is drawn once at init. Measured over a complete pass,
seeds 999 / 12345 / 7: **p2p 1.085 / 1.432 / 1.050, 32 distinct, mean −0.286 /
−0.202 / −0.249**, against the sine's 2.000 and today's 0.000.

Two things this spec states rather than hides:

- **The top quarter is still an amplitude fade**, from 2.000 to ~1.2 — a 3–6 dB
  drop across the last quarter. Shallower than today's fade to zero, not absent.
- **The buffer is a gravity-damped contour walk, not sample-and-hold**
  (`phrase_gen.h:41-50`). It is correlated, and its mean is systematically
  negative (−0.20 … −0.29). A printed S&H symbol would still overpromise, so
  **§1.3's faceplate answer is: print a stepped-contour glyph, not an S&H
  glyph** — and G8 holds the DC offset to a bound rather than assuming it away.

### 3.2 SMOOTH becomes interval-relative, with **two** tops

```
τ = frac(smooth) · interval
frac(s) = s · TOP        (linear; the audible taper comes from the one-pole)
interval:  FLOW LFO -> the lane cycle
           FLOW melody -> one SLOT (kFlowPhraseSlots), floored at _note_min_samples
           STEP -> one step
```

`τ` is the one-pole time constant (`lane.cpp:386,395`, `k = 1/(τ·sr)`), so for a
cycle `T` the attenuation is `1/√(1+(2πτ/T)²)`:

| frac | 0.05 | 0.1 | 0.2 | **0.35** | 0.5 | 0.8 | 1.0 |
|---|---|---|---|---|---|---|---|
| dB | −0.4 | −1.5 | −4.1 | **−7.6** | −10.4 | −14.2 | −16.1 |
| phase | 17° | 32° | 52° | **65°** | 72° | 79° | 81° |

A usable, monotonic axis with a real middle — **for texture**. Not for melody: a
note must arrive inside its own slot, which is what `kFlowSlewFrac = 0.35` (I4,
ear-confirmed) encodes. So:

- **`TOP_TEXTURE = 1.0`** for the four texture lanes.
- **`TOP_MELODY = 0.35`**, reusing the ear-confirmed constant rather than
  re-deriving it.

**Draft 4 deferred this to "an ear question". It is not one** — no single top
satisfies both cases, so the ear cannot resolve it. What *is* an ear question,
and is deferred honestly: whether `TOP_TEXTURE = 1.0` is too much once heard.

Two corrections to draft 4 in this area:

- Its claim that `kFlowSlewFrac` "becomes unconditionally dead code
  (`0.3474·interval < 0.35·effective` always)" is **false under its own
  definition of `interval`**. Measured at the rate `test_flow_melody.cpp`'s floor
  case runs (14 Hz): under the *phrase* reading, τ = 1191 samples against a cap
  of 1008 — the clamp binds. It is true only under the *slot* reading, which is
  why `interval` is pinned to the slot above. The clamp becomes redundant with
  `TOP_MELODY = 0.35` and may be folded into it, but that is a simplification,
  not a proof of unreachability.
- Its plan to **retire** `test_flow_melody.cpp:564` is wrong-reasoned. That test
  goes RED *by design* here (STEP's slew changes from absolute 0.5 s to
  `frac × _steps`), which is a behaviour change to re-baseline, not a gate that
  cannot fail. `fireflow-tests-must-be-able-to-fail` covers the second case only.

### 3.3 DRIFT's shape tap comes off the axis — and the cost is named

`kShapeTap` / `kShapeMax` (`center.cpp:14,17`, applied at `:143-144`) are
removed, and `_ev_shape`'s clamp tightens from ±0.25 to ±0.10 (`lane.cpp:693`).
Hidden range: melodic lane ±0.40 → ±0.10, texture lane ±0.75 → ±0.45.

**This trades against the owner's Marbles goal, and draft 4 did not say so.**
`_ev_shape` walks only under `_variation > 0` — it is VARY's *only* reach into
the shape axis, and this cuts it by 60 %. `kShapeTap` fans one slow shared
weather signal out to shape, rate and detune with different per-deck taps, which
is literally "several modulations changing at once, non-linearly but coherently".
Removing it narrows the axis the scheduled Marbles round
(`roadmap.md:2534-2568`) is chartered to build on.

**The ruling, stated so it can be overturned:** a knob the hand cannot aim is
worse than a knob with less weather on it, and DRIFT keeps rate and detune, so
the instrument still breathes. If the Marbles round wants coupled motion back on
this axis, the right shape is weather *around* the knob within a visible band —
the knob as reference, not as one addend among four. That is the round's
decision, not this spec's.

### 3.4 The melody stops playing a decoy

`lane.cpp:551`'s guard is hoisted from `_flow_melody_on()` to
`_melody_engine_on()` (`lane.h:200`), so **any** melodic lane running the melody
system emits its phrase directly, in STEP as in FLOW, at every SHAPE.

Consequences, measured:

- The SHAPE-0 sine staircase (§1.3) is gone. FORM and SONG become audible at
  every SHAPE position instead of only above 0.75, which the terrain draws in
  4.65 % of cases and never on a drone.
- SHAPE becomes inert on the melodic lane in **both** modes. Today it is already
  inert in FLOW melody mode; this makes the two modes agree, which
  `roadmap.md:2519-2523` names as what the rework exists to decide.

**What is given up, plainly:** the blend I2 protects — sweeping SHAPE left of the
S&H end to bend a phrase into a contour — on the STEP note deck. That is a real
melodic tool. This spec removes it rather than keeping a control whose bottom
half plays a fixed sine on every seed. **Redefining SHAPE for the melodic lane
(glide amount, or phrase-vs-walk blend) is the honest successor and is out of
scope here**; leaving the decoy in place to preserve the tool is not.

And `LANE_PITCH` gets its own RANGE law in `engine/mod/range.h`, expressed in
**scale degrees** rather than amplitude, with a floor. From §1.3's table a floor
equivalent to RANGE ≈ 0.35 clears ~3.5 degrees; the law must also remove the
0.40→0.50 seam and the fall at 1.00, both of which are artefacts of an amplitude
law applied to a quantized axis. The floor value is a by-ear call (G9 measures,
the owner rules).

---

## 4. Gates

Each must be shown RED once (`fireflow-tests-must-be-able-to-fail`).

| # | Gate | Red when |
|---|---|---|
| G1 | A texture lane in FLOW at SHAPE 1.0, VARY 0 emits ≥ 24 distinct values **within two lane cycles, at 0.02 Hz and at 5 Hz** | the advance is rate-dependent again |
| G2 | At VARY 0 the **wrap-aligned slot sequence** is exactly period-32 over ≥ 64 wraps | anything mutates or draws at LOOP (I1). *Not sample-aligned from t=0: `_cur_step` is −1 before the first wrap, so cycle 1 is offset and the naive form goes red for the wrong reason.* |
| G3 | A **melodic** lane's output is unchanged by §3.1 in all **three** melodic states — including `_melodic && !_step_mode && !_flow_melody`, the PITCH lane of every SAMPLER and BBD deck | the advance was keyed on the early return instead of `!_melodic`. Measured: keying on the early return gives that lane p2p 0.363 / 5 distinct where stock gives 0.000 / 1 |
| G4 | SMOOTH 0.5 gives the same τ/cycle ratio at 0.05 Hz and 5 Hz within 2 % | the law is still absolute |
| G5 | The melody note floor still holds at 14 Hz | §3.2 dropped `_note_min_samples` (I4) |
| G6 | With DRIFT swept 0→1, composite SHAPE stays within ±0.10 of the knob — **at `Center`/`Instrument` level, via a new test accessor** | the tap survived. *Cannot sit at `ModLane` level: composite SHAPE has no observable, and the only injection path is the setter §3.3 deletes — a `ModLane` gate would be vacuous* |
| G7 | The calm-corner gate stays green — **run against §3.2, not §3.1** | drone SMOOTH `{.5,.9}` (`taste.h:1000-1001`) reinterprets under the new law and reaches every terrain |
| G8 | A texture lane at SHAPE 1.0 has \|mean\| < 0.30 over a complete buffer pass | the walk's DC bias grew |
| G9 | On a drone-band terrain (RANGE ≤ 0.4) the phrase clears ≥ 3 scale degrees | the pitch RANGE floor is absent or too low |
| G10 | FORM changes the emitted sequence at SHAPE **0.0**, not only at 1.0 | §3.4 did not land |

**G7 is aimed at §3.2, not §3.1 — this reverses draft 4.** Measured: `0x707`, the
calm-corner terrain, is itself ARCH_DRONE with SHAPE_A 0.077 / SHAPE_B 0.158, and
`shape_value` reads `sh_hold` **only in `case 3`** (SHAPE ≥ 0.75). Over 20 000
terrains, drone decks in that zone: **0 of 9 905**. §3.1 provably cannot reach the
calm corner; a gate aimed there could not go red, which is the vacuous shape
`fireflow-vacuous-test-gates` records. §3.2 reaches every terrain at every SHAPE.

---

## 5. Blast radius

- `engine/mod/lane.cpp` — `_sh_slot()` and the FLOW wrap (§3.1); `_update_slew`
  `:360` and the `kFlowSlewFrac` clamp `:376-377` (§3.2); `_ev_shape` clamp
  `:693` and the `_shape_offset` summand `:554` (§3.3); the guard at `:551`
  (§3.4).
- `engine/mod/lane.h` — `set_shape_offset` `:139`, `_shape_offset` `:304`,
  `kFlowSlewFrac` `:275`.
- `engine/mod/range.h:12-20` — the pitch branch (§3.4).
- `engine/center/center.cpp` — `kShapeTap` `:14`, `kShapeMax` `:17`, the calls at
  `:143-144`. `engine/mod/super_modulator.h:111` — remove the dead fan-out.
- **`host/vcv/src/init_patch.hpp:10,30`** — `SMOOTH_A = 0.836144507f`,
  `SMOOTH_B = 1.0f`. Under the new law these mean something else; the factory
  sound changes unless converted (`fireflow-control-merge-init-trap`). Mirrored
  in `gen_panel.py:596,641` — change both or the panel guard fails.
- **`engine/flow/taste.h:1000-1001`** — `P_SMOOTH_A/B` drone `{.5, .9}`,
  "drone = glassy". Today that is τ = 3–181 ms against a 4–100 s cycle
  (cosmetic); under the new law it is 0.5–0.9 **× the cycle**, and the drone's
  texture lanes very nearly stop. **This reaches ~50 % of all terrains and is the
  single largest behavioural consequence in the spec.** Decide the conversion
  before any listening pass. Draft 4 omitted it.
- Tests: `test_engine_map.cpp` (G1 inverts a pin), `test_lane.cpp:65-80` (the
  only direct test of the axis §3.3 removes — it will not compile),
  `test_super_modulator.cpp:70-83`, `test_flow_melody.cpp:564` (re-baseline, not
  retire), `test_evolve.cpp`, `test_flow_taste.cpp:109,112`, `test_lane_tick.cpp`,
  `test_waveforms.cpp`.
- `bench/workloads_mod.cpp:25,27` (and `setup_lane_flow_s10()` `:35`, exactly the
  case §3.1 changes); `bench/audition/init_patch.cpp:55,57`. Beware
  `fireflow-bench-stale-object-trap`.
- **Render hashes:** only two scenarios are gated (`CMakeLists.txt:227,249`).
  `wave_formant_sweep` moves. **`ctrl_identity.json` must NOT** — verified by
  parsing it: `set_smooth` twice with 0.0, zero `set_shape`, zero `set_step`.
- `shell/` and `bench/` compile `engine/` and must be rebuilt.

---

## 6. What this still does not do

- **Marbles (the owner's goal 4) is still the scheduled VARY round**, and §3.3
  narrows its axis. §3.3's ruling is written to be overturned by that round.
- **Terrain reachability.** After §3.4 the melody no longer needs SHAPE, so the
  0.01 % draw above SHAPE 0.95 stops mattering for the melody — but SHAPE's own
  reachability (mean drawn 0.316, fade zone 4.65 %) is untouched, and whether the
  bank's four quarters deserve equal shares of a knob the terrain samples that
  way is an `engine/flow/` question.
- **SHAPE's meaning on the melodic lane.** §3.4 makes it inert in both modes,
  consistently. Giving it a *new* melodic meaning is the honest successor spec.

---

## 7. Ordering

1. **§3.4** — the melody. Largest audible gain, no gate risk from I3, and it
   makes every later SHAPE measurement interpretable because the note deck stops
   emitting a waveform. G9, G10.
2. **§3.3** — smallest, and it removes ±0.30 of wander that would otherwise
   blur §3.1's measurements. G6.
3. **§3.1** — with G1 measured at **both** 0.02 Hz and 5 Hz. G2, G3, G8. Run G7
   as a cheap sanity check, not as a veto.
4. **§3.2** — **with G7 as its real veto**, and the `taste.h` drone-SMOOTH
   conversion decided *before* the listening pass that sets `TOP_TEXTURE`.
5. The faceplate glyph (§3.1), once the top quarter is a stepped contour.
