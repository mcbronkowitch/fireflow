# Spotykach — Scale List Expansion

Date: 2026-07-23
Status: approved (brainstorm 2026-07-23)
Supersedes parts of: `2026-07-11-spotykach-scales-design.md` — the 6-entry
scale table and the "larger scale lists rejected" line at the end of that
spec. Everything else there (one global scale, per-part quantize mode,
relative ALT+TUNE stepping, change slew, V/Oct summing, persistence) is
unchanged and remains binding.

## Problem

The scale list has six entries, all Western: two pentatonics, three modes,
whole tone. Practical scales that players reach for are missing —
Mixolydian, harmonic minor, blues-adjacent pentatonics — and so is the
entire non-Western vocabulary the instrument's percussive, drone-leaning
character invites. Handpan players in particular ask for scales by name
(Hijaz, Pygmy, Equinox), and none of those names currently resolve to
anything.

The 2026-07-11 spec rejected a larger list on the grounds that there is no
display and every scale must be blind-navigable. That constraint stands;
this spec answers it with structure rather than with brevity.

## Scale list

Thirteen entries in three groups. Dark → bright ordering is preserved
*within* each group; the groups themselves are ordered familiar → exotic.
Bit i set = semitone i above the root is allowed.

| # | Enum | Scale | Semitones | Mask |
|---|------|-------|-----------|------|
| | | **A — Modes** | | |
| 0 | `SCALE_AEOLIAN` | Aeolian | 0 2 3 5 7 8 10 | `0x05AD` |
| 1 | `SCALE_DORIAN` | Dorian *(default)* | 0 2 3 5 7 9 10 | `0x06AD` |
| 2 | `SCALE_MIXO` | Mixolydian | 0 2 4 5 7 9 10 | `0x06B5` |
| 3 | `SCALE_LYDIAN` | Lydian | 0 2 4 6 7 9 11 | `0x0AD5` |
| | | **B — Pentatonics** | | |
| 4 | `SCALE_HIRAJOSHI` | Hirajoshi | 0 2 3 7 8 | `0x018D` |
| 5 | `SCALE_PYGMY` | Pygmy | 0 2 3 7 10 | `0x048D` |
| 6 | `SCALE_MIN_PENT` | Minor pentatonic | 0 3 5 7 10 | `0x04A9` |
| 7 | `SCALE_KUMOI` | Kumoi | 0 2 3 7 9 | `0x028D` |
| 8 | `SCALE_MAJ_PENT` | Major pentatonic | 0 2 4 7 9 | `0x0295` |
| | | **C — Exotic / handpan** | | |
| 9 | `SCALE_PHRYGIAN` | Phrygian | 0 1 3 5 7 8 10 | `0x05AB` |
| 10 | `SCALE_HIJAZ` | Hijaz (Phrygian dominant) | 0 1 4 5 7 8 10 | `0x05B3` |
| 11 | `SCALE_HARM_MIN` | Harmonic minor (*Equinox*) | 0 2 3 5 7 8 11 | `0x09AD` |
| 12 | `SCALE_WHOLE` | Whole tone | 0 2 4 6 8 10 | `0x0555` |

`SCALE_LIST_COUNT` becomes 13. Boot default stays Dorian, now index 1.

**Why Phrygian sits in group C, not with the modes.** It shares the minor
second with Hijaz, and that interval — not its mode membership — is what a
listener hears. The two belong next to each other on the sweep.

**Why Hirajoshi, Pygmy and Kumoi are adjacent.** All three share the core
`0x008D` (0 2 3 7) and differ only in their fifth note (b6 / b7 / 6).
Stepping across them reads as one scale being bent, not as three jumps.

**On handpan scales generally.** A handpan scale is a note *layout* — pitches
in a fixed octave arrangement — not a pitch-class set. Reduced to a 12-bit
mask, most of the well-known ones collapse onto entries the list already
has: Kurd, Amara and Annaziska become Aeolian; Celtic Minor becomes Aeolian
or minor pentatonic. The genuinely new masks are Hijaz (Integral), harmonic
minor (Equinox, Mystic) and Pygmy. The value of the handpan framing is
therefore the *names*, which is why the display strings carry them.

## Blind navigability

The old spec required every scale to be reachable without a display. Six
entries met that by being few enough to count. Thirteen meets it by being
grouped: four modes, five pentatonics, four exotic. A player counts blocks
and lands in a neighbourhood, then steps within it — and because each group
is internally ordered dark → bright, the direction of travel still means
what it meant before.

Relative ALT+TUNE stepping (2026-07-11 spec) becomes ~1/13 of knob travel
per step instead of ~1/6. The hardware UI is not wired yet, so this is a
note for the UI milestone rather than a change to be made now.

## Architecture

**`engine/pitch/quantizer.h`** — the `ScaleId` enum and `SCALE_MASKS` grow to
the table above. The quantizer itself is untouched: it consumes a mask and
knows nothing about which scales exist.

**New: `SCALE_NAMES[SCALE_LIST_COUNT]`** — display strings, living beside
`SCALE_MASKS` in the same header so engine, VCV host and scenario parser
share one source of truth and cannot drift apart. Names match the table's
Scale column.

**`host/vcv/src/Spotymod.cpp`** — the SCALE knob is a snapped int param over
`0 .. SCALE_LIST_COUNT-1` and already picks up the new count automatically.
Two changes:

- A `ScaleQuantity : ParamQuantity` overriding `getDisplayValueString()` to
  return `SCALE_NAMES[index]`, following the existing `FluxRateQuantity` /
  `DustQuantity` / `RotQuantity` pattern in the same file. A bare index in
  the tooltip was tolerable at six entries; at thirteen it is not.
- The init patch keeps `SCALE_LYDIAN` and therefore keeps sounding as it
  does today — the enum name resolves to the new index at compile time.

**`host/render/scenario.cpp`** — `parse_scale_name` gains the
seven new names: `mixo`, `hirajoshi`, `pygmy`, `kumoi`, `phrygian`, `hijaz`,
`harm_min`. The six existing names keep their meaning. Unknown input still
falls back to Dorian.

## Saved-patch compatibility

VCV stores params as bare numbers, so the old indices point at different
scales after this change: a patch saved in Lydian (old 4) opens in
Hirajoshi (new 4).

**Decision: no migration.** The changelog records the shift. A remap in
`dataFromJson` keyed on a schema-version field was considered and rejected
as a migration path not worth carrying forward at this stage of the project.
Nothing in the codebase is affected — every reference goes through the enum
names, including the init patch — and scenario JSONs reference scales by
name, so the render/test corpus is untouched. The exposure is limited to
`.vcv` files users saved from an earlier release.

## Tests

- `test_quantizer.cpp` already iterates `0 .. SCALE_LIST_COUNT` and picks up
  the new scales with no change.
- **New mask sanity test:** for every entry, bit 0 is set (the root is always
  allowed), the mask fits in 12 bits, and its population count matches the
  note count in the table above. A mistyped hex digit otherwise surfaces only
  as a melody that sounds slightly wrong.
- **New name test:** `test_scenario.cpp` gains cases for two of the new names
  (`hijaz`, `hirajoshi`) asserting they resolve to the right index, plus one
  confirming an unknown name still yields Dorian.
- `test_chord.cpp` and `test_part.cpp` reference scales by enum name and need
  no change.

## Documentation

`2026-07-11-spotykach-scales-design.md` gets a supersession note above its
scale table and beside its "larger scale lists" rejection, in the same style
the file already uses for the M6 changes — the old spec stays readable as
what it was rather than being rewritten.

## Out of scope

- Per-part scales (rejected in the 2026-07-11 spec; unchanged).
- User-defined scales.
- Scales that need more than 12 pitch classes. Handpan layouts with octave
  doublings and a specific note order cannot be expressed as a pitch-class
  mask — that is a layout question, not a quantizer question.
- Wiring the hardware ALT+TUNE stepping to the longer list; that belongs to
  the UI milestone.
