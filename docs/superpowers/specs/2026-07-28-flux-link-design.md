# FLUX link — one knob, two ways the neighbour reaches into the echo

**Date:** 2026-07-28
**Status:** design approved (owner, 2026-07-28), not yet planned
**Amends:** `docs/superpowers/specs/2026-07-28-flux-rhythm-drag-design.md`. That
spec is **not** superseded — its mechanism survives intact as this control's
positive half. What changes there: §2's control becomes bipolar, and the knob's
name.
**Implemented on:** branch `bbd-delay`, on top of the DRAG work (`3893525`).

## What this is

The DRAG knob becomes bipolar. Centre is neutral — nothing happens, and that is
the bit-identical path.

- **Right of centre:** DRAG, exactly as built. The neighbour's rhythm pulls the
  delay time, the clock moves, and moving a BBD's clock bends pitch. A
  sound-design gesture.
- **Left of centre:** the clock never moves. The delay stays on its RATE rung,
  and the neighbour's rhythm decides **which repeats sound**. Rhythm without
  pitch.

## Why the left half exists

The DRAG branch was played and the verdict was specific:

> ich hatte eigentlich gedacht dass das delay rhythmischer wird statt immer nur
> 1/4 jedes einzelne delay. jetzt bekomme ich diesen Gummiband-Effekt inkl.
> Pitch. Den wollte ich gar nicht, eigentlich nur rhythmische und ggf. tonale
> Variation, aber kein Pitch.

**That is not a tuning complaint, and no setting of DRAG answers it.** On a
single bucket brigade, uneven repeat spacing and pitch bending are the same
phenomenon: there is no read pointer, the only way to change the interval
between two repeats is to change the clock, and the clock simultaneously sets
the pitch of everything already stored. The rubber band *is* the mechanism.

So the left half does not move anything. It puts the repeats on a fine grid and
takes some away.

**This is the tap-bank idea reached by subtraction.** The retired taps design
(`2026-07-28-flux-rhythm-taps-design.md`) added delay heads at positions the
neighbour chose, and died on a measured 14.4 % of the block budget. Set RATE to
1/16 instead, let the echo run evenly there, and silence the repeats the
neighbour did not ask for: the same musical result — echoes at uneven intervals,
articulated by the other deck — for one multiply per sample. Nothing is added;
something is removed.

## 1. The control

Bipolar, `-1 … +1`, centre `0`, default `0`.

| knob | effect |
|---|---|
| `0` | neutral. Bit-identical to FLUX without this feature. |
| `0 → -1` | thinning depth: skipped repeats fade from full level to silent. |
| `0 → +1` | DRAG depth, unchanged (`2026-07-28-flux-rhythm-drag-design.md` §1.3). |

The two halves never run together, so neither has to reason about the other.
`apply_drag` takes `max(0, knob)` as its depth and is otherwise untouched; the
thinning path takes `max(0, -knob)`.

**This is not the "two meanings on one knob" the BBD spec rejected.** That
objection was about a *unipolar* travel whose meaning changes partway along —
a hidden mode switch you have to know the position of. A bipolar control with a
neutral centre is a different shape: the centre is unambiguous, each half is
monotone in one quantity, and the instrument already carries three of them
(MELODY, CHOKE, VARIATION are all `-1 … +1`).

**Name: `LINK`.** Four characters, fits the panel's label width, and it names
the *axis* — both directions are ways the other deck's rhythm reaches into this
echo — rather than one of its two ends. `DRAG` now describes only the positive
half and would mislabel the knob.

The param id is reused again, exactly as DRIVE→DRAG did: renamed in place in
`res/gen_panel.py`, `PART_STRIDE` stays 23, nothing before it moves. Old
patches carrying a DRAG value in `0..1` land on the same *value* at a
different *position* in the new range — a saved 0.5 was mid-travel and is
now three-quarters travel — so the positive half is bit-compatible by
accident of arithmetic — worth noting, not worth designing around.

## 2. The left half

### 2.1 The neighbour's rhythm, in units of this echo's repeats

```
n0 = clamp( round( gap[0] / (t_delay · sr) ), 1, kMaxSkip )
n1 = clamp( round( gap[1] / (t_delay · sr) ), 1, kMaxSkip )
```

`t_delay` is the RATE ladder's time. It does not move, so these are stable.

The pattern is then: one repeat sounds, `n0 - 1` are skipped, one sounds,
`n1 - 1` are skipped, repeat. RATE at 1/16 against a neighbour playing quarter
notes gives `n0 = 4` — every fourth repeat. Unequal gaps give unequal spacings,
which is the whole point.

`kMaxSkip = 16` bounds the sparse end. Beyond sixteen repeats between hits the
result stops reading as an echo pattern; clamping keeps something audible rather
than muting the control, and at a 1/16 rung sixteen repeats is a bar.

**The uniformity guard does not apply here, and the thinning path therefore does
not call `derive_intervals`.** It reads `rv.gap[]` directly. On the DRAG side an
even pattern is a failure — it produces a plain delay, which RATE already
offers. Here an even pattern is a *result*: `n0 == n1 == 4` at a 1/16 rung is a
quarter-note echo whose repeats still carry 1/16 resolution, and that is a sound
worth having. Spreading it to ×3/4 would damage it.

A degenerate gap needs no guard either: a buzz-length gap rounds to `n = 1`,
which means "every repeat sounds", which is silence-of-effect rather than a
malfunction.

### 2.2 The pattern advances on the accumulator DRAG already has

`Flux::process` already counts samples to the current delay time and fires at
each boundary (`_drag_phase`, `_drag_step_len`). That boundary **is** a repeat
arrival. The thinning path reuses it as its clock; only the interpolation on the
other side of the branch is skipped.

At each boundary:

```
++count
if (count == 1)        this repeat SOUNDS
if (count >= n[idx])   count = 0; idx ^= 1
```

The first repeat of each interval sounds and the rest are skipped, which puts
the audible event on the neighbour's onset rather than before it.

### 2.3 The gate, and where it sits

Skipped repeats are attenuated to `1 - depth`, not hard-muted: near the centre
the pattern is an accent, at full left it is a rhythm cut out of silence. The
sounding repeats stay at unity throughout.

The gain moves through a **3 ms one-pole ramp** (`kGateRampS`). This is the only
new smoother in the design, it sits on a level rather than on the clock, and it
therefore cannot interact with the delay-time slew the DRAG half depends on.
Without it a gate on a continuous signal clicks.

It multiplies the echo return **before `_mix_lin`**:

```
l += _echo_l.Process(l * send, hz) * _gate * _mix_lin;
```

**Output side, not the feedback path.** Gating inside the loop would stop the
skipped repeats regenerating and the tail would die unevenly; gating the return
leaves the train intact and selects what is heard from it.

### 2.4 What this actually sounds like, stated honestly

A BBD's output is continuous, not a series of discrete events — every repeat
overlaps its neighbours, more so at long tails. So the gate mutes a *window*,
not an object.

For percussive input that distinction does not matter: the window around repeat
*k* is dominated by repeat *k*, and the result is the rhythm this design
promises. For sustained input the same mechanism reads as a **rhythmic tremolo
on the wet return, locked to the echo's own grid and patterned by the other
deck** — a different sound, also musical, and not a defect. The ear pass should
audition both kinds of input deliberately.

## 3. Tonal variation

Out of scope here, and recorded so the next spec does not reach for the wrong
lever: tonal movement must come from a **filter** on the echo return, driven by
the same pattern. Not from STAGES — stage count and clock are related by
`f = stages / (2·t)`, so sweeping STAGES at a fixed delay time moves the clock
and brings the pitch bend back through the side door.

## 4. What survives from the DRAG branch

Everything except the sign of one number and half of one function:

- `engine/fx/drag.{h,cpp}` — `derive_intervals`, unchanged, still serving the
  positive half.
- The cross-deck rhythm push in `Instrument`'s control-rate block, unchanged.
- `Flux::set_rhythm`, the `rv.valid` self-gating, the step accumulator.
- The panel slot, and DRIVE's move to the right-click menu.
- `apply_drag`, unchanged apart from receiving `max(0, knob)`.

New: the two skip counts, the pattern counter, the gate gain and its ramp, and
the `set_link` setter replacing `set_drag`'s signature.

## 5. Testing

The load-bearing one first:

- **The clock does not move on the left half.** `clock_hz()` is constant across
  a full pattern cycle at LINK `-1`. This is the assertion that pins the user's
  actual requirement, and no existing test covers it.
- **Neutral is bit-identical.** LINK `0` reproduces FLUX without the feature,
  sample for sample — the same `==` shape the DRAG branch used.
- **The pattern is the neighbour's rhythm in repeat units.** RATE at a known
  rung, `gap[0]` four repeats long and `gap[1]` two: the gate is at unity on
  repeats 1, 5, 7, 11, … and at `1 - depth` between them.
- **Depth.** At LINK `-0.5` the skipped repeats sit at half gain, not silent.
- **`kMaxSkip` clamps** rather than mutes.
- **An even neighbour rhythm is preserved, not spread** — the assertion that
  pins §2.1's decision not to route this path through the uniformity guard.
- **Self-gating.** `rv.valid == false` → every repeat sounds at any LINK.
- **The positive half still behaves.** Every DRAG test from the previous branch
  passes unchanged once its `set_drag(x)` becomes `set_link(x)`.

## 6. Risks

1. **The gate's phase is free-running against the transport and against the
   input.** The pattern is periodic and correct in shape, but where it sits
   relative to the bar is arbitrary. This is the same open question the DRAG
   spec's §7 recorded, and it has the same answer: onset-locked stepping, still
   out of scope.
2. **The smear may blunt the pattern** at long delay times and high feedback,
   where adjacent repeats overlap heavily (§2.4). The lever if it does is RATE —
   a finer rung shortens the window — not a change to the mechanism.
3. **`kMaxSkip`, `kGateRampS` and the depth curve are ear values.** The depth
   curve in particular is specified linear in gain for want of a better guess.

## 7. Out of scope

- Onset-locked pattern phase (risk 1). The more expressive option, needing a
  cross-deck onset event `RhythmView` does not carry.
- The tonal filter of §3.
- Quantising DRAG's interpolated time to the ladder. Considered during this
  discussion and dropped: it addresses the positive half's unpredictability,
  which the bipolar split has now made a deliberate, opt-in character rather
  than the default behaviour.
- Any migration for patches saved before this change.

## 8. Errata (ear pass, 2026-07-28)

Implemented on `bbd-delay` as `db5d94c`, `f735f78`, `334c754`, `45ec3aa`,
`c944c78`. Auditioned by the owner in Rack and accepted as built.

**No tuning value moved.** `kMaxSkip = 16`, `kGateRampS = 0.003` and the
linear depth curve of §2.3 all stand as specified. §6 named these three as
ear values expected to shift; none did.

**§6's risks were not heard.** The free-running pattern phase (risk 1) and
the smear at coarse rungs with high feedback (risk 2) drew no objection in
play. That is weaker evidence than a deliberate hunt for them would be — the
pass confirmed the control works, it did not stress the two risks
individually — so neither is struck, only unconfirmed.

**§2.4's two input classes were not separately reported**, so the question
that section asks — whether the sustained-input reading (a rhythmic tremolo
on the wet return rather than separate echoes) earns its keep as a second
sound — remains open. Nothing about the mechanism changed, so §2.4's
description still holds; only its evaluation is outstanding.

### Two behaviours the implementation has that this spec did not describe

Both were surfaced by review, flagged to the owner before the ear pass, and
drew no objection. They are recorded here rather than fixed.

1. **Depth is dead until the next repeat boundary.** `_gate_target` is only
   written when the pattern advances, so moving the knob mid-repeat changes
   nothing audible until that repeat ends — 62 ms at the intended 1/32 rung,
   but half a second at 1/4 and longer at a slow tempo. Making the knob live
   is one guarded line in `set_link`; left out because a control that only
   commits on the grid may be the better feel, and that is an ear question.

2. **The gate also thins the FLUX excitation tap.** `PartFx` derives the tap
   from `(l - pre_flux_l)`, which is the gated contribution, so a deck
   selecting FLUX as an excitation source inherits the pattern. Coherent,
   arguably right, and not a decision this spec made.

### One property of the gate's smoother, for whoever tunes it next

The snap that returns the gate to exactly unity was widened from `1e-6` to
`1e-4` during the branch, because `1e-6` sat *below* the float32 stall floor
of the one-pole and therefore never fired — the branch that restores the
bit-exact path could not switch itself off. The floor is `8.9e-11 · sr`.

The widened threshold also swallows the first step away from unity, so
thinning depths below `1e-4 · kGateRampS · sr` pin the gate at 1 and do
nothing: 1.4 % of the knob's travel at 48 kHz, 5.8 % at 192 kHz. This dead
zone is inherent to snap-on-`fonepole` and cannot be tuned away — a smaller
threshold stops catching the stall. Anyone replacing the smoother should
know that both properties come as a pair.
