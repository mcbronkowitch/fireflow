# Shared STEP Shuffle

**Date:** 2026-07-23

**Status:** Approved design

**Scope:** Engine, VCV Rack host and panel, offline renderer, tests

**Hardware mapping:** Deferred to M6

## Problem

The phrase and groove engine decides which steps fire, their note lengths, and
how patterns evolve, but every step boundary still lies on a straight grid.
The instrument needs a rhythmic shuffle control because groove has proved to
be one of its most important musical dimensions.

Shuffle must affect the complete STEP time base rather than only delaying
melodic triggers. Pitch, the four texture lanes, gates, sampler events, and
modulation must continue to move as one rhythmic system. At the same time,
shuffle must not disturb phrase length, downbeats, external-clock anchors, or
the center section's synchronization.

## User-facing behavior

One shared `SHUFFLE` control in the center section drives both parts.

- Range is normalized `0..1`.
- `0` is a straight grid.
- `1` produces a classic `2:1` long/short pair.
- Intermediate values interpolate linearly.
- Shuffle affects STEP mode only. FLOW is unchanged.
- Each pair starts on an even-numbered step, beginning with step 0.
- For an odd step count, the final unpaired step stays straight.
- A live control change is adopted at the start of each lane's next even step.
  The current pair finishes with its previously latched value.
- FLOW-to-STEP keeps the existing transport/partner phase snap. The current
  shuffle target is latched immediately for the entry step; normal even-step
  latching resumes at the next pair boundary.

The shared target is common to both parts, but each lane latches it at its own
pair boundary. This preserves the musical ratios between independently running
lanes without changing their phase or forcing an off-grid jump.

## Timing model

The engine keeps the existing straight raw phase. Only STEP boundary lookup is
warped.

For normalized shuffle amount `s`:

```text
long_step  = 1 + s / 3
short_step = 1 - s / 3
```

The units above are nominal straight-step lengths. Their sum is always `2`, so
every pair and every complete phrase retains its original duration.

At the endpoints:

```text
s = 0: long = 1,   short = 1    -> 1:1
s = 1: long = 4/3, short = 2/3  -> 2:1
```

The last step of an odd-length phrase is not treated as the first half of a
pair. It occupies exactly one nominal step and ends on the unchanged phrase
wrap.

## Architecture

### Shared API

`Instrument` gains:

```cpp
void set_shuffle(float normalized);
```

The value is clamped to `0..1`, defaults to `0`, and is forwarded to both
`SuperModulator` instances. Each `SuperModulator` forwards it to all five
`ModLane` instances.

### Boundary helper

Shuffle boundary calculations live in one small, allocation-free, deterministic
helper used by both lane execution paths. Given raw phase, step count, and the
latched shuffle amount, it provides:

- the current step index;
- the raw-phase position of the next step boundary;
- the current step's length in nominal-step units.

`ModLane::process()` and `ModLane::tick()` must call the same helper. The PITCH
lane remains sample-accurate; the four texture lanes retain their 96-sample
control raster and continue walking every crossed boundary in order.

The raw phase remains the source of truth for:

- cycle wraps;
- `Center` grid and hard-lock servos;
- COUPLE and DRIFT;
- external clock phase alignment;
- reset and STEP-entry snaps;
- the existing step-clock factor.

Shuffle must not enter any of those calculations.

### Live changes and mode changes

Each lane stores a target and a latched shuffle amount.

- Entering even step `0, 2, 4, ...` copies target to latched.
- A reset starts at step 0 and latches immediately.
- A FLOW-to-STEP transition preserves the existing `Center::_snap_phase`
  contract: under SYNC it lands on the current transport grid; under FREE it
  lands on the other part's phase. The landed step latches the current target
  immediately, even when it is odd, because there is no active pair from STEP
  mode to finish.
- Turning SHUFFLE during an odd step cannot shorten or extend the active pair.
- Changing `STEPS` live preserves the current audible step and the fractional
  progress within its long or short interval. The existing no-ghost-boundary
  contract remains in force.
- FLOW neither consults nor latches shuffle for output timing. On the next STEP
  entry, the transport/partner-snapped step adopts the current target.

Phase kicks retain the existing behavior: the final phase determines the
resulting current step, and skipped intermediate steps do not synthesize new
events.

## Groove and note behavior

Shuffle is independent of phrase composition:

- `GrooveCell::rank_of_slot` still decides which steps DENSITY reveals.
- `GrooveCell::note_len` still decides note length in steps.
- `NEW PHRASE`, PRINCIPLE, RENEW/GROW, and groove mutations keep their existing
  deterministic RNG draw order.
- No random draw is added by shuffle.

Because note holds end on step boundaries, their real-time duration naturally
follows long and short shuffled steps. The composed length remains expressed in
steps.

## Sampler behavior

The sampler receives its note and slice fires from the shuffled PITCH lane, so
its audible event timing follows the same long/short grid as the synth.

The grid-fallback source map remains straight. `_step_samples` continues to
describe the nominal tempo-grid spacing used to divide recorded material.
Shuffle must not move source slice positions, alternate slice lengths, resample
audio, or change pitch. It changes when a slice is fired, not where that slice
is cut from the recording.

Marker-mode traversal is unchanged apart from its shuffled fire times.

## Clock, sync, and reset

External clock input remains one pulse per beat and continues to define straight
beat anchors. The host-derived BPM and `Transport::clock_pulse()` behavior are
unchanged. Internal offbeat STEP boundaries move between those anchors.

Since the raw phase is unchanged:

- phrase downbeats remain aligned;
- RST still lands all lanes on raw phase 0;
- two equally configured parts remain aligned at the same shared shuffle value;
- parts with different divisions or step counts keep their intended musical
  relationship;
- hard COUPLE continues locking the raw PITCH clock rather than chasing warped
  offbeats.

## VCV Rack host and panel

VCV gains one shared normalized parameter, appended at the end of `ParamId` so
existing parameter IDs remain stable. The module pushes it through
`Instrument::set_shuffle()`. Existing patches load with the default straight
value.

The center panel is reflowed without changing the 42 HP width:

- TIME grows into a `2 x 2` control grid:
  - top row: `SYNC`, `TEMPO`;
  - bottom row: `COUPLE`, `SHUFFLE`.
- ROOM becomes a compact `3 x 2` grid that preserves semantic columns:
  - `SIZE` over `DECAY`;
  - `TONE` over `DIFF`;
  - `SMEAR` over `WOBL`.
- BLEND and DUO retain their controls and hierarchy.

The panel generator remains the single source of truth. Generated SVG and C++
tables are regenerated together, and panel overlap tests must pass.

## Offline renderer

Scenario parsing gains:

```json
{ "action": "set_shuffle", "value": 0.0 }
```

The value is shared; no `part` field is accepted or required. A listening
scenario demonstrates straight timing, an intermediate amount, full `2:1`
shuffle, and a live mid-pair change.

## Testing

### Timing helper

- Exact straight boundaries at `s = 0`.
- Exact `4/3` and `2/3` durations at `s = 1`.
- Representative intermediate values.
- Monotonic boundaries and strictly positive durations over `s in [0,1]`.
- Even, odd, one-step, and maximum supported phrase lengths.
- Pair and whole-cycle duration invariants.

### ModLane

- PITCH fire timestamps follow the expected long/short sequence.
- A mid-pair target change takes effect only at the next even step.
- Odd final step stays straight.
- FLOW output is bit-identical for different shuffle settings.
- Reset fires step 0 and latches the current target.
- FLOW-to-STEP retains the current transport/partner snap, aligns the sampler
  cursor to the landed step, and latches the current target there.
- A live `STEPS` change preserves audible step position without duplicate or
  skipped boundary events.
- Existing deterministic groove and variation behavior remains bit-identical
  at shuffle `0`.

### Raster equivalence

- `tick()` crosses the same boundaries, in the same order, as repeated
  `process()` calls.
- Cover straight, intermediate, and full shuffle; high reachable STEP rates;
  wrap events; live target changes; and odd step counts.

### Integration

- Both parts receive the same shared value.
- All five STEP lanes use shuffle while FLOW lanes remain unchanged.
- Center sync, COUPLE, RST, and external-clock tests retain raw-phase and
  downbeat invariants.
- Synth and sampler fires move to shuffled timestamps.
- Sampler grid-fallback slice positions and nominal source spacing do not move.
- VCV parameter IDs before SHUFFLE remain unchanged.
- Panel generation and collision tests pass.
- Renderer action parsing and listening scenario pass.
- The complete existing test suite passes.

## Alternatives considered

### Explicit alternating-duration scheduler

Giving each step a sample countdown directly represents long and short
durations, but it duplicates the existing phase clock and requires a larger
rewrite of tick traversal, STEP changes, and Center synchronization.

### Delayed offbeat event queue

Keeping a straight lane and buffering odd events is initially smaller, but it
separates triggers from modulation values, gates, note holds, and sampler
cursors. It cannot satisfy the requirement that shuffle affect the complete
STEP time base.

### Selected: raw-phase boundary warp

Warping only boundary lookup preserves the proven transport and synchronization
architecture while applying shuffle to every STEP consumer.

## Non-goals

- Per-part or per-lane shuffle amounts.
- Reverse or negative shuffle.
- Random or evolving shuffle.
- Swinging FLOW.
- Moving external-clock pulses or raw phase.
- Alternating sampler source-slice positions or lengths.
- Hardware gesture or panel mapping before M6.
