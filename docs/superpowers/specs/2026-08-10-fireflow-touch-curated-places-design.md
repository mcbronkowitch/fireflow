# FireFlow on Simple Touch 2: curated places — design

**Date:** 10 August 2026
**Status:** Draft, approved. None of it is implemented.
**Trigger:** The residency offer is still open. Until it lands, the running plan
holds; when it lands, this becomes the first project.
**Touches:** `engine/flow/**` (data storage, no mechanism), `host/render/**`
(audition), `tests/**` (gate). Not: `shell/`, not the `engine/` DSP, not the big
panel.

---

## §1 What changed

If the offer lands, the residency runs in two phases:

| | Period | Board | Deliverable |
|---|---|---|---|
| **Phase 1** | Sep–Dec 2026 | Synthux **Simple Touch 2** (Daisy **Seed**) | a *finished* instrument, with its own faceplate |
| **Phase 2** | Jan–May 2027 | own hardware (Daisy **Patch Submodule**) | this is where it grows into the big one |

Phase 1 is not a warm-up. It is a deliverable of its own with a faceplate of its
own, and every instrument in the cohort gets one. That moves the running M6 plan
(Patch SM prototype, 66 positions) from "next" to phase 2 — it does not become
wrong, it just stops being the thing that has to be finished in December.

The Touch 2 hardware, per the manufacturer: **1× Daisy Seed, 12 touch pads,
6 trim knobs, 2 faders, 2 switches, stereo in, stereo out**, plus five swappable
faceplates.

Six trim knobs, and `engine/flow/flow_ids.h` knows **exactly six macros**
(`M_MOTION`, `M_DENSITY`, `M_BRIGHT`, `M_DIRT`, `M_WANDER`, `M_SPACE`). The
instrument that belongs on this board already exists in the repo: Glow.

## §2 The finding: what is broken in Glow

Glow's only door is called NEW, and NEW is a slot machine — press, roll, hope.
The hit rate is not good enough: not every drawn place sounds good. As long as
NEW is the only way in, that is not a blemish, it is unplayability. On hardware
it gets worse, not better: in a rack you press ten times and look away, on a
board in front of an audience you press once.

**This is a finding about the generator, not about the six macros.** The macros
are the good part of Glow. This spec repairs the door, not the room.

A second, smaller finding belongs with it: a terrain judged at *one* knob
position has not been judged at all. A place is a six-dimensional space; whether
it carries is decided by crossing it. §5 draws the consequence.

## §3 The decision: curated ground, randomness inside the archetype

**Twelve pads, twelve places.** A pad is a place — not a modifier, not a menu
entry. Twelve hand-picked terrains, signed off by ear, sit fixed in the image;
the faceplate carries their names.

Glow's failure mode disappears **by construction**: no place reaches the board
that was not kept. The hit rate is 100 % because it stopped being a result of
chance.

The randomness does not disappear, though — it gets a fence, and the fence
already exists in the code:

- **Tap a pad** → `wake(code)` onto the stored place. Immediate, no blend
  (`Flow::wake`).
- **Hold a pad** → `new_partial(0x3F)`: reroll all six macro domains,
  **`master` stays put**.
- **Tap the pad again** → back to the curated state. No undo mechanism is
  needed for this.

Why that is the right fence is written in `engine/flow/terrain.h`: the archetype
comes from `arch_of(master)` with its counter pinned to 0, and `adventure_base`
is drawn "from the master ALONE", which is why "a partial reroll cannot shift a
base parameter at all". **A `new_partial` rerolls a place's stories, never its
ground.** That is exactly "curated ground, randomness as excursion" — and it is
already implemented.

What that means for the generator: it moves house. It keeps inventing places,
but in the workshop (VCV, desktop render), not on stage. All the `engine/flow/`
work stays valuable; it becomes the authoring pipeline.

## §4 What a place is, and how it is stored

A place is a `TerrainState`: `uint32_t master` plus six `uint16_t` reroll
counters — **16 bytes**. `engine/flow/terrain_code.h` already writes it as a
24-character string today (`kTerrainCodeLen == 24`):

```
F1-DEADBEEF-000100020000
```

Header-only, no heap, `encode_code` / `decode_code` both stateless. Glow already
persists its state this way (`Glow.cpp:203`). **There is no storage format to
invent.**

### §4.1 The trap

A code is not a recording, it is a **reference into the generator**.
`generate()` is pure table arithmetic over `taste.h`. Change a number there and
the same code produces a different place — and twelve auditioned places would be
silently gone, without anything going red.

From that follows an ordering that is inconvenient and holds anyway: **structural
`taste.h` work comes before curating, not after.** Fine tuning is handled by the
curation itself, by discarding bad places.

### §4.2 Three ways, and the choice

**A — codes only.** 16 bytes, shareable, typeable, trivial in firmware. Every
`taste.h` change silently invalidates the collection. *Rejected because of §4.1.*

**B — resolved terrains.** Serialize the whole `Terrain` struct (`base[]` over
all `P_COUNT`, six `MacroMap`s, weather, adventure) — roughly one to two
kilobytes per place. Immune to `taste.h`, but a new serialization format, nothing
readable or shareable by hand any more, and the places become dead recordings
that never benefit from generator improvements again. *Rejected.*

**C — codes plus a fingerprint gate. ← chosen.** What gets stored is the code.
Next to it, per place, sits a fingerprint of the *generated* terrain. A test
generates every place and compares. Touch `taste.h` and the test goes red and
names the places that moved. Cost: one test and one column. Gain: the collection
can no longer die quietly, and `taste.h` stays changeable.

### §4.3 The collection file

One file, `engine/flow/places/pool.tsv`, TSV, one line per kept place:

| Column | Contents |
|---|---|
| `code` | 24 characters, `F1-…` |
| `arch` | archetype, from `arch_of(master)` — redundant, but sortable |
| `date` | date of sign-off |
| `fp` | fingerprint (§6) |
| `pad` | empty, or 1–12: the pad position in the shipped set |
| `name` | the name that goes on the faceplate |
| `note` | one sentence: why it was kept |

**One file, not two.** The pool grows over months; the shipped set is the subset
with a `pad` value. That gives one truth, one gate and no sync path between two
files. The firmware header is generated from the twelve `pad` rows, sorted by
`pad`.

## §5 The curation pipeline

Two stages with a clear division of labour. Eighty percent of it exists.

**What is already there:** `host/render/scenario.cpp` speaks Flow in full —
`flow_wake` (with a terrain code), `flow_macro`, `flow_cv`, `flow_new`,
`flow_new_partial`, `flow_undo`, `flow_lock`. There is already
`scenarios/flow_new_ride.json`, and a comment in the scenario parser speaks
literally of an *"audition file"*. Glow's context menu has **"Copy terrain
code" / "Paste terrain code"** (`Glow.cpp:694/697`).

### §5.1 Stage 1 — screening: offline, in batches

A script draws *N* candidates, fills *N* scenarios from **one** audition
template, renders them through `build/render.exe` and drops `place-<code>.wav`
into a folder. What gets listened to is a folder, the way one listens through
field recordings; what gets kept is filenames. No rack, no mouse.

A candidate is **one master with six zero counters** — nothing more is needed,
because a place's ground hangs off the master (§3). An archetype filter is free,
because `arch_of()` is a pure function of the master and needs no `generate()`
call.

Batch size *N* is a script argument. How hard the funnel sieves is what the first
batch says; before that, any number is a guess.

### §5.2 The audition

Identical for **every** candidate, or the judgements are not comparable. Four
fixed readings of 8 s each after a 2 s lead-in, **34 s** in total:

| Reading | Macros | Question |
|---|---|---|
| middle | all 0.5 | Is this place's middle good? |
| low | all 0.2 | Does it carry when you pull it back? |
| high | all 0.8 | Does it fall apart when you drive it? |
| skewed | fixed, asymmetric, written into the template | Is it musical off the diagonal too? |

**Four readings, not six macro sweeps.** At 50 candidates that is 28 minutes
instead of about 50, and the screening is meant to be coarse and to discard
hard.

**The readings are jumped, not ramped.** The jump is a single event in the
scenario format, and it answers a real question on the side: does the place
survive being torn. The first half second of each reading is transition and is
not judged.

### §5.3 Stage 2 — rehearsal: live in Rack, hands on the knobs

The survivors are pasted in via "Paste terrain code" and played. This is where
the decision falls, and it falls **without new software**.

Two things get checked here that stage 1 cannot see:

1. **Does the place survive a reroll?** `new_partial` across all six domains,
   repeatedly. A place with good ground must survive every one of its own
   excursions — otherwise the pad does not belong to it (§3).
2. **How does it feel under the hand?** That is the question with no offline
   substitute, and it is the reason this stage exists.

## §6 The fingerprint gate

A test in `tests/` reads `pool.tsv`, decodes every line, calls `generate()` and
compares against the `fp` column.

The fingerprint is a hash over the **complete** generated `Terrain`: `arch`,
`a_carries`, `base[]`, `storied[]`, all six `MacroMap`s including their curve
breakpoints, `window[]`, the weather and the adventure values. **Not just
`base[]`** — the stories are what the six knobs *do*, and a `taste.h` change that
only moves curves changes the place just as thoroughly as one that moves the base
patch. A gate that checks only `base[]` would miss exactly the half that sits
under the hand on the Touch.

**The red case is the important one.** It does not say "error", it says: *these
places have moved, audition them again or take the change back.* It therefore has
to print the affected rows **by name**, not merely fail.

Per the repo's house rule (*"a test that cannot go red gets fixed"*), the red is
proven once: move a `taste.h` number experimentally, watch the gate fall and name
the right rows, put it back.

## §7 What this spec does not decide

Not left open because it is unclear — because today it could only be guessed.
Every line names what settles it:

| Open | Settled by |
|---|---|
| Pads binary or continuous? | A measurement on the arrived board. It decides whether "hold a pad = lean into this place, release = fall back" can exist — potentially the best gesture in the instrument, and an alternative to §3's reroll-on-hold. Not a datasheet reading, a measurement. |
| What the 2 faders and 2 switches do | After twelve places have been played. Before that it is furnishing a room nobody has entered. |
| All 12 pads places, or some of them actions? | Same reason. §8.1 bounds the space this gets decided in. |
| Which part engines the Seed image carries | A measurement on the Seed. `docs/bench/2026-08-07-seed-vs-patch-sm.md` explicitly forbids the transfer — in both directions. |

## §8 Principles that hold from here on

They need no hardware and they bound the space in which §7 gets decided.

**§8.1 One pad, one place. No combinations.** Pad × knob would yield many
possibilities that nobody remembers. That is not an interface, it is a menu with
bad labelling.

**§8.2 No menu.** Whatever needs an explanatory label does not go on the Touch.
The faceplate carries names of places, not an instruction manual.

**§8.3 Flow is the only driver.** There is no second path to the `Instrument`
parameters. That is how it is today, and it is the reason the surface *can* stay
small.

**§8.4 No speculative board abstraction.** No rebuild of `shell/` toward board
independence before there are two real users. The repo has made and justified
this decision once already: `bench/` and `shell/` share exactly one file, "and
that is deliberate" (`shell/README.md`).

## §9 The bridge to phase 2

**A place is 16 bytes and knows nothing about hardware.** No board dependency, no
panel dependency, no control dependency. Phase 2 inherits the twelve places for
free, and the big instrument gets a role it did not have before:

> **Phase 1 chooses places. Phase 2 goes inside them.**

The 66 positions of the big panel are then not "more knobs", they are the way
*into* a place you can enter and colour on the Touch. Two instruments inhabiting
the same land — not one big one plus a stripped-down version.

**Consequence for the current state:** the panel redistribution
(`2026-08-10-hw-panel-redistribution-design.md`, merged, not implemented) and M6
**pause, they do not die**. They are phase 2.

**Consequence if the offer does not land:** not one hour of the curation is lost.
The same places then carry the big instrument.

## §10 What is to be built

1. **Candidate drawing** — a small desktop tool that emits *N* codes, optionally
   filtered to one archetype.
2. **The audition template** (§5.2) as a scenario, plus a batch script that fills
   it with codes and runs `build/render.exe` over them.
3. **`engine/flow/places/pool.tsv`** (§4.3) and the firmware header generated
   from its twelve `pad` rows.
4. **The fingerprint gate** (§6), including its red proven once.

Nothing more. Everything else in phase 1 begins when the hardware arrives.

## §11 Risks

**Curating does not parallelize.** It is the only item that can genuinely break
the December deadline, and it can neither be delegated nor accelerated by
tooling — only started earlier. That is why it comes before everything else.

**The desktop render does not prove how it sounds on the Touch.** The screening
decides *musical content* — notes, not boards. The open block-rate artifact in
`shell/README.md` stands beside it as the warning: sound identity between render
and board is not a given on this project, it is a measurement of its own. It
belongs to bring-up, not to curation.

**Twelve good places might not come together.** If the generator is so far off
that screening plus rehearsal cannot produce twelve places that carry, the real
diagnosis is a different one: then the fault lies in the macro mapping itself and
not in the hit rate. What that would look like: the survivors all sound alike, or
they only survive because individual macros do nothing. In that case this spec
gets replaced, not patched.
