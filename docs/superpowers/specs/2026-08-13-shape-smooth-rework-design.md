# SHAPE and SMOOTH — repair the two axes, and split the melody out

**Status:** design, fourth draft. Supersedes drafts 1–3 (`1af9747`, `0714429`,
`1c3e6cc`) entirely; they are not a base to amend. All three were rejected by
their first review on assertions the code does not support, which is why
[`docs/engine-map.md`](../../engine-map.md) now exists and why **every number
below names the probe that printed it**.

**The change from draft 3:** the melodic lane's reachability is no longer part of
this spec. Measurement showed it is not a lane problem at all — the phrase is
emitted correctly and is unreachable for reasons that live in `engine/flow/`.
See §6.

---

## 0. Invariants this work must not break

Written first, because two of the three earlier drafts died here rather than on
their own mechanism.

| # | Invariant | Owner | How this spec respects it |
|---|---|---|---|
| I1 | **ENTROPY 0 = LOOP: nothing mutates, the buffer repeats exactly.** | `2026-07-12-…-entropy-sequencer-design.md:61` | §3.1 advances a *read* index over a buffer nothing writes. No draw, no mutation, exact repeat. |
| I2 | **"The SHAPE knob is a good melodic tool and stays as it is: sweeping it left of the S&H end blends the composed pitch with the lane waveform… That behaviour is not under review."** | `2026-07-25-mod-lane-step-grid-lock-design.md:12-14` | §3.1 changes only *non-melodic* lanes in FLOW. The melodic blend is untouched in both modes. |
| I3 | **The drone SHAPE cap `{0, 0.25}` is load-bearing for the calm-corner gate.** Reverting it alone moves 0x707 RMS 6.6e-03 → 9.9e-02, over the ceiling; the other two edits in that commit do not. | `taste.h:100-115` (reversion evidence) | Nothing here touches `taste.h`. §6 records that raising this cap is the obvious melody fix **and is forbidden**. |
| I4 | `kFlowNoteMinS = 0.060` and `kFlowSlewFrac = 0.35` are **set by arithmetic, then confirmed by ear** (owner, 2026-08-13). | `lane.h:266-275` | §3.2 must preserve the note floor. Any replacement of the slew ceiling needs a fresh listening pass, not a re-derivation. |
| I5 | FLOW melody mode is **off by default**; a missing push from `Part` must be a silent revert to the old sound, never a silent adoption of the new one. | `lane.h:35-40` | No new defaulted-on state is introduced. |
| I6 | No bit-exactness gates. Renders are sanity checks. | `fireflow-bit-exactness-not-required` | Gates below are behavioural, and §5 states which render hashes may move and why. |

---

## 1. Diagnosis, measured

Three complaints from the owner, and what the instrument actually does.

### 1.1 "Turning SHAPE is completely unpredictable"

It is not unpredictable. For the top **quarter** of its travel on a texture lane
in FLOW it does nothing but fade the modulation out, and whether it does that
depends on a different knob.

Above SHAPE 0.75 the bank crossfades pulse → S&H (`waveforms.h`). But
`_sh_slot()` **early-returns 0** for a non-melodic lane in FLOW
(`lane.cpp:564`, `if (!_step_mode && !_flow_melody_on()) return 0;`), so the
"S&H" value is one permanently held constant and the crossfade toward it is an
amplitude fade. Measured (texture lane, FLOW, 30 s, seed 999, VARY 0):

| SHAPE | 0.70 | 0.80 | 0.90 | 0.95 | 1.00 |
|---|---|---|---|---|---|
| p2p | 1.600 | 1.600 | 0.800 | 0.400 | **0.000** |
| distinct | 796 | 2 | 2 | 2 | **1** |

The law is `p2p = 2·(1 − 4·(sh − 0.75))`. It does **not** end in silence: the
lane parks on the held value, measured **−0.53 … +0.27** across ten seeds — a
permanent static offset, per seed. VARY is the only thing that makes the corner
move again (p2p 0.906 at VARY +0.5), which is why the knob's meaning appears to
change without the knob moving. *(Pinned by `tests/test_engine_map.cpp`, map §3.)*

Compounding it: SHAPE is the only axis whose played value is a **sum**. Four
sources reach `lane.cpp:554` — the knob, `_ev_shape` (±0.25), `_shape_offset`
(DRIFT, ±0.15·tap·weather·drift) and `_kick_shape` (SPOT, ±0.35 per draw,
accumulating). SPOT skips `LANE_PITCH` (`super_modulator.cpp:182`), so the
sustained hidden range is ±0.40 on the melodic lane and ±0.75 peak on a texture
lane. *(Map §2.)*

### 1.2 "In FLOW I am always at SMOOTH max; the middle does not exist"

Correct, and it is arithmetic, not misuse. The slew law is **absolute seconds**:

```
t = _fixed_slew ? 0.02 : 0.00002 · 25000^smooth        (lane.cpp:360)
```

so `t` runs 20 µs … 500 ms regardless of how fast the lane is running. A FLOW
texture lane cycles in 4–100 s. Against a 100 s cycle even the top of the knob is
a 0.5 % smoothing — inaudible. The usable behaviour is compressed into the last
few percent of travel, which is exactly the reported feel. SMOOTH is also the
**only** axis with no hidden contributors at all: one writer, one reader
(`lane.cpp:349` → `:360`). It is not unpredictable; it is inert. *(Map §2.)*

### 1.3 "Would printed shapes on the faceplate help?"

Partly, and less than expected. A printed ramp/pulse/S&H strip would describe
quarters 1–3 honestly. It would describe quarter 4 **falsely** on every texture
lane in FLOW, because the printed symbol would promise a waveform where the
instrument delivers a fade to a fixed offset. **Print the strip after §3.1, not
before.** Once the S&H end is real, the legend is true on every lane.

---

## 2. Approach considered and rejected

**Merge SHAPE and SMOOTH into one axis** (drafts 1 and 2). Rejected twice by
review, and the measurements say why: the two axes have different fan-in (four
writers vs one), different failure modes (a dead quarter vs a compressed law),
and on a FLOW note deck a merged knob would have to mean two things at once.
They are two faults in two controls, not one fault in a pair.

**Re-space the waveform bank** so the S&H end occupies more of the axis (draft
3 §4.4). It moves the dead zone rather than removing it, and it perturbs the
drone's `{0, 0.25}` band, which I3 forbids. Dropped.

---

## 3. The design

Two independent repairs. Either can ship without the other.

### 3.1 The S&H end becomes real on texture lanes in FLOW

`_sh_slot()` advances one slot per lane cycle for a **non-melodic lane in FLOW**,
walking the full 32-slot buffer. Melodic lanes are untouched in both modes
(`lane.cpp:551` returns the phrase before this code runs in FLOW melody mode;
STEP already advances `_cur_step`). Texture lanes in STEP already advance.

**Measured, texture lane, 40 s, VARY 0, seeds 999 / 12345 / 7:**

| | p2p | distinct | mean (DC) |
|---|---|---|---|
| today, frozen | **0.000** | 1 | −0.355 / +0.090 / −0.527 |
| **32 slots advancing** | **1.085 / 1.432 / 1.050** | 32 | −0.268 / −0.206 / −0.238 |
| 8 slots advancing | 0.436 / 0.741 / 0.525 | 8 | −0.241 / +0.173 / −0.423 |
| sine reference (SHAPE 0) | 2.000 | 17914 | −0.000 |

The dead quarter becomes a stepped waveform with **roughly half the sine's
ambitus**, and the parked DC offset *shrinks* from up to 0.53 to about 0.24,
because the lane now averages the whole buffer instead of sitting on slot 0.

**This is not draft 2's rejected mechanism.** Draft 2 replaced the entire bank
with a buffer walk and was rejected on a measured ambitus of 0.535 against the
sine's 2.000. That figure is the **8-slot** row above. Over 32 slots it is
1.05–1.43, and — decisively — the comparison here is not against the sine but
against **0.000**, because this changes only the quarter where there is currently
nothing at all.

**I1 (LOOP) holds by construction:** this advances a read index. No RNG is
consumed, no slot is written, and at VARY 0 the 32-value sequence repeats
exactly, cycle after cycle.

**Open, to be measured during implementation:** whether the slot should advance
per lane cycle or per some subdivision of it. One slot per cycle over 32 slots
means the pattern repeats every 32 cycles, which at drone rates is minutes. A
faster subdivision trades pattern length for step rate. Measure both against the
calm-corner gate before choosing; do not decide it in prose.

### 3.2 SMOOTH becomes interval-relative

`t` becomes a fraction of the lane's own interval rather than an absolute time,
so the knob means the same thing at every rate:

```
t = frac(smooth) · interval,   interval = the lane's own cycle
```

with three cases for `interval`: FLOW LFO → the lane cycle; FLOW melody →
`kFlowPhraseSlots`; STEP → `_steps`. **The note floor must survive:** use
`max(slot, _note_min_samples)` in melody mode, per `lane.cpp:362-365` — a raw
slot reference makes the glide far too tight where the note floor decimates.

Two things this spec does **not** settle, because they need ears and are not
derivable:

- **The top of the fraction.** Draft 3 proposed 0.80, justified by re-expressing
  `kFlowSlewFrac`. That justification is void: the clamp becomes unconditionally
  dead code under the new law (`0.3474·interval < 0.35·effective` always), and
  it was ear-confirmed against a *melodic note staircase*, not a texture LFO.
  An independent review measured that at fraction 1.0 a FLOW LFO lane loses
  **7.60 dB and 65.4°** at every rate against today's −0.02 dB at drone rates.
  Whether that is "SMOOTH finally does something" or "SMOOTH eats the modulation"
  is an ear question. **Pick the top by listening, then record it as a by-ear
  value.** (I4.)
- **`kFlowSlewFrac` must be deleted, not re-expressed**, since it can no longer
  be reached — and `test_flow_melody.cpp:563` ("STEP's slew is unchanged by the
  melody clamp") becomes unobservable and must be **retired, not rewritten**. A
  gate that cannot go red gets removed.

### 3.3 DRIFT's shape tap comes off the axis

`kShapeTap` / `kShapeMax` (`center.cpp:143-144`) are removed, and `_ev_shape`'s
clamp tightens from ±0.25 to ±0.10. This takes the melodic lane's hidden range
from ±0.40 to ±0.10 and a texture lane's peak from ±0.75 to ±0.45.

DRIFT keeps its rate and detune taps, so it still does what it is for — the
weather still moves the instrument, it just stops moving the one axis the player
is trying to aim. This is draft 3's §4.3 unchanged; no review found fault with it.

---

## 4. Gates

Each must be shown to go RED once before it counts (`fireflow-tests-must-be-able-to-fail`).

| # | Gate | Where | Red when |
|---|---|---|---|
| G1 | A texture lane in FLOW at SHAPE 1.0, VARY 0 emits **≥ 8 distinct values** over 40 s and p2p > 0.5 | `tests/test_engine_map.cpp` §4 case — **this inverts an existing pin** | the slot is frozen again |
| G2 | At VARY 0 the SHAPE-1.0 sequence is **exactly periodic**: samples `[0, N)` equal `[N, 2N)` for the 32-slot period | new | anything mutates or draws at LOOP (I1) |
| G3 | A **melodic** lane's output at every SHAPE, both modes, is **unchanged** from today, sample for sample | new | §3.1 leaked onto the melodic lane (I2) |
| G4 | SMOOTH 0.5 produces the same t/interval ratio at 0.05 Hz and at 2 Hz, within 2 % | new | the law is still absolute |
| G5 | The melody note floor still holds at 14 Hz (`test_flow_melody.cpp`'s existing case) | existing | §3.2 dropped `_note_min_samples` (I4) |
| G6 | With DRIFT swept 0→1, a lane's **composite SHAPE** stays within ±0.10 of the knob | new — observe at `ModLane` level, since DRIFT also moves rate | the tap survived |
| G7 | The calm-corner gate stays green (0x707 RMS under `kCalmCornerRmsMin`) | existing | §3.1 made the quiet terrains loud (I3) |

**G7 is the one that can veto §3.1.** Turning a frozen constant into a stepped
waveform adds modulation to every texture lane on every terrain, including the
drone. It is the exact class of change the drone SHAPE cap exists to prevent.
Run it early, not last.

---

## 5. Blast radius

Enumerated mechanically, because three drafts under-counted it.

- `engine/mod/lane.cpp` — `_sh_slot()` (§3.1), `_update_slew` (§3.2), the
  `kFlowSlewFrac` clamp (deleted), `_ev_shape` clamp at `:693` (§3.3).
- `engine/mod/lane.h` — `kFlowSlewFrac` declaration and its comment block.
- `engine/center/center.cpp` — `kShapeTap`, `kShapeMax`, and the two
  `set_shape_offset` calls at `:143-144`.
- `engine/mod/super_modulator.h:111` — `set_shape_offset` fan-out becomes dead;
  remove it rather than leaving an unreachable setter.
- **`host/vcv/src/init_patch.hpp:10,30`** — `SMOOTH_A = 0.836144507f`,
  `SMOOTH_B = 1.0f`. Under a new SMOOTH law these stored values mean something
  different, so **the factory sound changes unless they are converted**. This is
  `fireflow-control-merge-init-trap`, and it applies without any merge. Mirrored
  in `gen_panel.py:596,641` — change both or the panel guard fails.
- Tests: `test_engine_map.cpp` (G1 inverts a pin), `test_flow_melody.cpp:563`
  (retire), `test_evolve.cpp` (fixes `set_shape(0.25)` with a comment §3.3
  invalidates), `test_flow_taste.cpp:109,112`, `test_lane_tick.cpp`,
  `test_waveforms.cpp`.
- `bench/workloads_mod.cpp:31-34` — sets SHAPE/SMOOTH; re-measure, and beware
  `fireflow-bench-stale-object-trap`.
- **Render hashes:** `wave_formant_sweep` moves. **`ctrl_identity.json` must
  NOT** — it sets SMOOTH 0.0 (passthrough under both laws), never calls
  `set_shape`, never calls `set_step`. Pre-authorising it would contradict
  `check_render_hash.cmake:19-20`: *"only the hashes the change actually reaches
  may move. An unexplained one is a finding, not a baseline."*
- `shell/` and `bench/` compile `engine/` and must be rebuilt; neither has its
  own copy of these constants.

---

## 6. What this does not do: the melody stays unreachable

Drafts 1–3 all promised to make FORM, SONG and the phrase audible. Measurement
moved that problem out of this spec.

**FORM is not inert — it is unreachable.** At SHAPE 1.0, three of the four other
`Principle`s emit a different value set from `TwoMotif`. At SHAPE 0.0, none do:
all five collapse onto the same 5-value sine staircase. So the melody system
works, and lives only at the top of the SHAPE axis. Two gates keep it there, and
**neither is in `engine/mod/`**:

1. **Terrain draw.** Over 20 000 real `generate()` calls: mean drawn SHAPE
   **0.316**; either deck past 0.75 in **4.65 %**; either deck above 0.95 in
   **0.01 %**. The drone archetype — **49.2 %** of all terrains — never gets
   there, because `taste.h:998-999` caps its span at `{0, 0.25}`. Neither
   `P_SHAPE_A/B` nor `P_RANGE_A/B` appears in any story curve, so **no macro can
   move them**; only the in-lane offsets can, and after §3.3 those total ±0.10.
2. **RANGE, through the quantizer.** The pitch axis is 36 semitones
   (`part.cpp:228-229`) and `LANE_PITCH` gets depth 1.0 unconditionally
   (`part.cpp:98`), so RANGE sets the phrase's ambitus outright: **8.42 semitones
   → 4 degrees** at RANGE 1.0, **4.11 → 3** at 0.4, **2.57 → 2** at 0.25, and
   **1.03 semitones → 1 degree** at 0.1. A quarter of terrains draw deck A below
   0.25.

The obvious fix — raise the drone SHAPE cap — is **forbidden by I3**, with
measured evidence: that single span reverted takes 0x707 RMS from 6.6e-03 to
9.9e-02, over the ceiling.

So the melody's reachability is a `engine/flow/` problem about spans, archetype
weights and RANGE headroom under a calm-corner constraint. **It gets its own
spec.** Trying to solve it from the lane is what sank three drafts.

One inversion to carry into that spec: at SHAPE 0 the melodic lane emits a
seed-independent staircase that spans the *whole* pitch axis and clears 3–4 scale
degrees, while the real phrase moves less and is drawn almost never. **The
instrument plays the decoy loudly and the melody almost never.**

---

## 7. Ordering

1. **§3.3** (DRIFT off the axis) — smallest, no gate risk, and it makes every
   later measurement repeatable by removing ±0.30 of wander from the axis.
2. **§3.1** with **G7 first**. If the calm-corner gate goes red, the whole repair
   needs a per-archetype answer and that is a different conversation.
3. **§3.2**, ending with a listening pass to set the top of the fraction.
4. The faceplate strip (§1.3), once §3.1 has made the legend true.

The melody spec (§6) is unscheduled and sits against `engine/flow/`, which at 5.6
mean revision rounds per spec is the most expensive area in the repo — worth
knowing before starting it.
