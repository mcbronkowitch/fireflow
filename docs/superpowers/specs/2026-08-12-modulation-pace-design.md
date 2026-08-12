# PACE — one global modulation time-stretch

Date: 2026-08-12
Status: design, revision 2. Reviewed adversarially on three lenses (engine/DSP,
flow layer, hosts/transfer/tests) before planning; revision 1's claims that did
not survive that review are marked in place rather than deleted, so the next
reader can see what was already tried.

## 1. The problem

There is no control anywhere in FireFlow that slows the modulation layer down.
Three separate controls look like they should, and none of them does the job.

**TEMPO is inert in the free world.** `SuperModulator::_update_rate` reads

```cpp
_base_hz = _synced ? division_hz(division_index(_rate_norm), _bpm)
                   : free_hz(_rate_norm);
```

(`engine/mod/super_modulator.cpp:28-29`). BPM does not appear in the free
branch at all. SYNC in turn is not its own switch any more — it is the right
half of COUPLE, `inst.set_sync(coupleKnob >= kCoupleZoneSplit)`
(`host/vcv/src/Fireflow.cpp:880-881`). With COUPLE left of centre, turning
TEMPO does nothing whatsoever.

**TIDE never reaches the melodic lane.** `_apply_rate` gives the PITCH lane
`_pitch_scale` and only the four texture lanes `_mod_scale * _tide_mult`
(`super_modulator.cpp:44-46`). TIDE is a *ratio* between the texture lanes and
the melody, not a speed.

**RATE is the only real speed control and it is unusable at the bottom.**
`free_hz(n) = 0.02 * 1500^n` (`engine/mod/divisions.h:57-60`). The floor is
0.02 Hz — a 50 second cycle, so minutes are not reachable at all — and the
curve is steep: 30% of knob travel is already a 5.5 s cycle. The whole drone
territory is squeezed into the bottom fifth of one knob, per deck.

**Under Glow there is no speed control at all.** `P_RATE_A/B` and `P_TIDE` are
base rules only (`engine/flow/taste.h:965-966, 980`); no story owns either.
M_MOTION gave TIDE up on 2026-08-12 precisely so speed would travel with a
transferred patch. So how fast a woken pad modulates is pure terrain luck, and
the player cannot do either of the two things they want: make a pad modulate
rhythmically when the terrain supports it, or stretch its modulation into
minutes-long sine waves for a slow drone.

## 2. What PACE is

One new engine control, `Instrument::set_pace(float norm)`, 0..1, where **0.5
is exactly x1 and therefore a bit-identical no-op** — the same property that
makes TIDE 0.5 a no-op, and for the same reason: a global time control must
have a position where it provably does not exist.

The mapping is piecewise so both halves meet at exactly 1.0:

```
norm <= 0.5 :  mult = 32 ^ (2*norm - 1)     ->  1/32 .. 1
norm >  0.5 :  mult =  4 ^ (2*norm - 1)     ->     1 .. 4
```

Asymmetric on purpose. The fast end is already reachable through RATE; the slow
end is what is missing, so the resolution goes there.

| knob | 0.0 | 0.25 | 0.5 | 0.75 | 1.0 |
|---|---|---|---|---|---|
| multiplier | x1/32 | x1/5.7 | x1 | x2 | x4 |
| an 8 s cycle becomes | 4.3 min | 45 s | 8 s | 4 s | 2 s |
| a 50 s cycle becomes | 27 min | 4.7 min | 50 s | 25 s | 12.5 s |

**The mapping lives in `engine/mod/divisions.h`** as `pace_mult(norm)` and
`pace_name(norm)`, next to `free_hz`, `kTideRatios` and `kTideNames`, for the
reason stated at `divisions.h:55-56`: the curve lives in the engine header so
the host tooltip shows exactly what the engine runs. Re-typing it into
`Fireflow.cpp` would need a scraper test to hold the two copies together and
would drift anyway.

### 2.1 The accumulator floor, and why the melodic lane moves to double

**x1/32 does not work on the current accumulator.** `ModLane::_phase` is a
`float` (`engine/mod/lane.h:190`) accumulated per sample at
`engine/mod/lane.cpp:585`:

```cpp
_phase += _phase_inc * (1.f + _ev_rate);
```

`LANE_PITCH` is driven through this path every sample
(`super_modulator.cpp:109`); the four texture lanes use `tick()`, which
advances 96 samples at once and has 96x the headroom. Once `_phase_inc` falls
below half an ulp of the current binade, the addition returns `_phase`
unchanged and the lane stops. Simulated in float32 at 48 kHz over 600 s:

| lane rate | phase inc | result |
|---|---|---|
| 0.02 Hz (RATE 0, today) | 4.17e-07 | runs |
| 0.0025 Hz (x1/8) | 5.21e-08 | runs |
| 0.00125 Hz (x1/16) | 2.60e-08 | **freezes at phase 0.50** |
| 0.000625 Hz (x1/32) | 1.30e-08 | **freezes at phase 0.25** |

The last row is exactly this document's own "a 50 s cycle becomes 27 min". The
melodic lane would crawl to phase 0.25 and stop: no wraps, no `_wrap_events`,
no fires, no sampler cursor, no `_deck_step`, and `Center::_grid_servo` pinned
at maximum correction against a target that keeps moving. Just above the stall
there is a worse band — where `inc` sits between half an ulp and a full ulp,
every add rounds up to one ulp and the lane runs *faster* than commanded by up
to 2x, so PACE stops being calibrated before it stops working.

This is a **latent bug that already exists**: at RATE 0 today the margin is only
a factor of 14, and DRIFT's `_ev_rate` eats up to 20% of it.

**Decision (owner, 2026-08-12): `ModLane::_phase` and `_phase_inc` become
`double`.** The public interface stays float — `phase()`, `phase_eff()`,
`step_at_phase()`, `clock_scale()`, `pitch_step_samples()` all keep their float
return types and cast on read, so `Center` and every observer are untouched.
The cost is two double-precision adds per sample (one per deck's PITCH lane);
the STM32H7 has a double-precision FPU. Doing it uniformly for all five lanes
rather than only the melodic one avoids a melodic/texture divergence inside one
class, and the texture lanes' cost is 1/96 of that.

**This changes the render hash.** Double accumulation produces different low
bits, so `ctrl_identity` and any other byte-identity render gate
(`tests/check_render_hash.cmake`) must be re-baselined in the same commit,
deliberately and with the reason in the commit message.

The alternatives were measured and rejected: shortening the range to x1/8
delivers the wish (6.7 min at RATE 0) but parks 1.75x from a cliff that fails
*silently*, and DRIFT pushes that to 1.4x; clamping the effective lane rate
keeps the knob travel but creates a dead zone whose size depends on the
terrain, which is the exact defect this work exists to remove.

## 3. Where PACE applies

`Instrument::set_tempo_bpm` calls itself "the real single door" for tempo and
fans out to three consumers (`engine/instrument.cpp:111-113`). PACE uses the
same door and reaches two of them.

| consumer | receives | effect |
|---|---|---|
| `_center.set_tempo_bpm` | `bpm * pace` | transport beats accumulate stretched, so the step grid stretches and the grid servo aims at the stretched target instead of fighting it |
| `p.mod().set_pace` | `pace` | `_base_hz *= _pace` in **both** branches of `_update_rate` |
| `p.fx().set_bpm` | `bpm` **raw** | see §3.2 — FLUX is a partial, not a clean, exclusion |

### 3.1 `set_pace` must walk through the door itself

`Instrument::set_pace` **re-runs the same fan-out**; it does not merely store
`_pace` and wait for the next tempo push. Without that:

- Under Glow, `P_TEMPO_BPM` only reaches `apply_param` when its own value moves
  (the change guard at `engine/flow/flow.cpp:607-612`), and `Glow.cpp:1037-1042`
  re-pushes tempo only when a fader is assigned to it. Turning the PACE macro on
  a settled terrain would stretch the lanes and leave the transport unstretched
  — precisely the servo fight this section exists to prevent.
- In the render host, `host/render/main.cpp:65` calls `set_tempo_bpm` once
  before any scenario event, so a `set_pace` event would never reach the
  transport at all.

Fireflow would have self-healed (`Fireflow.cpp:918-924` pushes tempo every
control tick), which is exactly why this defect would have shipped: the host
the change is developed in is the one host that hides it.

### 3.2 The external clock

`Transport::clock_pulse()` is `_beats = std::round(_beats)`
(`engine/center/transport.h:26`), driven once per real beat from
`Fireflow.cpp:931-936`. It encodes "one external pulse == one transport beat",
and a paced transport breaks that invariant outright: at x1/32 `_beats`
advances 0.03125 between pulses and `round()` snaps it back to the same
integer, so **the transport freezes at beat 0** while the lanes keep running.
At x4 every pulse delivers a phase discontinuity of up to half a beat straight
into the hard servo.

The fix follows from what the pulse means. One external pulse is one *real*
beat, which is `pace` paced-beats, so the snap target is the nearest multiple of
`pace`:

```cpp
void clock_pulse() { _beats = std::round(_beats / _pace) * _pace; }
```

At `pace == 1` this is bit-identical to today. `Transport` therefore needs to
know the pace — it is the same value `set_bpm` is already being handed
pre-multiplied, so it is passed explicitly rather than reverse-engineered from
the BPM.

### 3.3 What PACE reaches that revision 1 claimed it did not

Revision 1 asserted a clean FLUX exclusion and "no glide needed because PACE
changes frequencies, not amplitudes". Both were wrong, and the plan must own
the consequences rather than restate the claim.

- **FLUX is desynced, not excluded.** `engine/fx/flux.cpp:57` derives the delay
  time from `division_hz(kFluxRateOffset + slice, _bpm)` on the **raw** BPM, so
  a synced FLUX "1/4" is no longer a quarter of the stretched grid, and
  `FluxRateQuantity` prints a division name that becomes a lie. Separately, its
  THIN pattern compares `_rhy_gap` (measured in samples between PITCH-lane
  onsets, fully paced) against an unpaced `rep` (`flux.cpp:94-104`), so at
  x1/32 the skip count pins at `kMaxSkip`. **Ruling for the plan: FLUX follows
  PACE on the fast half only** (`pace > 1`), where the buffer is not at risk,
  and is clamped at x1 below — and the tooltip states it. The original
  bandwidth argument (`divisions.h:41-49`, the ladder stops at "1/2" because
  bandwidth collapses) only ever argued against *slowing* FLUX.
- **The BBD inherits PACE through `master_hz`.** `part.cpp:441-443` pushes
  `set_cycle(1/master_hz)`, and `bbd_engine.cpp:222` derives its clock window
  from that. At x1/32 the BBD clock window drops to roughly 0.16–5.1 Hz, and a
  PACE turn is a delay-line pitch bend of up to five octaves — the same
  mechanism `host/vcv/README.md:492-500` documents for SIZE. This is a real
  audible gesture, not a bug, but "nothing clicks" is false for a BBD deck and
  the plan must include a listening check at both ends.
- **Synth envelopes rescale with the cycle.** `synth_engine.cpp:275-276`
  derives attack and decay from `_cycle_s`, re-pushed to all voices every
  control tick, so a PACE turn changes the envelope of currently sounding
  notes. That is an amplitude-domain change. It also saturates: `kDecayMaxS` is
  20 s, so below roughly x1/3 the "envelope follows the cycle" law is already
  flat and PACE stops lengthening decays.
- **Every wall-clock constant stays unpaced**, by design but worth naming:
  `kOuTau` (45 s, the DRIFT walk), SMOOTH's slew (`lane.cpp:262-276`), SPOT's
  kick, `_settle_coef`. At x1/32 DRIFT's rate wander cycles dozens of times
  inside one lane cycle. PACE is a lane-rate control, not a "modulation pace"
  control, and the difference is loudest at exactly the settings this design is
  sold on. Whether DRIFT should follow PACE is a listening question, deferred.

Out of scope and unchanged: the reverb's SMEAR/WOBL diffuser LFOs, and Glow's
weather oscillators.

`Transport::set_bpm` drops non-positive and non-finite values
(`transport.h:22`); the lowest reachable product, 40 BPM x 1/32 = 1.25 BPM, is
safe in the double beat accumulator.

## 4. The flow layer: DIRT becomes PACE

### 4.1 Why PACE must not be a story

Story-owned parameters are unreachable from the base overlay **by
construction** — `generate()` applies the overlay by iterating `kBaseRules`,
not `P_COUNT` (`engine/flow/terrain.cpp:437-461`). A macro that *owned* the
pace would throw away a transferred patch's own speed, the same class of bug
887b767 and 3e03e81 just closed one level down.

- **`P_PACE` is a new flow parameter with a base rule**, span `{0.5, 0.5}` for
  all four archetypes. The terrain draws no pace of its own; the row exists so
  the overlay has a destination and the coverage check has no hole.
- **The macro is an offset:** `clamp01(base[P_PACE] + (eff[M_PACE] - 0.5))`.
- **`M_DIRT` is renamed `M_PACE`**, keeping slot index 3.
- **NEW does not change the speed** (but see §4.4 for what NEW *does* do).

Note the offset is not constant in log units across the knob, because the
mapping is `32^` below centre and `4^` above: the same +0.1 of knob travel is
x2.0 on the slow half and x1.44 on the fast half, and on a pad carrying a
transferred `base[P_PACE]` the macro's sensitivity depends on where that base
sits. This is a real property of the performance surface, stated rather than
hidden; it is the price of the asymmetric range chosen in §2.

### 4.2 Where the offset is applied — the one safe window

The formula is not the risk; the insertion point is. `Flow::begin_blend`
computes `_resid[p] = _cont_now[p] - _cand_cur[p]` (`flow.cpp:147-149`), and
the entire veto-safety argument at `flow.cpp:565-576` rests on that residual
being exactly zero on a fresh press from a settled instrument.

- `_cand_cur[p]` is set at `flow.cpp:447` from raw `eval_terrain` output.
- `_cont_now[p]` is set at `flow.cpp:489`.

If the offset lands **between those two lines**, `_resid[P_PACE]` becomes
`eff - 0.5`, up to ±0.5, *on every press including a fresh one*. Concrete
failure: knob at 0.75, settled push 0.75; press NEW and the pushed value is 1.0
at `ph = 0`, decaying back to 0.75 over six seconds — a x4 → x2 tempo slide
nobody asked for, directly contradicting "NEW does not change the speed".

**The offset is therefore applied in the guard chain after `flow.cpp:489`**,
beside the BODY FILT floor and the BBD RANGE cap, and **before** the veto loop
(`:577`) and the change guard (`:607`) so `param_now()` publishes the value the
engine actually gets. Applying it inside `eval_terrain` would also be safe
(both quantities would carry it) but violates that function's stated contract
at `flow.cpp:305-313`.

### 4.3 The first macro without a story

M_PACE has no `StoryVariant`. Its whole effect is a runtime role — not
unprecedented, since M_MOTION already scales the entire weather offset
(`flow.cpp:432-433`) — but it is the first macro whose effect is *only* that.

`terrain.cpp:531` calls `pick_index(r, n_var)`, which returns `n - 1 == -1` for
`n_var == 0` (`terrain.cpp:84-87`). Stage 4 must guard `n_var == 0` **before**
that call and set both:

```cpp
mm.story = -1;
t.window[m] = {0.f, 1.f};      // the identity, not the {0,0} value-init
```

`engine/flow/terrain.h:94-102` warns in so many words that `window` has no
default member initialiser and that `{0,0}` instead of the `{0,1}` identity is
"currently unreachable" *because* every macro has a story — and asks to be
re-read if that invariant ever moves. This design moves it, so that comment is
rewritten in the same commit.

### 4.4 Weather, and what NEW on a marked PACE knob does

`weather_of` already skips M_MOTION (`flow.cpp:291-296`). **M_PACE gets the
same exclusion**, for a different reason: the weather offset is up to ±0.10 in
knob units, and in this mapping 0.10 is a factor of **x2**. A tempo wandering by
a factor of two makes every other macro's motion unreadable.

The exclusion is also load-bearing for two gates, not only for taste:
`test_flow_transfer_diff.cpp:79` sets every macro to 0.5, and without the
exclusion `eff[M_PACE]` would drift off 0.5 and break the "every carried value
survives to the engine" gate intermittently; the same is true of §2's
bit-identical-no-op claim, which depends on `eff[M_PACE]` being exactly 0.5.
The two decisions are not independent.

**`new_partial(1u << M_PACE)` still fires and is not a no-op.** The weather
counter is the *sum* of all six macro counters (`terrain.h:47-51`), so
rerolling a story-less PACE redraws the entire weather layer — which drives the
other five macros — plus a 6 s blend and a duck schedule. This is reachable by
accident: `gesture.h:76-82` marks a macro after 0.01 of knob travel during a
hold, and `Glow.cpp:971` fires `new_partial(uiMask)` on release, so nudging
PACE while holding NEW rerolls everyone else's weather. The plan must rule:
either M_PACE is excluded from the mark mask, or the behaviour is documented as
intended. It cannot be left to fall out.

### 4.5 The four orphaned parameters

`P_GRIT_A`, `P_GRIT_B`, `P_COMP_A` and `P_DRIVE` lose their owner and become
base rules with spans taken from the calm end of the story curve they leave.

- They now **travel with a transferred patch**. `P_COMP_A` needs *two* bridge
  edits, not one: the "NO DESTINATION … story-owned, not a base rule" note at
  `flow_patch_bridge.hpp:465-468` is deleted **and** replaced by a `set_base`
  plus the same veto-band note `P_COMP_B` already carries at `:449-458` —
  `kVetos` confines `P_COMP_A` to 0.40..0.60 (`taste.h:643`), and
  `test_flow_transfer_diff.cpp:105-140` requires a `"REWRITTEN AT RUNTIME"` tag
  on any carried value the runtime moves. Omit it and that gate fails on every
  master.
- `P_GRIT_A/B` stay **inaudible under Glow**, exactly as `P_FLUXMIX_A/B` is:
  nothing in `engine/flow/` or `Glow.cpp` calls `set_fx_on`, `SoftSwitch` boots
  off, and `Grit::process` returns bit-exact dry (`engine/fx/grit.cpp:94-95`).
  Their map rows carry the note the FLUXMIX row already does. **Fixing that gap
  is deliberately not part of this work** — it hits FLUX identically and is
  entangled with the GRIT wet path's -9.3 dB attenuation, so it is one sound
  question to solve and audition in one piece.
- The bridge's hand-counted summary prose (`flow_patch_bridge.hpp:541-559`:
  "63 parameters and 42 base rules, so 21 are story-owned … the 17 remaining …
  GRIT … plus DRIFT, DRIVE") becomes false in four places. It is user-facing
  text pulled from the clipboard report and no test checks its arithmetic, so it
  will rot silently unless the plan names it. Same for `Glow.cpp:89`'s "a 38-row
  overlay", which is already wrong today.

**Revision 1 claimed "every existing terrain code renders identically". That is
false**, and not because of the append (§4.6). Moving `P_COMP_A` from DIRT's
story `bp0` — drawn off `kStreamMacroBase + M_DIRT` under
`t.adventure[M_DIRT]` — to `kStreamParamBase + P_COMP_A` under
`t.adventure_base` (`terrain.cpp:382-384`) is a different stream and a
different adventure level, so its value changes for every existing code.
`P_GRIT_A/B` and `P_DRIVE` survive only because their `bp0` spans are the
degenerate `{0, 0}` (`taste.h:901-902, 911`); if "the calm end" is read wider
than bp0, they change too.

`distance()` also shifts: it divides by `P_COUNT` (`terrain.cpp:683-691`), and
`P_PACE` contributes exactly 0 to the sum while the denominator goes 63 → 64,
so every base-patch distance shrinks by a flat 1.6%. Behaviourally negligible,
but the 90-line measurement block at `terrain.cpp:594-682` states `P_COUNT 63`
and derives min/mean/max from it. Re-measure it or it becomes exactly the kind
of stale comment it was written twice to correct.

### 4.6 Where P_PACE goes in the parameter table

`P_MODE` carries a "MUST STAY LAST" note for two reasons, and only one survives
contact with a new parameter.

Load-bearing: base draws are keyed `kStreamParamBase + param`
(`terrain.cpp:382`; the line number in `flow_params.h:107`'s own comment is
stale), so inserting a parameter *before* `P_MODE` re-seeds `P_MODE`'s stream
and re-resolves the FLOW/STEP draw of every existing terrain code.

Positional, not structural: `flow.cpp:526-560` argues `_mode_now` is safe to
read during the `P_RANGE_A/B` iteration because `P_MODE` is last and unpushed.

So **`P_PACE` is appended after `P_MODE`**, and the positional argument becomes
an explicit `static_assert(P_RANGE_A < P_MODE && P_RANGE_B < P_MODE)` beside
the two that already guard ENGINE ordering (`flow.cpp:53-56`). Verified during
review: `kStreamParamBase` is 0 with a 1000-wide block
(`engine/flow/flow_rng.h:11-12`), so id 63 collides with nothing;
`push_mode_and_steps` reads `_pushed[]` after the loop and is position-free;
`encode_base` is name-keyed `NAME:value;` pairs (`flow_patch_bridge.hpp:768-788`)
and the terrain code holds only master plus counters, so **no saved format
changes length or layout**, and old clipboard strings and `pool.tsv` rows stay
valid.

`apply_param` gains `case P_PACE: in.set_pace(v); break;`.

## 5. Zero is not neutral — the init trap

PACE's safety argument is "0.5 is provably a no-op". This codebase's safety
convention is value-initialisation to **zero**, and under this mapping zero is
**x1/32**, the extreme of the range. That collision is survivable for TIDE
(zero means x1/4, a quarter off); for PACE the symptom is a pad whose
modulation has a 27-minute cycle, which a user files as "Glow is broken".

Every carrier must be set to 0.5 explicitly, and each is a separate edit:

| carrier | site | consequence if missed |
|---|---|---|
| `FireflowPatch::p[]` | `flow_patch_bridge.hpp:158-161` (`= {}`) | `test_flow_transfer_diff.cpp:47` and 19 `FireflowPatch fp{}` sites in `test_flow_patch_bridge.cpp` carry x1/32 into every transfer |
| `INIT_DEFAULTS["PACE"]` | `src/init_patch.hpp`, required for every `PARAMS` entry by `res/test_panel.py:2619-2622` | the approved `FF_hw_Init.vcvm` instrument reboots at a different speed — the `fireflow-control-merge-init-trap` shape exactly |
| `Instrument::_pace` | new member | the `ctrl_identity` render gate only survives if this is exactly `1.0f` |
| `bench/audition/init_patch.cpp` | mirrors the host push (`:33-40, 187-188`) | the audition firmware boots at a different pace than VCV, and **no test catches it** — `test_panel.py`'s scraper guards only three named calls (`:2986-2993`) |
| an older `.vcv` with no PACE id | Rack fills from the configured default | old patches transfer at x1/32 unless `INIT_DEFAULTS` is right |

This is the single most likely way this feature ships broken, so it gets its own
section rather than a bullet.

## 6. The two host surfaces

**Fireflow.** A `PACE` `SMKNOB` in the free `ROW_TIME1` slot beside TEMPO.
Verified during review: the slot at `(CX-9.0, 42.0)` is genuinely free
(`gen_panel.py:438`), the nearest neighbours are 18 mm and 12 mm away against a
6.0 mm glyph, the label lands clear at `y = 47.6` mirroring the existing
TEMPO/SHUFFLE pair, it sits inside the TIMING box, and "PACE" collides with no
printed word. The parameter is appended at the end of `PARAMS`.

Two things move even so: `tests/test_seed_audition_init.cpp:74` asserts
`NUM_PARAMS == 68` and becomes 69, and `bench/audition/init_patch.cpp` needs
`inst.set_pace(...)` (§5).

**OPEN — the hardware panel needs a physical slot, and the generator will not
run without one.** `gen_hw_panel.py:166` builds `HW_PARAMS` from
`gp.RUNTIME_PANEL_PARAMS` and `place()` ends in `raise KeyError(f"no hw slot
for {base}")` (`:164`). `CENTER_POS` has no `PACE` key, and the `Y_B1K` row
already carries five knobs at 12.5 mm pitch. Until a position is chosen,
`res/test_hw_panel.py` fails at *import* and takes the `hw_panel_guard` ctest
with it, and neither `generated_hw_panel.hpp` nor `FireflowHW.svg` can be
regenerated. The new position must also clear
`test_hw_slot_map_matches_the_reduced_inventory` (`:200-209`) and
`test_labels_stay_off_neighbour_footprints` (`:214-229`, `LBL_MARGIN = 1.5`).
**This is the one decision this design does not make.** It is a 68 HP layout
question for the owner, and it belongs with the open items in
`hw-panel-regroup-open-decisions`.

**Glow.** The rename touches more than revision 1 listed: `gen_flow_panel.py`
(`_MACRO_NAMES` at `:233` and the tip text at `:238`), `Glow.cpp:46` and `:74`,
`src/generated_flow_panel.hpp:21` and `:74` (both byte-gated by
`test_flow_panel.py:385`), `test_flow_panel.py:40, 48, 79, 84`,
`engine/flow/flow_ids.h:5`, `engine/flow/taste.h:896, 900, 1032`,
`host/vcv/README.md:528, 541` (ungated), and six test files
(`test_flow_gesture.cpp:39, 110`, `test_flow_new.cpp:69, 70, 483, 488`,
`test_flow_taste.cpp:48`, `test_flow_veto.cpp:107`). `res/Glow.svg` needs no
edit — the macro knobs print no caption — and a saved Glow patch cannot break,
because Rack persists params by numeric id and the slot index is unchanged.

**Tooltips that would otherwise lie:** the RATE tooltip must multiply PACE in;
`FluxRateQuantity` must state the §3.2 clamp; PACE's own tooltip reads
`pace_name(norm)`.

## 7. Gates

Every gate below is a doctest gate. The render host can *demonstrate* PACE but
can gate almost none of this: `flow_wake` takes a terrain code only and has no
overlay verb (`host/render/scenario.cpp:238-251`), and the CSV logs lane
outputs and `phase_err` but neither lane Hz nor BPM. The render host still needs
`else if (a == "set_pace") inst.set_pace(e.value);` in `scenario.cpp`'s setter
chain — note that unknown actions there are **silently ignored** (`:218`), so a
scenario written before the verb lands is a no-op, not an error.

| gate | what it holds | observer |
|---|---|---|
| centre is neutral | `set_pace(0.5)` leaves every lane's rate **exactly** unchanged | `lane_rate_hz_for_test` ✔ |
| endpoints | 0 → x1/32, 0.5 → x1.0, 1 → x4 | `pace_mult()` reader — **new** |
| **phase actually advances** | at every PACE value, **wraps per unit time** match the commanded rate to within a few percent, on the melodic lane | needs a wrap counter — **new** |
| free world | lane Hz scales with `set_synced(false)` | `lane_rate_hz_for_test` ✔ |
| grid world | lane Hz **and** transport BPM scale, and the servo's steady-state correction is ≈ 0 | `phase_err()` ✔; **transport BPM observer is new** — `_center` is private and there is no `Instrument::bpm()` |
| external clock | with pulses at a fixed real-beat interval, transport beats advance `pace` per pulse at x1/32 and x4 | as above |
| FLUX | delay time unchanged below x1, scaled above | `flux_delay_target_for_test` ✔ |
| transfer | a carried patch plays its own pace at knob centre | `param_now`, `is_base_rule` ✔ — and `test_flow_overlay.cpp:38-72` covers `P_PACE` automatically, since it iterates `kBaseRuleCount` |
| coverage | `P_PACE` has exactly one owner; exactly one named story-less macro is permitted | `test_flow_taste.cpp` ✔ |

**The wraps-per-time gate is the important one.** Every other gate measures the
*commanded* rate, which stays perfectly correct while the lane is frozen — that
is exactly why revision 1's gate list would have shipped §2.1's stall green.

### 7.1 Tests that go red and must be updated deliberately

Six, not two. Each carries a comment asking to be updated rather than deleted:

1. `test_flow_overlay.cpp:34-36` — `kBaseRuleCount == 42` becomes 47, and
   `CHECK_FALSE(is_base_rule(P_COMP_A))` inverts. The comment at `:24-33`
   requires both numbers to move together and to be named in the commit.
2. `test_flow_terrain.cpp:23` — `mm.n_targets >= 1`.
3. `test_flow_terrain.cpp:39` — `span_max >= kMinSpan`, the "no dead knob" gate
   from spec 7.1. Widening it needs the same "exactly one named story-less
   macro" discipline as the taste gate, or that guarantee quietly dies for all
   six macros.
4. `test_flow_taste.cpp:26-28` — `per_macro[m] >= 1`.
5. `test_flow_new.cpp:393` — `tested_macros >= MACRO_COUNT - 1`. The one
   permitted non-contributor is already spent on SPACE.
6. `test_seed_audition_init.cpp:74` — `NUM_PARAMS == 68` becomes 69.

`test_flow_veto.cpp:232-233` also needs a note: it filters terrains on the max
over all seven adventure levels, and `t.adventure[M_PACE]` is still drawn
(correctly, its own stream) but now never used, so a dead level can qualify a
terrain as "high adventure" and weaken that sample by roughly 1/7 without the
number moving.

### 7.2 The veto proof must be re-homed BEFORE the DIRT story is deleted

`tests/test_flow_veto.cpp:196-206` is the suite's only red-capable proof that
the runtime veto clamp at `flow.cpp:577-582` fires at all, as distinct from the
ordinary `clamp_to(kParams, …)` every parameter already gets. Its own comment
records the measurement: `P_COMP_A` is the **sole** parameter this sweep drives
past an interior bound, and it says why — "COMP_A alone gets a story curve with
more range than COMP_B's near-constant base rule".

§4.5 replaces that curve with a near-constant base rule. Afterwards the only
driver is `_resid`, bounded by the span width, and the gate collapses to a coin
flip at best and a vacuous green at worst. The failure mode is social, not
technical: an implementer sees a red `interior_hit`, reads §4.5 as licence, and
weakens it to `edge_hits > 0` — which that same comment already explains is
*not* evidence of the veto clamp. The by-ear COMP ceiling (an owner ruling,
`taste.h:631-643`) and the REV_MOD tail veto would then be protected by a gate
that cannot go red, which is what `fireflow-vacuous-test-gates` and "a test that
cannot go red gets fixed" exist to prevent.

**The plan must, as a task ordered before the DIRT story is removed:** pick the
parameter that takes over the proof — `P_REVMIX_A`'s 0.08 interior bound, driven
by both BRIGHT and SPACE, is the only remaining candidate — and *measure* that
the sweep can actually drive it past that bound. If it cannot, the sweep changes,
not the assertion.

### 7.3 RED proofs

- **The grid gate.** Remove the `_pace` factor from the synced branch only; the
  gate must redden. It only does so at a PACE outside **[0.438, 0.608]**: the
  servo's authority is `kLockCap = 0.35` (`center.cpp:45`), so it fully absorbs
  any mismatch in `pace ∈ [0.65, 1.35]` and settles at a small constant error.
  The gate must therefore assert the steady-state correction is ≈ 0, not merely
  that the phase error is *bounded* — the error is bounded even while the servo
  is fighting, because the correction is capped.
- **The transfer gate.** Remove the `P_PACE` row from the overlay path; it must
  redden.
- **The wraps gate.** Revert `_phase` to `float`; the x1/32 case must redden.

## 8. Deliberately not in this design

- Any glide or smoothing on PACE.
- A weather target for PACE (§4.4), and whether DRIFT should follow PACE (§3.3).
- Regrouping the TIMING box.
- The `set_fx_on` gap that makes GRIT and FLUX inaudible under Glow (§4.5).
- Changing TIDE. It keeps its position and its meaning; PACE only makes that
  meaning nameable, since TIDE is a ratio and PACE is the speed.

## 9. Corrections to revision 1

Kept here so the next reader does not re-derive them.

- "x1/32 … a 50 s cycle becomes 27 min" — false on a float accumulator; the
  lane freezes (§2.1).
- "FLUX stays in real time. The one deliberate exclusion." — false; it
  desyncs from the stretched grid and its THIN pattern breaks (§3.2, §3.3).
- "PACE changes frequencies, not amplitudes … nothing to click" — false for
  synth envelopes and for a BBD deck (§3.3).
- "PACE uses the same door" — true but insufficient; `set_pace` must re-enter
  the fan-out itself or Glow and the render host desync (§3.1).
- "every existing terrain code renders identically" — false, via `P_COMP_A`'s
  stream change, not via the append (§4.5).
- Two gates listed as needing updates; it is six, plus a veto proof that has to
  be re-homed first (§7.1, §7.2).
- `terrain.cpp:160` cited for the stream key; the real site is
  `terrain.cpp:382`, and `flow_params.h:107`'s own comment is stale.
