# BBD Dynamic Stage Changes — Click-Free Design

**Date:** 2026-08-03

**Status:** Approved for implementation planning

## 1. Problem

The BBD part engine derives a separate clock and stage count for each stereo
line from `COLOR`. `Part` modulates the effective COLOR value with the MOTION
lane, so the two stage counts can change at the lane's RATE. The current
`BbdLine::SetStages()` changes the active circular-buffer length immediately.
When the current index lies beyond a newly shortened length it also assigns
`imem_ = 0`.

That index jump changes the oldest charge packet being read without a
transition. The fixed output reconstruction filter turns the discontinuity
into a short impulse. COLOR raises the left clock and stage count while lowering
the right clock and stage count, so a rising COLOR phase preferentially
shortens the right line and matches the reported right-channel crack.

An isolated mono render established the discriminating measurements:

- `COLOR = 0`: maximum adjacent-sample delta approximately `0.0117`, with
  bit-identical left and right output.
- Fixed `COLOR = 0.2`: maximum delta approximately `0.0117`.
- `COLOR = 0.2` modulated by MOTION at quarter-note RATE: maximum delta
  approximately `0.0873`, with recurring events on the RATE grid.

The existing stage-change test proves only that output remains finite and
bounded. It does not test continuity, so the defect is currently ungated.

## 2. Goals

1. Preserve BBD COLOR modulation by MOTION/RATE.
2. Remove audible discontinuities when either stereo line's stage count grows
   or shrinks.
3. Preserve the steady-state BBD law:

   ```text
   delay = stages / (2 * f_clk)
   ```

4. Preserve the existing COLOR decision: both channels remain on the same
   rhythmic grid while their stage counts, bandwidth and grain differ.
5. Preserve the existing compander, feedback, dither, loss-pole and output
   reconstruction-filter behavior.
6. Add no line-sized memory and no transcendental work to the audio path.
7. Keep the fixed-stage path bit-identical. A difference there means the
   full-ring equivalence or indexing is wrong and is not accepted as part of
   this fix.

## 3. Non-goals

- Removing MOTION's routing to COLOR.
- Replacing COLOR with pan, feedback cross-coupling or a static spectral tilt.
- Reintroducing different left/right repeat times as the stereo mechanism.
- Retuning COLOR's 30-cent endpoint, DETUNE, FILT, feedback, freeze or the BBD
  reconstruction filters.
- Adding a second BBD buffer or a second complete filter/compander path per
  channel.
- Hiding the problem with an always-on output low-pass or limiter.

## 4. Considered approaches

### 4.1 Persistent full ring with crossfaded read taps — selected

The physical write ring always spans the injected `max_cells_` memory. The
requested stage count selects a read distance behind the write head. A stage
change crossfades the old and new read distances over a fixed number of BBD
READ ticks before the existing reconstruction filter.

This removes the topology change that currently resets or prematurely wraps
the active ring. Both taps read the same continuously written charge history,
so no second buffer is needed. During a transition the extra work is one memory
read, a few scalar operations and index arithmetic per READ tick; outside a
transition the path still performs one read.

### 4.2 Output dezipper — rejected

A short correction ramp after a detected output step would be a smaller patch,
but a resize can produce another discontinuity later when the shortened ring
wraps or an expanded region becomes active. The approach would mask observed
impulses without removing the invalid ring transition.

### 4.3 Stop changing stages dynamically — rejected

Keeping stages fixed and moving only the clock, or replacing the stage spread
with a filter tilt, would avoid this defect. It would also change the accepted
COLOR behavior: repeat timing would move off-grid or the stereo mechanism would
cease to be the BBD stage/bandwidth difference selected in the original design.

## 5. Architecture

### 5.1 Stable physical write history

`BbdLine` replaces the active-length write wrap with a write head that always
wraps at `max_cells_`:

```text
write mem[write_index]
write_index = (write_index + 1) mod max_cells
```

For a settled delay of `N` cells, a READ tick reads:

```text
read_index = (write_index + max_cells - N) mod max_cells
```

At a fixed `N`, this emits the same chronological charge sequence as the
current N-cell active ring. The physical addresses differ when `N <
max_cells_`, but the samples, dither order and delay length do not.

The whole buffer remains cleared by `Reset()`. Because every WRITE advances
through the full ring, shorter and longer taps always address a coherent past
history rather than stale cells left outside a previous active length.

### 5.2 Requested stages versus active tap

The line tracks separate state for:

- the most recently requested and clamped cell count;
- the currently settled read distance;
- the source and destination read distances of an active transition;
- the transition position.

`SetStages(int stages)` retains its immediate clamping behavior and stores the
latest request. It never resets the write head and never changes the physical
ring length.

If no transition is active and the request differs from the settled read
distance, the next READ tick starts a transition. If a transition is already
active, the newest request replaces the queued target; the active endpoints are
not restarted. Coalescing intermediate targets prevents a 96-sample control
raster from repeatedly jumping a partially audible destination tap.

The public `cells()` observer continues to report the latest requested,
clamped cell count. That preserves its existing immediate-setter semantics;
the transition state remains an internal de-click mechanism.

### 5.3 READ-tick crossfade

An active transition reads both source and destination taps from the same write
history. Their charge values are blended before the existing output
reconstruction filter:

```text
x = transition_index / 15
w = x * x * (3 - 2 * x)
y_bbd = old_tap + w * (new_tap - old_tap)
```

The first transition READ uses `w = 0`, so it continues the old tap exactly.
The final transition READ uses `w = 1`, so it lands exactly on the new tap.
The transition index runs from 0 through 15, so sixteen READ ticks are the
fixed transition length. This gives approximately
1.6 ms at a 10 kHz BBD clock and 0.5 ms at the 32 kHz ceiling. At very low BBD
clocks it lasts longer in wall-clock time, but still supplies sixteen bounded
charge steps instead of completing between sparse READ events.

The smoothstep curve has zero slope at both endpoints and uses only arithmetic.
No sine/cosine lookup, division per audio sample or other `libm` work is added.
The division by 15 is represented by a compile-time reciprocal.

After the final READ, the destination becomes the settled tap. If the latest
requested cell count changed during the transition, a new transition begins
from that settled tap on the following READ. Only the latest queued request is
kept.

When no transition is active, the second memory read and crossfade arithmetic
are skipped.

### 5.4 Existing processing remains single-path

Only the raw charge read is crossfaded. The result continues through the one
existing `ybbd_old_` delta calculation and the one existing output
reconstruction-filter state. `BbdEcho` therefore retains one compander, one
feedback state, one loss pole, one reconstruction path and one injected buffer.

Feedback receives the crossfaded output exactly as it receives the current
single-tap output. No special feedback gain or freeze behavior is introduced.

### 5.5 Reset and degenerate memory

`Reset()` must:

- clear the full injected buffer as today;
- reset the full-ring write head to zero;
- cancel an active tap transition;
- make the settled tap equal the latest requested/clamped cell count;
- clear all existing BBD/filter states and reseed dither exactly as today.

With null or zero-sized memory, no WRITE or READ tick accesses memory and the
output remains finite, matching the current contract. `SetStages()` continues
to clamp its requested count to at least one logical cell even when no usable
buffer exists.

## 6. Data flow

```text
MOTION lane at RATE
  -> Part::_color_eff
  -> BbdEngine::set_width
  -> BbdEngine::_apply_width
  -> per-channel BbdEcho::SetStages
  -> BbdLine requested read distance
  -> old/new read-tap smoothstep (only while moving)
  -> existing reconstruction filter
  -> existing expander, feedback and BbdEngine mix
```

The left/right COLOR laws remain unchanged. A rising COLOR value can still
lengthen the left tap and shorten the right tap; neither operation changes the
physical write ring or resets its write head.

## 7. Testing

### 7.1 Symptom-level regression

Add an engine-level test using mono sustained input and fixed lane targets. At
96-sample intervals, drive `set_width()` with the same sinusoidal COLOR movement
that `Part` produces from MOTION. Run long enough to cover multiple modulation
cycles and ignore startup/swap settling.

The test must establish all of the following:

- the modulated-width render is non-silent;
- left and right differ, proving width really moved;
- both channels remain finite and within the engine's stated output bound;
- the largest adjacent-sample delta after settling is below `0.03` on each
  channel.

The `0.03` threshold is measurement-derived: the clean fixed-width cases are
approximately `0.0117`, while the current defect reaches approximately
`0.0873`. It leaves more than 2.5 times the clean program-material delta while
remaining below half the measured defective maximum in the isolated
reproduction.

The test must be run before implementation and fail on the delta assertion.

### 7.2 Line-level transition tests

Add focused `BbdLine` tests for:

- a shortening transition;
- a lengthening transition;
- a new target arriving before the current 16-READ transition completes;
- exact landing on the newest requested cell count;
- Reset during a transition;
- null/zero memory with pending stage changes.

Test-only observers may expose whether a transition is active and the settled
read-cell count under `SPKY_TESTING`. They must not add production behavior or
host-facing API.

### 7.3 Preserved contracts

The existing tests must continue to prove:

- COLOR 0 with mono input is bit-identical left to right;
- opened COLOR separates the channels while both repeat intervals remain on
  the same grid;
- fixed stage count retains the existing arrival time and clock law;
- clock changes still bend stored pitch;
- reset removes old charge;
- silence, denormal, freeze and output-bound contracts remain valid.

Before changing the line, capture a deterministic fixed-stage output sequence
or digest in a regression test. After the change it must remain identical. If
the chronological-equivalence claim does not produce bit-identical samples,
stop and inspect the first differing READ tick rather than relaxing the test.

### 7.4 Integration verification

Run the complete desktop test suite and both render-hash gates. Re-render the
isolated `COLOR = 0`, fixed-COLOR and MOTION-modulated-COLOR cases and report the
same adjacent-sample measurement for all three. The modulated case must meet
the `< 0.03` bound and must still produce stereo difference.

Provide the modulated render for a final headphone check because the defect is
an audible transient and the numeric bound is a regression gate, not a
substitute for listening.

## 8. Performance and memory constraints

- No additional line-sized allocation.
- No heap allocation.
- No additional complete filter, compander or feedback path.
- No audio-rate transcendental calls.
- Settled path: one charge read, matching the current read count.
- Transition path: two charge reads and one smoothstep blend per BBD READ tick
  for exactly sixteen READ ticks.
- Existing ITCM placement requirements for `BbdLine::Process` remain in force.
- The desktop and hardware benchmark builds must compile. If hardware is
  available, rerun the isolated BBD and `instrument_worst_bbd` rows; otherwise
  record hardware measurement as not run rather than inferring a result.

## 9. Files in scope

- `engine/fx/bbd.h`: stable full-ring indexing, tap-transition state and
  crossfade; update the incorrect current comment that stage changes cannot
  click.
- `tests/test_bbd.cpp`: fixed-stage identity/digest and line-level transition
  coverage.
- `tests/test_bbd_engine.cpp`: symptom-level modulated-COLOR continuity test.
- `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md`: amend the
  `SetStages` description and COLOR implementation notes to reference the
  click-free read-tap transition.
- BBD benchmark or build files only if verification exposes a required update;
  no benchmark redesign is part of this fix.

No host, panel, parameter, patch-migration or public engine-interface change is
required.

## 10. Acceptance criteria

The fix is complete only when:

1. MOTION/RATE still changes BBD stereo COLOR.
2. The engine-level reproduction passes the `< 0.03` adjacent-sample bound on
   both channels.
3. Fixed-stage output remains bit-identical to its pre-fix deterministic
   baseline.
4. COLOR 0 remains bit-identical left to right for mono input.
5. Opened COLOR remains stereo and both repeat intervals stay on the grid.
6. Shrink, expand, rapid retarget and Reset transitions pass focused tests.
7. The complete test suite and render-hash gates pass.
8. No new line-sized memory, heap work or audio-rate transcendental calls are
   introduced.
9. The generated modulated-COLOR WAV passes a headphone check without the
   rhythmic short impulse.
