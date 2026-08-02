# VCV BBD PITCH and FLUX TIME surface — design

**Date:** 2026-08-02
**Status:** approved in design discussion
**Scope:** VCV Rack panel and host wiring only; no Daisy Seed hardware-panel change

## 1. Problem

Movement 3 removed the BBD stage-count control from FLUX and restored FLUX as a
stereo tape echo. The VCV panel kept the old `STGS` caption and its position in
the FX box. The saved `STAGES_A/B` parameters are not dead: on a BBD deck they
now supply the `LANE_PITCH` base. The result is technically wired but visually
wrong — an engine control appears to belong to the post-engine FX chain.

At the same time, tape FLUX has one live control without a panel base:
`FXT_FLUX_TIME`. It is fixed at its neutral base (`0.5`) unless the FX target
plane modulates it. The vacated physical position is the natural home for that
tape control.

## 2. Decisions

1. **BBD PITCH moves into VOICE.** On a BBD deck, the upper-left VOICE position
   currently occupied by `ATK` becomes `PITCH` and edits the existing
   `STAGES_A/B` parameter.
2. **BBD ATTACK leaves the visible panel.** Its DSP meaning — freeze
   engage/release ramp — stays intact and remains editable as a context-menu
   slider named `Freeze Attack`.
3. **Other engines keep ATK unchanged.** The upper-left VOICE position continues
   to show and edit `ATTACK_A/B` for Synth, Sampler, Wave and Body.
4. **The old FX position becomes TIME.** A new `TIME` knob occupies the former
   `STGS` coordinates and supplies the `FXT_FLUX_TIME` base.
5. **RATE and TIME remain distinct.** `RATE` chooses the tempo-synchronised base
   division. `TIME` multiplies that base geometrically from `x0.25` through
   `x1` to `x4`; its existing 30 ms tape-time slew intentionally produces
   Doppler motion.
6. **The engine/FX boundary is literal.** The FX box contains only controls for
   tape FLUX, GRIT, COMP and ROOM. No BBD-engine control remains inside it.

## 3. Final per-deck layout

### 3.1 VOICE box

The existing 3-by-2 grid stays in place.

| position | non-BBD engine | BBD engine |
|---|---|---|
| top left | `ATK` (`ATTACK_A/B`) | `PITCH` (`STAGES_A/B`) |
| top middle | `FILT` | `FILT` — BBD loss-pole corner |
| top right | `SUB` | `SUB` — BBD input level |
| bottom left | `DEC` | `DEC` — freeze decay |
| bottom middle | `RES` | `RES` — feedback-path tilt |
| bottom right | engine-aware SOURCE caption | `DRIVE` — saturation inside the BBD loop |

The BBD `ATTACK_A/B` value is not deleted or reassigned. It is merely hidden
from the faceplate while BBD is selected and exposed through the context menu.

### 3.2 FX box

| row | controls |
|---|---|
| top | `RATE`, `MIX`, `FB`, `ROOM` |
| bottom | `LINK`, `TIME`, `GRIT`, `COMP` |

`TIME` uses exactly the former `STGS` coordinates, so the existing balanced
four-column FX grid remains intact. `LINK` remains tape THIN; `GRIT`, `COMP` and
`ROOM` retain their current meanings.

## 4. Parameter schema and patch compatibility

Patch compatibility is an invariant.

- `ATTACK_A/B` keep their ids, range, defaults and saved values.
- `STAGES_A/B` keep their ids, range, defaults and saved values. They continue
  to supply BBD `LANE_PITCH`; only their widget position and caption change.
- Add `FLUXTIME_A/B` as **new trailing parameter ids** after the current final
  parameter. Do not insert them into either part template and do not change
  `PART_STRIDE`.
- `FLUXTIME_A/B` default to `0.5`, the neutral `x1` multiplier. Old patches do
  not contain these ids and therefore load at the neutral default.
- No JSON migration is required. Existing LINK and FORM/SONG schema markers are
  unrelated and remain untouched.
- No parameter is removed, reordered or recycled for a new meaning.

`gen_panel.py` currently derives persistent enum order, runtime widget geometry
and static SVG glyphs from too few collections. That would place any ordinary
new panel control before the existing hidden DETUNE/DRIVE tail, and rendering
both controls at the shared ATK/PITCH coordinates would draw two static glyphs
and captions on top of each other. The implementation must separate all three
concerns (collection names are implementation-local):

```text
ParamId order         = existing PARAMS through DRIVE_B + FLUXTIME_A/B
runtime kParamCtls    = existing visible controls + moved STAGES_A/B + FLUXTIME_A/B
static SVG controls   = runtime controls excluding STAGES_A/B
```

The runtime table deliberately contains both ATTACK and STAGES at one coordinate
because Rack needs two independently saved parameter widgets to switch between.
The SVG deliberately renders only ATTACK there, because its static preview is
the default Synth surface. Geometry tests use the runtime collection but allow
only the two declared ATTACK/STAGES overlaps. Moving `STAGES_A/B` changes only
its runtime coordinates, never its position in persistent enum order.

The old physical STGS position is reused, not the old parameter id. Reusing
`STAGES_A/B` for tape TIME would destroy the BBD PITCH state and couple an
engine parameter to an FX parameter; this design explicitly forbids it.

## 5. Runtime wiring

### 5.1 Audio/control path

Every VCV control tick:

1. Dispatch the selected engine as today.
2. If the resulting engine is BBD, push `STAGES_A/B` to
   `set_target_base(part, LANE_PITCH, value)`.
3. Push `FLUXTIME_A/B` to
   `set_fx_target_base(part, FXT_FLUX_TIME, value)` for every engine, because
   FLUX is the deck's post-engine effect.
4. Keep `FLUXRATE_A/B` as the synced base division and `FLUXFB_A/B` as the
   feedback target base.

No new tape DSP setter is needed. The established path already reaches
`PartFx::process` → `Flux::set_time_mod` → `tape_time_mult` → the shared 30 ms
delay-time slew.

Both Rack `configParam` defaults and the generated/default patch snapshot must
set `FLUXTIME_A/B` to `0.5`; factory reset and first insertion must therefore
agree with an old patch that lacks the new trailing ids.

### 5.2 Engine-aware widget at the ATK position

The Rack widget owns two parameter widgets at the same resolved coordinates:
the existing `ATTACK_A/B` widget and the moved `STAGES_A/B` widget. Exactly one
is visible and interactable:

- BBD selected: show `STAGES_A/B`, caption `PITCH`; hide `ATTACK_A/B`.
- Any other engine: show `ATTACK_A/B`, caption `ATK`; hide `STAGES_A/B`.
- Module-browser preview (`module == nullptr`): show the default Synth surface,
  therefore `ATK`.

The visibility decision reads the corresponding `ENGINE_A/B` Rack parameter,
rounds it exactly as the existing engine dispatcher does, and treats only value
`4` as BBD. It must not read `inst.engine_id()`: that engine-owned state updates
on the throttled control tick and would let visible UI lag the selected Rack
parameter. The same rounded-parameter helper/interpretation is shared by widget
visibility, PITCH/ATK caption and context-menu eligibility. The hidden widget
must not receive pointer events, scroll edits or tooltips.

### 5.3 Text and quantities

- Remove visible `STGS` text from generated SVG/header output and runtime panel
  text.
- The engine-aware VOICE caption is `PITCH` for BBD and `ATK` otherwise.
- Rename the user-facing STAGES quantity to BBD pitch terminology; its display
  remains a normalised pitch percentage unless a more musical display already
  exists in the implementation plan.
- `TIME` displays `x0.25` at 0, `x1` at 0.5 and `x4` at 1, with sensible rounded
  intermediate multipliers. This is the requested multiplier; the existing
  tape-buffer limit may still clamp the resulting absolute delay at the longest
  RATE divisions.
- Tooltips must say `BBD Pitch` and `Tape Time`, never `Stages`.

The committed SVG is a static preview, so it renders the default non-BBD `ATK`
glyph/caption at the shared VOICE position and excludes the moved STAGES glyph
entirely. `kParamCtls` still carries both runtime widgets. Rack's generic caption
loop skips both ATTACK and STAGES at this coordinate, then one engine-aware draw
path emits exactly one caption: `PITCH` for rounded ENG value 4, `ATK`
otherwise. This matches the existing SOURCE-caption pattern without ever
drawing `ATK` and `PITCH` together.

## 6. Context menu

For each deck whose corresponding rounded `ENGINE_A/B` Rack parameter is exactly
`4`, expose an existing-parameter slider:

- `BBD A — Freeze Attack`
- `BBD B — Freeze Attack`

Each slider binds that deck's unchanged `ATTACK_A/B` `ParamQuantity`. It is not
a new hidden parameter and does not duplicate state. A non-BBD deck exposes no
Freeze Attack entry because its ATTACK knob is already visible. Eligibility
must not use `inst.engine_id()` and therefore cannot lag the Rack ENG parameter.
Existing context-menu controls for BBD DRIVE/DETUNE remain unchanged.

## 7. Tests and verification

### 7.1 Panel/schema contracts

Extend `host/vcv/res/test_panel.py` to prove:

- all pre-existing parameter ids retain their order;
- `FLUXTIME_A/B` are appended after the former tail and remain outside
  `PART_STRIDE`;
- `FLUXTIME_A/B` occupy the old `STAGES_A/B` FX coordinates;
- `STAGES_A/B` occupy the corresponding `ATTACK_A/B` coordinates;
- the only allowed exact parameter overlap is the engine-exclusive
  ATTACK/STAGES pair for each deck;
- the generated surface contains `TIME` and contains no visible `STGS`;
- the default static preview contains `ATK`, not an overlapping `ATK/PITCH`;
- runtime source contains mutually exclusive BBD/non-BBD widget visibility and
  engine-aware caption selection;
- tooltips/configuration identify BBD PITCH and tape TIME correctly.
- the generated/default init snapshot tail is exactly `DRIVE_A`, `DRIVE_B`,
  `FLUXTIME_A = 0.5`, `FLUXTIME_B = 0.5`;
- both TIME controls are configured from `initParamDefault(c.id)`, so first
  insertion, factory reset and an old shorter patch converge on neutral TIME.

### 7.2 Routing contracts

Add focused tests or source contracts proving:

- BBD routes `STAGES_A/B` to `LANE_PITCH` exactly once;
- non-BBD operation never routes `STAGES_A/B` into FLUX;
- `FLUXTIME_A/B` route to `FXT_FLUX_TIME` on both decks;
- TIME 0/0.5/1 reaches the existing `x0.25/x1/x4` tape multiplier;
- switching engines changes only widget visibility, not either parameter's
  stored value;
- only decks whose rounded `ENGINE_A/B` parameter equals `4` expose `BBD A/B —
  Freeze Attack`; non-BBD decks expose no such entry;
- each Freeze Attack entry binds the matching existing `ATTACK_A/B`
  `ParamQuantity`.

### 7.3 Full verification

Run at minimum:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
cd host/vcv/res && python test_panel.py
cd .. && ./build-local.sh install
```

After installation, restart Rack and manually verify both decks:

1. Synth/Wave/Body/Sampler show `ATK`; BBD shows `PITCH` at the same VOICE slot.
2. Switching engines preserves both ATK and PITCH values independently.
3. FX shows `TIME`, not `STGS`, for every engine.
4. TIME centre is neutral; at a RATE whose resulting delays fit the tape buffer,
   its endpoints produce the expected quarter/fourfold delay and audible smooth
   tape travel. At the longest RATE divisions the existing buffer clamp remains
   in force.
5. BBD Freeze Attack remains editable from the context menu.
6. In BBD, dragging and scrolling the shared VOICE position changes only
   `STAGES_A/B`; `ATTACK_A/B` remains unchanged and the tooltip says
   `BBD Pitch`.
7. After switching away from BBD, dragging and scrolling the same position
   changes only `ATTACK_A/B`; `STAGES_A/B` remains unchanged and the normal
   attack tooltip returns.

## 8. Non-goals

- No Daisy Seed hardware faceplate or hardware control remap.
- No change to tape filtering, saturation, feedback law, THIN scheduler or
  buffer size.
- No new BBD DSP parameter.
- No reassignment of any existing saved parameter id.
- No redesign of the other VOICE meanings or the nine modulation-ring knobs.

## 9. Definition of done

- No BBD-engine control appears in the VCV FX box.
- BBD `PITCH` occupies the VOICE ATK position only while BBD is selected.
- BBD Freeze Attack remains reachable in the context menu.
- Tape `TIME` occupies the former STGS physical position and controls
  `FXT_FLUX_TIME` over `x0.25..x4` with `x1` at centre.
- Old patches retain all existing parameter meanings and values.
- Generated panel, Rack runtime captions/tooltips, tests and documentation agree.
- Desktop tests, panel tests and the locally installed VCV plugin all pass.
