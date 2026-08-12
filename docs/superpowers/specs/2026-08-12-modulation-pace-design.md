# PACE — one global modulation time-stretch

Date: 2026-08-12
Status: design, revision 3. Reviewed adversarially five times across two rounds
(engine/DSP, flow layer, hosts/transfer/tests; then fix-integrity and
consistency/implementability). Claims that did not survive are kept in §10
rather than deleted, so the next reader does not re-derive them.

## 1. The problem

There is no control anywhere in FireFlow that slows the modulation layer down.
Three controls look like they should, and none does the job.

**TEMPO is inert in the free world.** `SuperModulator::_update_rate` reads

```cpp
_base_hz = _synced ? division_hz(division_index(_rate_norm), _bpm)
                   : free_hz(_rate_norm);
```

(`engine/mod/super_modulator.cpp:28-29`). BPM does not appear in the free
branch at all. SYNC is not its own switch any more — it is the right half of
COUPLE (`host/vcv/src/Fireflow.cpp:879-880`, which computes a `grid` bool from
`coupleKnob >= kCoupleZoneSplit` and passes it to `set_sync`). With COUPLE left
of centre, turning TEMPO does nothing whatsoever.

**TIDE never reaches the melodic lane.** `_apply_rate` gives the PITCH lane
`_pitch_scale` and only the four texture lanes `_mod_scale * _tide_mult`
(`super_modulator.cpp:44-46`). TIDE is a *ratio*, not a speed.

**RATE is the only real speed control and is unusable at the bottom.**
`free_hz(n) = 0.02 * 1500^n` (`engine/mod/divisions.h:57-60`): the floor is a
50 s cycle, so minutes are unreachable, and 30% of travel is already 5.5 s. The
whole drone territory sits in the bottom fifth of one knob, per deck.

**Under Glow there is no speed control at all.** `P_RATE_A/B` and `P_TIDE` are
base rules only (`engine/flow/taste.h:965-966, 980`). M_MOTION gave TIDE up on
2026-08-12 so speed would travel with a transferred patch. So a woken pad's
modulation speed is pure terrain luck, and the player can do neither of the two
things they want: make a pad modulate rhythmically when the terrain supports it,
or stretch its modulation into minutes-long sine waves for a drone.

## 2. What PACE is

`Instrument::set_pace(float norm)`, 0..1, where **0.5 is exactly x1 and a
bit-identical no-op** — the property that makes TIDE 0.5 a no-op, for the same
reason: a global time control must have a position where it provably does not
exist. (Scoped: no-op *within a build that already carries §2.1's double
accumulator*, not against today's binary.)

```
norm <= 0.5 :  mult = 32 ^ (2*norm - 1)     ->  1/32 .. 1
norm >  0.5 :  mult =  4 ^ (2*norm - 1)     ->     1 .. 4
```

Asymmetric on purpose: the fast end is already reachable through RATE.

| knob | 0.0 | 0.25 | 0.5 | 0.75 | 1.0 |
|---|---|---|---|---|---|
| multiplier | x1/32 | x1/5.7 | x1 | x2 | x4 |
| an 8 s cycle becomes | 4.3 min | 45 s | 8 s | 4 s | 2 s |
| a 50 s cycle becomes | 27 min | 4.7 min | 50 s | 25 s | 12.5 s |

**`pace_mult(norm)` lives in `engine/mod/divisions.h`**, beside `free_hz` and
`kTideRatios`, for the reason at `divisions.h:55-56`: the curve lives in the
engine header so the host tooltip shows what the engine runs. The *name* string
does not follow it there — `kTideNames` is a 9-rung discrete table and PACE is
continuous, so a name must be formatted, and a formatted string in a header the
firmware compiles is either an allocation or a static buffer. **The host owns
the formatting; only the multiplier is shared.**

### 2.1 The accumulator floor, and the double conversion

**x1/32 does not work on the current accumulator.** `ModLane::_phase` is a
`float` (`engine/mod/lane.h:190`) accumulated per sample at `lane.cpp:585`.
`LANE_PITCH` runs that path every sample (`super_modulator.cpp:109`). Once
`_phase_inc` falls below half an ulp of the current binade the addition returns
`_phase` unchanged. Simulated in float32 at 48 kHz over 600 s:

| lane rate | phase inc | result |
|---|---|---|
| 0.02 Hz (RATE 0, today) | 4.17e-07 | runs |
| 0.0025 Hz (x1/8) | 5.21e-08 | runs |
| 0.00125 Hz (x1/16) | 2.60e-08 | **freezes at phase 0.50** |
| 0.000625 Hz (x1/32) | 1.30e-08 | **freezes at phase 0.25** |

The last row is this document's own "a 50 s cycle becomes 27 min". Just above
the stall there is a worse band where every add rounds up to a full ulp and the
lane runs up to 2x *faster* than commanded. This is a **latent bug that already
exists**: at RATE 0 the margin is only 14x, and DRIFT's `_ev_rate` eats 20%.

**Decision (owner): `_phase` and `_phase_inc` become `double`, uniformly for
all five lanes.** The public interface stays float — `phase()`, `phase_eff()`,
`step_at_phase()`, `clock_scale()`, `pitch_step_samples()`, `base_hz()` keep
their float returns and cast on read. Verified in review: every *external*
reader survives, including `Center::_grid_servo`, the Kuramoto `dphi`,
`_rebase_grid` and `_snap_phase`, which take float differences of a quantity
moving ~21 float ulps per control tick at x1/32.

**`tick()`'s internal float shadow must convert with it.** This is the part
revision 2 missed and it is not optional. `process_window_end`
(`lane.cpp:18-33`) exists to "replay only `process()`'s raw float phase
additions for the whole control window" — it is a *model of `process()`*. Move
`process()` to double and the model stops modelling. Worse, `lane.cpp:703-704`
assigns `shadow_end.phase` — a float — **back into `_phase`**, so the
accumulator would be re-quantized to float on every near-endpoint tick, exactly
reintroducing the melodic/texture divergence that "convert all five" was chosen
to avoid. Convert together: `ProcessWindowEnd::phase`, `process_window_end`'s
`phase` parameter and `phase_per_sample_by_wrap`, `window_start_phase`
(`:645`), `window_dp[]` (`:646`), and `dp1` (`:675`).

**Two gates move, not one.** Besides the render hashes below,
`tests/test_lane_tick.cpp` asserts `skew_events <= 4` over 400 ticks, and its
comment at `:67-73` states the budget's premise outright — "the two paths
accumulate phase differently (96 rounded adds vs one fused product)". The
conversion changes that premise. **Re-derive the budget by measurement; do not
widen it to fit.**

**Render hashes must be re-baselined** in the same commit, deliberately and
with the reason in the message: double accumulation produces different low bits,
so `ctrl_identity` and any byte-identity gate in
`tests/check_render_hash.cmake` moves. Note the consequence for §5: a
re-baselined hash cannot *catch* a wrong `_pace` default, it bakes it in. The
default needs its own doctest gate.

**The texture lanes' headroom is 96 x `kLaneRatio[i]`, not a flat 96x** —
`kLaneRatio = {2, 0.5, 1, 0.75, 1.5}` (`super_modulator.cpp:8`), so the slowest
texture lane has 48x. Moot once all five convert, but do not quote the best
case as the worst. Verified in review: `tick()` genuinely advances in one fused
add (`lane.cpp:705`, `samples_left * dp1`) rather than a loop, so the headroom
claim itself holds.

**CPU.** `lib/libDaisy/core/Makefile:93` sets `-mfpu=fpv5-d16` — the
double-precision variant, not `fpv5-sp-d16` — and `shell/Makefile:127` includes
that Makefile, so these are hardware double adds, not soft-float. They are
still roughly twice the latency of float on the M7 and the melodic lane runs
per sample, so **§8's gate table carries a bench row**. Mind
`spotykach-bench-stale-object-trap`: verify the new row via `bench.map`, not the
memory table.

Rejected alternatives, measured: shortening to x1/8 delivers the wish (6.7 min
at RATE 0) but parks 1.75x from a cliff that fails *silently*, and DRIFT pushes
that to 1.4x; clamping the effective lane rate creates a dead knob zone whose
size depends on the terrain, which is the defect this work exists to remove.

## 3. Where PACE applies

`Instrument::set_tempo_bpm` calls itself "the real single door" for tempo and
fans out to three consumers (`engine/instrument.cpp:111-113`). PACE reaches two
of them; FLUX gets the raw BPM and is repaired differently (§3.3).

| consumer | receives | effect |
|---|---|---|
| `_center.set_tempo_bpm` | `bpm * pace` | transport beats accumulate stretched, so the step grid stretches and the grid servo aims at the stretched target instead of fighting it |
| `p.mod().set_pace` | `pace` | `_base_hz *= _pace` in **both** branches of `_update_rate` |
| `p.fx().set_bpm` | `bpm` **raw** | FLUX stays in real time; its rhythm reader is corrected instead (§3.3) |

Verified in review and worth keeping: the pace factor appears once on each side
of `Center::_grid_servo`'s comparison and cancels, and in the free world
`center.cpp:207-210` scales `geo` and every `division_hz(i, bpm*pace)`
together, so `nearest_division` picks a pace-invariant rung. Fireflow's own
clock measurement (`Fireflow.cpp:920-923`) is wall-clock and feeds the raw BPM,
so PACE never enters it.

### 3.1 `set_pace` must walk through the door itself

`Instrument::set_pace` **re-runs the fan-out**; it does not merely store `_pace`
and wait for the next tempo push. Without that:

- Under Glow, `P_TEMPO_BPM` only reaches `apply_param` when its own value moves
  (the change guard at `engine/flow/flow.cpp:607-612`), and `Glow.cpp:1037-1042`
  re-pushes tempo only when a fader is assigned to it. Turning PACE on a settled
  terrain would stretch the lanes and leave the transport unstretched — the
  servo fight this section exists to prevent.
- `host/render/main.cpp:65` calls `set_tempo_bpm` once before any scenario
  event, so a `set_pace` event would never reach the transport.

Fireflow would have self-healed (`Fireflow.cpp:918-924` pushes tempo every
tick), which is why this would have shipped: the host it is developed in is the
one host that hides it.

Three requirements come with that:

- **`Instrument::_bpm` stays the RAW bpm.** `set_tempo_bpm` writes `_bpm` at
  `instrument.cpp:110`, so `set_pace` must not re-enter the public setter with
  a pre-multiplied value or pace compounds on every call. Factor a private
  `_apply_tempo()` that both entry points call.
- **`set_pace` needs the same finite guard `set_tempo_bpm` has**
  (`instrument.cpp:101-109`), and for the same stated reason: `scenario.cpp`
  forwards scenario-file values unvalidated, and §8 adds `set_pace` to that
  chain. A NaN pace is worse than a NaN BPM — `Transport::set_bpm` catches the
  latter, but `set_pace(NaN)` makes `_base_hz` NaN, which `set_rate_hz` maps to
  0 (`lane.cpp:102`), and both decks go silent with no error.
- **An early-out** (`if (m == _pace) return;`), matching its FLUX sibling
  (`flux.cpp:42`). Fireflow's `pushParams` pushes every knob unconditionally, so
  without it the fan-out runs twice per control tick.

Cost, verified: no recursion, no callbacks; `_apply_steps` is *not* on this path
(only `set_tide`/`set_step` call it), so there is no `ModLane::set_step` churn.

### 3.2 The external clock

`Transport::clock_pulse()` is `_beats = std::round(_beats)`
(`engine/center/transport.h:26`), driven once per real beat from
`Fireflow.cpp:931-936`. It encodes "one external pulse == one transport beat",
which a paced transport breaks outright: at x1/32 `_beats` advances 0.03125
between pulses and `round()` snaps it back to the same integer, so **the
transport freezes at beat 0** while the lanes run. At x4 every pulse delivers up
to half a beat of discontinuity into the hard servo.

One external pulse is one *real* beat, which is `pace` paced-beats. But
snapping to a multiple of `pace` **anchored at absolute zero is wrong**, because
`_beats` is `∫ bpm·pace dt` — after a pace change, `_beats / pace_new` is not
the real beat count. Worked example: 100 beats at x1, then PACE to 0.6
(x1.32); the next pulse computes `round(100/1.32) = 76`, `76 * 1.32 = 100.28`,
a jump nobody asked for. Under a swept macro that fires on *every* pulse, up to
half a real beat, into a servo whose authority is `kLockCap = 0.35`
(`center.cpp:45`) — the same shock this section opens by condemning, moved from
"always" to "whenever PACE moves", and PACE moving is the feature.

**The anchor is the previous pulse, and the pace is an argument, not stored
state:**

```cpp
void clock_pulse(float pace) {
    _beats  = _anchor + pace * std::round((_beats - _anchor) / pace);
    _anchor = _beats;
}
```

`Center::clock_pulse` forwards the parameter. Passing rather than storing kills
a `Transport::_pace` member that would have been a value-initialised **zero
that divides** — `round(_beats/0.f)*0.f` is NaN, `_beats` never recovers, and
NaN propagates to `set_rate_hz` where it becomes 0 and stops both decks
silently. It also removes a third copy of the pace that could desync across
`reinit()`.

`set_pace` re-anchors (`_anchor = _beats`) so the grid follows the knob rather
than lagging it. At `pace == 1` the whole expression is bit-identical to today.

`_beats` no longer lands on integers at non-dyadic pace. Nothing reads it that
way — `beat_phase()` has no consumers in `engine/` or `host/`, and
`_grid_servo`/`_rebase_grid`/`_snap_phase` all use `beats() * cpb` continuously
— but a future downbeat reader must not assume integers.

`Transport::set_bpm` drops non-positive and non-finite values
(`transport.h:22`); the lowest product, 40 BPM x 1/32 = 1.25 BPM, is safe in
the double accumulator.

### 3.3 What PACE reaches that revision 1 denied

- **FLUX desyncs; the repair is in the rhythm reader, not the delay time.**
  `flux.cpp:57` derives the delay time from `division_hz(…, _bpm)` on the raw
  BPM, so a synced FLUX "1/4" is not a quarter of the stretched grid. Its THIN
  pattern compares `_rhy_gap` — samples between PITCH-lane onsets, fully paced
  — against an unpaced `rep` (`flux.cpp:94-104`), so below x1 the skip count
  pins at `kMaxSkip = 16` (`fx/drag.h:10`) and any LINK > 0 becomes a
  near-permanent mute of the FLUX return.
  **Ruling (owner): divide `_rhy_gap` by the pace inside
  `update_thin_pattern`**, so both sides of `n = _rhy_gap / rep` are measured in
  the same time frame. FLUX itself stays in real time; the buffer is untouched
  (`update_time_target` already clamps to the tape length, `flux.cpp:64-65`).
  Revision 2's "follow PACE on the fast half" is withdrawn: above x1 both
  quantities already shrink by pace, so `n` was **already** pace-invariant there
  and the rule fixed a non-problem. What remains unfixed is the tooltip:
  `FluxRateQuantity` prints a division name that is no longer a division of the
  stretched grid. That is stated in the tooltip and accepted in §9.
- **The BBD inherits PACE through `master_hz`.** `part.cpp:441-443` pushes
  `set_cycle(1/master_hz)` and `bbd_engine.cpp:222` derives its clock window
  from it; at x1/32 that window drops to roughly 0.16–5.1 Hz, and a PACE turn is
  a delay-line pitch bend of up to five octaves — the mechanism
  `host/vcv/README.md:492-500` documents for SIZE. A real gesture, not a bug,
  but "nothing clicks" is false for a BBD deck. Listening check at both ends.
- **The sampler's step clock is a third consumer.** `part.cpp:324` pushes
  `pitch_step_samples()` into a Sampler deck in FLOW *and* STEP; it grows by
  1/pace, so at x1/32 from RATE 0 it reaches ~9.6e6 samples, `_pool_size()`
  collapses to 1 (`sampler_engine.cpp:737`) and the slice pool stops being a
  pool. Same listening check.
- **Synth envelopes rescale with the cycle.** `synth_engine.cpp:275-276`
  derives attack and decay from `_cycle_s`, re-pushed to all voices every
  control tick, so a PACE turn changes the envelope of sounding notes — an
  amplitude change. It also saturates at `kDecayMaxS = 20 s`, so below about
  x1/3 PACE stops lengthening decays.
- **Wall-clock constants stay unpaced**, by design but worth naming: `kOuTau`
  (45 s, the DRIFT walk), SMOOTH's slew (`lane.cpp:262-276`), SPOT's kick,
  `_settle_coef`. At x1/32 DRIFT's rate wander cycles dozens of times inside one
  lane cycle. PACE is a lane-rate control, not a "modulation pace" control, and
  the difference is loudest at the settings this design is sold on. Whether
  DRIFT should follow PACE is a listening question, deferred.

Out of scope and unchanged: the reverb's SMEAR/WOBL diffuser LFOs, and Glow's
weather oscillators.

## 4. The flow layer: DIRT becomes PACE

### 4.1 Why PACE must not be a story

Story-owned parameters are unreachable from the base overlay **by
construction** — `generate()` applies the overlay by iterating `kBaseRules`,
not `P_COUNT` (`engine/flow/terrain.cpp:437-461`). A macro that *owned* the
pace would throw away a transferred patch's speed, the class of bug 887b767 and
3e03e81 just closed one level down.

- **`P_PACE` is a new flow parameter with a base rule**, span `{0.5, 0.5}` for
  all four archetypes. The terrain draws no pace; the row exists so the overlay
  has a destination and the coverage check has no hole.
- **The macro is an offset**, applied as described in §4.2.
- **`M_DIRT` is renamed `M_PACE`**, keeping slot index 3.
- **NEW does not change the speed** (but see §4.4 for what NEW *does* do).

The offset is not constant in log units across the knob: the mapping is `32^`
below centre and `4^` above, so the same +0.1 of travel is x2.0 on the slow half
and x1.44 on the fast half, and on a pad carrying a transferred `base[P_PACE]`
the macro's sensitivity depends on where that base sits. Stated rather than
hidden; it is the price of §2's asymmetric range.

### 4.2 Where the offset is applied — the one safe window, and on which operand

The formula is not the risk; the insertion point and the operand are.

**Placement.** `Flow::begin_blend` computes `_resid[p] = _cont_now[p] -
_cand_cur[p]` (`flow.cpp:147-149`), and the veto-safety argument at
`flow.cpp:565-576` rests on that residual being exactly zero on a fresh press
from a settled instrument. `_cand_cur[p]` is set at `flow.cpp:447`,
`_cont_now[p]` at `:489`. An offset applied **between those two lines** puts
`eff - 0.5` — up to ±0.5 — into `_resid[P_PACE]` on *every* press: knob at
0.75, press NEW, and the pushed value is 1.0 at `ph = 0` decaying to 0.75 over
six seconds, a x4 → x2 slide that contradicts "NEW does not change the speed".

**So the offset goes into the guard chain after `flow.cpp:489`**, beside the
BODY FILT floor and the BBD RANGE cap, and **before** the veto loop (`:577`)
and the change guard (`:607`) so `param_now()` publishes what the engine gets.

**Operand.** At that point the live variable is `v` — the blend line's result
(`flow.cpp:483-485`), *not* `t.base[]`. On a settled terrain the two agree for a
story-less base rule, but during a blend between two terrains carrying
*different* transferred paces, writing `t.base[P_PACE]` literally produces a
step where the design promises a six-second ramp. The guard reads:

```cpp
else if (p == P_PACE) v = clamp_to(kParams[p], v + (_eff[M_PACE] - 0.5f));
```

`clamp_to`, not `clamp01`, to match every neighbouring guard.

### 4.3 The first macro without a story

M_PACE has no `StoryVariant`. Its whole effect is a runtime role — M_MOTION
already scales the entire weather offset (`flow.cpp:432-433`) — but it is the
first macro whose effect is *only* that.

`terrain.cpp:531` calls `pick_index(r, n_var)`, which returns `-1` for
`n_var == 0` (`terrain.cpp:84-87`). Stage 4 must guard `n_var == 0` **before**
that call and set both:

```cpp
mm.story = -1;
t.window[m] = {0.f, 1.f};      // the identity, not the {0,0} value-init
```

`engine/flow/terrain.h:93-102` warns that `window` has no default member
initialiser and that `{0,0}` instead of the `{0,1}` identity is "currently
unreachable" *because* every macro has a story, and asks to be re-read if that
invariant moves. This design moves it; that comment is rewritten in the same
commit.

### 4.4 Weather, and what a reroll of PACE does

`weather_of` already skips M_MOTION (`flow.cpp:297`). **M_PACE gets the same
exclusion**: the weather offset is up to ±0.10 in knob units, and in this
mapping 0.10 is a factor of **x2**. A tempo wandering by two makes every other
macro's motion unreadable.

The exclusion also keeps §2's exact-no-op claim true, since that depends on
`eff[M_PACE]` being exactly 0.5. (It additionally steadies
`test_flow_transfer_diff`, which sets every macro to 0.5 — but review measured
that margin at one order of magnitude, not a robust dependency, so it is a
supporting reason and must not become a plan's primary justification.)

**`new_partial(1u << M_PACE)` is not a no-op, and loses its last caller.** The
weather counter is the *sum* of all six macro counters (`terrain.h:47-51`), so
rerolling a story-less PACE redraws the entire weather layer — which drives the
other five macros — plus a 6 s blend and a duck schedule. It does nothing at all
to PACE, which has no story to redraw.

Two callers reach `new_partial` today, and only one of them can name a single
macro. A pad hold fires `new_partial(0x3F)` (`Glow.cpp:1028`) — all six at once,
so the weather is redrawn there regardless and PACE's presence in the mask
changes nothing. The other is the Workshop menu's "Reroll one macro"
(`Glow.cpp:1357-1365`), one entry per macro, `1u << i`.

**Ruling: PACE is skipped in that submenu.** An entry that redraws everyone
else's weather under the label "PACE" does something other than what it says.
With the skip, `new_partial(1u << M_PACE)` has no caller and the question is
closed rather than answered.

*(Revision 3 of this section asked the plan to rule on whether M_PACE should be
excluded from a gesture mark mask. That question was built on dead code: the
Simple Touch 2 surface removed Glow's NEW button and its `GestureBridge` on
2026-08-11, and `engine/flow/gesture.h` now has no caller outside `tests/`. The
hold-and-turn gesture it described does not exist in the shipping module.)*

### 4.5 The four orphaned parameters

`P_GRIT_A`, `P_GRIT_B`, `P_COMP_A` and `P_DRIVE` lose their owner and become
base rules.

**Span ruling (owner): real spans, not the degenerate `bp0`.** The alternative
was inheriting GRIT's and DRIVE's `{0, 0}` bp0 spans (`taste.h:901-902, 911`),
which would have kept every existing terrain code rendering identically — at
the price of pinning `P_DRIVE` at 0 forever. DRIVE is audible today (the story
reaches .25–.40 in Q4) and **has had no Fireflow control since the 2026-08-09
reduction retired MASTER_DRIVE/PUSH and DRIVE_A/B** (`gen_panel.py:444, 538`),
so nothing could restore it: Glow would lose master drive outright. Real spans
instead, and **existing terrain codes re-render — accepted**, consistent with
this project's dev-alpha stance on compatibility.

Terrain codes move for a second reason regardless of the span choice: `P_COMP_A`
shifts from DIRT's story `bp0`, drawn off `kStreamMacroBase + M_DIRT` under
`t.adventure[M_DIRT]`, to `kStreamParamBase + P_COMP_A` under
`t.adventure_base` (`terrain.cpp:382-384`) — a different stream and a different
adventure level.

**Transfer status differs per parameter, and the map is the authority (§5):**

| param | Fireflow source | status |
|---|---|---|
| `P_COMP_A` | the deck-A LVL knob | mapped, and needs a veto note (below) |
| `P_GRIT_A/B` | `GRIT_A/B` (`gen_panel.py:307`) | mapped, but **inaudible under Glow** |
| `P_DRIVE` | **none — retired 2026-08-09** | **UNREACHABLE**, joining `P_ROOT` |

- `P_COMP_A` needs *two* bridge edits. The "NO DESTINATION … story-owned"
  note (`flow_patch_bridge.hpp:465-468`) is replaced by a `set_base` **plus**
  the veto-band note `P_COMP_B` already carries at `:449-458` — `kVetos`
  confines it to 0.40..0.60 (`taste.h:643`) and `test_flow_transfer_diff.cpp`
  requires a `"REWRITTEN AT RUNTIME"` tag on any carried value the runtime moves
  (helper `:100-107`, gate `:111-145`). That deleted note also carries a second
  fact — the deck BALANCE loss — which must survive somewhere, not vanish.
- `P_GRIT_A/B` stay inaudible under Glow exactly as `P_FLUXMIX_A/B` is: nothing
  in `engine/flow/` or `Glow.cpp` calls `set_fx_on`, `SoftSwitch` boots off, and
  `Grit::process` returns bit-exact dry (`grit.cpp:94-95`). Their map rows carry
  the note the FLUXMIX row already does. **Fixing that gap is not part of this
  work** — it hits FLUX identically and is entangled with the GRIT wet path's
  -9.3 dB attenuation, so it is one sound question to solve in one piece.

**`distance()` needs the full harness re-run, not an arithmetic patch.** It
divides by `P_COUNT` (`terrain.cpp:683-691`), which goes 63 → 64 for a flat
−1.56%; but `P_COMP_A`'s value changes on every terrain, and that feeds
`distance()`, which feeds `draw_new`'s `kDistanceMin` retry loop. The 90-line
measurement block at `terrain.cpp:594-682` states `P_COUNT 63` and warns "any
table measurement that moves this number is wrong before it is interesting".
Re-run it.

### 4.6 Where P_PACE goes in the parameter table

`P_MODE` carries a "MUST STAY LAST" note for two reasons; one survives.

Load-bearing: base draws are keyed `kStreamParamBase + param`
(`terrain.cpp:382`; the line number in `flow_params.h:107`'s own comment is
stale), so inserting a parameter *before* `P_MODE` re-seeds its stream and
re-resolves the FLOW/STEP draw of every terrain code.

Positional only: `flow.cpp:526-560` argues `_mode_now` is safe to read during
the `P_RANGE_A/B` iteration because `P_MODE` is last and unpushed.

So **`P_PACE` is appended after `P_MODE`**, the positional argument becomes
`static_assert(P_RANGE_A < P_MODE && P_RANGE_B < P_MODE)` beside the two
guarding ENGINE ordering (`flow.cpp:53-56`), and **the "MUST STAY LAST" comment
is rewritten** — at `flow_params.h:107` and at its three echoes in
`flow.cpp:512-513, 527-528, 542-543` — or it becomes a lie the moment the
parameter lands.

Verified: `kStreamParamBase` is 0 with a 1000-wide block (`flow_rng.h:11-12`),
so id 63 collides with nothing; `push_mode_and_steps` reads `_pushed[]` after
the loop and is position-free; `encode_base` is name-keyed `NAME:value;` pairs
(`flow_patch_bridge.hpp:768-788`) and the terrain code holds only master plus
counters, so **no saved format changes length or layout** and old clipboard
strings and `pool.tsv` rows stay valid.

`apply_param` gains `case P_PACE: in.set_pace(v); break;`.

## 5. The parameter map is a deliverable, not a follow-up

`docs/flow-fireflow-param-map.md` is the repo's declared authority — CLAUDE.md:
"if a mapping is not in that file, the converter does not do it". This design
moves five rows through it, and every count in it becomes false:

- the Counts table (`:16-18`): mapped 41 / UNREACHABLE 1 / total 42 → 44 / 2 / 46
  (four orphans added, `P_DRIVE` landing in UNREACHABLE, plus `P_PACE`);
- `:28`, "`P_COMP_A` is deliberately absent … story-owned";
- `:33`, "The other 21 of `P_COUNT` = 63";
- `:178-182`, "Nothing outside the 42 has a destination … GRIT … DRIVE …
  `COMP_A`".

An implementer who adds `set_base(P_COMP_A)` to the bridge without a map row
violates the repo's own contract. **The map edit lands in the same commit as
the base-rule change.**

The same arithmetic rots in four more places, none of them gated by a test:
`flow_patch_bridge.hpp:540-559` (six numbers and two names), `Glow.cpp:89` ("a
38-row overlay", already wrong today), `docs/roadmap.md:2234`,
`engine/flow/terrain.h:55-57` ("38 base-rule slots … 315 bytes", where 315 =
63×5 becomes 320), and `CLAUDE.md:17`.

## 6. Zero is not neutral — the init trap

PACE's safety argument is "0.5 is a no-op". This codebase's convention is
value-initialisation to **zero**, and zero here is **x1/32**, the extreme. For
TIDE that collision was survivable (zero means x1/4); for PACE the symptom is a
27-minute cycle, which a user files as "Glow is broken".

Seven carriers. The one that matters most is the one revision 2 missed, because
it reaches the *engine* rather than a host:

- **`Flow::_knob[MACRO_COUNT] = {}`** (`flow.h:177`), re-zeroed in
  `Flow::init()` (`flow.cpp:82`). A Flow whose macros were never pushed sits at
  offset −0.5, i.e. **x1/32 on every parameter draw**. Glow is safe (it
  configures the macro at 0.5 and re-pushes all six every tick), but
  `host/render`'s `flow_wake` pushes no macros (`scenario.cpp:251`), so every
  headless demo of PACE would play at x1/32 — as would every doctest that
  constructs a `Flow` without `set_macro`. **`Flow::init()` sets
  `_knob[M_PACE] = 0.5f`.**
- `FireflowPatch::p[]` (`flow_patch_bridge.hpp:158-161`) — 20 `fp{}` sites
  across the bridge and transfer-diff tests.
- `INIT_DEFAULTS["PACE"] = 0.5` in `src/init_patch.hpp`, required for every
  `PARAMS` entry by `res/test_panel.py:2619-2622` — the
  `fireflow-control-merge-init-trap` shape exactly.
- `Instrument::_pace = 1.0f`. **Needs its own doctest gate**: §2.1 re-baselines
  the render hash in this same work, so the hash bakes the default in rather
  than catching it.
- `bench/audition/init_patch.cpp` (`:33-40, 187-188`) — and **no test catches a
  miss**: `test_panel.py`'s scraper guards only three named calls (`:2986-2993`).
- an older `.vcv` with no PACE id, filled by Rack from the configured default.
- `Transport::_pace` — **eliminated by design**, since §3.2 passes the pace as
  an argument rather than storing it. Listed so nobody reintroduces it.

## 7. The two host surfaces

**Fireflow.** A `PACE` `SMKNOB` in the free `ROW_TIME1` slot beside TEMPO. The
slot is genuinely free (`gen_panel.py:438`) and a small knob there is legal —
neighbour spacing, label placement and printed-word uniqueness were all checked
against `res/test_panel.py` during review. Exact coordinates belong in the plan.
The parameter is appended at the end of `PARAMS`;
`tests/test_seed_audition_init.cpp:74` asserts `NUM_PARAMS == 68` and becomes
69.

**Hardware panel (60 HP — `gen_hw_panel.py:16`, `HP = 60`; the 68 that appears
in older notes is the parameter count, not the width).**

```python
CENTER_POS["PACE"] = (127.4, Y_B1G)     # HW_SIZE["PACE"] = "S"
```

Directly under TEMPO, immediately left of TIDE. Two reasons:

- It follows the panel's own grammar. The G row is already used for a second
  control hanging under its K-row column head — `MOD` under `SHAPE` (both
  26.5), `DENSITY` under `RANGE` (both 51.5). `PACE` under `TEMPO` (both 127.4)
  is the same construction, and it mirrors the VCV decision of putting PACE in
  the TIMING box beside TEMPO.
- It puts PACE next to TIDE. Those two are the confusion pair this whole design
  came out of; adjacency is what makes the distinction learnable — TIDE is a
  ratio, PACE is the speed.

Clearances, computed against the real geometry rather than estimated:

| check | worst case | slack |
|---|---|---|
| glyph vs glyph | TIDE, 13.0 mm centre distance | **2.00 mm** |
| PACE's caption vs other footprints | TIDE | 8.26 mm |
| other captions vs PACE's footprint | TEMPO | 3.00 mm (needs 1.5) |

13.0 mm to TIDE is *more* generous than the row above, which runs a 12.5 mm
pitch at the same radii. Inside `KEEP_TOP`/`KEEP_BOT`. The position must still
clear `test_hw_slot_map_matches_the_reduced_inventory` and
`test_labels_stay_off_neighbour_footprints` (`LBL_MARGIN` at
`test_hw_panel.py:213`) when the generator actually runs.

Two consequences named rather than discovered:

- **The row loses its symmetry.** TIDE and CHOKE sit at exactly ±12 mm about
  the panel centre today; the row becomes −25 / −12 / +12. The centre is a set
  of centred clusters rather than one axis, so this is defensible — but it is a
  change to the layout's grain, not a pure addition.
- **PACE cannot be a large knob here.** A `G` glyph (r = 8.0) needs 13.5 mm to
  TIDE and gets 13.0. Making PACE a primary-sized control means moving TIDE,
  which is a regrouping decision, not a placement one — and belongs with
  `hw-panel-regroup-open-decisions`, not with this work.

The hard gate remains, and it sets the commit boundary: `gen_hw_panel.py:166`
runs `HW_PARAMS = [place(c) for c in gp.RUNTIME_PANEL_PARAMS]` at *module
scope* and `place()` ends in `raise KeyError` (`:164`), so the instant PACE
enters `PARAMS` without a `CENTER_POS` entry, `import gen_hw_panel` throws,
`res/test_hw_panel.py` dies at import and takes the `hw_panel_guard` ctest with
it. The `PARAMS` edit and the `CENTER_POS` entry are **one commit** (§9).

**Glow.** `M_DIRT` → `M_PACE`. The panel is generated and byte-gated
(`test_flow_panel.py:385`), and `res/Glow.svg` needs no edit because the macro
knobs print no caption. A saved Glow patch cannot break: Rack persists params by
numeric id and the slot index is unchanged. The plan derives the edit sites by
grep rather than from a list here — but three of them are not renames and must
be handled deliberately:

- `taste.h:1032` and `test_flow_veto.cpp:107` describe DIRT's *story*, which
  this work deletes. They are **deletions or rewrites, not substitutions**.
- `docs/superpowers/specs/2026-08-11-glow-touch-2-panel-design.md:212` assigns
  DIRT to trim knob `S34` on the shipping Touch-2 panel. That is a **live
  hardware assignment**, not history, and must be updated. The older flow-machine
  and taste-structure specs stay untouched per the repo's convention on finished
  decisions.

**Tooltips that would otherwise lie:** RATE must multiply PACE in;
`FluxRateQuantity` must state that its division name no longer refers to the
stretched grid (§3.3); PACE's own tooltip formats `pace_mult` host-side.

## 8. Gates

All of these are doctest gates. The render host can *demonstrate* PACE but gate
almost none of it: `flow_wake` takes a terrain code with no overlay verb
(`scenario.cpp:238-251`) and the CSV logs lane outputs and `phase_err` but
neither lane Hz nor BPM. It still needs `else if (a == "set_pace")
inst.set_pace(e.value);` — and note that unknown actions there are **silently
ignored** (`scenario.cpp:220`), so a scenario written before the verb lands is a
no-op, not an error.

| gate | what it holds | observer |
|---|---|---|
| centre is neutral | `set_pace(0.5)` leaves every lane's rate **exactly** unchanged | `lane_rate_hz_for_test` ✔ |
| endpoints | 0 → x1/32, 0.5 → x1.0, 1 → x4 | `pace_mult()` is a public free function in `divisions.h`, not an observer ✔ |
| **phase actually advances** | at every PACE value, **wraps per unit time** match the commanded rate | wrap counter — **new**, see below |
| default is neutral | a fresh `Instrument` and a fresh `Flow` both run at x1 | as above ✔ |
| free world | lane Hz scales with `set_synced(false)` | ✔ |
| grid world | lane Hz **and** transport BPM scale, and the servo's steady-state correction is ≈ 0 | `phase_err()` ✔; **transport BPM path is new** |
| external clock | at a fixed real-beat pulse interval, transport beats advance `pace` per pulse at x1/32 and x4, and a PACE change mid-stream causes no jump | as above |
| FLUX | delay time unchanged at every PACE; `_thin_n` unchanged at every PACE | `flux_delay_target_for_test` ✔; THIN needs a reader |
| transfer | a carried patch plays its own pace at knob centre | ✔ — `test_flow_overlay.cpp:38-72` covers `P_PACE` automatically since it iterates `kBaseRuleCount` |
| coverage | `P_PACE` has exactly one owner; exactly one named story-less macro is permitted | ✔ |
| CPU | the melodic lane's per-sample cost after the double conversion | `bench/` row — **new** |

**The wraps-per-time gate is the important one.** Every other gate measures the
*commanded* rate, which stays perfectly correct while the lane is frozen — which
is exactly why revision 1's gate list would have shipped §2.1's stall green.

**Observer conventions**, audited: the `_for_test` suffix is universal (30+
sites); the `SPKY_TESTING` guard is applied in engine-core headers (`lane.h:40`,
`super_modulator.h:45`, `instrument.h:82`, `flow.h:119`) but not in `fx/`. All
new observers land in guarded territory. Two specifics:

- **The wrap counter's `++` sits in the per-sample hot path** at
  `lane.cpp:587`, so the *increment* is `SPKY_TESTING`-guarded, not only the
  getter — `body_voice.cpp:178` is the precedent. Otherwise firmware pays for a
  test.
- **The transport observer must read `Transport::bpm()`**, which is already
  public (`transport.h:23`) and needs only a `Center` → `Instrument`
  pass-through. It must **not** expose `Instrument::_bpm`, which is the raw,
  unpaced value — a gate asserting against that number would be measuring
  something PACE never touches, a textbook `fireflow-vacuous-test-gates` shape.

### 8.1 Tests that go red and must be updated deliberately

Eight. Each carries a comment asking to be updated rather than deleted:

1. `test_flow_overlay.cpp:34-36` — `kBaseRuleCount == 42` → 47, and
   `CHECK_FALSE(is_base_rule(P_COMP_A))` inverts. The comment at `:24-33`
   requires both numbers to move together and to be named in the commit.
2. `test_flow_terrain.cpp:23` — `mm.n_targets >= 1`.
3. `test_flow_terrain.cpp:39` — `span_max >= kMinSpan`, the "no dead knob" gate.
   Widening needs the same "exactly one named story-less macro" discipline as
   the taste gate, or that guarantee quietly dies for all six macros.
4. `test_flow_taste.cpp:26-28` — `per_macro[m] >= 1`.
5. `test_flow_new.cpp:393` — `tested_macros >= MACRO_COUNT - 1`; the one
   permitted non-contributor is already spent on SPACE.
6. `test_seed_audition_init.cpp:74` — `NUM_PARAMS == 68` → 69.
7. `test_lane_tick.cpp` — the `skew_events <= 4` budget, whose premise §2.1
   changes. **Re-derive by measurement.**
8. `test_flow_veto.cpp:107` — describes `P_DRIVE`'s degenerate DIRT story,
   which this work removes.

`test_flow_veto.cpp:232-233` also needs a note: it filters terrains on the max
over all seven adventure levels, and `t.adventure[M_PACE]` is still drawn but
never used, so a dead level can qualify a terrain as "high adventure" and weaken
that sample by roughly 1/7 without the number moving.

### 8.2 The veto proof must be re-homed BEFORE the DIRT story is deleted

`test_flow_veto.cpp:196-206` is the suite's only red-capable proof that the
runtime veto clamp at `flow.cpp:577-582` fires at all, as distinct from the
ordinary `clamp_to(kParams, …)`. Its comment records the measurement: `P_COMP_A`
is the **sole** parameter this sweep drives past an interior bound, "because
COMP_A alone gets a story curve with more range than COMP_B's near-constant
base rule".

§4.5 removes that curve. Afterwards the only driver is `_resid`, bounded by the
span width, and the gate collapses to a coin flip at best. The failure mode is
social: an implementer sees a red `interior_hit`, reads §4.5 as licence, and
weakens it to `edge_hits > 0` — which that same comment already explains is
*not* evidence of the veto clamp.

**Ordered before the story is removed:** re-home the proof to `P_REVMIX_B`'s
0.08 interior bound and *measure* that the sweep drives it past that bound.
**Acceptance criterion:** `interior_hit[P_REVMIX_B]` true on at least the same
share of masters the current `P_COMP_A` gate achieves. If the existing sweep
cannot reach it, the sweep changes — a wider macro excursion or a second press
mid-blend — and not the assertion. If neither works, this design stops and the
DIRT story stays until a proof exists.

**Why B and not A — measured 2026-08-12.** `eval_terrain` resolves a parameter
owned by several stories by **farthest from base wins** (`flow.cpp:329`,
`d > dist[c.param]`). `P_REVMIX_A` has two owners: BRIGHT "dawn"
(`taste.h:894`), whose lowest cell is 0.40, and SPACE "bloom" (`taste.h:928`),
whose Q0 cell reaches 0.08–0.15. BRIGHT's distant high candidate keeps winning
the combine, so A is held off its own floor and never overshoots it.
`P_REVMIX_B` (`taste.h:929`) carries the identical SPACE curve and the
identical 0.08 veto bound with **no second owner**, so it reaches the floor
freely. Measured over the same sweep at a re-press cadence of `% 23`: A scores
**0/400 masters**, B scores **236/400**, against a `P_COMP_A` bar of 171/400
(13/60 at the unwidened `% 30` cadence).

Single ownership is therefore the property that earns the role — the opposite
of what revision 3 of this section claimed. Neither macro that owns REVMIX_B is
touched by §4.5, so the proof survives the DIRT deletion.

### 8.3 RED proofs

- **The grid gate.** Remove the `_pace` factor from the synced branch only. It
  only reddens at a PACE outside **[0.438, 0.608]**: the servo's authority is
  `kLockCap = 0.35` (`center.cpp:45`), so it fully absorbs any mismatch in
  `pace ∈ [0.65, 1.35]`. The gate must assert the steady-state correction is
  ≈ 0, not merely that phase error is *bounded* — it is bounded even while the
  servo fights, because the correction is capped.
- **The wraps gate.** Revert `_phase` to `float`; the x1/32 case must redden.
- **The transfer gate.** Remove the `P_PACE` row from the overlay path.
- **The external-clock gate.** Revert the anchor to absolute zero; the
  pace-change-mid-stream case must redden.

## 9. Ordering, and the two atomic commits

Two steps cannot be split without leaving the tree broken in between:

- **The flow commit.** Deleting the `M_DIRT` story, §4.3's `n_var == 0` guard,
  the five new base rules, the map-doc edit (§5), and gates 1–5 and 8 of §8.1
  land **together**. Deleting the story without the guard leaves
  `t.window[M_PACE]` at the `{0,0}` non-identity — no crash, silently wrong
  terrain output.
- **The panel commit.** The `PARAMS` append, the `CENTER_POS` entry,
  `INIT_DEFAULTS`, `NUM_PARAMS`, and `bench/audition/init_patch.cpp` land
  together, because `gen_hw_panel.py` throws at import without the slot (§7).

Everything else is orderable: §8.2's re-homing first (it can veto the design),
then the double conversion and `divisions.h`, then the engine control, then the
flow commit, then the bridge, then the panel commit.

## 10. Deliberately not in this design

- Any glide or smoothing on PACE.
- A weather target for PACE (§4.4); whether DRIFT should follow PACE (§3.3).
- Regrouping the TIMING box.
- The `set_fx_on` gap that makes GRIT and FLUX inaudible under Glow (§4.5).
- Changing TIDE — it keeps its position and meaning; PACE only makes that
  meaning nameable.
- **Accepted defect:** `FluxRateQuantity`'s division name no longer refers to
  the stretched grid at any PACE ≠ 1. Mitigated by the tooltip only (§3.3).

## 11. Corrections to earlier revisions

- **r1:** "x1/32 … a 50 s cycle becomes 27 min" — false on a float accumulator;
  the lane freezes (§2.1).
- **r1:** "FLUX stays in real time. The one deliberate exclusion." — false; it
  desyncs and its THIN pattern breaks (§3.3).
- **r1:** "PACE changes frequencies, not amplitudes … nothing to click" — false
  for synth envelopes, a BBD deck and a Sampler deck (§3.3).
- **r1:** "PACE uses the same door" — true but insufficient; `set_pace` must
  re-enter the fan-out (§3.1).
- **r1:** "every existing terrain code renders identically" — false, via
  `P_COMP_A`'s stream change (§4.5). Under r3's span ruling they change further,
  and that is now accepted rather than denied.
- **r1:** two gates listed; it is eight, plus a veto proof to re-home first.
- **r2:** the double conversion left `tick()`'s float shadow behind, and that
  shadow writes float back into the accumulator (§2.1).
- **r2:** `clock_pulse`'s snap was anchored at absolute zero, re-creating the
  half-beat servo shock on every pace change (§3.2).
- **r2:** "FLUX follows PACE on the fast half only" — withdrawn; above x1 `n`
  was already pace-invariant, so the rule fixed a non-problem and left the real
  one (§3.3).
- **r2:** §5's carrier inventory missed `Flow::_knob`, the only carrier that
  reaches the engine (§6), and would have introduced an eighth in
  `Transport::_pace` (§3.2).
- **r2:** the offset's operand was named as `base[P_PACE]`; at the correct
  insertion point it is the blend line's `v` (§4.2).
- **r2:** `docs/flow-fireflow-param-map.md`, the repo's declared authority for
  exactly the rows this work moves, was not mentioned at all (§5).
- **r2:** "they now travel with a transferred patch" — false for `P_DRIVE`,
  which has had no Fireflow control since 2026-08-09 (§4.5).
- **r2:** `terrain.cpp:160` cited for the stream key; the site is
  `terrain.cpp:382`, and `flow_params.h:107`'s own comment is stale.
- **r3:** §8.2 named `P_REVMIX_A` as the veto proof's new home "driven by both
  BRIGHT and SPACE". Having two owners is what *disqualifies* it — the
  farthest-from-base combine lets BRIGHT hold it off its own floor. Measured
  0/400 masters. The proof goes to the single-owner `P_REVMIX_B`, 236/400.
- **r3:** §4.4 asked the plan to rule on a gesture mark mask. The gesture it
  described was removed from Glow on 2026-08-11 with the NEW button, and
  `engine/flow/gesture.h` has had no caller outside `tests/` since. The real
  question was one Workshop menu entry, and §4.4 now rules on that instead.
