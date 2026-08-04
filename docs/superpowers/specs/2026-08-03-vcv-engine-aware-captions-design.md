# VCV engine-aware panel captions — design

**Date:** 2026-08-03
**Status:** approved in design discussion
**Scope:** VCV Rack panel captions, one host wiring change, and the generator
that feeds both; no Daisy Seed hardware-panel change, no engine DSP change
**Compatibility:** none required. This is a dev-branch instrument; saved
`.vcv` patches are allowed to break. No parameter id is preserved on purpose,
no migration is written, and no caption decision is constrained by what an old
patch stored.

## 1. Problem

The faceplate lettering is a snapshot of one engine printed onto a panel that
now hosts five. Three distinct failures have accumulated:

**Words that lie.** `ATK`, `DEC`, `RES`, `SUB` and `FILT` are pushed to every
engine through `Part::set_voice_*`, but the receiving engines do very different
things with them. On BODY, `DEC` is damping and `SUB` is the excitation bus
level. On the BBD, `DEC` trims the tail below unity, `RES` tilts the feedback
path, `SUB` is the input level and `FILT` is the loss-pole corner. The panel
says `DEC RES SUB FILT` on all five.

`MELO` is wrong on *every* engine including the Synth: the parameter is
`Instrument::set_variation`, the bipolar `RENEW ← LOOP → GROW` axis. Nothing
about it is a melody control.

**Words printed twice.** Five captions collide on one plate:

| Word | First occurrence | Second occurrence |
|---|---|---|
| `RATE` | orbit macro (modulator rate) | FX box (FLUX division) |
| `GRIT` | PLAY pad (Drive/Reduce mode) | FX box (grit mix) |
| `TIME` | centre group legend | FX box (tape time multiplier) |
| `ROOM` | centre group legend | FX box (per-deck reverb send) |
| `DRIVE` | centre `MASTER_DRIVE` | BBD's live `SOURCE` caption |

**Second words printed permanently.** `SAMPLER_LBL` prints `SCAN` beside `MELO`
and `LEN` beside `SUB` on the static plate, on both decks, on all five engines.
They are true on exactly one. They also drag a whole private geometry with them
(`sampler_texts()`, `SAMPLER_GAP`, `SAMPLER_RADIAL`, `MONO_ADV`, `text_w`,
`mirror_label`, `mirror_anchor`) whose only job is to seat a word that should
not be there in the first place.

Two controls already do the right thing and are the model for everything
below: `SOURCE` reads `TIMB` / `ORG` / `FRAME` / `MATL` / `DRIVE` from the live
engine state, and the upper-left VOICE slot reads `ATK` or `PITCH`. Both are
hand-written special cases in `PanelText::draw`; a third would establish a
pattern of accumulating them.

## 2. Decisions

1. **Every caption that depends on state is driven by a table, not by code.**
   The table lives in `res/gen_panel.py`, the single source of truth that
   already guarantees the SVG preview and the Rack widgets cannot drift apart.
2. **One control loses a job.** In the Sampler, `MELODY` drives `sampler_scan`
   only; `set_variation` receives a fixed `0` (LOOP) there.
3. **`DENSITY` and `COLOR` keep both jobs and one word each.** Their two
   meanings point the same direction, so a single caption is accurate rather
   than a compromise (see §6).
4. **The FX box yields on every naming collision.** Orbit `RATE`, centre `TIME`
   and centre `ROOM` keep their words; the FX captions move.
5. **A control that does nothing is not shown.** `REC` is visible only on a
   Sampler deck, the same rule the BBD `PITCH`/`ATK` pair already follows.
6. **The static SVG renders the Synth column** and nothing else. It is the
   module-browser and website preview, and after this change it is true for
   the first time instead of Synth-plus-Sampler-leftovers.

## 3. Caption matrix

Engine states are the existing `ENG` order: **0 Synth · 1 Sampler · 2 Wave ·
3 Body · 4 BBD**. Bold marks a word that changes.

### 3.1 Orbit macros

The nine orbit knobs are modulator macros pushed to `SuperModulator`, identical
on every engine. Only one carries an engine-specific meaning.

| Param | Synth | Sampler | Wave | Body | BBD |
|---|---|---|---|---|---|
| `MELODY` | **VARY** | **SCAN** | **VARY** | **VARY** | **VARY** |

`RATE`, `SHAPE`, `DENS`, `SMTH`, `RANGE`, `MOD`, `TUNE` and `COLOR` are
unchanged and carry no table entry.

### 3.2 VOICE row

| Param | Synth | Sampler | Wave | Body | BBD |
|---|---|---|---|---|---|
| `ATTACK` | ATK | ATK | ATK | **HIT** | — ¹ |
| `STAGES` | — ¹ | — ¹ | — ¹ | — ¹ | **BEND** |
| `FILT` | FILT | FILT | FILT | **BRITE** | **LOSS** |
| `SUB` | SUB | **LEN** | SUB | **EXCIT** | **INPUT** ² |
| `DECAY` | DEC | DEC | DEC | **DAMP** | **TAIL** |
| `RES` | RES | RES | RES | **CHAR** | **TILT** |
| `SOURCE` | TIMB | ORG | FRAME | MATL | DRIVE |

¹ `ATTACK` and `STAGES` share one physical slot and are already swapped by
visibility, not by caption (`EngineExclusiveTrimpot`, spec 2026-08-02). That
mechanism stays; the difference here is that `ATTACK`'s caption gains a Body
entry, so the slot's word now comes from whichever widget is visible.

² Shipped as `FEED` and renamed to `INPUT` on 2026-08-04 — see §3's near-miss
note, which recorded the collision this word had to be pulled out of.

Word sources, so a later reader can check each against the code rather than
against taste:

- Body `HIT` / `DAMP` — `BodyVoice::set_env_times(attack_s, decay_s)` is
  commented *"exciter length, damping"*.
- Body `CHAR` — `BodyVoice::set_resonance`, *"exciter character"*.
- Body `EXCIT` — `BodyVoice::set_sub_level`, *"excitation bus level"*.
- Body `BRITE` — `BodyVoice::set_cutoff_hz`, *"brightness"*.
- BBD `TAIL` — `BbdEngine::set_decay`, *"a trim BELOW k0"*.
- BBD `TILT` — `BbdEngine::set_resonance`, *"the feedback-path tilt"*.
- BBD `INPUT` — `BbdEngine::set_sub`, *"the input level"*.
- BBD `LOSS` — `BbdEngine::set_filt`, *"the loss-pole corner"*.
- Sampler `LEN` — `SUB_A/B` is the `LANE_SIZE` base on a Sampler deck.
- Sampler `SCAN` — `Instrument::sampler_scan`.
- `VARY` — `Instrument::set_variation`, `-1..+1 RENEW ← LOOP → GROW`.
- BBD `BEND` — see §3.3; `STAGES` is a static rename, not a table entry.

Wave is `SynthEngineT<VoiceT<WtOsc>>` — the same voice edit layer as Synth with
a different oscillator, so its column matches Synth except for `SOURCE`.

### 3.3 Collision renames

| Param | Old | New | Reason |
|---|---|---|---|
| `FLUXRATE_A/B` | `RATE` | **DIV** | It selects a division (`flux_division_index`). Frees `RATE` for the orbit macro, and reads as a pair with `MULT`. |
| `FLUXTIME_A/B` | `TIME` | **MULT** | It multiplies that division ×0.25…×4. Frees `TIME` for the centre legend. |
| `REV_MIX_A/B` | `ROOM` | **SEND** | `Instrument::set_reverb_mix`'s own comment calls it the deck's SEND. Frees `ROOM` for the centre legend. |
| `MASTER_DRIVE` | `DRIVE` | **PUSH** | It is `_limiter.set_drive`, not a distortion stage. Frees `DRIVE` for the BBD `SOURCE` caption. |
| `GRITMODE_A/B` | `GRIT` | **SAT** / **CRSH** | A mode pad should show its mode, not its block's name. Frees `GRIT` for the grit-mix knob beside it. |
| `STAGES_A/B` | `PITCH` | **BEND** | Collides with the `PITCH` sector eyebrow over the orbit. The README's own account of the control is a bend: a bucket-brigade line's first pass is always at unity pitch, and only recirculating repeats sample the moved clock more than once. Revises spec 2026-08-02, which named this slot `PITCH`. |

`GRITMODE` is the one dynamic caption that does **not** follow `ENG`: it
follows its own value (`configSwitch` states `{"Drive", "Reduce"}` → `SAT`,
`CRSH`). §4 accommodates this rather than special-casing it.

### 3.4 Uniqueness

After the renames every word printed on the plate is unique across all five
engine states, all group legends and all sector eyebrows. This was verified
against the current generator before the plan was written, and it is what
turned up the `PITCH` collision in §3.3 that six months of reading the panel
had not.

Jack captions are outside the rule on purpose: `PIT`, `GATE`, `L` and `R`
each appear twice across the five jack groups, and the generator's own comment
records why — the groups sit on differently coloured wells with their own
legends, which is what disambiguates them.

One near-miss was recorded here deliberately rather than hidden: `FB` (FLUX
feedback, FX box) and `FEED` (BBD input level, VOICE box) are phonetically
close. The argument for keeping them was that they sit in different fieldsets,
differ in length, and never appear in the same box; the alternatives considered
at the time (`IN` collides with the jack group legend, `LEVEL` collides with
the lane name) were worse.

**Overturned 2026-08-04.** That argument undercounted the collision. It is not
two words but three: the BBD's strongest control is the MOTION lane read as
FEEDBACK, so a four-letter `FEED` sitting on a panel that also prints `FB`
reads as a third feedback control — and an input level is the one thing this
knob is not. `INPUT` was never on the original shortlist. It is five letters,
a length `EXCIT` already carries in this exact slot, so it costs the layout
nothing. The word is now `INPUT`.

## 4. Mechanism

### 4.1 Generator

`res/gen_panel.py` gains one table:

```python
# (target param base, driver param base, words indexed by the driver's value)
DYNAMIC_CAPTIONS = [
    ("MELODY", "ENGINE",   ("VARY", "SCAN", "VARY", "VARY", "VARY")),
    ("SOURCE", "ENGINE",   ("TIMB", "ORG", "FRAME", "MATL", "DRIVE")),
    ("ATTACK", "ENGINE",   ("ATK", "ATK", "ATK", "HIT", "ATK")),
    ("DECAY",  "ENGINE",   ("DEC", "DEC", "DEC", "DAMP", "TAIL")),
    ("RES",    "ENGINE",   ("RES", "RES", "RES", "CHAR", "TILT")),
    ("SUB",    "ENGINE",   ("SUB", "LEN", "SUB", "EXCIT", "INPUT")),
    ("FILT",   "ENGINE",   ("FILT", "FILT", "FILT", "BRITE", "LOSS")),
    ("GRITMODE", "GRITMODE", ("SAT", "CRSH")),
]
```

A **driver** column instead of a hardcoded "the engine decides" rule is what
lets `GRITMODE` live in the same table as everything else. `ENGINE` as a driver
resolves per deck: the `_A` target takes `ENGINE_A`, the `_B` target takes
`ENGINE_B`. A self-driving entry (`GRITMODE`) resolves to its own id.

The generator expands each row into both decks and emits:

```cpp
struct DynCaption { int id; int driverId; int count; const char* words[5]; };
static const DynCaption kDynCaptions[] = { … };
```

The static SVG writes `words[0]` for every entry, which is the Synth column by
construction. `ATTACK`'s BBD column is never rendered statically because the
existing `STATIC_PANEL_PARAMS` filter already drops `STAGES_A/B` from the
preview and the BBD `ATTACK` cell is a don't-care (the slot shows `STAGES`
there).

### 4.2 Runtime

`PanelText::draw` loses `sourceCaption()`, `sourceCaptionAt()` and
`attackPitchCaptionAt()`. The single `captions()` loop gains one lookup: if the
`PanelCtl`'s id appears in `kDynCaptions`, draw
`words[clamp(round(params[driverId].getValue()), 0, count-1)]`; otherwise draw
`c.label`. Position, anchor, size and colour keep coming from the same
`PanelCtl`, so a dynamic caption can never land anywhere its static twin would
not have.

`isBbdSelected` and `roundedEngineState` survive — the BBD widget visibility
still needs them.

### 4.3 Visibility

`EngineExclusiveTrimpot` generalises into a small mixin over the widget type,
so the same rule can gate a `Trimpot` (the existing `ATTACK`/`STAGES` pair) and
a `VCVLatch` (`REC_A/B`). Its predicate becomes an engine-state set rather than
a `bbdOnly` bool:

- `ATTACK_A/B` — visible when the deck's engine is **not** BBD (unchanged)
- `STAGES_A/B` — visible when the deck's engine **is** BBD (unchanged)
- `REC_A/B` — visible when the deck's engine **is** Sampler (new)

The `REC` LED is untouched: `kLightCtls`'s brightness already goes dark on any
non-Sampler engine.

### 4.4 Host wiring — the one functional change

In `Spotymod::pushParams`, `inst.set_variation(p, pp(MELODY_A, p))` currently
fires unconditionally at the top of the per-deck block, before this tick's
`set_engine` has run. It moves down beside the existing `sampler_scan` call,
where `samplerPart` is already resolved from the live `engine_id(p)`, and
becomes:

```cpp
inst.set_variation(p, samplerPart ? 0.f : pp(MELODY_A, p));
if (samplerPart) inst.sampler_scan(p, pp(MELODY_A, p));
```

This is the same shape as the `LANE_SIZE` gate directly below it, which parks
the base at `0.5f` off the Sampler. The neutral value for variation is `0`
(LOOP), the centre of the bipolar range.

Consequence, stated plainly: a Sampler deck's phrases stop renewing on their
own. `NEW` still queues a fresh A/B pair on demand, and the grain cloud's own
motion is unaffected — but a Sampler in STEP now repeats what `NEW` last drew
until asked for another. That is the accepted price of `MELO`/`SCAN` being one
knob with one honest word.

## 5. Testing

The panel suite is the contract, and this change deletes part of it. That is
intended: the tests below pin the inline geometry of the printed second words,
and they describe a solution being removed, not a behaviour being preserved.

**Removed outright** — six tests whose entire subject is the printed second
word: `test_sampler_captions_exist`,
`test_sampler_words_sit_inline_behind_their_caption`,
`test_scan_sits_outward_of_melo_and_clear_of_its_knob`,
`test_sampler_centred_captions_hand_their_centring_to_the_pair`,
`test_sampler_radial_caption_did_not_move`,
`test_sampler_inline_pairs_fit_the_voice_row`.

**Amended:** `test_all_deck_local_geometry_is_exactly_mirrored` loses its
sampler-alias arm and keeps everything else — deck-local mirroring is still a
hard invariant, there is simply one fewer text pair to mirror.

**Generalised:** `test_source_caption_geometry_for_every_engine_state` is the
template for new test 1 below. It already walks all five engine states for one
control; it becomes a walk over every `DYNAMIC_CAPTIONS` row × every state of
that row's driver, and `SOURCE` stays covered as one of those rows.

**Added:**

1. **Table coverage** — every `DYNAMIC_CAPTIONS` row has exactly as many words
   as its driver has states, and every dynamic target id exists in `PARAMS`.
2. **Uniqueness** — the union of all static captions, all dynamic words, all
   group legends and all jack captions contains no duplicate. This test is the
   one that would have caught `RATE`/`RATE` and `GRIT`/`GRIT` years ago; it is
   the durable half of this spec.
3. **No stale word** — `SCAN` and `LEN` appear in no static text table, and
   `sampler_texts` no longer exists.
4. **Host wiring guard** — `PanelText::draw` contains no per-control caption
   special case; the `kDynCaptions` lookup is the only path. Follows the
   existing `test_*_host_wiring` / `test_*_guard_rejects_representative_
   regressions` pair pattern.
5. **Variation gate** — `set_variation` appears exactly once in `pushParams`
   and is gated by the same `samplerPart` expression the other Sampler-only
   pushes use.
6. **Visibility** — `REC_A/B` are wired to the visibility mixin with the
   Sampler predicate.

Each new test must be shown red once before its implementation lands: a wrong
word in the table, a duplicated caption, or an ungated `set_variation` must
each produce a failure. A caption test that cannot go red is decoration.

`test_no_overlap`, `test_on_panel`, `test_orbit_positions` and the mirror tests
stay untouched and must still pass — no coordinate moves in this change.

## 6. Alternatives considered

**Make `DENSITY` and `COLOR` engine-exclusive too.** Rejected. `DENS` is groove
density on every engine and additionally grain overlap on the Sampler; both
mean *sparser* in the same direction, so one word is accurate rather than a
compromise. `COLOR` is chord colour and additionally FLOW dispersion; both mean
*how widely spread*. Making them exclusive would have cost the Sampler two
modulation axes to fix a problem it does not have.

**Keep the words in C++.** Rejected: `gen_panel.py` exists precisely so the SVG
and the Rack widgets cannot disagree, and caption text is exactly the kind of
thing that drifts when it lives in two places.

**Five per-engine SVGs.** Rejected: no geometry changes between engines, so
four of the five files would differ only in glyph text — five times the
regeneration cost for nothing.

**Physically re-sorting the panel** so engine-dependent controls occupy one
zone. Explicitly out of scope for this change. The layout is learned muscle
memory and the caption problem is solvable without moving a single coordinate.

**Renaming the centre instead of the FX box** (`TIME` → `GRID`, `ROOM` →
`VERB`, orbit `RATE` → `SPEED`). Rejected: it changes more words, and the
centre strip's legends are the panel's coarsest navigation aid.

## 7. Out of scope

- Any Daisy hardware panel work (M6).
- Parameter id changes, preset migration, or soft takeover across `ENG` flips
  — the "knob position holds across engines" line in the VCV README stands.
- The `SIZE` lane base on a BBD deck, which is currently pinned at `0.5f`
  because that is the non-Sampler default. It is a real gap and it is not a
  caption problem.

## 8. Documentation

`host/vcv/README.md` needs three edits after implementation: the "Four controls
take on a different job" table (MELODY's row becomes exclusive, the `MELO`/`SCAN`
slash notation goes away), the SOURCE section (the caption list moves to the
generator), and the BBD section's control mapping (which now has panel words
for `TAIL`, `TILT`, `INPUT` and `LOSS` instead of prose).

The BBD section additionally carries a subsection headed "BBD PITCH and tape
TIME surface" whose two named controls are both renamed here. Both the heading
and its body need to follow `PITCH` → `BEND` and `TIME` → `MULT`, or the
README will document a panel that no longer exists.
