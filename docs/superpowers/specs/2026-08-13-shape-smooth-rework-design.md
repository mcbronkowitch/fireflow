# SHAPE + SMOOTH rework — design

**Date:** 2026-08-13
**Status:** design, not implemented
**Follows:** the FLOW melody engine (`docs/superpowers/specs/2026-08-13-flow-melody-engine-design.md`)
**Precedes:** the Glow rework (`docs/roadmap.md`)
**Evidence base:** `docs/2026-08-13-glow-macro-audit.md`, plus the measurements in §1 taken during this session

**Revision:** third draft, after four reviewers over two passes. Both earlier
drafts were rejected, and the rejections are what this draft is built from.

- **Draft 1** proposed "one axis, two renderings" — a trajectory for slot-walking
  lanes, the existing waveform bank for FLOW LFO lanes. Two reviewers rejected it
  independently: on a FLOW note deck both renderings run at once from one knob and
  disagree across the whole axis. It also called `lane.cpp:611` a defect; that line
  is the ENTROPY LOOP contract.
- **Draft 2** collapsed to a single mechanism — every lane walks its 32-slot buffer,
  SHAPE becomes the glide fraction, `waveforms.h` is deleted. The technical review
  found the central change is a **no-op** (§1.7), that the buffer walk is four times
  *weaker* than the sine it replaces and carries a per-seed DC offset (§1.8), and
  that "no new RNG draw" was false. The goal review found that it delivered
  predictability by deleting the only thing SHAPE could change, and removed
  modulation character from the instrument with no successor.
- **Draft 3 does not merge the two controls.** It repairs the three defects both
  passes confirmed, and it dissolves the SHAPE/SMOOTH overlap by *ownership* rather
  than by merging (§3). The waveform bank stays.

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
lower 60 % of the travel is one setting**, and everything musical lives above 0.75.
A knob whose usable range is its top quarter is operated exactly the way the owner
operates it.

The absolute scale is the deeper defect: a fixed number of milliseconds has no
musical meaning across tempo changes, so the control cannot mean the same thing
twice. The terrain shows the same symptom — it draws SMOOTH from `{.1, .5}` across
the non-drone archetypes (`taste.h:1000-1001`), which is 55 µs to 3.2 ms: a spread
that cannot be heard at all.

### 1.2 The melody hangs off SHAPE's top quarter

`_compute_raw` passes the pattern value as `shape_value`'s third argument
(`lane.cpp:555`); `waveforms.h:32` blends it in only above 0.75, weight
`(shape - 0.75) * 4`. Below that the pattern is computed and discarded. FORM, SONG,
the phrase generator and VARY's pitch mutation all hang off that one blend — in
STEP, and on any lane still running the FLOW LFO.

### 1.3 The knob is not the value

`sh = clampf(_shape + _ev_shape + _shape_offset + _kick_shape, 0.f, 1.f)`
(`lane.cpp:554`). Three sources write onto the axis:

| Source | Bound | Character |
|---|---|---|
| DRIFT | ±0.12 deck A, ±0.15 deck B, bipolar (`w = _weather * _drift`, `_weather = tanh(_ou)`) | permanent, every control tick (`center.cpp:14,17,139,143-144`) |
| EVOLVE | ±0.25 (bound fixed; the step scales with VARY) | permanent, creeping (`lane.cpp:693`) |
| SPOT | ±0.35, τ = 1.5 s, skips PITCH | gesture (`super_modulator.cpp:180-182`) |

Up to ±0.75 on an axis of 1.0 before the clamp. The effective state can sit three
quarters of the axis from where the knob points.

### 1.4 The morph is a table index, not a quantity

`shape_value` crossfades four waveforms plus a held value
(`engine/mod/waveforms.h:22-33`) in four equal quarters. The stops are discrete
objects; equal turns move the index equally and move what is heard unequally. This
is a real defect, and it is the *smallest* of the four — which is why draft 2's
attempt to solve it by deleting the bank cost more than it bought (§1.9).

### 1.5 The overlap is only on the melodic lane

SHAPE's low end means "the value travels smoothly between its states"; that is what
SMOOTH owns. But a lane only has states to travel between if it walks slots — which
means the melodic lane, in STEP and (since the FLOW melody engine) in FLOW on a note
deck. **On the four texture lanes there is no overlap at all**: they run a continuous
waveform, and SMOOTH is the only smoothing they have.

Drafts 1 and 2 both read this as "the two controls are one axis" and merged them.
That was the wrong conclusion from the right observation. The overlap is a
**question of ownership on one lane**, and §3 resolves it there.

### 1.6 What draft 1 got wrong about the frozen lane

Draft 1 reported that at SHAPE 1 a lane freezes and proposed removing
`lane.cpp:611`. Three corrections from the code review:

- **The S&H end is not an S&H.** `_mutate_slot` fires with probability `variation²`
  — 9 % of cycles at VARY 0.3 — then takes a small, gravity-damped step from the
  previous value (`lane.cpp:649-659`). At the top of the axis a FLOW texture lane
  emits a slowly creeping DC, not noise.
- **A second redraw path exists on the other side of VARY.** `_renew_walk()`
  (`lane.cpp:678-681`, from `_evolve_outgoing_pattern` at `_variation < 0`) rewrites
  the whole buffer including `pitch[0]`. RENEW is panel-reachable
  (`flow_params.h:83`). The lane is frozen only at **exactly VARY = 0**.
- **`lane.cpp:611` is not a defect.** It is the ENTROPY contract the roadmap
  records: *"0 — LOOP: the melody repeats exactly"*, built because STEP + S&H
  melodies were "unusable note salad (one random value per cycle)".

The residue — a FLOW LFO lane at exactly VARY = 0 and SHAPE 1 emits a constant — is
**recorded as a known defect and not fixed here** (§7). Fixing it needs slots in
FLOW, and §1.7 is what that costs.

### 1.7 Why draft 2's mechanism is not available cheaply

Draft 2 proposed giving FLOW lanes eight slots per cycle by changing `_sh_slot()`.
That change is a **no-op**: `_sh_slot()` reads `_cur_step` (`lane.cpp:558-567`), and
in the FLOW LFO path nothing writes it — `process()`'s wrap branch
(`lane.cpp:771-774`) never touches it, `tick()`'s arms are gated on `_step_mode` and
`_flow_melody_on()` (`:884-915`, `:935-937`, `:1001`), and `_enter_step()` is
reached only under `_step_on` (`super_modulator.cpp:167`). `_cur_step` stays at −1
and the lane emits `pitch[0]` forever. A real slot raster in FLOW means editing
`process()` and four sites in `tick()`.

### 1.8 And why it would not have sounded right either

`pg_contour_walk(_rng, pitch, 32, 0.f, 0.6f, 0.12f)` (`lane.cpp:662-665`),
re-implemented exactly and measured over 4000 seeds on the first 8 values — the ones
a FLOW cycle would read:

| observable | buffer walk | sine (`shape_value` at SHAPE 0) |
|---|---|---|
| peak-to-peak, median | **0.535** | **2.000** |
| per-cycle DC offset | sd 0.263 | exactly 0 |
| slot→slot hops below 0.05 | 34.6 % | — |

Four times weaker than what it replaces, with a per-seed DC offset a waveform cannot
produce — a LEVEL lane whose slots sit at +0.3 shifts the base instead of modulating
around it. A third of the "hard jumps" at the top of the axis would be inaudible.

### 1.9 The waveform bank is not the defect

Both reviewers concluded independently that deleting `waveforms.h` impoverishes the
instrument. After the deletion, a texture lane's contour would come only from
`pg_contour_walk`'s hard-coded width 0.6 and gravity 0.12 — no parameter, no terrain
row, no knob — so the *statistical character* of texture modulation becomes a
compile-time constant, and the terrain generator is left with no row expressing
modulation character at all. `taste.h:101-114` also records, by reversion isolation,
that the drone SHAPE cap is what keeps the calm-corner population gate green
(`0x707` RMS 6.6e-03 as shipped against 9.9e-02 with the cap reverted, a factor of
15). The bank is load-bearing. It stays.

## 2. Decisions taken

Rulings from the brainstorming session of 2026-08-13, in the order they were taken.
Rulings 5 and 6 reverse earlier ones after the second review pass; that is recorded
rather than hidden.

1. **Gestures may write on the axis, permanent sources may not.**
2. **Patch compatibility is not a concern** — dev alpha.
3. **SHAPE's reach across all five lanes is not a complaint** and does not change.
4. **The engine may be changed** where a control has no audio path (audit ruling 1).
5. **The merge is off.** SMOOTH stays its own control. Drafts 1 and 2 both merged;
   both mechanisms failed review, and §1.5 shows the premise was over-read.
   Consequence: no base-rule change (47 stays 47), no panel change, no param-map
   change, no patch-bridge change, no `bench/` API change.
6. **The waveform bank stays** (§1.9), re-spaced rather than replaced.
7. **Goal fidelity is stated, not assumed.** This spec delivers predictability
   (§1.3, §1.4), a live SMOOTH middle (§1.1) and a reachable melody (§1.2). It does
   **not** deliver the owner's Marbles requirement — a knob that changes several
   modulations at once, non-linearly and coherently. §7 records where that goal
   goes instead.

## 3. Ownership: SHAPE shapes the texture, the melodic lane plays notes

One rule, and everything below follows from it.

**The melodic lane emits its phrase value, in both modes.** In FLOW on a note deck
this is already true — the FLOW melody engine returns the phrase note directly and
never calls `shape_value` (`lane.cpp:551`). This spec extends it to **STEP**.

**In STEP this is not new behaviour.** At SHAPE 1 the blend weight
`(shape - 0.75) * 4` is exactly 1 and `waveforms.h:32` already returns the slot
value verbatim. The change makes an existing path unconditional; §6 gate 6 pins that
the two are bit-identical at SHAPE 1.

Three things follow:

- **The 0.75 threshold is gone from the melodic lane.** FORM, SONG, the phrase
  generator and VARY's pitch mutation reach the audio at every SHAPE position.
- **The overlap dissolves without a merge.** SHAPE now owns the four texture lanes'
  waveform character; SMOOTH owns how any value travels. Two controls, two subjects.
- **SHAPE becomes inert on the melodic lane**, in both modes, consistently. That is
  the same state FLOW already has, made uniform instead of mode-dependent. It is a
  real reduction in what SHAPE does, and it is the price of the melody being
  reachable.

**Not changed:** the melodic lane on a SAMPLER or BBD deck in FLOW keeps its
continuous LFO (`part.cpp` pushes `set_flow_melody(false)` there). Draft 2's
reversal of that exclusion is withdrawn — the technical review found it would reset
the Sampler's slice cursor every cycle (`sampler_engine.cpp:788`) and turn the BBD
deck's clock into an eight-step staircase per cycle.

## 4. The four repairs

### 4.1 SMOOTH becomes interval-relative

The observable is **t90: the time to cover 90 % of the distance to the target**,
expressed as a fraction of the lane's own interval — its slot interval where it
walks slots, its cycle interval on a FLOW LFO lane.

| SMOOTH | 0.00 | 0.25 | 0.50 | 0.75 | 1.00 |
|---|---|---|---|---|---|
| t90 fraction | 0 | 0.07 | 0.20 | 0.45 | 0.80 |

t90 rather than a one-pole time constant because a one-pole never arrives: at τ =
one interval the value reaches 63 %. t90 is measurable, which is what makes gate 2
possible.

**0.80 is not a new number.** The FLOW melody engine clamps the melodic lane at
`kFlowSlewFrac = 0.35 × interval` as a safety ceiling; τ = 0.35 is t90 = 2.303 ×
0.35 = **0.806**. The owner accepted that by ear on 2026-08-13 (`flow_melody.wav`).
The top of the new range is the setting he already approved, so no fresh ruling on
how far a glide may go is needed, and the melody engine's clamp stops being a
special case: it becomes the law's own maximum.

**The three intermediate knots are a first guess, tunable by ear.** What is not
tunable: fraction of the interval, not seconds; 0.80 at the top; 0 at the bottom.

**Where the interval comes from.** `_update_slew()` keeps its current call sites
(rate change, mode change, init) and derives the interval as it does today, with two
corrections the code review forced:

- Not from `_effective_length()`, which clamps at `kSeqSlots = 32`
  (`lane.cpp:267-271`) while a STEP texture lane reaches **64** slots
  (`lane_len.h:29,42`, via LANE_SIZE × STEPS ÷ TIDE — flagged at `lane.cpp:243-244`).
  It must use the lane's real slot count.
- The `_phase_inc == 0` guard stays. Draft 2 claimed a boundary-time interval would
  remove it; the review showed the dependence is unchanged, so the existing guard is
  kept rather than replaced by a silent inert branch.

**Validity floor, stated rather than discovered:** texture lanes advance in `tick()`
at a 96-sample raster and `_slew_tick` steps once per call (`lane.cpp:1023`). Below
a slot of about 96 samples the glide cannot be rendered and t90 stops being
measurable. Gate 1 runs above that floor and the floor is written into the test.

### 4.2 The melodic lane emits its phrase

§3. `_compute_raw`'s melodic branch returns `_active_pattern().pitch[_sh_slot()]`
in STEP as it already does in FLOW melody mode. The texture lanes' path is
untouched.

### 4.3 The knob holds

- **DRIFT** no longer writes the axis: `set_shape_offset` and `_shape_offset` are
  deleted (`lane.h:139,304`, `super_modulator.h:111`, `instrument.h`), with the taps
  at `center.cpp:143-144`. DRIFT keeps its rate tap and its detune tap
  (`center.cpp:140-146`).
- **EVOLVE** is capped at ±0.10 instead of ±0.25 (`lane.cpp:693`).
- **SPOT** keeps ±0.35.

Two consequences, named rather than assumed. Decision 1 says permanent sources may
not write the axis, and EVOLVE at ±0.10 still does — this is a deliberate reduction
rather than a removal, because EVOLVE's shape walk is one of GROW's few texture
mechanisms; the residual foreign write is ±0.45 rather than ±0.75. And DRIFT loses
one of three mechanisms just as the Glow rework prepares to build MOTION on it, so
**the audit's DRIFT impact rows go stale and are re-measured** in the same branch.

### 4.4 The bank is re-spaced

`shape_value`'s four equal quarters become perceptually chosen break points. First
guess, explicitly tunable by ear:

| SHAPE | 0.00 | 0.35 | 0.55 | 0.75 | 1.00 |
|---|---|---|---|---|---|
| wave | sine | triangle | ramp | pulse | hold |

The reasoning is `taste.h:995-997`'s own: *from the ramp up the lane emits a
discontinuity per cycle, and that is what makes a drone read as rhythmic.* The
audible event is the arrival of a discontinuity, so the smooth pair is compressed
and the edged stops get the larger share.

This moves a number the terrain depends on. `taste.h:998-999`'s drone cap `{0, .25}`
exists to keep a drone below the first discontinuity; with the triangle stop at
0.35 the same intent reads `{0, .35}`, and `tests/test_flow_taste.cpp:98-118`
asserts both that cap and `ARCH_ARP.hi > 0.25`. Both move together, and **the
calm-corner population gate (`tests/test_flow_audio.cpp:447-470`) is the arbiter**:
per `taste.h:101-114` the cap is what keeps it green, so if the re-spacing takes it
red, the re-spacing is reverted, not the gate re-baselined.

This is the least-evidenced of the four repairs and the only one that touches the
terrain. It is implemented **last**, so the other three land whatever it does.

## 5. Blast radius

Small, and deliberately so. Everything drafts 1 and 2 would have moved and this one
does not: the panel and both generators, `docs/flow-fireflow-param-map.md`,
`flow_patch_bridge.hpp`, `flow_params.h`, the base-rule count, `io-budget.md`,
`bench/`'s API use, the render host's scenario actions.

**Engine**

- `engine/mod/lane.cpp` — `_update_slew()` (§4.1), `_compute_raw()`'s melodic branch
  (§4.2), the `_ev_shape` clamp (§4.3), `_shape_offset` removal.
- `engine/mod/waveforms.h` — re-spaced break points (§4.4).
- `engine/center/center.cpp:14,17,143-144` — the DRIFT shape tap.
- `engine/mod/lane.h:139,304`, `engine/mod/super_modulator.h:111`,
  `engine/instrument.h` — `set_shape_offset` removal.
- `engine/flow/taste.h` — the SHAPE spans at `:998-999` (§4.4) and the SMOOTH spans
  at `:1000-1001`, which become audible for the first time under §4.1 and must be
  re-chosen rather than kept.

**Tests that break and are rewritten:** `test_waveforms.cpp:6-22` (break points),
`test_flow_taste.cpp:98-118` (the cap), `test_lane.cpp:35` (SMOOTH) and `:66-77`
(shape_offset), `test_lane_tick.cpp:168`, `test_center.cpp` and
`test_instrument.cpp:292` (the shape_offset identity), `test_flow_melody.cpp:539,570`
(the `kFlowSlewFrac` gates, which become the law's maximum), `test_param_impact.cpp`
(FORM/SONG, §6 gate 3), `test_flow_audio.cpp` (re-run, §4.4).

**Other trees:** `shell/` has no SHAPE or SMOOTH source reference but compiles
`engine/mod/lane.cpp` (`shell/Makefile:94-95`), so it needs a rebuild round.
`bench/workloads_mod.cpp:13-17` labels its FLOW rows by `shape_value`'s four
segments (`s00`/`s03`/`s07`/`s10`); the API is unchanged, but the labels stop
matching the break points and are corrected in the same commit —
`spotykach-bench-stale-object-trap` applies.

**Hash gates.** `ctrl_identity.json` and `wave_formant_sweep.json` both call
`set_smooth`, so §4.1 changes what they render and their byte-identity hashes in
`tests/check_render_hash.cmake` break. Order: **the owner listens first, then the
hashes move.** `ambient_wash.json` follows and carries no hash gate.

## 6. Verification

Seven gates. Each names its observable and its RED.

1. **Tempo invariance.** In STEP — free lanes never read `_bpm`, so the gate would
   be vacuous in FLOW — measured t90 ÷ measured slot interval is equal at two tempi,
   above §4.1's validity floor. Output-domain, not a read-back of the coefficient.
   RED: restore the absolute-seconds formula.
2. **SMOOTH has a live middle.** Observable: **hold fraction** — the share of each
   slot the lane output spends within 10 % of its target. Across the five SMOOTH
   knots it must fall monotonically, and each quarter turn must move it by at least a
   stated minimum. RED: the old law, where at a fixed tempo the hold fraction is
   ≈ 1.0 across the lower 60 % of the travel and the monotone-step requirement fails.
3. **The melody is reachable everywhere.** `tests/test_param_impact.cpp` checks FORM
   and SONG at SHAPE 0, 0.5 and 1 in STEP, under the FLOW-melody spec's gate-20
   discipline: `DEPTH_A/B` forced to 1.0 and `_active` true, so a measured zero
   cannot be a downstream attenuator. **Pre-decided:** a residual zero under that
   control is an out-of-scope finding to be recorded, not a gate failure — the
   audit measured that forcing `SHAPE_A = 1.0` made FORM audible on only 1 of 6 STEP
   terrains, so at least one further gate exists that this spec does not claim to
   remove (§7). RED: restore the 0.75 blend.
4. **The knob holds.** Sweep DRIFT 0 → 1 with SHAPE fixed: the lane's emitted
   waveform is invariant. Plus the EVOLVE ±0.10 clamp asserted directly. RED:
   restore the DRIFT tap, or ±0.25.
5. **The drone cap stays below the first discontinuity.** A relational invariant:
   `P_SHAPE`'s drone `hi` sits at or below the triangle→ramp break point, whatever
   both values are. RED: move either one independently. This is what survives §4.4's
   retuning, and it is the reason the cap can be re-chosen without losing its
   meaning.
6. **The STEP melodic path is unchanged at SHAPE 1.** The new unconditional phrase
   emission is bit-identical to today's output at SHAPE 1, proving §3's claim that
   this is an existing behaviour made unconditional rather than new behaviour. RED:
   perturb either path.
7. **The calm corner stays green.** `tests/test_flow_audio.cpp:447-470` runs
   unchanged and un-rebaselined. It is the arbiter for §4.4, not a gate to be tuned
   around.

**Listening pack** (same shape as `flow_melody.wav`):

- SMOOTH at 0 / 0.25 / 0.5 / 0.75 / 1 on one STEP terrain and one drone — five
  different things, or two?
- a STEP terrain at SHAPE 0 / 0.5 / 1 — does the phrase carry at every position now?
- the same terrain before and after §4.2, at SHAPE 0.3 — the position where a
  stepped waveform used to play and the phrase now does
- SHAPE across its four stops after re-spacing, on a texture-heavy terrain
- SPOT, before against after (§4.3 changes what its shape kick lands on)

**CPU.** Nothing moves into the per-sample path: `_update_slew()` keeps its existing
call sites, `_compute_raw`'s melodic branch gets shorter, and the texture path is
untouched. No bench round is planned and **no figure is claimed**. The board's 2.17
points of reserve remain the frame; `bench/`'s mod rows are relabelled, not
re-measured.

## 7. Out of scope, and not promised

- **The Marbles requirement is not delivered by this spec** (decision 7). The axis in
  this engine that already changes several modulations at once, non-linearly and
  coherently, is **ENTROPY/VARY** — LOOP → GROW → RENEW, reaching all five lanes and
  rewriting what they emit. If a Marbles knob is the goal, that is where it lives,
  and it needs its own spec. SHAPE, after this spec, is an honest texture-character
  control and nothing more.
- **The frozen FLOW lane at exactly VARY = 0 and SHAPE 1** (§1.6) stays a recorded
  defect. Fixing it needs a real slot raster in FLOW (§1.7), which is a larger piece
  of work than this spec's whole scope.
- Three unexplained threads from the audit are **re-measured and recorded, not
  fixed**: `SONG_A` audible only in STEP, `SONG_B` dead in both modes, and the second
  silent-deck cause (12 of 52 terrains, always with BODY on the silent side).
- The second FORM/SONG gate the audit measured (§6 gate 3's pre-decision) is not
  claimed to be removed. On the measured evidence this spec's repair may leave FORM
  inaudible on most STEP terrains; that would be a finding for the next round, not a
  failure of this one.
- `set_fixed_slew` stays. It is dead surface (no host, no scenario — only
  `instrument.h:143`, `scenario.cpp:154` and `tests/test_step.cpp:52`), and removing
  it buys nothing this spec needs.

## 8. Roadmap

`docs/roadmap.md`'s Planned entry "SHAPE + SMOOTH rework" is replaced by a pointer to
this spec, and its three falsified premises are already corrected there. The Glow
rework stays behind it. `docs/roadmap.md:2392`'s description of the FLOW-melody slew
clamp changes with §4.1: the clamp becomes the law's maximum rather than a special
case.
