# LED feedback — what the panel says while the instrument runs

**Status:** design, not built. Brainstormed 2026-08-16 with the owner.
**Scope:** the `FireflowHW` module and the shared light inventory. No new
hardware decisions — the envelope is settled in
[`docs/hardware/io-budget.md`](../../hardware/io-budget.md) §3.

## 1. What this is for

The panel currently says almost nothing about what the instrument is doing.
Ten LEDs are drawn; four of them can light at all, and of those four, two sit
in a row where they mean nothing.

The owner's request was "more feedback about what is happening", and the first
decision of the round narrowed it: **the LEDs answer "what is modulating right
now"**, not "what is set" and not "where is the beat". Everything below follows
from that, with two exceptions added at the end of the round because they cover
documented blind spots (§3.3, §3.4).

**The design principle, and it decides every placement question:** a light sits
where the question it answers is asked. A value light sits at the knob whose
destination it moves — not at the knob that sets its base. A speed light sits at
the speed knob. A phrase light sits at the arrangement knob.

## 2. Measured ground

Everything in this section was measured or read on 2026-08-16, not carried over
from an earlier document.

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
place, `Fireflow.cpp:1932`, and only for caption text. The six LEDs above are
painted into the SVG and have no `LightId`, so nothing can drive them. Any light
this design relies on must have an ID in the shared inventory.

**Three of deck A's LEDs are crowded into 8 mm** — `REC_A_L` 108,5, `CAP_A_L`
112,5, `GATE_A_L` 116,5 — beside the REC button at 101,0. Only `REC_A_L` has a
neighbour it relates to. `CAP` is the capture sequencer and `REC` is the sampler
recorder: two unrelated mechanisms, 4 mm apart, drawn identically.

**`CAP_A/B_L` indicate a feature that no longer exists.** `engine/mod/capture.h`
was deleted on 2026-07-14 in `6e6e2de` ("remove capture/replay sequencer from
engine, host and tests", 562 lines including `tests/test_capture.cpp` and the
demo scenarios). No capture control appears among the 69 panel parameters and
`gen_panel.py` does not mention capture at all.

**`SYNC_L` is placed by symmetry, not by meaning.** It is the mirror of
`TEMPO_L` about the centre and therefore lands beside `SHUFFLE`. There is no
sync control at that position — "SYNC" on the plate is `COUPLE`'s caption
(`HW_CAPTION["COUPLE"] = "SYNC"`).

**The five lanes and their panel homes.** `LANE_SOURCE`, `LANE_SIZE`,
`LANE_PITCH`, `LANE_MOTION`, `LANE_LEVEL` (`engine/mod/lane_id.h`), each at a
**fixed** ratio of one master rate (×2, ×½, ×1, ×¾, ×1,5). Only one has an
unambiguous panel control: `set_target_base` is called from exactly four sites
in `Fireflow.cpp` — `LANE_SOURCE` from SOURCE unconditionally (`:783`),
`LANE_PITCH` conditionally (`:805`), `LANE_SIZE` from SUB on some engines and
pinned to 0,5 otherwise (`:871`, `:873`). `LANE_MOTION` has no base setter (MOD
is its depth control, and it feeds COLOR and DENS at once — `part.cpp:258-292`)
and `LANE_LEVEL` has none at all.

**What the engine already exposes**, all public and const, all consumed by the
render host today: `lane_output(p, s)`, `target_value(p, lane)`, `gate(p)`,
`pitch_gate(p)` (`instrument.h:409-414`), `active_pattern_for_test(p)` (`:97`)
and `song_position_for_test(p)` (`:94`).

**What it does not expose:** the limiter's gain reduction. `Limiter::process`
computes `const float gain = _peak > 1.f ? 1.f / _peak : 1.f`
(`engine/fx/limiter.h:68`) as a local and discards it.

**Chain budget** (io-budget §3): three 74HC595 give 24 outputs, of which 4 go to
mux addresses and 5 to mux enables. A fourth register brings it to 32. Brightness
comes from the mux scan for free at **16 steps** — the scan rewrites the whole
chain once per address step, sixteen times per sweep, so varying which LED bits
are set across those writes is PWM at no additional cost. Finer brightness would
need a PWM loop decoupled from the scan, which is real per-block CPU.

## 3. The inventory

**19 LEDs: 8 of today's 10 kept, 2 deleted, 11 new.** Two of the kept ones move,
and four of them are given IDs they never had. With 4 address and 5 enable lines
that is **28 of 32 chain outputs**, four spare.

### 3.1 Value lights — "how hard is this being pushed right now"

Three per deck, each at the knob its lane moves:

| Light at | Lane | Reads |
|---|---|---|
| `SOURCE` (102,25 / 202,55) | `LANE_SOURCE` | `target_value(p, LANE_SOURCE)` |
| `FILT` (86,25 / 218,55) | `LANE_SIZE` | `target_value(p, LANE_SIZE)` |
| `COLOR` (23,50 / 281,30) | `LANE_MOTION` | `target_value(p, LANE_MOTION)` |

`target_value()` and not `lane_output()`: the former is what actually reaches the
engine, the latter is the raw bipolar lane before the deck is done with it.

**Two lanes deliberately get no value light.** `LANE_PITCH` is already displayed
— the GATE light flashes on every note it fires, and a second lamp beside it
would repeat one fact. `LANE_LEVEL` has no destination on the plate and its
effect is immediately audible as loudness; it is dropped rather than housed
somewhere arbitrary.

### 3.2 Speed lights — "how fast is all of this running"

One per deck, at `RATE` (61,00 / 243,80): a short decaying flash on each master
cycle wrap.

One flash suffices for all five lanes because **their ratios never change**. The
lanes differ in speed by construction, so the master pulse plus a fixed ladder
is the whole truth about tempo. TIDE and PACE become readable here without
lights of their own, because both stretch exactly this pulse — which is why the
round dropped the owner's original TIDE and PACE lamps: the information is
already larger and closer to hand.

### 3.3 Phrase light — "which of the two phrases is sounding"

One per deck, at `SONG` (48,00 / 256,80), reading `active_pattern_for_test(p)`.

`SONG` arranges two stored snapshots into AAAB, ABAB, ABBB, BUILD, ROTATE,
MIRROR or OFF, and nothing on the instrument reveals which one is currently
playing. Worse, **structural changes land on phrase boundaries**, so turning
SONG does nothing for a while — the classic "is this broken?" moment, built in
by design. This light is the answer to both questions at once.

### 3.4 Ceiling light — "the limiter is working"

One, central, near the master output.

`MASTER_DRIVE` was retired from the hardware panel by the control-reduction
round, so **no control tells you where the ceiling is any more**. When COMP and
GRIT drive the instrument into the limiter the sound changes and nothing says
so. This is the one addition in this design that costs an engine change (§6).

### 3.5 Kept, moved, deleted

| LED | Decision |
|---|---|
| `REC_A/B_L` | **kept in place** — the only one of the top-row three with a neighbour it belongs to. Behaviour unchanged (`Fireflow.cpp:1018-1033`) |
| `FLOW_A/B_L` | **kept in place, and given an ID.** In FLOW, SONG, VARY and FORM are inert, and three of the four knobs in that row are exactly those — the light says whether the row it sits in is live |
| `GATE_A/B_L` | **moved** out of the timing row into the VOICE row (y 34), where the note is shaped. Behaviour unchanged (slewed gate, `Fireflow.cpp:1011-1016`) |
| `SYNC_L` | **moved** to the `CLOCK` jack, where an external clock actually arrives, and given an ID |
| `TEMPO_L` | **kept in place**, given an ID. The beat is not the modulation cycle; it does not duplicate §3.2 |
| `CAP_A/B_L` | **deleted** — the feature has not existed since 2026-07-14 |

## 4. The display law

### 4.1 Value lights: three states

- **dark** — the lane is inactive. This happens by itself where it should: a
  Sampler deck's PITCH lane is deactivated by the host, so its light is dark
  without a special case.
- **glow** — active but not moving: a fixed low floor, distinguishable from dark
  at arm's length.
- **breathing** — brightness follows the value.

"Moving or not" comes from a one-pole lowpass over `|Δ target_value|` per block,
one per lane. Ten of those at block rate is not a measurable cost.

**Why three states and not two:** brightness alone lies. A lane that is switched
on but modulating by zero sits at a constant value and would glow steadily,
claiming activity it does not have — and "is this knob doing anything" is a
question this instrument has burned several rounds on already.

### 4.2 Brightness is gamma-corrected

LED perception is strongly non-linear; a linearly driven breath looks static
across its top half. The law applies a gamma curve before quantisation. Without
it the display computes correctly and looks wrong.

### 4.3 Sixteen steps, on both hosts

The law quantises to **16 brightness levels even in Rack**, where the host could
do better, because 16 is what the mux scan gives the hardware for free (§2). A
Rack module that breathes more finely than the panel ever can is a design that
validates itself against the wrong instrument.

### 4.4 Event lights

`RATE`, `GATE`: a flash at the event, decaying with a fixed coefficient — the
shape the existing GATE light already uses. `SONG`: steady, two brightness
levels for the two snapshots (not a colour change; the panel has one LED colour
per zone). `FLOW`, `SYNC`, `TEMPO`, ceiling: state, on or off, with the ceiling
light decaying rather than blinking so brief peaks stay visible.

## 5. Placement

Ten of the eleven new lights sit beside controls that already exist, so they
mirror by themselves — the panel's mirror-symmetry guard covers them without new
rules.
Exact millimetres are the generator's business and are settled by the existing
guards (clearance circles, caption gaps, the 8,03 mm Rack jack body), not by
this document. The two placements that are not simply "beside an existing knob"
are `GATE` (VOICE row) and the ceiling light (centre column, near the master
output); both need a position that survives `hw_panel_guard`.

## 6. What gets built

**Only the `FireflowHW` module.** Both widgets share one `Fireflow` module
class, so the display law is computed once and the large module simply does not
draw the new lights.

1. **`res/gen_panel.py`** — 15 new `LightId`s in the shared inventory: eleven for
   the new lights (six value, two speed, two phrase, one ceiling) and four for
   lamps that are drawn today but cannot light — `FLOW_A/B_L`, `TEMPO_L`,
   `SYNC_L`. With the four that already have IDs (`GATE_A/B_L`, `REC_A/B_L`)
   that is 19. **Trap:** the parameter and light number spaces must not collide
   (`Fireflow.cpp:1571`, `REC_A_L == 2 == DENSITY_A`).
2. **`res/gen_hw_panel.py`** — delete `CAP_A/B_L`, move `GATE_A/B_L` and
   `SYNC_L`, add eleven positions.
3. **`src/led_law.hpp`** (new) — the law as a pure, Rack-free unit: value and
   movement measure in, quantised brightness out. This exists so the law can be
   tested; `Fireflow.cpp` keeps only the wiring.
4. **`src/Fireflow.cpp`** — feed the law in `process`, drive 19 lights.
5. **`engine/fx/limiter.h`** and **`engine/instrument.h`** — store the gain
   factor `limiter.h:68` currently discards and expose it as a const accessor.
   About three lines, and the only engine change in this design.

## 7. Gates

Every gate must be shown red once before it is trusted.

- **G1 — three states are distinguishable.** For a lane driven inactive, active
  but static, and moving, `led_law` returns brightness in three disjoint bands.
- **G2 — a static lane is not dark and not breathing.** At a constant value the
  law settles on the glow floor within a bounded number of blocks.
- **G3 — sixteen levels, exactly.** Sweeping the input across its whole range
  yields at most 16 distinct outputs, and all 16 are reachable.
- **G4 — gamma is applied.** The output is not proportional to the input:
  midpoint brightness sits measurably above the linear midpoint.
- **G5 — the panel inventory is right.** `hw_panel_guard` asserts 19 lights, no
  `CAP_*`, `GATE_*` and `SYNC_L` at their new positions, and every new light
  mirrored.
- **G6 — no light ID collides with a parameter ID** (`panel_guard`).
- **G7 — the limiter accessor reports what the limiter did.** Driving the
  instrument past the ceiling returns a value below 1; leaving it below returns
  exactly 1.

G1–G4 and G7 are unit tests in `spky_tests`; G5 and G6 are panel guards.

## 8. Deliberately not delivered

- **A light per lane.** Five lamps in a row say *that* something modulates and
  never *what*, which is the whole value. The round rejected this explicitly.
- **`LANE_LEVEL`.** No destination on the plate; audible anyway.
- **TIDE and PACE lamps**, from the owner's original list — the RATE pulse shows
  their effect at four times the size.
- **An audio-input light.** Considered and dropped this round; it is cheap
  (measurable at the host boundary, no engine change) and can be added later
  into the four spare chain outputs.
- **Anything in the large `Fireflow` module.** It gets the IDs and draws none of
  them.
- **Finer brightness than 16 steps**, which would cost a decoupled PWM loop and
  feed the unexplained block-rate artifact (io-budget §6).

## 9. Consequences

- **The chain needs its fourth 74HC595** — one part, no GPIO. It was going to
  need it for any LED expansion; this design fixes the number at 19 of 32 with
  four spare after the mux lines.
- **Nothing here is measured on hardware.** The 16-step figure and the "costs no
  CPU" claim are derived from the chain topology (io-budget §3), not timed on a
  board. The Rack implementation is the design's only proving ground until
  bring-up.
- **The roadmap and README still list M3 "Capture sequencer" as done**, which
  §2 shows to be false since 2026-07-14. That is a documentation defect this
  round uncovered and does not fix; it belongs in a status commit, not here.
