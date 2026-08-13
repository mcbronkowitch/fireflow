# Glow faceplate source provenance

The files named in this record are local-only references under
`.reference/glow-touch2/`. They are not committed to this repository. Public
source-code availability does not grant rights to Synthux trademarks, product
photography, or product artwork.

| Source ID | First-party URL | Retrieved | SHA-256 | Committed | Permitted use | Derived outputs |
| --- | --- | --- | --- | --- | --- | --- |
| `AUDREYTOUCH_FACEPLATE` | https://github.com/Synthux-Academy/AudreyTouch/tree/main/Faceplate | 2026-08-13 | `Simple Touch Faceplate template.dxf`: `55c84aa39e8d4e1484ae05a5145a545dd4749097129adb35a6fdb9da1ff606a0`; `Simple Touch Faceplate template.pdf`: `1201dab4d01081fb6679c360d209fcec96d447dcce2269211423ca2283fce1d1` | no | Official DXF/PDF: mechanical reference pending Synthux confirmation. Photographs and wordmarks: reference only pending written approval. | Later faceplate outline, diagonal opening, hole, and slot validation; no direct artwork distribution. |
| `SIMPLE_HARDWARE_PANEL` | https://github.com/Synthux-Academy/simple-hardware/tree/main/synth-interface-panel | 2026-08-13 | Not locally acquired | no | KiCad workflow and vector-template reference only. Any photographs and wordmarks are reference only pending written approval. | Vendor-neutral KiCad workflow notes; no source artwork, footprints, or branded assets copied into this project. |
| `OFFICIAL_KICAD_VIDEO` | https://www.youtube.com/watch?v=SU3fhliWxpM | 2026-08-13 | Not locally acquired | no | Production-process reference only. Any photographs and wordmarks are reference only pending written approval. | KiCad, Gerber, and independent-review process notes only. |
| `TOUCH2_PRODUCT_PAGE` | https://www.synthux.academy/store/simple-touch-2 | 2026-08-13 | `Audrey-Touch-Faceplates.jpg`: `0ec76540f644d242ace01508a0cd567d963ee797ce294c959384286d17093244`; `Simple-Touch-22-Synthux-Academy.jpg`: `cc8c93a79f9322983833622006edb3adc40937c7911eb6796aa22da7e4b35839` | no | Photographs and wordmarks: reference only pending written approval. | Local frontal comparison and clean-room material analysis only; no product photograph or wordmark is a distributable VCV asset. |

The machine-readable hashes are in [checksums.sha256](checksums.sha256). A
checksum verifies local file identity; it does not confer a licence or approval.

## Clean-room generated material input

`CLEANROOM_FR4_MATERIAL` is an original generated material input, not a
first-party Synthux source. It is intentionally listed separately from the
official references above.

| Source ID | Source category | Built-in source | Prompt category | Local filename | SHA-256 | Committed | Derivative role |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `CLEANROOM_FR4_MATERIAL` | Clean-room, material-only generated source | Codex built-in Imagegen, generated for Task 7 | Flat orthographic black FR4/fibreglass surface; unbranded material texture with no text, logo, circuit design, hardware, or directional lighting | `.reference/glow-touch2/cleanroom-fr4-material.png` | `72b42ed0102b37e801ac2a67079f7a59279fac50ca8fc860938ca5b13d3a1c1c` | no | Softened, clipped, and dark-tone blended into the fixed-rear and lower-board substrate masks only. It supplies no product identity, electrode contour, silver artwork, switch geometry, or controller layout. |

Task 7 reconstructs every non-material feature deterministically from local
geometry and original code-native drawing: named Catmull-Rom electrode masks,
a logo-free generic controller silhouette, restrained routing, clean-room
silver crosshatching, static switch bases, and moving-only toggle overlays.
No official photograph, Synthux/Touch wordmark, or copied decorative artwork
is present in the exported rasters. `RIGHTS_APPROVED` remains `no`.

| Exported Task 7 asset | SHA-256 | Derivation |
| --- | --- | --- |
| `host/vcv/res/GlowRear.png` | `2bb614248129986d76f8a47ad85810415913d132c812ee8ae588e77ecaccd70c` | Clean-room FR4 tone under the exact upper-rear mask; neutral P10/P11 masks and original logo-free fixed-hardware drawing. |
| `host/vcv/res/GlowTouch.png` | `05d1cc168c44ed109523120a31835fc4322a4ce6016bf47ea8745f49c6197582` | Clean-room FR4 tone under the exact lower-board mask; neutral P00-P09 masks, original silver treatment, and static switch bases. |
| `host/vcv/res/GlowSwitchDown.png` | `ec7d0fa6d5039cc51552a63be525900501652f1fb0ff50723b9c8f8921ae1c44` | Original moving lever, highlight, and shadow around common source pivot `(48, 96)`; transparent elsewhere. |
| `host/vcv/res/GlowSwitchCenter.png` | `ea19921d50773a468561f101230dcf9612a2549442dc68b786f5c4f1ee694c3c` | Original foreshortened moving lever, highlight, and shadow around common source pivot `(48, 96)`; transparent elsewhere. |
| `host/vcv/res/GlowSwitchUp.png` | `d28cede11b4cf4f809c441118de0c3497ecbe38f3a3ba136c4bbb6a76e1ae0b5` | Original moving lever, highlight, and shadow around common source pivot `(48, 96)`; transparent elsewhere. |

`GlowFaceplate.png` is not part of this transformation. Task 9 owns its
replacement from the physical KiCad preview.

## Task 8 physical-master derivation

Task 8 used only the two locally held `AUDREYTOUCH_FACEPLATE` mechanical files
for fabrication geometry. The PDF was rendered at 600 dpi and visually checked
against the DXF: both show the same outer silhouette/diagonal, two jack rings,
six knob rings, two long fader routes, and five small mounting rings. The PDF
page is 239.483 x 203.512 points; the drawn mechanical extent is 80.900 x
68.000 mm.

The DXF declares `$INSUNITS=1` (inches). Its final `ENTITIES` record is a
closed, planar degree-3 spline with 343 control points and clamped/tripled
knots, exactly 114 cubic Bezier segments. Its geometric bounds in DXF inches
are:

```text
min_x = -0.0893630597716
max_x =  3.0956761910848
min_y = -2.2384768963327
max_y =  0.4386881946530
```

The committed origin/axis transform is:

```text
x_mm = (x_dxf - min_x) * 25.4
y_mm = (max_y - y_dxf) * 25.4
```

This is a translation, Y-axis reflection, and inch-to-millimetre conversion;
it contains no design rescale. The last spline becomes the one closed
`Edge.Cuts` contour. Its analytical extent is 80.899996972 x 67.999993311 mm.
All 27 DXF spline records are also retained byte-derived on `Dwgs.User` as a
locked `mechanical_reference` group: 1,673 native cubic curves with complete
group membership.

The source analyzer joins matching endpoints among records 0-25. It finds 15
closed mechanical unions: 13 roundish apertures and two high-aspect routes.
Records 19, 20, and 22 form two open reference-only components and are retained
but not drilled. Every closed union is emitted as an explicit board-only oval
NPTH whose centre and X/Y envelope come from analytical cubic extrema. No
nominal 8.0, 9.0, or 4.5 mm drill authority exists, and the small ellipticities
are not silently rounded. KiCad serializes the derived numbers at its native
1 nm resolution, limiting coordinate rounding to 0.5 nm. The exact locked
Bézier path remains authoritative where a standard KiCad oval/capsule drill
cannot express an arbitrary source contour.

The editable artwork is clean-room FireFlow work. It contributes three
original flow curves plus unprotected FireFlow identification, revision, and
date metadata. It contributes no official photograph, Synthux/Touch identity,
copied decorative artwork, or provisional control assignment. The physical
SVG separates `copper_front`, `mask_front`, `silk_front`, and
`mechanical_reference`; matching named KiCad groups make the layer ownership
explicit.

| Task 8 master | SHA-256 | Derivation |
| --- | --- | --- |
| `hardware/glow-faceplate/glow-faceplate.kicad_pcb` | `70619cd3168309cc0957485e5bf59d57b514cf1b4a4c50e55dd1a3b03d6e0c22` | Deterministic native KiCad curves, source-envelope board-only NPTH mechanics, and clean-room front artwork from `scripts/generate_master.py`. |
| `hardware/glow-faceplate/artwork/glow-faceplate.svg` | `fec5228d972b1ab765f939f0737ad5f8619a97a020fa1d2a1bed4d504cc89aad` | DXF-exact physical-size editable artwork with the four required named groups and all 27 mechanical-reference paths. |

For comparison with the existing Rack geometry only, a uniform least-squares
map is applied after generation:

```text
x_vcv = 1.023423583 * x_local - 0.736154
y_vcv = 1.023423583 * y_local + 7.610624
```

Across the two jacks, six knobs, and two route centres, the maximum residual is
0.218 mm, inside the 0.25 mm Task 8 comparison limit. The verifier parses the
actual `CONTROL_CENTRES_MM` mapping in `host/vcv/res/touch2_geometry.py` and
recomputes this fit rather than embedding its coordinates or coefficients.
This comparison compensates the earlier photograph-rectified Rack coordinates
and is not used by KiCad or any future fabrication export. The physical master
always remains at the exact 1:1 DXF conversion above.

KiCad evidence was captured from the existing stable
`C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe`, version
`10.0.5`. KiCad 10 does not expose DXF in `pcb import`, so the deterministic
headless converter emits native `gr_curve` records and KiCad performs the
downstream parse, custom-rule DRC, SVG plot, and 3D render. The committed
`glow-faceplate.kicad_dru` and board setup are vendor-neutral pre-production
defaults, not a Synthux manufacturing profile. Final DRC reported 0 violations,
0 unconnected pads, and 0 footprint errors, with no exclusions added by this
task. No Gerber, drill file, or production package was generated because
`RIGHTS_APPROVED=no`, `LABELS_FROZEN=no`, and
`SYNTHUX_PROFILE_APPROVED=no` remain closed.

## Touch-electrode geometry rectification

Task 2 uses the official DXF only for removable-faceplate mechanics. It does
not derive electrode artwork from the DXF, and nothing in
`host/vcv/res/touch2_geometry.py` overrides the DXF outline, diagonal opening,
holes, or slots. The physical 16 HP panel coordinate system is 81.28 x 128.5
mm with its origin at the upper-left.

The frontal geometry source is `Simple-Touch-22-Synthux-Academy.jpg` from
`TOUCH2_PRODUCT_PAGE`. Six image-plane fiducials were fitted to panel
millimetres with a least-squares projective transform: the visible upper-left
and lower-right board corners plus all four mounting-hole centres. The
independent `Audrey-Touch-Faceplates.jpg` view was used as a perspective-angled
silhouette check, not as a second calibration plane.

| Fiducial | Source pixel `(x, y)` | Panel millimetres `(x, y)` | Fit residual |
| --- | ---: | ---: | ---: |
| upper-left board corner | `(493.50, 291.00)` | `(0.00, 0.00)` | `0.259 mm` |
| lower-right board corner | `(1113.50, 1289.00)` | `(81.28, 128.50)` | `0.062 mm` |
| upper-left mounting hole | `(549.25, 318.25)` | `(7.50, 3.00)` | `0.282 mm` |
| upper-right mounting hole | `(1057.00, 319.00)` | `(73.78, 3.00)` | `0.017 mm` |
| lower-left mounting hole | `(546.00, 1262.25)` | `(7.50, 125.50)` | `0.005 mm` |
| lower-right mounting hole | `(1055.00, 1265.00)` | `(73.78, 125.50)` | `0.063 mm` |

For a homogeneous source point `[x_px, y_px, 1]^T`, divide the first two
components of `H * point` by the third component. The pixel-to-millimetre
matrix is:

```text
H = [ 0.130959605   0.000589551  -64.7027732 ]
    [-0.000747855   0.130337729  -37.7993325 ]
    [ 0.000000148   0.000005032    1.0000000 ]
```

The six-point fit has `0.161 mm` RMS residual and `0.282 mm` maximum residual.
That error, JPEG compression, the shallow camera angle, and partly occluded
toggle regions make the result appropriate for Rack reference geometry only;
it is not a manufacturing measurement.

The frontal views resolve ten lower gold fields, two upper-rear gold fields,
two large silver hatched decoration fields, and two metal switches. P00-P09 are
therefore encoded in the lower-board zone, P10/P11 in the upper-rear zone, and
the silver and switch regions are exclusion geometry. Each electrode uses 16
reviewed anchors. P10/P11 specifically use the brief's permitted fallback: the
prior Glow SVG profiles were sampled at 16 anchors and transformed into the
observed upper-rear zone because neither frontal photograph isolates their
electrode contours faithfully. The public `AudreyTouch/Faceplate` directory currently
contains only the AI/DXF/PDF mechanical template, not the labelled P00-P11
artwork described in the approved design. Until Synthux supplies that labelled
drawing or production boards can be scanned, every path is emitted with
`verified=false` and a source note; the repository does not present the traced
P-number identity or occluded contour segments as original production
geometry.
