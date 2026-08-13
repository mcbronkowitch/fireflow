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
x_mm = (x_dxf - -0.0893630597716) * 25.4
y_mm = ( 0.438688194653 - y_dxf) * 25.4
```

That transform produces the DXF's analytical 80.899997 x 67.999993 mm
removable-plate extent (80.900 x 68.000 mm at drawing precision). The diagonal
opening is part of the one continuous outer contour, not a raster mask or an
independent screen crop. `Edge.Cuts` contains one 114-curve outer chain plus
15 distinct closed internal chains made from 967 source-derived cubic Bezier
segments. A separately locked `mechanical_reference` group on
`Dwgs.User` retains all 27 official DXF spline records: 1,673 native cubic
curves in total, including every zero-length source no-op and the three open
decorative/reference records that are not cuts.

### Explicit mechanical inventory

| Feature | DXF record union | Board-local centre, mm | Source envelope, mm |
| --- | --- | --- | --- |
| upper jack | `23` | `(5.066302968, 7.407949984)` | `8.010596778 x 8.010699775` |
| lower jack | `24` | `(5.066302968, 22.641449607)` | `8.010596778 x 8.010701682` |
| upper knobs 1-4 | `2+12`, `3+11`, `4+10`, `5+7+9` | `(17.163203227, 36.903498480)`, `(32.680002280, 36.903498480)`, `(48.196805148, 36.833496881)`, `(63.713604201, 36.903498480)` | respectively `9.010596722 x 9.010596722`, `9.010596722 x 9.010596722`, `9.010604351 x 9.010600536`, `9.010604351 x 9.010596722` |
| lower knobs 5-6 | `14+16`, `15` | `(17.163203227, 54.048496065)`, `(63.713604201, 54.048496065)` | `9.010596722 x 9.010600536`, `9.010604351 x 9.010600536` |
| left/right routes | `1+13+17`, `8` | `(5.079803181, 47.976949852)`, `(75.774203803, 47.976949852)` | each `4.300994631 x 26.040899718` |
| left top/bottom mounts | `0`, `18` | `(5.079803181, 32.485598643)`, `(5.079803181, 63.468293432)` | each `4.510604604 x 4.527999623` |
| right top/bottom mounts | `6`, `21` | `(75.774196174, 32.485598643)`, `(75.774196174, 63.468293432)` | each `4.510604604 x 4.527999623` |
| upper-right mount | `25` | `(75.661700758, 26.907098209)` | `4.510589346 x 4.510600790` |

There is no nominal 8/9/4.5 mm geometry in the generator or verifier. The
generator connects matching DXF spline endpoints, retains only closed unions
as mechanics, and calculates the table metadata from cubic extrema. Every
mechanic is its source-faithful, separately closed internal `Edge.Cuts` chain;
there are no NPTH pads and therefore no capsule approximation or doubled
physical cut.
The outer chain winds clockwise in board coordinates and all internal chains
wind counter-clockwise, matching the source; the verifier also rejects chain
intersection, duplication, omission, or opening.

Illustrator emitted 396 repeated zero-length cubic no-ops inside the 15 closed
unions. They remain in the locked reference but are deliberately omitted from
fabrication `Edge.Cuts` because they add no path geometry and would create
zero-length/duplicate board items. Of the 1,014 non-degenerate linear cubics,
KiCad 10 rejects 47 source segments measuring 92-141 nm as “very small”. Each
is collapsed by bridging its neighboring source vertices; independent analysis
measures the maximum resulting contour deviation as 0.000104757 mm
(0.104757 micrometres). The remaining 967 paths are reparameterized only along
their unchanged straight-line locus to avoid singular endpoint derivatives,
then serialized at KiCad's 1 nm coordinate resolution. This documented
sub-micrometre adaptation is KiCad's true minimum-segment limitation; there is
no capsule/drill approximation and the complete source stays locked beside it.

### Vendor-neutral pre-production rules

[glow-faceplate.kicad_dru](glow-faceplate.kicad_dru) is the auditable KiCad 10
custom-rule source. It sets 0.25 mm board-edge clearance, 0.25 mm drilled-hole
spacing, 0.20 mm minimum copper track/graphic width, 0.15 mm annular width,
0.15 mm silkscreen clearance, 0.80/0.10 mm minimum silkscreen text height/
stroke, and rejects bridged mask apertures. The board source separately sets a
0.20 mm minimum solder-mask web. These are deliberately conservative review
defaults, not an approved production profile; `SYNTHUX_PROFILE_APPROVED=no`.
Hole-spacing and annular-width rules are intentionally retained as conservative
project defaults but are vacuous for the current all-routed, pad-free master.

### Artwork ownership

The DXF-exact physical-size SVG and the KiCad board use the same named
ownership groups:

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
$env:KICAD_CLI = 'C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe'
python -B -m unittest hardware/glow-faceplate/scripts/test_mechanical_guard.py -v
& 'C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe' pcb drc --output .superpowers/sdd/2026-08-13-glow-hardware-faithful-hybrid-panel/task-8-drc-report.txt --format report --units mm --severity-all --exit-code-violations hardware/glow-faceplate/glow-faceplate.kicad_pcb
& 'C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe' pcb render --output .superpowers/sdd/2026-08-13-glow-hardware-faithful-hybrid-panel/task-8-kicad-preview.png --width 1600 --height 1400 --side top --background transparent --quality high --preset follow_plot_settings hardware/glow-faceplate/glow-faceplate.kicad_pcb
```

The final Task 8 DRC result is 0 violations, 0 unconnected pads, and 0
footprint errors; no warning was waived. The independent mechanical guard
re-parses the pinned DXF without importing generator constants. It checks the
exact outline, all 1,673 locked reference curves, exact group membership, all
15 internal `Edge.Cuts` chains and their closure/count/winding/nonintersection,
every canonical SVG reference path, conservative rule source, mask web, and
forbidden text on KiCad `F.Cu`, `F.Mask`, and `F.SilkS` as well as the SVG
fabrication groups. It parses `CONTROL_CENTRES_MM` directly from
`host/vcv/res/touch2_geometry.py` and recomputes the uniform fit on every run;
the current maximum residual is 0.218 mm. That comparison is evidence only and
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
composites, and afterwards in real Rack 2.6.6 renders at the same four zoom
levels. User visual approval of the material layers remains outstanding.

### KiCad-derived VCV faceplate

`host/vcv/res/GlowFaceplate.png` is now a compositing derivative of the
committed [proof/glow-faceplate-preview.png](proof/glow-faceplate-preview.png),
not independent Rack artwork. Generate the inspection render from the
repository root with the pinned KiCad 10.0.5 CLI:

```powershell
New-Item -ItemType Directory -Force hardware/glow-faceplate/proof | Out-Null
& 'C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe' pcb render --output hardware/glow-faceplate/proof/glow-faceplate-preview.png --width 1920 --height 1680 --side top --background transparent --quality high --preset follow_plot_settings hardware/glow-faceplate/glow-faceplate.kicad_pcb
python -B host/vcv/res/validate_glow_assets.py --derive-faceplate
```

KiCad rounds that request to a 1904 x 1656 RGBA image. In the committed render,
the >= 50% alpha board bounds are `(141,147)-(1761,1509)`, giving a measured
uniform source density of 20.040601636 px/mm (509.031 DPI). KiCad high-quality
post-processing adds only a low-alpha halo outside the solid board. The
deriver removes pixels below 50% alpha before resampling, then uses bilinear
sampling for a one-pixel antialiased edge; it never redraws, warps, or moves a
mechanical contour.

The one physical-to-Rack transform is, in millimetres:

```text
x_rack = 1.0 * x_board + 0.1900015
y_rack = 1.0 * y_board + 8.5
pixel   = rack_mm * (960 / 81.28) = rack_mm * 11.811023622
```

Thus the VCV derivative is an exact 300 DPI, uniform-scale placement in the
960 x 1520 canvas. The horizontal translation centres the official
80.899997 mm plate in 81.28 mm; the vertical translation leaves the physical
lower edge at 76.499993 mm, 0.600007 mm above the fixed/lower-board boundary.
Outside the board, the outer diagonal opening, and all 15 internal openings
remain transparent. PNG text records the board hash, preview hash, opaque
bounds, transform, source density, and unfiltered-pixel hash. During every
normal validation, the guard reruns the compositor from the pinned preview
entirely in memory and byte-compares the complete expected 960 x 1520 RGBA
pixel buffer with the committed image; validation writes no file. Consequently
a manual edit cannot authorize itself by updating PNG hashes or sidecar text.
Seven explicit alpha-transition scans cross the two mount holes, two knob
apertures, the diagonal edge, and the two fader slots. Each fader scan crosses
the 0.169 mm web between a mount hole and its adjacent slot, so it records two
transitions and the slot endpoint is the second of them. Their observed signed
edge offsets are compared with the regenerated, source-derived transitions and
may differ by at most one pixel, 0.084667 mm.

That one-pixel figure is the contour scans' own limit, not the tolerance the
asset as a whole enjoys. The full-buffer byte comparison runs on every
validation and admits no difference at all, so it rejects a one-pixel edge
shift that the contour scans would still accept. The scans exist to name
*which* mechanical feature moved; complete pixel equality is what decides pass
or fail.

The older `touch2_geometry.py` control centres remain rectified-photography
evidence. At the exact 1:1 mechanical placement, the comparable knob/fader
centres differ by 0.454-0.840 mm. Task 8's separate least-squares photographic
fit reports 0.218 mm only after scale `1.023423583`; that fit is not applied to
this render. The validator loads the generated control table and keeps this
comparison under a separate 1.00 mm source-quality guard, so it cannot be
mistaken for the 0.084667 mm mechanical-raster contract or used to fudge the
master.

Task 9 image-only before/after review composites exist at 75%, 100%, 125%, and
150% in the ignored SDD workspace. Real Rack screenshots were captured
afterwards from `C:\Program Files\VCV\Rack2Pro\Rack.exe` (Rack Pro 2.6.6) by
pointing Rack at a throwaway user directory holding only this plugin and
running its screenshot mode, which renders each module panel and exits:

```powershell
& "C:\Program Files\VCV\Rack2Pro\Rack.exe" -u <throwaway-user-dir> -t 1
```

Visual user approval of the result remains outstanding.

## Tooling and export policy

This project requires **KiCad 8 or later**. Task 8 located the existing stable
installation at
`C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe` and
captured `kicad-cli version` output `10.0.5`; therefore
`KICAD_AVAILABLE=yes`.

KiCad 10's `pcb import` command accepts PCB exchange formats but does not list
DXF. To keep the official Adobe-authored DXF exact and the build headless, the
committed generator analysis reads the cubic spline records directly, verifies
the provenance hash, derives connected aperture unions, converts inches to
millimetres once, and writes native KiCad objects. The verifier independently
repeats the DXF parsing/derivation. KiCad 10 then parses the `.kicad_dru`, runs
DRC, plots, and renders the board through the CLI. This headless native-object
conversion is the only CLI difference from the original GUI-import plan.

Gerbers, drill files, and production fabrication packages cannot be generated
while any production gate is `no`. Mechanical study files and clean-room
development outputs remain non-production until all four gates have evidence.
The current 1.6 mm KiCad thickness is a neutral preview default, not a Synthux
manufacturing-profile decision. `RIGHTS_APPROVED=no`, `LABELS_FROZEN=no`, and
`SYNTHUX_PROFILE_APPROVED=no` remain closed; Task 8 generated no Gerber,
Excellon, or final fabrication export.

## Verification status

Recorded 2026-08-13 at revision `2f077aa` on `codex/glow-hardware-panel-design`.

| Check | Result |
|---|---|
| `ctest --test-dir build` (Release, clang) | 9/9 targets, 1147 doctest cases, 0 failures |
| `python host/vcv/res/validate_glow_assets.py` | `assets OK` |
| `python host/vcv/res/test_glow_assets.py` | `assets tests OK` |
| `python host/vcv/res/test_flow_panel.py` | `panel OK` |
| `python host/vcv/res/test_touch2_geometry.py` | `geometry OK` |
| `host/vcv/build-local.sh dist` | exit 0; package carries the six PNGs and the `Glow.svg` fallback |
| Rack render, Rack Pro 2.6.6, zoom 0.75/1.0/1.25/1.5 | module renders at a fixed 16 HP |
| Missing-raster fallback, each of the six PNGs removed in turn | all six still render at 16 HP; a missing panel layer falls back to the vector plate, a missing switch frame degrades only the switches |

The Release configuration matters and is easy to get wrong in a worktree:
`env.sh` is gitignored, so a fresh worktree has no `CC`/`CXX` and CMake picks
up whatever compiler it finds. Copy `env.sh` in from the main checkout and
`source` it before configuring, or the tree builds with the wrong toolchain and
its failures are artefacts rather than defects.

Not verified here, and not claimed: interactive Rack behaviour — module
creation, existing-patch load, control manipulation, context menus,
live/excursion/refused states, save/reload persistence, and audio. Rack's
screenshot mode renders panels and exits; none of that is reachable from it.
Physical proof status is unchanged: no Gerber, drill, 1:1 PDF, or ordered
prototype exists, because three of the four production gates are still `no`.

## Restricted reference material

Official DXF, PDF, and frontal images are local-only under
`.reference/glow-touch2/`. They are intentionally ignored. Their byte hashes
and permitted-use record are committed in [sources/provenance.md](sources/provenance.md)
and [sources/checksums.sha256](sources/checksums.sha256).
