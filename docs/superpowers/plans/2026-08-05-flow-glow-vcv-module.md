# FireFlow Glow — VCV Module (Plan B) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build **FireFlow Glow** — a second, 12 HP module in the existing `Fireflow` VCV plugin that drives the already-merged `engine/flow/` layer from six macro knobs, one NEW button, five CV inputs and a clock input, per §5 and §6 of `docs/superpowers/specs/2026-08-05-flow-machine-design.md`.

**Architecture:** Three layers, mirroring how the big Fireflow module is built. (1) `res/gen_flow_panel.py` is the single source of truth for panel geometry: it emits `res/Glow.svg` and `src/generated_flow_panel.hpp`, and the C++ never hardcodes a coordinate or a label. (2) `src/glow_ui.hpp` holds every piece of module logic that does **not** need a Rack type — CV scaling, knob-travel tracking, LED signatures, persistence payload — so the desktop doctest suite can test it headlessly. (3) `src/Glow.cpp` is thin Rack glue: it owns an `Instrument` + `flow::Flow` + `flow::Gesture`, reads the panel each control tick, and pushes audio out.

**Tech Stack:** C++17, VCV Rack SDK 2.6.6, MinGW GCC (via `build-local.sh`), Python 3 for the panel generator, doctest (vendored) for the headless units.

## Global Constraints

- **Build the VCV plugin only via `host/vcv/build-local.sh`** — never a hand-rolled `g++`/`make` invocation. The system `g++` on this machine is the Daisy ARM cross-compiler and fails with "MinGW not found". `./build-local.sh` builds; `./build-local.sh install` builds and syncs into Rack's user plugin dir.
- **Build the engine tests only per CLAUDE.md:** `source env.sh`, then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build`, `ctest --test-dir build --output-on-failure`. **`-DCMAKE_BUILD_TYPE=Release` is not optional** — a Debug configure makes pre-existing render-hash tests fail with "SYNTH reference moved".
- **Never hand-edit `res/Glow.svg` or `src/generated_flow_panel.hpp`.** Both are generated; the only way to change them is to change `res/gen_flow_panel.py` and re-run it. Task 1's `test_flow_panel.py` fails if the committed files disagree with the generator.
- **The panel enum order is a frozen patch-compatibility contract** from the moment Task 1 lands. Appending is allowed; reordering or removing silently corrupts every saved patch. `test_flow_panel.py` pins the order explicitly.
- **`ton-k` must not appear on the panel.** It is the brand, not panel copy (spec §6). The silkscreen is `FireFlow` in a light weight plus `GLOW` bold, on **one line**.
- **The plugin slug stays `Fireflow`.** The new module slug is `Glow`. Do not rename the plugin, the existing module, or `res/Fireflow.svg`.
- **Nothing in this plan may change `engine/flow/` logic.** The one permitted engine edit is adding the house-seed constant to `engine/flow/taste.h` (Task 2) — that file is explicitly tuning *data*.
- **No new byte-identity or render-hash gates.** Project convention: renders are sanity checks (memory `spotykach-bit-exactness-not-required`).
- **Every new test must be proven RED once** before its implementation exists — project convention "a test that cannot go red gets fixed".
- **Commit trailer on every commit:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **Spec is the authority:** `docs/superpowers/specs/2026-08-05-flow-machine-design.md`. Where this plan and the spec disagree, the spec wins — stop and ask.

## Context the implementer needs

`engine/flow/` is **already built and merged** (`main`, commit `8b1fe99`). Do not reimplement any of it. Its public surface, all in `namespace spky::flow`:

```cpp
// engine/flow/flow.h
class Flow {
    void init(Instrument* inst, float ctrl_hz);   // resets EVERYTHING
    void set_ctrl_hz(float ctrl_hz);              // rate change only; keeps state
    void wake(const TerrainState& s);             // instant, force-pushes every setter
    void set_macro(int m, float v);               // knob, 0..1
    void set_cv(int m, float v);                  // additive, any range; Flow clamps
    void tick();                                  // one control tick
    bool new_full();                              // true = accepted, false = refused
    bool new_partial(uint8_t macro_mask);
    bool undo();
    void set_lock(bool on);
    bool locked() const;  bool can_undo() const;
    const TerrainState& state() const;
    const TerrainState& undo_state() const;
    void restore_undo(const TerrainState& s, bool have_undo);
    float blend_phase() const;                    // 1.f when settled
    float param_now(int p) const;                 // last value pushed to the engine
    double now_s() const;                         // flow-internal clock, seconds
};
```

```cpp
// engine/flow/gesture.h -- pure decision logic, calls nothing
struct GestureOut { enum Op { NONE, NEW_FULL, NEW_PARTIAL, UNDO, LOCK_TOGGLE, REFUSED } op; uint8_t mask; };
class Gesture {
    void button(bool down, double now_s, bool locked);
    void knob_delta(int macro, float delta, double now_s);
    void tick(double now_s, bool can_undo);
    GestureOut poll();
    enum Led { LED_IDLE, LED_BLEND, LED_MARKED, LED_UNDO_ARMED, LED_LOCKED, LED_REFUSE };
    Led led(float blend_phase, bool locked) const;
};
```

```cpp
// engine/flow/flow_ids.h
enum Macro { M_MOTION = 0, M_DENSITY, M_BRIGHT, M_DIRT, M_WANDER, M_SPACE, MACRO_COUNT };
// engine/flow/terrain_code.h
constexpr int kTerrainCodeLen = 3 + 8 + 1 + MACRO_COUNT * 2;   // == 24
int  encode_code(const TerrainState& st, char* out, int cap);  // -1 if cap too small
bool decode_code(const char* code, TerrainState& out);         // strict; false leaves out untouched
```

Restore order matters and is documented in `flow.h`: `wake(state)` → `set_lock(lock)` → `restore_undo(undo, have_undo)`. `wake()` deliberately clears the undo slot, so restoring it must come last.

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `host/vcv/res/gen_flow_panel.py` | **Create.** Single source of truth for Glow's geometry, labels and colours. Emits the SVG and the header. | 1 |
| `host/vcv/res/test_flow_panel.py` | **Create.** Guard rails: enum order, panel size, no overlap, on-panel, logo copy, generator-vs-committed-files agreement. | 1 |
| `host/vcv/res/Glow.svg` | **Generated.** The faceplate; doubles as the 1:1 M6 panel draft (spec §6). | 1 |
| `host/vcv/src/generated_flow_panel.hpp` | **Generated.** Enums + control tables in `namespace spkyvcv::glow`. | 1 |
| `engine/flow/taste.h` | **Modify.** Add `kHouseCode` — the wake-up terrain (spec §5). Tuning data only. | 2 |
| `host/vcv/src/glow_ui.hpp` | **Create.** Rack-free module logic: CV scaling, knob-travel tracker, LED signatures, persistence payload. | 2, 4, 5 |
| `tests/test_glow_ui.cpp` | **Create.** Desktop doctests for everything in `glow_ui.hpp`. | 2, 4, 5 |
| `CMakeLists.txt` | **Modify.** Add `tests/test_glow_ui.cpp` to `spky_tests`. | 2 |
| `host/vcv/src/Glow.cpp` | **Create.** The Rack module + widget + context menu. | 3, 4, 5 |
| `host/vcv/src/plugin.hpp`, `plugin.cpp` | **Modify.** Declare and register `modelGlow`. | 3 |
| `host/vcv/plugin.json` | **Modify.** Second module entry. | 3 |
| `host/vcv/Makefile` | **Modify.** Add `engine/flow/*.cpp` to `SOURCES`, `res/Glow.svg` to `DISTRIBUTABLES`. | 3 |
| `host/vcv/README.md`, `docs/roadmap.md`, `CLAUDE.md` | **Modify.** Document the module. | 6 |

---

### Task 1: Panel generator and its guard rails

Glow's geometry, in one Python file, with tests that make the committed SVG and header un-driftable. No C++ in this task.

**Files:**
- Create: `host/vcv/res/gen_flow_panel.py`
- Create: `host/vcv/res/test_flow_panel.py`
- Generated (commit both): `host/vcv/res/Glow.svg`, `host/vcv/src/generated_flow_panel.hpp`

**Interfaces:**
- Consumes: `host/vcv/res/gen_panel.py` — imported as a module purely for its palette constants (`PAPER`, `PAPER_HI`, `PAPER_LO`, `INK`, `MUTED`, `GRAPHITE`, `WELL`, `WHITE`, `LINE`, `GREEN`, `MM_PER_HP`). Importing it is safe: it only writes files under `if __name__ == "__main__"`.
- Produces, for Task 3 to include:
  - `namespace spkyvcv::glow` containing `struct XY { float x, y; };`,
    `enum WidgetKind { WK_MACRO, WK_BTN, WK_IN, WK_OUT };`,
    `struct PanelCtl { int id; WidgetKind kind; XY mm; const char* label; XY lbl; unsigned char anchor; float lblSize; unsigned lblRgb; const char* tip; };`,
    `struct PanelTxt { XY mm; float size; unsigned rgb; unsigned char anchor; const char* str; };`
  - `enum ParamId { MOTION, DENSITY, BRIGHT, DIRT, WANDER, SPACE, NEW_BTN, NUM_PARAMS };`
  - `enum InputId { CV_MOT, CV_DEN, CV_BRT, CV_DRT, CV_SPC, CLK, NUM_INPUTS };`
  - `enum OutputId { OUT_L, OUT_R, NUM_OUTPUTS };`
  - `enum LightId { NEW_L, NUM_LIGHTS };`
  - `static constexpr float kPanelW`, `kPanelH`, `kKnobR`, `kBtnR`, `kJackR`
  - `static const PanelCtl kParamCtls[]`, `kInputCtls[]`, `kOutputCtls[]`
  - `static const PanelTxt kTexts[]`

**Why the types live in `namespace spkyvcv::glow`:** the big module's `generated_panel.hpp` already defines `spkyvcv::XY`, `spkyvcv::PanelCtl` and `spkyvcv::PanelTxt` with different contents. Two different definitions of the same type in one `plugin.dll` is an ODR violation even though no single translation unit includes both headers. The nested namespace makes them genuinely distinct types.

**Layout (spec §6), all in mm, panel origin top-left:**

| Element | Coordinates |
|---|---|
| Plate | 60.960 × 128.500 (12 HP × standard Eurorack height) |
| Logo line | baseline y = 10.0; `FireFlow` ends at x = 29.9 (anchor `end`), `GLOW` starts at x = 31.1 (anchor `start`) |
| Knob columns | x = 10.48, 30.48, 50.48 (20 mm pitch, centred on 30.48) |
| Knob rows | y = 32.0 (MOTION · DENSITY · BRIGHT), y = 54.0 (DIRT · WANDER · SPACE) |
| Knob radius | 8.0 (16 mm knobs) |
| Knob labels | knob y + 10.5, centred, size 2.6 |
| NEW button | (30.48, 78.0), radius 4.5; label at y = 84.5, size 2.9 |
| Jack columns | x = 9.48, 23.48, 37.48, 51.48 (14 mm pitch, centred) |
| Jack rows | y = 100.0 (`CV MOT · CV DEN · CV BRT · CV DRT`), y = 117.0 (`CV SPC · CLK · OUT L · OUT R`) |
| Jack radius | 4.2 |
| Jack labels | jack y − 5.6, centred, size 2.2 |

- [ ] **Step 1: Write the failing test**

```python
# host/vcv/res/test_flow_panel.py
#!/usr/bin/env python3
"""Guard rails for the generated FireFlow Glow panel.

Runs the generator in-process and asserts what must never drift: the enum
ORDER (patch compatibility), the geometry of spec 6, the silkscreen copy,
and that the committed SVG/header still match what the generator emits.

No pytest in this environment -- plain asserts, exit code says it all.
Run from host/vcv/:  python res/test_flow_panel.py
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_flow_panel as g

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def approx(a, b, tol=0.01):
    return abs(a - b) <= tol


# --- the frozen contract: enum ORDER defines ids in every saved patch --------
PARAM_ORDER = ['MOTION', 'DENSITY', 'BRIGHT', 'DIRT', 'WANDER', 'SPACE',
               'NEW_BTN']
INPUT_ORDER = ['CV_MOT', 'CV_DEN', 'CV_BRT', 'CV_DRT', 'CV_SPC', 'CLK']
OUTPUT_ORDER = ['OUT_L', 'OUT_R']


def test_enum_order():
    check([c.enum for c in g.PARAMS] == PARAM_ORDER,
          "param enum order drifted: %s" % [c.enum for c in g.PARAMS])
    check([c.enum for c in g.INPUTS] == INPUT_ORDER,
          "input enum order drifted: %s" % [c.enum for c in g.INPUTS])
    check([c.enum for c in g.OUTPUTS] == OUTPUT_ORDER,
          "output enum order drifted: %s" % [c.enum for c in g.OUTPUTS])


def test_macro_params_match_flow_macro_order():
    # engine/flow/flow_ids.h: M_MOTION, M_DENSITY, M_BRIGHT, M_DIRT,
    # M_WANDER, M_SPACE. Glow.cpp indexes params[MOTION + m] directly, so
    # the first six params MUST be the six macros in that order.
    check([c.enum for c in g.PARAMS][:6] ==
          ['MOTION', 'DENSITY', 'BRIGHT', 'DIRT', 'WANDER', 'SPACE'],
          "the first six params must mirror flow_ids.h's Macro order")


def test_panel_size():
    check(approx(g.W, 60.96), "panel width is %.3f, want 60.96 (12 HP)" % g.W)
    check(approx(g.Hh, 128.5), "panel height is %.3f, want 128.5" % g.Hh)


def test_knob_geometry():
    knobs = [c for c in g.PARAMS if c.kind == g.MACRO]
    check(len(knobs) == 6, "want 6 macro knobs, have %d" % len(knobs))
    check(approx(g.KNOB_R, 8.0), "knobs must be 16 mm (r=8), have r=%.2f" % g.KNOB_R)
    xs = sorted({c.x for c in knobs})
    ys = sorted({c.y for c in knobs})
    check(len(xs) == 3 and len(ys) == 2, "want a 3x2 grid, have %dx%d" % (len(xs), len(ys)))
    check(approx(xs[1] - xs[0], 20.0) and approx(xs[2] - xs[1], 20.0),
          "column pitch must be 20 mm, have %s" % [round(b - a, 2)
                                                   for a, b in zip(xs, xs[1:])])
    check(approx((xs[0] + xs[2]) / 2.0, g.W / 2.0),
          "knob grid is not centred on the plate")
    row1 = [c.enum for c in knobs if approx(c.y, ys[0])]
    row2 = [c.enum for c in knobs if approx(c.y, ys[1])]
    check(row1 == ['MOTION', 'DENSITY', 'BRIGHT'], "row 1 is %s" % row1)
    check(row2 == ['DIRT', 'WANDER', 'SPACE'], "row 2 is %s" % row2)


def test_jack_geometry():
    jacks = g.INPUTS + g.OUTPUTS
    check(len(jacks) == 8, "want 8 jacks, have %d" % len(jacks))
    xs = sorted({c.x for c in jacks})
    ys = sorted({c.y for c in jacks})
    check(len(xs) == 4 and len(ys) == 2, "want 4x2 jacks, have %dx%d" % (len(xs), len(ys)))
    for a, b in zip(xs, xs[1:]):
        check(approx(b - a, 14.0), "jack pitch must be 14 mm, have %.2f" % (b - a))
    check(approx((xs[0] + xs[3]) / 2.0, g.W / 2.0),
          "jack block is not centred on the plate")
    row1 = [c.enum for c in jacks if approx(c.y, ys[0])]
    row2 = [c.enum for c in jacks if approx(c.y, ys[1])]
    check(row1 == ['CV_MOT', 'CV_DEN', 'CV_BRT', 'CV_DRT'], "jack row 1 is %s" % row1)
    check(row2 == ['CV_SPC', 'CLK', 'OUT_L', 'OUT_R'], "jack row 2 is %s" % row2)


def test_wander_has_no_cv_and_there_is_no_rst():
    names = [c.enum for c in g.INPUTS]
    check('CV_WAN' not in names, "WANDER must have no CV jack (spec 6)")
    check('RST' not in names, "there is deliberately no RST jack (spec 6)")


def test_no_overlap():
    all_ctls = g.PARAMS + g.INPUTS + g.OUTPUTS
    for i, a in enumerate(all_ctls):
        for b in all_ctls[i + 1:]:
            d = ((a.x - b.x) ** 2 + (a.y - b.y) ** 2) ** 0.5
            need = g.radius_of(a) + g.radius_of(b)
            check(d >= need,
                  "%s and %s overlap (%.2f mm apart, need %.2f)"
                  % (a.enum, b.enum, d, need))


def test_on_panel():
    for c in g.PARAMS + g.INPUTS + g.OUTPUTS:
        r = g.radius_of(c)
        check(c.x - r >= 2.0 and c.x + r <= g.W - 2.0,
              "%s runs off the plate horizontally" % c.enum)
        check(c.y - r >= 2.0 and c.y + r <= g.Hh - 2.0,
              "%s runs off the plate vertically" % c.enum)


def test_labels_clear_every_glyph():
    all_ctls = g.PARAMS + g.INPUTS + g.OUTPUTS
    for c in all_ctls:
        lx, ly = g.label_xy(c)
        for other in all_ctls:
            d = ((lx - other.x) ** 2 + (ly - other.y) ** 2) ** 0.5
            check(d >= g.radius_of(other),
                  "%s's label baseline sits inside %s" % (c.enum, other.enum))


def test_silkscreen_copy():
    words = [t.str for t in g.TEXTS]
    check('FireFlow' in words, "the logo must read FireFlow")
    check('GLOW' in words, "the logo must read GLOW")
    ys = {t.y for t in g.TEXTS if t.str in ('FireFlow', 'GLOW')}
    check(len(ys) == 1, "FireFlow and GLOW must sit on ONE line (spec 6)")
    joined = " ".join(words + [c.label for c in
                               g.PARAMS + g.INPUTS + g.OUTPUTS])
    check('ton-k' not in joined and 'ton k' not in joined.lower(),
          "ton-k is the brand and must not appear on the panel (spec 6)")


def test_committed_files_match_the_generator():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    for path, produced in ((os.path.join(here, "Glow.svg"), g.svg()),
                           (os.path.join(root, "src",
                                         "generated_flow_panel.hpp"), g.header())):
        if not os.path.exists(path):
            FAILS.append("%s is missing -- run res/gen_flow_panel.py" % path)
            continue
        with open(path) as f:
            on_disk = f.read()
        check(on_disk == produced,
              "%s differs from the generator's output -- it was hand-edited, "
              "or the generator was changed without re-running it" % path)


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("panel OK")
```

- [ ] **Step 2: Run the test to verify it fails**

Run from `host/vcv/`:
```bash
python res/test_flow_panel.py
```
Expected: `ModuleNotFoundError: No module named 'gen_flow_panel'`.

- [ ] **Step 3: Write the generator**

```python
# host/vcv/res/gen_flow_panel.py
#!/usr/bin/env python3
"""Single source of truth for the FireFlow Glow VCV panel (spec 6).

12 HP, drawn at true hardware dimensions so the faceplate doubles as the 1:1
draft for the M6 panel: six 16 mm macro knobs in two rows of three, a large
NEW button, and eight jacks in two rows of four along the bottom.

Palette is shared with the big Fireflow panel (gen_panel.py) so the two
modules read as one instrument. Layout is NOT shared: gen_panel.py is built
around a 42 HP two-part faceplate with LED rings, and nothing there applies.

Emits (both committed):
  - res/Glow.svg                     the faceplate
  - src/generated_flow_panel.hpp     enums + control/text tables

Run from host/vcv/:  python3 res/gen_flow_panel.py
The C++ never hardcodes a coordinate, label or colour -- it reads them from
the generated header, so graphics and widget placement can never drift apart.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_panel as base

HP = 12
W  = HP * base.MM_PER_HP          # 60.96 mm
Hh = 128.5                        # standard Eurorack height

# --- geometry (spec 6) --------------------------------------------------------
KNOB_R = 8.0                      # 16 mm macro knobs
BTN_R  = 4.5
JACK_R = 4.2
COL_X  = (10.48, 30.48, 50.48)    # 20 mm pitch, centred on W/2
ROW_Y  = (32.0, 54.0)
KNOB_LBL_DY = 10.5
NEW_XY = (30.48, 78.0)
NEW_LBL_DY = 6.5
JACK_X = (9.48, 23.48, 37.48, 51.48)   # 14 mm pitch, centred on W/2
JACK_Y = (100.0, 117.0)
JACK_LBL_DY = -5.6
LOGO_Y = 10.0

# --- control kinds ------------------------------------------------------------
MACRO = "MACRO"
BTN   = "BTN"
IN    = "IN"
OUT   = "OUT"

RADIUS = {MACRO: KNOB_R, BTN: BTN_R, IN: JACK_R, OUT: JACK_R}
LBL_DY = {MACRO: KNOB_LBL_DY, BTN: NEW_LBL_DY, IN: JACK_LBL_DY, OUT: JACK_LBL_DY}
LBL_SZ = {MACRO: 2.6, BTN: 2.9, IN: 2.2, OUT: 2.2}
WKMAP  = {MACRO: "WK_MACRO", BTN: "WK_BTN", IN: "WK_IN", OUT: "WK_OUT"}


class Ctl(object):
    def __init__(self, enum, kind, x, y, label, tip):
        self.enum, self.kind = enum, kind
        self.x, self.y = x, y
        self.label, self.tip = label, tip


class Txt(object):
    def __init__(self, x, y, size, rgb, anchor, s):
        self.x, self.y, self.size = x, y, size
        self.rgb, self.anchor, self.str = rgb, anchor, s


def radius_of(c):
    return RADIUS[c.kind]


def label_xy(c):
    """Baseline position of a control's caption."""
    return (c.x, c.y + LBL_DY[c.kind])


# --- the tables ---------------------------------------------------------------
PARAMS = [
    Ctl("MOTION",  MACRO, COL_X[0], ROW_Y[0], "MOTION",  "MOTION -- how much everything moves"),
    Ctl("DENSITY", MACRO, COL_X[1], ROW_Y[0], "DENSITY", "DENSITY -- how much happens"),
    Ctl("BRIGHT",  MACRO, COL_X[2], ROW_Y[0], "BRIGHT",  "BRIGHT -- spectral centre"),
    Ctl("DIRT",    MACRO, COL_X[0], ROW_Y[1], "DIRT",    "DIRT -- clean to driven"),
    Ctl("WANDER",  MACRO, COL_X[1], ROW_Y[1], "WANDER",  "WANDER -- predictable to wandering"),
    Ctl("SPACE",   MACRO, COL_X[2], ROW_Y[1], "SPACE",   "SPACE -- close to vast"),
    Ctl("NEW_BTN", BTN,   NEW_XY[0], NEW_XY[1], "NEW",
        "NEW -- tap: new terrain. Hold + turn a knob: reroll that macro. "
        "Hold 1.5 s: undo. Hold 5 s: lock."),
]
INPUTS = [
    Ctl("CV_MOT", IN, JACK_X[0], JACK_Y[0], "CV MOT", "CV into MOTION (0..10 V, adds to the knob)"),
    Ctl("CV_DEN", IN, JACK_X[1], JACK_Y[0], "CV DEN", "CV into DENSITY (0..10 V, adds to the knob)"),
    Ctl("CV_BRT", IN, JACK_X[2], JACK_Y[0], "CV BRT", "CV into BRIGHT (0..10 V, adds to the knob)"),
    Ctl("CV_DRT", IN, JACK_X[3], JACK_Y[0], "CV DRT", "CV into DIRT (0..10 V, adds to the knob)"),
    Ctl("CV_SPC", IN, JACK_X[0], JACK_Y[1], "CV SPC", "CV into SPACE (0..10 V, adds to the knob)"),
    Ctl("CLK",    IN, JACK_X[1], JACK_Y[1], "CLK",    "Clock in, one pulse per beat -- overrides the terrain's tempo"),
]
OUTPUTS = [
    Ctl("OUT_L", OUT, JACK_X[2], JACK_Y[1], "OUT L", "Main out, left"),
    Ctl("OUT_R", OUT, JACK_X[3], JACK_Y[1], "OUT R", "Main out, right"),
]
TEXTS = [
    Txt(29.9, LOGO_Y, 5.0, base.MUTED, 2, "FireFlow"),
    Txt(31.1, LOGO_Y, 5.0, base.INK,   1, "GLOW"),
]


# --- SVG ----------------------------------------------------------------------
def mm(v):
    return "%.3f" % v


ANCHOR_SVG = {0: "middle", 1: "start", 2: "end"}


def knob_svg(c):
    """Graphite cap with a white pointer tick at 12 o'clock."""
    return (
        '  <circle cx="%s" cy="%s" r="%s" fill="%s" stroke="%s" stroke-width="0.30"/>\n'
        '  <line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" stroke-width="0.55" '
        'stroke-linecap="round"/>\n'
        % (mm(c.x), mm(c.y), mm(KNOB_R), base.GRAPHITE, base.LINE,
           mm(c.x), mm(c.y - KNOB_R * 0.45), mm(c.x), mm(c.y - KNOB_R * 0.85),
           base.WHITE))


def button_svg(c):
    return (
        '  <circle cx="%s" cy="%s" r="%s" fill="%s" stroke="%s" stroke-width="0.35"/>\n'
        '  <circle cx="%s" cy="%s" r="%s" fill="%s" opacity="0.85"/>\n'
        % (mm(c.x), mm(c.y), mm(BTN_R), base.WELL, base.LINE,
           mm(c.x), mm(c.y), mm(BTN_R * 0.55), base.GREEN))


def jack_svg(c):
    return (
        '  <circle cx="%s" cy="%s" r="%s" fill="%s" stroke="%s" stroke-width="0.30"/>\n'
        '  <circle cx="%s" cy="%s" r="%s" fill="%s"/>\n'
        % (mm(c.x), mm(c.y), mm(JACK_R), base.GRAPHITE, base.LINE,
           mm(c.x), mm(c.y), mm(JACK_R * 0.38), base.WELL))


def text_svg(x, y, size, rgb, anchor, s):
    return ('  <text x="%s" y="%s" font-family="Inter, Helvetica, sans-serif" '
            'font-size="%s" fill="%s" text-anchor="%s">%s</text>\n'
            % (mm(x), mm(y), mm(size), rgb, ANCHOR_SVG[anchor], s))


def svg():
    out = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>\n')
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%smm" height="%smm" '
               'viewBox="0 0 %s %s">\n' % (mm(W), mm(Hh), mm(W), mm(Hh)))
    out.append('  <defs><linearGradient id="plate" x1="0" y1="0" x2="0" y2="1">\n')
    out.append('    <stop offset="0" stop-color="%s"/>\n' % base.PAPER_HI)
    out.append('    <stop offset="1" stop-color="%s"/>\n' % base.PAPER_LO)
    out.append('  </linearGradient></defs>\n')
    out.append('  <rect x="0" y="0" width="%s" height="%s" fill="url(#plate)"/>\n'
               % (mm(W), mm(Hh)))
    # hairline above the jack block
    out.append('  <line x1="4.000" y1="%s" x2="%s" y2="%s" stroke="%s" '
               'stroke-width="0.25"/>\n'
               % (mm(JACK_Y[0] + JACK_LBL_DY - 4.0), mm(W - 4.0),
                  mm(JACK_Y[0] + JACK_LBL_DY - 4.0), base.LINE))
    for c in PARAMS:
        out.append(knob_svg(c) if c.kind == MACRO else button_svg(c))
    for c in INPUTS + OUTPUTS:
        out.append(jack_svg(c))
    for t in TEXTS:
        out.append(text_svg(t.x, t.y, t.size, t.rgb, t.anchor, t.str))
    for c in PARAMS + INPUTS + OUTPUTS:
        lx, ly = label_xy(c)
        out.append(text_svg(lx, ly, LBL_SZ[c.kind], base.INK, 0, c.label))
    out.append('</svg>\n')
    return "".join(out)


# --- header -------------------------------------------------------------------
def rgb(hexcol):
    return "0x" + hexcol.lstrip("#").upper()


def ctl_row(c):
    lx, ly = label_xy(c)
    return ('    { %s, %s, {%sf, %sf}, "%s", {%sf, %sf}, 0, %sf, %s, "%s" },\n'
            % (c.enum, WKMAP[c.kind], mm(c.x), mm(c.y), c.label,
               mm(lx), mm(ly), mm(LBL_SZ[c.kind]), rgb(base.INK), c.tip))


def enum_block(name, ctls, last):
    body = "".join("    %s,\n" % c.enum for c in ctls)
    return "enum %s {\n%s    %s\n};\n" % (name, body, last)


def header():
    out = []
    out.append("// GENERATED by res/gen_flow_panel.py -- do not edit by hand.\n")
    out.append("#pragma once\n")
    out.append("namespace spkyvcv { namespace glow {\n")
    out.append("struct XY { float x, y; };\n")
    out.append("enum WidgetKind { WK_MACRO, WK_BTN, WK_IN, WK_OUT };\n")
    out.append("struct PanelCtl { int id; WidgetKind kind; XY mm; const char* label;"
               " XY lbl; unsigned char anchor; float lblSize; unsigned lblRgb;"
               " const char* tip; };\n")
    out.append("// anchor: 0 = middle, 1 = start (left-aligned), 2 = end (right-aligned)\n")
    out.append("struct PanelTxt { XY mm; float size; unsigned rgb;"
               " unsigned char anchor; const char* str; };\n")
    out.append(enum_block("ParamId", PARAMS, "NUM_PARAMS"))
    out.append(enum_block("InputId", INPUTS, "NUM_INPUTS"))
    out.append(enum_block("OutputId", OUTPUTS, "NUM_OUTPUTS"))
    out.append("enum LightId {\n    NEW_L,\n    NUM_LIGHTS\n};\n")
    out.append("static constexpr float kPanelW = %sf;\n" % mm(W))
    out.append("static constexpr float kPanelH = %sf;\n" % mm(Hh))
    out.append("static constexpr float kKnobR = %sf;\n" % mm(KNOB_R))
    out.append("static constexpr float kBtnR  = %sf;\n" % mm(BTN_R))
    out.append("static constexpr float kJackR = %sf;\n" % mm(JACK_R))
    for name, ctls in (("kParamCtls", PARAMS), ("kInputCtls", INPUTS),
                       ("kOutputCtls", OUTPUTS)):
        out.append("static const PanelCtl %s[] = {\n" % name)
        for c in ctls:
            out.append(ctl_row(c))
        out.append("};\n")
    out.append("static const PanelTxt kTexts[] = {\n")
    for t in TEXTS:
        out.append('    { {%sf, %sf}, %sf, %s, %d, "%s" },\n'
                   % (mm(t.x), mm(t.y), mm(t.size), rgb(t.rgb), t.anchor, t.str))
    out.append("};\n")
    out.append("} } // namespace spkyvcv::glow\n")
    return "".join(out)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    with open(os.path.join(here, "Glow.svg"), "w") as f:
        f.write(svg())
    with open(os.path.join(root, "src", "generated_flow_panel.hpp"), "w") as f:
        f.write(header())
    print("wrote res/Glow.svg and src/generated_flow_panel.hpp")
```

- [ ] **Step 4: Generate the artefacts and run the test**

Run from `host/vcv/`:
```bash
python res/gen_flow_panel.py
python res/test_flow_panel.py
```
Expected: `wrote res/Glow.svg and src/generated_flow_panel.hpp`, then `panel OK`.

If `test_labels_clear_every_glyph` or `test_no_overlap` fails, **change the coordinate constants at the top of the generator** — never the test's geometry assertions, which are the spec.

- [ ] **Step 5: Prove the guard can go red**

Temporarily append one character to `host/vcv/src/generated_flow_panel.hpp` and re-run `python res/test_flow_panel.py`. Expected: FAIL naming `generated_flow_panel.hpp`. Then re-run the generator to restore it and confirm green again. This proves `test_committed_files_match_the_generator` is load-bearing.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/res/gen_flow_panel.py host/vcv/res/test_flow_panel.py \
        host/vcv/res/Glow.svg host/vcv/src/generated_flow_panel.hpp
git commit -m "feat(glow): the Glow faceplate gets a generator and guard rails

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The house seed and the Rack-free module logic

Everything Glow does that does not need a Rack type, in one header the desktop doctest suite can reach. This is where the module's real decisions get tested; `Glow.cpp` becomes glue.

**Files:**
- Modify: `engine/flow/taste.h` (append one constant near the other gesture-phase constants at lines 37–40)
- Create: `host/vcv/src/glow_ui.hpp`
- Create: `tests/test_glow_ui.cpp`
- Modify: `CMakeLists.txt` (add the test to `spky_tests`, next to `tests/test_vcv_link_migration.cpp` at line 129)

**Interfaces:**
- Consumes: `spky::flow::Flow`, `spky::flow::Gesture::Led`, `spky::flow::TerrainState`, `spky::flow::kTerrainCodeLen`, `encode_code`, `decode_code`, `spky::flow::MACRO_COUNT`.
- Produces, all in `namespace spkyvcv`:
  - `constexpr int kCvMacro[5]` — CV jack index → `spky::flow::Macro`
  - `float cv_to_macro(float volts)`
  - `struct KnobTracker { void prime(const float* v); bool deltas(const float* v, float* out); }`
  - `float led_level(int led, float blend_phase, double t)`
  - `struct GlowSave { char code[25]; char undo[25]; bool lock; bool have_undo; }`
  - `GlowSave glow_capture(const spky::flow::Flow& fl)`
  - `bool glow_restore(spky::flow::Flow& fl, const GlowSave& s)`
- Produces in `engine/flow/taste.h`: `constexpr char kHouseCode[]`.

**On the house seed:** spec §5 wants a curated wake-up terrain, chosen during a listening phase. The render-based listening pass was stopped on 2026-08-05 (`docs/superpowers/specs/2026-08-05-flow-listening-notes.md`) precisely because level is the only thing a bounced file can judge — so this constant ships as a **measured placeholder, not a curated choice**, and re-choosing it by ear on the finished module is explicit follow-up work. `F1-00000020-000000000000` is the one terrain that pass actually measured as a reasonable opener: its first note lands at 1.25 s, where the previous candidate (`F1-00000002`) opened with 11.4 s of silence.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_glow_ui.cpp
#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include "vcv/src/glow_ui.hpp"
#include "flow/taste.h"
#include "flow/terrain_code.h"

using namespace spky;
using namespace spky::flow;
using namespace spkyvcv;

TEST_CASE("glow: the house code is a decodable terrain code") {
    TerrainState st;
    CHECK(decode_code(kHouseCode, st));
}

TEST_CASE("glow: CV jacks map to the five CV-carrying macros, not WANDER") {
    CHECK(kCvMacro[0] == M_MOTION);
    CHECK(kCvMacro[1] == M_DENSITY);
    CHECK(kCvMacro[2] == M_BRIGHT);
    CHECK(kCvMacro[3] == M_DIRT);
    CHECK(kCvMacro[4] == M_SPACE);
    for (int i = 0; i < 5; ++i) CHECK(kCvMacro[i] != M_WANDER);
}

TEST_CASE("glow: unipolar 0..10 V spans the macro range") {
    CHECK(cv_to_macro(0.f)  == doctest::Approx(0.f));
    CHECK(cv_to_macro(5.f)  == doctest::Approx(0.5f));
    CHECK(cv_to_macro(10.f) == doctest::Approx(1.f));
    // Out-of-range voltages are NOT clamped here -- Flow::set_cv clamps the
    // sum, and clamping twice would silently hide a hot patch cable.
    CHECK(cv_to_macro(-5.f) == doctest::Approx(-0.5f));
    CHECK(cv_to_macro(15.f) == doctest::Approx(1.5f));
}

TEST_CASE("glow: the knob tracker reports travel, once, per macro") {
    float v[MACRO_COUNT] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float d[MACRO_COUNT];
    KnobTracker kt;
    kt.prime(v);
    CHECK_FALSE(kt.deltas(v, d));            // nothing moved
    for (int m = 0; m < MACRO_COUNT; ++m) CHECK(d[m] == doctest::Approx(0.f));

    v[M_BRIGHT] = 0.62f;
    CHECK(kt.deltas(v, d));
    CHECK(d[M_BRIGHT] == doctest::Approx(0.12f));
    CHECK(d[M_MOTION] == doctest::Approx(0.f));

    // The same position on the next tick is no longer travel.
    CHECK_FALSE(kt.deltas(v, d));
    CHECK(d[M_BRIGHT] == doctest::Approx(0.f));

    // Direction is irrelevant: the decoder marks on absolute travel.
    v[M_BRIGHT] = 0.5f;
    CHECK(kt.deltas(v, d));
    CHECK(d[M_BRIGHT] == doctest::Approx(0.12f));
}

TEST_CASE("glow: an unprimed tracker reports no travel on its first look") {
    // A module that has just been added must not spray six phantom deltas
    // into the gesture decoder on its first control tick.
    float v[MACRO_COUNT] = {0.f, 0.2f, 0.4f, 0.6f, 0.8f, 1.f};
    float d[MACRO_COUNT];
    KnobTracker kt;
    CHECK_FALSE(kt.deltas(v, d));
    for (int m = 0; m < MACRO_COUNT; ++m) CHECK(d[m] == doctest::Approx(0.f));
}

TEST_CASE("glow: every LED signature stays in range and is distinguishable") {
    const int leds[] = { Gesture::LED_IDLE, Gesture::LED_BLEND,
                         Gesture::LED_MARKED, Gesture::LED_UNDO_ARMED,
                         Gesture::LED_LOCKED, Gesture::LED_REFUSE };
    for (int led : leds) {
        float lo = 2.f, hi = -1.f;
        for (int k = 0; k < 400; ++k) {
            const double t = 0.005 * k;          // two seconds at 200 Hz
            const float b = led_level(led, 0.5f, t);
            CHECK(b >= 0.f);
            CHECK(b <= 1.f);
            lo = std::fmin(lo, b);
            hi = std::fmax(hi, b);
        }
        // Locked is solid; idle is a steady dim glow. Everything else moves.
        if (led == Gesture::LED_LOCKED) {
            CHECK(lo == doctest::Approx(1.f));
        } else if (led == Gesture::LED_IDLE) {
            CHECK(hi == lo);
            CHECK(hi < 0.2f);
        } else {
            CHECK(hi - lo > 0.4f);
        }
    }
}

TEST_CASE("glow: MARKED and REFUSE are not the same light") {
    // Both flicker; a player must still be able to tell "I marked a macro"
    // from "the module refused me".
    int differ = 0;
    for (int k = 0; k < 200; ++k) {
        const double t = 0.005 * k;
        if (std::fabs(led_level(Gesture::LED_MARKED, 1.f, t) -
                      led_level(Gesture::LED_REFUSE, 1.f, t)) > 0.25f)
            ++differ;
    }
    CHECK(differ > 40);
}

TEST_CASE("glow: a saved payload restores the terrain, the lock and the undo slot") {
    Instrument inst;
    inst.init(48000.f);
    Flow fl;
    fl.init(&inst, 100.f);

    TerrainState house;
    REQUIRE(decode_code(kHouseCode, house));
    fl.wake(house);
    REQUIRE(fl.new_full());                  // fills the undo slot
    fl.set_lock(true);

    const GlowSave s = glow_capture(fl);
    CHECK(s.lock);
    CHECK(s.have_undo);
    CHECK(std::strlen(s.code) == size_t(kTerrainCodeLen));

    Instrument inst2;
    inst2.init(48000.f);
    Flow fl2;
    fl2.init(&inst2, 100.f);
    CHECK(glow_restore(fl2, s));
    CHECK(fl2.state().master == fl.state().master);
    for (int m = 0; m < MACRO_COUNT; ++m)
        CHECK(fl2.state().reroll[m] == fl.state().reroll[m]);
    CHECK(fl2.locked() == fl.locked());
    CHECK(fl2.can_undo());
    CHECK(fl2.undo_state().master == fl.undo_state().master);
    // A restore is bookkeeping, not a gesture: no blend may be in flight.
    CHECK(fl2.blend_phase() == doctest::Approx(1.f));
}

TEST_CASE("glow: a malformed saved code changes nothing") {
    Instrument inst;
    inst.init(48000.f);
    Flow fl;
    fl.init(&inst, 100.f);
    TerrainState house;
    REQUIRE(decode_code(kHouseCode, house));
    fl.wake(house);

    GlowSave bad;
    std::strcpy(bad.code, "F1-NOTHEX00-000000000000");
    bad.lock = true;
    CHECK_FALSE(glow_restore(fl, bad));
    CHECK(fl.state().master == house.master);
    CHECK_FALSE(fl.locked());
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Expected: FAIL — `fatal error: vcv/src/glow_ui.hpp: No such file or directory` (once the CMake entry from Step 4 is in place; before that the file simply isn't compiled, which is not a RED — add the CMake line first if the build silently passes).

- [ ] **Step 3: Add the house seed to `taste.h`**

Insert after line 40 (`constexpr float kRefuseFlashS = 0.25f;`):

```cpp
// Spec 5, the house seed: the terrain the instrument wakes on, so the first
// sound after power-on is a decision rather than a draw. PLACEHOLDER, not a
// curated choice -- the render-based listening pass was stopped on
// 2026-08-05 (docs/superpowers/specs/2026-08-05-flow-listening-notes.md)
// because a bounced file can only judge level. This is the one opener that
// pass did measure as reasonable: its first note lands at 1.25 s, where the
// previous candidate opened with 11.4 s of silence. Re-choose it by ear once
// FireFlow Glow can actually be played.
inline constexpr char kHouseCode[] = "F1-00000020-000000000000";
```

- [ ] **Step 4: Wire the test into CMake**

In `CMakeLists.txt`, immediately after `tests/test_vcv_link_migration.cpp` (line 129), add:

```cmake
    tests/test_glow_ui.cpp
```

`target_include_directories(spky_tests PRIVATE host)` (line 167) already puts `host/` on the include path, which is why the test includes `"vcv/src/glow_ui.hpp"` — exactly the pattern `tests/test_vcv_form_song_migration.cpp` uses.

- [ ] **Step 5: Write `glow_ui.hpp`**

```cpp
// host/vcv/src/glow_ui.hpp
//
// FireFlow Glow's module logic that needs no Rack type: CV scaling, knob
// travel, LED signatures and the persistence payload. Kept out of Glow.cpp
// so the desktop doctest suite can test it headlessly -- the same split
// form_song_migration.hpp and bbd_edge_state.hpp already use.
//
// No <rack.hpp>, no jansson, no widgets. Glow.cpp is the only file that
// knows what a Module is.
#pragma once
#include <cmath>
#include <cstring>
#include "flow/flow.h"
#include "flow/flow_ids.h"
#include "flow/gesture.h"
#include "flow/terrain_code.h"

namespace spkyvcv {

// Panel jack order -> macro (spec 6: five CV jacks, WANDER has none).
inline constexpr int kCvMacro[5] = {
    spky::flow::M_MOTION, spky::flow::M_DENSITY, spky::flow::M_BRIGHT,
    spky::flow::M_DIRT,   spky::flow::M_SPACE
};

// Unipolar Eurorack convention: 0..10 V spans the macro's whole travel.
// Deliberately NOT clamped -- Flow::set_cv clamps the knob+CV+weather sum,
// and clamping here as well would just hide how hot an input is running.
inline float cv_to_macro(float volts) { return volts * 0.1f; }

// Physical knob travel between control ticks, for the NEW gesture decoder's
// "hold and turn a knob to mark it" (spec 5). Absolute travel: which way the
// player turned is not part of the gesture.
struct KnobTracker {
    float last[spky::flow::MACRO_COUNT] = {};
    bool  primed = false;

    void prime(const float* v) {
        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m) last[m] = v[m];
        primed = true;
    }

    // Writes |travel| per macro into out[]; returns true if anything moved.
    // The first look at an unprimed tracker reports nothing: a freshly added
    // module must not hand the decoder six phantom deltas.
    bool deltas(const float* v, float* out) {
        bool any = false;
        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m) {
            const float d = primed ? std::fabs(v[m] - last[m]) : 0.f;
            out[m] = d;
            if (d > 0.f) any = true;
            last[m] = v[m];
        }
        primed = true;
        return any;
    }
};

// The LED signatures of spec 5's gesture table, as brightness in 0..1.
// A pure function of (state, blend phase, flow clock) so it can be tested
// without a running module.
inline float led_level(int led, float blend_phase, double t) {
    using G = spky::flow::Gesture;
    const double kTwoPi = 6.283185307179586;
    switch (led) {
        case G::LED_LOCKED:
            return 1.f;                                   // solid while locked
        case G::LED_REFUSE:                               // fast hard blink
            return std::fmod(t, 0.1) < 0.05 ? 1.f : 0.f;
        case G::LED_MARKED:                               // faster, dimmer flicker
            return std::fmod(t, 0.05) < 0.025 ? 0.85f : 0.15f;
        case G::LED_UNDO_ARMED: {                         // two short pulses, then rest
            const double p = std::fmod(t, 1.0);
            return (p < 0.09 || (p >= 0.18 && p < 0.27)) ? 1.f : 0.05f;
        }
        case G::LED_BLEND: {                              // breathes through the blend
            const float depth = 1.f - blend_phase;        // widest at press, closing
            const float breath =
                0.5f - 0.5f * float(std::cos(kTwoPi * 0.8 * t));
            return 0.12f + 0.88f * breath * (0.35f + 0.65f * depth);
        }
        default:
            return 0.06f;                                 // idle: a dim ember
    }
}

// Exactly what a patch stores (spec 5: current terrain code, lock, undo slot).
struct GlowSave {
    char code[spky::flow::kTerrainCodeLen + 1] = {};
    char undo[spky::flow::kTerrainCodeLen + 1] = {};
    bool lock = false;
    bool have_undo = false;
};

inline GlowSave glow_capture(const spky::flow::Flow& fl) {
    GlowSave s;
    spky::flow::encode_code(fl.state(), s.code, int(sizeof s.code));
    spky::flow::encode_code(fl.undo_state(), s.undo, int(sizeof s.undo));
    s.lock = fl.locked();
    s.have_undo = fl.can_undo();
    return s;
}

// Applies a saved payload. Returns false and touches NOTHING if the terrain
// code is malformed -- a corrupt patch must not silently move the player to
// some other instrument. The order is the one flow.h documents: wake clears
// the undo slot, so restoring it comes last.
inline bool glow_restore(spky::flow::Flow& fl, const GlowSave& s) {
    spky::flow::TerrainState st;
    if (!spky::flow::decode_code(s.code, st)) return false;
    spky::flow::TerrainState un = st;
    const bool have = s.have_undo && spky::flow::decode_code(s.undo, un);
    fl.wake(st);
    fl.set_lock(s.lock);
    fl.restore_undo(un, have);
    return true;
}

}  // namespace spkyvcv
```

- [ ] **Step 6: Run the tests**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: PASS, including `./build/spky_tests.exe -tc="glow*"` reporting every `glow:` case green.

- [ ] **Step 7: Commit**

```bash
git add engine/flow/taste.h host/vcv/src/glow_ui.hpp tests/test_glow_ui.cpp CMakeLists.txt
git commit -m "feat(glow): the module's Rack-free logic, and a house seed to wake on

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The module — engine, audio path, macros, CV, clock

Glow becomes a real, audible Rack module: it wakes on the house seed, the six knobs and five CV jacks drive the flow layer, the clock overrides tempo, and stereo audio comes out. NEW and persistence are Tasks 4 and 5.

**Files:**
- Create: `host/vcv/src/Glow.cpp`
- Modify: `host/vcv/src/plugin.hpp`, `host/vcv/src/plugin.cpp`
- Modify: `host/vcv/plugin.json`
- Modify: `host/vcv/Makefile`

**Interfaces:**
- Consumes: `spkyvcv::glow::*` from Task 1's generated header; `spkyvcv::kCvMacro`, `cv_to_macro`, `KnobTracker` from Task 2; `spky::flow::kHouseCode`.
- Produces for Tasks 4 and 5: `struct Glow : Module` with members `spky::flow::Flow flow;`, `spky::flow::Gesture gest;`, `spkyvcv::KnobTracker knobs;`, and the methods `void reinit(float sr)`, `void controlTick(float sr)`, `void wakeHouse()`. `Model* modelGlow`.

**The three traps in this task:**

1. **`inst.init()` wipes every setter, and `Flow` caches what it last pushed.** After any re-init the flow layer would push nothing until a value happened to change, leaving the engine at its defaults. The fix is the restore sequence `flow.h` documents: capture `(state, lock, undo, have_undo)`, re-init the instrument, `flow.set_ctrl_hz(...)`, then `flow.wake(state)` — `wake()` force-pushes every parameter — then `set_lock`, then `restore_undo`.
2. **`Flow::init()` must be called exactly once per module instance.** On a *sample-rate change* call `set_ctrl_hz()`, never `init()`: `init()` resets the terrain, the lock and the undo slot.
3. **The terrain owns the tempo, so the clock override must be re-asserted every tick.** `Flow` pushes `P_TEMPO_BPM` itself; if the module overwrites it with a measured clock tempo, `Flow`'s pushed-value cache will not restore the terrain's tempo when the cable is pulled. So the module sets the tempo *unconditionally* every control tick, from either the clock or `flow.param_now(P_TEMPO_BPM)`.

- [ ] **Step 1: Write the module**

```cpp
// host/vcv/src/Glow.cpp
//
// FireFlow Glow (spec 6): six macro knobs, one NEW button and eight jacks
// over the portable engine/flow/ layer. The big Fireflow module is the
// full-control view of the same engine; this one is the flow machine.
//
// Everything that does not need a Rack type lives in glow_ui.hpp and is
// tested by the desktop suite. Every coordinate and label comes from
// generated_flow_panel.hpp. Nothing here is hand-placed.
#include <cmath>
#include <string>
#include <vector>
#include "plugin.hpp"
#include "generated_flow_panel.hpp"
#include "glow_ui.hpp"
#include "sampler_ui.hpp"

// The portable engine core -- the same headers the render host uses.
#include "instrument.h"
#include "flow/flow.h"
#include "flow/gesture.h"
#include "flow/taste.h"
#include "flow/terrain_code.h"

using namespace spkyvcv::glow;

// The module indexes params[MOTION + m] with m a spky::flow::Macro, so the
// panel's first six params must BE the macro enum. res/test_flow_panel.py
// guards the panel side; this guards the C++ side.
static_assert(MOTION == spky::flow::M_MOTION, "panel macro order drifted");
static_assert(DENSITY == spky::flow::M_DENSITY, "panel macro order drifted");
static_assert(BRIGHT == spky::flow::M_BRIGHT, "panel macro order drifted");
static_assert(DIRT == spky::flow::M_DIRT, "panel macro order drifted");
static_assert(WANDER == spky::flow::M_WANDER, "panel macro order drifted");
static_assert(SPACE == spky::flow::M_SPACE, "panel macro order drifted");
static_assert(CV_DEN == CV_MOT + 1 && CV_DRT == CV_MOT + 3,
              "CV jacks must stay contiguous -- controlTick indexes CV_MOT + i");

struct Glow : Module {
    spky::Instrument inst;
    spky::FxMem fxmem;
    spky::AmbientReverb reverb;

    // Engine-owned memory the host has to provide, exactly as the big module
    // does: the FX chain and the BBD engine index into these.
    std::vector<float> echoMem[spky::PART_COUNT][2];
    float bbd[spky::PART_COUNT][2][spky::BbdEngine::kCells];

    // A terrain may put SAMPLER on the texture deck (engine/flow/taste.h,
    // kTextureEngine), and a Sampler deck with an empty buffer is a silent
    // deck. Glow has no REC button and never records, so the buffer only
    // ever holds res/factory.wav -- 16 s is comfortably more than the file
    // needs, against 42 s on the big module which does record.
    static constexpr double kSamplerBufferSeconds = 16.0;
    std::vector<spky::SampleBuffer::Frame> samplerMem[spky::PART_COUNT];
    spky::WavData factoryNative;
    bool factoryNativeTried = false;
    std::vector<float> factoryL, factoryR;

    spky::flow::Flow flow;
    spky::flow::Gesture gest;
    spkyvcv::KnobTracker knobs;

    float curSr = 0.f;
    static constexpr int kCtrlDiv = 16;          // flow ticks at sr / 16
    dsp::ClockDivider ctrlDiv;
    dsp::SchmittTrigger clockTrig;
    float clkSamples = 0.f;                      // samples since the last edge
    float clkPeriod = 0.f;                       // samples between the last two
    static constexpr float kClockTimeoutS = 2.f; // spec 4: fall back to the
                                                 // terrain's own tempo
    bool woken = false;

    Glow() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        for (const auto& c : kParamCtls) {
            if (c.kind == WK_MACRO)
                configParam(c.id, 0.f, 1.f, 0.5f, c.label);
            else
                configButton(c.id, c.label);
        }
        for (const auto& c : kInputCtls)  configInput(c.id, c.tip);
        for (const auto& c : kOutputCtls) configOutput(c.id, c.tip);
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            fxmem.bbd[p][0] = bbd[p][0];
            fxmem.bbd[p][1] = bbd[p][1];
        }
        fxmem.reverb = &reverb;
        ctrlDiv.setDivision(kCtrlDiv);
    }

    // Read res/factory.wav off disk once per module instance, on the main
    // thread, before process() can run. Same split the big module uses: the
    // audio thread only ever memcpys the already-decoded, already-resampled
    // copy in factoryL/factoryR.
    void onAdd(const AddEvent& e) override {
        Module::onAdd(e);
        if (!factoryNativeTried) {
            factoryNativeTried = true;
            std::string err;
            spky::read_wav(asset::plugin(pluginInstance, "res/factory.wav"),
                           factoryNative, err);
        }
        reinit(curSr > 0.f ? curSr : 48000.f);
        if (!woken) wakeHouse();
    }

    void wakeHouse() {
        spky::flow::TerrainState st;
        if (!spky::flow::decode_code(spky::flow::kHouseCode, st)) st = {};
        flow.wake(st);
        woken = true;
    }

    void reinit(float sr) {
        // Capture whatever the flow layer is holding: inst.init() below wipes
        // every setter, and Flow only re-pushes on wake(), which is the last
        // thing this function does.
        const bool hadFlow = woken;
        const spky::flow::TerrainState st = flow.state();
        const spky::flow::TerrainState un = flow.undo_state();
        const bool lock = flow.locked(), haveUndo = flow.can_undo();

        const float prevSr = curSr;
        curSr = sr;
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            for (int ch = 0; ch < 2; ++ch) {
                if (echoMem[p][ch].size() != spky::Flux::kMaxSamples)
                    echoMem[p][ch].resize(spky::Flux::kMaxSamples);
                fxmem.echo[p][ch] = echoMem[p][ch].data();
            }
        }
        const size_t frames = (size_t)(kSamplerBufferSeconds * (double)sr);
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            if (samplerMem[p].size() != frames) samplerMem[p].resize(frames);
            fxmem.sampler_buf[p] = samplerMem[p].data();
        }
        fxmem.sampler_frames = frames;

        inst.init(sr, fxmem);

        // Rebuild the factory audio for this rate, then load it into BOTH
        // decks: a terrain can hand either deck to the SAMPLER engine at any
        // NEW press, and there is no player gesture that could load it later.
        if (!factoryNative.l.empty() && (sr != prevSr || factoryL.empty())) {
            factoryL = factoryNative.l;
            factoryR = factoryNative.r;
            if (factoryNative.sample_rate > 0
                && (float)factoryNative.sample_rate != sr) {
                const double ratio = (double)sr / (double)factoryNative.sample_rate;
                spkyvcv::resample_linear(factoryL, ratio);
                spkyvcv::resample_linear(factoryR, ratio);
            }
        }
        if (!factoryL.empty()) {
            size_t n = factoryL.size();
            if (n > frames) n = frames;
            for (int p = 0; p < spky::PART_COUNT; ++p)
                inst.load_sample(p, factoryL.data(), factoryR.data(), n);
        }

        // Flow::init() resets the terrain, the lock and the undo slot, so it
        // may run at most once per module instance. Every later rate change
        // goes through set_ctrl_hz(), which touches only the tick period and
        // the SPACE slew coefficient.
        const float ctrlHz = sr / float(kCtrlDiv);
        if (!hadFlow) {
            flow.init(&inst, ctrlHz);
        } else {
            flow.set_ctrl_hz(ctrlHz);
            flow.wake(st);            // force-pushes every setter into the
            flow.set_lock(lock);      // freshly initialised Instrument
            flow.restore_undo(un, haveUndo);
        }
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        reinit(e.sampleRate);
    }

    void onReset() override {
        reinit(curSr > 0.f ? curSr : 48000.f);
        wakeHouse();
        knobs.primed = false;
    }

    void controlTick(float sr) {
        float k[spky::flow::MACRO_COUNT];
        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m) {
            k[m] = params[MOTION + m].getValue();
            flow.set_macro(m, k[m]);
        }
        for (int i = 0; i < 5; ++i) {
            const Input& in = inputs[CV_MOT + i];
            flow.set_cv(spkyvcv::kCvMacro[i],
                        in.isConnected() ? spkyvcv::cv_to_macro(in.getVoltage())
                                         : 0.f);
        }

        flow.tick();

        // Tempo: the terrain owns it and Flow pushes it, but an external
        // clock overrides (spec 4). Set it unconditionally every tick --
        // Flow caches what it last pushed, so it would NOT restore the
        // terrain's own tempo by itself once the cable is pulled.
        float bpm = flow.param_now(spky::flow::P_TEMPO_BPM);
        if (clkPeriod > 1.f && sr > 0.f && clkSamples < sr * kClockTimeoutS) {
            const float measured = 60.f * sr / clkPeriod;
            if (measured >= 20.f && measured <= 400.f) bpm = measured;
        }
        inst.set_tempo_bpm(bpm);
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate != curSr) reinit(args.sampleRate);

        if (inputs[CLK].isConnected()) {
            clkSamples += 1.f;
            if (clockTrig.process(inputs[CLK].getVoltage(), 0.1f, 1.f)) {
                if (clkSamples > 1.f) clkPeriod = clkSamples;
                clkSamples = 0.f;
                inst.clock_pulse();
            }
        } else {
            clkPeriod = 0.f;
        }

        if (ctrlDiv.process()) controlTick(args.sampleRate);

        float outl = 0.f, outr = 0.f;
        inst.process(nullptr, nullptr, &outl, &outr, 1);
        outputs[OUT_L].setVoltage(clamp(outl, -1.f, 1.f) * 5.f);
        outputs[OUT_R].setVoltage(clamp(outr, -1.f, 1.f) * 5.f);
    }
};

// The SVG's <text> is invisible to NanoSVG, so the lettering is redrawn here
// from the generated tables -- the same reason the big module has PanelText.
struct GlowText : Widget {
    void draw(const DrawArgs& args) override {
        std::shared_ptr<window::Font> font =
            APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        auto put = [&](float xmm, float ymm, float szmm, unsigned c,
                       unsigned char anchor, const char* s) {
            nvgFontSize(args.vg, mm2px(szmm));
            nvgFillColor(args.vg, nvgRGB((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff));
            nvgTextAlign(args.vg,
                         (anchor == 1 ? NVG_ALIGN_LEFT
                                      : anchor == 2 ? NVG_ALIGN_RIGHT
                                                    : NVG_ALIGN_CENTER) |
                         NVG_ALIGN_BASELINE);
            const Vec p = mm2px(Vec(xmm, ymm));
            nvgText(args.vg, p.x, p.y, s, nullptr);
        };
        for (const auto& t : kTexts)
            put(t.mm.x, t.mm.y, t.size, t.rgb, t.anchor, t.str);
        for (const auto& c : kParamCtls)
            put(c.lbl.x, c.lbl.y, c.lblSize, c.lblRgb, c.anchor, c.label);
        for (const auto& c : kInputCtls)
            put(c.lbl.x, c.lbl.y, c.lblSize, c.lblRgb, c.anchor, c.label);
        for (const auto& c : kOutputCtls)
            put(c.lbl.x, c.lbl.y, c.lblSize, c.lblRgb, c.anchor, c.label);
    }
};

struct GlowWidget : ModuleWidget {
    GlowWidget(Glow* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Glow.svg")));

        auto* labels = new GlowText();
        labels->box.size = box.size;
        addChild(labels);

        for (const auto& c : kParamCtls) {
            const Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            if (c.kind == WK_MACRO)
                // Rack's stock knobs come in fixed sizes; RoundLargeBlackKnob
                // is 46 px ~ 15.6 mm, the nearest to the panel's 16 mm.
                // RoundBigBlackKnob (54 px ~ 18.3 mm) would overhang the
                // printed footprint by more than a millimetre a side.
                addParam(createParamCentered<RoundLargeBlackKnob>(pos, module, c.id));
            else
                addParam(createLightParamCentered<VCVLightBezel<GreenLight>>(
                    pos, module, c.id, NEW_L));
        }
        for (const auto& c : kInputCtls)
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(c.mm.x, c.mm.y)),
                                                     module, c.id));
        for (const auto& c : kOutputCtls)
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(c.mm.x, c.mm.y)),
                                                       module, c.id));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH,
                RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    }
};

Model* modelGlow = createModel<Glow, GlowWidget>("Glow");
```

- [ ] **Step 2: Register the model**

`host/vcv/src/plugin.hpp` — add below `extern Model* modelFireflow;`:
```cpp
extern Model* modelGlow;
```

`host/vcv/src/plugin.cpp` — add below `p->addModel(modelFireflow);`:
```cpp
    p->addModel(modelGlow);
```

- [ ] **Step 3: Declare the module in `plugin.json`**

Add a second entry to the `modules` array (after the existing `Fireflow` object, inside the same array):

```json
    {
      "slug": "Glow",
      "name": "FireFlow Glow",
      "description": "Flow machine: six macro knobs and a NEW button over a generative ambient terrain.",
      "tags": ["Generator", "Random", "Synth voice", "Drone", "Polyphonic"]
    }
```

- [ ] **Step 4: Add the flow sources and the panel asset to the Makefile**

`host/vcv/Makefile` — in the portable-engine `SOURCES` block, after `$(REPO)/engine/instrument.cpp`, add:

```make
	$(REPO)/engine/flow/terrain.cpp \
	$(REPO)/engine/flow/flow.cpp \
```

and extend the distributables line so Rack ships the new faceplate:

```make
DISTRIBUTABLES += res/factory.wav res/Fireflow.svg res/Glow.svg
```

- [ ] **Step 5: Build and verify**

```bash
./host/vcv/build-local.sh
```
Expected: a clean build producing `host/vcv/plugin.dll`. If it fails with "MinGW not found" you invoked `make`/`g++` directly instead of `build-local.sh` — the system `g++` is the Daisy ARM cross-compiler.

Then install and listen:
```bash
./host/vcv/build-local.sh install
```
Restart Rack, add **FireFlow Glow**, and confirm by ear and eye:
- the panel renders with all six knob captions, `NEW`, and eight jack captions;
- the logo reads `FireFlow GLOW` on one line;
- sound comes out of `OUT L`/`OUT R` within a few seconds of adding the module;
- turning `SPACE` up audibly opens the room; turning every knob fully counter-clockwise leaves a quiet, dark, sparse background (the calm corner);
- patching a 0–10 V LFO into `CV BRT` moves the brightness;
- patching a clock into `CLK` changes the pulse rate, and unpatching it returns to the terrain's own tempo within about two seconds.

Record what you heard in the commit message. Anything that sounds wrong here is a **finding to report**, not something to fix by editing `engine/flow/` — the tuning tables are Bastian's call.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/src/Glow.cpp host/vcv/src/plugin.hpp host/vcv/src/plugin.cpp \
        host/vcv/plugin.json host/vcv/Makefile
git commit -m "feat(glow): the module makes sound -- six macros, five CVs, a clock

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The NEW button — gestures and the LED

The one-button gesture family of spec §5, wired from the panel to `flow::Gesture` to `Flow`'s verbs, with the LED signatures rendering the state back.

**Files:**
- Modify: `host/vcv/src/Glow.cpp` (`controlTick`, plus two new members)
- Modify: `tests/test_glow_ui.cpp` (append)
- Modify: `host/vcv/src/glow_ui.hpp` (append `GestureBridge`)

**Interfaces:**
- Consumes: `spky::flow::Gesture`, `spky::flow::GestureOut`, `spkyvcv::KnobTracker`, `spkyvcv::led_level`.
- Produces in `namespace spkyvcv`: `struct GestureBridge { bool prevDown = false; bool edge(bool down); }`.

**Why a bridge type at all:** the press/release edge is the one piece of gesture wiring with a failure mode worth a test — a decoder that is handed `button(true, ...)` on every tick instead of only on the edge re-arms its hold timer forever, so undo and lock can never fire. Rack's `dsp::BooleanTrigger` only reports rising edges; the decoder needs both.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_glow_ui.cpp`:

```cpp
TEST_CASE("glow: the button bridge reports each edge exactly once") {
    GestureBridge b;
    CHECK_FALSE(b.edge(false));         // still up
    CHECK(b.edge(true));                // press
    CHECK_FALSE(b.edge(true));          // held: NOT an edge
    CHECK_FALSE(b.edge(true));
    CHECK(b.edge(false));               // release
    CHECK_FALSE(b.edge(false));
}

TEST_CASE("glow: a held button reaches the lock threshold through the bridge") {
    // The regression this guards: feeding button(true) every tick instead of
    // only on the edge restarts the hold timer forever, so the 5 s lock
    // gesture can never fire and the module has no way to be locked.
    Gesture g;
    GestureBridge b;
    bool locked = false;
    double t = 0.0;
    const double dt = 1.0 / 100.0;
    for (int i = 0; i < 800; ++i, t += dt) {   // eight seconds held down
        if (b.edge(true)) g.button(true, t, locked);
        g.tick(t, /*can_undo=*/false);
        const GestureOut op = g.poll();
        if (op.op == GestureOut::LOCK_TOGGLE) locked = !locked;
    }
    CHECK(locked);
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cmake --build build
```
Expected: FAIL — `'GestureBridge' was not declared in this scope`.

- [ ] **Step 3: Add `GestureBridge` to `glow_ui.hpp`**

Append inside `namespace spkyvcv`, after `KnobTracker`:

```cpp
// Press/release edges for the NEW button. flow::Gesture wants button(down)
// exactly once per transition: telling it "down" on every control tick would
// restart the hold timer forever, and undo and lock could never fire.
struct GestureBridge {
    bool prevDown = false;
    bool edge(bool down) {
        const bool changed = down != prevDown;
        prevDown = down;
        return changed;
    }
};
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Wire the gestures into the module**

In `host/vcv/src/Glow.cpp`, add next to the other members:

```cpp
    spkyvcv::GestureBridge newBtn;
```

and replace the body of `controlTick` between the CV loop and `flow.tick()` with:

```cpp
        // --- the NEW gesture family (spec 5) ----------------------------
        // The decoder's clock is Flow's own, which advances one dt per
        // tick(); reading it before the tick means every event this pass
        // carries the same timestamp, which is exactly what a control tick
        // is.
        const double t = flow.now_s();
        float d[spky::flow::MACRO_COUNT];
        if (knobs.deltas(k, d))
            for (int m = 0; m < spky::flow::MACRO_COUNT; ++m)
                if (d[m] > 0.f) gest.knob_delta(m, d[m], t);

        const bool down = params[NEW_BTN].getValue() > 0.5f;
        if (newBtn.edge(down)) gest.button(down, t, flow.locked());
        gest.tick(t, flow.can_undo());

        bool refused = false;
        const spky::flow::GestureOut op = gest.poll();
        switch (op.op) {
            case spky::flow::GestureOut::NEW_FULL:
                refused = !flow.new_full(); break;
            case spky::flow::GestureOut::NEW_PARTIAL:
                refused = !flow.new_partial(op.mask); break;
            case spky::flow::GestureOut::UNDO:
                refused = !flow.undo(); break;
            case spky::flow::GestureOut::LOCK_TOGGLE:
                flow.set_lock(!flow.locked()); break;
            case spky::flow::GestureOut::REFUSED:
                refused = true; break;
            default: break;
        }
        // A press the decoder let through but Flow turned down (nothing to
        // undo, an empty macro mask) must still light the refusal, or the
        // panel would silently swallow a gesture. gesture.h's own REFUSED
        // only covers the locked case, which is why Flow's verbs return bool.
        if (refused) gest.button(false, t, /*locked=*/true);
```

Then, after `flow.tick();` and the tempo block, add:

```cpp
        lights[NEW_L].setBrightness(
            spkyvcv::led_level(gest.led(flow.blend_phase(), flow.locked()),
                               flow.blend_phase(), flow.now_s()));
```

Also prime the tracker when the module wakes, so the first control tick after an add cannot mark a macro: in `wakeHouse()`, after `woken = true;`, add `knobs.primed = false;`.

- [ ] **Step 6: Build and verify on the panel**

```bash
./host/vcv/build-local.sh install
```
Restart Rack and check each row of spec §5's gesture table by hand:
- **tap** NEW (under ~0.4 s) → the sound moves to a different terrain over about six seconds, the LED breathing through it;
- **hold NEW and turn BRIGHT**, then release → the timbre changes but the tonality, roles and pace stay; the LED flickers while marked;
- **hold NEW ~2 s** without touching a knob, then release → the previous terrain comes back; the LED double-pulses while armed;
- **hold NEW past ~5 s** → the LED goes solid and stays solid; taps now produce a short refusal blink and nothing else; another ~5 s hold unlocks;
- **tap NEW twice quickly** → the second press retargets from wherever the first blend had reached, with no jump.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/src/Glow.cpp host/vcv/src/glow_ui.hpp tests/test_glow_ui.cpp
git commit -m "feat(glow): NEW gets its whole gesture family, and an LED that says so

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Persistence and the context menu

Spec §5's three kinds of power-on, and the VCV-only niceties: show a terrain code, enter one, toggle the lock.

**Files:**
- Modify: `host/vcv/src/Glow.cpp` (`dataToJson`, `dataFromJson`, `GlowWidget::appendContextMenu`)

**Interfaces:**
- Consumes: `spkyvcv::GlowSave`, `glow_capture`, `glow_restore` (Task 2, already tested); `spky::flow::encode_code`, `decode_code`, `kTerrainCodeLen`.

**The rule from spec §5:** *first insert* wakes on the house seed (Task 3's `onAdd` already does that). *Patch reload* restores the full saved state — terrain code, lock and undo slot. `Module::dataFromJson` can run before `onAdd`, so the wake-on-house in `onAdd` must not clobber a restore: guard it with the `woken` flag, which `glow_restore` sets.

- [ ] **Step 1: Add persistence to `Glow`**

```cpp
    // --- persistence (spec 5) --------------------------------------------
    // A live set built around a locked terrain has to survive a restart, so
    // a patch carries the whole state: the terrain code, the lock and the
    // one undo slot. Preset *systems* -- banks, slots, favourites -- are out
    // of scope (spec 8); this is the baseline.
    json_t* dataToJson() override {
        const spkyvcv::GlowSave s = spkyvcv::glow_capture(flow);
        json_t* root = json_object();
        json_object_set_new(root, "terrain", json_string(s.code));
        json_object_set_new(root, "lock", json_boolean(s.lock));
        if (s.have_undo) json_object_set_new(root, "undo", json_string(s.undo));
        return root;
    }

    void dataFromJson(json_t* root) override {
        if (!root) return;
        spkyvcv::GlowSave s;
        json_t* code = json_object_get(root, "terrain");
        if (!json_is_string(code)) return;              // nothing to restore
        std::snprintf(s.code, sizeof s.code, "%s", json_string_value(code));
        if (json_t* u = json_object_get(root, "undo")) {
            if (json_is_string(u)) {
                std::snprintf(s.undo, sizeof s.undo, "%s", json_string_value(u));
                s.have_undo = true;
            }
        }
        if (json_t* l = json_object_get(root, "lock"))
            s.lock = json_boolean_value(l);
        // A malformed code leaves everything alone -- a corrupt patch must
        // not silently move the player to some other instrument. Marking
        // `woken` keeps onAdd() (which can run AFTER this) from overwriting
        // the restored terrain with the house seed.
        if (spkyvcv::glow_restore(flow, s)) {
            woken = true;
            knobs.primed = false;
        }
    }

    // Enter a terrain code by hand (context menu). Returns false and changes
    // nothing if the string is not a valid code.
    bool setTerrainCode(const std::string& text) {
        spky::flow::TerrainState st;
        if (!spky::flow::decode_code(text.c_str(), st)) return false;
        flow.wake(st);
        woken = true;
        return true;
    }

    std::string terrainCode() {
        char buf[spky::flow::kTerrainCodeLen + 1] = {};
        spky::flow::encode_code(flow.state(), buf, int(sizeof buf));
        return std::string(buf);
    }
```

Add `#include <cstdio>` to the include block for `std::snprintf`.

Note the ordering hazard `dataFromJson` closes: `flow.init()` runs in `reinit()` from `onAdd`, but `dataFromJson` can arrive first, when `flow` has no `Instrument` pointer yet. `Flow::wake()` on an uninitialised object would push into a null `Instrument`. Guard it — in `dataFromJson`, wrap the restore in `if (curSr > 0.f)` and otherwise stash the payload:

```cpp
    spkyvcv::GlowSave pending;
    bool havePending = false;
```

and in `dataFromJson`, replace the final `if (spkyvcv::glow_restore(...))` block with:

```cpp
        if (curSr <= 0.f) {          // dataFromJson ran before onAdd
            pending = s;
            havePending = true;
            woken = true;            // don't let onAdd wake the house seed
            return;
        }
        if (spkyvcv::glow_restore(flow, s)) { woken = true; knobs.primed = false; }
```

and in `onAdd`, after `reinit(...)`, replace `if (!woken) wakeHouse();` with:

```cpp
        if (havePending) {
            havePending = false;
            if (!spkyvcv::glow_restore(flow, pending)) wakeHouse();
            knobs.primed = false;
        } else if (!woken) {
            wakeHouse();
        }
```

- [ ] **Step 2: Add the context menu**

In `GlowWidget`:

```cpp
    void appendContextMenu(Menu* menu) override {
        auto* m = getModule<Glow>();
        if (!m) return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Terrain " + m->terrainCode()));

        // Share a terrain: the code is the whole state (spec 4), so copying
        // it out and pasting it in is the entire sharing story.
        menu->addChild(createMenuItem("Copy terrain code", "", [m]() {
            glfwSetClipboardString(APP->window->win, m->terrainCode().c_str());
        }));
        menu->addChild(createMenuItem("Paste terrain code", "", [m]() {
            const char* s = glfwGetClipboardString(APP->window->win);
            if (s) m->setTerrainCode(s);
        }));

        auto* field = new ui::TextField;
        field->box.size.x = 180.f;
        field->placeholder = "F1-XXXXXXXX-000000000000";
        field->setText(m->terrainCode());
        menu->addChild(field);

        menu->addChild(createBoolMenuItem(
            "Terrain lock", "",
            [m]() { return m->flow.locked(); },
            [m](bool on) { m->flow.set_lock(on); }));
    }
```

The `ui::TextField` needs to apply on Enter, which the base class does not do. Add this small subclass above `GlowWidget`:

```cpp
// A menu text field that hands its contents to the module on Enter and
// closes the menu. ui::TextField itself has no commit behaviour.
struct TerrainCodeField : ui::TextField {
    Glow* module = nullptr;
    void onSelectKey(const SelectKeyEvent& e) override {
        if (module && e.action == GLFW_PRESS
            && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
            module->setTerrainCode(text);
            e.consume(this);
            if (MenuOverlay* overlay = getAncestorOfType<MenuOverlay>())
                overlay->requestDelete();
            return;
        }
        ui::TextField::onSelectKey(e);
    }
};
```

and construct `TerrainCodeField` instead of `ui::TextField`, setting `field->module = m;`.

- [ ] **Step 3: Build and verify persistence by hand**

```bash
./host/vcv/build-local.sh install
```
Restart Rack, then check each spec §5 case:
- add a fresh Glow → it wakes on the house terrain; the context menu shows `F1-00000020-000000000000`;
- tap NEW a few times, hold 5 s to lock, save the patch, quit Rack, reopen it → the same terrain code is showing, the LED is solid (still locked), and one undo press returns to the terrain from before the save;
- right-click → **Copy terrain code**, add a second Glow, right-click → **Paste terrain code** → both modules are on the same terrain;
- type a nonsense string into the menu field and press Enter → nothing changes;
- **Initialize** (Ctrl+I) on the module → it returns to the house terrain, unlocked.

- [ ] **Step 4: Commit**

```bash
git add host/vcv/src/Glow.cpp
git commit -m "feat(glow): a patch remembers its terrain, its lock and its undo slot

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: Documentation

The module exists; the repo's maps should say so.

**Files:**
- Modify: `host/vcv/README.md`
- Modify: `docs/roadmap.md`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Document the module in the VCV README**

Add a section to `host/vcv/README.md` describing FireFlow Glow. It must cover, in prose that matches the file's existing voice:
- what the module is (12 HP, six macros and a NEW button over a generated terrain) and how it relates to the big module (same engine, different view);
- the six macro meanings, one line each, copied from spec §3's table: MOTION (how much everything moves), DENSITY (how much happens), BRIGHT (spectral centre), DIRT (clean ↔ driven), WANDER (predictable ↔ wandering), SPACE (close ↔ vast);
- the calm corner — all six knobs fully counter-clockwise is a defined quiet background on every terrain, and it is the module's gas pedal;
- the full gesture table from spec §5 (tap / hold + turn / hold 1.5 s / hold 5 s) with its LED signatures;
- the jack block: five CV inputs at 0–10 V additive onto the knobs, `CLK` at one pulse per beat overriding the terrain's tempo and falling back after about two seconds, stereo out. State plainly that WANDER has no CV jack and there is no RST jack, both by design (spec §6);
- terrain codes: what they look like, that they are the whole state, and that the context menu copies, pastes and shows them;
- that `res/Glow.svg` and `src/generated_flow_panel.hpp` are generated by `res/gen_flow_panel.py` and guarded by `res/test_flow_panel.py`, and must never be hand-edited;
- that the house seed in `engine/flow/taste.h` is a measured placeholder awaiting a by-ear choice on the finished module.

- [ ] **Step 2: Update the roadmap**

In `docs/roadmap.md`, record that Plan B (FireFlow Glow) is built, and that the eight open items in `docs/superpowers/specs/2026-08-05-flow-listening-notes.md` are now reachable — the listening file was blocked on having an instrument to play, which is what this module is.

- [ ] **Step 3: Update the CLAUDE.md map**

In the "Where things are" table, extend the VCV row so it names both modules, and add a line under "Building" noting that Glow's panel comes from `host/vcv/res/gen_flow_panel.py` (run from `host/vcv/`, guarded by `res/test_flow_panel.py`) exactly as the big panel comes from `gen_panel.py`.

- [ ] **Step 4: Full verification before the final commit**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cd host/vcv && python res/test_panel.py && python res/test_flow_panel.py && cd ../..
./host/vcv/build-local.sh
git status --porcelain
```
Expected: ctest green, both panel guards green, the plugin builds, and the only untracked/modified files are the ones this task touches.

- [ ] **Step 5: Commit**

```bash
git add host/vcv/README.md docs/roadmap.md CLAUDE.md
git commit -m "docs(glow): the repo's maps learn about the second module

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Self-review against the spec

**Spec coverage.** §2 (Glow embeds the same `Instrument`, talks only to `flow.h`) — Task 3. §3 macro semantics and CV — Tasks 1 (labels/jacks) and 3 (wiring); the story curves and calm corner are Plan A's, already built. §4 clock convention and terrain codes — Task 3 (clock, with the timeout fallback) and Task 5 (codes). §5 house seed — Task 2's constant, Task 3's `wakeHouse`; three kinds of power-on — Task 3 (`onAdd`) and Task 5 (`dataFromJson`); the tap/hold/mark/lock family and its LEDs — Task 4; the VCV context menu — Task 5. §6 panel — Task 1, at true hardware dimensions, no RST, no CV over WANDER, `ton-k` off the panel. §7 — Plan A's suite already covers the flow layer; this plan adds the headless module-logic tests of Tasks 2 and 4, and the panel guards of Task 1.

**Two gaps, stated rather than hidden.** (a) The house seed is a measured placeholder, not the curated terrain §5 asks for — the listening pass that was to choose it was stopped, and re-choosing it needs the very module this plan builds. Task 2 says so in the code comment, Task 6 in the README. (b) Everything about how the module *feels* — whether a knob is alive under the hand, whether NEW lands somewhere worth being — is verified by hand in Tasks 3, 4 and 5, not by an automated test. That is the whole reason the render-based pass was stopped; the hand checks are written out step by step so they are not skipped, but they are hand checks.

**One thing this plan deliberately does not do.** It does not touch `host/vcv/src/Fireflow.cpp`, `res/gen_panel.py`, `res/Fireflow.svg` or `src/generated_panel.hpp`. The big module is the full-control view and stays exactly as it is (spec §1). If a change there looks necessary, stop and ask — it is almost certainly a sign that something belongs in `glow_ui.hpp` instead.
