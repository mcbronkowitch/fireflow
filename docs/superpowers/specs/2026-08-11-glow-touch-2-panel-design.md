# Glow becomes Touch 2: the VCV rehearsal rig — design

**Date:** 11 August 2026
**Status:** Implemented on branch `glow-touch-2-panel` (11 August 2026), six
tasks, reviewed task by task and once whole. §11 is still open.
**Revision:** second draft. The first was reviewed on three axes (fact-check,
completeness, adversarial design) and rewritten. §13 records what the review
changed and what was overruled, so the reasoning is not lost.
**Trigger:** `2026-08-10-fireflow-touch-curated-places-design.md` puts FireFlow
on a Synthux Touch 2 for residency phase 1. That spec leaves four questions open
(§7) and answers all of them with "after the board arrives". The board is not
here. Rack is.
**Touches:** `host/vcv/**`, `tests/**`, `CMakeLists.txt`, `docs/**` — the full
list is §10. Not: `engine/`, not `shell/`, not the big Fireflow panel, not
`engine/flow/places/`.

---

## §1 What this is

Glow's VCV panel is redrawn at the Touch 2's true size, and its control surface
is **replaced** by the board's: 12 touch pads, 6 trim knobs, 2 faders,
2 switches, one stereo out. The module keeps its slug, its name and its `Module`
class; only the widget, the panel and the parameter set change.

The point is not cosmetic. The parent spec's §5.3 asks for a rehearsal stage —
"live in Rack, hands on the knobs" — and its §7 defers four decisions to a board
that arrives in September at the earliest. A faithful Rack module is that stage,
available now.

**What it can and cannot answer.** Being honest about this is the difference
between a rehearsal and a toy:

| §7 question | What the rig does |
|---|---|
| What the 2 faders and 2 switches do | Both assignable (§4.3). The rig narrows the field. |
| All 12 pads places, or some actions? | Twelve pads exist and are playable. The rig can show that twelve places is too many or too few; it cannot invent an action. |
| Pads binary or continuous? | **Nothing.** §2.3 narrows the question; only a measurement closes it. |
| Which part engines the Seed carries | Nothing. That is a Seed measurement. |

**And what it structurally cannot do:** a mouse is not a finger. Every §7 answer
this rig produces is a hypothesis to re-check on the board, never a signature.
§12 keeps that risk standing rather than arguing it away. One mitigation is free
and should be used: Rack's own **MIDI-Map** maps MIDI CC onto any module
parameter, so a cheap fader-and-pad controller puts real fingers on the faders
and real pads under the hands without the module needing a single CV jack.

## §2 What the hardware actually is

Three findings, all from sources, all checkable.

### §2.1 The board is a 16 HP eurorack plate

ModularGrid lists Simple Touch 2 as **16 HP** — 81.28 × 128.5 mm. The
manufacturer's own page insists it is *not* a eurorack module ("Do not connect
Eurorack signal without attenuation!"), and both are true: it carries eurorack
plate dimensions at line level. The reference photo's aspect ratio (417 × 667 px
= 0.625) agrees with 81.28 / 128.5 = 0.632.

Rack's own constants confirm the arithmetic: `RACK_GRID_WIDTH = 15` px at
`SVG_DPI = 75` is exactly 5.08 mm, so 16 HP = 81.28 mm. (Rack's
`RACK_GRID_HEIGHT = 380` px is 128.69 mm — a rounding of the 128.5 mm Eurorack
3U standard. This repo generates at 128.5 mm and keeps doing so.)

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

The drawing shows no pads. Their positions come from the photo (§3.2).

### §2.3 The pads are an MPR121, read binary

`simple-touch-daisy.h` wraps an **Adafruit MPR121** and reads only
`_cap.touched()` — a 12-bit binary word. The chip also exposes
`filteredData()` per electrode, so continuous sensing exists in hardware and is
simply unused by the vendor driver.

This is a driver reading, **not a measurement.** Parent §7 demands a measurement
and still does. What changed is only the size of the question: from "is
continuous possible?" to "is the value good enough to play?".

### §2.4 What is not available

No mechanical drawing. `simple-hardware/synth-interface-panel/03 Simple Vector
Template` is a 635 × 400 landscape panel for a different board. Synthux publishes
no Touch 2 outline, no drill pattern, no faceplate SVG in any of its public
repositories.

**Worth asking them for one, but it blocks nothing here.** Touch 2 ships with
five swappable faceplates, so the plate outline, the pad windows and the drill
pattern exist as vector files in somebody's hands — and phase 1's deliverable is
a faceplate of our own, so that template is needed eventually regardless. If it
arrives, §3.2's measuring step is skipped and the geometry is exact for free.
That is an errand for the faceplate job, not a precondition for this one.

## §3 The panel

### §3.1 The generator stays the single truth

`res/gen_flow_panel.py` is rewritten, not replaced. It keeps emitting
`res/Glow.svg` and `src/generated_flow_panel.hpp`, and the C++ keeps reading
every control centre, label and colour from the header.

The precise rule, stated as it actually holds today: **no control centre, no
caption and no colour is written into `Glow.cpp`.** Widget *class* choices and
the two screw positions are and remain C++ decisions (`Glow.cpp:634-664`), and
`Glow.cpp` already documents each widget choice against the printed footprint.
§4.4 extends that reckoning to the three new widget types.

One deliberate carve-out is added by this spec: **pad names are runtime data and
therefore not in the header** (§6.3).

The board's channel names — `S30`–`S37`, `S07`–`S10` — travel into the generator
as a comment on every control, so panel, Rack module and the later Seed firmware
argue about `S36`, not about "the left fader".

### §3.2 Geometry: measured off the photo, and that is enough

**This panel is a VCV panel. It is not the faceplate draft.** Dropping that
second role is what makes the precision question go away, and it has to be
dropped explicitly, because today's `gen_flow_panel.py` header claims it — "drawn
at true hardware dimensions so the faceplate doubles as the 1:1 draft for the M6
panel". That claim does not survive the rewrite.

Nothing in Rack can see a half-millimetre. Rack draws at 75 DPI ≈ 2.95 px/mm
against a typical display's ~3.78 px/mm, and the user zooms freely, so the plate
is not life-size under the hands at any zoom anyway. Dimensions are here so the
pads sit right *relative* to the knobs and so the module reads as the board —
both of which survive any error a photograph can introduce.

So geometry is one task with one output, and no error budget:

1. **Pin the source — outside this repository.** A source that can be deleted
   by tidying a Screenshots folder is not a source, so the reference photo gets
   a permanent home and is cited by path in `touch2_geometry.py` and in the
   generated header. That home is **not** `host/vcv/res/ref/`. It is a
   photograph of a Synthux product and `mcbronkowitch/fireflow` is public, so
   the image lives in the owner's private website repo — currently
   `FireFlow_Website/docs/reference/touch2-fx-2026-08-11.png`, written relative
   to this repository's parent so the committed string does not publish a user
   name either. Only the path travels with the code. Nothing downstream reads
   the image, so nothing breaks when it is absent; the provenance is simply
   weaker than a committed file would be, and that is the trade made
   deliberately.

   *(An earlier draft of this spec said the photo was committed to
   `host/vcv/res/ref/`. The owner reversed that for privacy before
   implementation began. It is written down here because §11's re-measure will
   be run from this spec, and the old instruction would walk the next measurer
   straight into the mistake the decision was made to avoid.)*
2. **Measure.** Calibrated to 81.28 mm plate width (≈ 0.195 mm/px): 12 pad
   centres, 6 knob centres, 2 fader centres plus travel length, 2 switch
   centres, 2 jack centres — a table in mm, committed as the generator's input
   constants.

The numbers are provisional and the generator header says so in one line: they
come from a photograph and get corrected against the board when it arrives. That
is a later, separate job (§11) and it blocks nothing here.

### §3.3 Look and lettering

FireFlow's own language on the board's geometry: palette, type and decoration
from `gen_panel.py` (PAPER / INK / GREEN / COPPER), so Glow and the big Fireflow
module keep reading as one instrument. The Synthux engraving is not reproduced —
it is somebody else's artwork, it is one of five swappable plates, and an
organic engraving cannot be computed by a generator that owns every coordinate.

Three zones, as on the board:

| Zone | Contents |
|---|---|
| Head | the two jacks at the left; at the right, where the Seed sits on the board, the wordmark and the `ALPHA` pennant. The Seed itself is not drawn. |
| Control field | six trim knobs, two faders |
| Pad field | the lower third: twelve plates, two switches |

**What the plate prints, and what it must not.** The pads carry the numbers
1–12 as generated `PanelTxt` entries. The knobs, faders and switches carry
**no** function captions. That is not laziness: §4 declares the macro order and
both assignments provisional, and a printed `TEMPO` beside a fader assigned to
`off` would be a lie baked into an SVG. Function names live in tooltips, which
are runtime. The masthead rules, the two dots and the `ALPHA` pennant survive,
re-placed for a head zone that is now short and wide instead of 12 HP tall.

## §4 The controls

| Board | Count | Rack widget | Default assignment |
|---|---|---|---|
| Trim knobs `S31`–`S34`, `S30`, `S35` | 6 | `Trimpot` | MOTION, DENSITY, BRIGHT, DIRT (upper row, left to right); WANDER (`S30`), SPACE (`S35`) |
| Fader `S36` (left) | 1 | `VCVSlider` | TEMPO |
| Fader `S37` (right) | 1 | `VCVSlider` | MASTER |
| Switch `S09`/`S10` (left) | 1 | `NKK` | LOCK |
| Switch `S07`/`S08` (right) | 1 | `NKK` | SCALE |
| Touch pads | 12 | `TouchPlate` (new, §5) | place 1–12 |
| Jacks | 2 | `PJ301MPort` | OUT L (upper), OUT R (lower) |

### §4.1 Macro order, without breaking the enum

`Glow.cpp:35-46` pins `params[MOTION + m]` to `flow_ids.h`'s `Macro` order with
six `static_assert`s, and `controlTick` indexes it directly. "The rehearsal may
resort it" therefore needs a mechanism, not a wish.

**A `kKnobMacro[6]` table** maps knob position → macro id, exactly as
`kCvMacro` (`glow_ui.hpp:22`) already maps jack → macro. The static_asserts
stay, the enum stays, and resorting is a one-line data change.

### §4.2 The two jacks

The board has exactly two jack holes. Both are used: upper = OUT L, lower =
OUT R. Two holes, two ports, no invented geometry.

The board's stereo *input* is not drawn. Glow passes `nullptr, nullptr` into
`Instrument::process` (`Glow.cpp:562`) and has no audio input at all; a drawn
input jack would be a dead hole. Giving Glow an audio input is a different
project.

**The input tables go away, and the generator must handle that.** With the five
CV jacks and CLK removed, `INPUTS` is empty and today's `header()` would emit
`static const PanelCtl kInputCtls[] = {};` — a zero-length array, ill-formed in
standard C++. The generator therefore **omits the array and the `configInput`
loop entirely when the list is empty**, keeps `enum InputId { NUM_INPUTS };`
(which legally yields 0), and `config()` is called with `0` inputs.

### §4.3 Faders and switches: assignable, with real laws

Assignments are module state, saved in `dataToJson`. The candidate lists are
short on purpose.

| Control | Candidates |
|---|---|
| Either fader | TEMPO, MASTER, off |
| Either switch | LOCK, SCALE, off |

**TEMPO.** Fader 0..1 maps linearly to `P_TEMPO_BPM`'s declared range, 50–140
BPM (`flow_params.h:80`), and drives `Instrument::set_tempo_bpm`.

The first draft justified this by claiming CLK's removal left no tempo source.
That was wrong, and the correction matters for the implementation: **the terrain
owns the tempo.** `flow_params.h:177` pushes `P_TEMPO_BPM` into the Instrument on
every terrain change, with per-archetype spans in `taste.h:983` (drone 55–75,
arp 90–130), and today's CLK is only an *override* on top of that fallback
(`glow_ui.hpp:162`, `Glow.cpp:529`). So:

- A TEMPO fader replaces the override, not a vacuum.
- It must be re-applied **every control tick**, because Flow overwrites the
  tempo on every push — the same reason `clock_bpm()` is called unconditionally
  today (`Glow.cpp:521-527`).
- `off` gives the place its own tempo back. That is the musically interesting
  setting, because per-place tempo is exactly what `pool.tsv` curates, and a
  flat fader flattens it.

**MASTER.** Linear gain 0..1, default **1.0**, applied to `outl`/`outr` before
the existing `clamp(±1) * 5 V` (`Glow.cpp:563`). `off` = unity, not muted. A
module that boots at half gain is a bug report.

**LOCK.** Uses the two end positions, centre reads as off. **The context menu's
Terrain-lock toggle is removed** — one control, one truth; a physical switch and
a menu item for the same state is a synchronisation bug waiting to be filed.

**SCALE — and its missing value.** The first draft had the switch freeze a scale
while deleting the 14-position knob that chooses which (`glow_ui.hpp:49`,
`scale_of_knob`). The switch gated an override with no value.

Corrected: **the values move to the context menu, the switch only gates them.**
Root already has a submenu (`Glow.cpp:681`); Scale gains one alongside it, over
the existing `kScaleKnobOrder`.

| Position | Effect |
|---|---|
| down | AUTO — `set_scale_override(-1)`, `set_root_override(-1)` |
| centre | scale fixed to the menu's Scale, root free |
| up | scale and root both fixed to the menu's values |

Parent §8.2 ("whatever needs an explanatory label does not go on the Touch") is
a real objection to the centre position: "scale fixed, root free" is not
engravable. It is accepted **for the rig only**, and recorded in §11 as a
decision the board may have to drop back to two positions.

### §4.4 Widget footprint versus printed footprint

`Glow.cpp:634/645` already reckons each widget against its print (a
`RoundLargeBlackKnob` is ~15.6 mm for a 16 mm print). The three new types get
the same treatment, recorded in the generator next to the printed size:

- `VCVSlider` is 19.843 × 76.535 px at 75 DPI = **6.72 × 25.92 mm**, handle
  3.98 mm, travel vertical. The photo's fader measures ≈ 24 mm, so the stock
  widget is close enough to use unmodified — a lucky fit, and worth stating so
  nobody re-derives it.
- `Trimpot` and `NKK` footprints are measured from the Rack installation during
  implementation (the SDK ships no `res/ComponentLibrary`) and written into the
  generator as constants.

## §5 The `TouchPlate` widget

### §5.1 Base class

`TouchPlate : app::Switch` with `momentary = true`, `draw()` overridden for a
NanoVG tile.

Not `ParamWidget`: `app/Switch.hpp:14-30` **already is** "a ParamWidget which, in
momentary mode, sets the value to maxValue when held and minValue when
released", with `onDragStart`/`onDragEnd` implemented. `app::SvgSwitch` is the
subclass that adds frames; `Switch` itself has no artwork, which is exactly the
custom-drawn case.

**The widget stays stupid.** Tap-versus-hold is decided by the module in the
control tick, not by the widget — the same split NEW uses today. One place owns
the gesture.

### §5.2 Pad shape

**Centres from the photo, shape from us.** On the board the pad positions are
copper on the PCB and therefore fixed; the engraving is only art laid over them.
Each true centre gets a rounded tile around it, large enough to hit and small
enough not to overlap its neighbours.

This was challenged in review — the argument being that the player aims at the
plate art, not the copper, so the panel ends up showing the PCB and not a
designed surface. The counter that decides it: **in September we draw the plate,
and the plate has to sit over that copper.** Tile size and corner radius are ours
and are tuned until the field reads as designed; the centres are not ours. If
the measured centres turn out to make a field that cannot be made to look
deliberate, that is a finding worth having early, not a reason to draw a tidy
grid that will not fit.

### §5.3 The pad state machine

The first draft gave three rows and no machine; §8.2's doctest had no subject.
The full definition:

**State is one live pad and one flag** — `int live_pad` (−1 = none) and
`bool excursion`. Not twelve states: parent §3 makes an excursion transient
("tap the pad again → back to the curated state. No undo mechanism is needed for
this"), so leaving a pad discards its excursion by design.

| Event | Effect |
|---|---|
| Press pad *n* (rising edge) | `wake(code[n])` **immediately** — no latency. `live_pad = n`, `excursion = false`. Arm the hold timer. |
| Still held at 400 ms | `new_partial(0x3F)` once. `excursion = true`. Not repeating. |
| Release | Nothing. The timer disarms. |
| Press pad *n* again | `wake(code[n])` — which *is* the return to the curated state. No special case needed. |

Waking on press rather than on release is what makes this work without latency
*and* without the collision the first draft had: a hold is a wake followed by a
reroll, and the next tap is a plain wake that undoes it.

**Under LOCK:** `wake()` is not a gesture and is not refused, but `new_partial`
is (`flow.h:47-51`). So with LOCK on, pads still change place and holds do
nothing. That is intended — LOCK guards the generator, not the recall — and the
refusal is shown (§5.4).

**400 ms is a starting value to be tuned by ear, not a measurement.** NEW's
1.5 s is too sluggish for a pad. Recorded as adjustable so nobody later mistakes
it for a result.

### §5.4 Feedback

The first draft specified a three-state behaviour with nothing to see. The
player could not tell which pad was live, whether it was excursed, or that a
hold had been refused.

`TouchPlate` draws its own state from a module-owned array — **no `LightId`
entries**, because twelve lights in the generated header would put runtime state
into a panel table:

| State | Tile |
|---|---|
| idle | plate fill, thin ink outline |
| live, curated | GREEN collar |
| live, excursed | COPPER collar |
| refused hold | brief COPPER flash, then back |

The refusal reuses `RefuseFlash` (`glow_ui.hpp`), which today serves NEW and
would otherwise be orphaned. `NEW_L` and `led_level()` are covered in §7.

## §6 State, curation and export

Twelve terrain codes live in the module and travel through `dataToJson` — the
same route `glow_capture()` / `GlowSave` already takes (`glow_ui.hpp:181`), not
`terrainCode()`, which is only the menu-facing helper.

### §6.1 The draw, and its determinism

On creation the module draws twelve places, three per archetype — `ARCH_COUNT`
is four (`flow_ids.h:7`), so twelve divides evenly.

**The draw is deterministic.** A fixed seed constant means every fresh module,
on every machine, holds the same twelve places. That is what lets a note, a
video or a manual refer to "pad 7" and mean something. `draw_new(cur, seq, want)`
(`terrain.h:142`) takes a caller-owned `Rng`, so the module owns one seeded from
that constant.

**One edge that now happens twelve times.** `draw_new`'s genre branch, on
exhausting `kGenreDrawCap`, returns a default `TerrainState` whose archetype need
not match `want` (`terrain.cpp:640-645`). At construction this is hit twelve
times instead of once: on a mismatch the module advances the seed and retries
rather than accepting an off-archetype place, with a bounded retry count.

**And the honest caveat.** Parent §7 settles the fader question "after twelve
places have been *played*" — twelve *curated* places, the survivors of §5.1
screening and §5.3 rehearsal. Twelve drawn places are not that; parent §2 calls
the draw a slot machine with a hit rate that is not good enough. So: the drawn
twelve make the module playable on creation and are **not** evidence. Any §7
answer this rig produces must come from a session whose pads were pinned from
curated terrain, and the spec says so rather than pretending the default is
good enough.

### §6.2 Momentary params must not fire on load

Rack's `paramsToJson` writes every param, momentary ones included. A patch saved
while a pad was held would restore that pad at 1.0, and §5.3 would arm a hold and
fire `new_partial` 400 ms after load — destroying the terrain that was just
restored.

**Rule:** all twelve pad params are forced to 0 after `dataFromJson` and in
`onAdd`, and the hold timer arms only on a genuine rising edge — the same rule
`GestureBridge` (`glow_ui.hpp:99`) already encodes for NEW.

### §6.3 Names

A place's name is runtime state: a fixed `char name[kNameCap + 1]` buffer
capped at **32 characters** (`touch_pads.hpp`; `Place` is made trivially
copyable so the audio thread can copy the whole array without allocating),
with tab and newline **rejected on input** (they would break the TSV of
§6.4).

Names are editable from the context menu and appear as the pad's tooltip. A live
tooltip is the one place §3.1's "everything from the generated header" cannot
hold, because `configButton`'s string is fixed at construction: the pad's
`ParamQuantity::getLabel()` is overridden to read the module's name array. This
is the carve-out, stated here so it is a decision and not a leak.

The printed plate carries only the numbers 1–12.

### §6.4 The export, and its deliberate limit

A context menu item copies all twelve rows to the clipboard in the `pool.tsv`
format of parent §4.3, column order `code, arch, date, fp, pad, name, note`.

- Filled: `code`, `arch`, `pad`, and `name` where one was typed.
- Empty but present as fields: `date`, `fp`, `note` — they are interior columns,
  so their tabs must be emitted.
- A header row is emitted. Line ending `\n`, not `\r\n`, because the destination
  is a repo file.
- `arch` is spelled with the enum's short name — `DRONE`, `PULSE`, `ARP`,
  `FRAGMENT`.
- All twelve rows, named or not.

**The fingerprint is not computed here.** Parent §6 puts it in `tests/`, and a
second producer of it in a second language is precisely the silent divergence the
gate exists to catch.

**`note` is the perishable one.** Parent §4.3 defines it as "one sentence: why it
was kept", and it is capturable only in the seconds after the judgement. It is
therefore editable from the same menu as the name, stored alongside it, and
exported — not left to a later pass over a TSV, where it will not be written.

### §6.5 Initialize and Randomize

**Initialize** (`onReset`, `Glow.cpp:412`) — today documented in
`host/vcv/README.md:590` as returning to `kHouseCode` and clearing lock and
tonality. The new contract:

| Item | On Initialize |
|---|---|
| The twelve codes | re-drawn from the fixed seed — i.e. the same twelve as a fresh module |
| Names and notes | cleared |
| Fader / switch assignments | back to §4's defaults |
| `live_pad`, `excursion` | pad 1, no excursion |
| Lock, scale, root | cleared, in the existing order (tonality before wake) |

`kHouseCode` and `wakeHouse()` are superseded: pad 1 is the house place. Whether
`kHouseCode` becomes pad 1's code or is retired is an implementation call.

Note this module has no `defaultFor()` / `configControls()` — that convention
belongs to `Fireflow.cpp:302/311` and `init_patch.hpp`. Stated so nobody goes
looking.

**Randomize.** `configSel()` (`Glow.cpp:147`) already clears `randomizeEnabled`
on GENRE and SCALE with a documented rationale. Extending it: **the six macros
join Randomize; the twelve pads, two faders and two switches do not.** A
Randomize that jams the tempo, sets a random output gain, throws LOCK and pokes
twelve momentary pads is not a musical dice roll, it is a fault.

## §7 What happens to Glow's gesture layer

NEW leaves the plate, and a large amount of working, tested code hangs off it.
Leaving that unstated would produce either a field of dead code or the deletion
of tests another host still needs. Per item:

| Item | Fate |
|---|---|
| NEW as a control | → context menu, "Draw a new terrain" |
| GENRE (draw constraint) | → context menu submenu, as today |
| Per-macro reroll ("hold NEW, turn a knob") | → context menu, "Reroll ▸ MOTION / DENSITY / …". **Kept deliberately**: pads only ever call `new_partial(0x3F)`, so without this the partial-mask API loses its only caller and rots. |
| `undo()` / `GlowSave.undo` / `restore_undo` | **kept**, reachable as a menu item. Rack's Ctrl+Z undoes params and never touched the terrain; that stays true and is worth saying in the README. |
| `RefuseFlash` | **kept**, repurposed for refused pad holds (§5.4) |
| `clock_bpm()` | **deleted** with CLK. Its fallback role is taken by §4.3's TEMPO-`off`. |
| `led_level()`, `NEW_L` | **deleted**. Pad feedback is drawn by the widget, not by a `Light`. |
| `KnobTracker`, `GestureBridge` | `GestureBridge`'s rising-edge rule is **kept** and reused for pads (§6.2). `KnobTracker` exists for the hold-and-turn gesture; it follows that gesture into the menu or dies with it — decided in the plan, not guessed here. |
| `engine/flow/gesture.h` decoder | **untouched.** It is engine code and the Seed firmware will want it. Only Glow's *use* of it changes. |
| `tests/test_glow_ui.cpp` | shrinks; `kCvMacro` / `cv_to_macro` / `clock_bpm` / `led_level` tests go with their subjects. `scale_of_knob` **was deleted**, not kept — it converted a knob position into a `ScaleId`, and the Touch 2 surface has no scale knob; its range-clamping rule now lives in `clamp_menu_scale` (`glow_ui.hpp`), with test coverage it did not have before. |
| `tests/test_flow_gesture.cpp` | **untouched** — it tests engine code. |

## §8 Tests

### §8.1 Panel geometry — `res/test_flow_panel.py`

The first draft said this file "grows". It does not: most of it is rewritten.

**Already generic, and will cover pads for free** once pads are in `g.PARAMS`
with a footprint entry: `test_no_overlap`, `test_on_panel`,
`test_labels_clear_every_glyph`. The byte-comparison gate
`test_committed_files_match_the_generator` also carries over unchanged and stays
the strongest assertion in the file.

**Must be rewritten or deleted:** `test_panel_size` (60.96 → 81.28),
`test_knob_geometry` (3×2 at 20 mm pitch → the measured layout),
`test_jack_geometry` (8 jacks at 14 mm pitch → 2 jacks),
`test_wander_has_no_cv_and_there_is_no_rst` (no CV jacks exist),
`test_patch_field_has_no_second_horizontal_rule`, the silkscreen and wordmark
tests, and `PARAM_ORDER` — whose own comment calls it "the frozen contract" and
which is re-pinned to the new 22-entry order.

**Genuinely new:** a control-count assertion — 12 pads, 6 knobs, 2 faders,
2 switches, 2 jacks.

**And a model change the existing tests cannot express.** `radius_of()` gives one
scalar per control kind. A tile is a rectangle and a `VCVSlider` is a tall
rectangle. The generator therefore gains a per-kind `(w, h)` box beside `RADIUS`,
and the collision test moves to rect-vs-rect using the `dist_to_rect` helper that
`test_hw_panel.py:349` already provides for the SD cutout.

**What these gates may and may not assert.** Tile-vs-tile overlap is a legitimate
gate *because tile size is ours* — red means shrink the tile. Nothing asserts a
property of the copper centres themselves; a gate whose red has no remedy is not
a gate.

### §8.2 Pad state and export — doctest in `tests/`

The state machine of §5.3 and the TSV formatting of §6.4 move into
`host/vcv/src/touch_pads.hpp` with `tests/test_touch_pads.cpp`, following
`bbd_edge_state.hpp`, `song_rung_state.hpp`, `form_song_migration.hpp` and
`glow_ui.hpp`.

Constraints, verified against those four:

1. **No Rack header** — no `plugin.hpp`, no `rack.hpp`, no `rack::` type, no
   `nvg*`, no `Vec` / `Widget` / `Module`.
2. **No `json_t`** — jansson comes from Rack. `Glow.cpp` does the JSON
   marshalling, exactly as it does for `GlowSave`.
3. **Header-only**: `#pragma once`, `inline` / `struct`, no `.cpp`.
4. Includes limited to the C++ stdlib and headers under `engine/` (reachable via
   `spky_engine`'s interface).
5. The test includes it as `"vcv/src/touch_pads.hpp"`
   (`target_include_directories(spky_tests PRIVATE host)`).
6. `tests/test_touch_pads.cpp` is added to the `spky_tests` source list by hand —
   there is no glob.

### §8.3 Every new assertion is made red once

`test_flow_panel.py` accumulates into `FAILS` and exits non-zero only at the end,
so "red" means the whole suite is proven red per new assertion — move a
coordinate, watch that assertion fall and name the right control, put it back.

(The first draft cited `test_hw_panel.py` as a cautionary tale in the present
tense. That is out of date: it now carries 36 real checks and runs as ctest
`hw_panel_guard`. The vacuity is history, and `CMakeLists.txt:247-258` documents
the fix. The house rule stands on its own.)

## §9 Definition of done

Three commands, in this order. None of the traps is obvious and all are in
`CLAUDE.md`:

```bash
# 1. regenerate, then gate the panel -- run FROM host/vcv/
cd host/vcv && python res/gen_flow_panel.py && python res/test_flow_panel.py

# 2. engine + tests. Release is NOT optional: Debug fails unrelated render hashes
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure

# 3. the plugin -- never a hand-rolled g++; the system g++ is the ARM cross-compiler
host/vcv/build-local.sh
```

Step 1 must run in that order: `test_committed_files_match_the_generator`
byte-compares both artifacts against a fresh generation.

## §10 Files touched

| File | What |
|---|---|
| `host/vcv/res/gen_flow_panel.py` | rewritten |
| `host/vcv/res/Glow.svg` | regenerated |
| `host/vcv/src/generated_flow_panel.hpp` | regenerated |
| `host/vcv/res/test_flow_panel.py` | largely rewritten (§8.1) |
| *(no file)* | the pinned source photo stays **out of this repo** — private sibling repo, path only (§3.2) |
| `host/vcv/src/Glow.cpp` | control surface, pad machine wiring, menu |
| `host/vcv/src/glow_ui.hpp` | shrinks (§7) |
| `host/vcv/src/touch_pads.hpp` | new (§8.2) |
| `tests/test_touch_pads.cpp` | new |
| `tests/test_glow_ui.cpp` | shrinks with its subjects |
| `CMakeLists.txt` | new test source; `flow_panel_guard` already exists |
| `host/vcv/plugin.json` | Glow's description still says "six macro knobs and a NEW button" |
| `host/vcv/README.md` | the whole `## FireFlow Glow` section, ~504–660 |
| `docs/roadmap.md` | Glow's status |
| `docs/release-notes.md` | Glow is no longer unchanged |

`host/vcv/Makefile:89` already lists `res/Glow.svg` and needs no edit, because
the filename is kept.

## §11 What this spec does not decide

| Open | Settled by |
|---|---|
| Pads binary or continuous | A measurement on the arrived board. §2.3 narrows it, nothing more. |
| The final fader and switch assignment | The rehearsal — from pads pinned with curated terrain, not from the default draw (§6.1). |
| The true pad centres | **Later, and deliberately.** A 600 dpi scan of the arrived board takes an hour and belongs to the faceplate job. Nothing in Rack can see the difference (§3.2). |
| Macro-to-knob order | The rehearsal. §4.1 gives it a mechanism. |
| Whether SCALE stays three-valued on the board | Parent §8.2. The centre position is not engravable; the rig keeps it, the plate may not. |
| `KnobTracker`'s fate | The plan, once the menu's reroll entry is written (§7). |
| Whether `pool.tsv` is ever written from here | Parent §10. This module produces clipboard rows, not the file. |

## §12 Risks

**A mouse is not a finger.** A fader that feels right under a mouse may feel
wrong under a finger, and a 400 ms hold on a mouse button is not a 400 ms hold on
a capacitive plate — which the board reads with no LEDs at all, while a Rack tile
confirms the hold visually. The rig will therefore flatter the gesture it is
meant to test. Mitigation, not cure: drive it through Rack's MIDI-Map from a
physical controller (§1) before treating any §7 answer as more than a
hypothesis.

**The rig can under-report its own subject.** Every moment where twelve pads are
not enough is exactly the data point being collected — and in this design every
one of them can be resolved with a right-click. A session meant to produce a §7
answer must therefore be played *without* the menu; that is a discipline, not a
mechanism, and it is the weakest link in the argument.

**Photo geometry is provisional — and, here, cheap.** Every coordinate is
provisional until the board is scanned. This is *not* a real risk for the VCV
module, because nothing Rack renders can resolve the error (§3.2) and the tile
sizes that could actually collide are ours, not the board's. It becomes a real
risk only if the panel is later reused as a faceplate draft without re-measuring
first — which is exactly why §3.2 removes that claim from the generator header
instead of leaving it there with a caveat.

**Touch 2 versus Simple Touch v1.** The published sketch and pin map come from
the v1 instrument repo. The control complement is identical — 12 pads, 6 knobs,
2 faders, 2 switches — and the reference photo matches the drawing, but nothing
here is confirmed against a Touch 2 board. The two halves cost differently: **pin
names** are comments and labels, so a mismatch costs a rename; **pad geometry**
is the panel, so a mismatch costs a re-measure and a regenerate — cheap only
because §3.1 keeps every coordinate in one Python file.

**The premise itself is conditional.** The parent spec's own trigger line reads
"the residency offer is still open… when it lands, this becomes the first
project". This spec proceeds as if it lands. If it does not, parent §9 holds —
the places survive and carry the big instrument — but this panel does not.

## §13 What the review changed

Recorded so the reasoning is not re-litigated.

**Corrected facts.** Five CV inputs, not six (WANDER has no jack, and
`test_flow_panel.py:93` asserts it). `TouchPlate` derives from `app::Switch`, not
`ParamWidget`. Persistence goes through `glow_capture()`, not `terrainCode()`.
The terrain owns the tempo and CLK is only an override — the first draft's
justification for the TEMPO fader was wrong (§4.3).

**Closed blockers.** The zero-length `kInputCtls` array (§4.2), momentary pads
firing on load (§6.2), the SCALE switch with no value source (§4.3), and the
complete absence of pad feedback (§5.4).

**Filled gaps.** The pad state machine (§5.3), draw determinism (§6.1),
Initialize and Randomize (§6.5), the gesture layer's fate (§7), the macro-order
mechanism (§4.1), the real shape of the test work (§8.1), the file list (§10)
and the build commands (§9).

**Raised and dismissed: patch compatibility.** Review flagged that Rack matches
params by index and `Param::setValue` does not clamp, so an old patch would write
`GENRE`'s 4 into a 0..1 gain. Dismissed on the owner's call: Glow shipped as a
dev alpha, no patches containing it exist, and the repo's standing rule is that
alpha patches break freely. No version marker, no migration.

**Raised and overruled: scope and timing.** Review argued this work should be
additive — a second `Model` over the same `Module`, following
`FireflowHW` (`Fireflow.cpp:1963`) — and that it should wait behind parent §10's
four curation deliverables, none of which is begun, since parent §11 calls
curation the only real threat to December and parent §5.3 says stage 2 falls
"without new software". The owner chose the replacement, on the grounds that
Glow is a dev alpha nobody has used. The argument is recorded here rather than
in a comment thread, because if December gets tight this is the first place to
look for the hour that was spent.
