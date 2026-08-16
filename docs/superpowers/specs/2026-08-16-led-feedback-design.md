# LED feedback — what the panel says while the instrument runs

**Status:** design, not built. Brainstormed 2026-08-16 with the owner, reviewed
the same day, and **recomposed after the review** — see §10 for what changed and
why, because three of the first draft's load-bearing arguments were false against
the code.
**Scope:** the `FireflowHW` module and the shared light inventory. The hardware
envelope is settled in [`docs/hardware/io-budget.md`](../../hardware/io-budget.md)
§3 and this design does not reopen it.

## 1. What this is for

The panel says almost nothing about what the instrument is doing. Ten LEDs are
drawn; six of them cannot light at all, and of the four that can, two sit in a
row where they mean nothing.

**The lights answer one question: what is modulating right now, and how hard.**
Not what is set — the knob shaft already says that. Not where the beat is.

**The principle that decides every placement:** a light sits where the question
it answers is asked. And **the quantity is the modulation excursion, never the
target value** — the first draft read `target_value()`, which is
`clampf(_base[slot] + mod, 0.f, 1.f)` (`part.cpp:117`), i.e. knob plus
modulation. A knob at 0,9 sitting perfectly still would then outshine a knob at
0,1 swinging full scale, and the display would answer the question §1 rejects.

## 2. Measured ground

Read or measured on 2026-08-16. Nothing here is carried over from an earlier
document.

**What exists.** `gen_hw_panel.py` draws ten LEDs:

| LED | Position (mm) | Driveable? |
|---|---|---|
| `FLOW_A_L` / `FLOW_B_L` | 93,50 / 211,30 at y 14,5 | no — `HW_ONLY` |
| `REC_A_L` / `REC_B_L` | 108,50 / 196,30 at y 14,5 | yes |
| `CAP_A_L` / `CAP_B_L` | 112,50 / 192,30 at y 14,5 | no — `HW_ONLY` |
| `GATE_A_L` / `GATE_B_L` | 116,50 / 188,30 at y 14,5 | yes |
| `TEMPO_L` | 130,40 at y 34,0 | no — `HW_ONLY` |
| `SYNC_L` | 174,40 at y 34,0 | no — `HW_ONLY` |

**`HW_ONLY` elements cannot light.** `kHwOnlyCtls` is consumed in exactly one
place, `Fireflow.cpp:1932`, and only for caption text. Any light this design
relies on needs a `LightId` in the shared inventory.

**Three of deck A's LEDs are crowded into 8 mm** beside the REC button at 101,0,
and only `REC_A_L` relates to it. `CAP` is the capture sequencer, `REC` is the
sampler recorder: unrelated mechanisms, 4 mm apart, drawn identically.

**`CAP_A/B_L` indicate a feature that no longer exists.** `engine/mod/capture.h`
was deleted on 2026-07-14 in `6e6e2de`, together with `tests/test_capture.cpp`
and the demo scenarios — 562 lines. No capture control appears among the 69
panel parameters.

**`SYNC_L` is placed by symmetry, not meaning.** It is `TEMPO_L`'s mirror about
the centre and therefore lands beside `SHUFFLE`. "SYNC" on the plate is
`COUPLE`'s caption (`gen_hw_panel.py:145`).

**The lane ratios are not fixed.** `super_modulator.cpp:44-46`:

```cpp
const float s = (i == LANE_PITCH) ? _pitch_scale : _mod_scale * _tide_mult;
_lanes[i].set_rate_hz(_base_hz * s * kLaneRatio[i]);
```

**TIDE multiplies the four texture lanes and leaves PITCH alone.** Measured
across the TIDE range (`kTideRatios`, ×1/4…×4), lane rate over master rate:

| TIDE | SOURCE | SIZE | PITCH | MOTION | LEVEL |
|---|---|---|---|---|---|
| ×1/4 | 0,500 | 0,125 | 1,000 | 0,188 | 0,375 |
| ×1 | 2,000 | 0,500 | 1,000 | 0,750 | 1,500 |
| ×4 | 8,000 | 2,000 | 1,000 | 3,000 | 6,000 |

A 16× spread. In STEP it is worse: `lane_slots()` (`lane_len.h:35-44`) divides by
TIDE and then rounds and clamps to `[2, 64]`, so at STEPS 16 / TIDE ×1/4 two
lanes collapse onto the same cycle length. `_pitch_scale` and `_mod_scale` are
also written independently by COUPLE and DRIFT.

**The "inactive lane" state is not reachable for texture lanes.** `_active[]`
boots all true (`part.h:639`) and the only writer that runs by itself is
`Fireflow.cpp:881`, `set_target_active(p, LANE_PITCH, !samplerPart)` — PITCH
only. And `docs/engine-map.md:380` says of that mechanism: *"a known defect, not
a contract … do not build a design that relies on it."*

**What the engine exposes**, public and const, consumed by the render host today:
`lane_output(p, s)`, `target_value(p, lane)`, `gate(p)`, `pitch_gate(p)`
(`instrument.h:409-414`), `active_pattern_for_test(p)` (`:97`).

**What it does not expose:** the modulation excursion (`_depth`, `_tdepth` and
`_base` are all private to `Part`), and the limiter's gain reduction —
`Limiter::process` computes `const float gain = _peak > 1.f ? 1.f / _peak : 1.f`
(`limiter.h:68`) as a local and discards it.

**The limiter's audible onset is not where its gain reduction starts.** The VCV
host pushes `set_master_drive(0.40f)` unconditionally (`Fireflow.cpp:962`, pinned
by `test_panel.py`), so `_pre = 1,48` and `knee = 0,7147`. Measured: `shape()`
begins bending at bus peak **0,483** while `gain < 1` only from **0,676** — about
2,9 dB of audible soft saturation before any gain reduction exists to report.

**Chain budget** (io-budget §3): three 74HC595 give 24 outputs, 4 to mux
addresses and 5 to mux enables; a fourth register brings it to 32. Brightness
rides the mux scan for free at one step per address, so **the step count equals
the mux width — 16 with 16:1 parts, 8 with 8:1** — and that choice is explicitly
still open (io-budget §6).

## 3. The inventory

**19 LEDs: 8 of today's 10 kept, 2 deleted, 11 new.** Two of the kept ones move
and four are given IDs they never had. With 4 address and 5 enable lines that is
**28 of 32 chain outputs**, four spare — and one spare if the mux choice lands on
8:1, which needs nine muxes and therefore nine enables.

### 3.1 Excursion lights — "how hard is this lane pushing right now"

Four per deck, one per texture lane, each at the knob nearest the lane's usual
destination:

| Light at | Lane | Position A / B (mm) |
|---|---|---|
| `SOURCE` | `LANE_SOURCE` | 102,25 / 202,55 |
| `FILT` | `LANE_SIZE` | 86,25 / 218,55 |
| `COLOR` | `LANE_MOTION` | 23,50 / 281,30 |
| `COMP` | `LANE_LEVEL` | 106,50 / 198,30 |

All four read the **excursion** — the modulation term alone, `lane_output(slot) ·
depth · tdepth`, which is zero when MOD is zero and zero when the lane is not
moving. Not `target_value()`, which carries the knob (§1), and not the raw
`lane_output()`, which ignores depth and would show full swing at MOD 0.

**The placement claims a lane, not a destination.** This matters because the
destinations move per engine: `LANE_SOURCE` is timbre on SYNTH, read position on
SAMPLER and **drive** on BBD; `LANE_MOTION` is stereo width plus COLOR on SYNTH
and **feedback** on BBD; `LANE_SIZE` is filter cutoff on SYNTH and **grain size**
on SAMPLER, where its base comes from SUB and not from FILT. A light that
promised "this is your filter" would be lying on two of five engines. A light
that says "the lane that lands here is pushing this hard" is true on all five —
which is why the excursion framing is not merely a fix for §1's contradiction
but the thing that makes a fixed placement defensible at all.

`LANE_LEVEL` gets a light **because** it has no control anywhere: on a BBD deck
it is the wet/dry mix (`bbd_engine.h:15`, *"MIX is on LANE_LEVEL, not on a
knob"*, `bbd_engine.cpp:215`). Its light at COMP is the only readout that
parameter can ever have.

**`LANE_PITCH` gets no excursion light**, and this is the one place the design
accepts a known gap. The GATE light shows when a note fires, not how far the
lane moved, and on a Sampler deck the lane fires identically whether pitch is
moving or frozen. The gap is recorded in §9 rather than papered over.

### 3.2 Phrase light — "which of the two phrases is sounding"

One per deck at `SONG` (48,00 / 256,80), reading `active_pattern_for_test(p)`.

`SONG` arranges two stored snapshots into AAAB, ABAB, ABBB, BUILD, ROTATE,
MIRROR or OFF and nothing reveals which is playing. Structural changes land on
phrase boundaries, so turning SONG does nothing for a while — the built-in "is
this broken?" moment. **Two snapshots are two blink patterns, steady versus
double-pulse, not two brightness levels:** brightness is the channel the
excursion lights already use, and two brightness levels on one LED is the least
reliable discrimination available at arm's length.

### 3.3 Ceiling light — "the sound is being squeezed"

One, central, near the master output. `MASTER_DRIVE` was retired from the
hardware panel, so nothing tells you where the ceiling is any more.

**It reports the audible onset, not the gain reduction:** the measurand is
`peak > knee` inside `Limiter::process`, not `gain < 1`. Reporting gain
reduction alone would leave the light dark through the 2,9 dB of soft saturation
measured in §2 — the very stretch where the sound changes first.

### 3.4 Kept, moved, deleted

| LED | Decision |
|---|---|
| `REC_A/B_L` | **kept** — the only one of the top-row three with a neighbour it belongs to. Behaviour unchanged (`Fireflow.cpp:1018-1033`) |
| `FLOW_A/B_L` | **kept, given an ID.** It disambiguates the one thing the row cannot show: whether STEPS is exactly at 0. In FLOW, `SONG` is inert and `MELODY` is inert on the melody but still live on the texture lanes and on a Sampler deck — the light is worth having, but not for the sweeping reason the first draft gave (§10) |
| `GATE_A/B_L` | **moved** out of the timing row into the VOICE row (y 34), where the note is shaped. Behaviour unchanged |
| `SYNC_L` | **moved** to the `CLOCK` jack, where an external clock arrives, and given an ID |
| `TEMPO_L` | **kept**, given an ID |
| `CAP_A/B_L` | **deleted** — the feature has not existed since 2026-07-14 |

## 4. The display law

### 4.1 Intensity: an envelope sets the ceiling, the excursion breathes inside it

Per light, from the excursion `e`:

- **`E`** — a peak-tracked envelope of `|e|`, fast attack, slow release. This is
  the modulation *depth*.
- **intensity** = `E · (kFloor + (1 − kFloor) · |e| / max(E, ε))`.

So the trough of every breath is `kFloor · E` and its peak is `E`. Three
readings fall out of one formula instead of being bolted on:

- **dark** — `E = 0`: nothing is modulating here. Reachable, honest, and the
  common case at MOD 0.
- **dim breath** — shallow modulation.
- **bright breath** — deep modulation.

**Why the floor is proportional and not fixed:** a fixed floor would make a lane
parked at zero look the same as one modulating gently, and would put every
breath's trough through the same dark band, which reads as a loose contact at
the slow rates this instrument is built for. A proportional floor also solves
what the review called the display's worst failure: **a very slow, deep
modulation shows as a bright steady light — "a lot is happening here, slowly" —
rather than as inert**, because `E` is large even when `|e|` barely moves.

### 4.2 Brightness is gamma-corrected, in the correct direction

Perceived lightness goes roughly as the cube root of duty cycle, so
perceptual linearity needs **duty = intensity^γ with γ ≈ 2,2**, which puts the
midpoint duty *below* the linear midpoint (0,5^2,2 ≈ 0,22). A linear duty ramp
looks static across its top half.

### 4.3 Quantisation must not swallow the bottom of the breath

The step count is **the mux width, not a constant 16** — the 8:1/16:1 choice is
open (§2), so it is a parameter of the law and the gates are written against it.

Naive quantisation of a gamma curve at 16 steps sends everything below intensity
≈ 0,37 to duty 0, i.e. **off** — the bottom third of every breath would be
indistinguishable from "nothing is modulating", destroying §4.1's whole point.
The law therefore requires: **every non-zero intensity maps to at least one duty
step, and only intensity exactly 0 maps to duty 0.** Lift, then quantise.

### 4.4 Event and state lights

`GATE`: unchanged. `SONG`: steady versus double-pulse (§3.2). `FLOW`, `SYNC`,
`TEMPO`: state. Ceiling: decays rather than blinks, so brief peaks stay visible.

**No light uses the existing GATE coefficient as an event-flash model.** That
coefficient (`Fireflow.cpp:1014`, `0.05f` applied per sample) is τ ≈ 0,42 ms —
edge-softening on a sustained gate, not a decay. An event flash needs its own,
of order 100–200 ms.

## 5. Placement

Ten of the eleven new lights sit beside controls that already exist; the ceiling
light is the exception and needs a position in the centre column that survives
`hw_panel_guard`.

**They do not mirror by themselves.** `place()` (`gen_hw_panel.py:236-257`)
routes `_A`/`_B` enums through `DECK_POS`, but light enums end in `_L` and fall
through to `LIGHT_POS`, which raises `KeyError` if an entry is missing. Every new
light needs a hand-written, hand-mirrored `LIGHT_POS` pair; `test_mirror_symmetry`
checks them, it does not generate them.

Three coupling constraints the implementation will hit:

- `test_hw_panel.py:32` requires `HW_LIGHTS` and `gp.LIGHTS` to match **in order**.
- `gen_panel.py:729`: the C++ side centres the LED rings on `kLightCtls[0..1]`,
  so the gate lights must keep indices 0 and 1.
- The large module must exclude the new lights via `STATIC_LIGHTS`
  (`gen_panel.py:739`) **and** skip them widget-side, or its panel draws 19 LEDs
  at positions it has to invent.

Whether an LED fits beside `FILT_A` is not assumed: `FILT_A` (86,25/53,00, r 8,5)
and `SOURCE_A` (102,25/50,22, r 6,0) are 16,24 mm apart, so the direct line
between them has no legal point under `test_hw_panel.py`'s `d ≥ a.r + b.r`. The
light goes off-axis, and the guard decides.

## 6. What gets built

**Only the `FireflowHW` module.** Both widgets share one `Fireflow` module class,
so the law is computed once and the large module simply does not draw the new
lights.

1. **`engine/instrument.h`** — `lane_excursion(p, slot) const`, returning the
   modulation term alone. The host cannot derive it: `_depth`, `_tdepth` and
   `_base` are private to `Part`.
2. **`engine/fx/limiter.h`** + **`engine/instrument.h`** — store and expose
   whether the shaper is bending (`peak > knee`) and by how much. **Note the
   early return at `limiter.h:66`**, `if (_pre == 1.f && _peak <= 1.f && peak <=
   knee) return;` — it skips line 68 entirely, so a naive "store what line 68
   discards" leaves a stale value on that path.
3. **`res/gen_panel.py`** — 15 new `LightId`s: eleven for the new lights (eight
   excursion, two phrase, one ceiling) and four for lamps drawn today that cannot
   light (`FLOW_A/B_L`, `TEMPO_L`, `SYNC_L`). With the four that already have IDs
   that is 19. **Trap:** parameter and light number spaces must not collide
   (`Fireflow.cpp:1571`, `REC_A_L == 2 == DENSITY_A`).
4. **`res/gen_hw_panel.py`** — delete `CAP_A/B_L`, move `GATE_A/B_L` and
   `SYNC_L`, add eleven `LIGHT_POS` entries — five mirrored pairs plus the
   single ceiling light.
5. **`src/led_law.hpp`** (new) — the law as a pure, Rack-free unit: excursion in,
   quantised duty out. It exists so the law can go red in `spky_tests`;
   `Fireflow.cpp` keeps only the wiring.
6. **`src/Fireflow.cpp`** — feed the law, drive 19 lights.

Two engine additions, both const observers. The first draft's RATE pulse would
have needed a third (there is no phase accessor on `Instrument`); it is gone.

## 7. Gates

Each must be shown red once before it is trusted.

- **G1 — dark means zero modulation, and it is reachable.** On a real `Fireflow`
  module at MOD 0, all eight excursion lights read duty 0. With MOD up and a lane
  moving, the same light reads non-zero. *(Also a wiring gate: it fails if the
  light is never written.)*
- **G2 — no non-zero intensity quantises to off.** Sweeping intensity across its
  whole range, the only input producing duty 0 is exactly 0.
- **G3 — the step count is the mux width, and every step is reachable.** At the
  configured width N, the law produces exactly N distinct duties and all N occur.
- **G4 — gamma runs in the correct direction.** Duty at intensity 0,5 sits
  measurably *below* half of full scale, and `duty(v)^(1/γ)` tracks `v` within
  tolerance.
- **G5 — the trough scales with depth.** For a deep and a shallow modulation at
  equal instantaneous phase, the deep light's trough is brighter than the
  shallow light's trough; and a lane whose `E` is large but whose `|e|` is
  frozen stays bright rather than decaying to dark.
- **G6 — every light is written every block.** Instantiate `Fireflow`, run a
  handful of blocks, assert all 19 brightnesses have been set and that a
  modulating lane's light changes across blocks. This is the gate that would
  have caught six LEDs sitting on the panel for months with no `LightId`.
- **G7 — the panel inventory is right.** 19 lights, no `CAP_*`, `GATE_*` and
  `SYNC_L` at their new positions, every new light mirrored. Replaces
  `test_hw_panel.py`'s hard-coded `kinds.get("L") == 6` and `total_leds == 10`.
- **G8 — no light ID collides with a parameter ID.** Check first whether
  `panel_guard` already compares the two enum spaces; `test_hw_panel.py:169` only
  asserts the tables exist, so this may be new work rather than an existing guard.
- **G9 — the ceiling light tracks the audible onset, in order.** Drive the
  instrument above the knee, then below, then assert the reported value returns
  to exactly "not bending". Written as two independent cases the second passes
  trivially from the init value and cannot catch the `limiter.h:66` staleness.
  And it must light in the band where `peak > knee` but `gain == 1`.

G1–G6 and G9 are unit tests in `spky_tests`; G7 and G8 are panel guards.

## 8. Deliberately not delivered

- **A light per lane in a row.** Five lamps side by side say *that* something
  modulates and never *what*. Rejected in round 1.
- **A deck speed pulse at RATE.** Round 1 justified one pulse per deck with
  "the lane ratios never change"; §2 measures a 16× spread across TIDE. The
  breath is the rate display — three lanes breathing at different speeds show
  the ratios directly, and TIDE becomes visible as the lights drifting apart,
  which is the very thing the deck pulse could not show.
- **TIDE and PACE lamps**, from the owner's original list — see above; their
  effect is legible in the breath.
- **An ENGINE indicator per deck.** The review's second recommendation, declined:
  ENGINE is a five-zone detented pot and its pointer shows the zone, while one
  LED would have to encode five states as a blink code — worse to read than the
  pointer. The excursion framing (§3.1) also removes the argument's force, since
  a light no longer claims a destination that the engine can move.
- **An audio-input light.** Cheap and needs no engine change; deferred to the
  spare outputs rather than dropped.
- **A `LANE_PITCH` excursion light** (§3.1) and **a per-deck output-level lamp**,
  which the review proposed as a way to answer "why is deck B silent". Both are
  live candidates for the spare outputs; neither is designed here.
- **Anything in the large `Fireflow` module.**

## 9. Consequences and open risks

- **The chain needs its fourth 74HC595** — one part, no GPIO.
- **The two headline numbers hang on an undecided part choice.** With 8:1 muxes
  it is 8 duty steps, nine muxes and nine enables: 19 + 3 + 9 = 31 of 32, and
  the four spare outputs become one. §4.3 is written against the width for that
  reason, and no number in this design should be quoted as settled until the
  part is chosen.
- **The refresh rate is unmeasured and is the largest visual risk.** PWM refresh
  equals the mux sweep rate. Blocks run at 500 Hz, io-budget §6 records that the
  per-channel settling time is unmeasured and that a full sweep need not run
  every block. If it cannot, an N-step sweep lands near 30 Hz and the LEDs
  strobe — worst at low duty, which is where the gamma curve puts most of the
  breath.
- **`LANE_PITCH` has no excursion display**, and the RANGE law that flattens a
  phrase onto one scale degree (engine-map §7) stays undiagnosable from the
  panel.
- **Nothing here is measured on hardware.** The Rack module is the design's only
  proving ground until bring-up.

## 10. What the review changed

The first draft was reviewed on 2026-08-16 and three load-bearing arguments did
not survive contact with the code. All three were re-verified before rewriting.

1. **The lights showed the knob, not the modulation.** They read
   `target_value()`, which is base plus modulation (`part.cpp:117`) — the
   quantity §1 explicitly rejects. Now the excursion, which also removed the
   dependency on `target_value`'s divergence from `_tg` on Sampler decks, made
   the FILT placement honest (the FILT knob enters the engine *after* the lane,
   so it never moved that light), and gave the dark state something to mean.
2. **"The lane ratios never change" was false**, by 16× across TIDE, measured.
   It was the sole justification for one pulse per deck and for dropping the
   owner's TIDE lamp. The pulse is gone; the breath carries the rate.
3. **The dark state was unreachable.** `_active[]` is written for `LANE_PITCH`
   only, and PITCH has no light — so the three-state law was a two-state law and
   its gate tested a fixture. The proportional floor (§4.1) makes the states fall
   out of one quantity instead.

Smaller corrections: the gamma gate ran backwards; quantisation at 16 steps
swallowed the bottom third of the breath; the ceiling light measured gain
reduction instead of the audible onset, and its gate could not go red for the
one bug it can have; `FORM` was cited as a hardware control although the
control-reduction round retired it; the RATE pulse needed a phase accessor the
first draft did not count; the lights do not mirror by themselves; and §9 named
a documentation defect that had already been fixed two commits earlier.
