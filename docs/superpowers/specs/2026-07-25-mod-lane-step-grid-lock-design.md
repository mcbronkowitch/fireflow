# Modulation Lane Grid Lock in STEP Mode

**Date:** 2026-07-25
**Status:** Approved in brainstorming; revised during implementation after a
float-drift measurement invalidated the first mechanism (see "Why not equal
rates"). Not implemented.
**Scope:** `engine/mod/` (lane, super_modulator). No panel change, no new
control.

## Problem

The SHAPE knob is a good melodic tool and stays as it is: sweeping it left of
the S&H end blends the composed pitch with the lane waveform, which bends a
phrase into a contour. That behaviour is not under review.

What is under review is that the four texture lanes drift off the deck's step
grid, and that SHAPE exposes it. At the S&H end a phase offset only reads as
"different random values"; anywhere left of it the same offset reads as a ramp
that steps beside the beat. The report is that the texture lanes sit
permanently askew and that everything left of S&H feels mushy.

Permanently is the operative word. `kLaneRatio`'s `x3/4` and `x3/2` realign
after four and two master cycles respectively, so they cannot by themselves
produce a lasting offset. Seven independent sources contribute, and four of
them never heal:

| # | Source | Effect in STEP |
|---|---|---|
| 1 | `kLaneRatio` `x3/4` / `x3/2` (`super_modulator.cpp`) | boundaries fall between master steps; realign only every 4 resp. 2 cycles |
| 2 | TIDE in free mode (`tide_free`) | continuous factor, the lanes never meet the grid again |
| 3 | DRIFT with COUPLE below maximum in the synced world (`center.cpp`, `mod_scale = pitch_scale * rate_drift^(1-couple)`) | texture lanes run at a permanently different rate than PITCH |
| 4 | EVOLVE rate walk at VARIATION > 0 (`lane.cpp`, `_evolve_outgoing_pattern`) | every lane walks its own `_ev_rate` by up to +-20 % |
| 5 | SPOT (`SuperModulator::spot`) | kicks each texture lane by up to +-1/2 cycle of phase, permanently |
| 6 | FLOW -> STEP entry (`snap_pitch_phase`) | only the PITCH lane is pulled onto the transport |
| 7 | float32 phase accumulation | five independent phasors round differently; see below |

Sources 3, 4, 5 and 7 produce offsets that nothing ever removes, which is why
no one-shot snap can fix this.

Patch compatibility is out of scope; the instrument is in development.

### Why not equal rates

The first version of this design gave all five lanes the same `rate_hz` and
turned the ratio into a slot count, reasoning that the step clock is already
normalized: `_phase_inc = rate_hz / sr * (8 / steps)` makes one step last
`sr / (8 * rate_hz)` whatever `_steps` is, so equal rates should mean one
shared grid.

That holds in exact arithmetic and fails in float32. Each lane integrates its
own phasor, and the rounding error per addition scales with the magnitude of
the running phase — so how much a lane loses depends on how long its cycle is.
Measured on the per-sample path, two lanes at 2 Hz (a 3000-sample step) with
slot counts 8 and 16:

| Slots | Drift against the 8-slot lane after 60 s |
|---|---|
| 2 | -46 samples |
| 4 | -16 |
| 6 | -21 |
| 12 | -3 |
| 16 | ~-2050 |
| 24 | +7 |
| 32 | ~-3070 |

The 16-slot case is linear, not a jump: 102 samples after 3 s, roughly 2
samples per step, a full step of slip after about 90 seconds. SIZE at
STEPS = 8 is a 16-slot lane, so the worst case is the default configuration.
This is the same `_phase` drift already documented in the codebase (~0.0006
per cycle); it simply does not cancel between lanes with different cycle
lengths.

An equal-rate design would therefore have made the reported symptom slower
rather than absent.

## Decision

**In STEP the texture lanes do not integrate a phase at all.** They read the
deck's position instead: `SuperModulator` keeps an integer count of the steps
the PITCH lane has entered, and hands each texture lane that count together
with the fraction the deck currently sits at inside its step. A texture lane's
own position is `count mod slots` plus that fraction.

One phasor, four readers. Because the step index is an integer modulo and the
fraction is one shared float, no lane can round away from another — the grid
is exact by construction rather than by agreement between five accumulators.

The lane ratio stops being a rate factor and becomes the lane's slot count.
All seven sources are removed at once:

- **1, 2** — `kLaneRatio` and TIDE act on the slot count, which is an integer.
- **3, 4, 7** — a follower has no rate and no phasor, so there is nothing left
  to scale, walk, or round differently.
- **5** — SPOT's kick becomes an integer slot offset on the follower's index.
- **6** — STEP entry needs no snap at all: a follower's position is derived,
  never remembered, so the first follow call after the switch already puts it
  on the grid.

Because the alignment comes from the deck's own step count and not from a
correction, it is identical at every SHAPE setting. That is the requirement.

### Accepted cost: SOURCE and SIZE step resolution

SOURCE (`x2` today) and SIZE (`x1/2` today) lose their finer resp. coarser step
resolution. SOURCE currently steps twice as fast as the deck; it will step with
the deck and traverse its waveform in 4 slots instead of 8. Cycle length and
contour speed are unchanged, the number of sample points halves.

Sub-step resolution was considered and rejected. A follower advances when the
deck advances; giving SOURCE a boundary halfway through a deck step would mean
reintroducing an interpolated clock for it alone, which is the class of thing
this design exists to delete.

## Cycle lengths

The ratio becomes a length factor `f = 1 / ratio` on the phrase length:

| Lane | Rate today | `f` | Slots at STEPS = 8 |
|---|---|---|---|
| SOURCE | `x2` | 1/2 | 4 |
| SIZE | `x1/2` | 2 | 16 |
| PITCH | `x1` | 1 | 8 (= STEPS, always) |
| MOTION | `x3/4` -> `x2/3` | 3/2 | 12 |
| LEVEL | `x3/2` -> `x4/3` | 3/4 | 6 |

```
slots[i]     = clamp(round(STEPS * f[i] / tide), 2, 64)   // texture lanes
slots[PITCH] = STEPS                                       // TIDE never applies
```

`tide` is the ladder ratio, so TIDE `x1/2` halves the lane speed and therefore
doubles the slot count. MOTION and LEVEL are rounded from `x3/4` and `x3/2` to
`x2/3` and `x4/3` so that all lengths are clean 2- and 3-relations to the
phrase. At STEPS = 8 the set is 4, 6, 8, 12, 16; the five lanes are congruent
again every 48 steps, or six phrases. The polyrhythm is preserved, it is just
deliberate now.

In a STEP deck TIDE always snaps to `kTideRatios`, even when the global SYNC
switch is set to Free. This is what removes source 2.

### Rates

```
// STEP
rate_hz[PITCH] = base_hz * pitch_scale        // the only clock in the deck
rate_hz[i]     = base_hz * pitch_scale        // assigned but unused by followers

// FLOW: unchanged
rate_hz[i]     = base_hz * scale[i] * tide_mult * kLaneRatio[i]
```

Followers are still handed the master rate so that nothing reads a stale value
across a mode switch, but nothing in the follow path consumes it.

### Slot bounds

`clamp(..., 2, 64)`. The lower bound is functional, not cosmetic: with a single
slot the lane's only position would be slot 0 and it would emit a constant
value. At STEPS = 2, SOURCE therefore gets 2 slots rather than 1.

### Contour buffer beyond 32 slots

The contour buffer stays at 32 slots. Where `slots > 32` — for example SIZE at
STEPS = 16 with TIDE `x1/2`, which asks for 64 — the contour repeats inside one
cycle, because `_sh_slot()` reads with `% kSeqSlots`. Timing and wrap events
remain correct; only the movement repeats earlier than the loop. This affects
texture lanes exclusively and never the melody.

Capping the cycle at 32 instead was rejected: it would make TIDE partially
inert at high STEPS values, which is a worse failure than an early-repeating
contour.

## Center controls

**DRIFT rate wander.** In the synced world `center.cpp` computes
`mod_scale = pitch_scale * rate_drift^(1-couple)`. A STEP deck's followers have
no rate, so `mod_scale` simply has nothing to act on. DRIFT's other taps —
shape offset via `set_shape_offset` and detune — are untouched, so DRIFT still
moves something audible; it just no longer moves the lanes against each other.
The free world is unaffected: `center.cpp` already assigns both scales the same
value there. No change is needed in `center.cpp` for this.

**COUPLE.** For a STEP deck in the synced world this leaves COUPLE without a
job, since "how loosely do the texture lanes follow" is precisely what is being
abolished. This is accepted and documented rather than replaced. COUPLE
continues to work unchanged for a deck in FLOW and for both decks in the free
world. Inventing a second function for the control is out of scope for this
work.

**EVOLVE.** The rate walk needs no special handling: a follower inherits the
deck's timing whole, including whatever `_ev_rate` the PITCH lane is currently
walking, because it reads that lane's step count. `_ev_phase` and `_ev_shape`
stay per lane and keep walking — they displace the value that is read, not the
position, since the follower's slot index comes from the deck count and only
`_compute_raw()` adds `_ev_phase`.

**SPOT.** In STEP the phase kick becomes an integer offset on the follower's
slot index: `slot = (deck_count + offset) mod slots`. The offset persists, is
exact, and cannot leave the grid. Unlike a phase jump it needs no rounding and
no parity care — the boundary times come from the deck, not from the lane's own
warp, so any integer offset is safe. The shape kick is unchanged. SPOT still
stumbles, on the grid.

## Boundaries of the change

**FLOW is untouched.** `kLaneRatio` still acts on the rate there, TIDE stays
continuous in free mode, DRIFT and EVOLVE keep their per-lane freedom, and the
lanes integrate their own phasors as before. The entire switch hangs off the
respective deck's STEP flag. Deck A in STEP locks while deck B in FLOW is
unaffected; each part owns its `SuperModulator`.

**STEP entry.** `snap_pitch_phase` keeps its current job — pulling the PITCH
lane onto the transport — and needs no extension. The followers require no snap
because they hold no phase to be stale: the first follow call after the switch
derives their position from the deck count. The jump in filter and pan that the
existing comment warns about still happens, absorbed by the SMOOTH slew, and in
a rhythmic deck reads as an accent.

**Leaving STEP.** A follower resumes integrating from wherever its derived
phase last put it, which is a valid phase on its own grid, so FLOW starts
cleanly. No special handling.

**SHUFFLE** needs no special handling and no tolerance. Boundary times come
from the PITCH lane's warped grid, which every follower inherits by
construction, so the swing is shared exactly — including at odd STEPS, where
the equal-rate design would have had a parity mismatch. A follower applies its
own slot count only when mapping its integer position to a phase for the
waveform lookup, which affects the value read, never when a boundary happens.

**Control-rate accuracy.** The four texture lanes are followed on the
96-sample raster. Their position is exact in deck time, but a boundary becomes
audible at the next raster edge, up to roughly 2 ms later. This is the accepted
asymmetry already documented in `part.h` and the mod-plane control-rate spec,
not a new error.

**Steps per raster window.** At the fastest panel-reachable rate a step lasts
about 200 samples, so at most one deck step falls inside a 96-sample window.
The follow path must nevertheless handle a multi-step advance, because
`pitch_scale` can be pushed up by COUPLE and DRIFT: it walks however many slots
the integer count says have elapsed, firing each boundary and running each
cycle wrap in order.

## Testing

The core is a single invariant.

1. **Grid invariant.** Deck in STEP; SHAPE at 0, 0.25, 0.5, 0.75 and 1;
   VARIATION > 0; DRIFT > 0; TIDE across the ladder; SHUFFLE > 0; SPOT fired.
   Assert that every texture-lane fire coincides with a deck step. This one
   test covers all seven sources at once: if any of them returns, it fails.
2. **Long-horizon lock.** The same deck run for at least ten minutes of audio,
   asserting the fire count and alignment have not slipped by a single step.
   This is the test the equal-rate design would have failed, and it is the
   reason the design changed — it must exist.
3. **Shape independence.** The set of fire times is identical across all five
   SHAPE values.
4. **Slot table.** `slots[i]` for STEPS 2..16 against every TIDE rung,
   including the clamps at 2 and 64.
5. **Multi-step window.** A rate high enough that more than one deck step falls
   inside one 96-sample raster window still fires every boundary, in order,
   with wrap events in the right places.
6. **SPOT offset.** After a kick the lane is offset by exactly the requested
   number of slots and is still on the grid.
7. **Live STEPS turn.** 8 -> 16 -> 8 while running; alignment survives.
8. **FLOW regression.** FLOW behaviour is unchanged against today.
9. **`test_lane_tick`** stays green.

Renders are sanity checks only; there is no byte-identity gate on this work.
The `ctrl_identity` gate exists and must stay green, but it guards a synth
scenario, not the mod plane.
