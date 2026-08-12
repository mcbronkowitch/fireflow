# PACE — one global modulation time-stretch

Date: 2026-08-12
Status: design, approved by the owner. Not yet planned, not yet implemented.

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
the melody, not a speed. Its own range is x1/4..x4 (`tide_free`,
`engine/mod/divisions.h:78-80`).

**RATE is the only real speed control and it is unusable at the bottom.**
`free_hz(n) = 0.02 * 1500^n` (`divisions.h:57-60`). The floor is 0.02 Hz — a
50 second cycle, so minutes are not reachable at all — and the curve is steep:
30% of knob travel is already a 5.5 s cycle. The entire drone territory is
squeezed into the bottom fifth of one knob, per deck.

**Under Glow there is no speed control at all.** `P_RATE_A/B` and `P_TIDE` are
base rules only (`engine/flow/taste.h:965-966, 980`); no story owns either.
M_MOTION gave TIDE up on 2026-08-12 precisely so that speed would travel with a
transferred patch. The consequence is that how fast a woken pad modulates is
pure terrain luck, and the player cannot answer either of the two things they
actually want to do: make a pad modulate rhythmically when the terrain supports
it, or stretch its modulation into minutes-long sine waves for a slow drone.

## 2. What PACE is

One new engine control, `Instrument::set_pace(float norm)`, 0..1, where **0.5
is exactly x1 and therefore a bit-identical no-op** — the same property that
makes TIDE 0.5 a no-op, and for the same reason: a global time control must
have a position where it provably does not exist.

The mapping is piecewise so that both halves meet at exactly 1.0:

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

## 3. Where it applies

`Instrument::set_tempo_bpm` calls itself "the real single door" for tempo and
fans out to three consumers (`engine/instrument.cpp:111-113`). PACE uses the
same door and reaches two of them.

| consumer | receives | effect |
|---|---|---|
| `_center.set_tempo_bpm` | `bpm * pace` | transport beats accumulate stretched, so the step grid stretches and the grid servo aims at the stretched target instead of fighting it |
| `p.mod().set_pace` | `pace` | `_base_hz *= _pace` in **both** branches of `_update_rate` — synced via `division_hz`, free via `free_hz` |
| `p.fx().set_bpm` | `bpm` **raw** | FLUX stays in real time. The one deliberate exclusion |

Applying it in both places rather than one is what keeps the three clock
consumers coherent:

- **Synced lanes vs. transport.** `Center::_grid_servo` targets
  `transport.beats() * cpb * clock_scale` (`engine/center/center.cpp:167-173`).
  If only the lane rate scaled, the servo would pull the lanes back onto the
  unstretched grid and win — its hard-lock gain is deliberately strong enough to
  outrun EVOLVE's rate wander. Scaling the transport too keeps the ratio intact.
- **Free-world grid gravity.** `center.cpp:208-209` computes
  `nearest_division(geo, _transport.bpm())` where `geo` derives from
  `SuperModulator::base_hz()`. Both sides now carry the same factor, so it
  cancels and COUPLE's gravity latches onto the same rung it does today.
- **FLUX.** Excluded because it is an audio effect with a finite buffer, not a
  modulator. Its rate ladder already stops at "1/2" because bandwidth collapses
  below that (`divisions.h:41-49`); x1/32 has no buffer to live in.

Also out of scope, decided explicitly: the reverb's SMEAR/WOBL diffuser LFOs
(reverb character, and x4 is where the tail comes apart — there is already a
veto at WOBL 0.25), and Glow's weather oscillators (already 5–20 minutes; x1/32
would make them 10 hours, which is indistinguishable from off).

**No glide.** PACE changes frequencies, not amplitudes. Lane phase stays
continuous across a change and so do transport beats, so there is nothing to
click. Whether a jump is *musically* too abrupt is a listening question for
later, not a correctness one.

`Transport::set_bpm` drops non-positive and non-finite values
(`engine/center/transport.h:22`). The lowest reachable product is 40 BPM x 1/32
= 1.25 BPM, which is positive, finite, and safe in the double beat accumulator.

## 4. The flow layer: DIRT becomes PACE

### 4.1 Why PACE must not be a story

Story-owned parameters are unreachable from the base overlay **by
construction** — `generate()` applies the overlay by iterating `kBaseRules`,
not `P_COUNT`, and stage 4 would erase a story-owned entry anyway
(`engine/flow/terrain.cpp:437-461`). A macro that *owned* the pace would
therefore throw away a transferred patch's own speed, which is exactly the
class of bug 887b767 and 3e03e81 just closed one level down (TIDE, COLOR).

So:

- **`P_PACE` is a new flow parameter with a base rule**, span `{0.5, 0.5}` for
  all four archetypes. The terrain draws no pace of its own. The row exists so
  the overlay has a destination and the coverage check has no hole.
- **The macro is an offset, not a replacement.** The pushed value is
  `clamp01(base[P_PACE] + (eff[M_PACE] - 0.5))`. At knob centre a transferred
  patch plays at exactly its own speed; turning the knob shifts it in the same
  log units in both hosts.
- **`M_DIRT` is renamed `M_PACE`**, keeping slot index 3, so no macro enum is
  renumbered and Glow's panel positions do not move.
- **NEW no longer changes the speed.** Pressing NEW keeps whatever pace is set,
  which is what a performance control should do.

### 4.2 The first macro without a story

M_PACE has no `StoryVariant`. Its whole effect is a runtime role, which is not
unprecedented — M_MOTION already carries one on top of its story (it scales the
whole weather offset, `engine/flow/flow.cpp:432-433`) — but it is the first
macro whose effect is *only* that. Three things follow:

1. `tests/test_flow_taste.cpp:26-28` asserts `per_macro[m] >= 1` for every
   macro. That gate must be deliberately widened to permit exactly one
   story-less macro, named, rather than loosened to `>= 0` for all six.
2. `terrain.cpp:531` calls `pick_index(r, n_var)`. With `n_var == 0`,
   `pick_index` returns `n - 1 == -1` (`terrain.cpp:84-87`). No variant loop
   body runs, so nothing reads the -1 — but `t.window[m]` stays `{0, 0}` from
   value-initialisation and `mm.story` stays `0`, silently naming story index
   0 as this macro's story. Guard `n_var == 0` explicitly and set
   `mm.story = -1`; do not leave it to the value-init.
3. The guard goes *before* the `pick_index` call, so the draw consumes no RNG
   value it no longer needs. Nothing downstream depends on this — the macro
   streams are per-macro (`kStreamMacroBase + m`) and a story-less macro has no
   later draw of its own — but a stream position that means nothing is a trap
   for the next reader, not a saving.

### 4.3 Weather does not reach PACE

`weather_of` already skips M_MOTION, because letting the weather modulate its
own depth is a feedback loop (`flow.cpp:291-295`). M_PACE gets the same
exclusion, for a different reason: the weather offset is up to +/-0.10 in knob
units, and in this mapping 0.10 is a factor of **x2**. A tempo that wanders by
a factor of two makes every other macro's motion unreadable, because the pace
is the frame the ear judges them against.

A gentler weather-on-PACE (a fraction of the normal depth) is a good idea for a
later listening loop. It is not in this design.

### 4.4 The four orphaned parameters

`M_DIRT`'s targets — `P_GRIT_A`, `P_GRIT_B`, `P_COMP_A`, `P_DRIVE` — lose their
owner. `test_flow_taste.cpp:62-70` requires every ParamId to be covered by
exactly one of a story or a base rule, so all four become base rules, with
spans taken from the calm end of the story curve they are leaving.

Two consequences worth stating rather than discovering:

- They now **travel with a transferred patch**, which none of them does today.
  `docs/flow-fireflow-param-map.md` gains five rows (these four plus `P_PACE`).
- `P_GRIT_A/B` remain **inaudible under Glow**, exactly as `P_FLUXMIX_A/B` is,
  and for the same reason: nothing in `engine/flow/` or `Glow.cpp` ever calls
  `set_fx_on`, and `SoftSwitch` boots off, so `Grit::process` returns bit-exact
  dry (`engine/fx/grit.cpp:94-95`). Their map rows carry the same note the
  FLUXMIX row already does. **Fixing that gap is deliberately not part of this
  work** — it affects FLUX identically and is entangled with the GRIT wet
  path's -9.3 dB attenuation at the modulation lane's default intensity, so it
  is one sound question to be solved and auditioned in one piece.

Nothing about DIRT gets worse in the meantime. Measured before this design:
of DIRT's four targets, the two GRIT rows already reach no audio at all, and
what remained was `P_COMP_A` moving 0.13 on deck A only (about +4 dB of
auto-makeup, `engine/fx/comp.cpp:58`) plus `P_DRIVE` staying flat at 0 until
75% of the knob and then reaching the 0.40 veto ceiling, which
`engine/fx/limiter.h:37` itself calls "gentle". The knob was a loudness
control, which is what its own veto comment warned against
(`taste.h:629-630`, "it only gets louder").

### 4.5 Where P_PACE goes in the parameter table

`P_MODE` carries a "MUST STAY LAST" note for two reasons, and only one of them
survives contact with a new parameter.

The load-bearing reason: base draws are keyed `kStreamParamBase + param`
(`terrain.cpp:160`), so inserting a parameter *before*
`P_MODE` re-seeds `P_MODE`'s stream and re-resolves the FLOW/STEP draw of every
existing terrain code.

The second reason is positional, not structural: `flow.cpp:527-545` argues that
`_mode_now` is safe to read during the `P_RANGE_A/B` iteration because `P_MODE`
is last in the table and has not been pushed yet this tick.

Therefore **`P_PACE` is appended after `P_MODE`**, and the positional argument
is converted into an explicit `static_assert(P_RANGE_A < P_MODE && P_RANGE_B <
P_MODE)` alongside the two that already guard the ENGINE ordering
(`flow.cpp:53-56`). No stream is re-seeded, every existing terrain code renders
identically, and the ordering dependency becomes checkable instead of implied.

`apply_param` gains `case P_PACE: in.set_pace(v); break;`.

## 5. The two host surfaces

**Fireflow.** A `PACE` `SMKNOB` in the free `ROW_TIME1` slot beside TEMPO —
`host/vcv/res/gen_panel.py:438` records that slot as deliberately empty since
the SYNC switch folded into COUPLE. The TIMING box is where a time control
belongs. The parameter is appended at the end of `PARAMS` so no existing id
shifts.

`divisions.h:55-56` states that the free-mode rate curve lives there so "the
VCV tooltip shows exactly the Hz the engine runs". The RATE tooltip must
therefore multiply the PACE factor in, or it starts lying on the first turn.
PACE's own tooltip shows the multiplier ("x1/5.7").

**Glow.** The label changes from DIRT to PACE in three places that must stay in
step: the generator `host/vcv/res/gen_flow_panel.py`, the name table at
`host/vcv/src/Glow.cpp:74`, and the `static_assert` at `Glow.cpp:46`.
`host/vcv/res/test_flow_panel.py:78-79` guards the macro order and moves with
them.

## 6. Gates

| gate | what it holds |
|---|---|
| centre is neutral | `set_pace(0.5)` leaves every lane's `rate_hz_for_test()` **exactly** unchanged — no epsilon. This is why the mapping is piecewise |
| endpoints | 0 -> x1/32, 0.5 -> x1.0, 1 -> x4 |
| free world | with `set_synced(false)`, lane Hz scales by the factor |
| grid world | with `set_synced(true)`, lane Hz **and** transport BPM scale, and the grid servo's phase error stays bounded across the change. This is the gate that proves the servo is not fighting the stretch |
| FLUX exclusion | the FLUX delay time does not move at any PACE value |
| transfer | `base[P_PACE]` is reachable from the overlay (extends `tests/test_flow_overlay.cpp`), and at knob centre a carried patch plays its own pace |
| coverage | `P_PACE` has exactly one owner; `test_flow_taste.cpp` permits exactly one named story-less macro |

Two RED proofs belong in the plan, or the gates are hollow (see
`fireflow-vacuous-test-gates` and the project rule that a test which cannot go
red gets fixed):

- Remove the `_pace` factor from the synced branch only. The grid-world gate
  must redden — if it does not, it is measuring the lane against itself.
- Remove the `P_PACE` row from the overlay path. The transfer gate must redden.

## 7. Deliberately not in this design

- Any glide or smoothing on PACE.
- A weather target for PACE.
- Regrouping the TIMING box now that it has two knobs on the top row again.
- The `set_fx_on` gap that makes GRIT and FLUX inaudible under Glow (§4.4).
- Changing TIDE. It keeps its panel position and its meaning; PACE simply makes
  it possible to name that meaning honestly, since TIDE is a ratio and PACE is
  the speed.
