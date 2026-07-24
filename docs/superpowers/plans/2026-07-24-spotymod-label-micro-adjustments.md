# Spotymod Label Micro-adjustments Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move SCAN to the outward side of MELO and raise both RATE captions by 0.80 mm without changing controls, parameter IDs, or any other faceplate geometry.

**Architecture:** Keep `host/vcv/res/gen_panel.py` as the sole geometry source. Adjust the top radial-label radius and the radial sampler-alias branch, lock both behaviors in `test_panel.py`, then regenerate the committed SVG/C++ table and reinstall through the existing machine-local build script.

**Tech Stack:** Python 3 generator/guards, SVG, generated C++ header, VCV Rack SDK 2.6.6, MSYS2 Make, WinLibs x86_64 GCC.

## Global Constraints

- Panel remains exactly 42 HP.
- All 80 parameter IDs, runtime names, widget kinds, and control positions remain unchanged.
- Both LED rings remain unchanged.
- MELO keeps its current primary radial-caption position.
- Deck B remains the exact geometric mirror of Deck A.
- VOICE, FX, PLAY, center groups, fields, colors, and opacities remain unchanged.
- Generated SVG/header output must be deterministic.

---

### Task 1: Correct RATE and SCAN label geometry

**Files:**
- Modify: `host/vcv/res/test_panel.py`
- Modify: `host/vcv/res/gen_panel.py`
- Regenerate: `host/vcv/res/Spotymod.svg`
- Regenerate: `host/vcv/src/generated_panel.hpp`

**Interfaces:**
- Consumes: `orbit_label(cx, cy, ang_deg, mir)`, `SAMPLER_RADIAL`, `SAMPLER_GAP`, `sampler_texts()`, `mirror_label()`.
- Produces: RATE baselines at `3.00 mm`; outward SCAN records mirrored from Deck A.

- [ ] **Step 1: Write the failing RATE and outward-SCAN guards**

Change RATE in `ORBIT_A`:

```python
'RATE_A': (39.500, 9.000, 39.500, 3.000, 'middle'),
```

Replace the alias anchor/gap portion of
`test_sampler_words_sit_inline_behind_their_caption()` with radial-aware
expectations:

```python
radial = base in g.SAMPLER_RADIAL
if radial:
    want_anchor = 'end' if suffix == '_A' else 'start'
else:
    want_anchor = 'start' if suffix == '_A' else 'end'
check(t[5] == want_anchor,
      f"{c.enum}: {word} anchored {t[5]!r}, want {want_anchor!r}")

cap_l, cap_r = text_span(lx, anchor, c.label, size)
word_l, word_r = text_span(t[0], t[5], word, t[2])
if radial:
    gap = cap_l - word_r if suffix == '_A' else word_l - cap_r
else:
    gap = word_l - cap_r if suffix == '_A' else cap_l - word_r
check(approx(gap, g.SAMPLER_GAP),
      f"{c.enum}: gap {gap:.2f} mm, want {g.SAMPLER_GAP}")
```

Add a dedicated visual-contract guard:

```python
def test_scan_sits_outward_of_melo_and_clear_of_its_knob():
    for suffix in ('_A', '_B'):
        c = ctl('MELODY' + suffix)
        lx, _ly, anchor, size, _colour = g.label_of(c)
        scan = sampler_text('SCAN', c)
        cap_l, cap_r = text_span(lx, anchor, c.label, size)
        scan_l, scan_r = text_span(scan[0], scan[5], 'SCAN', scan[2])
        if suffix == '_A':
            check(scan_r < cap_l and scan_r < c.x,
                  "SCAN_A is not outward of MELO_A")
        else:
            check(scan_l > cap_r and scan_l > c.x,
                  "SCAN_B is not outward of MELO_B")
        check(scan_l >= 1.0 and scan_r <= g.W - 1.0,
              f"{c.enum}: SCAN leaves panel ({scan_l:.2f}..{scan_r:.2f})")
```

- [ ] **Step 2: Run guards and record RED**

Run:

```powershell
cd host/vcv
python res/test_panel.py
```

Expected: failures for RATE A/B Y geometry and both SCAN outward/anchor/gap
contracts. Parameter-order failures must not appear.

- [ ] **Step 3: Implement the minimal generator changes**

Raise only the top radial caption by changing the top-label radius inside
`orbit_label()`:

```python
r = 31.3 if c < -0.38 else (31.5 if (abs(s) < 0.38 and c > 0.38) else 31.7)
```

In the radial branch of Deck-A alias generation, derive SCAN from MELO's left
glyph edge and make it grow outward:

```python
if base in SAMPLER_RADIAL:
    lx, ly, anchor, size, col = c.lbl
    cap_left = lx - text_w(c.label, size) if anchor == "end" else lx
    alias = (cap_left - SAMPLER_GAP, ly, SAMPLER_SIZE,
             0.0, MUTED, "end", word)
else:
    _lx, ly, _anchor, size, col = default_label_of(c)
    mid = (text_w(c.label, size) - ws) / 2.0
    cap_end = c.x + mid - SAMPLER_GAP / 2.0
    c.lbl = (cap_end, ly, "end", size, col)
    alias = (cap_end + SAMPLER_GAP, ly, SAMPLER_SIZE,
             0.0, MUTED, "start", word)
```

Keep Deck B derived only through the existing X/anchor mirror code. Update the
nearby sampler docstring so it states that radial aliases grow outward while
centered aliases retain their existing inline pair behavior.

- [ ] **Step 4: Regenerate and verify GREEN**

Run:

```powershell
cd host/vcv
python res/gen_panel.py
python res/test_panel.py
```

Expected:

```text
params=80 (stride=23) inputs=4 outputs=6 lights=4  panel=42HP
PASS -- panel guards ok
```

- [ ] **Step 5: Verify determinism and compatibility**

Hash SVG/header, regenerate, and require identical hashes. Then run:

```powershell
python -c "import sys; sys.path.insert(0, 'res'); import gen_panel as g; import test_panel as t; assert [c.enum for c in g.PARAMS] == t.PARAM_ORDER; assert g.HP == 42; print('PARAM_ORDER unchanged:', len(t.PARAM_ORDER))"
git diff --check
git status --short
```

Expected: `PARAM_ORDER unchanged: 80`, deterministic files, and only the four
scoped panel files modified.

- [ ] **Step 6: Visually inspect the regenerated panel**

Reload the local faceplate preview at Rack 100% scale. Confirm:

- SCAN is fully outside the MELO knob on both decks.
- A reads `SCAN MELO`; B reads `MELO SCAN`.
- Both RATE captions have visibly more air without clipping the top edge.
- No other label or background field moved.

- [ ] **Step 7: Commit**

```powershell
git add host/vcv/res/test_panel.py host/vcv/res/gen_panel.py `
        host/vcv/res/Spotymod.svg host/vcv/src/generated_panel.hpp
git commit -m "fix(vcv): refine scan and rate labels"
```

- [ ] **Step 8: Build and install**

From the worktree, use the existing machine-local script with the explicit SDK
override required by the linked-worktree path:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc `
  'source /c/Users/bernd/Documents/AI/Spotykach/host/vcv/build-local.sh install RACK_DIR=/c/Users/bernd/Documents/AI/Rack-SDK' `
  'C:/Users/bernd/Documents/AI/Spotykach/.worktrees/vcv-face-cleanup/host/vcv/build-local.sh'
```

Expected: exit `0`, archive
`Spotymod-2.11.0-win-x64.vcvplugin`, and an updated unpacked
`%LOCALAPPDATA%\Rack2\plugins-win-x64\Spotymod`.

- [ ] **Step 9: Verify installed artifacts**

Require SHA-256 equality between:

- `dist/Spotymod/plugin.dll` and installed `Spotymod/plugin.dll`
- source `res/Spotymod.svg` and installed `Spotymod/res/Spotymod.svg`

Report whether Rack is running and therefore needs a restart.
