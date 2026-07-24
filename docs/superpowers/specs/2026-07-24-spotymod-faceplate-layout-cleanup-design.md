# Spotymod faceplate layout cleanup

**Date:** 2026-07-24

**Status:** approved in visual review

**Visual direction:** Twin Islands + Connected Fields + Quiet Technical

## Goal

Calm and reorganize the current 42 HP Spotymod VCV faceplate without removing
or changing any control, function, jack, light, or parameter ID.

The two LED rings remain the visual and physical signature. Their current VCV
positions are not hardware constraints and are deliberately moved. The lower
control area receives a consistent grid, narrower VOICE groups, wider FX
groups, functional control ordering, and mirrored background fields that make
the effect families legible without adding visual noise.

## Scope and invariants

- Panel size remains **42 HP**, `213.36 × 128.5 mm`.
- Both LED rings remain, at their existing rendered size.
- Every current knob, button, switch, light, jack, label, and function remains.
- `PARAMS` order in `host/vcv/res/gen_panel.py` remains byte-for-byte
  unchanged. Coordinates and labels may change; enum order may not.
- Existing `.vcv` parameter IDs must not move.
- Deck B is a full geometric mirror of deck A for every deck-local element:
  controls, labels, secondary labels, sector graphics, and FX background masks.
- DSP, defaults, control ranges, tooltips, and behavior are out of scope.

## 1. Overall geometry: Twin Islands

The two ring/orbit assemblies move slightly upward and outward. This opens the
center spine and creates more air between the orbit and the lower control
groups.

| Element | Deck A | Deck B |
|---|---:|---:|
| Ring center X | `39.50 mm` | `173.86 mm` (`W - 39.50`) |
| Ring center Y | `34.50 mm` | `34.50 mm` |
| LED dot radius | `16.00 mm` | `16.00 mm` |
| Macro orbit radius | `25.50 mm` | `25.50 mm` |

The current nine macro positions and semantic sector order remain:

| Angle | Control | Sector |
|---:|---|---|
| `0°` | RATE | MOTION |
| `40°` | DENS | MOTION |
| `80°` | SMTH | MOTION |
| `120°` | SHAPE | TIMBRE |
| `160°` | MOD | TIMBRE |
| `200°` | RANGE | PITCH |
| `240°` | MELO | PITCH |
| `280°` | TUNE | PITCH |
| `320°` | COLOR | PITCH |

Orbit labels continue to sit radially outside their knobs. Their placement is
recomputed from the new center and radius rather than shifted individually.
The radial-label rule changes with the compact orbit so the top labels do not
clip:

- upper labels: `30.70 mm` label radius
- side labels: `31.70 mm` label radius
- lower labels: `31.30 mm` label radius
- the existing anchor and baseline-shift rules remain

Sector wedges retain their current angular ranges but use an annulus from
`20.50` to `31.00 mm` around the new center. Deck-A sector captions sit at:

- PITCH: `(9.00, 8.20 mm)`
- MOTION: `(70.00, 8.20 mm)`
- TIMBRE: `(70.00, 67.00 mm)`

Deck B mirrors the X coordinates.

## 2. Lower deck geometry

VOICE becomes narrower and FX becomes wider while their combined footprint and
the PLAY width stay unchanged.

### Deck A boxes

| Group | X | Y | Width | Height |
|---|---:|---:|---:|---:|
| VOICE | `4.00` | `72.40` | `31.50` | `24.50` |
| FX | `38.00` | `72.40` | `44.00` | `24.50` |
| PLAY | `4.00` | `98.60` | `78.00` | `12.60` |

Deck B mirrors these boxes around the panel center.

The VOICE/FX gap remains `2.50 mm`. Both groups use the same horizontal base
pitch for small knobs:

- VOICE centers: `9.25`, `19.75`, `30.25 mm`
- FX centers: `44.25`, `54.75`, `65.25`, `75.75 mm`
- Common pitch: `10.50 mm`
- Row centers: `77.30` and `89.40 mm`

PLAY retains all current controls and the current two-part rhythm:

- mode/record cluster: ENG, GRIT, REC, record LED
- sequencer cluster: STPS, STEP, PRIN, NEW, TRIG

The two clusters are distinguished by spacing and a very light tonal change,
not by an additional divider line.

## 3. VOICE functional order

VOICE is organized as three vertical functional pairs. Deck A reads from the
outer panel edge toward the LED ring:

| Column | Top | Bottom | Function |
|---|---|---|---|
| 1 | ATK | DEC | envelope |
| 2 | FILT | RES | filter |
| 3 | SUB | DTUN | source and tuning |

Deck B mirrors the complete order. On screen it therefore reads
`SUB · FILT · ATK` over `DTUN · RES · DEC`, preserving the same
ring-to-edge relationship as deck A.

## 4. FX order and Connected Fields

Deck A uses this two-row order:

```text
RATE   MIX   FB    ROOM
DUST   ROT   GRIT  COMP
```

Panel labels remain short. The existing parameter names and tooltips remain
unchanged; `RATE`, `MIX`, and `FB` are the faceplate presentation of the
existing FLUX controls.

The order reflects three functional families:

1. **FLUX / TAPE:** RATE, MIX, FB, DUST, ROT
2. **GRIT / dynamics:** GRIT, COMP
3. **ROOM:** the per-deck room control

FLUX owns two connected low-contrast fields: top columns 1–3 and bottom
columns 1–2. GRIT/COMP share the bottom-right field. ROOM occupies the
top-right field.

Deck B mirrors both the control order and all field masks:

```text
ROOM   FB    MIX   RATE
COMP   GRIT  ROT   DUST
```

The mirrored field rule is mandatory:

- Deck B FLUX top field anchors right.
- Deck B FLUX bottom field anchors right.
- Deck B ROOM field sits top-left.
- Deck B GRIT/COMP field sits bottom-left.

No deck-local background may reuse deck A coordinates without mirroring.

## 5. Center spine and spacing

The existing BLEND, TIME, DUO, and ROOM boxes remain in their current
full-height sequence and keep `2.5 mm` visual gaps.

Small-knob three-column rows in DUO and ROOM move to one common grid:

```text
CX - 10.50 mm
CX
CX + 10.50 mm
```

TIME remains a two-column story because it contains a mixed switch/knob grid.
BLEND retains the larger MORPH control and smaller TIDE control. Mixed-size
groups are optically centered; they are not forced onto the small-knob grid.

The ROOM bottom edge remains flush with the PLAY boxes at `111.20 mm`.

## 6. Surface treatment: Quiet Technical

The LED rings and macro controls remain the dominant gesture. Static faceplate
graphics become quieter.

### Palette

| Token | Value | Use |
|---|---|---|
| PAPER | `#F7F4EC` | existing main plate |
| PAPER_DEEP | `#EDE5D6` | existing group-fill source |
| INK | `#171713` | primary labels |
| MUTED | `#656056` | existing legends and secondary labels |
| LINE | `#D7CDBB` | existing group outlines |
| DECK_A | `#1D6F5F` | existing ring and deck-A identity |
| DECK_B | `#B96532` | existing ring and deck-B identity |
| FX_FLUX | `#DFE5DC` | connected FLUX/TAPE field |
| FX_GRIT | `#E6DDD1` | GRIT/COMP field |
| FX_ROOM | `#E8E0D4` | per-deck ROOM field |

### Graphic weights

- Group outline: `0.30 mm`, reduced from `0.35 mm`.
- Group fill: PAPER_DEEP with SVG `fill-opacity="0.45"`.
- FX family fields use the exact FX_FLUX, FX_GRIT, and FX_ROOM tokens above.
- Orbit sector fills use SVG `fill-opacity="0.045"`, reduced from `0.07`.
- Small knobs remain bare graphite without accent collars.
- Orbit knobs retain deck-color collars.
- MORPH retains its split deck-color collar.
- Pads remain without accent halos.
- Output wells remain dark because they communicate signal direction.

Color is reserved for:

- LED rings and active ring glow
- orbit collars
- the MORPH bridge
- deck/CV identity where it disambiguates A from B

Color is removed from decorative secondary text.

## 7. Typography and labels

- Primary control labels: current condensed utility face, `1.9 mm`, INK.
- Group legends: `1.8 mm`, MUTED, consistent chip height and left inset.
- Sector captions: same utility face, common size and radial offset.
- Secondary sampler aliases `SCAN`, `LEN`, and `ORG`: MUTED, not deck color.
- Faceplate text stays uppercase and short.
- Every small-knob label uses the same baseline offset from the glyph.
- Radial orbit labels use explicit anchor and position data from the generated
  header; no runtime label-placement special cases are added.

The panel must remain legible at normal Rack zoom. Final implementation review
checks 100%, 75%, and 50% Rack zoom; 50% is a hierarchy check, not a promise
that every secondary label remains readable.

## 8. Implementation boundaries

Primary implementation file:

- `host/vcv/res/gen_panel.py`

Generated outputs:

- `host/vcv/res/Spotymod.svg`
- `host/vcv/src/generated_panel.hpp`

Expected test updates:

- `host/vcv/res/test_panel.py`

`host/vcv/src/Spotymod.cpp` remains unchanged. The existing generated label
metadata already carries explicit positions, anchors, sizes, and colors; this
redesign changes generator data only.

## 9. Verification

The implementation plan must include:

1. Snapshot the complete `ParamId` order before editing.
2. Add or update geometry tests for ring centers, orbit radius, group boxes,
   VOICE/FX control centers, and center-grid columns.
3. Add explicit mirror tests for every deck-local control, label, and FX field.
4. Regenerate SVG and header from `gen_panel.py`.
5. Assert the post-change `ParamId` order exactly matches the snapshot.
6. Run `host/vcv/res/test_panel.py`.
7. Build the VCV plugin.
8. Render and inspect the panel in Rack at 100%, 75%, and 50% zoom.
9. Compare the rendered panel against the approved geometry for overlaps,
   clipped labels, unequal margins, incorrect field masks, or lost hierarchy.

## Acceptance criteria

- Panel remains exactly 42 HP.
- Both LED rings render at the approved Twin Islands coordinates.
- All current controls, functions, lights, and jacks remain present.
- Parameter ID order is unchanged.
- VOICE and FX use the approved widths and shared `10.5 mm` horizontal pitch.
- VOICE uses the approved vertical functional pairs.
- FX uses the approved order and Connected Fields.
- Deck B controls, labels, and background fields are exact mirrors of deck A.
- Center three-column groups use the shared `10.5 mm` pitch.
- Static graphics follow Quiet Technical contrast and color restraint.
- No visible overlaps or clipped labels occur at normal Rack zoom.
- Panel generator tests and the VCV build pass.
