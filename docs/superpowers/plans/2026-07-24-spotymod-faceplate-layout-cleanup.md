# Spotymod Faceplate Layout Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize the existing 42 HP Spotymod VCV faceplate into the approved Twin Islands + Connected Fields + Quiet Technical layout without changing controls, behavior, or parameter IDs.

**Architecture:** Keep `host/vcv/res/gen_panel.py` as the single source of truth for geometry, labels, static SVG fields, and generated C++ tables. Extend its existing declarative geometry with mirrored FX-field and PLAY-field records, then regenerate `Spotymod.svg` and `generated_panel.hpp`. Lock every approved measurement and mirror relationship in the plain-assert test suite before changing generator code.

**Tech Stack:** Python 3 panel generator and guard tests, SVG, generated C++ header, VCV Rack 2 plugin build.

## Global Constraints

- Panel size remains exactly **42 HP**, `213.36 × 128.5 mm`.
- Both LED rings remain at the existing rendered size with centers A `(39.50, 34.50 mm)` and B `(173.86, 34.50 mm)`.
- Every current knob, button, switch, light, jack, label, and function remains.
- `PARAMS` order in `host/vcv/res/gen_panel.py` remains byte-for-byte unchanged; existing `.vcv` parameter IDs must not move.
- Deck B is a full geometric mirror of deck A for controls, labels, secondary labels, sector graphics, FX fields, and PLAY mode fields.
- VOICE is `(4.00, 72.40, 31.50, 24.50 mm)` and FX is `(38.00, 72.40, 44.00, 24.50 mm)` on deck A; deck B mirrors both.
- Small-knob horizontal pitch is `10.50 mm`; VOICE centers are `9.25, 19.75, 30.25 mm`; FX centers are `44.25, 54.75, 65.25, 75.75 mm`.
- Deck-A VOICE order is `ATK/FILT/SUB` above `DEC/RES/DTUN`; deck B mirrors it.
- Deck-A FX order is `RATE/MIX/FB/ROOM` above `DUST/ROT/GRIT/COMP`; deck B mirrors it.
- Center DUO and ROOM columns are `CX - 10.50`, `CX`, and `CX + 10.50 mm`.
- Group stroke is `0.30 mm`; group fill is PAPER_DEEP with SVG `fill-opacity="0.45"`.
- Sector annulus is `20.50..31.00 mm` with SVG `fill-opacity="0.045"`.
- Deck-A PLAY mode field is `(5.00, 99.60, 29.00, 10.60 mm)`, PAPER_DEEP at `fill-opacity="0.25"`; deck B mirrors it.
- `SCAN`, `LEN`, and `ORG` use MUTED `#656056`, not deck accents.
- The one-line `host/vcv/src/Spotymod.cpp` `c.tip` compatibility wiring preserves
  runtime parameter names/tooltips while generator data controls faceplate
  captions; DSP, defaults, and ranges remain unchanged.

---

### Task 1: Twin Islands geometry and functional control order

**Files:**
- Modify: `host/vcv/res/test_panel.py:185-350`
- Modify: `host/vcv/res/gen_panel.py:101-225`
- Modify: `host/vcv/res/gen_panel.py:291-388`
- Regenerate: `host/vcv/res/Spotymod.svg`
- Regenerate: `host/vcv/src/generated_panel.hpp`

**Interfaces:**
- Consumes: Existing `Ctl`, `orbit()`, `orbit_label()`, `part_controls()`, `SHARED`, and append-only `PARAMS` assembly.
- Produces: Exact constants `RING_CX_A=39.5`, `RING_CY=34.5`, `KNOB_R=25.5`; new VOICE/FX center arrays; the approved control-to-coordinate mapping; center columns at `CX ± 10.5`.

- [ ] **Step 1: Replace geometry expectations and add explicit compatibility assertions**

Update `ORBIT_A`, `LOWER_A`, center expectations, and group-box expectations in `test_panel.py` with these exact records:

```python
ORBIT_A = {
    'RATE_A':    (39.500,  9.000, 39.500,  3.800, 'middle'),
    'DENSITY_A': (55.891, 14.966, 59.876, 10.216, 'start'),
    'SMOOTH_A':  (64.613, 30.072, 70.718, 29.695, 'start'),
    'SHAPE_A':   (61.584, 47.250, 66.607, 52.350, 'start'),
    'MOD_A':     (48.222, 58.462, 50.205, 66.112, 'middle'),
    'RANGE_A':   (30.778, 58.462, 28.795, 66.112, 'middle'),
    'MELODY_A':  (17.416, 47.250, 12.393, 52.350, 'end'),
    'TUNE_A':    (14.387, 30.072,  8.282, 29.695, 'end'),
    'COLOR_A':   (23.109, 14.966, 19.124, 10.216, 'end'),
}

LOWER_A = {
    'ATTACK_A': (9.25, 77.30), 'FILT_A': (19.75, 77.30), 'SUB_A': (30.25, 77.30),
    'DECAY_A': (9.25, 89.40), 'RES_A': (19.75, 89.40), 'DETUNE_A': (30.25, 89.40),
    'FLUXRATE_A': (44.25, 77.30), 'FLUX_A': (54.75, 77.30),
    'FLUXFB_A': (65.25, 77.30), 'REV_MIX_A': (75.75, 77.30),
    'DUST_A': (44.25, 89.40), 'ROT_A': (54.75, 89.40),
    'GRIT_A': (65.25, 89.40), 'COMP_A': (75.75, 89.40),
    'ENGINE_A': (10.00, 103.60), 'GRITMODE_A': (17.50, 103.60),
    'STEPS_A': (37.00, 103.60), 'STEP_A': (46.00, 103.60),
    'PRINCIPLE_A': (56.50, 103.60), 'NEWPHRASE_A': (67.00, 103.60),
    'TRIGGER_A': (77.50, 103.60),
}
```

Change the part-box expectation to:

```python
want = [
    (4.0, 72.4, 31.5, 24.5, 'VOICE'),
    (38.0, 72.4, 44.0, 24.5, 'FX'),
    (4.0, 98.6, 78.0, 12.6, 'PLAY'),
]
```

Change DUO/ROOM entries in `CENTER` from `±11.5` and `±12.0` to `±10.5`. Keep TIME, BLEND, all Y coordinates, and `PARAM_ORDER` unchanged.

Add:

```python
def test_layout_constants():
    check(approx(g.RING_CX_A, 39.5), f"RING_CX_A {g.RING_CX_A}, want 39.5")
    check(approx(g.RING_CY, 34.5), f"RING_CY {g.RING_CY}, want 34.5")
    check(approx(g.KNOB_R, 25.5), f"KNOB_R {g.KNOB_R}, want 25.5")
    check(g.VOICE_X == [9.25, 19.75, 30.25], f"VOICE_X {g.VOICE_X}")
    check(g.FX_TOP == [44.25, 54.75, 65.25, 75.75], f"FX_TOP {g.FX_TOP}")
    check(g.FX_BOT == g.FX_TOP, f"FX rows disagree: {g.FX_TOP} / {g.FX_BOT}")
```

Update `test_sector_captions()` to expect deck A `(70.00, 8.20, MOTION)`, `(70.00, 67.00, TIMBRE)`, `(9.00, 8.20, PITCH)` and exact mirrored X coordinates for deck B.

- [ ] **Step 2: Run the guard suite and verify the new tests fail**

Run:

```powershell
cd host/vcv
python res/test_panel.py
```

Expected: `FAIL` with old ring constants, old part-box widths, old VOICE/FX coordinates, old center offsets, and old sector-caption positions. `PARAMS order changed` must not appear.

- [ ] **Step 3: Implement the approved geometry and mappings**

In `gen_panel.py`, set:

```python
RING_CY   = 34.5
RING_R    = 16.0
KNOB_R    = 25.5
RING_CX_A = 39.5

SECTORS = [
    ("MOTION", -16.0,  96.0, (70.0,  8.2)),
    ("TIMBRE", 112.0, 176.0, (70.0, 67.0)),
    ("PITCH",  192.0, 336.0, ( 9.0,  8.2)),
]
```

Make `orbit_label()` use `31.3` for lower, `30.7` for upper, and `31.7` for side labels while keeping the existing anchor and `dy` rules:

```python
r = 31.3 if c < -0.38 else (30.7 if (abs(s) < 0.38 and c > 0.38) else 31.7)
```

Change `part_groups()` and lower-grid constants:

```python
return [(fx(4.0, 31.5), 72.4, 31.5, 24.5, "VOICE", MUTED),
        (fx(38.0, 44.0), 72.4, 44.0, 24.5, "FX", MUTED),
        (fx(4.0, 78.0), 98.6, 78.0, 12.6, "PLAY", MUTED)]

VOICE_X = [9.25, 19.75, 30.25]
FX_TOP  = [44.25, 54.75, 65.25, 75.75]
FX_BOT  = [44.25, 54.75, 65.25, 75.75]
```

Keep enum construction order intact, but map controls to the approved positions:

```python
for (enum, lbl, x, y) in [
    ("ATTACK", "ATK",  VOICE_X[0], ROW_V1),
    ("DECAY",  "DEC",  VOICE_X[0], ROW_V2),
    ("RES",    "RES",  VOICE_X[1], ROW_V2),
    ("SUB",    "SUB",  VOICE_X[2], ROW_V1),
    ("DETUNE", "DTUN", VOICE_X[2], ROW_V2),
]:
    out.append(Ctl(enum, SMKNOB, fx(x), y, lbl))

out.append(Ctl("FLUX", SMKNOB, fx(FX_TOP[1]), ROW_V1, "MIX"))
for enum, lbl, i in (("GRIT", "GRIT", 2), ("COMP", "COMP", 3)):
    out.append(Ctl(enum, SMKNOB, fx(FX_BOT[i]), ROW_V2, lbl))
```

Move appended controls without moving their list positions:

```python
Ctl("FILT_A", SMKNOB, VOICE_X[1], ROW_V1, "FILT")
Ctl("FILT_B", SMKNOB, W - VOICE_X[1], ROW_V1, "FILT")
Ctl("FLUXRATE_A", SMKNOB, FX_TOP[0], ROW_V1, "RATE")
Ctl("FLUXRATE_B", SMKNOB, W - FX_TOP[0], ROW_V1, "RATE")
Ctl("FLUXFB_A", SMKNOB, FX_TOP[2], ROW_V1, "FB")
Ctl("FLUXFB_B", SMKNOB, W - FX_TOP[2], ROW_V1, "FB")
Ctl("DUST_A", SMKNOB, FX_BOT[0], ROW_V2, "DUST")
Ctl("DUST_B", SMKNOB, W - FX_BOT[0], ROW_V2, "DUST")
Ctl("ROT_A", SMKNOB, FX_BOT[1], ROW_V2, "ROT")
Ctl("ROT_B", SMKNOB, W - FX_BOT[1], ROW_V2, "ROT")
```

Set `L, R = CX - 10.5, CX + 10.5` and use those positions for every DUO and ROOM outer column. Do not alter `PARAMS` list order.

- [ ] **Step 4: Regenerate outputs and run the guard suite**

Run:

```powershell
cd host/vcv
python res/gen_panel.py
python res/test_panel.py
```

Expected generator summary: unchanged param/input/output/light counts. Expected tests: `PASS -- panel guards ok`.

- [ ] **Step 5: Commit Task 1**

```powershell
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/res/Spotymod.svg host/vcv/src/generated_panel.hpp
git commit -m "feat(vcv): reorganize faceplate geometry"
```

---

### Task 2: Connected FX fields with exact deck mirroring

**Files:**
- Modify: `host/vcv/res/test_panel.py`
- Modify: `host/vcv/res/gen_panel.py`
- Regenerate: `host/vcv/res/Spotymod.svg`

**Interfaces:**
- Consumes: Task 1 FX box `(38.0, 72.4, 44.0, 24.5)` and `FX_TOP`/`FX_BOT` centers.
- Produces: `FX_FIELDS`, a declarative list of `(mirror, name, x, y, w, h, fill)` records drawn after group boxes and before controls.

- [ ] **Step 1: Add failing field-geometry, color, layering, and mirror tests**

Add to `test_panel.py`:

```python
FX_FIELDS_A = {
    "FLUX_TOP":    (39.0, 73.6, 31.0, 10.0, "#dfe5dc"),
    "ROOM":        (70.5, 73.6, 10.5, 10.0, "#e8e0d4"),
    "FLUX_BOTTOM": (39.0, 84.4, 21.0, 11.3, "#dfe5dc"),
    "GRIT":        (60.5, 84.4, 20.5, 11.3, "#e6ddd1"),
}

def test_fx_fields_are_exact_mirrors():
    check(len(g.FX_FIELDS) == 8, f"{len(g.FX_FIELDS)} FX fields, want 8")
    for name, (x, y, w, h, fill) in FX_FIELDS_A.items():
        a = next((f for f in g.FX_FIELDS if not f[0] and f[1] == name), None)
        b = next((f for f in g.FX_FIELDS if f[0] and f[1] == name), None)
        check(a is not None and b is not None, f"{name}: missing A or B field")
        if a is None or b is None:
            continue
        _, _, ax, ay, aw, ah, af = a
        _, _, bx, by, bw, bh, bf = b
        check(all(approx(v, want) for v, want in
                  zip((ax, ay, aw, ah), (x, y, w, h))),
              f"{name} A geometry {a[2:6]}")
        check(approx(bx, g.W - x - w) and approx(by, y)
              and approx(bw, w) and approx(bh, h),
              f"{name} B is not mirrored: {b[2:6]}")
        check(af == fill and bf == fill, f"{name} fill {af}/{bf}, want {fill}")

def test_fx_fields_render_below_controls():
    s = g.svg()
    field = g.fx_field_svg(next(f for f in g.FX_FIELDS
                                if not f[0] and f[1] == "FLUX_TOP"))
    knob = g.knob_svg(ctl("FLUX_A"))
    check(field in s, "FLUX_TOP field missing from SVG")
    check(s.index(field) < s.index(knob), "FX field must render below controls")
```

- [ ] **Step 2: Run tests and verify the new contract fails**

Run:

```powershell
cd host/vcv
python res/test_panel.py
```

Expected: `FAIL` because `FX_FIELDS` and `fx_field_svg` do not exist.

- [ ] **Step 3: Implement declarative Connected Fields**

Add exact palette tokens:

```python
FX_FLUX = "#dfe5dc"
FX_GRIT = "#e6ddd1"
FX_ROOM = "#e8e0d4"
```

Add:

```python
def part_fx_fields(mir):
    def mx(x, w):
        return W - x - w if mir else x
    return [
        (mir, "FLUX_TOP",    mx(39.0, 31.0), 73.6, 31.0, 10.0, FX_FLUX),
        (mir, "ROOM",        mx(70.5, 10.5), 73.6, 10.5, 10.0, FX_ROOM),
        (mir, "FLUX_BOTTOM", mx(39.0, 21.0), 84.4, 21.0, 11.3, FX_FLUX),
        (mir, "GRIT",        mx(60.5, 20.5), 84.4, 20.5, 11.3, FX_GRIT),
    ]

FX_FIELDS = part_fx_fields(False) + part_fx_fields(True)

def fx_field_svg(field):
    _mir, _name, x, y, w, h, fill = field
    return (f'<rect x="{mm(x)}" y="{mm(y)}" width="{mm(w)}" '
            f'height="{mm(h)}" rx="1.0" fill="{fill}"/>')
```

In `svg()`, draw `FX_FIELDS` after group boxes and before wells/glyphs:

```python
for field in FX_FIELDS:
    P.append(fx_field_svg(field))
```

- [ ] **Step 4: Regenerate and run tests**

Run:

```powershell
cd host/vcv
python res/gen_panel.py
python res/test_panel.py
```

Expected: unchanged generated counts and `PASS -- panel guards ok`.

- [ ] **Step 5: Commit Task 2**

```powershell
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/res/Spotymod.svg
git commit -m "feat(vcv): group mirrored fx fields"
```

---

### Task 3: Quiet Technical surface treatment and final compatibility verification

**Files:**
- Modify: `host/vcv/res/test_panel.py`
- Modify: `host/vcv/res/gen_panel.py`
- Regenerate: `host/vcv/res/Spotymod.svg`
- Regenerate: `host/vcv/src/generated_panel.hpp`

**Interfaces:**
- Consumes: Task 1 geometry and Task 2 `FX_FIELDS`.
- Produces: Fixed group/sector opacity constants, mirrored `PLAY_FIELDS`, neutral sampler aliases, and final generated assets.

- [ ] **Step 1: Add failing Quiet Technical and PLAY-field tests**

Add:

```python
def test_quiet_technical_tokens():
    check(approx(g.GROUP_STROKE, 0.30), f"GROUP_STROKE {g.GROUP_STROKE}")
    check(approx(g.GROUP_FILL_OPACITY, 0.45),
          f"GROUP_FILL_OPACITY {g.GROUP_FILL_OPACITY}")
    check(approx(g.SECTOR_R_IN, 20.50), f"SECTOR_R_IN {g.SECTOR_R_IN}")
    check(approx(g.SECTOR_R_OUT, 31.00), f"SECTOR_R_OUT {g.SECTOR_R_OUT}")
    check(approx(g.SECTOR_OPACITY, 0.045), f"SECTOR_OPACITY {g.SECTOR_OPACITY}")

def test_play_mode_fields_are_mirrored():
    check(len(g.PLAY_FIELDS) == 2, f"{len(g.PLAY_FIELDS)} PLAY fields, want 2")
    a = next(f for f in g.PLAY_FIELDS if not f[0])
    b = next(f for f in g.PLAY_FIELDS if f[0])
    _, ax, ay, aw, ah = a
    _, bx, by, bw, bh = b
    check(all(approx(v, want) for v, want in
              zip((ax, ay, aw, ah), (5.0, 99.6, 29.0, 10.6))),
          f"PLAY A field {a[1:]}")
    check(approx(bx, g.W - ax - aw) and approx(by, ay)
          and approx(bw, aw) and approx(bh, ah),
          f"PLAY B is not mirrored: {b[1:]}")
    s = g.svg()
    for field in g.PLAY_FIELDS:
        check(g.play_field_svg(field) in s, f"PLAY field missing: {field}")
```

Change the sampler alias color assertion in
`test_sampler_words_sit_inline_behind_their_caption()` to:

```python
check(t[4] == g.MUTED,
      f"{c.enum}: {word} colour {t[4]}, want {g.MUTED}")
```

Add an SVG assertion that `group_box()` contains
`fill-opacity="0.45"` and `stroke-width="0.30"`, and that a wedge contains
`opacity="0.045"`.

- [ ] **Step 2: Run tests and verify the styling contract fails**

Run:

```powershell
cd host/vcv
python res/test_panel.py
```

Expected: `FAIL` for missing style constants, missing PLAY fields, old group/wedge weights, and deck-colored sampler aliases.

- [ ] **Step 3: Implement fixed surface constants and PLAY fields**

Add:

```python
GROUP_STROKE = 0.30
GROUP_FILL_OPACITY = 0.45
SECTOR_R_IN = 20.50
SECTOR_R_OUT = 31.00
SECTOR_OPACITY = 0.045
PLAY_FIELD_OPACITY = 0.25

def part_play_field(mir):
    x, y, w, h = 5.0, 99.6, 29.0, 10.6
    return (mir, W - x - w if mir else x, y, w, h)

PLAY_FIELDS = [part_play_field(False), part_play_field(True)]

def play_field_svg(field):
    _mir, x, y, w, h = field
    return (f'<rect x="{mm(x)}" y="{mm(y)}" width="{mm(w)}" '
            f'height="{mm(h)}" rx="1.0" fill="{PAPER_DEEP}" '
            f'fill-opacity="{PLAY_FIELD_OPACITY:.2f}"/>')
```

Use the constants in `group_box()`:

```python
f'fill="{PAPER_DEEP}" fill-opacity="{GROUP_FILL_OPACITY:.2f}" '
f'stroke="{LINE}" stroke-width="{GROUP_STROKE:.2f}"/>'
```

Use the sector constants in `wedge_svg()`:

```python
R_OUT, R_IN = SECTOR_R_OUT, SECTOR_R_IN
...
f'... fill="{colour}" opacity="{SECTOR_OPACITY:.3f}"/>'
```

Draw `PLAY_FIELDS` after `FX_FIELDS` and before controls:

```python
for field in PLAY_FIELDS:
    P.append(play_field_svg(field))
```

In `sampler_texts()`, emit every `SCAN`, `LEN`, and `ORG` text record with
`MUTED` instead of the per-deck accent. Do not change positions, anchors, or
sizes.

- [ ] **Step 4: Regenerate, run all guards, and verify generated assets are stable**

Run:

```powershell
cd host/vcv
python res/gen_panel.py
python res/test_panel.py
$svgBefore = (Get-FileHash res/Spotymod.svg -Algorithm SHA256).Hash
$hppBefore = (Get-FileHash src/generated_panel.hpp -Algorithm SHA256).Hash
python res/gen_panel.py
$svgAfter = (Get-FileHash res/Spotymod.svg -Algorithm SHA256).Hash
$hppAfter = (Get-FileHash src/generated_panel.hpp -Algorithm SHA256).Hash
if ($svgBefore -ne $svgAfter -or $hppBefore -ne $hppAfter) {
    throw "panel generation is not deterministic"
}
```

Expected: both test runs print `PASS -- panel guards ok`; the second generator
run leaves both generated-file hashes unchanged. After staging, use
`git diff --cached --check` to verify whitespace and inspect the generated
changes.

- [ ] **Step 5: Verify patch compatibility and scope**

Run:

```powershell
python -c "import sys; sys.path.insert(0, 'res'); import gen_panel as g; import test_panel as t; assert [c.enum for c in g.PARAMS] == t.PARAM_ORDER; print('PARAM_ORDER unchanged:', len(t.PARAM_ORDER))"
git diff --check
git status --short
```

Expected: `PARAM_ORDER unchanged: 80`, no diff-check errors, and
only the generator, its tests, and generated SVG/header are modified.

- [ ] **Step 6: Attempt the VCV plugin build when an x86_64 MinGW compiler is available**

Check:

```powershell
Get-Command x86_64-w64-mingw32-g++ -ErrorAction SilentlyContinue
```

If found, run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc 'cd host/vcv && make RACK_DIR=/c/Users/bernd/Documents/AI/Rack-SDK CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ TMP=/tmp TEMP=/tmp -j4'
```

Expected: exit `0` and a built Rack plugin. On the current host only the ARM
Daisy `g++` is installed; if the x86_64 compiler check returns nothing, record
the build as unavailable rather than invoking the ARM compiler.

- [ ] **Step 7: Commit Task 3**

```powershell
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/res/Spotymod.svg host/vcv/src/generated_panel.hpp
git diff --cached --check
git commit -m "style(vcv): quiet faceplate hierarchy"
```

---

## Final branch verification

After all three task reviews are clean:

```powershell
cd host/vcv
python res/gen_panel.py
python res/test_panel.py
cd ../..
git diff main...HEAD --check
git status --short
```

Expected: generator counts unchanged, `PASS -- panel guards ok`, no
whitespace errors, and a clean worktree. Inspect the generated
`host/vcv/res/Spotymod.svg` at 100%, 75%, and 50% scale for clipped labels,
incorrect mirrored masks, uneven margins, and lost hierarchy. The x86_64 VCV
build must be reported as either passing or unavailable with the compiler check
evidence above.
