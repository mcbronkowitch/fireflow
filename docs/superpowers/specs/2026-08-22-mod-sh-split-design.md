# MOD depth split: S&H left / continuous right — design

**Date:** 2026-08-22
**Status:** designed, not implemented
**Plan:** `docs/superpowers/plans/2026-08-23-mod-sh-split.md`

**Scope:** engine (`engine/mod/`, `engine/parts/`) + `FireflowHW` host layer.
Builds directly on `2026-08-22-mod-latch-layer-design.md` (implemented); this
spec changes the *meaning* of that layer's depth knobs, not its structure.

> **Revised 2026-08-23.** §2's "STPS gains a FLOW meaning" and §3's FLOW
> texture row were withdrawn: STPS is measurably unreachable in FLOW on the
> target host. The FLOW grid is a fixed 8-slot constant instead. The
> withdrawn claim and the measurement that killed it are in §7.

## 1. Concept

While the MOD latch is active, every depth knob becomes **bipolar around
noon**:

- **Noon = standstill.** Depth 0, the parameter sits on its sound value.
- **Right of noon = continuous modulation, 0..100 %.** Exactly the current
  behaviour: the assigned lane's output, scaled by depth × master MOD.
- **Left of noon = S&H derivation of the same lane, 0..100 %.** The lane's
  output sampled at its slot boundaries and held — the parameter *steps*,
  with hard edges, independent of SHAPE and SMOOTH.

One knob, one lane, two readings of it. The left half exists so a parameter
can be stepped even when the lane itself is set smooth — and so a smooth and
a stepped target can ride the *same* lane on one deck.

## 2. Decisions taken in the brainstorm

- **The S&H clock is a slot grid the lane already owns, not a new TEMP grid**
  ("Raster B"). In STEP that grid is the lane's own slot count — which is
  derived from the deck's STEPS, so it is synced to the sequencer via the
  follower and nothing is lost. In FLOW the lane steps at its own cycle
  (RATE/PACE), divided into a **fixed 8 slots** (§3). A TEMP-locked grid for
  FLOW is explicitly deferred, not rejected ("später können wir immer noch
  ein Temp-Raster dazu holen").
- **One mechanism in the engine, both paths consume it** (approach 1 of 2).
  The lane computes its own stepped output; signed depth selects it. The
  host does not rebuild boundary detection the lane already owns, and the
  `shell/` firmware inherits the feature with the engine core.
- **What is sampled is the finished lane output** — after SHAPE, after
  SMOOTH. Edges are therefore always hard; SMOOTH only changes *which*
  values the grid catches. Literal reading of the request: stepping
  independent of shape.
- ~~**STPS gains a FLOW meaning as a side effect, deliberately.**~~
  **Withdrawn 2026-08-23** (owner's call, after probe 2 in §7): on the VCV
  host the STPS knob position that *selects* FLOW is 0, so in FLOW every
  lane carries `_steps == 1` and a `_steps`-derived grid produces no edges
  at all. FLOW uses `kShFlowSlots = 8` instead — the same slot count the
  FLOW melody lane already runs on (`kFlowPhraseSlots`), reachable from
  every host, no new control.
- **No swing in FLOW:** the shuffle latch is STEP-only and stays STEP-only.
- **Init sounds exactly like today:** inherited engine boot depths land on
  the right half (+1.0 / +0.7 / +0.55), every other depth inits at noon.
- **Pitch anchor untouched:** the pitch lane is not part of the `_tdepth`
  system (`part.cpp` `_mod_term`, `d = 1` for `LANE_PITCH`); TUNE is
  host-computed and gets its S&H half there like every other host target.
  RANG stays the pitch lane's amplifier.

## 3. Engine: the second lane output

`ModLane` gains a held value `_stepped_out` and an accessor
`stepped_output()`. Latch rule: whenever the lane's S&H slot index changes,
sample the finished output of that same call. The slot index comes from
where the lane already knows it:

| Lane state | Slot source | Grid |
|---|---|---|
| STEP (follower / standalone) | `_cur_step` | the lane's OWN slot count (`_steps`), which `lane_slots()` derives from the deck's STEPS + TIDE; SHFL swing |
| FLOW texture LFO | `step_index(_phase, kShFlowSlots)` | 8 straight slots per lane cycle |
| FLOW melody (note deck) | `_cur_step` | `kFlowPhraseSlots` (8), straight |

The FLOW texture case is the only new boundary detection — `_cur_step` is
explicitly unreliable on that path (engine map §4: the FLOW LFO's
`next_edge` is always 1.0), which is why the slot derives from `_phase`
directly. Probe 2 (§7) confirms `step_index(phase, 8)` yields exactly 8
transitions per cycle on the production `tick()` path, at every rate from
0.25 Hz to `kRateFreeMax`.

Note that in STEP the five lanes carry *different* slot counts (measured
4/16/8/12/6 at deck STEPS 8, probe 1) — the earlier draft's "deck grid,
STEPS count" was wrong about that. It does not change the mechanism: each
lane samples on its own `_cur_step` change, which is what "the lane's own
slot boundaries" always meant.

`SuperModulator` mirrors `_out_stepped[]` beside `_out[]` (updated on the
same tick raster), `Part` and `Instrument` pass through
`lane_output_stepped(part, slot)` beside the existing `lane_output`.

## 4. Signed depth

- `Part::set_target_depth` / `set_fx_target_depth` (`part.h:120` / `:124`):
  clamp widens from `0..1` to `−1..1`.
- `_mod_term` (`part.cpp:120`) and `fx_target_value` (`part.cpp:170`): the
  **sign** of the stored depth picks `lane_output` vs.
  `lane_output_stepped`, the **magnitude** scales. Nothing else in either
  formula moves — master MOD, the sampler SOURCE exponent, the LEVEL floor
  all stay.
- The LED law reads `_mod_term` through `lane_excursion` (`part.h:263`), so
  the LEDs report the S&H variant correctly with zero extra code.

## 5. Host (`mod_layer.hpp` + push loop)

- `modded()` accepts signed depth; negative selects the stepped lane term.
  The identity early-return survives, now at `|depth|` inside the deadband —
  the existing bit-exact depth-0 identity test keeps leaning on it.
- **Knob mapping:** depth params become bipolar −1..+1, default 0, with a
  small deadband (~±0.04) around noon so standstill is exact without a
  detent. Engine-backed inits move to the right half per §2.
- **FX activation** becomes `set_fx_target_active(slot, depth ≠ 0)` (after
  deadband) instead of `depth > 0`.
- **Center column:** one depth knob, its sign selects for *both* decks —
  left of noon pushes
  `0.5 × (MOD_A × stepped_A + MOD_B × stepped_B)`. The sum of two
  staircases is itself a staircase (edges on both decks' grids); no extra
  clock.

## 6. Panel and params

- Param ids stay; only range and default change (dev alpha, patch breakage
  is a non-issue). Panel print unchanged — the zone-accent rings keep
  meaning only "modulatable". Tooltips read "⟵ S&H · mod ⟶ <name> depth".
- `gen_hw_panel.py` / guards: no geometry change; only the param range table
  and the depth-param guard follow the bipolar range.

## 7. Measured facts this design stands on

### Probe 1 — the FLOW slot count on the production push path

2026-08-23, scratchpad (`probe_flowsteps.cpp`), recipe `docs/engine-map.md`
§6. Setup: `SuperModulator`, `init(48000, seed 12345)`, then exactly the call
`Part::set_step` forwards. Printed: `lane_slots_for_test(i)` (== `_steps`)
and `lane_effective_length_for_test(i)`.

| Call | source | deck_steps | lane steps |
|---|---|---|---|
| `set_step(false, 0)` | VCV FLOW (`Fireflow.cpp:1097`) | 1 | 1 1 1 1 1 |
| `set_step(true, 8)` | VCV STEP 8 | 8 | 4 16 8 12 6 |
| `set_step(false, 0)` after STEP | VCV, live mode switch | 1 | 1 1 1 1 1 |
| `set_step(false, 8)` | render host (`param_table.h`) | 8 | 8 8 8 8 8 |
| `set_step(false, 16)` | render host | 16 | 16 16 16 16 16 |

On the VCV host the STPS knob is 0..16 and `set_step(p, steps > 0, steps)` —
the position that selects FLOW *is* 0, so `_steps` is 1 there and no STPS
setting can raise it without leaving FLOW. Only the render host and `shell/`
(separate mode param, `P_STEPS_A` 2..16) can reach a FLOW deck with a real
slot count. This is what withdrew §2's STPS bullet.

A note deck's LANE_PITCH is the exception: `_effective_length()` reads 8
(`kFlowPhraseSlots`) even in VCV FLOW, which is why §3's melody row stands
unchanged.

### Probe 2 — S&H on the production texture path

2026-08-23, scratchpad (`probe_sh_flow8.cpp`). Setup: bare `ModLane`,
`set_melodic(false)` **before** `init()`, 48 kHz, seed 12345,
`set_step(false, 1)` (FLOW as the VCV host pushes it), SHAPE 0, RANGE 1,
VARY 0, 20 s driven by **`tick()`** — the 96-sample raster
`SuperModulator` actually uses for texture lanes
(`super_modulator.cpp:167-168`), not `process()`. The S&H shadow is the
proposal: `slot = step_index(phase, slots)`, sample the value that same
call returned when the slot index changes. "Expected" = cycles × slots + 1
(the initial latch).

| Case | rate | cycles | edges | expected | cont p2p | cont distinct | held p2p | held distinct |
|---|---|---|---|---|---|---|---|---|
| 8-slot grid, SMOOTH 0 | 0.5 Hz | 10 | 81 | 81 | 1.9994 | 611 | 1.9994 | 6 |
| 8-slot grid, SMOOTH 0.7 | 0.5 Hz | 10 | 81 | 81 | 0.9411 | 3931 | 0.9054 | 32 |
| **1-slot (`_steps` today)** | 0.5 Hz | 10 | **1** | — | 0.9411 | 3931 | **0.0000** | **1** |
| 8-slot, SIZE lane ×½ | 0.25 Hz | 5 | 40 | 41 | 0.9411 | 7863 | 0.9050 | 32 |
| 8-slot, SOURCE lane ×2 | 1 Hz | 20 | 161 | 161 | 0.9411 | 1965 | 0.9076 | 32 |
| 8-slot, fast | 5 Hz | 100 | 801 | 801 | 0.9411 | 714 | 0.9179 | 56 |
| 8-slot, `kRateFreeMax` | 30 Hz | 600 | 4801 | 4801 | 0.9407 | 146 | 0.9341 | 71 |

What each row carries:

1. **The fixed FLOW grid holds on the raster that ships.** Edge counts equal
   cycles × 8 + 1 exactly at every rate tried, including 30 Hz where a slot
   is only ~2 tick calls wide. The 96-sample raster neither skips a slot nor
   double-counts one. (The ×½ row lands at 40 rather than 41 because 5 cycles
   in 20 s ends exactly on a boundary — an endpoint, not a skip.)
2. **A `_steps`-derived FLOW grid is not a slow staircase, it is a flat
   line.** One latch, then `floor(phase × 1)` never changes again: p2p
   0.0000 over 20 s. This is the number that withdrew the STPS bullet.
3. **S&H holds hard regardless of SMOOTH:** at SMOOTH 0.7 the continuous
   twin visits 3931 distinct values on this path, the held stream 32.
4. **The held values are NOT ≤ slots distinct per run** — slot boundaries
   land on slightly different samples each cycle, so 10 cycles give 32
   distinct helds, not 8. A test gate must assert *edges only at slot
   changes* and *orders of magnitude below the continuous twin*, not a
   per-cycle distinct cap.
5. **Sampling shaves p2p slightly** (0.9054 vs. 0.9411 at SMOOTH 0.7) — the
   grid rarely catches the exact extremes. Expected S&H behaviour, worth a
   sentence in the release notes, not a defect.

### Probe 3 — the STEP half (inherited, 2026-08-22, `probe_sh.cpp`)

Same recipe, `process()` path, `ModLane` standalone:

| Case | cont p2p | cont distinct | held p2p | held distinct | transitions |
|---|---|---|---|---|---|
| STEP 8st SHAPE 0 SMOOTH 0.7 | 1.9616 | 149 673 | 1.9615 | 14 | 81 |
| STEP 8st SHAPE 0 SMOOTH 0 | 2.0000 | 5 | 2.0000 | 5 | 81 |

**STEP at SMOOTH 0: left and right half are the same signal** (5 distinct,
p2p 2.000 both) — the follower is already a staircase. The audible
difference between the halves grows with SMOOTH and is largest in FLOW.

## 8. Testing

- **Lane gate:** `stepped_output()` changes value only when the S&H slot
  index changes (count edges against transitions), and its distinct count is
  orders of magnitude below the continuous output's at SMOOTH > 0, in FLOW
  and STEP. Per §7 probe 2.4, no per-cycle distinct cap.
- **FLOW grid gate:** a FLOW texture lane driven by `tick()` produces
  exactly `kShFlowSlots` S&H edges per cycle, and does so at
  `kRateFreeMax` too — the case a per-sample probe cannot see.
- **Engine gate:** at negative depth, `target_raw` / `fx_target_value` move
  as a staircase where the positive twin of equal magnitude glides;
  magnitude symmetry within the clamp behaviour.
- **Existing depth-0 identity test stays green** (deadband maps to exact 0
  before the setter).
- **LED law:** no new test needed — it reads `_mod_term`.
- Every new gate proven RED once (house rule). No bit-exactness gates;
  absolute epsilons per engine map §5.

## 9. Out of scope

- A TEMP-locked S&H grid for FLOW decks (deferred by decision, §2).
- Making STPS reachable in FLOW on the VCV host. Considered and rejected
  2026-08-23: `_deck_steps` feeds more than the S&H grid —
  `part.cpp:390` pushes `_mod.pitch_step_samples()` into the sampler
  unguarded by mode, and `step_samples()` scales with the slot count, so
  raising FLOW's count from 1 to 8 would move the sampler's step clock by a
  factor of 8. Its own design question, with its own listening pass.
- `shell/` firmware wiring (inherits the engine mechanism; pot pickup is an
  M6 question).
- CV over depths (MOD1..4 jacks), unchanged from the parent spec.
- Any change to lane shuffle semantics in FLOW.
