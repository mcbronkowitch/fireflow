# Glow Hardware-Faithful Hybrid Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Glow's invented vector-only 5-5-2 panel with a faithful Touch 2 10+2 hybrid panel, while establishing a physical KiCad faceplate as the source of truth for both fabrication and the VCV render.

**Architecture:** Keep mechanical geometry and pad identity in millimetres, generate the Rack-facing C++ header and validation SVG from that data, and render the module as three cached PNG hardware layers plus NanoVG interaction overlays and live widgets. A separate KiCad project owns the removable faceplate outline and fabrication layers; its reviewed render becomes `GlowFaceplate.png`. Rack interaction uses the same generated closed pad paths as drawing, with a bounds fast-path followed by exact curve hit testing. Missing raster assets fall back to a functional vector panel without affecting engine or parameter state.

**Tech Stack:** Python 3 panel generation and validation, C++17/Rack SDK 2.6.6, NanoVG, doctest/CMake, PNG with alpha, SVG/DXF interchange, KiCad 8 or later, Gerber X2 and Excellon drill output.

**Spec:** `docs/superpowers/specs/2026-08-13-glow-hardware-faithful-hybrid-panel-design.md`

## Global Constraints

- Work only in `C:\Users\bernd\Documents\AI\FireFlow\.worktrees\glow-hardware-panel-design` on branch `codex/glow-hardware-panel-design`.
- Do not copy or modify files in the primary worktree; another agent owns its in-progress changes.
- Keep all repository content, source comments, test names, commit messages, and production notes in English.
- Use the official Touch 2 DXF/PDF and first-party photographs only as documented references. Do not commit official photography or trademark-bearing derivative raster assets until the rights gate in Task 1 is satisfied.
- Treat the official DXF as authoritative for the removable faceplate outline, holes, slots, and diagonal opening. Preserve its millimetre scale.
- Preserve the true mapping: P00-P09 on the lower touch board; P10/P11 on the exposed upper rear PCB; the two silver fields are decorative; the two metal parts are centre-off switches.
- Do not change Glow's engine, musical state, parameter IDs, patch persistence, switch assignments, or context menus.
- Keep knobs, faders, jacks, screws, and moving toggle levers out of neutral raster backgrounds.
- Use `host/vcv/build-local.sh` for every full VCV build. Do not construct an alternative Rack build command.
- Demonstrate every new test failing before its implementation, then passing after the smallest corresponding change.
- End every task with `git diff --check`, its focused tests, and a dedicated commit with trailer `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Do not export production Gerbers until the label-freeze, rights, KiCad-tooling, and Synthux manufacturing-profile gates all pass.

## File Map

### New files

- `hardware/glow-faceplate/README.md` — physical-master workflow, tooling, review gates, and export commands.
- `hardware/glow-faceplate/sources/provenance.md` — URL, retrieval date, SHA-256, permitted use, and transformation record for every official source.
- `hardware/glow-faceplate/sources/checksums.sha256` — machine-checkable hashes for locally acquired reference files without committing restricted originals.
- `hardware/glow-faceplate/artwork/glow-faceplate.svg` — editable, physical-size artwork aligned to the official faceplate geometry.
- `hardware/glow-faceplate/glow-faceplate.kicad_pcb` — fabrication source of truth after KiCad and manufacturing-profile gates pass.
- `hardware/glow-faceplate/fab/README.md` — generated-output inventory and independent review checklist; generated Gerbers are added only after approval.
- `hardware/glow-faceplate/proof/glow-faceplate-1to1.pdf` — 1:1 mechanical proof generated from the approved KiCad board.
- `hardware/glow-faceplate/proof/glow-faceplate-preview.png` — inspection render used to derive the VCV faceplate layer.
- `host/vcv/res/touch2_geometry.py` — named physical geometry and fixed control centres for P00-P11, switches, and board zones.
- `host/vcv/res/validate_glow_assets.py` — deterministic checks for PNG names, dimensions, colour mode, alpha, and packaging.
- `host/vcv/res/test_glow_assets.py` — temporary-file unit tests for the raster validator and package manifest.
- `host/vcv/res/GlowRear.png` — 960 x 1520 fixed rear-PCB/Daisy visual layer.
- `host/vcv/res/GlowFaceplate.png` — 960 x 1520 render derived from the physical faceplate master.
- `host/vcv/res/GlowTouch.png` — 960 x 1520 neutral lower touch-board visual layer.
- `host/vcv/res/GlowSwitchDown.png` — transparent down-position toggle overlay.
- `host/vcv/res/GlowSwitchCenter.png` — transparent centre-position toggle overlay.
- `host/vcv/res/GlowSwitchUp.png` — transparent up-position toggle overlay.
- `host/vcv/src/pad_geometry.hpp` — Rack-independent closed Catmull-Rom flattening and exact point-in-path helper.
- `host/vcv/src/glow_panel.hpp` — layered image panel and three-frame toggle declarations.
- `host/vcv/src/glow_panel.cpp` — cached PNG rendering, fallback drawing, and toggle frame selection.
- `tests/test_pad_geometry.cpp` — pure curve and hit-testing tests.
- `tests/test_glow_panel_manifest.cpp` — pure layer order, frame selection, and fallback-policy tests.

### Modified files

- `host/vcv/res/gen_flow_panel.py` — consume authoritative named Touch 2 geometry; retain SVG only as validation/fallback output.
- `host/vcv/res/test_flow_panel.py` — replace invented-layout assertions with physical 10+2, zone, identity, clearance, and generated-header checks.
- `host/vcv/src/generated_flow_panel.hpp` — generated P00-P11 path data, bounds, centres, and accessibility metadata.
- `host/vcv/src/Glow.cpp` — install the layered panel, exact pad hit testing, real 10+2 placement, and custom toggle visuals.
- `host/vcv/Makefile` — package six explicit PNG assets and keep `Glow.svg` as the fallback resource.
- `CMakeLists.txt` — compile `glow_panel.cpp` and register the two new doctest sources plus asset validation.
- `docs/superpowers/specs/2026-08-13-glow-hardware-faithful-hybrid-panel-design.md` — change status to approved when implementation begins; no design content changes.

### Local-only reference workspace

- `.worktrees/glow-hardware-panel-design/.reference/glow-touch2/` — ignored working copies of official DXF/PDF/photos and rectified tracing images. The provenance and checksum files describe these inputs; restricted originals remain untracked.

---

## Task 1: Establish provenance, rights, tooling, and production gates

**Files:**

- Create: `hardware/glow-faceplate/README.md`
- Create: `hardware/glow-faceplate/sources/provenance.md`
- Create: `hardware/glow-faceplate/sources/checksums.sha256`
- Create: `hardware/glow-faceplate/fab/README.md`
- Modify: `.gitignore`
- Modify: `docs/superpowers/specs/2026-08-13-glow-hardware-faithful-hybrid-panel-design.md`

**Interfaces:**

- `provenance.md` records one row per source with columns `Source ID`, `First-party URL`, `Retrieved`, `SHA-256`, `Committed`, `Permitted use`, and `Derived outputs`.
- `README.md` exposes four explicit gates: `RIGHTS_APPROVED`, `LABELS_FROZEN`, `KICAD_AVAILABLE`, and `SYNTHUX_PROFILE_APPROVED`. Each remains `no` until evidence is linked.
- `.reference/glow-touch2/` is ignored without ignoring any committed file under `hardware/glow-faceplate/sources/`.

- [ ] Add `.reference/glow-touch2/` to `.gitignore` and create the local directory.

- [ ] Write `hardware/glow-faceplate/README.md` with the exact physical ownership model, the four gate table, the KiCad version requirement, and the rule that Gerbers cannot be generated while any gate is `no`.

- [ ] Record these first-party source entries in `provenance.md`: AudreyTouch Faceplate repository, simple-hardware panel repository, official KiCad video, and Touch 2 product page. Mark photographs and wordmarks `reference only pending written approval`; mark the official DXF/PDF `mechanical reference pending Synthux confirmation`.

- [ ] Acquire the official DXF, PDF, and the minimum frontal reference images into `.reference/glow-touch2/`, then generate `checksums.sha256` from their bytes. If network access is unavailable, use the already supplied local screenshots as temporary `user-provided reference` entries and leave the first-party files absent rather than inventing hashes.

- [ ] Verify each acquired hash locally:

```powershell
Get-Content hardware/glow-faceplate/sources/checksums.sha256 | ForEach-Object {
    $hash, $path = $_ -split '  ', 2
    if ((Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant() -ne $hash) {
        throw "Checksum mismatch: $path"
    }
}
```

Expected result: no output and exit code 0.

- [ ] Check for KiCad without installing or changing the machine:

```powershell
$command = Get-Command kicad-cli -ErrorAction SilentlyContinue
if ($command) { & $command.Source version } else { Write-Output 'KICAD_AVAILABLE=no' }
```

Expected result on the current machine: `KICAD_AVAILABLE=no`. Record this honestly; Tasks 2-7 remain executable, while Task 8 waits for user-approved installation or an absolute existing CLI path.

- [ ] Change the design spec status to `Approved for implementation on 13 August 2026`.

- [ ] Run the documentation guard:

```powershell
rg -n "T[B]D|T[O]DO|FIX[M]E|RIGHTS_APPROVED=yes|LABELS_FROZEN=yes|KICAD_AVAILABLE=yes|SYNTHUX_PROFILE_APPROVED=yes" hardware/glow-faceplate docs/superpowers/specs/2026-08-13-glow-hardware-faithful-hybrid-panel-design.md
git diff --check
```

Expected result: no placeholder markers and no gate incorrectly marked `yes`.

- [ ] Commit:

```powershell
git add .gitignore hardware/glow-faceplate docs/superpowers/specs/2026-08-13-glow-hardware-faithful-hybrid-panel-design.md
git commit -m "docs(glow): establish faceplate production gates" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 2: Replace invented pad data with named physical 10+2 geometry

**Files:**

- Create: `host/vcv/res/touch2_geometry.py`
- Modify: `host/vcv/res/gen_flow_panel.py`
- Modify: `host/vcv/res/test_flow_panel.py`
- Generate: `host/vcv/src/generated_flow_panel.hpp`
- Generate: `host/vcv/res/Glow.svg`

**Interfaces:**

```python
@dataclass(frozen=True)
class ClosedPath:
    pad_id: str
    zone: Literal["lower_touch", "upper_rear"]
    points_mm: tuple[tuple[float, float], ...]
    label_anchor_mm: tuple[float, float]

PADS: tuple[ClosedPath, ...]  # ordered P00 through P11
SWITCH_CENTRES_MM: tuple[tuple[float, float], tuple[float, float]]
CONTROL_CENTRES_MM: Mapping[str, tuple[float, float]]
```

The module validates unique IDs, clockwise closed shapes, at least eight anchors per pad, in-panel bounds, and zone membership at import time.

- [ ] Add failing Python tests proving that the current generator is wrong:

```python
def test_touch2_uses_true_ten_plus_two_mapping():
    assert [p.pad_id for p in PAD_SHAPES] == [f"P{i:02d}" for i in range(12)]
    assert [p.zone for p in PAD_SHAPES[:10]] == ["lower_touch"] * 10
    assert [p.zone for p in PAD_SHAPES[10:]] == ["upper_rear"] * 2

def test_upper_pads_are_above_lower_touch_board():
    lower_top = min(p.bounds.min_y for p in PAD_SHAPES[:10])
    assert all(p.bounds.max_y < lower_top for p in PAD_SHAPES[10:])
```

- [ ] Run RED:

```powershell
python host/vcv/res/test_flow_panel.py
```

Expected result: failures because the generator still emits the 5-5-2 field and has no zone metadata.

- [ ] Rectify the labelled first-party layout to physical millimetres using at least four mechanical fiducials: upper-left and lower-right board corners plus two mounting-hole centres. Document the transform matrix and residual error in `provenance.md`.

- [ ] Trace twelve individual paths in the local reference workspace. Preserve P00-P11 identity; use 12-32 anchors per path and do not smooth away characteristic corners. Verify each path against two frontal views where available.

- [ ] Encode only the reviewed millimetre paths and control centres in `touch2_geometry.py`. If a path remains ambiguous, encode the current SVG path with `verified=False` and its source note; do not label it original in docs or UI.

- [ ] Refactor `gen_flow_panel.py` to import `PADS`, remove `PAD_BOXES`, `PAD_PROFILES`, `PAD_UNIT_RING`, `PAD_POINT_OVERRIDES`, and `_make_pad_shape`, and emit these generated fields:

```cpp
enum class PadZone : std::uint8_t { LowerTouch, UpperRear };

struct PadShape {
    const XY* points;
    std::size_t pointCount;
    XY label;
    XY centre;
    XY min;
    XY max;
    const char* id;
    PadZone zone;
    bool verified;
};
```

- [ ] Update all obsolete visual-contract tests to assert physical facts: exactly ten lower pads, exactly two upper pads, two switch clearances, no electrode in either silver decoration zone, official panel size, stable pad IDs, and no printed P-number labels in the neutral SVG.

- [ ] Regenerate committed outputs:

```powershell
python host/vcv/res/gen_flow_panel.py
python host/vcv/res/test_flow_panel.py
```

Expected result: `panel OK` and byte-for-byte agreement between generator output and committed `Glow.svg`/`generated_flow_panel.hpp`.

- [ ] Inspect a 4x render of `Glow.svg` and confirm the ten lower shapes and two upper shapes occupy their intended physical zones without overlapping switch keep-outs.

- [ ] Run final checks and commit:

```powershell
git diff --check
git add host/vcv/res/touch2_geometry.py host/vcv/res/gen_flow_panel.py host/vcv/res/test_flow_panel.py host/vcv/res/Glow.svg host/vcv/src/generated_flow_panel.hpp hardware/glow-faceplate/sources/provenance.md
git commit -m "feat(glow): generate the physical Touch 2 pad layout" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 3: Add exact closed-curve pad hit testing

**Files:**

- Create: `host/vcv/src/pad_geometry.hpp`
- Create: `tests/test_pad_geometry.cpp`
- Modify: `host/vcv/src/generated_flow_panel.hpp` through the generator
- Modify: `host/vcv/src/Glow.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
namespace spkyvcv::pad_geometry {
struct Point { float x; float y; };

Point catmullRom(Point p0, Point p1, Point p2, Point p3, float t);
bool pointInClosedCatmullRom(
    const Point* points,
    std::size_t pointCount,
    Point query,
    unsigned subdivisionsPerSegment = 12);
}
```

`TouchPlate::containsLocalPoint(Vec local)` first rejects outside `shape.min/shape.max`, converts the module-millimetre point to `Point`, and calls `pointInClosedCatmullRom()` using the generated anchors. `TouchPlate::onButton()` and `TouchPlate::onHover()` delegate to Rack only when this helper accepts the event position; returning without consuming the event lets black and silver gaps remain available to widgets underneath.

- [ ] Register `tests/test_pad_geometry.cpp` in the existing doctest executable before creating the helper.

- [ ] Write failing tests for a square-like closed spline: centre inside; far point outside; concave-gap point outside; point immediately inside a curved edge accepted; point immediately outside rejected; fewer than three points rejected safely.

- [ ] Add a regression using one generated lower pad and two negative points from documented black/silver gaps.

- [ ] Run RED in the required Release configuration. The compile must fail because `pad_geometry.hpp` does not exist yet:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

- [ ] Implement `catmullRom()` and flatten each closed segment into twelve line segments, then apply an even-odd ray crossing test. Treat a point within `1e-4f` of a flattened edge as inside so the visible boundary is clickable.

- [ ] Make the generated header use `spkyvcv::pad_geometry::Point` as `XY`, preserving all existing drawing call sites.

- [ ] Gate Rack button and hover handling in `TouchPlate` using the installed Rack 2.6.6 event API:

```cpp
bool TouchPlate::containsLocalPoint(const math::Vec& local) const {
    const XY panelPoint = localPoint(local);
    if (panelPoint.x < shape->min.x || panelPoint.x > shape->max.x ||
        panelPoint.y < shape->min.y || panelPoint.y > shape->max.y) {
        return false;
    }
    return pad_geometry::pointInClosedCatmullRom(
        shape->points, shape->pointCount, panelPoint);
}

void TouchPlate::onButton(const ButtonEvent& event) {
    if (!containsLocalPoint(event.pos))
        return;
    app::Switch::onButton(event);
}

void TouchPlate::onHover(const HoverEvent& event) {
    if (!containsLocalPoint(event.pos))
        return;
    app::Switch::onHover(event);
}
```

Rack dispatches position events through rectangular widget boxes, so the widget must reject non-path positions before `OpaqueWidget` consumes them. Keep the pure helper signature unchanged.

- [ ] Run GREEN: build and execute the focused doctest cases, then the existing touch-pad tests and Python panel guard:

```powershell
cmake --build build
build\spky_tests.exe --test-case="*pad geometry*,*touch pad*"
python host/vcv/res/test_flow_panel.py
```

- [ ] Build the plugin with `host/vcv/build-local.sh` and manually verify that clicking a rectangular corner outside a pad does not change its parameter, while clicking the gold interior does.

- [ ] Commit:

```powershell
git diff --check
git add host/vcv/src/pad_geometry.hpp tests/test_pad_geometry.cpp host/vcv/src/Glow.cpp host/vcv/src/generated_flow_panel.hpp host/vcv/res/gen_flow_panel.py CMakeLists.txt
git commit -m "feat(glow): use exact touch electrode hit testing" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 4: Define and test the layered-panel runtime contract

**Files:**

- Create: `host/vcv/src/glow_panel.hpp`
- Create: `host/vcv/src/glow_panel.cpp`
- Create: `tests/test_glow_panel_manifest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
namespace spkyvcv::glow_panel {
enum class Layer : std::uint8_t { Rear, Faceplate, Touch };

struct LayerSpec {
    Layer layer;
    const char* resourcePath;
    bool required;
};

constexpr std::array<LayerSpec, 3> kLayers = {{
    {Layer::Rear, "res/GlowRear.png", true},
    {Layer::Faceplate, "res/GlowFaceplate.png", true},
    {Layer::Touch, "res/GlowTouch.png", true},
}};

int switchFrameIndex(float value);
bool allRequiredLayersAvailable(const bool* availability, std::size_t count);

struct GlowHardwarePanel : widget::Widget {
    void draw(const DrawArgs& args) override;
};

struct GlowToggle : app::Switch {
    void draw(const DrawArgs& args) override;
};
}
```

- [ ] Add failing pure tests for stable layer order, exact resource names, all layers required, and switch mapping `-1 -> down`, `0 -> centre`, `+1 -> up` with clamping around parameter extremes.

- [ ] Add a testable `allRequiredLayersAvailable(const bool*, std::size_t)` C++17 policy and verify one missing layer selects fallback without changing panel dimensions.

- [ ] Run RED: compile fails because `glow_panel.hpp` is absent.

- [ ] Implement the manifest and pure selection helpers first; run the focused doctest until GREEN.

- [ ] Implement `GlowHardwarePanel::draw()` with a fixed 16 HP box. For each layer, call Rack's cached `APP->window->loadImage(asset::plugin(pluginInstance, path))` in the draw path, immediately use the returned handle in `nvgImagePattern`, and do not retain the image pointer across frames.

- [ ] If any required image cannot load, draw a neutral near-black panel, the generated faceplate boundary, all twelve pad outlines, and fixed zone separators. Log each missing path once per widget instance; keep drawing children and controls.

- [ ] Implement `GlowToggle` directly on `app::Switch`, with `momentary = false`, a fixed box matching the physical lever canvas, and custom PNG drawing. Rack's integer switch behaviour still cycles the configured `-1, 0, +1` parameter; avoiding `CKSSThree` also prevents hidden stock SVG children from being drawn. Keep the existing parameter quantity and snap behaviour.

- [ ] Add `glow_panel.cpp` to the Rack plugin target and the manifest test to the doctest target.

- [ ] Run GREEN, then temporarily rename one local raster placeholder during a Rack smoke test and confirm the module loads at 16 HP with controls intact. Restore the file before staging.

- [ ] Commit:

```powershell
git diff --check
git add host/vcv/src/glow_panel.hpp host/vcv/src/glow_panel.cpp tests/test_glow_panel_manifest.cpp CMakeLists.txt
git commit -m "feat(glow): add resilient layered panel rendering" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 5: Build deterministic raster-asset validation and clean-room placeholders

**Files:**

- Create: `host/vcv/res/validate_glow_assets.py`
- Create: `host/vcv/res/test_glow_assets.py`
- Create: six PNG assets listed in the File Map
- Modify: `host/vcv/res/test_flow_panel.py`
- Modify: `host/vcv/Makefile`
- Modify: `CMakeLists.txt`

**Interfaces:**

```python
PANEL_ASSETS = {
    "GlowRear.png": (960, 1520, "RGBA"),
    "GlowFaceplate.png": (960, 1520, "RGBA"),
    "GlowTouch.png": (960, 1520, "RGBA"),
}

SWITCH_ASSETS = {
    "GlowSwitchDown.png": (96, 192, "RGBA"),
    "GlowSwitchCenter.png": (96, 192, "RGBA"),
    "GlowSwitchUp.png": (96, 192, "RGBA"),
}
```

The three switch images must have identical dimensions and non-empty alpha. The panel images must have exact 4x dimensions. All six names must appear individually in `DISTRIBUTABLES`.

- [ ] Write `test_glow_assets.py` with failing tests for a missing asset, a 959-pixel-wide panel, an RGB image without alpha, mismatched toggle sizes, fully transparent toggle content, and an asset absent from the Makefile package list.

- [ ] Run RED:

```powershell
python host/vcv/res/test_glow_assets.py
```

Expected result: import failure because `validate_glow_assets.py` does not exist yet.

- [ ] Create rights-safe placeholder assets from repository-owned geometry: near-black rear region, diagonal faceplate silhouette, neutral lower board with generated pad outlines, and three simple metal-lever frames. Do not include Synthux photography, logos, or copied decorative artwork at this stage.

- [ ] Implement validation with Pillow or the repository's available PNG reader. If neither is present, use the bundled workspace Python dependencies; do not add a runtime dependency to the plugin.

- [ ] Replace the single `Glow.svg` distribution entry with the fallback SVG plus the six explicit PNG paths in `host/vcv/Makefile`.

- [ ] Add the validator to `flow_panel_guard` in CMake so missing or malformed assets fail the build before packaging.

- [ ] Run GREEN:

```powershell
python host/vcv/res/test_glow_assets.py
python host/vcv/res/validate_glow_assets.py
python host/vcv/res/test_flow_panel.py
```

Expected result: `assets OK` followed by `panel OK`.

- [ ] Build and inspect the plugin package contents; verify exactly the six PNGs plus `Glow.svg` are present and the local `.reference` tree is absent.

- [ ] Commit:

```powershell
git diff --check
git add host/vcv/res/validate_glow_assets.py host/vcv/res/test_glow_assets.py host/vcv/res/GlowRear.png host/vcv/res/GlowFaceplate.png host/vcv/res/GlowTouch.png host/vcv/res/GlowSwitchDown.png host/vcv/res/GlowSwitchCenter.png host/vcv/res/GlowSwitchUp.png host/vcv/res/test_flow_panel.py host/vcv/Makefile CMakeLists.txt
git commit -m "feat(glow): validate and package hybrid panel assets" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 6: Wire the physical 10+2 layout and custom switches into Glow

**Files:**

- Modify: `host/vcv/src/Glow.cpp`
- Modify: `host/vcv/src/glow_panel.cpp`
- Modify: `tests/test_glow_ui.cpp`
- Modify: `tests/test_touch_pads.cpp`
- Modify: `host/vcv/res/test_flow_panel.py`

**Interfaces:**

- `GlowWidget` sets a 16 HP module box and installs `GlowHardwarePanel` instead of `createPanel(...Glow.svg)`.
- `TouchPlate::configure(const PadShape&, int paramId, std::string accessibleName)` works for both lower and upper zones.
- `GlowToggle` replaces both `CKSSThree` instances without changing their parameter IDs, ranges, snap, or persistence.
- Widget centres come from `CONTROL_CENTRES_MM`; no copied pixel constants are permitted for the two jacks, six knobs, two faders, two switches, or twelve pads.

- [ ] Add failing tests that enumerate pad-to-param mapping and require P00-P09 to map to `PAD_1` through `PAD_10`, P10/P11 to `PAD_11`/`PAD_12`, with accessible names `Touch electrode P00` through `Touch electrode P11`.

- [ ] Add failing geometry tests that require both jacks, every knob, and both faders to lie inside the removable-faceplate polygon; P10/P11 must lie inside the upper-rear zone; all lower pads and switches must lie inside the lower-board zone.

- [ ] Add failing tests proving both toggles still expose three positions and the original parameter ranges.

- [ ] Run RED with the existing Glow UI/touch tests and panel Python test.

- [ ] Replace `createPanel()` with `GlowHardwarePanel`, set `box.size = Vec(16 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT)`, and preserve child z-order: panel, pad feedback, static controls, moving switch overlays, screws/tooltips.

- [ ] Instantiate all twelve `TouchPlate` widgets from generated bounds and centres. Ensure the two upper pads remain behind neither the Daisy image nor another widget.

- [ ] Replace both stock toggle widgets with `GlowToggle` at their generated physical centres. Use the existing parameter configuration unchanged.

- [ ] Remove printed pad numbers from the panel draw; expose P00-P11 through `ParamQuantity` names/tooltips and screen-reader labels.

- [ ] Confirm runtime treatments remain: neutral gold idle, warm-gold live collar, green inner excursion contour, muted brick-red refusal flash. Reuse the path helper for every outline.

- [ ] Run GREEN: focused C++ tests, Python panel/asset guards, and full local VCV build.

- [ ] Manual interaction pass in Rack: click each pad centre, each nearby black gap, both silver fields, both switch surrounds, and P10/P11. Only gold interiors may trigger pad parameters.

- [ ] Commit:

```powershell
git diff --check
git add host/vcv/src/Glow.cpp host/vcv/src/glow_panel.cpp tests/test_glow_ui.cpp tests/test_touch_pads.cpp host/vcv/res/test_flow_panel.py
git commit -m "feat(glow): wire the hardware-faithful panel layout" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 7: Produce the reviewed fixed-hardware and lower-board raster layers

**Files:**

- Modify: `host/vcv/res/GlowRear.png`
- Modify: `host/vcv/res/GlowTouch.png`
- Modify: `host/vcv/res/GlowSwitchDown.png`
- Modify: `host/vcv/res/GlowSwitchCenter.png`
- Modify: `host/vcv/res/GlowSwitchUp.png`
- Modify: `hardware/glow-faceplate/sources/provenance.md`
- Modify: `hardware/glow-faceplate/README.md`

**Interfaces:**

- `GlowRear.png` contains only the fixed rear PCB, exposed Daisy Seed region, and neutral P10/P11 surfaces.
- `GlowTouch.png` contains only the lower board, neutral P00-P09 surfaces, silver decorative regions, and static switch bases.
- Toggle frames contain only moving lever/highlight/shadow pixels and share canvas size and pivot.
- All material layers are 4x panel scale and align to physical millimetre geometry within 0.25 mm at mechanical fiducials.

- [ ] Obtain written Synthux decision for photography, wordmark, electrode-artwork, and attribution use. Record the evidence link and set `RIGHTS_APPROVED=yes` only for explicitly approved categories.

- [ ] If photographic derivative use is approved, rectify the selected first-party frontal source against board edges and mounting holes, remove directional light/control shadows, and reconstruct damaged regions. If it is not approved, create a clean-room PCB material treatment from measured colours and traced geometry; omit protected wordmarks or use an approved supplied logo asset.

- [ ] Export the rear and touch layers at exactly 960 x 1520 RGBA. Use hard physical masks at board edges and pad silhouettes; retain neutral state only.

- [ ] Render the toggle lever at down, centre, and up angles around a common pivot. Keep the base hardware in `GlowTouch.png` so frame transitions do not move the mounting point.

- [ ] Run asset validation and generate 1x/2x review reductions without committing the reductions:

```powershell
python host/vcv/res/validate_glow_assets.py
```

- [ ] Capture Rack screenshots at 75%, 100%, 125%, and 150% zoom. Review for shimmer, doubled controls, baked lever positions, P10/P11 discoverability, alpha seams, and state visibility.

- [ ] Present the four screenshot crops to the user for visual approval. Apply requested material/contrast corrections without moving physical geometry.

- [ ] Run the full VCV build and panel guards again after the approved asset revisions.

- [ ] Commit only rights-cleared raster outputs and updated provenance:

```powershell
git diff --check
git add host/vcv/res/GlowRear.png host/vcv/res/GlowTouch.png host/vcv/res/GlowSwitchDown.png host/vcv/res/GlowSwitchCenter.png host/vcv/res/GlowSwitchUp.png hardware/glow-faceplate/sources/provenance.md hardware/glow-faceplate/README.md
git commit -m "art(glow): add reviewed Touch 2 hardware layers" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 8: Create the physical KiCad faceplate master

**Prerequisite gate:** Stop this task before changing system software unless the user approves installing KiCad. Continue only when `KICAD_AVAILABLE=yes`. Mechanical construction may proceed with vendor-neutral rules, but fabrication export remains blocked until `SYNTHUX_PROFILE_APPROVED=yes` and `LABELS_FROZEN=yes`.

**Files:**

- Create: `hardware/glow-faceplate/artwork/glow-faceplate.svg`
- Create: `hardware/glow-faceplate/glow-faceplate.kicad_pcb`
- Modify: `hardware/glow-faceplate/README.md`
- Modify: `hardware/glow-faceplate/sources/provenance.md`

**Interfaces:**

- Board coordinates remain in millimetres with a documented origin at the official DXF's upper-left datum.
- `Edge.Cuts` is a single closed outer contour plus the diagonal opening/approved internal routing contours.
- Mechanical holes and fader slots use official coordinates and explicit NPTH or routed-slot definitions.
- Artwork SVG has named groups `copper_front`, `mask_front`, `silk_front`, and `mechanical_reference`.

- [ ] With user approval, install or locate KiCad 8+ and record the absolute `kicad-cli` path/version in `README.md`; set `KICAD_AVAILABLE=yes` with the captured output.

- [ ] Import the official DXF at 1:1 onto a locked reference layer and verify the overall width, height, two jack holes, six knob holes, both fader slots, mounting holes, and diagonal opening against the official PDF.

- [ ] Add a failing mechanical verification script or KiCad CLI check that rejects an open `Edge.Cuts`, duplicate segments, wrong overall dimensions, missing slots, or missing NPTHs. Run it against the initial empty board to demonstrate RED.

- [ ] Build the board outline and mechanical features from the locked reference, then run the check to GREEN. Do not approximate the diagonal with a raster mask.

- [ ] Create `glow-faceplate.svg` at physical size with copper/mask/silkscreen groups. Keep provisional macro/fader/switch text off fabrication groups until the user freezes final names and `LABELS_FROZEN=yes` is recorded.

- [ ] Import the approved artwork layers into KiCad and assign ownership explicitly: exposed copper/gold, solder-mask opening/coverage, and front silkscreen. Do not rely on rendered colour as manufacturing intent.

- [ ] Add board title, revision, fabrication date field, and non-trademark FireFlow identification in a mechanically safe area. Include approved Synthux attribution only if the rights evidence requires or permits it.

- [ ] Run KiCad DRC with vendor-neutral conservative rules and capture the report. Resolve every edge-clearance, open-outline, mask-sliver, and minimum-feature error; document intentionally waived warnings individually.

- [ ] Render a front preview and compare mechanical fiducials to the VCV control centres. Maximum alignment error is 0.25 mm.

- [ ] Commit the editable physical master, not production Gerbers:

```powershell
git diff --check
git add hardware/glow-faceplate/artwork/glow-faceplate.svg hardware/glow-faceplate/glow-faceplate.kicad_pcb hardware/glow-faceplate/README.md hardware/glow-faceplate/sources/provenance.md
git commit -m "feat(glow): create the physical faceplate master" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 9: Derive the VCV faceplate layer from the physical master

**Files:**

- Create: `hardware/glow-faceplate/proof/glow-faceplate-preview.png`
- Modify: `host/vcv/res/GlowFaceplate.png`
- Modify: `host/vcv/res/validate_glow_assets.py`
- Modify: `hardware/glow-faceplate/README.md`

**Interfaces:**

- A documented command renders the KiCad front at a known DPI and crops/scales it to the 960 x 1520 Rack canvas without non-uniform scaling.
- `GlowFaceplate.png` is a compositing derivative of the KiCad preview, not an independently edited faceplate.
- A validation fiducial set checks at least two mounting holes, two fader-slot endpoints, two knob centres, and the diagonal opening within one 4x pixel (81.28 mm / 960 = 0.0847 mm after the documented transform).

- [ ] Add a failing validator assertion that the current placeholder `GlowFaceplate.png` does not match the physical preview's fiducial manifest/hash.

- [ ] Run RED with `validate_glow_assets.py`.

- [ ] Render `glow-faceplate-preview.png` from KiCad with transparent background outside the board, retaining copper/mask/silkscreen appearance and no controls.

- [ ] Composite the preview into the full Rack canvas using the documented physical-to-Rack transform. Do not manually warp or repaint mechanical edges after export.

- [ ] Add a sidecar fiducial manifest inside `validate_glow_assets.py` or `touch2_geometry.py`, then verify every faceplate fiducial against the generated control centres.

- [ ] Run GREEN: asset validator, panel generator tests, C++ UI tests, and full VCV build.

- [ ] Capture before/after Rack screenshots and obtain user approval that the removable faceplate reads as the intended Glow design while the fixed controller region remains visually separate.

- [ ] Commit:

```powershell
git diff --check
git add hardware/glow-faceplate/proof/glow-faceplate-preview.png host/vcv/res/GlowFaceplate.png host/vcv/res/validate_glow_assets.py hardware/glow-faceplate/README.md
git commit -m "art(glow): derive the Rack faceplate from KiCad" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 10: Generate and independently review fabrication outputs

**Prerequisite gate:** Begin only after the user freezes final macro/fader/switch labels, Synthux confirms thickness, solder-mask colour, surface finish, copper finish, minimum features, drill/slot handling, and panelisation, and all four gates in `hardware/glow-faceplate/README.md` are `yes`.

**Files:**

- Create: `hardware/glow-faceplate/proof/glow-faceplate-1to1.pdf`
- Create: `hardware/glow-faceplate/fab/gerbers/*`
- Create: `hardware/glow-faceplate/fab/drill/*`
- Create: `hardware/glow-faceplate/fab/drc-report.txt`
- Create: `hardware/glow-faceplate/fab/gerber-review.md`
- Modify: `hardware/glow-faceplate/fab/README.md`
- Modify: `hardware/glow-faceplate/README.md`

**Interfaces:**

- `fab/README.md` names the confirmed Synthux/JLCPCB profile and exact KiCad export command.
- `gerber-review.md` records reviewer, viewer/tool version, layer polarity, outline, drill/slot, mask, copper, silkscreen, dimensions, and approval result.
- The 1:1 PDF includes a 100 mm calibration bar and explicit `print at 100%, no scaling` text outside the board outline.

- [ ] Update the KiCad constraints to the written Synthux profile and run DRC. Store a zero-error report or document every approved exception with Synthux evidence.

- [ ] Add final frozen control labels to the correct copper/silkscreen layers and repeat DRC plus preview alignment validation.

- [ ] Export Gerber X2 and Excellon drill/routing files to a newly empty, explicitly verified `fab` output directory. Never mix outputs from different revisions.

- [ ] Export the 1:1 PDF with calibration bar and print it without scaling. Physically compare all holes, slots, edges, and the diagonal opening to a Touch 2 chassis or an official Synthux mechanical proof.

- [ ] Open the Gerbers in an independent viewer, not KiCad's board editor. Complete every row in `gerber-review.md`, including copper/mask polarity, exposed gold, text direction, edge clearance, slot presence, and overall size.

- [ ] Send the proof package and completed review to Synthux; record their approval/revision response. Regenerate all outputs after any board-source change and repeat the independent review.

- [ ] Order only a small prototype batch. Mount and photograph one faceplate, then record fit, finish, legibility, and any revision required before a larger run.

- [ ] Run checksum and repository verification:

```powershell
git diff --check
python host/vcv/res/test_flow_panel.py
python host/vcv/res/test_glow_assets.py
python host/vcv/res/validate_glow_assets.py
```

- [ ] Commit the reviewed proof package only after Synthux approval:

```powershell
git add hardware/glow-faceplate
git commit -m "fab(glow): add approved faceplate production package" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 11: Run final regression and visual acceptance

**Files:**

- Modify only if verification finds a defect in an earlier task.
- Update: `hardware/glow-faceplate/README.md` with final verified revision and evidence links.

**Interfaces:**

- Final acceptance covers source generation, asset validation, C++ unit tests, Rack build/package, multi-zoom screenshots, missing-resource fallback, patch reload, and physical proof status.

- [ ] Run generator and asset guards from a clean worktree:

```powershell
python host/vcv/res/test_flow_panel.py
python host/vcv/res/validate_glow_assets.py
```

Expected result: `panel OK` and `assets OK`.

- [ ] Run the complete configured C++ test suite in the project's required Release configuration and confirm no existing engine, state, or UI regression.

- [ ] Run `host/vcv/build-local.sh`, inspect the built package, and confirm the six PNGs and fallback SVG are present.

- [ ] In Rack, verify: module creation; existing patch load; all six knobs; both faders; both jacks; both three-position switches; P00-P11; context menus; live/excursion/refused states; save/reload persistence.

- [ ] Deliberately remove each required PNG one at a time from a disposable package copy. For each case, confirm fixed 16 HP size, vector fallback, interactive controls, patch loading, and audio remain functional. Restore the package from the build afterward.

- [ ] Capture final 75%, 100%, 125%, and 150% screenshots and compare them to the approved reference: diagonal construction, faceplate ownership, exposed Daisy region, 10+2 electrodes, silver decoration, and two switches.

- [ ] Run final hygiene checks:

```powershell
git diff --check
rg -n "T[B]D|T[O]DO|FIX[M]E" hardware/glow-faceplate host/vcv/res/touch2_geometry.py host/vcv/res/validate_glow_assets.py host/vcv/res/test_glow_assets.py host/vcv/src/glow_panel.* host/vcv/src/pad_geometry.hpp tests/test_pad_geometry.cpp tests/test_glow_panel_manifest.cpp
git status --short
```

Expected result: no placeholder markers and only the intended final documentation change before commit.

- [ ] Update `hardware/glow-faceplate/README.md` with the verified Git revision, build result, Rack version, screenshot locations, physical proof status, and any remaining external approval without claiming it complete.

- [ ] Commit final verification evidence:

```powershell
git add hardware/glow-faceplate/README.md
git commit -m "docs(glow): record hybrid panel verification" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

## Completion Criteria

- The generated geometry and UI expose exactly P00-P09 below and P10/P11 above, with exact-path interaction and no black/silver gap activation.
- The VCV module uses three neutral hardware PNG layers, three custom toggle frames, live NanoVG pad feedback, and a safe vector fallback.
- All controls remain at official physical centres and retain existing parameter/state behaviour.
- The committed raster assets pass provenance and rights review; otherwise the clean-room variants remain in use.
- The KiCad board, VCV faceplate render, and mechanical fiducials share one millimetre coordinate system.
- Production outputs are generated only from the approved KiCad revision after all four gates pass and are reviewed independently.
- Automated tests, Release tests, local Rack build, package inspection, fallback smoke tests, and multi-zoom visual review all pass with recorded evidence.
