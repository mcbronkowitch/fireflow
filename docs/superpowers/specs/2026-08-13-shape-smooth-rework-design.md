# SHAPE + SMOOTH rework — design

**Date:** 2026-08-13
**Status:** design, not implemented
**Follows:** the FLOW melody engine (`docs/superpowers/specs/2026-08-13-flow-melody-engine-design.md`)
**Precedes:** the Glow rework (`docs/roadmap.md`)
**Evidence base:** `docs/2026-08-13-glow-macro-audit.md`, plus the measurements in §1 taken during this session

**Revision:** second draft, after two reviewers.

Draft 1 proposed "one axis, two renderings" — a trajectory for slot-walking lanes,
the existing waveform bank for FLOW LFO lanes. Both reviewers rejected it
independently and for the same reason: on a FLOW note deck **both renderings run
at once from one knob**, and they diverge (§2.2). Draft 1 also called
`lane.cpp:611` a defect and proposed removing it; that line is the ENTROPY LOOP
contract, and removing it restores the "note salad" the entropy sequencer was
built to end (§1.5). Draft 2 collapses to a single mechanism, which repairs both
at once and costs less code than draft 1 did.

Every correction the code-truth reviewer verified is carried inline; the claims
draft 1 got wrong are marked where they mattered.

## 1. The problem

The owner's report, in his own words: turning SHAPE is unpredictable and does not
feel good; in FLOW he keeps SMOOTH at the top; a middle SMOOTH setting effectively
does not exist — only "down for rhythm" and "fully up for flow". He asked whether
he had been operating the knobs wrongly.

He had not. Both reports are the correct reading of the code.

### 1.1 SMOOTH has two usable positions because its scale is not musical

`ModLane::_update_slew()` sets the glide time to `t = 0.00002 * 25000^smooth`
seconds (`engine/mod/lane.cpp:360`), in absolute seconds:

| SMOOTH | 0.00 | 0.25 | 0.50 | 0.60 | 0.75 | 0.90 | 1.00 |
|---|---|---|---|---|---|---|---|
| glide time | 0.02 ms | 0.25 ms | 3.2 ms | 8.7 ms | 40 ms | 180 ms | 500 ms |

Anything below roughly 10 ms is not heard as gliding, only as "immediate". **The
lower 60 % of the travel is one setting.** Everything musical lives above 0.75. A
knob whose usable range is its top quarter is operated exactly the way the owner
operates it.

The absolute scale is the deeper defect: a fixed number of milliseconds has no
musical meaning across tempo changes, so the control cannot mean the same thing
twice.

Side finding: the terrain draws SMOOTH from `{.1, .5}` across the non-drone
archetypes (`engine/flow/taste.h:1000-1001`) — 55 µs to 3.2 ms, a spread that
cannot be heard at all.

### 1.2 SHAPE is a waveform bank, which is the wrong model

`shape_value` crossfades four fixed waveforms plus a held random value
(`engine/mod/waveforms.h:22-33`). sine → triangle → ramp occupies 0 … 0.5, the
morph into pulse 0.5 … 0.75, the S&H blend 0.75 … 1.

Draft 1 claimed three quarters of the axis sound alike. That is overstated, and
the repo asserts the opposite as mechanics: `taste.h:995-997` and
`tests/test_flow_taste.cpp:99-102` record that *from the ramp up the lane emits a
discontinuity per cycle, and that is what makes a drone read as rhythmic*. The
honest statement is narrower and enough: **the axis is a table index, not a
quantity.** Equal turns move the index equally and move what is heard unequally,
and no re-spacing of break points can cure that, because the stops are discrete
objects rather than points on a continuum.

### 1.3 The melody hangs off the top quarter

`_compute_raw` passes the pattern value as `shape_value`'s third argument
(`lane.cpp:555`); `waveforms.h:32` blends it in only above 0.75, weight
`(shape - 0.75) * 4`. Below that the pattern is computed and discarded. FORM,
SONG, the phrase generator and VARY's pitch mutation all hang off that one blend,
in STEP and on any lane still running the FLOW LFO.

### 1.4 The knob is not the value

`sh = clampf(_shape + _ev_shape + _shape_offset + _kick_shape, 0.f, 1.f)`
(`lane.cpp:554`). Three sources write onto the axis:

| Source | Bound | Character |
|---|---|---|
| DRIFT | ±0.12 deck A, ±0.15 deck B — bipolar, `w = _weather * _drift` with `_weather = tanh(_ou)` | permanent, every control tick (`engine/center/center.cpp:14,17,139,143-144`) |
| EVOLVE | ±0.25 (the bound is fixed; the step scales with VARY) | permanent, creeping (`lane.cpp:693`) |
| SPOT | ±0.35, decays with τ = 1.5 s, skips PITCH | gesture (`super_modulator.cpp:180-182`, `lane.cpp:103`) |

Up to **±0.75 on an axis of 1.0**, before the clamp at the point of use. The
effective state can sit three quarters of the axis away from where the knob points.

The owner ruled the knob's *reach* — one control over all five lanes
(`super_modulator.cpp:78`) — out as a complaint. The reach stays.

### 1.5 What draft 1 got wrong about the frozen lane

Draft 1 reported that at SHAPE 1 a lane freezes, and proposed removing the
condition at `lane.cpp:611` to fix it. The code-truth review corrected three
things, and they change the diagnosis:

- **The S&H end is not an S&H.** `_mutate_slot` fires with probability
  `variation²` — 9 % of cycles at VARY 0.3 — and then takes a small,
  gravity-damped random *step from the previous value* (`lane.cpp:649-659`). At
  the top of the axis a FLOW texture lane emits a slowly creeping DC, not noise.
  "SHAPE 1 is a random source" is true nowhere today.
- **There is a second redraw path, on the other side of VARY.** `_renew_walk()`
  (`lane.cpp:678-681`, called from `_evolve_outgoing_pattern` at `_variation < 0`)
  rewrites the whole buffer including `pitch[0]`. RENEW is panel-reachable
  (`engine/flow/flow_params.h:83`). So the lane is frozen only at **exactly
  VARY = 0**, not across the lower half.
- **`lane.cpp:611` is not a defect.** It is the ENTROPY contract the roadmap
  records: *"0 — LOOP: the melody repeats exactly"*, built because STEP + S&H
  melodies were "unusable note salad (one random value per cycle)". Removing it
  restores exactly that, at ENTROPY 0, on every deck.

What survives is the real defect, and it is smaller and different: **a FLOW lane
has one slot** (`_sh_slot()` returns 0 — `lane.cpp:564`), so at the top of the
axis it has one value to hold and nothing to move between. The fix is **more
slots, not more randomness** (§3.2).

### 1.6 The two controls are one axis

Under any repair, SHAPE's low end means "the value travels smoothly between its
states" — which is what SMOOTH already owns on a lane that has states. That
overlap is why neither knob owns anything: SMOOTH has no live middle because
SHAPE's bottom half decides the same thing, and SHAPE feels inert at the bottom
because SMOOTH holds the movement there.

## 2. Decisions taken

Rulings from the brainstorming session of 2026-08-13. Rulings 1–6 were taken
before the review, 7–9 after it, on the reviewers' findings.

1. **The Marbles framing is the target.** A knob should bend one property of a
   single process, monotone in one perceived quantity, non-linear in value — not
   crossfade between fixed building blocks.
2. **SHAPE and SMOOTH merge into one control.** SMOOTH disappears as a parameter.
   Rejected: two cleanly separated knobs (leaves them coupled — at SHAPE 1 the
   glide still smears the edge away), and re-purposing SMOOTH as a jitter axis
   (invents a function; ENTROPY and DRIFT already hold that ground).
3. **Gestures may write on the axis, permanent sources may not.** DRIFT loses its
   shape tap, SPOT keeps ±0.35, EVOLVE is capped at ±0.10.
4. **The freed panel positions stay empty for now**, and the hole is a real
   design object, not an absence — see §5.
5. **Faceplate landmarks, no detents.** The terrain draws continuous values;
   snapping would quantize the terrain with it.
6. **Patch compatibility is not a concern.** Stored patches may break, VCV
   parameter IDs may shift — dev alpha, already decided.
7. **One mechanism, no second rendering.** The FLOW LFO is replaced by a walk over
   the 32-slot buffer the lane already owns; SHAPE is then the glide fraction on
   every lane in every mode, and `shape_value` and the waveform bank leave the
   instrument. Rejected: keeping the waveform for LFO lanes (§2.2).
8. **The glide maximum is `kFlowSlewFrac` re-expressed, not a new value** (§3.3).
9. **Three branches, in order** (§7): axis ownership, then the engine axis with
   `set_smooth` kept as a pass-through, then the surface removal.

### 2.1 Why the buffer walk and not more randomness

The single-slot FLOW lane is a *lack of states*, so the repair is to give it
states. The buffer is already there: `_fill_walk()` seeds all 32 slots with
`pg_contour_walk` at init (`lane.cpp:76,662-665`), non-melodic lanes carry
all-true gates (`lane.cpp:78,591`), and ENTROPY already owns how those values
evolve. Walking it costs no new RNG draw, keeps LOOP exactly repeating, keeps
ERODE and RENEW meaningful, and leaves the BBD deck's PITCH lane — which is that
deck's clock — free of unrequested per-cycle jumps.

### 2.2 Why "two renderings" was rejected

On a FLOW note deck the melodic lane walks slots while the four texture lanes ran
the LFO, both driven by the same knob. Across the axis they disagree:

| SHAPE | slot lane | LFO lane (draft 1) |
|---|---|---|
| 0.00 → 0.60 | the whole portamento range, continuously variable | sine → ramp: nothing |
| 0.85 | soft edge, glide fraction ≈ 0.04 | two hard square edges per cycle |
| 1.00 | hard jump | hard throw |

The bottom 60 % of the turn would move the melody from glissando to nearly stepped
and the textures not at all; at 0.85 the textures would be harder than the melody.
That is two knobs, which is the complaint this rework exists to answer. Draft 1's
closing argument — "a waveform already is a trajectory" — argues for replacing the
waveform, not for keeping it.

## 3. The axis

One control per deck, `SHAPE`, `0..1`: **how edged the modulation moves.** One
mechanism, every lane, both modes.

### 3.1 Every lane walks slots

- **STEP** — unchanged: every lane already walks its slots.
- **FLOW, melodic lane on a note deck** — unchanged: the FLOW melody engine
  already walks `kFlowPhraseSlots = 8` per cycle (`lane.h:261`).
- **FLOW, everything else** (the four texture lanes, and the melodic lane on a
  SAMPLER or BBD deck) — **new**: `_sh_slot()` walks `kFlowPhraseSlots` slots per
  cycle instead of returning 0. No gate pattern: non-melodic lanes keep all-true
  gates (`lane.cpp:591`), so DENSITY gains no new reach and this is not a second
  melody engine.

The buffer walked is the one that exists (`pitch[kSeqSlots]`), seeded by
`_fill_walk()` and evolved by ENTROPY exactly as today. **No new RNG draw is
introduced anywhere**, so LOOP still repeats bit-exactly and ERODE keeps its
meaning.

**This reverses the FLOW melody engine's exclusion of SAMPLER and BBD decks**
(that spec's decision 3). Those lanes get slots too. Their default stays
continuous because the terrain's drone cap holds SHAPE low (§4), but the reversal
is deliberate and carries its own listening item (§6).

### 3.2 SHAPE is the glide fraction, and nothing else

The observable is **t90: the time the value takes to cover 90 % of the distance to
the next slot value**, expressed as a fraction of that slot's interval:

| SHAPE | 0.00 | 0.25 | 0.50 | 0.75 | 1.00 |
|---|---|---|---|---|---|
| t90 fraction | 0.80 | 0.45 | 0.20 | 0.07 | 0 |
| feel | wanders continuously | travels, settles | aims, holds | step with a soft edge | hard jump |

t90 rather than a one-pole time constant, because a one-pole never arrives: draft 1
wrote "arrives just as the next slot begins", which is false — at τ = one interval
the value reaches 63 %. t90 is a stated, measurable quantity, which is also what
makes gate 2 possible (§6).

`shape_value`, `wave_sine/triangle/ramp/pulse` and `engine/mod/waveforms.h` are
deleted. The lane emits its slot value; SHAPE decides only the path there. **The
0.75 threshold disappears with no replacement**: FORM, SONG, the phrase generator
and VARY's pitch mutation are audible at every knob position.

### 3.3 The maximum is a value already confirmed by ear

0.80 is not a new number. The FLOW melody engine clamps the melodic lane's slew at
`kFlowSlewFrac = 0.35 × interval` as a *safety ceiling*, because at SMOOTH 1 short
notes never arrive and the melody flattens into a wobble; τ = 0.35 interval is
t90 = 2.303 × 0.35 = **0.806 interval**. The owner accepted that by ear on
2026-08-13 (`flow_melody.wav`). This spec turns that ceiling into the axis
maximum, so the bottom of the new axis is the setting he already approved, and
the rework needs no fresh ruling on how far a glide may go.

**The five knots are a first guess between the endpoints, tunable by ear.** What is
not tunable: fraction of the interval, not seconds; 0.80 at the bottom; 0 at the
top.

### 3.4 Which interval, and when it is computed

The fraction is taken of **the interval to the next boundary, computed at each
boundary** — not of a nominal slot length. That single choice answers four
questions at once: SHUFFLE's uneven slots (`lane.cpp:464-465`), TIDE's stretched
slot counts, the grid lock's per-lane cycle lengths, and rate changes mid-cycle.
It also removes the dependence on `_phase_inc` that today forces a silent-inert
guard (`lane.cpp:374`) and makes `_ev_rate`'s exclusion from the recompute
(`lane.cpp:368-371`) moot.

Draft 1 said "the `kFlowSlewFrac` special case becomes the rule". That was wrong in
one detail the reviewer caught: that clamp derives its interval from
`_effective_length()`, which is capped at `kSeqSlots = 32` (`lane.cpp:267-271`),
while a STEP texture lane reaches **64** slots (`lane_len.h:29-30,42`, reachable
via LANE_SIZE × STEPS ÷ TIDE — flagged at `lane.cpp:243-244`). Generalizing it
verbatim would glide twice as long as the slot on those lanes. The boundary-time
rule above does not have that failure mode.

**The axis read at the boundary is the composite** `_shape + _ev_shape +
_kick_shape` (`_shape_offset` is gone, §3.5), so SPOT still reaches the glide as
decision 3 requires. It is sampled per boundary, **not per sample** — `_kick_shape`
decays per sample (`lane.cpp:737`), and following that continuously would put
`_update_slew`'s `std::pow` in the per-sample path on a board with 2.17 points of
reserve. Per-boundary sampling is the reason the CPU claim in §6 holds.

At fraction 0 the one-pole is bypassed and the target is set directly; there is no
seconds-valued floor anywhere, or §1.1's defect would be back in the code. The
click-free requirement at fraction 0 is a gate, not a hidden constant (§6, gate 7).

### 3.5 The knob holds

- **DRIFT** no longer writes to the axis. `set_shape_offset` and the taps at
  `center.cpp:143-144` are deleted; `_shape_offset` leaves `ModLane`. DRIFT keeps
  its rate tap and its detune tap (`center.cpp:140-146`).
- **EVOLVE** is capped at ±0.10 instead of ±0.25 (`lane.cpp:693`).
- **SPOT** keeps ±0.35 (`super_modulator.cpp:181`).

Two honest consequences, neither of which draft 1 named. DRIFT loses one of three
mechanisms just as the Glow rework prepares to build MOTION on it, so **the
audit's DRIFT impact rows go stale and are re-measured in branch 1** (§7). And
SPOT's shape component changes character: it used to jump a waveform, now it
changes a glide fraction, which is a quieter gesture. Whether ±0.35 is still the
right number is a listening item, not an assumption.

### 3.6 What this costs

- **Every stored STEP terrain sounds different.** Below SHAPE 0.75, STEP today
  emits a stepped waveform sampled at each boundary; afterwards it emits its slot
  values everywhere. That is the price of making the melody reachable, and it is
  the change the audit asked for.
- **STEP loses its only monotone-contour generator.** A stepped ramp or triangle
  is an ascending or descending run; nothing replaces it at any control position.
  The mod grid lock (2.13.2) keeps its ratios as pattern lengths, but the lanes no
  longer trace *contours* of different lengths against the grid — its ratio
  structure becomes pattern-length variety. This is accepted, and named here so it
  is not discovered later.
- **Texture-lane modulation depth rises.** At the owner's playing position SMOOTH
  is a 500 ms one-pole — a corner at 0.32 Hz, which attenuates a several-Hz
  texture lane by 15–20 dB. Removing it raises effective depth on FILT, TIMBRE,
  LEVEL and MOTION across every terrain, in one direction. Gate 8 pins it; §4's
  terrain re-tuning is where it is compensated.
- **Every FLOW render changes**, not only STEP terrains: the four texture lanes
  and the SAMPLER/BBD pitch lanes now walk eight slots per cycle where they held
  one.

## 4. Blast radius

**Engine**

- `ModLane::set_smooth`, `SuperModulator::set_smooth` (`super_modulator.cpp:78`,
  `super_modulator.h:38`) and `Instrument::set_smooth` (`instrument.h:60`) are
  removed — in branch 3; branch 2 keeps `set_smooth` as an ignored pass-through
  (§7).
- `_update_slew()` is replaced by a per-boundary derivation (§3.4).
- `_sh_slot()` walks `kFlowPhraseSlots` in FLOW for every lane (`lane.cpp:564`).
- `_compute_raw()` emits the slot value; `engine/mod/waveforms.h` is deleted.
- `set_fixed_slew` (fixed 20 ms) goes: it contradicts an interval-relative axis
  outright. It has no scenario user — only `host/render/scenario.cpp:154` declaring
  the action and `tests/test_step.cpp:43-52` exercising it.
- `set_shape_offset` and `_shape_offset` are removed (`lane.h:139,304`,
  `super_modulator.h:111`, `instrument.h`), with `center.cpp:14,17,143-144`.

**Terrain and base rules**

- `P_SMOOTH_A/B` leave the X-macro at `engine/flow/flow_params.h:79` and the apply
  switch at `:165-166`: **47 base rules become 45** (counted in
  `taste.h:952-1112`).
- `taste.h:1000-1001` (the SMOOTH rows) are deleted. `taste.h:994-997`'s comment
  describes the morph and goes with it.
- `P_SHAPE_A/B`'s spans (`taste.h:998-999`) are re-chosen **in branch 3**, together
  with the drone cap's new rationale — under the new axis `{0, .25}` reads "glides
  continuously", which is what a drone wants, but the old justification ("from the
  ramp up the lane emits a discontinuity per cycle") describes a mechanism that no
  longer exists. `tests/test_flow_taste.cpp:98-118` asserts both the drone cap and
  `ARCH_ARP.hi > 0.25` and is rewritten with them.

**Patch transfer**

- `host/vcv/src/flow_patch_bridge.hpp` loses `kFfSmoothA/B` (`:77`, `:98`) and the
  `smth` lines (`:380`, `:387`). The converter **reports SMOOTH in its
  "could not carry" list** rather than dropping it silently.
- `docs/flow-fireflow-param-map.md` is the authority for that mapping: row `:165`
  goes, row `:164` (SHAPE) stays with new wording, and both rows' call-site
  citations are stale today — the real lines are `Fireflow.cpp:613` (`set_shape`)
  and `:615` (`set_smooth`).

**Hosts, panels, firmware, bench**

- `host/vcv/src/Fireflow.cpp:615` (the module's own push).
- Both panel generators — `host/vcv/res/gen_panel.py` and
  `host/vcv/res/gen_hw_panel.py:32,107` — plus the generated headers
  (`generated_panel.hpp:20,40,115,135`, `generated_hw_panel.hpp:14,34`) and the
  panel guard `host/vcv/res/test_panel.py:52,56,69,72,439,2263,2287`, whose
  `:2134` already documents the ParamId-aliasing failure a removal causes.
- `docs/hardware/io-budget.md:73,93` carries SMOOTH in the 68-on-66 budget.
- `host/render/scenario.cpp:143,154`; three scenarios call `set_smooth`
  (`ambient_wash.json`, `ctrl_identity.json`, `wave_formant_sweep.json`) and none
  calls `set_fixed_slew`.
- **`bench/`** — missing from draft 1 entirely. `bench/audition/init_patch.cpp:57`
  calls `set_smooth` and includes `generated_panel.hpp`, so the enum shift moves
  every index it reads; `bench/workloads_mod.cpp:13-17,27,44,71` is built around
  `shape_value`'s four segments and its row labels (`s00`/`s03`/`s07`/`s10`) stop
  meaning anything. Given `spotykach-bench-stale-object-trap`, the bench is updated
  in the same commit as the engine change, never later.
- **`shell/`** has no SMOOTH or SHAPE reference in its own source, but compiles
  `engine/mod/lane.cpp` and `engine/mod/super_modulator.cpp`
  (`shell/Makefile:94-95`), so it needs a rebuild round in branch 2.
- `docs/roadmap.md:2392` states "SMOOTH's slew is clamped against the slot
  interval" in the FLOW-melody entry and is corrected.

**Tests that break and are rewritten**: `test_waveforms.cpp:6-22` (deleted with
the header), `test_flow_taste.cpp:98-118`, `test_lane.cpp:35` and `:66-77`,
`test_lane_tick.cpp:168`, `test_step.cpp:43-52`, `test_flow_melody.cpp:539,570`,
`test_flow_transfer_diff.cpp:62`, `test_center.cpp`, `test_instrument.cpp:292`.

## 5. The panel hole is a design object

`gen_panel.py:122` places nine controls on a ring at a 40° pitch, grouped into
three captioned sector arcs. **SMOOTH sits at 80°, inside the MOTION sector;
SHAPE at 120°, inside TIMBRE.** So the merge leaves a visible gap mid-arc on both
decks, on the VCV panel and on the hardware plate — and puts the merged control in
TIMBRE although half of what it now does (the glide) belonged to MOTION.

Decision 4 keeps the position empty for now, which means branch 3 must choose
between re-spacing the ring (moving every knob, and re-opening the four unchecked
position mirrors from the panel round) and accepting the hole deliberately. That
choice is made **with a rendered picture in front of the owner**, not in prose, and
it belongs to branch 3 — but it is named here so it is not discovered at
implementation time.

The printed landmarks change with the mechanism: they are no longer waveform
symbols (arc / triangle / ramp / pulse), because no waveform survives. They become
the four points of the glide axis — **wander / travel / step / jump** — and they
are honest in both modes for the first time, because there is now one mechanism to
be honest about.

## 6. Verification

Eight gates. Each names its observable and its RED.

1. **Tempo invariance.** In STEP (free lanes never read `_bpm`, so the gate would
   be vacuous in FLOW): measured t90 ÷ measured slot interval is equal at two
   tempi, within tolerance. Output-domain, not a read-back of the coefficient.
   RED: restore the absolute-seconds formula.
2. **Even axis.** Observable: **hold fraction** — the share of each slot the lane
   output spends within 10 % of its target. Sweep SHAPE across the five knots; the
   hold fraction must rise monotonically and each quarter turn must move it by at
   least a stated minimum. RED: the old law, where at a fixed tempo the hold
   fraction is ≈ 1.0 across the lower 60 % of the travel — flat, and the gate
   fails on the monotone-step requirement rather than on a retuned knot.
3. **The melody is reachable everywhere.** `tests/test_param_impact.cpp` checks
   FORM and SONG at SHAPE 0, 0.5 and 1 in STEP, under the FLOW-melody spec's
   gate-20 discipline: `DEPTH_A/B` forced to 1.0 and `_active` true, so a measured
   zero cannot be a downstream attenuator. **Pre-decided:** a residual zero under
   that control is an out-of-scope finding to be recorded, not a gate failure —
   the audit's second FORM/SONG gate is explicitly not this spec's job (§8).
   RED: restore the 0.75 blend.
4. **A FLOW lane moves at ENTROPY 0.** Every lane's output travels at least a
   stated magnitude (in lane-output units, not float epsilon) across N cycles, on
   every deck engine, at VARY = 0 and at SHAPE 1. RED: return `_sh_slot()` to a
   single slot.
5. **LOOP still loops.** At ENTROPY 0 the emitted slot sequence repeats
   bit-exactly across cycles, on a FLOW texture lane and on a STEP lane. This gate
   exists because draft 1 would have broken it; it is the guard against solving
   gate 4 with randomness. RED: introduce a per-cycle redraw.
6. **The knob holds.** Sweep DRIFT 0 → 1 with SHAPE fixed: the hold fraction from
   gate 2 is invariant. Plus the EVOLVE ±0.10 clamp, asserted directly.
   RED: restore the DRIFT tap, or ±0.25.
7. **Click-free at fraction 0.** Maximum per-sample discontinuity on the LEVEL and
   FILT lanes stays under a stated bound at SHAPE 1. RED: remove whatever bounds it.
8. **Modulation depth is not silently raised.** On one fixed terrain, texture-lane
   output depth before and after the change stays within a stated band — the guard
   on §3.6's third bullet. This gate can only be written in branch 2, against a
   pre-branch baseline capture.

**Gate 9, the init conversion, is branch 3's** and is a *comparison*, not a
self-baseline: the post-merge init render is compared against the **pre-merge**
init render on a stated measure and threshold. RED: leave SHAPE at the
unconverted 0.0. A gate baselined on its own output could only go red by later
editing the init, which is the tautology the `fireflow-control-merge-init-trap`
memory records biting four times.

**Two hash gates are re-baselined in branch 3.** `ctrl_identity.json` and
`wave_formant_sweep.json` both call `set_smooth`; when the action goes their
byte-identity hashes in `tests/check_render_hash.cmake` break. Order: **the owner
listens to the new renders first, then the hashes move** — never the other way
round. `ambient_wash.json` follows as the third scenario and carries no hash gate.

**Listening pack** (branch 2, same shape as `flow_melody.wav`):

- a drone at SHAPE 0 / 0.35 / 0.7 / 1
- a STEP terrain across the same four positions — does the phrase carry everywhere?
- **a FLOW note deck across the same four positions** — the case that exposed
  draft 1: melodic and texture lanes on one knob, now one mechanism
- **a SAMPLER deck and a BBD deck in FLOW** — §3.1 reverses their exclusion, and
  the BBD deck's PITCH lane is its clock
- the ENTROPY 0 case that freezes today
- SPOT, before and against after (§3.5: the gesture changes character)

**CPU.** The per-sample path loses a `shape_value` call and gains nothing; the
slew derivation moves from rate-change time to boundary time, which is a small
fixed cost per slot rather than per sample. The change is expected to remove work,
and **no figure is claimed** — the board's 2.17 points of reserve remain the frame.
Branch 2 rebuilds `shell/` and refreshes the `bench/` mod rows (§4).

## 7. Three branches, in order

Draft 1 was one commit spanning engine, both hosts, both panel generators, the
firmware, the bench and three hash re-baselines. Split:

**Branch 1 — axis ownership.** DRIFT's shape tap out, EVOLVE capped at ±0.10
(§3.5). Small, independent of the merge, gates 6's second half. Ends by
**re-measuring the audit's DRIFT impact rows**, because the Glow rework builds
MOTION on DRIFT and would otherwise inherit stale numbers.

**Branch 2 — the engine axis.** The buffer walk, the glide fraction, the deletion
of `waveforms.h`. `set_smooth` stays as an ignored pass-through so no host, panel,
scenario, bench or hash file has to move yet — the owner hears the sound change on
its own, undiluted by a mechanical cross-host diff. Gates 1–8, the listening pack,
the `shell/` and `bench/` rebuild.

**Branch 3 — the surface.** `P_SMOOTH_A/B` out of `flow_params.h` and `taste.h`,
47 → 45 base rules, the patch bridge and the param map, both panel generators, the
panel hole decision (§5), `io-budget.md`, the three scenarios, gate 9 and the two
hash re-baselines.

## 8. Out of scope, and not promised

- **The knob's reach across all five lanes stays as it is.** The owner ruled it out
  as a complaint.
- Three unexplained threads from the audit are **re-measured and recorded under the
  new axis, not fixed**: `SONG_A` audible only in STEP, `SONG_B` dead in both
  modes, and the second silent-deck cause (12 of 52 terrains, always with BODY on
  the silent side).
- The `sh` composition as the second FORM/SONG gate should fall away through §3.5.
  Whether it does is decided by the measurement, not by this spec (gate 3's
  pre-decision).
- DENSITY gains no reach into the FLOW texture lanes: they keep all-true gates
  (§3.1). Giving them a gate pattern would be a second melody engine and belongs to
  a spec of its own.
- Which controls take the two freed panel positions.
- The COLOR caveat from the FLOW melody engine's §6.3 — the chord lay is chosen
  against the post-slew root — is unchanged by this spec: the glide maximum is the
  ear-approved `kFlowSlewFrac`, so the latch is no longer than it is today.

## 9. Roadmap

`docs/roadmap.md`'s Planned entry "SHAPE + SMOOTH rework" is replaced by a pointer
to this spec and the three-branch order. The Glow rework stays behind it, for the
reason already recorded there: designing the macro layer against today's behaviour
would bake the workarounds in. `docs/roadmap.md:2392`'s description of the
FLOW-melody slew clamp is corrected in branch 2 (§4).
