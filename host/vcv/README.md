# FireFlow — VCV Rack host

A third host over the shared portable engine (`engine/`), alongside the desktop
render host (`host/render/`) and the future Daisy firmware shell (M6). **No
engine code is duplicated**: this plugin compiles the exact same `engine/*.cpp`
sources against the VCV Rack SDK.

```
host/vcv/
├── Makefile              Rack plugin build (points SOURCES at ../../engine)
├── plugin.json           Rack plugin manifest
├── src/
│   ├── plugin.{hpp,cpp}  plugin entry + model registration
│   ├── Fireflow.cpp      Module: param→engine mapping, process(), widget
│   └── generated_panel.hpp   GENERATED (enums + control table)
└── res/
    ├── gen_panel.py      single source of truth for the panel layout
    └── Fireflow.svg      GENERATED faceplate
```

## Design: one control per function (VCV-native)

The hardware Spotykach drives its ~60 parameters through capacitive pads +
hold/combo gestures (a layer that is firmware milestone **M6**, not yet built).
VCV has no clean equivalent for pad gestures, so this host deliberately maps
**every engine setter to its own dedicated knob/switch/button** — a wider panel
than the hardware, but the full engine is playable today without waiting on M6.

The visual language follows the residency devlog ("workbench paper"): a warm
paper plate with ink lettering, solder-green accents on part A (left) and
copper-orange on part B (right); MORPH wears a split green/copper collar
because it bridges the two parts. Each part's
32-LED ring is a **live** custom widget (`SpkyRing` in `src/Fireflow.cpp`): it
draws in the light layer and lights a moving dot per modulation lane from
`Instrument::lane_output()` / `lane_fired()`, so the rings animate with the
engine (mirroring `src/ui/led.ring.h`). The SVG only provides the dim housing.
The shared centre column beside MORPH also carries **PUSH**, the master
drive into the output limiter — one knob for both parts, unlike every
per-part control above.

## TIMING

**SHUFFLE** is one shared control for both parts: `0` is a straight grid and
full travel gives a classic `2:1` long/short pair. It warps only STEP timing;
FLOW stays straight. Live changes latch at each lane's next pair boundary so
the active pair finishes intact, while external CLOCK pulses, resets, phrase
downbeats, and the transport's raw-phase anchors stay straight.

## SONG

Each Part's PLAY row reads **STEP · SONG**. FORM and the NEW pad are gone
(spec 2026-08-09 hw-control-reduction): SONG alone walks a curated 14-rung
ladder through the (Principle, SongMode) grid and re-rolls the phrase on
every rung change — turn the knob, the melody develops differently.

Every rung pairs one of five phrase engines with one of seven arrangements.
The five phrase engines are **TWO MOTIFS**, **ONE + VAR**, **HIERARCHICAL**,
**CALL / RESPONSE**, and **OSTINATO** — they create the musical material.
The seven arrangements decide when the two persistent phrase snapshots A and
B are heard:

- **AAAB**, **ABAB**, and **ABBB** repeat their named four-phrase sequence.
- **BUILD** follows `AAAB · AABB · ABBB · AABB`.
- **ROTATE** follows `AAAB · AABA · ABAA · BAAA`.
- **MIRROR** follows the deterministic, non-repeating Thue–Morse A/B stream.
- **OFF** disables arrangement by playing A continuously; A still evolves,
  while the stored B phrase is retained for returning to another rung.

The factory SONG setting is the ladder's first rung. SONG changes wait for
the next STEP phrase boundary, and every rung change queues a fresh A/B pair
and restarts the arrangement at its first phrase — the gesture the retired
NEW pad used to fire on demand. On a Sampler it also jumps the tape head back
to ORGANIZE and spawns a grain immediately.

## SOURCE and Detune

Each part has one physical **SOURCE** control. Its live caption follows the
selected ENG — see [Engine-dependent captions](#engine-dependent-captions)
below for the full table across all five engines. SOURCE-lane modulation
moves the selected source around the knob's base value.

Each part's right-click context menu provides a separate **Detune A** or
**Detune B** control. Detune is independent of SOURCE: it is a constant
`0..35 ct` spread and defaults to `6 ct`.

Each part's context menu still carries the append-only **Drive A** / **Drive B**
patch state. Movement 3 no longer routes it into FLUX; it remains present only
so saved parameter IDs do not move.

## Engine-dependent captions

Seven controls change their caption with the deck's `ENG`. The words live in
`DYNAMIC_CAPTIONS` in `res/gen_panel.py` and reach both the SVG and Rack from
there — the C++ holds no caption word at all.

| Control | Synth | Sampler | Wave | Body | BBD |
|---|---|---|---|---|---|
| MELODY | `VARY` | `SCAN` | `VARY` | `VARY` | `VARY` |
| ATTACK | `ATK` | `ATK` | `ATK` | `HIT` | — (`BEND` occupies the slot) |
| DECAY | `DEC` | `DEC` | `DEC` | `DAMP` | `TAIL` |
| RES | `RES` | `RES` | `RES` | `CHAR` | `TILT` |
| SUB | `SUB` | `LEN` | `SUB` | `EXCIT` | `INPUT` |
| FILT | `FILT` | `FILT` | `FILT` | `BRITE` | `LOSS` |
| SOURCE | `TIMB` | `ORG` | `FRAME` | `MATL` | `DRIVE` |

GRIT itself is bipolar (spec 2026-08-09 hw-control-reduction task 4): there is
no separate mode pad any more. The knob's sign picks the mode -- left of
centre is Reduce (bit-crush), right of centre is Drive (saturation) -- and its
magnitude is the mix. A small dead zone around centre gives "off" a reachable
resting spot on a real pot.

`REC` is drawn only on a Sampler deck; it has never done anything on the other
four.

## Sampler

ENG is patch-compatible in the exact order **Synth = 0**, **Sampler = 1**,
**Wave = 2**, **Body = 3**. States 2 and 3 select the portable wavetable and
resonator engines and are never treated as Sampler. Patches saved before Body
existed store only 0..2, so their meaning is unchanged. The context-menu
test-tone development override remains limited to state 1; it cannot replace
Wave or Body. The latch has no halo for Synth, a warm halo for Sampler, an
ice-blue halo for Wave, and a green one for Body.

**ENG** (per part, latched) selects **Synth**, **Sampler**, **Wave**, or
**Body**. **Body** is a resonator that morphs from plucked string through
prepared piano to struck bell; its excitation sources — the deck's own FLUX
echo, the other deck, and the audio input — are checkboxes in the same
context menu.

**The "other deck" checkbox (menu item `Route: other deck (BODY excite,
SAMPLER feed+rec, BBD feed)`) is not Body-only.** It also drives the
audio-rate cross-deck bus (spec 2026-07-31 bbd-part-engine): the engines
that consume that bus today are **Sampler** and **BBD**, so turning this on
for either deck audibly routes (and, for Sampler, records) the neighbouring
deck's output — where on earlier releases the same checkbox did nothing for
either. A patch saved with it on for a Sampler or BBD part will sound
different after updating to a build with the cross-deck bus. It also has a
second, easy-to-miss effect regardless of whether the neighbour is making
sound: the external **audio input** is summed with the neighbour's tap and
the sum is soft-clipped (`fast_tanh`) before reaching the engine, so
enabling this checkbox puts the deck's own audio-in monitoring/recording
through that same soft clip — audibly softer than the normal dry-at-unity
monitor, even with a silent neighbour. Neither behaviour is a bug; it is
what routing two decks' audio through one shared flag necessarily does. A
**BBD** deck defaults this checkbox ON the moment ENG lands on BBD (see
below) precisely so it always has something to echo.

**Sampler** is a granular texture deck over the shared record buffer. Flipping ENG to
Sampler on an empty part autoloads the embedded first four bars of the
project author's own 110 BPM bass loop (`res/factory.wav`) so the deck makes
sound on the very first gesture; it never overwrites content already in the
buffer, and a deliberate *Clear sample* stays cleared even if you flip ENG
back and forth. On a Synth, Wave or Body part, ENG is the only mode control — REC is inert
there.

**REC** (per part, latched) records from **IN L/R** into that part's buffer
while the sampler is free to keep playing what it already has (fill-follows:
the granular cloud reads whatever's been written so far, so the deck never
goes silent while filling). Starting a recording clears any remembered
sample path/factory flag — once REC has touched the buffer, its content no
longer matches a file on disk or the factory sample, so *Save sample…* is
the only way to keep it. The REC LED has three states: **pulsing** (2 Hz)
while recording, a **steady brightness proportional to fill level** once the
part holds content and isn't recording, and **dark** when the part is empty
or on a non-Sampler engine — the light tracks ENG, not leftover buffer state, so switching
a part away from Sampler doesn't relight it.

**Four controls take on a different job the moment ENG says Sampler.**
The parameter IDs don't change — for the hardware this is a merge of existing
controls, not a new set of them — only what turning the knob does:

| Control | Sampler caption | Sampler meaning | Range |
|---|---|---|---|
| MELODY | `SCAN` | tape-head advance; the deck's VARY job is parked at LOOP here | centre is a true dead zone; linear out to real time at three-quarters of travel, then linear up to 4×; sign is direction |
| DENSITY | `DENS` | groove density *and* grain overlap — one word because both mean sparser | 1…8, continuous; the MOTION lane modulates around it |
| SUB | `LEN` | grain length | 1 ms…42 s |
| SOURCE | `ORG` | read position in the material | full material length |

MELODY drove variation and scan at once until 2026-08-03. It now drives scan
alone on a Sampler deck, so the deck's phrases stop renewing by themselves —
a `SONG` rung change is the gesture that asks for a fresh pair.

SCAN's dead zone is exact and deliberate: a frozen tape head has to stay
frozen even through knob noise, so nothing moves for the first couple of
percent off centre. From there it ramps in gently and linearly, and real
time lands on a fixed, refindable knob position three-quarters of the way
out rather than somewhere you have to hunt for — the last quarter is the
steepest stretch of the curve, carrying the head up to four times real time
in either direction.

**SCAN springt beim ENG-Flip sofort auf die Knopfposition — offene Frage
(F-07, Review 2026-07-22).** VARY trägt im Synth VARIATION und im Sampler
SCAN, und die Init-Werte stehen für VARIATION an den Extremen (−0.728 und
−1.0). Als SCAN gelesen sind das −0.97× und −4× Realtime rückwärts: der
erste Flip auf Sampler lädt die Factory-Drone und schickt den Lesekopf im
selben Control-Tick rückwärts los, praktisch schon bei Realtime, ohne dass
jemand etwas angefasst hat. (Seit 2026-08-03 parkt die VARIATION-Hälfte
selbst bei LOOP, sobald ENG auf Sampler steht — SCAN liest aber weiterhin
direkt dieselbe rohe Knopfposition, unabhängig von diesem Park, also bleibt
das hier beschriebene Verhalten unverändert.)

Das ist als Fehler gemeldet worden, ist aber genau das Verhalten, das
"Known limitations" weiter unten bewusst wählt: Knopfposition gilt über den
Engine-Wechsel hinweg, ohne getrenntes Gedächtnis und ohne Soft-Takeover,
weil die Hardware kein Soft-Takeover hat. Ein Soft-Takeover wurde gebaut und
wieder zurückgenommen — über Patch-Laden hinweg dicht zu bekommen verlangt
persistenten Zustand, also genau das, was diese Zeile ausschließt. Die
Entscheidung liegt beim Autor des Instruments, nicht in der Engine.

Mit derselben Änderung ging eine stille Last weg (K-03): `sampler_scan()`
wurde für **beide** Decks aufgerufen, auch für ein Synth-Deck, und
`scan_rate()` enthielt im unteren Zweig ein `std::pow`. Bei `ctrlDiv = 16`
waren das bis zu 6000 `pow`-Aufrufe pro Sekunde im Audio-Callback für eine
Engine, die niemand hört. Der Aufruf hängt seither an `samplerPart`, weil
`sampler_scan()` nur auf einem Sampler-Deck eine Wirkung hat. Die untere Zone
ist inzwischen linear (spec 2026-07-23 sampler-performance-fixes), also gibt
es dieses `pow` gar nicht mehr — das Gate bleibt trotzdem: außerhalb des
Sampler-Decks wird `_scan_rate` nie gelesen, und hineinzuschreiben wäre nur
Arbeit ohne Wirkung.

**A SONG rung change also fires "new grain now" in the Sampler:** the tape
head snaps back to ORGANIZE's position and a fresh grain spawns immediately,
in addition to the fresh phrase-pair request described above. This exists
because a grain's position, pitch and length are frozen the instant it's
spawned, and the next chance to change any of them is the next scheduled
spawn — at overlap 1 and a long LEN that's up to ten seconds away. Without
this gesture, the long end of LEN wouldn't be a playable state at all; the
deck would just stop answering every knob for that stretch.

**LEN is live downward, latched upward.** Turning LEN *down* immediately
rescales every grain that's already sounding to the length it would have got
at the new setting, fading it out click-free; turning LEN *up* leaves running
grains exactly as they are. The asymmetry is the point. Length is latched at
spawn (see the SONG rung change above), and that used to apply in both
directions: a grain
spawned at the top of LEN sounded for its full 42 s however far the knob came
back down — 84 s in Tape with a pitch an octave under — and nothing on the
deck could stop it, since the only thing that releases a running grain is the
*other* part's CHOKE window. LEN effectively had a settling time of up to 42 s
during which the knob and what you heard disagreed. Downward-live fixes that
without touching the deliberate lag on the way up, so the cloud still drags
behind a rising lane. In Tape the rescale is proportional, so "low notes smear
long" survives it rather than being clipped to LEN.

**The tape head shows up on the LED ring** as a bright travelling dot,
as soon as a part is in Sampler and has material in its buffer.

**Pitch holds still in the Sampler.** The PITCH lane is switched off there;
tuning happens exclusively through TUNE, the bipolar ±18-semitone transpose
shared with the Synth's scale grid. The point is that a Dorian sample on one
deck and a Dorian-played Synth on the other land in the same key. Rhythmic
triggering through STEP survives this untouched — the lane keeps firing on
step boundaries, it just stops moving pitch while it does.

**SUB stays a sub-level control; SOURCE carries the contextual source job.**
On a Sampler, SOURCE reads `ORG` and selects position in the material; on
Synth and Wave it reads `TIMB` and `FRAME`. Detune A/B stays an independent
per-part context-menu spread for Synth and Wave, not a visible `DTUN` knob.

The right-click context menu carries a **Sampler A / Sampler B** submenu per
part:
- **Load sample…** / **Save sample…** — WAV import/export via a file dialog.
- **Clear sample** — empties the buffer and forgets any remembered
  path/factory flag.
- **Speed mode** — Tape (default) or Digital. Tape couples speed and pitch
  like varispeed; Digital repitches grains at unchanged grain duration.
- **Reverse** — plays the buffer backwards.
- **Overdub feedback** — how much of the existing buffer content survives
  under a new recording (a slider, not a toggle).
- **Engine: test tone (dev)** — a leftover development aid; with it set,
  ENG's second position plays a test tone instead of the sampler. Not meant
  for normal patches.

A recorded or loaded texture **survives patch save/reopen**, but not through
`dataToJson`/`dataFromJson` — those only carry the sample path, speed mode,
reverse, feedback and a couple of internal flags (`factory`, `factoryTried`).
`factoryTried` in particular is what makes a deliberate *Clear sample* stay
cleared through save/reopen: it is persisted intent, restored verbatim from
JSON, and nothing after that point overwrites it back to false — only Rack
*Initialize* (`onReset`) resets it, letting the factory sample autoload
again.
The audio itself goes through Rack's **patch storage** instead: `onSave`
writes any part that didn't come from a file or the factory WAV out as a WAV
into the patch's own storage directory, and `dataFromJson`/`onAdd` reload it
from there on reopen (a part whose content DID come from a file or the
factory sample reloads from that source instead, so nothing is written for
it). Every part that this save does *not* write its own stored WAV for —
because it now has a file path, is factory-loaded, or has nothing recorded —
also has any leftover stored WAV from an earlier save deleted, so neither a
deliberate *Clear sample* nor loading a file over an old recording leaves a
stale WAV sitting in patch storage.

### Known limitations

- **Grain density is capped, and that caps tape's downward smear too** (since
  2.8.0). MOTION jitters the spawn interval by ±75 % while grain length stays
  fixed, so short intervals used to stack grains — the cloud reached 11 live
  grains where DENS had asked for 8, and the cost of an audio block is linear
  in that number. `kSpawnHeadroom` (`engine/sampler/sampler_config.h`) now
  caps it at `DENS + 2`.

  Two things you can hear. At DENS near maximum the densest moments are
  slightly thinner than before — a few percent of spawns are dropped there,
  and none at all at ordinary settings. And in **Tape** mode, transposing down
  stops making grains longer past roughly four semitones: a long tape grain
  *is* density, so the same ceiling bounds both. The value is an ear decision
  and the trade table sits at the constant; raise it if you want the smear
  back.

- **Knob position holds across engines — there is no separate memory and no
  soft takeover.** This is on purpose: it's the one behaviour VCV and the
  eventual hardware can share exactly, since the hardware has no
  soft-takeover to fall back on. The price is that an ENG switch can't be
  prepared in advance: SOURCE keeps its knob base, so a `TIMB`/`FRAME` setting
  becomes the initial `ORG` setting after the switch. SUB remains sub-level,
  while Detune A/B stays independent in the context menu; neither is
  repurposed by SOURCE. Fine for a staged transition on stage; not for a
  seamless one.
- **There are no parameter CV inputs.** The jacks are IN L/R, CLOCK and
  RESET; PIT and GAT are outputs. External modulation of these controls only
  reaches VCV through third-party mapping modules.
- **A sample-rate DROP silently truncates the recording's tail.** The record
  buffer is sized in frames (42 s × the engine's sample rate), so switching
  your audio device from 48 kHz down to 44.1 kHz shrinks that allocation and
  loses roughly the last 1.2 s of a full 42 s buffer. This is safe — the
  engine clamps every read to the smaller capacity — but nothing in the UI
  warns you it happened.
- **A sample-rate CHANGE does not resample buffer content, but a file LOAD
  does.** If you already have material recorded or loaded into a part and
  then change your audio device's rate, that buffer plays back transposed at
  the new rate — this is deliberate tape behaviour, the same as changing tape
  speed. A fresh *Load sample…*, by contrast, always resamples the WAV to
  the engine's current rate before writing it into the buffer, so an
  imported file is always in tune. The asymmetry is intentional: importing a
  file at the wrong pitch would be a bug, but re-rating material that's
  already sitting in the buffer is varispeed, not a bug.
- **FLUX is a stereo tape echo.** `TIME` is a 12-detent knob over its
  synchronized division; `SEND` (per part) sets how much of the result
  reaches the shared reverb. `TIME` used to sit beside a free `MULT`
  multiplier (spec 2026-08-09 hw-control-reduction task 6: DIV and MULT
  described one quantity, so one notched knob now does it). MULT's
  modulation sink, a x0.25-to-x4 tape-time multiplier that reaches each new
  setting through a 30 ms slew for tape/Doppler motion instead of a stepped
  change, still exists in the engine at a pinned neutral base (`x1`) — CV
  and the mod lanes can still bend the tape, just not from a second panel
  knob.
- **LINK (per part) is THIN across its full travel.** It lets the other deck's
  rhythm thin FLUX's repeats without handing over the echo clock. Patches from
  the bipolar LINK era migrate automatically: old negative THIN settings keep
  their depth, while old positive DRAG settings load at `off`.
- **DRIVE and STAGES no longer voice FLUX.** Their append-only parameter IDs
  remain stable for old patches. STAGES is used only as the PITCH-lane base
  while a deck is in BBD.
- **Memory:** each `Fireflow` instance allocates well over its two 42 s
  stereo record buffers up front, whether or not the sampler is ever used on
  either part — closer to **~38.66 MB total**, not the 32 MB the record
  buffers alone account for:
  - ~32 MB — the two 42 s stereo sampler record buffers (`samplerMem`).
  - ~131 KB — the per-part stereo echo buffers the FX chain requires
    (`echo[]`: `2 × 2 × 8192` floats = 131,072 B), unrelated to the
    sampler.
  - ~130 KB — the shared reverb (`AmbientReverb`).
  - ~6.4 MB — the factory sample cache (`factoryNative`, decoded once in
    `onAdd()`, plus the rate-converted `factoryL`/`factoryR`, rebuilt in
    `reinit()`), held for the module's lifetime regardless of whether ENG
    is ever flipped to Sampler on either part.

## BBD

ENG's fifth position (**BBD**, state 4) selects a stereo bucket-brigade
delay engine — a self-contained sound source, not to be confused with
**FLUX**, the unrelated per-part echo effect present on every engine,
including this one. FLUX was itself a bucket-brigade model for a while;
movement 3 returned it to a stereo tape echo (`Flux` holds two
`TapeEcho`s, `engine/fx/flux.h`), so the two are no longer even the same
kind of delay — see "FLUX is a stereo tape echo" under Known limitations.
Old patches only ever stored ENG 0..3, so this is purely additive.

On a BBD deck the five modulation lanes take on the engine's own controls:
**SOURCE** drives DRIVE (the loop's internal saturation), **MOTION** drives
FEEDBACK, **LEVEL** sets the wet/dry mix between the deck's input and the
delay's return (unlike every other engine, LEVEL 0 does not silence a BBD
deck — it passes the input through dry, at unity), **PITCH** sets the delay
clock (subject to the FEEDBACK caveat below), and **SIZE** picks the
delay-time rung the same way FLUX's own TIME division does.

### BBD BEND and the tape multiplier

The upper-left **VOICE** slot is engine-aware: **BBD** uses `BEND` there,
while Synth, Sampler, Wave, and Body keep `ATK` in the same position. The
saved `STAGES_A/B` patch state remains the BBD BEND value; the visible `STGS`
label is gone and its faceplate caption changes with the selected engine,
reading `BEND` on a BBD deck. BBD's hidden ATTACK value remains
available as **BBD A — Freeze Attack** or **BBD B — Freeze Attack** in the
module context menu.

The FX bottom row is `LINK · GRIT COMP`; it contains no BBD control and, as
of task 6 (spec 2026-08-09 hw-control-reduction), no MULT either — that slot
is now empty. `TIME` selects the synchronized tape division. The engine still
carries a MULT-shaped modulation sink underneath it (`FXT_FLUX_TIME`):
multiplying that division from `x0.25`, through neutral `x1`, to `x4`, its
intentional 30 ms slew gives smooth tape/Doppler motion, but the panel now
pins its base to `x1` and only CV/mod lanes can move it — there is no longer
a knob for it. At the longest divisions, the existing delay-buffer limit
still clamps the absolute delay.

One panel control changes meaning:
- **SOURCE**'s live caption reads `DRIVE` instead of `TIMB`/`ORG`/`FRAME`/`MATL`.

The append-only **STAGES** control no longer voices FLUX. It is used only on a
BBD deck, where it supplies the PITCH lane's base — the same "re-point an
orphaned knob" pattern the Sampler uses to turn SUB into grain `LEN`.

The BBD control mapping gives **ATTACK** the freeze's engage/release time
(through the **Freeze Attack** context-menu slider), while **DECAY**
(captioned `TAIL`) trims the tail below unity so a frozen loop still runs
down instead of holding forever, **RES** (captioned `TILT`) tilts the
feedback path's brightness, **SUB** (captioned `INPUT`, and the one BBD
control whose tooltip reads as a percentage — the knob *is* the gain, so
40 % means 40 % of what arrives) sets how much signal (tape / other deck /
audio-in) actually reaches the delay line, **FILT**
(captioned `LOSS`) sets the loss-pole corner (the line's own damping/tone),
and **SOURCE** (captioned `DRIVE`, see above) sets the saturation.

The moment ENG lands on BBD (the switch *edge*, not every control tick, so
it never fights a player who changes these back): **FLUX defaults off** —
the BBD's own signal path already runs through six poles at 3600 Hz plus a
loss pole breathing under a compander, and its gappy repeats are its most
distinctive trait, which a FLUX echo behind it would otherwise smear — and
the **"Route: other deck" checkbox defaults on** (see above), so a BBD deck
with no external cabling still has the neighbouring deck's output to feed
its line instead of silence.

Three consequences that would otherwise read as bugs:
- **PITCH is inaudible at FEEDBACK 0.** A bucket-brigade delay writes and
  reads on the same clock, so the very first pass through the line is
  always at unity pitch; only a signal that recirculates (FEEDBACK above
  zero) samples the moved clock more than once, which is what actually
  makes a pitch bend audible. With FEEDBACK at zero, turning PITCH changes
  nothing you can hear — that is the physics of a single unity-pitch pass,
  not a stuck knob.
- **ATTACK and DECAY are inert in FLOW.** Both shape the freeze, and the
  freeze is a STEP-only gate — FLOW ignores the gate entirely and free-runs,
  so there is no freeze for either control to shape. This is an
  owner-confirmed decision, not a bug: switch to STEP to hear either control
  do anything.
- **In FLOW, SIZE is also a pitch gesture.** The clock is re-derived from
  the reachable window every control tick — which is also what gives PITCH
  its full travel at every division, with no dead zone — and that window's
  bounds move with the delay time. Moving SIZE therefore bends whatever is
  circulating, the same way moving PITCH does, even if PITCH itself never
  moved: at a fixed PITCH setting, taking SIZE from a 2 s repeat down to a
  0.25 s repeat has been measured moving the clock by close to 3 octaves.
  This is a real bucket-brigade delay's character, not a bug — its time
  knob and its clock are the same knob. In STEP this does not happen
  between fires: the clock only re-derives on a fire, so SIZE moves the
  rhythm there and leaves the pitch alone.

## FireFlow Glow — the second module

The plugin ships a second module alongside the one described above: **Glow**,
12 HP, six macro knobs, one button and two small switches over a generated
terrain, driving the same
portable engine through `engine/flow/` instead of through `Fireflow.cpp`'s
one-control-per-parameter mapping. Where the big module is the full-control
view — every engine setter on its own knob — Glow is the flow-machine view:
one seed generates an entire patch (both decks, every FX send, the tempo),
and the six knobs ride curated macro curves over it rather than individual
engine parameters. Both modules embed the exact same `Instrument`; nothing in
`engine/` is duplicated a second time for Glow, and the two never share
state — they are two independent instances of the same engine core.

### The six macros

| Knob | Meaning |
|---|---|
| **MOTION** | how much everything moves |
| **DENSITY** | how much happens |
| **BRIGHT** | spectral centre |
| **DIRT** | clean ↔ driven |
| **WANDER** | predictable ↔ wandering |
| **SPACE** | close ↔ vast |

Each knob's exact targets are the current terrain's choice — a fresh NEW
press can rewire what MOTION touches — but the one-word meaning above always
holds, and every knob is monotone: more knob is always more of that thing.

**The calm corner.** All six knobs fully counter-clockwise is a defined quiet
background on every terrain, not an accident of wherever the mapping curves
happen to bottom out — the generator is required to land there. It is the
module's gas pedal: pulling any subset of knobs down recedes the instrument
toward that corner, and turning any of them up adds a specific kind of energy
on top of it.

### NEW — one button, four gestures

All timing thresholds below are `engine/flow/taste.h` constants, not
approximations:

| Gesture | Action | LED |
|---|---|---|
| tap (release before 0.4 s, no knob turned) | full NEW — rolls a fresh terrain and blends into it over ~6 s | breathes through the blend |
| hold + turn a macro knob | marks that macro's domain; release fires a partial reroll of the marked domain(s), leaving everything else (tonality, roles, pace) untouched | flickers |
| hold past 1.5 s, no knob turned | arms undo; release blends back to the one remembered previous terrain | double-pulse, reversed feel |
| hold past 5 s, no knob turned | toggles terrain lock — tap/hold gestures refuse until unlocked the same way | solid while locked |

Turning a knob during a hold always wins: it cancels a still-arming undo or
lock timer and turns the hold into a partial-reroll gesture from that instant
on, however long it continues to hold. Any gesture the decoder allows but
`Flow` itself declines — an undo with no previous terrain to return to, a
partial reroll with an empty marked mask — still lights the module's own
short refusal flash; the decoder's own refusal LED covers only the
locked-press case; this is a separate flash the module runs itself when the
decoder let a press through and the engine layer turned it down anyway.

### GENRE and SCALE — the two switches flanking NEW

NEW is a slot machine, and these two switches are what let you stop it
spinning in one direction at a time. Both sit either side of the button:
GENRE to its left, SCALE to its right.

**GENRE** constrains which archetype the *next* NEW press may land in — ANY,
DRONE, PULSE, ARP or FRAGMENT. Off ANY, NEW keeps rolling until it finds a
terrain of that kind, and hands you the most different one it saw. It changes
nothing about the terrain you are on: turning GENRE moves no parameter and
makes no sound, which is the point — it is a constraint on the draw, not on
the instrument. Auditioning ten drones one after another is what it exists
for, and without it you cannot: the draw's own rules push every press into a
*different* archetype. Which archetype you are currently in is shown in the
right-click menu, next to the terrain code.

**SCALE** is the opposite kind of control: it takes effect at once. AUTO
leaves the terrain's own drawn scale alone; any other position pins the scale
outright and holds it across NEW presses, partial rerolls and blends, until
you return to AUTO — at which point the current terrain's own scale comes
back. The travel runs calm → sharp: the two pentatonics first, then the
seven-note modes, then the hirajoshi group, then the exotics. **ROOT** is the
same idea for the key and lives in the right-click menu rather than on the
panel — Auto, or one of the twelve.

Neither switch joins Rack's **Randomize**, because the leftmost position
already *is* the random setting: ANY draws the archetype at random and AUTO
takes whatever the terrain drew.

Rack's own **Initialize** (right-click → Initialize, or Ctrl+I) returns Glow
to the house terrain, clears the lock, and puts GENRE, SCALE and ROOT back to
Any / Auto / Auto — the same state a fresh insert starts in.

### Jacks

| Port | Meaning |
|---|---|
| CV MOT / DEN / BRT / DRT / SPC | 0–10 V, additive onto the matching knob, clamped into range after summing |
| CLK | one pulse per beat; overrides the terrain's own tempo while pulses keep arriving, and falls back to the terrain's tempo after about two seconds of silence on the input |
| OUT L / R | main mix |

WANDER has no CV jack and there is no RST jack — both by design, not
oversights. WANDER's CV was left for a future full-control expansion; RST's
slot went to CV SPACE instead, because a generative drone box has no obvious
meaning for "reset" and the jack existed on the big module mostly by
Eurorack convention.

### Terrain codes

A terrain's whole state — the master seed plus, once any partial rerolls
have happened, one small counter per macro — is a short string like
`F1-DEADBEEF-000100020000`. The code is the terrain's whole identity —
nothing about the *place* lives anywhere else — so copying the string out and
pasting it back in later (or into another instance) puts you back on the same
terrain. It is not quite the whole patch any more: a SCALE or ROOT override
rides on top of the terrain and travels in the saved patch rather than in the
code, so someone you send a code to lands on your terrain but hears it in the
key the terrain itself drew. Say which override you were on, or send the
patch. The
right-click context menu shows the live code, offers **Copy terrain code**
and **Paste terrain code**, and carries an editable text field for typing one
in by hand. Pasting or typing a malformed code is a no-op — it changes
nothing, rather than moving the player somewhere arbitrary; the same rule
protects a saved patch whose stored code is corrupt, which falls back to the
house terrain instead.

### Generated panel

`res/Glow.svg` and `src/generated_flow_panel.hpp` are both produced by
`host/vcv/res/gen_flow_panel.py` — the same one-script-drives-both approach
`gen_panel.py` uses for the big module's panel — and guarded by
`res/test_flow_panel.py`. Neither generated file is ever hand-edited; change
the control table in the script and regenerate.

### The house seed

The terrain Glow wakes on at first insert (and after Initialize) is
`kHouseCode` in `engine/flow/taste.h`. It is currently a **measured
placeholder, not a curated choice** — the render-based listening pass meant
to choose it by ear was stopped, because a bounced file can only judge
level, not whether a terrain is worth arriving on, and answering that
question properly needs the very module this section documents. Its code
comment says the same thing; re-choosing it by ear on the finished module is
still open.

## Build

Requires the [VCV Rack SDK](https://vcvrack.com/manual/Building#Setting-up-the-Rack-SDK)
(v2), plus `make` and `jq`. The Makefile's default `RACK_DIR` is `../../../Rack-SDK`
(i.e. unzip the SDK next to the repo), or pass `RACK_DIR=/path/to/Rack-SDK`.

The shared engine is **C++17** (`std::clamp`, ...), but the SDK defaults to
`-std=c++11`; the Makefile bumps it back up via `EXTRA_CXXFLAGS += -std=c++17`,
so nothing extra is needed on your end.

```bash
# from this directory:
make            # -> plugin.dll / .so / .dylib
make install    # packages a .vcvplugin and copies it into Rack's user plugin dir
```

`make install` drops `Fireflow-<version>-<arch>.vcvplugin` into Rack's user dir
(`%LOCALAPPDATA%\Rack2\plugins-win-x64\` on Windows, `~/.local/share/Rack2/…` /
`~/Library/Application Support/Rack2/…` elsewhere); Rack unpacks it on launch.
Restart Rack and the module appears under the **ton-k** brand
("FireFlow" in the module browser). A self-built plugin is unsigned, so Rack may
note it isn't from the library — it still loads.

**The version in `plugin.json` must start with `2`.** Rack reads that first
component as the plugin's ABI version and refuses anything that isn't its own,
so a `3.x.y` plugin is skipped at startup with no dialog and no module in the
browser — only a `warn` line in `log.txt` saying the version "does not match Rack
ABI version 2". This bit us for real: v3.0.0 was tagged and published as an
unloadable release. The major number belongs to Rack, not to us; mark our own
milestones in the minor (`2.19` → `2.20`).

**Windows: check where `install` actually copied to.** The SDK builds the
target path from `$(LOCALAPPDATA)`, and MSYS2/Git-Bash don't export that name
into the make environment — the variable expands empty and the package lands in
`C:\Rack2\plugins-win-x64\` instead, with `make` reporting success either way.
Pass the directory explicitly:

```bash
make install RACK_USER_DIR="$LOCALAPPDATA/Rack2"
```

The DaisySP submodule must be present (the engine's FX depend on it):

```bash
git submodule update --init lib/DaisySP    # run from the repo root
```

### Windows toolchain note

Rack plugins are native GCC/MinGW builds, so you need an **x86_64 MinGW-w64**
compiler (e.g. [WinLibs](https://winlibs.com/), or MSYS2's
`mingw-w64-x86_64-gcc`) — the MSVCRT variant matches Rack 2. Point `make` at it
with `CC=gcc CXX=g++` on the MinGW `bin/` in `PATH`.

Build from an **MSYS2 shell** (`pacman -S make jq`). This Makefile lists the
shared engine via absolute `$(REPO)/…` paths so the `.o` files stay in `build/`;
a *native* `make`/`mingw32-make` then trips over the `C:` drive colon
("multiple target patterns"). An MSYS2 `make` sees the paths as `/c/…` and
builds cleanly.

If you build from a **Git-Bash** shell instead of a real MSYS2 login shell, gcc
may die with `Cannot create temporary file … Permission denied` — the recipe
shell inherits `TMP=C:\WINDOWS`. Pass a writable temp dir and a POSIX shell:

```bash
make CC=gcc CXX=g++ SHELL=/usr/bin/bash \
  TMP="$LOCALAPPDATA/Temp" TEMP="$LOCALAPPDATA/Temp" -j4
```

From a normal MSYS2 shell you can drop those overrides.

## Regenerating the panel

`generated_panel.hpp` and `Fireflow.svg` are both produced from one script so
the graphics and the widget positions can never drift:

```bash
python3 res/gen_panel.py     # run from host/vcv/
```

Edit the control table in `res/gen_panel.py`, re-run, rebuild.

### The README hero image

`docs/img/fireflow-panel.png` is a raster of that same SVG, and **nothing
regenerates it automatically** — it silently went two caption rounds stale once
already. After a panel change, re-render it. Any browser's headless screenshot
does the job faithfully, including the knob-cap gradients that pure-Python SVG
rasterisers drop:

```bash
# wrapper keeps the exact pixel size; the SVG's own units are mm
printf '<style>html,body{margin:0;overflow:hidden}img{display:block;width:1640px;height:988px}</style><img src="Fireflow.svg">' > res/_r.html
<your-browser> --headless=new --hide-scrollbars --window-size=1640,988 \
  --screenshot=../../docs/img/fireflow-panel.png res/_r.html
rm res/_r.html
```

1640 × 988 is twice the 820 px the README displays it at.

## I/O

| Port | Meaning |
|------|---------|
| IN L/R | audio in (feeds the per-part FX chain and, while REC is latched on a Sampler part, that part's record buffer; optional) |
| CLOCK | one pulse per beat → sets tempo (overrides the TEMPO knob) and phase-aligns the transport on each pulse |
| RESET | resets the transport downbeat (bar/beat phase) |
| OUT L/R | main mix |
| PIT A/B | per-part pitch modulation CV (±5 V, not strict V/Oct) |
| GAT A/B | per-part gate (10 V) |

## Verifying against the reference engine

The engine is unchanged, so its regression suite still applies:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # from repo root
cmake --build build
ctest --test-dir build --output-on-failure   # Debug fails the render-hash gates
```

For an A/B sound check, render a known scenario with the desktop host and
compare by ear against the module at identical parameter settings:

```bash
./build/render host/render/scenarios/demo_step_melody.json out.wav mods.csv
```
