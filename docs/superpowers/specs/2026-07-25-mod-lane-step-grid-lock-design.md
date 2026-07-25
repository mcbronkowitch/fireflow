# Modulation Lane Grid Lock in STEP Mode

**Date:** 2026-07-25
**Status:** Approved in brainstorming; not implemented
**Scope:** `engine/mod/` (lane, super_modulator), `engine/center/center.cpp`
rate hooks. No panel change, no new control.

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
produce a lasting offset. Six independent sources contribute, and three of them
never heal:

| # | Source | Effect in STEP |
|---|---|---|
| 1 | `kLaneRatio` `x3/4` / `x3/2` (`super_modulator.cpp`) | boundaries fall between master steps; realign only every 4 resp. 2 cycles |
| 2 | TIDE in free mode (`tide_free`) | continuous factor, the lanes never meet the grid again |
| 3 | DRIFT with COUPLE below maximum in the synced world (`center.cpp`, `mod_scale = pitch_scale * rate_drift^(1-couple)`) | texture lanes run at a permanently different rate than PITCH |
| 4 | EVOLVE rate walk at VARIATION > 0 (`lane.cpp`, `_evolve_outgoing_pattern`) | every lane walks its own `_ev_rate` by up to +-20 % |
| 5 | SPOT (`SuperModulator::spot`) | kicks each texture lane by up to +-1/2 cycle of phase, permanently |
| 6 | FLOW -> STEP entry (`snap_pitch_phase`) | only the PITCH lane is pulled onto the transport |

Sources 3, 4 and 5 produce offsets that nothing ever removes, which is why no
one-shot snap can fix this.

Patch compatibility is out of scope; the instrument is in development.

## Decision

**In STEP mode the texture lanes no longer own a clock.** They run on the
master lane's step clock and advance exactly one slot per master step. The lane
ratio stops being a rate factor and becomes a cycle length.

This works because the step clock is already normalized: `_phase_inc =
rate_hz / sr * (8 / steps)`, so one step lasts `sr / (8 * rate_hz)` regardless
of `_steps`. Giving all five lanes the same `rate_hz` therefore yields one
shared step grid no matter how long each lane's cycle is.

All six sources are removed by construction rather than patched individually:

- **1 and 2** — `kLaneRatio` and TIDE act on the slot count, which is an
  integer, so boundaries cannot land between master steps.
- **3 and 4** — the texture lanes inherit `pitch_scale` and the master's
  `_ev_rate`; no rate difference is representable.
- **5** — SPOT's phase kick is quantized to whole slots.
- **6** — STEP entry snaps all five lanes.

Because the alignment comes from the clock and not from a correction, it is
identical at every SHAPE setting. That is the requirement.

### Accepted cost: SOURCE and SIZE step resolution

SOURCE (`x2` today) and SIZE (`x1/2` today) lose their finer resp. coarser step
resolution. SOURCE currently steps twice as fast as the deck; it will step with
the deck and traverse its waveform in 4 slots instead of 8. Cycle length and
contour speed are unchanged, the number of sample points halves.

Note that SOURCE and SIZE are already grid-true today (`x2` and `x1/2` are
integer ratios). They suffer only from sources 3 to 6. Keeping their step
resolution was considered and rejected: SHUFFLE warps boundaries per lane step,
not per master step, so a lane at `x2` would shuffle differently from the
master and reintroduce a sub-step offset. One shared step duration makes SHUFFLE
correct for free.

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
// STEP: one rate for every lane, the ratio moves into the slot count
rate_hz[i]   = base_hz * pitch_scale                       // all five lanes
slots[i]     = clamp(round(STEPS * f[i] / tide), 2, 64)    // texture lanes
slots[PITCH] = STEPS                                       // TIDE never applies

// FLOW: unchanged
rate_hz[i]   = base_hz * scale[i] * tide_mult * kLaneRatio[i]
```

`tide` is the ladder ratio, so TIDE `x1/2` halves the lane speed and therefore
doubles the slot count. The slot counts are recomputed wherever they can
change: `set_step`, `set_tide`, `set_synced`, and the rate hooks that already
call `_apply_rate`.

MOTION and LEVEL are rounded from `x3/4` and `x3/2` to `x2/3` and `x4/3` so
that all lengths are clean 2- and 3-relations to the phrase. At STEPS = 8 the
set is 4, 6, 8, 12, 16; the five lanes are congruent again every 48 steps, or
six phrases. The polyrhythm is preserved, it is just deliberate now.

In a STEP deck TIDE always snaps to `kTideRatios`, even when the global SYNC
switch is set to Free. This is what removes source 2.

### Slot bounds

`clamp(..., 2, 64)`. The lower bound is functional, not cosmetic: with a single
slot the only boundary would sit at phase 0 and the lane would emit a constant
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
`mod_scale = pitch_scale * rate_drift^(1-couple)`. A STEP deck ignores
`mod_scale` and uses `pitch_scale` for all five lanes. DRIFT's other taps —
shape offset via `set_shape_offset` and detune — are untouched, so DRIFT still
moves something audible; it just no longer moves the lane rates against each
other. The free world is unaffected: `center.cpp` already assigns both scales
the same value there.

**COUPLE.** For a STEP deck in the synced world this leaves COUPLE without a
job, since "how loosely do the texture lanes follow" is precisely what is being
abolished. This is accepted and documented rather than replaced. COUPLE
continues to work unchanged for a deck in FLOW and for both decks in the free
world. Inventing a second function for the control is out of scope for this
work.

**EVOLVE rate walk.** At VARIATION > 0 each lane currently walks its own
`_ev_rate`. In STEP the four texture lanes take the master lane's `_ev_rate`
instead. The per-lane draw still happens and is discarded, so `_ev_phase` and
`_ev_shape` keep their established RNG progression. Those two stay per lane:
they displace the value that is read, not the boundary — `_compute_raw()` adds
`_ev_phase`, while the step index derives from the raw `_phase`.

**SPOT.** In STEP the phase kick is quantized to whole slots. It is applied
through `shuffle_phase_for_position(cur_step + n, ...)` rather than
`_phase += dphase`, so the jump lands on a real boundary under SHUFFLE as well,
with `n = round(dphase * slots)`. The shape kick is unchanged. SPOT still
stumbles, on the grid.

## Boundaries of the change

**FLOW is untouched.** `kLaneRatio` still acts on the rate there, TIDE stays
continuous in free mode, DRIFT and EVOLVE keep their per-lane freedom. The
entire switch hangs off the respective deck's STEP flag. Deck A in STEP locks
while deck B in FLOW is unaffected; each part owns its `SuperModulator`.

**STEP entry** sets all five lanes to slot 0. This widens `snap_pitch_phase()`
into a deck-wide snap. The warning in its comment — that a jump in the texture
lanes would be an audible lurch in filter and pan — becomes a documented
property instead of a prohibition; the existing SMOOTH slew absorbs it, and in
a rhythmic deck the transient reads as an accent. The onset-gap ring is zeroed
as before; the cost (the other deck briefly loses its tape taps) is unchanged,
because it already applied to the pitch snap.

**SHUFFLE** needs no special handling. All lanes share one step duration and,
as long as STEPS is even, one step parity, so `shuffle_boundary_phase` warps
identically everywhere. At odd STEPS a lane can receive an odd slot count (at
STEPS = 5: SOURCE 3, MOTION 8, LEVEL 4) and its final warp then differs from
the master's. This is a known tolerance, not a defect.

**Control-rate accuracy.** The four texture lanes run on the 96-sample raster
(`tick()`). Alignment is exact in lane time — the walker inside `tick()` places
every boundary at its true phase — but the output value becomes visible at the
next raster edge, up to roughly 2 ms later. This is the accepted asymmetry
already documented in `part.h` and the mod-plane control-rate spec, not a new
error.

## Testing

The core is a single invariant.

1. **Grid invariant.** Deck in STEP; SHAPE at 0, 0.25, 0.5, 0.75 and 1;
   VARIATION > 0; DRIFT > 0; TIDE across the ladder; SPOT fired. Assert that
   every `lane_fired(i)` of a texture lane coincides with a master step. This
   one test covers all six sources at once: if any of them returns, it fails.
2. **Shape independence.** The set of fire times is identical across all five
   SHAPE values.
3. **Slot table.** `slots[i]` for STEPS 2..16 against every TIDE rung,
   including the clamps at 2 and 64.
4. **STEP entry.** After FLOW -> STEP all five lanes stand at slot 0.
5. **Live STEPS turn.** 8 -> 16 -> 8 while running; alignment survives.
6. **FLOW regression.** FLOW behaviour is unchanged against today.
7. **`test_lane_tick`** stays green: the change touches both `process()` and
   `tick()`.

Renders are sanity checks only; there is no byte-identity gate.
