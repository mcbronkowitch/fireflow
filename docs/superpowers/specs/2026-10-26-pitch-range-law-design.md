# The pitch RANGE law: a width around a fixed centre

Date: 2026-10-26. Status: **accepted** by Bastian on 2026-10-26 (*"Ok
passt"*), written without a question round at his request (*"versuch mal ohne
mich die specs zu schreiben ich schaue sie mir zum schluss an"*). He accepted it
without ruling on §6, so both defaults stand: **D1 = semitone 18**, **D2 = no
floor**. Follows: spec 2026-08-14
`melody-reachable` §2.2 and the roadmap's "Open thread — the pitch RANGE law".

## 1. Intent

Bastian, 2026-10-26, with the Rev A board running: *"ich bekomme keine richtigen
melodien mehr aus dem song mode. Die melodien bleiebn meistens auf dem gleichen
ton und selten kommt es zu melodien."*

The cause is the RANGE law on the PITCH lane. `apply_range` is an amplitude law,
and on the pitch axis it does two things at once: it sets the melody's width,
and it lifts the melody's centre — by 18 semitones at the knob's centre, which
puts the melody on the top of the 36-semitone span. Half of every phrase is then
clamped onto one note, the root. The centre of the RANGE knob is its worst
position.

This spec separates the two. On a deck that plays notes, RANGE becomes the
melody's width around a centre that does not move with the knob, and the pitch
chain reflects a note that leaves the span instead of clamping it onto the root.
The width at every knob position stays exactly what it is today.

A second, independent way to the same symptom turned up while this design was
checked: a deck that visits the BBD engine keeps the BBD's pitch base when it
returns to a note engine, and with the init patch's STAGES that base is the top
of the span (§3.6). It is fixed here as well, because it breaks the fixed centre
this spec promises.

Every number below names its probe (§2). The seeds matter: the measurements this
thread was opened on used seeds that bias every phrase downward, and they point
the wrong way (§3.4).

## 2. How the numbers were taken

**P1 — one lane, through the pitch chain's arithmetic** (scratch probe
`probe_range_law.cpp`). A `ModLane` built the way `SuperModulator::init` builds a
PITCH lane: `set_melodic(true)` before `init()`, then `set_flow_melody(true)`.
STEP, 8 steps, 2 Hz, SHAPE 0.45 (inert on a note lane), SMOOTH 0, DENSITY 1, the
lane's own FORM and SONG unless a SONG rung is named. 200 seeds
`0x1234abcd + (k + 2)·2654435761`, k = 0..199; k = 0 is the seed deck A's PITCH
lane gets. 8 s per seed: two phrases to settle, fourteen measured. The lane runs
at RANGE 1 and the law under test is applied to its held value on every fire.
The result goes through today's two clamps or through the reflection of §4.2,
with the centre at semitone 18 and the song offset at 0 unless stated, then
through a `Quantizer` at root 0 in the stated scale. "VARY" is the value
`set_variation()` receives: 0 is the MELODY knob's centre (LOOP), 0.671 is deck
B's init MELODY (GROW).

**P2 — end to end** (scratch probe `probe_melody2.cpp`). `spky::Instrument`
driven by `control::ControlLaw::tick()` every 96 samples — the firmware's
control path — from `ffctl::kInitParamDefaults` plus the stated overrides. 1800 s
per run, the first 4 s skipped, the SONG knob nudged one rung and back every 8 s
so that a new phrase is asked for. One note per PITCH fire, read on the settled
quantized pitch (the sample before the next fire). "Noon" is the all-noon Rev A
pot frame that `SHELL_BOOT_PATCH=3` boots (`shell/generated_boot_patch_noon.h`),
laid over the init defaults. The new behaviour comes from copies of
`engine/mod/lane.cpp` and `engine/parts/part.cpp` with §4.1 and §4.2 behind
`#ifdef`s, linked in place of the originals. The init decks are slow — 92 and
110 notes in 1800 s — so the noon frame, about 2400 notes per deck, carries the
weight.

**P3 — what else the change reaches.** All 35 scenarios in
`host/render/scenarios/`, rendered by three `render.exe` builds — stock, §4.1
alone, §4.1 + §4.2 — and compared byte for byte. `spky_tests` linked the same
three ways, with the Release flags and `SPKY_TESTING=1`.

**P4 — identities.** `apply_range(v, r)` against `v·r + min(r, 1 − r)` on a
1001 × 2001 grid over r ∈ (0, 1] and v ∈ [−1, 1]. `apply_range(v, 1)` against
`v`, bit for bit, at 2^24 + 1 evenly spaced v ∈ [−1, 1]. xorshift32's first five
draws and the first composed phrase for the engine map's ten probe seeds and for
Part's two PITCH-lane seeds.

**P5 — a round trip through BBD** (scratch probe `probe_bbd_base.cpp`). P2's
setup: 4 s on the init engines, then `ENGINE_A` = `ENGINE_B` = 4 (BBD) for 4 s,
then the init engines again for 6 s. At the end of each phase, per deck: the
engine id, `Part::target_raw(LANE_PITCH)` (base plus lane, before TUNE) and the
quantized pitch.

Measures used throughout:

- a **one-note phrase** is a complete phrase whose notes are all one pitch;
- a **rail note** sits on semitone 0 or 36 — both are the root, three octaves
  apart;
- **clamped** (today) and **reflected** (§4.2) count notes whose pre-quantize
  value left [0, 1];
- the **root share** is the share of notes on pitch class 0;
- the **span** is a phrase's highest minus its lowest note, averaged.

The probes are scratch files, not in the repo; each is described above well
enough to rebuild in minutes. The listening renders of §6 are in
`renders/pitch-range-law/` (gitignored).

## 3. What is wrong today

### 3.1 Today's law is the centred law plus an upward tent

`apply_range` (`engine/mod/range.h:12-19`) equals, to float precision,

    apply_range(v, r) = v·r + min(r, 1 − r)        (P4: max deviation 8.92e−08)

The first term is a width: ±r around the centre. The second is a shift, zero at
both ends of the knob and +0.5 at its centre. On the pitch axis — 36 semitones
over 0..1, with the lane's 0 at the PITCH base 0.5 (`part.h:740`), semitone 18 —
the identity gives:

| RANGE | 0.10 | 0.25 | 0.50 | 0.75 | 1.00 |
|---|---|---|---|---|---|
| width, semitones either way | ±3.6 | ±9 | ±18 | ±27 | ±36 |
| upward shift of the centre | +3.6 | +9 | **+18** | +9 | 0 |
| the centre sits on semitone | 21.6 | 27 | **36** | 27 | 18 |

At the knob's centre the melody is centred on the top of the span, so every note
above its centre — half of them — is clamped onto semitone 36, the root.

### 3.2 The centre of the knob is its worst position

P1, Dorian, VARY 0, TUNE 0.5 (full table in §5.1). At RANGE 0.5, 45.0 % of notes
are clamped onto the top, 58.0 % are rail notes, 58.5 % are the root, and 29.4 %
of phrases are one note — against 13.3 % at 0.4 and 4.1 % at 1.0. The phrase
spans 3.89 semitones on average, narrower than at 0.4 (4.58). On one-note
phrases only RANGE 0.75 and above beats 0.4. All thirteen scales show it (§5.2: 25.8 to 38.7 % one-note
phrases at noon).

### 3.3 Where the factory patch and the board put RANGE

The init patch boots both decks at RANGE 0 (`control/params.hpp:235`, `:255`),
where every law returns 0: each deck holds one note (P2: deck A semitone 19,
deck B semitone 7, every phrase one note). The shipping firmware boots that
patch. A pot takes over its parameter only once it moves more than `kPotTouch` =
144 counts, and then the parameter jumps to where the pot sits
(`shell/pot_owner.h:3-23`). A RANGE pot standing near its centre — where the noon
frame puts every pot — therefore lands on the worst position of §3.2. In the noon
frame deck A plays 80.3 % one-note phrases and its most common note 91 % of the
time (P2).

### 3.4 Why the earlier measurements said the opposite

`melody-reachable` §1.3, the roadmap's open thread and `docs/engine-map.md` §7
say the ambitus peaks at RANGE 0.5 and that 54.2 % of samples are clamped at
RANGE 1.0. Those probes used small seeds (999, 7, 1, …). `spky::Rng` is a bare
xorshift32, and a small seed's first draws sit near −1 (P4): 7 of the engine
map's 10 probe seeds draw −0.86 or lower first, and their first phrases average
−0.33 to −0.85. Arithmetic on §3.1's identity shows what such a phrase does: one
averaging −0.5 sits around semitone 27 at RANGE 0.5, inside the span, so the
tent stays hidden.

The instrument seeds each lane with `seed_base + i·2654435761`
(`SuperModulator::init`). Deck A's PITCH lane draws +0.168 first and composes a
phrase averaging +0.020; deck B's draws +0.935 and averages +0.658. With seeds of
that kind (P1) the picture inverts: RANGE 0.5 is the worst position, and RANGE
1.0 clamps 4.3 % of notes at the top and 3.6 % at the bottom.

### 3.5 TUNE and the song offset come after the clamp

The chain is `clampf(clampf(0.5 + lane) + (TUNE − 0.5) + offset)`
(`part.cpp:165`, `:187-189`). A note clamped by the first clamp keeps its clamped
value through TUNE and the song offset, so turning TUNE does not bring a clamped
melody back. And whenever TUNE is away from 0.5, the second clamp bites at the
other end.

Deck B boots at TUNE 0.175, 11.7 semitones down. P1, Minor pent, VARY 0.671,
TUNE −11.7 semitones: at RANGE 0.5, 49.1 % of notes are clamped at the top and
59.1 % are the root (the clamped notes land on semitone 24.3, which quantizes to
the C an octave below the top). At RANGE 1.0, 38.6 % are clamped by the second
clamp at the bottom, 41.0 % are rail notes and 68.7 % are the root.

### 3.6 A deck that visited BBD keeps the BBD's pitch base

On a BBD deck the STAGES knob is the PITCH lane's base: `control/control_law.h:697-699`
writes `prm(STAGES_A/B)` into it while the deck is BBD, and nothing writes it
back afterwards. The ledger comment above it says as much: "Other engines retain
their existing base" (`:676-678`). The init patch's STAGES are 1.0 on deck A and
0.0 on deck B (`control/params.hpp:292-293`). P5:

| base + lane, and the pitch | deck A | deck B |
|---|---|---|
| on the init engines | 0.5000, semitone 19 | 0.5000, semitone 7 |
| on BBD | 1.0000, semitone 36 | 0.0000, semitone 0 |
| back on the init engines | **1.0000, semitone 36** | **0.0000, semitone 0** |

After one visit to BBD, deck A's melody is centred on the top of the span. With
today's law its sum is 1.0 + r·(v + 1) at every RANGE r ≤ 0.5, never below 1, so
every note is the top C (arithmetic on §3.1's identity; P5 then ran 120 s at
RANGE 0.5, and all six notes deck A played were semitone 36). Deck B's centre
sits on the bottom edge, and its TUNE of 0.175 pulls it further down. Whether
Bastian's decks had visited BBD is not known; this is a second way to the same
symptom, on the board and in Rack alike.

## 4. The design

### 4.1 The centred law on a note lane

On a lane that carries notes, RANGE keeps the width and loses the shift — the
first term of §3.1's identity:

    out = 0      for r ≤ 0
    out = v·r    otherwise

The gate is `_note_lane()` (`engine/mod/lane.h:331`, `_melodic && _flow_melody`):
the PITCH lane of a deck whose engine plays notes, in both modes. It applies at
**all three** places that call `apply_range` today: `lane.cpp:656` (`follow()`),
`:1001` (`process()`) and `:1252` (`tick()`). Every other lane keeps
`apply_range`: the PITCH lane of a SAMPLER or BBD deck, and the four texture
lanes, which never see a RANGE other than 1 (`SuperModulator::set_range` reaches
`LANE_PITCH` only, `super_modulator.cpp:109`).

The two laws agree at both ends of the knob. Both give 0 at r = 0, and
`apply_range(v, 1)` is `v` bit for bit (P4: 0 of 16 777 217 values differ). So
this half alone changes nothing for a deck at RANGE 0 or RANGE 1 — including
every scenario that never sets RANGE, since a lane starts at `_range = 1`
(`lane.h:411`).

*Correction (2026-10-26, Task 1 measurement): the "bit for bit" holds on the
2^-23 grid P4 sampled. Off that grid `apply_range(v, 1)` computes
`uni + (v − uni)` and differs from `v` by one ulp in 415 of 2001 evenly spaced
values (max 5.96e-8). The two laws are equal on P4's grid and within one ulp
elsewhere; the renders at RANGE 1 did not move in the build.*

### 4.2 One reflection instead of two clamps on a note deck

On a deck whose engine plays notes, `Part::pitch_pre_quant()` sums first and
bounds once:

    x = base + mod + (TUNE − 0.5) + offset     [ + (centre − 18)/36, §6 D1 ]
    reflect x into [0, 1]:  x < 0 → −x,  x > 1 → 2 − x,  until it is inside

`base + mod` is what `target_raw(LANE_PITCH)` adds today before its clamp
(`part.cpp:164-165`). The gate is `_note_deck()` (`part.h:807`), the same set as
the song offset's. A SAMPLER or BBD deck keeps both clamps exactly as they are.

Where neither of today's clamps bites, the sum is the same additions in the same
order and the reflection is the identity, so the result is bit-identical.
Elsewhere a note that leaves the span comes back mirrored at the edge instead of
landing on the root: the phrase's contour survives, bent at the edge. *(My call:
a reflection, not an octave wrap. A wrap keeps the pitch class but puts a
12-semitone leap inside a phrase the generator wrote as a step.)*

TUNE and the song offset now act on the melody before anything bounds it, so
turning TUNE moves a melody that touches an edge instead of leaving it pinned
there.

### 4.3 What stays as it is

- `apply_range` and the existing cases of `tests/test_range.cpp`.
- The SAMPLER's and the BBD's PITCH lane, in both modes. A BBD's PITCH lane is
  the delay clock, not a note (`melody-reachable` invariant I3, the owner's
  ruling of 2026-08-07).
- The quantizer: root-relative mask, ties to the lower note, hysteresis 0.30.
- The song offset's latch rules (spec 2026-10-18 `root-and-wobl-knobs` §2.1),
  SHAPE (inert on a note lane), and the phrase generator, GROW's walk included.
- The init patch, if D1 stays at 18: at RANGE 0 both laws give 0 and no clamp
  bites, so both drones keep their notes (P2: semitones 19 and 7, as today).

### 4.4 Readers of the PITCH lane outside the pitch chain

*(Read from the code, not probed.)* Two MOD-layer rows read the PITCH lane's
output after the law (`control/params.hpp:184-185`, `:214-215`): `FLUX_A/B`,
the FX MIX tap (`FXT_FX_MIX` is lane 2, `engine/fx/part_fx.h:15`), and
`TUNE_A/B`, computed by the host from `_laneOut` (`control/control_law.h:424-461`).
The stepped (S&H) reading carries the new law too, through `_latch_stepped(out)`.
Both depths are 0 in the init patch (`params.hpp:315-316`, `:345-346`), so the
factory sound does not meet them. With a depth set, these modulations sit
centred on their knob at every RANGE instead of leaning upward below RANGE 1.
*(My call: they follow the lane. Keeping the old law for them would take a
second, uncentred lane output to preserve a behaviour nobody chose.)*

### 4.5 A note deck's PITCH base returns to 0.5

The control law writes 0.5 into the PITCH lane's base on every tick a deck is
not BBD — the pattern `LANE_SIZE` already follows in the same ledger ("restored
to 0.5f below when the deck is neither", `control_law.h:673-675`) — and the
ledger comment says so. 0.5 is the base `Part` starts with (`part.h:740`) and
the centre that §4.1 and D1 are about. *(My call: in scope, because §3.6 breaks
the fixed centre this spec is built on, and the fix is one write in the control
law. The render host does not run the control law, so no scenario sees it.)*

### 4.6 Render scenarios keep their register

Seven scenarios run a note deck at a RANGE strictly between 0 and 1:
`ambient_wash` (both decks), `chord_bloom`, `reverb_delay`, `reverb_wash`,
`flux_mono_dry`, `flux_mono_verb` and `flux_tape_echo`. Several set a low PITCH
base on purpose, because `docs/gotchas.md:203-207` tells scenario authors to keep
it at about 0.15–0.3 against today's upward shift. Under §4.1 those melodies
would drop by min(r, 1 − r)·36 semitones — up to 16.2, on `ambient_wash` deck A
at RANGE 0.45 — and bounce off the bottom edge.

So each such deck's PITCH base is raised by min(r, 1 − r), starting from 0.5
where a scenario sets none. By §3.1's identity, base + v·r is then the sum
today's law produces: every melody keeps its centre and its width, and only the
notes today's clamp piled up change, because they are now reflected. The
gotcha's advice goes with it. *(My call: this keeps the showcases where they
were voiced, without a listening round. Where today's centre sat high —
`ambient_wash` deck A at 1.0, the three `flux_*` scenarios at 0.9 — it stays
high.)*

## 5. Measured: today against the design

### 5.1 One lane, Dorian, VARY 0, TUNE 0.5 (P1)

| RANGE | one-note phrases | off the span (clamped → reflected) | rail notes | root share | span, semitones |
|---|---|---|---|---|---|
| 0.10 | 39.0 → 50.5 % | 0 → 0 % | 0 → 0 % | 8.4 → 0.0 % | 1.09 → 1.11 |
| 0.20 | 16.3 → 20.1 % | 0 → 0 % | 0 → 0 % | 30.2 → 2.0 % | 2.78 → 2.66 |
| 0.25 | 14.4 → 15.0 % | 0 → 0 % | 0.4 → 0 % | 17.4 → 3.3 % | 3.26 → 3.30 |
| 0.30 | 14.2 → 12.0 % | 1.5 → 0 % | 2.7 → 0 % | 13.0 → 8.4 % | 4.00 → 4.06 |
| 0.40 | 13.3 → 8.2 % | 15.5 → 0 % | 22.0 → 0 % | 24.0 → 13.8 % | 4.58 → 5.46 |
| **0.50** | **29.4 → 5.6 %** | **45.0 → 0 %** | **58.0 → 0.5 %** | **58.5 → 14.6 %** | **3.89 → 6.78** |
| 0.60 | 13.9 → 5.4 % | 23.2 → 1.1 % | 27.3 → 0.4 % | 34.4 → 13.7 % | 6.21 → 8.01 |
| 0.75 | 6.1 → 4.9 % | 11.0 → 2.7 % | 13.2 → 1.0 % | 24.2 → 14.5 % | 9.08 → 9.76 |
| 0.90 | 5.3 → 3.1 % | 7.7 → 4.8 % | 9.0 → 1.9 % | 19.8 → 14.5 % | 11.25 → 11.27 |
| 1.00 | 4.1 → 2.7 % | 7.9 → 7.9 % | 9.8 → 3.4 % | 19.2 → 13.1 % | 12.30 → 12.17 |

At noon 63.6 % of phrases use three or more different notes, against 33.4 %
today.

Below RANGE 0.3 the design is no better, and at 0.1 it is worse: 50.5 % one-note
phrases against 39.0 %. The width is identical by construction (§3.1), so the
only difference is where the centre sits against the scale grid. At RANGE 0.1 a
phrase spans about one semitone, and whether it crosses a quantizer boundary
decides whether it is one note. That is the bottom of the knob playing a drone
with a little motion. D2 (a floor) is the lever if that is not wanted, and D1
moves it as well (§6).

### 5.2 All thirteen scales at noon (P1, VARY 0, TUNE 0.5)

| Scale | today at 0.5: one-note / clamped / root share | new at 0.5: one-note / rail / root share | new at 1.0: one-note / rail |
|---|---|---|---|
| Aeolian | 29.8 / 45.0 / 58.5 % | 6.4 / 0.5 / 14.6 % | 2.4 / 3.4 % |
| Dorian | 29.4 / 45.0 / 58.5 % | 5.6 / 0.5 / 14.6 % | 2.7 / 3.4 % |
| Mixolydian | 29.1 / 45.0 / 58.5 % | 5.2 / 0.5 / 14.6 % | 3.4 / 3.4 % |
| Lydian | 25.8 / 45.0 / 54.0 % | 6.1 / 0.5 / 10.4 % | 3.9 / 2.3 % |
| Hirajoshi | 38.7 / 45.0 / 66.9 % | 9.8 / 0.8 / 22.8 % | 4.1 / 5.3 % |
| Pygmy | 30.1 / 45.0 / 58.5 % | 9.7 / 0.5 / 14.6 % | 4.8 / 3.3 % |
| Minor pent (init) | 30.1 / 45.0 / 58.9 % | 7.1 / 0.5 / 17.7 % | 2.9 / 4.9 % |
| Kumoi | 35.8 / 45.0 / 63.2 % | 8.9 / 0.5 / 18.1 % | 5.1 / 4.8 % |
| Major pent | 36.2 / 45.0 / 63.2 % | 7.5 / 0.5 / 18.0 % | 4.6 / 4.8 % |
| Phrygian | 29.8 / 45.0 / 58.4 % | 6.7 / 0.3 / 11.0 % | 2.7 / 2.7 % |
| Hijaz | 29.4 / 45.0 / 58.4 % | 5.9 / 0.3 / 11.0 % | 3.1 / 2.7 % |
| Harmonic minor | 26.1 / 45.0 / 54.0 % | 6.0 / 0.5 / 10.4 % | 2.7 / 2.3 % |
| Whole tone | 29.4 / 45.0 / 58.5 % | 8.0 / 0.5 / 14.6 % | 4.3 / 3.4 % |

Without any per-scale term the design spreads little across scales: 5.2 to 9.8 %
one-note phrases at noon, 2.3 to 5.3 % rail notes at RANGE 1. That is why there
is no per-scale law (§8).

### 5.3 With GROW (P1, Dorian, VARY 0.671)

| | today, RANGE 0.5 | new, RANGE 0.5 | today, RANGE 1.0 | new, RANGE 1.0 |
|---|---|---|---|---|
| one-note phrases | 3.0 % | 0.0 % | 0.1 % | 0.0 % |
| off the span (clamped / reflected) | 49.1 % | 0 % | 43.4 % | 43.4 % |
| rail notes | 53.9 % | 15.1 % | 45.9 % | 4.5 % |
| root share | 57.1 % | 25.8 % | 52.7 % | 14.4 % |

GROW drives phrase values to the ends of the lane: 12.8 % of notes have
|v| ≥ 0.999, at every RANGE. At exactly RANGE 0.5 the width is ±18 semitones,
so those notes land exactly on semitone 0 or 36, both the root — the 15.1 % rail
notes above. Above 0.5 they leave the span and are reflected inward (rail notes
4.4 to 5.2 % from 0.6 to 1.0). This residue belongs to GROW, sits at one knob
position, and is 15.1 % against today's 53.9 %; §8 says why no gain is spent on
it.

### 5.4 TUNE away from the centre: deck B's init (P1, Minor pent, VARY 0.671, TUNE −11.7 semitones)

| | today | §4.1 alone | §4.1 + §4.2 |
|---|---|---|---|
| RANGE 0.5: rail notes | 0.0 % | 33.2 % | 8.5 % |
| RANGE 0.5: root share | 59.1 % | 47.1 % | 29.9 % |
| RANGE 1.0: rail notes | 41.0 % | 41.0 % | 6.6 % |
| RANGE 1.0: root share | 68.7 % | 68.7 % | 18.3 % |

The law alone moves the pile from the top clamp to the bottom one at noon (28.7 %
of notes on the second clamp) and changes nothing at RANGE 1. The reflection is
what removes the pile. With it, 28.7 % of notes are reflected at the bottom at
RANGE 0.5 and 49.6 % at RANGE 1: deck B's centre sits on semitone 6.3, and GROW
reaches far below it. D1 moves that centre.

### 5.5 End to end (P2)

| Deck, setting | one-note phrases | most common note (semitone, share) | rail notes |
|---|---|---|---|
| A, noon frame | 80.3 → 14.6 % | 35, 91 % → 25, 29 % | 0 → 0 % |
| B, noon frame | 5.3 → 2.7 % | 36, 39 % → 17, 23 % | 39 → 0 % |
| A, init | 100 → 100 % | 19, 100 % → 19, 100 % | 0 → 0 % |
| B, init | 100 → 100 % | 7, 100 % → 7, 100 % | 0 → 0 % |
| A, init + RANGE_A 0.25 | 0.0 → 5.6 % | 27, 36 % → 19, 33 % | 4 → 0 % |
| A, init + RANGE_A 0.5 | 16.7 → 0.0 % | 36, 57 % → 19, 16 % | 57 → 8 % |
| A, init + RANGE_A 0.75 | 0.0 → 0.0 % | 36, 16 % → 19, 13 % | 20 → 1 % |
| A, init + RANGE_A 1.0 | 0.0 → 0.0 % | 0, 13 % → 15, 14 % | 21 → 4 % |
| B, init + RANGE_B 0.25 | 8.1 → 8.1 % | 15, 37 % → 7, 37 % | 0 → 7 % |
| B, init + RANGE_B 0.5 | 40.5 → 2.7 % | 24, 65 % → 7, 29 % | 0 → 12 % |
| B, init + RANGE_B 0.75 | 5.4 → 2.7 % | 24, 25 % → 7, 25 % | 11 → 3 % |
| B, init + RANGE_B 1.0 | 5.4 → 5.4 % | 0, 26 % → 7, 27 % | 26 → 3 % |

Both init decks run GROW (MELODY 0.688 and 0.671), so the 8 % and 12 % rail notes
at RANGE 0.5 are §5.3's residue. The init rows rest on 92 and 110 notes; read
them as a direction and the noon rows as the measurement.

### 5.6 What else the change reaches (P3)

- **Renders.** §4.1 alone moves 7 of the 35 scenarios: `ambient_wash`,
  `chord_bloom`, `flux_mono_dry`, `flux_mono_verb`, `flux_tape_echo`,
  `reverb_delay`, `reverb_wash`. §4.1 + §4.2 moves 15: those seven and
  `assets_percussive_source`, `body_sympathetic`, `ctrl_identity`,
  `demo_density_sweep`, `demo_song_aaab`, `demo_song_modes`,
  `demo_step_melody`, `pace_sweep`. Unchanged: `wave_formant_sweep`,
  `bbd_pitch_bend`, all 13 `sampler_*` scenarios, `flow_melody`, `feed_drone`,
  `body_bow`, `body_strum`, `demo_shuffle`.
- **Hash gates.** Of the two hash-gated renders, `ctrl_identity` moves — with
  §4.2, not with §4.1, since it never sets RANGE — and `wave_formant_sweep` does
  not.
- **Unit tests.** Stock 1433 of 1433; both prototype builds 1432 of 1433. The
  one failure is `param impact: a live parameter works in both operating
  modes`: FORM_A and FORM_B now move audio in **both** modes, so the
  mode-exclusive set loses exactly those two, which
  `test_param_impact.cpp:405-429` lists as sample-bound. §4.1 alone is enough
  for both, so the tent of §3.1 is what held them; that entry's comment had
  already traced FORM_A's dead FLOW reading to a lane sitting on the top of the
  span ("pre-quantize 36.0 throughout").

## 6. Decisions for Bastian

### D1 — where the melody's centre sits (default: semitone 18, as today)

With the shift gone, the melody circles one fixed point: semitone 18 plus TUNE
plus the song offset — F# at TUNE 0.5, the point the ROOT spec (§2.1 there)
already named as today's centre. Most scales do not contain F#, so at a small
RANGE the melody's home is the scale note next to it: the 4th or the 5th, or the
tritone itself in Lydian and Whole tone. A centre of 12 or 24 makes the root the
home. The centre would be one constant in §4.2's sum, on note decks only.

| P1, VARY 0, TUNE 0.5 | centre 18 | centre 12 | centre 24 |
|---|---|---|---|
| Dorian, RANGE 0.1: home | 4th 55.1 %, 5th 39.8 % | root 69.7 % | root 69.7 % |
| Lydian, RANGE 0.1: home | tritone 57.3 %, 5th 22.4 % | root 55.4 % | root 55.4 % |
| Minor pent, RANGE 0.1: home | 4th 55.1 %, 5th 41.6 % | root 75.6 % | root 75.6 % |
| Dorian, RANGE 0.5: one-note phrases / root share | 5.6 / 14.6 % | 7.6 / 26.5 % | 7.6 / 26.5 % |
| Minor pent, RANGE 0.5: one-note phrases / root share | 7.1 / 17.7 % | 9.1 / 29.4 % | 9.1 / 28.9 % |

| P1, deck B's init: Minor pent, VARY 0.671, TUNE −11.7 | centre 18 | centre 12 | centre 24 |
|---|---|---|---|
| RANGE 0.5: reflected / rail notes | 28.7 / 8.5 % | 49.1 / 13.0 % | 15.7 / 4.7 % |
| RANGE 1.0: reflected / rail notes | 49.6 / 6.6 % | 56.3 / 21.1 % | 45.2 / 5.7 % |

| P2 | centre 18 | centre 12 | centre 24 |
|---|---|---|---|
| init drones, deck A / deck B | 19 / 7 (G, G — as today) | 12 / 0 (C, C) | 24 / 12 (C, C) |
| noon frame, one-note phrases A / B | 14.6 / 2.7 % | 14.1 / 1.8 % | 16.7 / 1.5 % |
| deck B, init + RANGE_B 0.5: one-note phrases / rail notes | 2.7 / 12 % | 5.4 / 29 % | 5.4 / 2 % |

- **12 is ruled out on the numbers.** It puts deck B's drone on the bottom edge
  of the span (semitone 0) and reflects about half of deck B's notes at every
  RANGE from 0.1 up (44.4 to 50.0 % at the bottom edge alone).
- **24 against 18 is taste, with a precedent.** 24 makes both decks circle the
  root, and it changes the factory patch: both drones move from G to C. In the
  ROOT spec Bastian rejected "centre on the tonic" because it moved the init
  patch's centre from F# to C (§5 there); 24 does the same to the drones.
- **Default 18**, because it leaves the factory patch alone. *(My
  recommendation; Bastian decides by ear.)*

Listening: four 120 s renders through the control law at 48 kHz, of the init
patch with RATE_A = RATE_B = 0.5, RANGE_A = RANGE_B = 0.5 and
SONG_A = SONG_B = 6, in `renders/pitch-range-law/`:

- `1-today.wav` — stock;
- `2-new-centre-18.wav` — §4.1 + §4.2, centre 18;
- `3-new-centre-12.wav` — centre 12;
- `4-new-centre-24.wav` — centre 24.

### D2 — a floor at the bottom of the knob (default: none)

`melody-reachable` §2.2 asked for a floor and made its value Bastian's call. A
floor f maps RANGE r > 0 to the width f + (1 − f)·r. P1, Dorian, VARY 0, f = 0.3:

| RANGE | 0.10 | 0.25 | 0.50 |
|---|---|---|---|
| one-note phrases, no floor | 50.5 % | 15.0 % | 5.6 % |
| one-note phrases, f = 0.3 | 8.8 % | 6.8 % | 5.4 % |

The floor repairs the bottom of the knob, and the bottom of the knob is what it
costs: the first touch above 0 jumps from one held note to ±10.8 semitones
(0.3 × 36), and the drone with a little motion that RANGE 0.1 plays is gone.
**Default none.** *(My recommendation. The floor answered a drone band that the
terrain generator drew (`melody-reachable` §1.3); the generator was deleted on
2026-08-14, and a player who wants a wider melody turns the knob.)*

Listening: the same render setup as D1's, but at RANGE_A = RANGE_B = 0.1:
`5-range-0.1-no-floor.wav` (§4.1 + §4.2, centre 18) and
`6-range-0.1-floor-0.3.wav` (the same with f = 0.3).

If D2 picks a floor, §4.1's law becomes v·w(r) with w(r) = f + (1 − f)·r for
r > 0. A1 then checks w(r) instead of r; §1's "the width stays exactly what it
is today" holds only at RANGE 1; and §5's numbers below noon no longer describe
the build (A3 and A4 take their thresholds from the test's own readings either
way).

## 7. Goals

1. On a note deck RANGE sets the melody's width around a centre that does not
   move with the knob, and the centre of the knob is no longer its worst
   position (A3).
2. On a note deck the pitch chain never piles notes onto the root by clamping,
   and TUNE and the song offset act before anything bounds the melody (A4).
3. Nothing changes where the design should not reach: RANGE 0 and RANGE 1 on the
   lane, pitch sums inside the span, SAMPLER and BBD decks, the texture lanes
   (A1, A2).
4. The factory patch sounds as before at D1 = 18 (A5).
5. A deck that has visited BBD comes back with its PITCH base at 0.5 (A10).

## 8. Non-goals and anti-goals

- **No per-scale (degree) law**, although `melody-reachable` §2.2 asked for one.
  *(My call.)* §5.2 shows the centred law alone spreads little across the
  thirteen scales, and normalising by degrees per octave widens the pentatonics
  by 7/5: P1 with that gain and today's clamps puts 9.8 % of a pentatonic's
  notes on the top clamp and 12.2 % on the bottom at RANGE 1, with 25 % rail
  notes against Dorian's 9.8 %.
- **No floor** unless D2 says otherwise.
- **No gain below 1** to keep GROW's notes off the rails at exactly RANGE 0.5
  (§5.3). *(My call: it would narrow every melody at every RANGE to fix one knob
  position.)*
- **No init re-bake.** The factory patch keeps RANGE 0 on both decks, i.e. one
  held note per deck. Whether it should open RANGE now that noon plays a melody
  is Bastian's call after he has heard the build; a re-bake is its own round.
- **Ostinato stays near-static.** At noon, VARY 0, the two Ostinato rungs of the
  SONG ladder give 38.4 and 26.4 % one-note phrases under the design (80.9 and
  66.5 % today, P1). That is the principle, not the law.
- **Not touched:** GROW's pull toward |v| = 1; SONG_B's untraced deadness
  (`test_param_impact.cpp:296-306`, still dead under both prototypes); the fact
  that TwoMotif and OneMotif compose the same phrase at 8 steps (P1 prints
  identical numbers for SONG rungs 0/1, 2/3 and 4/5).
- **No patch migration** (dev alpha). A saved patch with RANGE between the ends
  will sound different.
- **Anti-goal: a gate on the slot** (`slot == LANE_PITCH`) or on
  `_melody_engine_on()`. Either reaches the BBD's delay clock (I3). The gate is
  the engine class: `_note_lane()` in the lane, `_note_deck()` in the part.
- **Anti-goal: anything under `hardware/`.**

## 9. Constraints

- **The gate is the engine class** (§8, and `melody-reachable` I3).
- **No libm call in the reflection.** `pitch_pre_quant()` runs on every Part
  control tick, on both decks of the board, and per-tick libm work is what once
  cost the Daisy's control path most of its budget (memory
  `fireflow-daisy-control-cost`). Every term of the sum is bounded, so a fixed
  number of conditional reflections is exact; no `fmodf`. The builder confirms
  the bound and pins it in a test.
- Release builds; the two toolchains are never mixed; VCV through
  `build-local.sh`; English in every file; commit trailer per CLAUDE.md.
- Any new number in the plan or in a test comes from a probe with Part-style
  seeds (§2).

## 10. Where it touches

- `engine/mod/lane.cpp:656`, `:1001`, `:1252` — §4.1 at all three, through one
  helper so the three cannot drift apart.
- `engine/mod/range.h` — the natural home for the centred law, beside
  `apply_range`; the header comment says which lanes use which law.
- `engine/parts/part.cpp:187-189` — §4.2 in `pitch_pre_quant()`. The comment
  above it (`:175-186`) says the clamp folds an offset song back at the span's
  edges; it now reflects there, and the comment says so.
- `tests/test_param_impact.cpp:373-429` — FORM_A and FORM_B leave
  `expected_sample_bound` (A6); their comment block is replaced by a dated note
  saying why they retired.
- `tests/test_melody_reachable.cpp:94-113` — the SAMPLER/BBD case runs at RANGE
  1 only, where the two laws are bit-identical, so it cannot go red for this
  change; A2 extends it.
- `tests/test_range.cpp` — direct cases for the centred law.
- `tests/test_song_offset.cpp:78-81` and `:276-290` — a comment and a test name
  ("clamp holds at the span edge") describe the clamp. The assertions still hold
  under the reflection (the value stays inside [0, 1]); the words change.
- `control/control_law.h:670-699` — §4.5's write, and the ledger comment at
  `:676-678`.
- `host/render/scenarios/` — §4.6's base changes in the seven scenarios, made
  after A7's comparison.
- New tests for A1, A3, A4, A5 and A10.
- `CMakeLists.txt:338-358` — `ctrl_identity`'s `EXPECTED`, re-baselined with its
  reason (A7).
- `shell/` — rebuilt with `make images` and flashed as a pair, QSPI first.
  `bench/` compiles `engine/` and must be rebuilt before its next run (memory
  `fireflow-bench-stale-object-trap`); no bench row is owed.
- `host/vcv/` — no source change; rebuilt and installed, since it compiles
  `engine/`.
- Docs (§13).

## 11. Acceptance

Each test is shown red once against the build it guards against.

**A1. The centred law, on the lane.** A note lane (melodic, note engine) at
RANGE r ∈ {0.1, 0.25, 0.5, 0.75} emits, sample for sample, exactly r times what
the same lane with the same seed emits at RANGE 1, and 0 at RANGE 0, in STEP and
in FLOW. Red against stock at r = 0.5.

**A2. Everything else keeps `apply_range`.** A SAMPLER/BBD PITCH lane
(`set_flow_melody(false)`) at the same four r emits `apply_range(v, r)` of its
own RANGE-1 stream, sample for sample, in both modes. Red against a variant
gated on `_melodic` alone — which is what a slot gate amounts to, since only
`LANE_PITCH` is melodic (`super_modulator.cpp:23`). A non-melodic lane gets the
same check as a boundary pin, like the existing case beside it: every plausible
gate leads with `_melodic &&`, so no variant reddens it.

**A3. Noon plays a melody.** Through the real pitch chain (lane →
`pitch_pre_quant()` → quantizer, on a SYNTH deck): STEP 8, Dorian, root 0,
TUNE 0.5, VARY 0, SMOOTH 0, Part-style seeds, RANGE 0.5. The share of one-note
phrases and the share of rail notes each stay below a threshold. P1 reads
29.4 → 5.6 % and 58.0 → 0.5 %; the test's own path will read slightly different
numbers. The builder measures stock and the new build through the test, writes
both readings into its comment, and sets each threshold so that both readings
clear it by at least a factor of two. Red against stock.

**A4. TUNE away from the centre does not pile notes.** Same path, Minor pent,
TUNE 0.1747 (deck B's init), VARY 0.671, RANGE 1.0: rail notes stay below a
threshold set the same way. P1 reads 41.0 % today, 41.0 % with §4.1 alone and
6.6 % with §4.2. Red against stock **and** against §4.1 without §4.2: this is
the test that proves the reflection is there.

**A5. The factory patch keeps its drones.** Through the control law from
`kInitParamDefaults`, deck A plays semitone 19 and deck B semitone 7 on every
fire (P2). Red against a centre of 12 or 24. If D1 goes the other way, this test
pins the new drones instead and `docs/by-ear-decisions.md` says why.

**A6. FORM_A and FORM_B work in both modes.** `test_param_impact`'s
mode-exclusive case no longer lists them as sample-bound (P3: they move audio in
both modes under §4.1 alone). Red against stock, where both are still
mode-exclusive.

**A7. Renders.** `ctrl_identity` is re-baselined by the procedure in
`tests/check_render_hash.cmake`, with a comment naming this spec and the
mechanism: the scenario never sets RANGE, so §4.1 leaves it bit-identical, and
§4.2 reflects the pitch sums that leave the span instead of clamping them.
`wave_formant_sweep` does not move. The builder renders all 35 scenarios before
and after, on the unchanged scenario files, and confirms that the moved set is
§5.6's fifteen; an unexplained one is a finding, not a baseline. §4.6's base
changes come after this comparison.

**A8. Builds.** Release build, then `ctest --test-dir build -E "^reva_"` green
(ctest does not build); `shell/` `make images`; `host/vcv/build-local.sh
install`, then Bastian restarts Rack.

**A9. Bastian's ears.** In his review of this spec: D1 from renders 1–4, D2
from renders 5–6. After the build, in Rack or on the board: RANGE swept from 0 to
full on the init patch; noon plays a melody, and no deck sits on one note above
RANGE 0.3.

**A10. A deck that visited BBD comes back centred.** Through the control law
from `kInitParamDefaults`: both decks to BBD and back, as in P5. Afterwards, at
RANGE 0, `target_raw(LANE_PITCH)` is 0.5 on both decks and the drones are back on
semitones 19 and 7. Red against stock, which leaves 1.0 and 0.0 (P5).

## 12. Left to the builder

- Name and home of the centred law: a `range.h` function beside `apply_range`
  or a helper in `lane.cpp`.
- How the reflection is written, within §9's no-libm rule, and the test that
  pins its bound.
- Test names and files, and whether A3–A5 drive a `Part`, an `Instrument` or
  the control law.
- Whether the D1 constant exists at all while D1 is 18 (it adds 0.0f).
- What becomes of the then-empty `expected_sample_bound` group in
  `test_param_impact.cpp`.

## 13. Docs owed in the same change

- `docs/engine-map.md` §6: the seed rule — probe with Part-style seeds,
  `0x1234abcd + k·2654435761` — and the measured bias of the small seeds
  (§3.4). The skeleton's seed changes with it.
- `docs/engine-map.md` §7, "The melody's second gate": the RANGE table (seeds
  12345 / 777 / 4242) is replaced by §5.1's numbers under the new law, and the
  "unsolved" paragraph is closed.
- `docs/engine-map.md` §1: its per-state numbers come from the small seeds; mark
  them as such. Re-measuring them is not this spec's work.
- `melody-reachable` §1.3 and §2.2: a "Superseded" note pointing here, like the
  one already in its §1.2. Its gates G3 and G4 retire with it. G3 measured a
  drone band of the deleted terrain generator, and D2 is that question now. G4
  counted clamped samples, which the reflection makes zero by construction — a
  gate that cannot go red. A4 carries its intent.
- `docs/roadmap.md`: the open thread (`:3427`) becomes this spec's status line,
  and the mentions at `:362`, `:700` and `:2861` point here.
- `docs/by-ear-decisions.md`: D1 and D2 as Bastian rules them.
- `docs/gotchas.md:203-207`: the advice to keep a PITCH base low against the
  upward shift goes (§4.6); on a note deck the base is now the melody's centre.
- `engine/mod/range.h`'s header comment and `part.cpp:175-186` (§10).
