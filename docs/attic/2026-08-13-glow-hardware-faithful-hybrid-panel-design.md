# Glow hardware-faithful hybrid panel design

**Date:** 13 August 2026

**Status:** Approved for implementation on 13 August 2026.

**Scope:** The Glow VCV panel, a fabrication-ready swappable faceplate for
Synthux Touch 2, and the shared visual sources that keep both representations
aligned.

**Supersedes:** The visual, geometry, and panel-production decisions in
`2026-08-11-glow-touch-2-panel-design.md`. That document remains authoritative
for Glow's musical behaviour, persistence, and workshop menu. This design also
replaces the 5-5-2 pad arrangement and the vector-only dark-copper treatment.

## 1. Goal

Glow should look and behave like a digital rehearsal of the real Synthux Touch
2 that will eventually run the same engine. The VCV module must reproduce the
physical construction rather than flattening the product into a generic Rack
panel:

- the fixed Touch 2 rear PCB and exposed Daisy Seed;
- a diagonally cut, swappable Glow faceplate that FireFlow can design freely;
- the original lower touch PCB, including its gold electrodes, silver artwork,
  and two metal switches;
- subtle VCV-only feedback for pad state.

The Glow faceplate is not merely a screen asset. Its physical KiCad project is
the source of truth, suitable for fabrication by Synthux after they confirm the
final manufacturing profile. VCV receives a render of that physical design.

## 2. Authoritative hardware layout

The official Synthux sources correct two assumptions in the existing Glow
panel.

### 2.1 Physical layers

The top of Touch 2 is not split horizontally. A removable faceplate covers the
front and falls diagonally from the upper-left region toward the exposed
controller area on the right. The two audio jacks at upper left, all six trim
knobs, and both faders belong to this removable faceplate. The visible rear PCB,
Touch branding, and Daisy Seed behind the diagonal opening are fixed Touch 2
hardware.

The lower touch PCB is a separate physical board. It carries the ten lower
capacitive electrodes and both centre-off toggle switches.

### 2.2 Pad mapping

Touch 2 uses a true **10+2** arrangement:

- P00-P09 are the ten gold touch electrodes on the lower touch PCB.
- P10 and P11 are the two gold touch electrodes at the top of the exposed rear
  PCB beside the Touch wordmark.
- The large silver hatched regions on the lower board are decorative PCB
  artwork, not electrodes.
- S09/S10 and S07/S08 identify the two centre-off toggle switches. They are not
  pads.

Glow's current twelve-pad 5-5-2 field is therefore not a hardware replica and
must be retired. P10 and P11 remain fully interactive after moving to the upper
rear-PCB region.

### 2.3 Sources

The design is grounded in public first-party Synthux material:

- [AudreyTouch Faceplate](https://github.com/Synthux-Academy/AudreyTouch/tree/main/Faceplate)
  provides the official faceplate template as AI, DXF, and PDF, a frontal
  P00-P11 layout, and product photographs.
- [simple-hardware](https://github.com/Synthux-Academy/simple-hardware/tree/main/synth-interface-panel)
  provides Synthux's KiCad panel project, footprints, and vector-template
  workflow.
- [How to Design a Synth Interface in KiCad](https://www.youtube.com/watch?v=SU3fhliWxpM)
  documents Synthux's PCB-layer, KiCad, Gerber, and JLCPCB production approach.
- [Touch 2](https://www.synthux.academy/store/simple-touch-2) documents the
  swappable-faceplate product and its physical control complement.

No public Touch 2 Gerber or complete production `.kicad_pcb` was found. The
official DXF is authoritative for the removable faceplate; the labelled frontal
layout and product photographs are authoritative for pad identity and visible
material. Synthux must confirm the final production profile.

## 3. Hybrid visual architecture

The module is assembled from four independently owned visual layers.

### 3.1 Fixed hardware layer

A cleaned high-resolution PNG represents the fixed rear PCB, Touch wordmark,
and exposed Daisy Seed. It is reconstructed from the official frontal sources,
perspective-corrected against known board geometry, and stripped of incidental
lighting, camera distortion, and compression artefacts.

P10 and P11 belong visually to this layer but remain separate interactive pad
paths at runtime.

### 3.2 Swappable Glow faceplate

The faceplate outline, diagonal opening, holes, and slots come from the official
DXF in physical millimetres. FireFlow owns the artwork within those mechanical
constraints. The source is a KiCad PCB design, not a screen illustration.

Knobs, faders, jacks, and screws are not baked into its render. Rack widgets sit
over the rendered plate at the same physical centres.

### 3.3 Lower touch PCB

The lower board uses the ten original P00-P09 silhouettes. Every silhouette is
traced individually from the official labelled layout and checked against the
unlabelled frontal product photographs. Gold linework, black PCB material, and
silver decorative fields use a dedicated raster material layer clipped by
precise vector masks.

The neutral board image contains no selected state and no toggle-lever
position. It contains the static switch hardware underneath the levers.

### 3.4 Runtime state and switch layer

Runtime NanoVG drawing owns live, excursion, and refusal feedback. The two
centre-off switches use custom transparent visual frames for down, centre, and
up positions. Their base hardware is static; lever angle, highlight, and shadow
change with the parameter value.

## 4. Physical faceplate master

The real faceplate is the design master. Its project lives at
`hardware/glow-faceplate/` and includes:

- a `.kicad_pcb` fabrication source;
- the imported official DXF on `Edge.Cuts`;
- non-plated mechanical holes and slots at official coordinates;
- editable vector artwork retained separately from the KiCad import;
- explicit copper, solder-mask, and silkscreen ownership;
- a vendor-neutral Gerber and drill export configuration;
- a 1:1 PDF mechanical proof;
- a rendered preview used to derive the VCV faceplate layer.

The expected visual medium is an FR4 PCB faceplate. Copper creates gold areas
and linework, solder mask controls exposed versus muted copper, and silkscreen
provides an optional light colour. Exact board thickness, solder-mask colour,
surface finish, copper treatment, minimum features, and panelisation remain
manufacturing inputs from Synthux rather than assumptions in FireFlow.

Provisional control names must not enter the fabrication layers. Macro, fader,
and switch assignments receive an explicit freeze gate before final artwork and
Gerber export.

## 5. Asset-production workflow

### 5.1 Geometry recovery

1. Import the official faceplate DXF without rescaling.
2. Rectify the official P00-P11 layout using board edges, mounting holes, and
   the known Touch 2 dimensions.
3. Trace P00-P11 into twelve named closed paths.
4. Compare the paths against at least two unlabelled frontal product images.
5. Store paths in physical millimetres and preserve pad identity throughout.

No generated island may replace an original pad merely because it is easier to
fit. If a reference is ambiguous, the current SVG shape is used temporarily and
marked as unverified; it is not presented as original geometry.

### 5.2 Texture reconstruction

Product photographs are references and possible texture sources, not complete
panel backgrounds. Texture preparation removes:

- perspective and lens distortion;
- directional illumination and cast shadows;
- labels or callouts added to reference images;
- JPEG ringing and enlarged pixel noise;
- photographed control positions that would conflict with live widgets.

Where source quality is insufficient, material is reconstructed procedurally or
manually from the reference rather than enlarged until artefacts become panel
features.

### 5.3 Resolution and export

Raster sources are exported at 4x Rack panel resolution,
960 x 1520 pixels for a 16 HP panel, with alpha where layers overlap. Downsampled
1x and 2x review renders are generated to expose line loss and shimmer early.

Runtime assets are `GlowRear.png`, `GlowFaceplate.png`, and `GlowTouch.png`, plus
transparent `GlowSwitchDown.png`, `GlowSwitchCenter.png`, and
`GlowSwitchUp.png` frames. All six files must be listed explicitly in the VCV
package; the entire generator directory must not be distributed.

## 6. One geometry source for drawing and interaction

The generator emits P00-P11 paths and their bounds into the generated panel
header. Runtime uses those same paths for:

- the visible neutral electrode mask;
- point-in-path hit testing;
- live and refusal collars;
- the excursion inner contour;
- screenshot and geometry tests.

Large rectangular hit areas are not acceptable. Black gaps, silver decorative
regions, and switch hardware must not trigger neighbouring pads. A bounding box
may be used only as a fast rejection test before the exact path test.

Pad numbers are not printed on the reconstructed hardware. P00-P11 remain
available in tooltips and accessibility labels.

## 7. Runtime states

The real board has no per-pad status LEDs, but Rack needs enough feedback to
make Glow's existing place state legible. Feedback preserves the physical pad
silhouette:

| State | Treatment |
|---|---|
| Idle | Natural gold surface only |
| Live curated | Restrained warm-gold outer edge and local halo |
| Live excursion | Live treatment plus one green inner contour |
| Refused hold | Brief muted brick-red outer flash |

P10 and P11 receive exactly the same state treatment as P00-P09. Effects remain
local to the electrode and may not obscure the PCB material or neighbouring
controls.

The custom toggle graphics retain the existing three-position parameter
behaviour and hit areas. This is a visual replacement, not a change to switch
assignments or persistence.

## 8. Loading and failure behaviour

Raster resources load through Rack's cached image API. The panel widget sets its
physical size independently of image success, so a failed asset never collapses
the module or moves controls.

If a required image is missing or invalid, Glow draws a neutral dark fallback
plate, the vector pad outlines, and all interactive widgets. It reports the
missing resource through Rack's log. Audio processing, patch loading, parameter
state, and context menus must remain functional.

## 9. Validation

### 9.1 Fabrication validation

Before ordering:

1. Print the generated mechanical PDF at 1:1 and check every hole, slot, edge,
   and the diagonal opening against Touch 2 or an official Synthux proof.
2. Run KiCad DRC with the confirmed manufacturer constraints.
3. Inspect all Gerbers and drill files in an independent Gerber viewer.
4. Check copper/mask polarity, exposed copper, text orientation, edge clearance,
   and minimum line widths explicitly.
5. Have Synthux approve the production profile, logo use, and final proof.
6. Order a small prototype set and mount one on Touch 2 before a larger run.

### 9.2 VCV automated validation

Tests must cover:

- 16 HP panel dimensions and fixed control centres;
- exactly P00-P09 on the lower board and P10/P11 on the upper rear PCB;
- byte-for-byte generator agreement for committed path data;
- exact-path hit testing, including negative points in black and silver gaps;
- no collision between pad paths and the two switch widgets;
- three visual frames per toggle;
- required asset existence, dimensions, alpha format, and package inclusion;
- fallback rendering with each raster dependency deliberately absent.

Every new assertion must be demonstrated red once. Existing musical-state tests
remain unchanged unless the widget boundary must expose a pure geometry helper.

### 9.3 Visual validation

Capture Rack screenshots at several common zoom levels and on at least the
Windows development build. Check that:

- fine gold lines do not shimmer or disappear;
- the module reads as layered hardware rather than one photograph;
- P10/P11 are discoverable and clickable;
- state feedback is visible without looking like added hardware LEDs;
- stock knobs, faders, jacks, and screws do not duplicate photographed parts;
- switch frames align at all three positions.

## 10. Rights and provenance

Keep a provenance record for every official source, transformation, and exported
asset. Public source-code licensing does not automatically grant rights to
Synthux trademarks, product photography, or all product artwork. Before public
distribution, Synthux must approve:

- use of the Touch/Synthux wordmark and visible product identity;
- reuse or derivative use of official photographs;
- reproduction of the original touch-electrode artwork;
- attribution required in the plugin package and repository.

If photographic reuse is not approved, retain the verified geometry and replace
the photographic material with a clean-room reconstruction.

## 11. Expected deliverables

- fabrication-ready Glow faceplate KiCad project;
- editable source artwork and source-provenance record;
- Gerber, drill, and 1:1 PDF proof outputs for Synthux review;
- twelve verified, named pad paths in physical coordinates;
- cleaned high-resolution rear-PCB and lower-board visual assets;
- three-position toggle artwork;
- generated VCV geometry/header and packaged raster assets;
- updated panel tests, VCV documentation, and production notes.

## 12. Non-goals

- Redesigning Touch 2's fixed rear PCB or lower touch PCB.
- Moving controls away from official physical centres.
- Changing Glow's sound engine, terrain behaviour, saved state, or workshop
  menus.
- Inventing a new twelve-pad arrangement.
- Treating the VCV image as a fabrication source.
- Finalising printed control labels before the hardware mapping is frozen.

## 13. Acceptance criteria

- The VCV module unmistakably shows the real diagonal Touch 2 construction.
- Both upper-left jacks, all six knobs, and both faders sit on the swappable
  Glow faceplate.
- The exposed fixed region shows the Touch branding and Daisy Seed without
  being baked into the removable faceplate.
- The lower board contains the ten original P00-P09 gold electrodes, the
  original silver decorative regions, and two three-position metal switches.
- P10 and P11 occupy their original upper rear-PCB positions.
- All twelve pads use common geometry for appearance, state, and hit testing.
- The physical faceplate can be fabricated from the KiCad source after Synthux
  supplies its production profile.
- VCV graphics are derived from the physical master and remain legible across
  normal Rack zoom levels.
- Missing visual assets cannot break audio, patch loading, or control access.
