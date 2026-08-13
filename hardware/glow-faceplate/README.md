# Glow faceplate physical master

`hardware/glow-faceplate/` is the physical design master for the removable
Glow faceplate. A reviewed render of that physical design may later be used for
the VCV faceplate layer; the VCV image is never a fabrication source.

## Physical ownership model

- **Fixed Touch 2 rear PCB:** Synthux hardware. It includes the exposed Daisy
  Seed, Touch identity, and P10/P11. FireFlow may use verified geometry for
  clean-room work, but does not claim ownership of this hardware, its wordmark,
  or its artwork.
- **Removable Glow faceplate:** FireFlow owns the new faceplate artwork and its
  KiCad fabrication source. The outline, diagonal opening, holes, and slots
  remain constrained by the official Synthux mechanical reference.
- **Lower touch PCB:** Synthux hardware. Its P00-P09 electrode silhouettes and
  decorative artwork are reference material; any public reproduction requires
  written Synthux approval or clean-room replacement.
- **Runtime state and switch layer:** FireFlow owns the VCV-only state feedback
  and transparent switch-frame treatment. It changes no fixed Touch 2 hardware
  and does not grant rights to Synthux identity or artwork.

## Production gates

Every gate stays `no` until the linked evidence exists in this repository or in
an approved external record. A gate may not be inferred from public source-code
availability.

| Gate | Status | Required evidence before opening |
| --- | --- | --- |
| `RIGHTS_APPROVED` | `no` | Written Synthux approval for the specific photography, wordmark, electrode-artwork, and attribution use proposed for public distribution. |
| `LABELS_FROZEN` | `no` | Approved, final macro, fader, and switch labels linked to the hardware mapping. |
| `KICAD_AVAILABLE` | `yes` | Existing installation captured below: `C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe`, version `10.0.5`. |
| `SYNTHUX_PROFILE_APPROVED` | `no` | Written Synthux manufacturing profile covering thickness, mask, finish, copper treatment, minimum features, and panelisation. |

## Editable physical master

The Task 8 master is [glow-faceplate.kicad_pcb](glow-faceplate.kicad_pcb),
generated from the locally held official DXF without rescaling. The matching
editable layer artwork is [artwork/glow-faceplate.svg](artwork/glow-faceplate.svg).
Both are development masters; neither is a fabrication release.

Board coordinates are faceplate-local millimetres. `(0, 0)` is the geometric
upper-left of the official DXF outer spline. The exact conversion from the DXF
inch coordinates is:

```text
x_mm = (x_dxf - -0.0893630708668) * 25.4
y_mm = ( 0.438688194653 - y_dxf) * 25.4
```

That transform produces an 80.900 x 68.000 mm removable plate. The diagonal
opening is part of the one continuous outer contour, not a raster mask or an
independent screen crop. `Edge.Cuts` contains 114 connected cubic Bezier
segments derived directly from the official closed spline. A separately
locked `mechanical_reference` group on `Dwgs.User` retains all 27 official DXF
spline records, including the aperture strokes used for the coordinate review.

### Explicit mechanical inventory

| Feature | Board-local centre(s), mm | Definition |
| --- | --- | --- |
| two audio jacks | `(5.066304, 7.407949)`, `(5.066304, 22.641449)` | circular NPTH, nominal 8.0 mm |
| four upper knobs | `(17.163205, 36.903497)`, `(32.678405, 36.903497)`, `(48.196807, 36.903497)`, `(63.711706, 36.903497)` | circular NPTH, nominal 9.0 mm |
| two lower knobs | `(17.163205, 54.048495)`, `(63.713606, 54.048495)` | circular NPTH, nominal 9.0 mm |
| two fader routes | `(5.079804, 47.976946)`, `(75.774196, 47.976946)` | oval NPTH/routed slot, 4.3010 x 26.0409 mm |
| four fader mounts | `(5.079804, 32.485598)`, `(75.774196, 32.485598)`, `(5.079804, 63.468294)`, `(75.774196, 63.468294)` | circular NPTH, nominal 4.5 mm |
| upper-right mount | `(75.661701, 26.907097)` | circular NPTH, nominal 4.5 mm |

The DXF spline extrema are 8.0106, 9.0106, and approximately 4.5106 mm for
the three circular classes. The pads use the intended nominal 8.0, 9.0, and
4.5 mm drills at the unchanged official centres; the complete source splines
remain locked beside them for review.

### Artwork ownership

The 80.900 x 68.000 mm SVG and the KiCad board use the same named ownership
groups:

- `copper_front`: three original FireFlow flow lines on `F.Cu`;
- `mask_front`: explicit, wider `F.Mask` openings over those copper lines;
- `silk_front`: `FIREFLOW / GLOW`, board title, revision A, and the
  `2026-08-13` fabrication-date field on `F.SilkS`;
- `mechanical_reference`: locked official mechanical geometry only.

No Synthux/Touch wordmark, copied product artwork, provisional macro name,
fader assignment, or switch assignment appears on a fabrication layer.

### Deterministic checks and preview

Run from the repository root:

```powershell
python -B hardware/glow-faceplate/scripts/generate_master.py
python -B hardware/glow-faceplate/scripts/verify_mechanics.py
& 'C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe' pcb drc --output .superpowers/sdd/2026-08-13-glow-hardware-faithful-hybrid-panel/task-8-drc-report.txt --format report --units mm --severity-all --exit-code-violations hardware/glow-faceplate/glow-faceplate.kicad_pcb
& 'C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe' pcb render --output .superpowers/sdd/2026-08-13-glow-hardware-faithful-hybrid-panel/task-8-kicad-preview.png --width 1600 --height 1400 --side top --background transparent --quality high --preset follow_plot_settings hardware/glow-faceplate/glow-faceplate.kicad_pcb
```

The final Task 8 DRC result is 0 violations, 0 unconnected pads, and 0
footprint errors; no warning was waived. The mechanical guard checks exact DXF
curves, contour closure, duplicate segments, dimensions, the locked reference,
all 13 circular NPTHs, both routed slots, SVG groups, forbidden text, and VCV
fiducials. It also records a 0.211 mm maximum error after one uniform
least-squares comparison from the official local datum to the existing
photograph-rectified Rack centres. That comparison scale is evidence only and
is never applied to the 1:1 KiCad geometry.

## VCV clean-room hardware layers

While `RIGHTS_APPROVED` is `no`, the shipped fixed-hardware images are
clean-room reconstructions only. They contain no official photography,
Synthux/Touch wordmark, copied decorative artwork, or protected product
identity. The generated FR4 input is a material-only Codex Imagegen source and
is recorded separately from the first-party references in
[sources/provenance.md](sources/provenance.md).

- `GlowRear.png` owns the fixed upper-rear substrate, a logo-free generic
  controller region, and neutral P10/P11 gold surfaces. It is transparent
  below the exact 77.1 mm board boundary.
- `GlowTouch.png` owns the lower substrate, neutral P00-P09 gold surfaces,
  original clean-room silver crosshatching, and both static switch bases. It is
  transparent above the same boundary.
- The three switch PNGs own only moving lever, highlight, and shadow pixels.
  Their 96 x 192 canvases share pivot `(48, 96)`; no mounting plate or washer
  changes between frames.
- Runtime NanoVG remains the sole owner of live, excursion, and refusal state.
  Neutral raster layers contain no selected-state glow or feedback colour.

All panel layers are 960 x 1520 RGBA at four times Rack panel resolution. The
board split lands exactly on source row 912; one-half source pixel is about
0.042 mm, inside the 0.25 mm mechanical-fiducial tolerance. The material
layers were inspected in generated 75%, 100%, 125%, and 150% image-only review
composites. They have not received user visual approval and were not captured
inside Rack because no `Rack.exe` is available. Task 9 still owns
`GlowFaceplate.png`; its current placeholder is not a physical-preview claim.

## Tooling and export policy

This project requires **KiCad 8 or later**. Task 8 located the existing stable
installation at
`C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe` and
captured `kicad-cli version` output `10.0.5`; therefore
`KICAD_AVAILABLE=yes`.

KiCad 10's `pcb import` command accepts PCB exchange formats but does not list
DXF. To keep the official Adobe-authored DXF exact and the build headless, the
committed generator reads its cubic spline records directly, verifies the
provenance hash, converts inches to millimetres once, and writes native KiCad
`gr_curve` objects. KiCad 10 then parses, checks, plots, and renders that board
through the CLI. This is the only CLI difference from the original GUI-import
plan.

Gerbers, drill files, and production fabrication packages cannot be generated
while any production gate is `no`. Mechanical study files and clean-room
development outputs remain non-production until all four gates have evidence.
The current 1.6 mm KiCad thickness is a neutral preview default, not a Synthux
manufacturing-profile decision. `RIGHTS_APPROVED=no`, `LABELS_FROZEN=no`, and
`SYNTHUX_PROFILE_APPROVED=no` remain closed; Task 8 generated no Gerber,
Excellon, or final fabrication export.

## Restricted reference material

Official DXF, PDF, and frontal images are local-only under
`.reference/glow-touch2/`. They are intentionally ignored. Their byte hashes
and permitted-use record are committed in [sources/provenance.md](sources/provenance.md)
and [sources/checksums.sha256](sources/checksums.sha256).
