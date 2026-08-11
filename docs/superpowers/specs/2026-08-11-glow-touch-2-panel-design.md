# Glow becomes Touch 2: the VCV rehearsal rig — design

**Date:** 11 August 2026
**Status:** Draft, approved. None of it is implemented.
**Trigger:** `2026-08-10-fireflow-touch-curated-places-design.md` puts FireFlow on
a Synthux Touch 2 for residency phase 1. That spec leaves four questions open
(§7) and answers all of them with "after the board arrives". The board is not
here. Rack is.
**Touches:** `host/vcv/**` (panel generator, Glow module, a new state header),
`tests/**` (state gate). Not: `engine/`, not `shell/`, not the big Fireflow
panel, not `engine/flow/places/` — the pool file stays unwritten here.

---

## §1 What this is

Glow's VCV panel is redrawn as a 1:1 Touch 2 faceplate, and its control surface
is replaced by the board's: **12 touch pads, 6 trim knobs, 2 faders, 2 switches,
one stereo out**. The module keeps its slug and its name.

The point is not cosmetic. The Touch spec's §5.3 asks for a rehearsal stage —
"live in Rack, hands on the knobs" — and its §7 defers four decisions to a board
that arrives in September at the earliest. A faithful Rack module *is* that
stage, available now, and it turns three of those four questions from guesses
into things that can be played:

| §7 question | What the rig does about it |
|---|---|
| What the 2 faders and 2 switches do | Both are assignable from a short candidate list; the rehearsal picks the winner. |
| All 12 pads places, or some actions? | Twelve pads exist and are playable; the answer falls out of use. |
| Pads binary or continuous? | **Not answered here.** §2.3 narrows it; only a measurement closes it. |
| Which part engines the Seed carries | Untouched. That is a Seed measurement and stays one. |

## §2 What the hardware actually is

Three findings, all from sources, all checkable.

### §2.1 The board is a 16 HP eurorack plate

ModularGrid lists Simple Touch 2 as **16 HP** — 81.28 × 128.5 mm. The
manufacturer's own page insists it is *not* a eurorack module ("Do not connect
Eurorack signal without attenuation!"), and both are true: it carries eurorack
plate dimensions at line level. The reference photo's aspect ratio (417 × 667 px
= 0.625) agrees with 81.28 / 128.5 = 0.632.

Consequence: a Rack module can hold the board at true size. Glow grows from
12 HP to 16 HP.

### §2.2 The layout is published, in a source comment

The faceplate in the reference photo is `SIMPLE TOUCH FX v.01`, and its firmware
is public: `Synthux-Academy/simple-touch-instruments`, `daisyduino/TouchFX`. The
sketch carries the arrangement as an ASCII drawing:

```
|-| (*)   (*)   (*)    (*) |-|
| | S31   S32   S33    S34 | |
|||                        |||
|_| (*)                (*) |_|
S36 S30                S35 S37

  S10 o o S09    o S07
                o S08
```

Four knobs in an upper row, two in a lower row, a fader outboard on each side,
and the two switches sitting *inside* the pad field. `simple-touch-daisy.h`
gives the channels: eight analog (`S30`–`S37` = six knobs plus two faders) and
four digital (`S07`–`S10`).

**Four digital pins for two switches means each switch is centre-off** —
three positions, not two. From the photo alone this would have been built wrong.

### §2.3 The pads are an MPR121, read binary

`simple-touch-daisy.h` wraps an **Adafruit MPR121** and reads only
`_cap.touched()` — a 12-bit binary word. The chip also exposes
`filteredData()` per electrode, so continuous sensing exists in hardware and is
simply unused by the vendor driver.

This is a driver reading, **not a measurement.** Touch spec §7 demands a
measurement and still does. What changed is only the size of the question: from
"is continuous possible?" to "is the value good enough to play?".

### §2.4 What is not available

No mechanical drawing. `simple-hardware/synth-interface-panel/03 Simple Vector
Template` is a 635 × 400 landscape panel for a different board. Synthux
publishes no Touch 2 outline, no drill pattern, no faceplate SVG.

Positions therefore come from the reference photo, and §3.2 states what that
costs.

## §3 The panel

### §3.1 The generator stays the single truth

`res/gen_flow_panel.py` is rewritten, not replaced. It keeps emitting
`res/Glow.svg` and `src/generated_flow_panel.hpp`, and the C++ keeps reading
every coordinate, label and colour from the header. **No coordinate is written
into `Glow.cpp`.** This is the rule that has kept graphics and widget placement
from drifting apart three times in this repo, and a redesign is exactly when it
would be tempting to break it.

The board's channel names — `S30`–`S37`, `S07`–`S10` — travel into the generator
as a comment on every control. Panel, Rack module and the later Seed firmware
then argue about `S36`, not about "the left fader".

### §3.2 Positions come from a photograph, and the file says so

Centres are measured off the reference photo, calibrated to 81.28 mm plate width
(≈ 0.195 mm/px). A photograph is not a drawing: perspective and lens are in
there. Expected fidelity **±0.5 mm**.

The generator's file header states this in plain words, together with the
consequence: when the board arrives, the panel is re-measured against it and the
generator corrected. Until then the plate is visually true, not manufacturably
true. Any later claim of manufacturing fidelity has to point at a measurement,
not at this spec.

### §3.3 Look

FireFlow's own language on the board's geometry: palette, type and decoration
from `gen_panel.py` (PAPER / INK / GREEN / COPPER), so Glow and the big Fireflow
module keep reading as one instrument. The Synthux engraving is not reproduced —
it is somebody else's artwork, it is one of five swappable plates, and an
organic engraving cannot be computed by a generator that owns every coordinate.

Three zones, as on the board:

| Zone | Contents |
|---|---|
| Head | the two jacks at the left; at the right, where the Seed sits on the board, the wordmark and the alpha pennant. The Seed itself is not drawn. |
| Control field | six trim knobs, two faders, centre lettering |
| Pad field | the lower third: twelve plates, two switches |

## §4 The controls

| Board | Count | Rack widget | Default assignment |
|---|---|---|---|
| Trim knobs `S31`–`S34`, `S30`, `S35` | 6 | `Trimpot` | MOTION, DENSITY, BRIGHT, DIRT (upper row, left to right); WANDER (`S30`), SPACE (`S35`) |
| Fader `S36` (left) | 1 | `VCVSlider` | **TEMPO** |
| Fader `S37` (right) | 1 | `VCVSlider` | **MASTER** |
| Switch `S09`/`S10` (left) | 1 | `NKK` | **LOCK** |
| Switch `S07`/`S08` (right) | 1 | `NKK` | **SCALE** |
| Touch pads | 12 | `TouchPlate` (new, §5) | place 1–12 |
| Jacks | 2 | `PJ301MPort` | OUT L (upper), OUT R (lower) |

**Macro order is a default, not a doctrine.** Reading order is the honest first
guess; the rehearsal may resort it.

**TEMPO on a fader is not arbitrary.** §4.2 removes the CLK input, and with it
the module's only tempo source. A control has to replace it. TEMPO drives
`Instrument::set_tempo_bpm`; MASTER is a host-side output gain in `Glow.cpp`,
applied after `Instrument::process`, and touches no engine parameter.

**The candidate lists**, chosen from what Flow and the host already expose, kept
short on purpose — a long list turns the rehearsal into shopping:

| Control | Candidates |
|---|---|
| Either fader | TEMPO, MASTER, off |
| Either switch | LOCK, SCALE, off |

Assignments live in `dataToJson` alongside the pad codes. A two-valued target on
a three-position switch uses the end positions and reads the centre as the lower
one.

**Why the switches are three-valued.** LOCK uses the two end positions with the
centre reading as off — a centre-off switch physically is that. SCALE uses all
three over two independent Flow setters that already exist:

| Position | Effect |
|---|---|
| down | AUTO — `set_scale_override(-1)`, `set_root_override(-1)` |
| centre | scale fixed, root free |
| up | scale and root both fixed |

### §4.1 The two jacks

The board has exactly two jack holes. Both are used: upper = OUT L, lower =
OUT R. Two holes, two ports, no invented geometry.

The board's stereo *input* is not drawn. Glow passes `nullptr, nullptr` into
`Instrument::process` (`Glow.cpp:562`) and has no audio input at all; a drawn
input jack would be a dead hole. Giving Glow an audio input is a different
project.

### §4.2 What leaves the panel, and where it goes

The six CV inputs, CLK, NEW, GENRE and SCALE-as-a-knob disappear as controls.
NEW, the draw and the genre constraint move into the context menu.

The reasoning is one sentence: **the board is the stage, Rack is the workshop.**
Touch spec §8.2 ("No menu") governs what ships on the Touch. A Rack context menu
never ships. Keeping the generator reachable from the menu is what lets the plate
stay strictly Touch 2 while the tool stays complete.

Existing Glow patches load nonsense afterwards. That is deliberate and, in this
alpha, free.

## §5 The `TouchPlate` widget

The one thing Rack does not ship. `TouchPlate : ParamWidget`, momentary: mouse
down is a touch, mouse up ends it. It draws a rounded tile via NanoVG.

**The widget stays stupid.** Tap-versus-hold is decided by the module in the
control tick, not by the widget — the same split NEW already uses. One place
owns the gesture.

### §5.1 Pad shape

**Centres from the photo, shape from us.** On the board the pad positions are
copper on the PCB and therefore fixed; the engraving is only art laid over them.
So each true centre gets a rounded tile around it, large enough to hit and small
enough not to overlap its neighbours.

If the real centres sit irregularly, the field looks irregular. That is the
board, not a defect. The alternative — a tidy 3 × 4 grid — looks better and does
not fit the PCB in September.

### §5.2 Pad behaviour

Straight from Touch spec §3, unchanged:

| Gesture | Effect |
|---|---|
| Tap | `wake(code)` — instant, no blend |
| Hold ≳ 400 ms | `new_partial(0x3F)` — all six macro domains rerolled, `master` untouched |
| Tap the same pad again | back to the curated state |

**400 ms is a starting value to be tuned by ear, not a measurement.** NEW's
1.5 s is too sluggish for a pad. The spec records it as adjustable so nobody
later mistakes it for a result.

## §6 State, curation and export

Twelve terrain codes live in the module and travel through `dataToJson`, the
same route `terrainCode()` already takes today.

**On creation the module draws twelve places, three per archetype** — `ARCH_COUNT`
is four (`DRONE`, `PULSE`, `ARP`, `FRAGMENT`), so twelve divides evenly. A fresh
module is therefore playable immediately and shows the range, instead of
happening to draw four drones. Each pad is individually overwritable ("pin
current terrain to this pad") and individually redrawable.

**Names are data, not panel.** The printed plate carries the numbers 1–12,
generated and fixed. A place's *name* is runtime state: editable from the
context menu, visible in the tooltip, carried into the export. Plate stays
generated; names stay data; the two never mix.

### §6.1 The export, and its deliberate limit

A context menu item copies the twelve rows to the clipboard in the `pool.tsv`
format of Touch spec §4.3.

Filled: `code`, `arch` (from `arch_of`), `pad`, and `name` where one was typed.
**Left empty: `fp`, `date`, `note`.**

The fingerprint is not computed here. Touch spec §6 puts it in `tests/`, and a
second producer of it in a second language is precisely the silent divergence
that the gate exists to catch. The VCV module fills the columns it owns and
leaves the rest to the pipeline.

## §7 Tests

Two levels, both with existing precedent in this repo.

**§7.1 Panel geometry — `res/test_flow_panel.py`.** Grows to assert: no overlap
between tile and tile, tile and knob, tile and fader; every control inside the
plate with margin; captions collision-free; and the header's control counts match
the board — 12 pads, 6 knobs, 2 faders, 2 switches, 2 jacks.

**§7.2 Pad state and export — doctest in `tests/`.** The pad state machine and
the TSV formatting move into a hardware-free header,
`host/vcv/src/touch_pads.hpp`, and get a test wired through `CMakeLists.txt`.
This is the pattern `bbd_edge_state.hpp`, `song_rung_state.hpp` and
`form_song_migration.hpp` already run: no Rack types cross into the header, so
the desktop build can test it.

**§7.3 Every new assertion is made red once.** `test_hw_panel.py` was green
without asserting anything for nine tasks. The red is proven deliberately —
move a coordinate, watch the gate fall, name the right control, put it back —
before the assertion counts as a gate.

## §8 What this spec does not decide

| Open | Settled by |
|---|---|
| Pads binary or continuous | A measurement on the arrived board. §2.3 narrows it, nothing more. |
| The final fader and switch assignment | The rehearsal. That is what the assignable list is for. |
| The true pad centres | The arrived board. The photo gives ±0.5 mm (§3.2). |
| Macro-to-knob order | The rehearsal. §4 sets a default. |
| Whether `pool.tsv` ever gets written from here | Touch spec §10. This module produces clipboard rows, not the file. |

## §9 Risks

**The photo is the only source.** Every position in the first version inherits
whatever the lens did. Mitigated by §3.2 stating it in the generator itself, so
no later reader mistakes the numbers for measurements — but it is not removed
until the board is on the desk.

**Touch 2 versus Simple Touch v1.** The published sketch and pin map come from
the v1 instrument repo. The control complement is identical — 12 pads, 6 knobs,
2 faders, 2 switches — and the reference photo matches the drawing, but nothing
here has been confirmed against a Touch 2 board.

The two halves of that risk cost very differently. The **pin names** are used
only as comments and labels, so a mismatch costs a rename. The **pad geometry**
is the panel, so a mismatch costs a re-measure and a regenerate — cheap only
because §3.1 keeps every coordinate in one Python file.

**The reference photo is the only geometry source, and it lives outside the
repo.** Before implementation starts it has to be pinned somewhere durable and
cited by path in the generator header. A spec whose sole source can be deleted
by tidying a Screenshots folder does not have a source.

**The rig can answer §7 wrongly.** A fader that feels right under a mouse may
feel wrong under a finger, and a 400 ms hold on a mouse button is not a 400 ms
hold on a capacitive plate. The rehearsal narrows the field; it does not sign
anything off. Whatever this rig decides is re-checked on the board.
