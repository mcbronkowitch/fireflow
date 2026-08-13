# SHAPE + SMOOTH rework — design

**Date:** 2026-08-13
**Status:** design, not implemented
**Follows:** the FLOW melody engine (`docs/superpowers/specs/2026-08-13-flow-melody-engine-design.md`)
**Precedes:** the Glow rework (`docs/roadmap.md`)
**Evidence base:** `docs/2026-08-13-glow-macro-audit.md`, plus the measurements in §1 taken during this session

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
lower 60 % of the travel is one setting.** Everything musical — portamento, filter
sweeps, slow pans — lives above 0.75. A knob whose usable range is its top quarter
is operated exactly the way the owner operates it.

The absolute scale is the deeper defect: a fixed number of milliseconds has no
musical meaning across tempo changes, so the control cannot mean the same thing
twice.

Side finding: the terrain draws SMOOTH from `{0.2, 0.5}` for non-drone worlds
(`engine/flow/taste.h:1000-1001`) — 50 µs to 3.2 ms, a spread that cannot be heard
at all.

### 1.2 SHAPE is unpredictable for four separate reasons

Measured during this session; three of the four match the owner's experience, and
he ruled the fourth (the knob's reach across all five lanes) out as a complaint.

| # | Cause | Mechanism |
|---|---|---|
| 1 | The morph curve is perceptually uneven | sine → triangle → ramp are nearly indistinguishable as slow modulation; pulse and S&H are not. `shape_value` spends three quarters of the axis on the indistinguishable part (`engine/mod/waveforms.h:22-33`) |
| 2 | The melody hangs off the top quarter | `_compute_raw` passes the pattern value as `shape_value`'s third argument (`lane.cpp:555`); `waveforms.h:32` blends it in only above 0.75, weight `(shape - 0.75) * 4`. Below that the pattern is computed and discarded |
| 3 | The knob is not the value | `sh = _shape + _ev_shape + _shape_offset + _kick_shape` (`lane.cpp:554`) |
| 4 | One knob, five lanes | `SuperModulator::set_shape` fans out to every lane (`engine/mod/super_modulator.cpp`) — **not a complaint; the reach stays** |

Cause 3, quantified. Three sources write onto the same 0..1 axis:

| Source | Range | Character |
|---|---|---|
| DRIFT | ±0.15 (deck A +0.12, deck B −0.15) | permanent, every control tick (`engine/center/center.cpp:14,17,143-144`) |
| EVOLVE | ±0.25, scaled by VARY | permanent, creeping (`lane.cpp:693`) |
| SPOT | ±0.35, decays over ~1.5 s, skips PITCH | gesture (`super_modulator.cpp:180-181`) |

Together up to **±0.75 on an axis of 1.0**. The effective state can sit three
quarters of the axis away from where the knob points.

### 1.3 A defect found while specifying: SHAPE at the top freezes lanes

In FLOW a texture lane has a single slot (`lane.cpp:564`), so the S&H source is one
value. It is redrawn only under
`_variation > 0.f && (!_melodic || _step_mode || _flow_melody_on())`
(`lane.cpp:611`). Therefore:

- texture lane in FLOW, VARY > 0 → real S&H, a new value per cycle. Expected.
- texture lane in FLOW, **VARY = 0** → the same value forever. SHAPE 1 is a standstill.
- **melodic lane on a SAMPLER or BBD deck in FLOW** → the condition is never true,
  `pitch[0]` never mutates. SHAPE 1 is constant DC, permanently.

The same knob position means "random" on one deck and "frozen" on the next. This is
repaired as part of the rework (§3.2), not deferred.

### 1.4 The two controls are one axis

Under any repair, SHAPE's low end means "the value travels smoothly between its
states" — which is what SMOOTH already owns. That overlap is why neither knob feels
like it owns anything: SMOOTH has no live middle because SHAPE's bottom half
already decides the same thing, and SHAPE feels inert at the bottom because SMOOTH
holds the movement there.

## 2. Decisions taken

Rulings from the brainstorming session of 2026-08-13, recorded so the plan inherits
them instead of re-deriving them.

1. **The Marbles framing is the target.** A knob should bend one property of a
   single process, monotone in one perceived quantity, non-linear in value — not
   crossfade between fixed building blocks. Today's SHAPE is a crossfader over four
   waveforms plus noise, which is the opposite.
2. **SHAPE and SMOOTH merge into one control.** SMOOTH disappears as a parameter;
   its range becomes the lower part of SHAPE. Rejected: keeping two cleanly
   separated knobs (leaves them coupled — at SHAPE 1 the glide can still smear the
   edge away), and re-purposing SMOOTH as a second statistics axis such as jitter
   (invents a function, and ENTROPY/DRIFT already occupy the neighbouring ground).
3. **One axis, two renderings.** Slot-walking lanes get a trajectory; LFO lanes keep
   a waveform, because with a single slot they would otherwise have no motion source
   at all. Rejected: giving the four texture lanes a phrase in FLOW too (largest
   piece of new behaviour, CPU on a board with 2.17 points of reserve, and a drone
   still wants continuous wandering at the bottom end), and coupling the texture
   lanes to PITCH at the top end (gives up the fixed musical ratios the whole
   instrument rests on).
4. **Gestures may write on the axis, permanent sources may not.** DRIFT loses its
   shape tap; SPOT keeps its ±0.35 because the player triggers it and it decays;
   EVOLVE is capped at ±0.10.
5. **The freed panel positions stay empty for now.** Assigning them belongs to the
   panel round and the Glow rework, not here.
6. **Faceplate landmarks, no detents.** The terrain draws continuous values; snapping
   would quantize the terrain with it.

## 3. The axis

One control per deck, `SHAPE`, `0..1`: **how edged the modulation moves.**

### 3.1 Part 1 — the glide becomes interval-relative (this replaces SMOOTH)

The glide is a **fraction of the lane's own slot interval**, and SHAPE sets that
fraction. It applies to lanes that change value in steps — every lane in STEP, and
the melodic lane on a note deck in FLOW. It does **not** apply to the continuous
waveform of an LFO lane; §3.2 states why.

| SHAPE | 0.00 | 0.25 | 0.50 | 0.75 | 1.00 |
|---|---|---|---|---|---|
| glide fraction | 1.0 | 0.55 | 0.25 | 0.08 | 0 |
| feel | always travelling | wanders, arrives | aims, holds briefly | step with a soft edge | hard jump |

At SHAPE 0 the value arrives just as the next slot begins and is never stationary;
at 1 it jumps and holds. Every quarter turn changes the movement visibly, and it
does so **identically at any tempo**.

**These five numbers are a first guess, tunable by ear.** What is not tunable is the
shape of the rule: fraction of the interval, not seconds.

The machinery exists. `_update_slew()` already clamps the slew against the real note
interval for the FLOW melody lane (`lane.cpp:372-384`, `kFlowSlewFrac = 0.35`). The
rework turns that special case into the rule and deletes the special case.

### 3.2 Part 2 — two renderings, one sensation

**Slot-walking lane** (STEP always; FLOW on note decks): it emits **its phrase
value**, always — no longer a waveform into which the phrase is mixed above 0.75.
SHAPE only decides the path there, per §3.1. **The 0.75 threshold disappears with
no replacement**: FORM, SONG, the phrase generator and VARY's pitch mutation are
audible at every knob position.

**LFO lane** (texture lanes in FLOW, and melodic lanes on SAMPLER/BBD decks in
FLOW): it keeps the waveform as its motion source. SHAPE picks the edge count, with
the stops redistributed:

| SHAPE | 0.00 | 0.35 | 0.60 | 0.85 | 1.00 |
|---|---|---|---|---|---|
| wave | sine | triangle | ramp | pulse | hold & throw |

**These four break points are a first guess, tunable by ear**, chosen so that equal
turns change an equal amount of what is heard rather than an equal amount of table
index.

At the top the value is **redrawn every cycle unconditionally**: the draw no longer
depends on `_variation` and no longer depends on `_melodic`. That kills §1.3 —
`lane.cpp:611`'s condition is the defect, and removing it from the S&H path is the
fix.

**The glide rule does not run on this rendering, and that is deliberate.** A
one-pole whose time constant is a whole cycle is, on a continuous waveform, a
lowpass at that waveform's own frequency: at SHAPE 0 the sine would come out
quieter and phase-shifted, not rounder. An LFO lane needs no glide anyway — its
waveform is already continuous, and where the axis does produce an edge (pulse at
0.85, throw at 1.00) the glide fraction §3.1 would hand it is 0.04 and 0, so the
edge is meant to be hard there. The LFO lane therefore keeps only whatever
smoothing it needs to stay click-free at the throw; the interval-relative glide is
the slot-walking lane's mechanism alone.

This is the one place where "one axis, two renderings" is not symmetric, and the
asymmetry is a consequence of what the two renderings are: a trajectory needs a
travel time, a waveform already is one.

### 3.3 Part 3 — the knob holds

- **DRIFT** no longer writes to the axis. `set_shape_offset` and the two taps at
  `center.cpp:143-144` are deleted; `_shape_offset` leaves `ModLane`. DRIFT keeps
  its rate tap and its detune tap (`center.cpp:140-146`), so it is not disarmed.
- **EVOLVE** is capped at ±0.10 instead of ±0.25 (`lane.cpp:693`).
- **SPOT** keeps ±0.35 (`super_modulator.cpp:181`), because the player triggers it
  and it decays over ~1.5 s.

At rest, the axis is exactly where the knob points.

### 3.4 What this costs

Below SHAPE 0.75, STEP today emits a stepped waveform — a regular rise and fall
sampled at each step boundary. After the rework that is gone; the phrase runs there
instead. **Every stored STEP terrain sounds different.** That is the price of making
the melody reachable at all, and it is exactly the change the audit asked for.

## 4. Blast radius

**Engine**

- `ModLane::set_smooth`, `SuperModulator::set_smooth` (`super_modulator.cpp:78`) and
  `Instrument::set_smooth` (`instrument.h:60`) are removed.
- `_update_slew()` derives the slew from SHAPE × interval instead of absolute
  seconds; the `kFlowSlewFrac` clamp becomes the rule.
- `_compute_raw()` gains the slot-lane / LFO-lane split; `shape_value()` survives
  only for the LFO rendering, with redistributed break points.
- `set_fixed_slew` (fixed 20 ms) is removed with it: it contradicts an
  interval-relative axis outright, and outside the render host's scenario action
  (`host/render/scenario.cpp:154`) its only caller is `tests/test_step.cpp:52`.
- `set_shape_offset` and `_shape_offset` are removed (§3.3).

**Terrain and base rules**

- `P_SMOOTH_A/B` leave `engine/flow/flow_params.h:165-166`: **47 base rules become
  45.**
- `engine/flow/taste.h:1000-1001` (the SMOOTH rows) are deleted; the SHAPE spans at
  `:998-999` are re-chosen. The drone cap `P_SHAPE_A/B = {0, .25}` keeps its meaning
  under the new axis — there it reads "glides continuously", which is what a drone
  wants.
- One thing genuinely goes away with §3.2: the drone span's smoothing of the four
  texture LFO lanes, the `// drone = glassy` note at `taste.h:1000`. At the free
  rates a drone actually runs, 3–180 ms is a small fraction of a cycle, so the
  expected change is small — expected, not measured, which is why the listening
  pack (§6) carries a drone.

**Patch transfer**

- `host/vcv/src/flow_patch_bridge.hpp` loses `kFfSmoothA/B` (`:77`, `:98`) and the
  `smth` lines (`:380`, `:387`).
- `docs/flow-fireflow-param-map.md` is the authority for that mapping and changes in
  the same commit; rows `:164-165` go.

**Panel**

- Both generators: `host/vcv/res/gen_panel.py` and the hardware plate
  (`host/vcv/src/generated_hw_panel.hpp:14,34`, `generated_panel.hpp:115,135`).
- The two freed positions stay empty (decision 5).
- SHAPE gets printed landmarks along its arc — five symbols: arc, triangle, ramp,
  pulse, jump — and **no detents** (decision 6). The symbols can be printed honestly
  only because the axis is monotone; on today's curve they would lie.

**Render host**

- The `set_smooth` and `set_fixed_slew` scenario actions go
  (`host/render/scenario.cpp:143,154`). Three scenarios use them: `ambient_wash.json`,
  `ctrl_identity.json`, `wave_formant_sweep.json`.

**Not a concern:** patch compatibility. Stored patches may break and VCV parameter
IDs may shift — dev alpha, already decided.

## 5. The init trap

The factory patch sits at `SHAPE_A/B = 0.0` with `SMOOTH_A = 0.836` and
`SMOOTH_B = 1.0` (`host/vcv/src/init_patch.hpp:8,10,28,30`, mirrored in
`gen_panel.py:596,641`). Both decks are therefore already in the "round and gliding"
corner, which the new axis reaches near SHAPE 0 — the distance is short.

That is a reason to expect the conversion to be easy, **not** a reason to skip it.
Merging controls has silently moved the factory sound four times in a single branch
before. The new init value is chosen by ear and pinned by a render gate (§6, gate 6).

## 6. Verification

Six gates, each provably red once, by restoring the old behaviour:

1. **Tempo invariance.** Same SHAPE, two tempi → same glide fraction of the slot.
   RED: restore the absolute-seconds formula.
2. **Even axis.** Movement hardness (mean jump height per slot) at SHAPE 0 / .25 /
   .5 / .75 / 1 changes by a comparable amount per quarter turn. This is the gate
   against "nothing for ages, then everything". RED: restore the old curve — the
   first three quarters then collapse together.
3. **The melody is reachable everywhere.** `tests/test_param_impact.cpp` (the
   per-parameter audio gate from the audit) checks FORM and SONG at SHAPE 0, 0.5 and
   1 in STEP. RED: restore the 0.75 blend.
4. **No frozen lane.** At SHAPE 1 every lane moves across N cycles — on every deck
   engine and at VARY = 0. RED: restore the `lane.cpp:611` condition; the SAMPLER
   case goes to DC immediately.
5. **The knob holds.** With DRIFT at full, the effective axis equals the knob value;
   EVOLVE stays within ±0.10. RED: restore the DRIFT tap, or ±0.25.
6. **Factory sound.** A render gate on the init patch, so the merge cannot move the
   factory state unnoticed.

**Two hash gates must be re-baselined.** `ctrl_identity.json` and
`wave_formant_sweep.json` both call `set_smooth`; when the action disappears their
byte-identity hashes in `tests/check_render_hash.cmake` necessarily break. The order
is the one the FLOW melody engine used: **the owner listens to the new renders
first, then the hashes are re-baselined** — never the other way round.
`ambient_wash.json` follows as the third scenario.

**Listening pack for the owner** (same shape as `flow_melody.wav`):

- a drone at SHAPE 0 / 0.35 / 0.7 / 1 — four different things, or three the same and
  one jump?
- a STEP terrain across the same four positions — does the phrase carry at every one?
- the VARY = 0 case that freezes today
- init patch, before against after

**CPU.** Slot lanes lose a `shape_value` call and gain an interpolation, so the
change removes work rather than adding it. No bench round is planned and
**no figure is claimed**. The board's 2.17 points of reserve remain the frame.

## 7. Out of scope, and not promised

- **The knob's reach across all five lanes stays as it is.** The owner ruled cause 4
  out as a complaint.
- Three unexplained threads from the audit are **re-measured and recorded under the
  new axis, not fixed**: `SONG_A` audible only in STEP, `SONG_B` dead in both modes,
  and the second silent-deck cause (12 of 52 terrains, always with BODY on the silent
  side).
- The `sh` composition as the second FORM/SONG gate should fall away through §3.3.
  Whether it does is decided by the measurement, not by this spec.
- Which controls take the two freed panel positions.

## 8. Roadmap

`docs/roadmap.md`'s Planned entry "SHAPE + SMOOTH rework" is replaced by a pointer to
this spec. The Glow rework stays behind it, for the reason already recorded there:
designing the macro layer against today's behaviour would bake the workarounds in.
