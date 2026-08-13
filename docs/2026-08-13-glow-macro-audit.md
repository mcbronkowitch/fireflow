# Glow macro audit — what each knob actually moves

Measured 2026-08-13. The trigger: the DIRT macro had been found dead by reading
(its GRIT targets set the wet/dry of a block nothing ever switches on) and
replaced by PACE. The question was whether the other five macros carry the same
defect. They do — two of them worse than DIRT did.

This document exists so the FLOW melody engine, the SHAPE/SMOOTH rework and the
Glow rework do not have to re-derive any of it. Everything below is measured, not
inferred; where a mechanism is stated but not proven, it says so.

## Method

Two `Instrument` + `Flow` pairs are woken on the same terrain seed and rendered
sample-for-sample in lockstep, differing in exactly one thing — one knob
position, or one parameter value. The figure reported is

```
rel_diff = sqrt( sum (a-b)^2 / sum a^2 )
```

over the render window, i.e. the energy of the difference relative to the energy
of the reference. **0.000000 means the two renders are bit-identical**: the knob
moved no audio at all. Sampler decks are filtered out before rendering (the rig
provides no sample buffers, so a Sampler deck would run silent for a reason that
has nothing to do with the subject).

Two limits of the metric, stated because they matter for reading the table:

- It is energy-relative, so it **systematically under-weights reverb tail
  character**. A 5 % whole-mix figure can be plainly audible inside the wet
  signal. As a ranking it is sound; as a verdict on a reverb parameter it is not.
- Where one end of a sweep nearly mutes the instrument (FILT at −1, RATE at its
  floor), the ratio blows up and the magnitude is meaningless. Those entries
  should be read as "large", not as their number.

The rig lives in `tests/test_param_impact.cpp`.

**One correction to how the numbers in §5 were taken.** That table was measured
from `Terrain::base[]`, and for a story-owned parameter stage 4 writes
`base[p] = bp[0]`, the curve's calm floor — so it rendered the corner where all
six macros sit at zero at once. `FILT_A/B` is held near −0.5 there on every
terrain and both decks are close to silence. The ranking survives (every row was
taken at the same point) but the absolute figures are pessimistic, and the
"dead on some seeds" entries in that table are artifacts of that corner rather
than properties of the parameters. The committed rig no longer does this: it
settles a `Flow` with every macro centred and takes the values it actually
pushes. The findings in §1–§4 and §6 do not depend on the operating point —
each has a mechanism in the source, and a bit-identical render is bit-identical
anywhere.

## Result 1 — ten parameters have no audio path at all

Exactly zero on every terrain, in both modes, over every window tried:

| Parameter | Why |
|---|---|
| `P_GRIT_A/B` | the GRIT block is never switched on — `apply_param` has no `set_fx_on` |
| `P_FLUXMIX_A/B` | same gate, FLUX block |
| `P_LINK_A/B` | a FLUX parameter, behind the same gate |
| `P_FORM_A/B` | the melody pattern reaches audio only above SHAPE 0.75 (see below) |
| `P_SONG_A/B` | same |

That is ten of the 64 parameters the flow layer declares — a sixth of the surface
— drawn per terrain, pushed every tick, inaudible.

## Result 2 — nine setters `apply_param` cannot reach

Diffing Fireflow's setter call sites against `apply_param`'s switch:

```
set_fx_on             the GRIT/FLUX gate; root cause of six of the ten above
set_grit_mode         Drive vs Reduce
set_flux_rate         the echo's division ladder
set_voice_detune      per-deck voice detune; Fireflow has a knob, flow has no ParamId
set_target_base       the modulation matrix
set_target_active     ditto
set_fx_target_base    the five FX modulation destinations
set_excitation_sources BODY excitation routing
set_part_level        per-deck level
```

`set_step` and `set_sync` also appear in that diff but are false positives —
`Flow::push_mode_and_steps` owns them deliberately.

The modulation-matrix defaults are usable (`_active` all true, `_tdepth` =
`{1, .55, 1, .7, 1}`), so this is a missing control surface, not a broken one.
The FX target defaults are not: `_fx_active` is all `false`.

GRIT's intensity and FLUX's feedback and time-mod are a level below — they are
not exposed on `Instrument` at all, so no host can reach them.

## Result 3 — DENSITY is dead in the free mode

Population of 40 terrains, DENSITY swept 0 → 1 with every other macro parked:

```
22 FLOW terrains   bit-identical  22/22
18 STEP terrains   bit-identical   0/18   (rel_diff 1.37 .. 4.06)
```

Still 0.00000000 at a 40 s window, so it is structural rather than slow. All
three of DENSITY's targets are step-grid concepts: `DENSITY_A/B` reach only
`ModLane::_effective_gate`, which is consulted only under `_step_mode`
(`lane.cpp:464`, and `_start_note` behind the same guard); `STEPS_A` feeds
`clock_scale()`, which returns a constant `1.f` in the free mode (`lane.h:77`).

Worse than the DIRT macro it outlived: DIRT at least moved `COMP_A` by 0.13.

## Result 4 — the melody system hangs off SHAPE's top quarter

`P_FORM_A` and `P_SONG_A` moved no audio on **40 of 40** terrains, in both modes,
over 60 s. The flow layer pushes them correctly — `param_now()` shows
`FORM_A 0 → 3`, `SONG_A 0 → 4` — and `Instrument::form()` confirms the value
arrives. It simply never reaches the audio.

`ModLane::_compute_raw` passes the pattern value as `shape_value`'s third
argument (`lane.cpp:422`); `waveforms.h:32` blends that argument in only in the
S&H segment, weight `(shape − 0.75) · 4`. Measured at the bare lane, form 0 vs 3:

```
SHAPE 0.00  0.0        SHAPE 0.90  6251.4
SHAPE 0.50  0.0        SHAPE 1.00 10419.1
SHAPE 0.75  0.0
```

Below 0.75 the melodic lane emits a plain LFO waveform and the generated pattern
is discarded. FORM, SONG, the phrase generator, the song ladder and VARIATION's
pitch mutation all depend on that one blend. `P_SHAPE_A/B` is capped at
`{0, .25}` for drone, so a drone can never reach it; the other archetypes draw
uniformly and land there a quarter of the time.

`sh` is `_shape + _ev_shape + _shape_offset + _kick_shape`, and DRIFT writes
`_shape_offset` every control tick (`center.cpp:143`) — so on a drifting terrain
the melody fades in and out of reach on its own. Forcing `SHAPE_A` to 1.0 made
FORM audible on only 1 of 6 STEP terrains, which means at least one further gate
exists that was **not** traced. Do not treat the SHAPE blend as the complete
explanation.

## Result 4b — CHOKE silenced one deck on most terrains (fixed)

Found while building the committed rig, and the largest single defect of the
audit: **at a centred macro vector, on 45 of 52 terrains one of the two decks
produced no audio at all** — muting it changed the render by exactly 0.0000, and
that held over a 30 s window. On free-mode terrains it was 31 of 31. The
carrier-plus-texture structure was not in operation.

The chain:

1. `instrument.cpp` computes the CHOKE inhibit window as
   `window = _parts[pri].gate() || _parts[pri].flow()`
2. `Part::flow()` is `return !_step_on` — "a FLOW drone is always on"
3. so in the free mode the window never closes, and `_parts[yld].set_inhibit(true)`
   stands for the life of the terrain
4. `Part::_note_suppressed = _inhibit` then swallows every note that deck would
   have started

CHOKE's magnitude is not consulted at either stage — only its sign and
`amt > 0` — so a drawn −0.016 muted a deck exactly as completely as −1.0 would.

**The defect was in the flow layer, not the engine.** CHOKE is a five-state zone
control: Fireflow drives it with `snapEnabled` over −2..+2 scaled by 0.5, i.e.
`{-1, -0.5, 0, +0.5, +1}`, and 0 — neither deck yields — is its centre position.
The base rule drew it *continuously* from `{-.25, .25}`, under a comment that
already called the values "by-ear states". A continuous draw lands on exactly 0
with probability zero, so every terrain drew a choke.

**The fix: the terrain draws no choke at all.** `P_CHOKE` becomes a
single-point base rule — the `P_PACE` shape, and for the same reason. The row
exists so the base overlay has a destination, and `draw_span` returns the centre
exactly when `lo == hi`. CHOKE therefore reaches Glow through a transferred
patch and nowhere else, which is a real path and not a theoretical one:
`flow_patch_bridge.hpp:569` carries it as `P_CHOKE = CHOKE * 0.5`, landing
Fireflow's five snapped by-ear states on flow's −1..1 unrounded, and the overlay
is applied after the base-rule loop so it wins.

Measured after the fix: **both decks audible on 40 of 52 terrains — 27 of 31
free-mode, 13 of 21 stepped** — against 7, 0 and 7 before.

### The version that was built first, and why it went

A zone-snapped draw was implemented, measured and then withdrawn in favour of
the simpler rule above. It is recorded because it looks like the smaller change
and is not:

- **Zone-snapped** (`kChokeZones` / `kChokeW`, `snap_choke`), weighted toward
  the centre, the shape `snap_rate` and `snap_steps` already use. This fixed the
  free mode, but a stepped terrain whose priority deck *sustains* still had its
  window permanently open, so the mute merely became rarer rather than gone.
- **Drawing the sign** put the priority on the texture deck half the time.
  Master 771 — STEP drone, SYNTH carrier on A, BBD texture on B — drew +0.5,
  handed the BBD priority and rendered at RMS 1.2e-5 against the 1e-3 floor of
  the fixed-seed audio gate. Taking the sign from `a_carries` instead fixed that
  case but left the mirror risk: a *quiet carrier* then chokes the audible
  texture deck.
- **Letting the draw reach ±1** opens stage 2 in `instrument.cpp`, where the
  yielding deck is blocked through the priority side's whole audible decay. The
  old continuous band could not reach it either; opening it at a 5 % weight per
  sign is what first broke master 771.

Three guards, each answering the previous one's measurement, and the deck
balance came out identical to simply not drawing the parameter. The rule that
survives is the one that needs no guards: a control whose inhibit is binary at
every stage has no gradient for a generator to sit on, so the generator does not
touch it.

**Not fully closed.** The remaining 12 are a second, untraced cause: they
survive `CHOKE = 0` and all of them carry a BODY deck on the silent side. That
is consistent with the known BODY fragilities and is not the same defect.

## Result 5 — per-parameter impact

Full legal `kParams` range, 3 FLOW + 3 STEP terrains, 6 s window. `dead` counts
bit-identical renders. Entries marked ~ have a near-silent end and their
magnitude is meaningless.

| Parameter | FLOW | dead | STEP | dead |
|---|---|---|---|---|
| `DRIVE` | 3.00 | 0/3 | 2.96 | 0/3 |
| `CHOKE` | 1.33 | 0/3 | 2.29 | 0/3 |
| `TIDE` | 2.04 | 0/3 | 0.92 | 0/3 |
| `SCALE` | 1.50 | 0/3 | 0.75 | 0/3 |
| `DRIFT` | 1.44 | 0/3 | 0.46 | 0/3 |
| `ROOT` | 1.37 | 0/3 | 1.07 | 0/3 |
| `DEPTH_A` | 1.37 | 1/3 | 2.05 | 0/3 |
| `DECAY_A` | 2.16 | 1/3 | 3.35 | 0/3 |
| `SUB_A` | 3.56 | 1/3 | 0.76 | 0/3 |
| `TUNE_A` | 1.66 | 1/3 | 2.10 | 0/3 |
| `RANGE_A` | 1.18 | 1/3 | 1.25 | 0/3 |
| `RES_A` | 1.16 | 1/3 | 42.0 | 0/3 |
| `SHAPE_A` | 1.00 | 1/3 | 1.50 | 0/3 |
| `SMOOTH_A` | 0.83 | 1/3 | 0.74 | 0/3 |
| `REVMIX_A` | 0.70 | 1/3 | 0.96 | 0/3 |
| `COLOR_A` | 0.28 | 1/3 | 0.55 | 0/3 |
| `ATTACK_A` | 0.19 | 1/3 | 0.19 | 0/3 |
| `COMP_A` | 12.4 | 1/3 | 13.4 | 0/3 |
| `FILT_A` | ~ | 1/3 | ~ | 0/3 |
| `RATE_A` | 1.67 | 0/3 | ~ | 0/3 |
| `PACE` | ~ | 0/3 | ~ | 0/3 |
| `MORPH` | ~ | 0/3 | 1.07 | 0/3 |
| `COUPLE` | 1.55 | 0/3 | **0.00** | **3/3** |
| `SHUFFLE` | **0.00** | **3/3** | 0.58 | 0/3 |
| `TEMPO_BPM` | **0.00** | **3/3** | 1.52 | 0/3 |
| `DENSITY_A` | **0.00** | **3/3** | 1.85 | 0/3 |
| `STEPS_A` | **0.00** | **3/3** | 1.78 | 0/3 |
| `VARIATION_A` | 0.05 | 2/3 | 1.32 | 0/3 |
| `REV_DECAY` | 0.15 | 0/3 | 0.06 | 0/3 |
| `REV_DIFF` | 0.053 | 0/3 | 0.049 | 0/3 |
| `REV_SIZE` | 0.052 | 0/3 | 0.054 | 0/3 |
| `REV_SMEAR` | 0.048 | 0/3 | 0.049 | 0/3 |
| `REV_MOD` | 0.044 | 0/3 | 0.039 | 0/3 |
| `REV_TONE` | 0.032 | 0/3 | 0.043 | 0/3 |

The `_B` rows are omitted: on 2 of 3 terrains per mode the B deck was inaudible
in this rig, so their `dead` counts say more about deck balance than about the
parameters. `_A` and `_B` share a table row in `kBaseRules` in every case.

Three readings worth carrying forward:

- **Mode-exclusive parameters exist in both directions.** `SHUFFLE`, `TEMPO_BPM`,
  `DENSITY_A`, `STEPS_A` are step-only; `COUPLE` is free-only (it corrects a
  phase error the grid does not leave). A macro built on either kind is half
  dead by construction.
- **The reverb's character parameters are an order of magnitude below its send.**
  Subject to the metric's known bias against tails.
- **`P_DEPTH_A/B` is the strongest unowned parameter** — 1.37 FLOW / 2.05 STEP,
  and it multiplies every lane output before its destination (`part.cpp:105`).
  It is the literal reading of "how much everything moves" and no macro touches
  it.

## Per-macro verdict

| Macro | Verdict |
|---|---|
| MOTION | alive, but carried by DRIFT alone once the reverb leaves; DRIFT is 3× weaker in STEP (0.46 vs 1.44), i.e. weakest on the three archetypes that are mostly STEP. Its second mechanism — scaling every other macro's weather depth, `flow.cpp:449` — is real and invisible to the per-target table |
| DENSITY | dead on every FLOW terrain, all three targets |
| BRIGHT | alive; `FILT_A/B` carry it. `REV_TONE` (0.03) and `REV_DECAY` (0.03) are decoration |
| PACE | alive in both modes |
| WANDER | `FORM_A` and `SONG_A` dead on 40/40; `VARIATION_A` dead on 17/40. Effectively a VARIATION-only knob, and that half only works in STEP |
| SPACE | alive; `REVMIX` carries it, `SIZE`/`DECAY` are decoration |

One further observation, not a defect but worth knowing: BRIGHT and SPACE share
`REVMIX_A` and `REV_DECAY`, resolved by "farthest from base wins"
(`flow.cpp:322-329`). With BRIGHT at its calm end its `bp0` sits at 0.75–0.9, and
a full SPACE sweep then moves `REVMIX_A` from 0.800 to 0.811 — SPACE effectively
controls only the B deck's send. The mechanism is documented; the size of the
effect is not.

## Decisions taken

Recorded so the follow-up sessions inherit them instead of re-deriving them. All
of these are the owner's rulings, taken during the audit on 2026-08-13.

1. **The engine may be changed** where a control has no audio path, accepting the
   blast radius across Fireflow and the firmware shell.
2. **The free mode gets its own melody engine**, built on the same song settings
   as the step mode but working differently — a long drone often wants one or two
   notes. Roadmap item, before the Glow rework.
3. **SHAPE and SMOOTH get a rework**, before the Glow rework. The SHAPE blend
   above is one of its inputs, not its whole subject.
4. **Reverb leaves the macro layer**: DECAY and MIX go on the right fader, the
   rest become terrain-only base rules with MOD held low and TONE high. SIZE is
   explicitly rejected as a knob — sweeping it produces the usual pitch artifact.
   COUPLE is rejected too: SYNC hangs off it and stepped terrains must not break.
5. **Fader defaults**: TEMPO left, REVERB right. Where MASTER goes is open.
6. **The freed sixth macro becomes DELAY**, on FLUX. GRIT stays out — drive is
   too strong to sit permanently beside a delay, and returns sparingly if at all.
7. **Ownership model**: the story curve is shifted so knob-centre meets the
   overlay's value when a patch supplies one, and stays absolute when none does.
   This removes the constraint that forced TIDE out of the MOTION story, and with
   it the question of whether a given parameter may belong to a knob.

What was deliberately **not** decided: which parameters the remaining five macros
own. That is the Glow rework's subject.

## What this session changed

Everything else above is a finding; these are the edits.

- `engine/flow/taste.h` — the CHOKE fix of §4b: a single-point base rule, no
  generator draw. `tests/test_flow_terrain.cpp` pins both halves, the always-zero
  draw and the overlay's value arriving unrounded. The RED was proven by putting
  the old continuous span back.
- `tests/test_param_impact.cpp` — the two gates, new.
- `docs/roadmap.md` — the FLOW melody engine and the SHAPE/SMOOTH rework, both
  ahead of the Glow rework, plus the Glow rework entry carrying the decisions
  above.

No other engine defect was touched. Three of the four found in `engine/mod/`
belong to the FLOW melody engine and one to the SHAPE/SMOOTH rework.

## Open threads

- **The second silent-deck cause** (§4b): 12 of 52 terrains still lose a deck
  with `CHOKE = 0`, all of them with BODY on the silent side. Not traced.
- **The second FORM/SONG gate** (§4): forcing `SHAPE_A` to 1.0 made FORM audible
  on only 1 of 6 STEP terrains, so the SHAPE blend is not the whole story.
  `sh` also carries `_ev_shape`, `_shape_offset` (DRIFT writes it every control
  tick) and `_kick_shape`. Belongs to the SHAPE/SMOOTH rework.
- **FORM/SONG re-measured under task 10** (flow-melody-engine, 2026-08-13):
  `tests/test_param_impact.cpp` no longer excludes FORM/SONG, and its
  `apply_patch()` now forces `DEPTH_A/B` to 1.0 and `LANE_PITCH`'s `_active`
  true whenever the swept parameter is `FORM_A/B` or `SONG_A/B`, to rule out
  a downstream attenuator as the cause of a measured zero (part 10's brief,
  decided in advance). Under that control: `FORM_A`/`FORM_B` move audio only
  in FLOW — consistent with the SHAPE-blend story above, since
  `ModLane::_compute_raw()` returns the phrase pitch directly under
  `_flow_melody_on()` (task 8) and no longer goes through the S&H blend that
  STEP still does. `SONG_A` moves audio only in **STEP** — the opposite
  direction, unexplained. `SONG_B` is dead in **both** modes even under the
  same DEPTH/`_active` control — also unexplained, and not the same failure
  shape as `SONG_A`'s. Neither `SONG_A`'s nor `SONG_B`'s asymmetry is
  DEPTH/`_active` masking (both were controlled for); both are recorded as
  expected-set entries in `test_param_impact.cpp` rather than investigated
  further, per task 10's scope. Belongs to the SHAPE/SMOOTH rework, same as
  the line above.
- **The §5 table** still needs re-measuring at the centred operating point; see
  the correction under Method.
- **Where MASTER goes** once REVERB takes the right fader.
