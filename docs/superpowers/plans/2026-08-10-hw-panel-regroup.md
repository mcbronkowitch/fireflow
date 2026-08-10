# HW-Panel Regroup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Das 60-HP-Hardware-Panel wird neu gruppiert — Knopfgrößen nach Rang statt nach VCV-Widget-Typ, Gruppen nach Engine-Struktur, acht CV-Eingänge unter ihren Zielen, plus MOD/SHIFT-Taster, 20 LEDs und der SD-Ausschnitt.

**Architecture:** Alles passiert in einem Python-Generator (`host/vcv/res/gen_hw_panel.py`), der zwei Artefakte erzeugt: `res/FireflowHW.svg` (geht später aufs Blech) und `src/generated_hw_panel.hpp` (liest das VCV-Rehearsal-Modul). Ein zweites Python-Skript (`res/test_hw_panel.py`) ist der Vertrag und läuft als ctest-Target `hw_panel_guard`. Der einzige C++-Eingriff ist im HW-Widget in `host/vcv/src/Fireflow.cpp`, das die Knopfgröße bisher aus `c.kind` ableitet und das künftig aus einer neuen Tabelle liest.

**Tech Stack:** Python 3 (stdlib only, kein pytest — plain `assert` + `check()`-Sammler), C++17 gegen die VCV-Rack-SDK, CMake/ctest für die Gates, clang+Ninja für den Engine-Build.

## Global Constraints

- **Spec:** `docs/superpowers/specs/2026-08-10-hw-panel-regroup-design.md`. Bei Widerspruch zwischen Plan und Spec gewinnt die Spec — außer an den zwei Stellen, die Task 7 ausdrücklich korrigiert.
- **Branch:** `hw-panel-regroup` ist angelegt, die Spec ist darauf committet. Nicht auf `main` arbeiten.
- **Commit-Trailer:** jeder Commit endet mit `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>` — nicht der Anthropic-Default.
- **Panelrahmen:** 60 HP = 304,8 mm × 128,5 mm. `KEEP_TOP = 9.0`, `KEEP_BOT = 119.5`, seitlich 2,0 mm. Unverändert.
- **Spiegelachse:** `x_B = 304.8 - x_A`, y identisch. `test_mirror_symmetry` ist unantastbar.
- **Sperrflächenradien (mm):** groß 8,0 · klein 5,5 · Taster 4,0 · Buchse 4,0 · LED 1,5 · SD-Ausschnitt Rechteck 15,0 × 12,0.
- **Beschriftungsmarge:** ein Beschriftungsanker muss **1,5 mm über** dem Sperrflächenradius jedes fremden Bedienelements liegen. Bloße Berührungsfreiheit reicht nicht.
- **Testdisziplin:** jede neue oder verschärfte Prüfung wird **einmal rot gesehen**, bevor sie als Gate zählt (Memory `spotykach-tests-must-be-able-to-fail`). Diese Datei hat neun Aufgaben lang ohne Runner bestanden (Memory `fireflow-vacuous-test-gates`) — Behauptungen über bestandene Gates ohne gezeigte Ausgabe sind wertlos.
- **Tests laufen aus `host/vcv/`:** `python res/test_hw_panel.py`. Aus dem Repo-Root schlägt es fehl, weil das Skript seine Pfade relativ auflöst. Als ctest-Target heißt es `hw_panel_guard`.
- **Engine-Build (nur für Task 7 nötig):** `source env.sh`, dann `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. `Release` ist **nicht optional** — Debug lässt `spky_tests` und `ctrl_identity` fehlschlagen.
- **VCV-Build (nur Task 6):** immer `host/vcv/build-local.sh`, nie `g++` von Hand. Das System-`g++` auf dieser Maschine ist der ARM-Cross-Compiler und scheitert mit „MinGW not found".
- **Nicht anfassen:** `gen_panel.py`, `gen_flow_panel.py`, `engine/`, `shell/`, `bench/`. Diese Runde betrifft ausschließlich das HW-Panel.

## File Structure

| Datei | Verantwortung | Task |
|---|---|---|
| `host/vcv/res/gen_hw_panel.py` | **modifiziert** — Größenklassen, Geometrie, HW-only-Inventar, Beschriftungsregel, SVG- und Header-Ausgabe | 1–5 |
| `host/vcv/res/test_hw_panel.py` | **modifiziert** — Vertrag; vier Prüfungen verschärft, vier neu | 1–5 |
| `host/vcv/res/FireflowHW.svg` | **generiert** — nie von Hand editieren | 1–5 |
| `host/vcv/src/generated_hw_panel.hpp` | **generiert** — nie von Hand editieren | 1–5 |
| `host/vcv/src/Fireflow.cpp` | **modifiziert** — `FireflowHWWidget` wählt Knopfgröße aus der neuen Tabelle statt aus `c.kind`; `HwPanelText` zeichnet die HW-only-Beschriftungen | 6 |
| `docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md` | **modifiziert** — Zählbasis §1, Raster §4 | 7 |
| `docs/superpowers/specs/2026-08-10-hw-panel-regroup-design.md` | **modifiziert** — zwei Korrekturen (§6 Beschriftungsregel, §6 `kHwCutouts`) | 7 |
| `docs/hardware/io-budget.md` | **modifiziert** — Buchsentabelle §5 | 7 |
| `docs/roadmap.md` | **modifiziert** — Neugruppierung erledigt | 7 |

---

## Warum die Reihenfolge so ist

Größe und Geometrie können nicht getrennt landen: sobald FILT von 5,5 auf 8,0 wächst, kollidiert es auf dem alten 15-mm-Raster, und `test_no_overlap_with_hw_radii` geht rot. Deshalb baut **Task 1 nur den Mechanismus** (Größe kommt aus einer eigenen Tabelle statt aus `c.kind`) und lässt die Zuordnung bei den heutigen Werten. Der Byte-Vergleich `test_committed_files_match_the_generator` beweist dann, dass Task 1 nichts verändert hat — ein Umbau, dessen Unschädlichkeit die Maschine bezeugt. **Task 2** dreht dann Zuordnung und Koordinaten in einem Zug.

---

### Task 1: Größenklasse als eigene Tabelle (reiner Umbau, keine sichtbare Änderung)

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py:22-28` (`HW_R`, `LBL_DY_HW`), `:78-99` (`place`), `:131-136` (`hw_label`)
- Modify: `host/vcv/res/test_hw_panel.py:43-51` (`test_hardware_footprints`)

**Interfaces:**
- Produces: `HW_SIZE: dict[str, str]` — Parameter-**Basisname** (ohne `_A`/`_B`) auf `"G"` (groß), `"S"` (klein) oder `"P"` (Taster). Task 2 dreht die Werte, Task 6 liest die Klasse über `hw_class(enum)`.
- Produces: `hw_class(enum: str) -> str` — nimmt einen vollen Enum-Namen (`"FILT_A"`), gibt `"G"`/`"S"`/`"P"`/`"J"`/`"L"` zurück. Buchsen und LEDs werden aus `gp.IN`/`gp.OUT`/`gp.LIGHT` abgeleitet, nicht aus `HW_SIZE`.
- Produces: `CLASS_R: dict[str, float]` — `{"G": 8.0, "S": 5.5, "P": 4.0, "J": 4.0, "L": 1.5}`.

- [ ] **Step 1: Die neue Prüfung schreiben, die es heute nicht gibt**

In `host/vcv/res/test_hw_panel.py` unmittelbar vor `def main():` einfügen:

```python
def test_every_control_has_a_size_class():
    """Die Größe eines Bedienelements ist eine Hardware-Aussage und steht in
    HW_SIZE -- nicht in c.kind. KNOBC heißt bipolar, KNOBI heißt gerastert;
    beides sagt nichts über einen Durchmesser (spec 2026-08-10 §1)."""
    for c in gp.RUNTIME_PANEL_PARAMS:
        base = c.enum[:-2] if c.enum.endswith(("_A", "_B")) else c.enum
        check(base in hw.HW_SIZE, f"no hardware size class for {base}")
    for c in hw.ALL_HW:
        assert hw.hw_class(c.enum) in ("G", "S", "P", "J", "L"), c.enum
        assert abs(c.r - hw.CLASS_R[hw.hw_class(c.enum)]) < 1e-9, (c.enum, c.r)
```

- [ ] **Step 2: Rot laufen lassen**

Run (aus `host/vcv/`): `python res/test_hw_panel.py`
Expected: `AttributeError: module 'gen_hw_panel' has no attribute 'HW_SIZE'` — der Traceback ist hier das gewünschte Rot; `HW_SIZE`, `hw_class` und `CLASS_R` existieren noch nicht.

- [ ] **Step 3: Die Tabelle und den Zugriff bauen**

In `host/vcv/res/gen_hw_panel.py` die Blöcke `HW_R` und `LBL_DY_HW` (Zeilen 22–28) **ersetzen** durch:

```python
# Hardware size class per parameter BASE name (spec 2026-08-10 §1). This is
# deliberately NOT derived from c.kind: KNOBC means bipolar and KNOBI means
# detented -- statements about the screen widget's behaviour, not about a
# diameter. A centre-detent pot ships in every size.
# Task 1 reproduces today's kind-derived assignment exactly, so the byte
# compare in test_committed_files_match_the_generator proves the refactor is
# inert. Task 2 is what actually changes it.
HW_SIZE = {
    # --- deck ---
    "RATE": "G", "SHAPE": "G", "DENSITY": "G", "SMOOTH": "G", "RANGE": "G",
    "MELODY": "G", "MOD": "G", "TUNE": "G", "COLOR": "G",
    "ATTACK": "S", "DECAY": "S", "RES": "S", "SUB": "S", "SOURCE": "S",
    "FLUX": "S", "GRIT": "G", "COMP": "S", "STEPS": "S", "DETUNE": "S",
    "SONG": "S", "FILT": "S", "FLUXRATE": "S", "FLUXFB": "S", "LINK": "S",
    "STAGES": "S", "REV_MIX": "S",
    "ENGINE": "P", "REC": "P",
    # --- centre ---
    "MORPH": "G", "TEMPO": "S", "COUPLE": "S", "SCALE": "S", "DRIFT": "S",
    "REV_SIZE": "S", "REV_DECAY": "S", "REV_TONE": "S", "REV_DIFF": "S",
    "CHOKE": "S", "TIDE": "S", "SHUFFLE": "S",
}

CLASS_R = {"G": 8.0, "S": 5.5, "P": 4.0, "J": 4.0, "L": 1.5}

# Baseline offset from the glyph centre to the caption, per class. Jacks read
# ABOVE their glyph (negative), everything else below.
CLASS_LBL_DY = {"G": 10.4, "S": 7.9, "P": 6.5, "J": -6.0, "L": 0.0}


def hw_class(enum):
    """Size class for a full enum name. Jacks and LEDs come from the shared
    inventory's kind (they have no hardware choice to make); everything a
    finger turns or presses comes from HW_SIZE."""
    if enum in _JACK_ENUMS:
        return "J"
    if enum in _LIGHT_ENUMS:
        return "L"
    base = enum[:-2] if enum.endswith(("_A", "_B")) else enum
    return HW_SIZE[base]


_JACK_ENUMS = {c.enum for c in gp.INPUTS} | {c.enum for c in gp.OUTPUTS}
_LIGHT_ENUMS = {c.enum for c in gp.LIGHTS}
```

Hinweis: `_JACK_ENUMS` und `_LIGHT_ENUMS` müssen **vor** dem ersten `hw_class`-Aufruf definiert sein, aber Python bindet Modul-Globals erst beim Aufruf — die gezeigte Reihenfolge (Funktion vor den Mengen) ist korrekt, solange `hw_class` nicht auf Modulebene beim Import aufgerufen wird. `place()` läuft in Zeile 101 und damit nach beiden.

- [ ] **Step 4: `place` und `hw_label` auf die Klasse umstellen**

In `place()` (Zeile 81) `n.r = HW_R[c.kind]` ersetzen durch:

```python
    n.r = CLASS_R[hw_class(c.enum)]
```

In `hw_label()` (Zeile 136) die letzte Zeile ersetzen durch:

```python
    return (c.x, c.y + CLASS_LBL_DY[hw_class(c.enum)], "middle", 2.2, gp.INK)
```

- [ ] **Step 5: `test_hardware_footprints` auf Klassen umstellen**

In `host/vcv/res/test_hw_panel.py` die Funktion `test_hardware_footprints` (Zeilen 43–51) **ersetzen** durch:

```python
def test_hardware_footprints():
    # Screen-widget radii are meaningless on sheet metal, and so is the
    # screen widget's KIND: it says bipolar/detented, not big/small. The
    # minimum clearance radius comes from the hardware size class
    # (spec 2026-08-10 §1).
    for c in hw.ALL_HW:
        want = hw.CLASS_R[hw.hw_class(c.enum)]
        assert c.r >= want - 1e-9, (c.enum, c.r, want)
```

- [ ] **Step 6: Generator laufen lassen und grün prüfen**

Run (aus `host/vcv/`):
```
python res/gen_hw_panel.py
python res/test_hw_panel.py
```
Expected: `wrote res/FireflowHW.svg and src/generated_hw_panel.hpp`, danach `PASS -- hw panel guards ok`.

**Entscheidend:** `git diff --stat res/FireflowHW.svg src/generated_hw_panel.hpp` muss **leer** sein. Das ist der Beweis, dass Task 1 nichts verändert hat. Ist er nicht leer, stimmt eine Zeile in `HW_SIZE` nicht mit der alten `kind`-Ableitung überein — vergleiche gegen `BIGKNOB`/`KNOBC` = `"G"`, `SMKNOB`/`KNOBI` = `"S"`, `LATCH` = `"P"`.

- [ ] **Step 7: Rot beweisen, dass der Byte-Vergleich greift**

Ändere probeweise `"RATE": "G"` zu `"RATE": "S"`, dann:
```
python res/gen_hw_panel.py && python res/test_hw_panel.py
```
Expected: FAIL mit `res/FireflowHW.svg differs from the generator's output` **und** einer Overlap-Meldung (RATE mit r=5,5 hat keine Kollision, aber die SVG-Bytes ändern sich). Danach zurückändern, Generator erneut laufen lassen, `git diff` wieder leer.

- [ ] **Step 8: Commit**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py
git commit -F - <<'EOF'
refactor(hw): knob size stops being a guess about the screen widget

HW_R was indexed by c.kind, so a bipolar knob was big because KNOBC happened
to sit next to BIGKNOB in the table. KNOBC means bipolar and KNOBI means
detented; neither is a diameter, and a centre-detent pot ships in every size.
Each control now names its hardware class in HW_SIZE.

The assignment is unchanged, and the byte compare against a fresh generator
run proves it: the SVG and the header do not move.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 2: Neue Geometrie und neue Größenvergabe

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py` — `HW_SIZE` (Werte), `DECK_POS`, `CENTER_POS`, `JACK_POS`, `LIGHT_POS`, `TEXTS`
- Modify: `host/vcv/res/test_hw_panel.py` — neue Prüfung `test_size_classes_match_the_spec`

**Interfaces:**
- Consumes: `HW_SIZE`, `CLASS_R`, `hw_class` aus Task 1.
- Produces: `DECK_POS`, `CENTER_POS` mit den Koordinaten aus Spec §3. Task 4 hängt CV-Buchsen an `X_FILT`/`X_TIMB`/`X_COLOR`/`X_LVL`, die hier als Modul-Konstanten entstehen.

- [ ] **Step 1: Die Prüfung schreiben, die die Größenvergabe festnagelt**

In `test_hw_panel.py` vor `def main():` einfügen:

```python
def test_size_classes_match_the_spec():
    """Die 18 großen sind namentlich beschlossen (spec 2026-08-10 §1).
    Ohne diese Prüfung wandert die Vergabe beim nächsten Umbau lautlos."""
    BIG = {"DENSITY", "MOD", "COLOR", "FILT", "SOURCE", "FLUX", "REV_MIX",
           "COMP", "MORPH", "REV_DECAY"}
    got = {b for b, cls in hw.HW_SIZE.items() if cls == "G"}
    check(got == BIG, f"big-knob set drifted: extra={got-BIG} missing={BIG-got}")
    big_positions = [c for c in hw.HW_PARAMS if hw.hw_class(c.enum) == "G"]
    check(len(big_positions) == 18, f"expected 18 big positions, got {len(big_positions)}")
    small = [c for c in hw.HW_PARAMS if hw.hw_class(c.enum) == "S"]
    check(len(small) == 46, f"expected 46 small params, got {len(small)}")
```

- [ ] **Step 2: Rot laufen lassen**

Run: `python res/test_hw_panel.py`
Expected: FAIL mit `big-knob set drifted: extra={'RATE', 'SHAPE', 'SMOOTH', 'RANGE', 'MELODY', 'TUNE', 'GRIT'} missing={'FILT', 'SOURCE', 'FLUX', 'REV_MIX', 'COMP', 'REV_DECAY'}` und `expected 18 big positions, got 21`.

- [ ] **Step 3: `HW_SIZE` auf die beschlossene Vergabe drehen**

Die sieben, die klein werden, und die sechs, die groß werden — in `gen_hw_panel.py` die betroffenen Einträge ändern, sodass `HW_SIZE` so aussieht:

```python
HW_SIZE = {
    # --- deck: one head per group (spec 2026-08-10 §1/§2) ---
    "MOD": "G", "DENSITY": "G",                       # MOD group
    "RATE": "S", "SHAPE": "S", "SMOOTH": "S", "RANGE": "S", "MELODY": "S",
    "COLOR": "G",                                     # PITCH group
    "TUNE": "S", "DETUNE": "S",
    "FILT": "G", "SOURCE": "G",                       # VOICE group
    "ATTACK": "S", "DECAY": "S", "RES": "S", "SUB": "S", "STAGES": "S",
    "FLUX": "G",                                      # FLUX group
    "FLUXRATE": "S", "FLUXFB": "S", "LINK": "S",
    "REV_MIX": "G",                                   # ROOM send
    "COMP": "G", "GRIT": "S",                         # OUT group
    "STEPS": "S", "SONG": "S",                        # PLAY group
    "ENGINE": "P", "REC": "P",
    # --- centre ---
    "MORPH": "G", "TIDE": "S", "CHOKE": "S",          # BLEND
    "TEMPO": "S", "COUPLE": "S", "SHUFFLE": "S",      # CLOCK
    "SCALE": "S", "DRIFT": "S",                       # TONALITY
    "REV_DECAY": "G", "REV_SIZE": "S", "REV_TONE": "S", "REV_DIFF": "S",
}
```

- [ ] **Step 4: Zeilenrhythmus und Koordinaten setzen**

In `gen_hw_panel.py` den gesamten Block von `# --- iteration-0 slot map` bis einschließlich `LIGHT_POS = {...}` (Zeilen 30–76) **ersetzen** durch:

```python
# --- slot map (spec 2026-08-10 §3) ------------------------------------------
# Row rhythm, top to bottom: a status/transport strip, two two-row bands, the
# jack row. The four CV targets sit in the LOWEST knob row so their jacks are
# adjacent, not merely aligned (spec §4).
Y_TOP = 15.0                      # transport + the whole LED field
Y_B1K, Y_B1G = 30.0, 46.0         # band 1: MOD, FLUX, ROOM send
Y_B2K, Y_B2G = 66.0, 86.0         # band 2: VOICE, PITCH, OUT -- the CV targets
JACK_Y = 107.0
SD_X, SD_Y, SD_W, SD_H = 152.4, 110.0, 15.0, 12.0

# The four CV columns. A jack and its target share this x exactly; the guard
# test_cv_sits_under_its_target holds them together.
X_FILT, X_TIMB, X_COLOR, X_LVL = 23.0, 42.5, 76.25, 100.0

DECK_POS = {
    # status + transport strip
    "STEPS":  (14.0, Y_TOP), "SONG":   (26.5, Y_TOP),
    "ENGINE": (38.5, Y_TOP), "REC":    (69.5, Y_TOP),
    # band 1 -- MOD is ONE engine object (_parts[p].mod()); MOD is its depth
    "RATE":   (14.0, Y_B1K), "SHAPE":  (26.5, Y_B1K), "SMOOTH": (39.0, Y_B1K),
    "RANGE":  (51.5, Y_B1K), "MELODY": (64.0, Y_B1K),
    "MOD":    (26.5, Y_B1G), "DENSITY": (51.5, Y_B1G),
    # band 1 -- FLUX, then the deck's way into the shared room
    "FLUXRATE": (76.0, Y_B1K), "FLUXFB": (88.5, Y_B1K), "LINK": (101.0, Y_B1K),
    "FLUX":     (88.5, Y_B1G), "REV_MIX": (110.0, Y_B1G),
    # band 2 -- the CV targets and their satellites
    "ATTACK": (14.0, Y_B2K), "DECAY": (26.5, Y_B2K),
    "RES":    (39.0, Y_B2K), "SUB":   (51.5, Y_B2K),
    "FILT":   (X_FILT, Y_B2G), "SOURCE": (X_TIMB, Y_B2G),
    "TUNE":   (70.0, Y_B2K), "DETUNE": (82.5, Y_B2K),
    "COLOR":  (X_COLOR, Y_B2G),
    "GRIT":   (100.0, Y_B2K), "COMP": (X_LVL, Y_B2G),
    # deliberate dual assignment: BEND rides ATTACK's knob (spec §6)
    "STAGES": (14.0, Y_B2K),
}

CENTER_POS = {
    "TEMPO":  (127.4, Y_B1K), "COUPLE": (139.9, Y_B1K),
    "SHUFFLE": (152.4, Y_B1K), "SCALE": (164.9, Y_B1K),
    "DRIFT":  (177.4, Y_B1K),
    "TIDE":   (140.4, Y_B1G), "CHOKE":  (164.4, Y_B1G),
    "MORPH":  (152.4, 62.0),
    "REV_DECAY": (152.4, 79.0),
    "REV_SIZE": (134.4, 94.0), "REV_TONE": (152.4, 94.0),
    "REV_DIFF": (170.4, 94.0),
}

JACK_POS = {"PITCH_A": 54.0, "GATE_A": 65.0,
            "IN_L": 112.0, "IN_R": 126.0, "CLOCK": 140.0,
            "RESET": 164.8, "OUT_L": 178.8, "OUT_R": 192.8,
            "GATE_B": W - 65.0, "PITCH_B": W - 54.0}

# The two LEDs the shared inventory already knows about sit at the right end
# of each deck's status strip; the other 14 arrive with HW_ONLY in task 4.
LIGHT_POS = {"REC_A_L": (77.0, Y_TOP), "GATE_A_L": (81.0, Y_TOP),
             "REC_B_L": (W - 77.0, Y_TOP), "GATE_B_L": (W - 81.0, Y_TOP)}
```

- [ ] **Step 5: Grün prüfen**

Run (aus `host/vcv/`):
```
python res/gen_hw_panel.py
python res/test_hw_panel.py
```
Expected: `params=68 inputs=4 outputs=6 lights=4  panel=60HP`, danach `PASS -- hw panel guards ok`.

Falls `test_no_overlap_with_hw_radii` meldet: die Koordinaten oben sind vollständig durchgerechnet und kollisionsfrei — eine Meldung heißt, ein Wert wurde beim Abtippen verändert. Gegen die Tabelle in Spec §3 vergleichen, nicht nachjustieren.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py \
        host/vcv/res/FireflowHW.svg host/vcv/src/generated_hw_panel.hpp
git commit -F - <<'EOF'
feat(hw): the panel is regrouped around what the engine actually is

RATE, SHAPE, DENS, SMTH, RANGE, VARY and MOD are one object -- _parts[p].mod(),
with MOD as its depth -- so they become one group instead of three orbit
sectors. COLOR joins TUNE, because chord.h:48 sweeps it from a single note to
a four-note diatonic chord; it was never a timbre control.

Eighteen knobs are big, one head per group, and the four that the engine's
own lanes already drive -- FILT, TIMB, COLOR, LVL -- move to the lowest knob
row so their jacks can sit under them in the next task. The four deliberate
holes of iteration 0 are gone.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 3: Beschriftungen weichen aus, statt sich zu verkürzen

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py:109-136` (`STAGES_LBL_*`, `FLUXFB_LBL_Y_OFFSET`, `hw_label`)
- Modify: `host/vcv/res/test_hw_panel.py:117-128` (`test_labels_stay_off_neighbour_footprints`)

**Interfaces:**
- Consumes: `CLASS_LBL_DY`, `hw_class`, `ALL_HW` aus Task 1/2.
- Produces: `LBL_MARGIN = 1.5` und ein `hw_label`, das drei Kandidatenpositionen in fester Reihenfolge probiert. Task 5 prüft gegen dasselbe `LBL_MARGIN`.

**Warum keine Verkürzung:** Spec §6 Punkt 4 sah vor, den Abstand auf 6,0 mm zu kürzen, wenn ein kleiner Knopf über einem großen sitzt. Das trägt nicht. Bei MORPH (152,4 / 62) und DECAY (152,4 / 79) sind beide groß und 17 mm auseinander; ein auf 7,0 mm gekürzter Abstand legt DECAYs Beschriftung bei y = 86 **auf den eigenen Knopf** (dessen Sperrfläche reicht bis 87). Eine Verkürzung kann das Problem nicht lösen, weil der eigene Radius die Untergrenze ist. Ausweichen kann es: DECAY beschriftet nach rechts, MORPH nach oben. Task 7 korrigiert die Spec.

- [ ] **Step 1: Die Marge in die bestehende Prüfung ziehen — das ist das Rot**

`test_labels_stay_off_neighbour_footprints` (Zeilen 117–128) **ersetzen** durch:

```python
LBL_MARGIN = 1.5


def test_labels_stay_off_neighbour_footprints():
    # A caption may sit near its own control, but its anchor must clear every
    # OTHER control's footprint by LBL_MARGIN. Bare non-overlap is not a
    # margin: at 16 mm row spacing the default offset lands 8.1 mm from an
    # 8.0 mm knob and would pass a zero-margin test with 0.1 mm to spare.
    for c in hw.HW_PARAMS + hw.HW_INPUTS + hw.HW_OUTPUTS:
        if not c.label:
            continue
        lx, ly = hw.hw_label(c)[0], hw.hw_label(c)[1]
        for other in hw.ALL_HW:
            if other is c:
                continue
            d = ((lx - other.x) ** 2 + (ly - other.y) ** 2) ** 0.5
            check(d >= other.r + LBL_MARGIN - 1e-6,
                  f"caption {c.enum} at ({lx:.1f},{ly:.1f}) is {d:.2f} from "
                  f"{other.enum} (needs {other.r + LBL_MARGIN:.2f})")


def test_captions_stay_off_their_own_knob():
    # The reason a shortened offset cannot be the answer: a control's own
    # radius is the floor. A caption inside its own footprint is printed ON
    # the knob (spec 2026-08-10 §6, corrected).
    for c in hw.HW_PARAMS:
        if not c.label:
            continue
        lx, ly = hw.hw_label(c)[0], hw.hw_label(c)[1]
        d = ((lx - c.x) ** 2 + (ly - c.y) ** 2) ** 0.5
        check(d >= c.r - 1e-6,
              f"caption {c.enum} at ({lx:.1f},{ly:.1f}) sits on its own knob")
```

- [ ] **Step 2: Rot laufen lassen**

Run: `python res/test_hw_panel.py`
Expected: FAIL mit mehreren Zeilen, darunter
`caption SHAPE_A at (26.5,37.9) is 8.10 from MOD_A (needs 9.50)` und
`caption MORPH at (152.4,72.4) is 6.60 from REV_DECAY (needs 9.50)`.
Notiere die vollständige Liste — sie ist die Abnahmeliste für Step 4.

- [ ] **Step 3: Die Ausweichregel bauen**

In `gen_hw_panel.py` den Kommentarblock samt `STAGES_LBL_Y_OFFSET`, `STAGES_LBL_SIZE`, `FLUXFB_LBL_Y_OFFSET` und `hw_label` (Zeilen 109–136) **ersetzen** durch:

```python
# BEND shares ATTACK's knob (deliberate dual assignment, spec §6). A screen
# widget can gate one caption away by engine state; an aluminium plate can't
# -- it prints both words. The plate reads BEND above the shared knob, ATK
# below it, slightly smaller so the pair reads as a stack, not a collision.
STAGES_LBL_Y_OFFSET = -7.0
STAGES_LBL_SIZE = 1.8

LBL_MARGIN = 1.5   # a caption anchor clears every FOREIGN footprint by this


def _caption_is_clear(c, lx, ly):
    """True when (lx, ly) sits outside c's own footprint and clears every
    other control's clearance circle by LBL_MARGIN."""
    if ((lx - c.x) ** 2 + (ly - c.y) ** 2) ** 0.5 < c.r - 1e-9:
        return False
    for o in ALL_HW:
        if o is c:
            continue
        if ((lx - o.x) ** 2 + (ly - o.y) ** 2) ** 0.5 < o.r + LBL_MARGIN - 1e-9:
            return False
    return True


def hw_label(c):
    """Caption placement, by rule rather than by named exception.

    The old code carried FLUXFB_LBL_Y_OFFSET because GRIT went bipolar and
    inherited KNOBC's larger clearance. That cause is gone with the size
    classes, the pattern is not: at 16 mm row spacing a small knob's default
    caption lands 8.1 mm from the 8.0 mm knob below it. Shortening the offset
    cannot fix the general case -- for two big knobs 17 mm apart, any offset
    short enough to clear the neighbour is inside the control's OWN footprint.
    So the caption steps aside instead: below, then above, then to the right.
    """
    if c.enum.startswith("STAGES_"):
        return (c.x, c.y + STAGES_LBL_Y_OFFSET, "middle", STAGES_LBL_SIZE, gp.INK)
    dy = CLASS_LBL_DY[hw_class(c.enum)]
    for lx, ly, anchor in ((c.x, c.y + dy, "middle"),
                           (c.x, c.y - dy, "middle"),
                           (c.x + c.r + 1.0, c.y + 1.0, "start")):
        if _caption_is_clear(c, lx, ly):
            return (lx, ly, anchor, 2.2, gp.INK)
    raise ValueError(f"no clear caption position for {c.enum} -- the geometry "
                     f"is too tight, move a control (spec 2026-08-10 §3)")
```

`_caption_is_clear` liest `ALL_HW`, das erst in Zeile 105 entsteht. Das ist in Ordnung, weil `hw_label` erst beim SVG-/Header-Bau aufgerufen wird — aber **`hw_label` darf nicht mehr aus `place()` heraus aufgerufen werden**. Es wird es auch nicht; `place()` setzt nur `n.lbl = None`.

- [ ] **Step 4: Grün prüfen und die Ausweichungen ansehen**

Run:
```
python res/gen_hw_panel.py
python res/test_hw_panel.py
```
Expected: `PASS -- hw panel guards ok`.

Zur Kontrolle, welche Beschriftungen ausgewichen sind:
```
python -c "import gen_hw_panel as h; [print(c.enum, h.hw_label(c)[:3]) for c in h.HW_PARAMS if h.hw_label(c)[1] < c.y or h.hw_label(c)[2] != 'middle']"
```
Expected: pro Deck weichen `SHAPE`, `RANGE`, `FLUXFB` und `STAGES` nach oben aus, in der Mitte `MORPH` nach oben und `REV_DECAY` nach rechts (`start`). Kommt eine andere Liste heraus, stimmt eine Koordinate aus Task 2 nicht.

- [ ] **Step 5: Commit**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py \
        host/vcv/res/FireflowHW.svg host/vcv/src/generated_hw_panel.hpp
git commit -F - <<'EOF'
fix(hw): captions step aside by rule, not by named exception

FLUXFB_LBL_Y_OFFSET existed because GRIT went bipolar and inherited KNOBC's
larger clearance. The size classes removed that cause; they did not remove the
pattern. At 16 mm row spacing a small knob's caption lands 8.1 mm from the
8.0 mm knob below it -- and the old test, which asked only for non-overlap,
called that a pass.

Shortening the offset cannot be the general answer: for MORPH and DECAY, two
big knobs 17 mm apart, every offset short enough to clear the neighbour is
inside the control's own footprint. The caption now tries below, then above,
then to the right, and the generator raises rather than printing a word on a
knob. The guard asks for 1.5 mm of margin, so 0.1 mm can no longer pass.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 4: HW-only-Inventar — MOD/SHIFT, acht CV-Eingänge, sechzehn LEDs

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py` — neue `HW_ONLY`-Liste, SVG-Schleife, `emit_table`, `header`
- Modify: `host/vcv/res/test_hw_panel.py` — `test_no_overlap_with_hw_radii`, `test_rail_keepout`, neu `test_cv_sits_under_its_target`, `test_hw_only_inventory`

**Interfaces:**
- Consumes: `X_FILT`, `X_TIMB`, `X_COLOR`, `X_LVL`, `JACK_Y`, `Y_TOP`, `CLASS_R` aus Task 2.
- Produces: `HW_ONLY: list` — Objekte mit denselben Feldern wie ein `PanelCtl`-Klon (`enum`, `kind`, `x`, `y`, `r`, `label`, `tip`), aber **ohne** VCV-Id. `ALL_HW` schließt sie ein, `HW_PARAMS` nicht. Header-Tabelle `kHwOnlyCtls`, die Task 6 für die Beschriftungen liest.

**Warum eine eigene Tabelle:** `test_same_runtime_params_same_order` hält Enum-Menge und -Reihenfolge von `HW_PARAMS` gegen `gp.RUNTIME_PANEL_PARAMS`. Dieser Vertrag bleibt — die HW-only-Elemente haben keine VCV-Id und dürfen dort nicht auftauchen. Sie brauchen trotzdem eine C++-Tabelle, weil Rack den Text im SVG **nicht** rendert (NanoSVG): jede Beschriftung, die im Rack-Rehearsal lesbar sein soll, muss durch `HwPanelText` laufen.

- [ ] **Step 1: Die Prüfungen schreiben**

In `test_hw_panel.py` vor `def main():` einfügen:

```python
def test_hw_only_inventory():
    """Was es auf Blech gibt, aber nicht im VCV-Modul: 2 Taster, 8 CV-Buchsen,
    16 zusätzliche LEDs (spec 2026-08-10 §5)."""
    kinds = {}
    for c in hw.HW_ONLY:
        kinds[hw.hw_class(c.enum)] = kinds.get(hw.hw_class(c.enum), 0) + 1
    check(kinds.get("P") == 2, f"expected 2 hw-only pads, got {kinds.get('P')}")
    check(kinds.get("J") == 8, f"expected 8 CV jacks, got {kinds.get('J')}")
    check(kinds.get("L") == 16, f"expected 16 hw-only LEDs, got {kinds.get('L')}")
    # and they must NOT have leaked into the shared param inventory
    assert [c.enum for c in hw.HW_PARAMS] == [c.enum for c in gp.RUNTIME_PANEL_PARAMS]
    total_leds = len([c for c in hw.ALL_HW if hw.hw_class(c.enum) == "L"])
    check(total_leds == 20, f"expected 20 LEDs on the panel, got {total_leds}")


def test_cv_sits_under_its_target():
    """Jede CV-Buchse trägt den Namen ihres Ziels, teilt dessen x exakt und
    liegt darunter (spec 2026-08-10 §4). Ohne diese Prüfung wandert eine
    Buchse in der nächsten Runde weg und die Beschriftung lügt."""
    by_label = {}
    for c in hw.HW_PARAMS:
        base = c.enum[:-2] if c.enum.endswith(("_A", "_B")) else c.enum
        by_label[(c.label, c.x > hw.CX)] = c
    cvs = [c for c in hw.HW_ONLY if c.enum.startswith("CV_")]
    check(len(cvs) == 8, f"expected 8 CV jacks, got {len(cvs)}")
    for j in cvs:
        target = by_label.get((j.label, j.x > hw.CX))
        check(target is not None, f"{j.enum} names {j.label!r}, which is not a knob")
        if target is None:
            continue
        check(abs(target.x - j.x) < 1e-6,
              f"{j.enum} at x={j.x} does not share x with {target.enum} "
              f"at x={target.x}")
        check(j.y > target.y, f"{j.enum} is not below {target.enum}")
        check(hw.hw_class(target.enum) == "G",
              f"{j.enum} points at {target.enum}, which is not a big knob")
```

- [ ] **Step 2: Rot laufen lassen**

Run: `python res/test_hw_panel.py`
Expected: `AttributeError: module 'gen_hw_panel' has no attribute 'HW_ONLY'`.

- [ ] **Step 3: `HW_ONLY` bauen**

In `gen_hw_panel.py` direkt **nach** der Zeile `ALL_HW = HW_PARAMS + HW_INPUTS + HW_OUTPUTS + HW_LIGHTS` (Zeile 105) einfügen — und diese Zeile anschließend, wie unten gezeigt, erweitern:

```python
# --- hardware-only inventory (spec 2026-08-10 §4/§5) ------------------------
# Elements that exist on sheet metal and have no VCV id: two reserved pads,
# eight CV inputs, sixteen further LEDs. They must NOT enter HW_PARAMS --
# test_same_runtime_params_same_order holds that list against the shared
# inventory. They DO need a C++ table: Rack does not render the SVG's text
# (NanoSVG), so every caption meant to be readable in the rehearsal goes
# through HwPanelText.
class HwOnly:
    __slots__ = ("enum", "kind", "x", "y", "r", "label", "tip")

    def __init__(self, enum, cls, x, y, label, tip):
        self.enum, self.x, self.y, self.label, self.tip = enum, x, y, label, tip
        self.kind = {"P": gp.LATCH, "J": gp.IN, "L": gp.LIGHT}[cls]
        self.r = CLASS_R[cls]


_CV = (("FILT", X_FILT), ("TIMB", X_TIMB), ("COLOR", X_COLOR), ("LVL", X_LVL))
# The four targets are read off engine/mod/lane_id.h, not chosen: LANE_SIZE,
# LANE_SOURCE, LANE_PITCH and LANE_LEVEL are slots the internal modulator
# already drives, and each has a big knob. LANE_MOTION (SHAPE) gets no jack --
# SHAPE is small and sits in band 1, where a cable would cross the playing
# surface. Deliberate deletion, spec §4.
_ENGINE_LED_X = [45.5, 49.5, 53.5, 57.5, 61.5]
_STATUS_LED_X = [85.0, 89.0]          # FLOW/STEP, CAPTURE (REC and GATE are shared)

HW_ONLY = [
    HwOnly("MODBTN", "P", 140.4, Y_TOP, "MOD", "reserved, no function"),
    HwOnly("SHIFTBTN", "P", 164.4, Y_TOP, "SHIFT", "reserved, no function"),
    HwOnly("TEMPO_L", "L", 149.4, Y_TOP, "", "tempo"),
    HwOnly("SYNC_L", "L", 155.4, Y_TOP, "", "sync"),
]
for _side, _sx in (("A", lambda v: v), ("B", lambda v: W - v)):
    for _name, _x in _CV:
        HW_ONLY.append(HwOnly(f"CV_{_name}_{_side}", "J", _sx(_x), JACK_Y,
                              _name, f"CV in -> {_name}"))
    for _i, _x in enumerate(_ENGINE_LED_X):
        HW_ONLY.append(HwOnly(f"ENG{_i}_{_side}_L", "L", _sx(_x), Y_TOP, "",
                              "engine"))
    for _name, _x in zip(("FLOW", "CAP"), _STATUS_LED_X):
        HW_ONLY.append(HwOnly(f"{_name}_{_side}_L", "L", _sx(_x), Y_TOP, "",
                              _name.lower()))

ALL_HW = HW_PARAMS + HW_INPUTS + HW_OUTPUTS + HW_LIGHTS + HW_ONLY
```

Die ursprüngliche `ALL_HW`-Zeile (105) wird durch die letzte Zeile oben ersetzt — sie darf nicht zweimal existieren.

`hw_class` muss die neuen Enums kennen. In `gen_hw_panel.py` `hw_class` um einen Vorabzweig erweitern, **vor** dem `_JACK_ENUMS`-Test:

```python
def hw_class(enum):
    if enum.startswith("CV_"):
        return "J"
    if enum in ("MODBTN", "SHIFTBTN"):
        return "P"
    if enum.endswith("_L"):
        return "L"
    if enum in _JACK_ENUMS:
        return "J"
    if enum in _LIGHT_ENUMS:
        return "L"
    base = enum[:-2] if enum.endswith(("_A", "_B")) else enum
    return HW_SIZE[base]
```

Achtung: `REC_A_L`/`GATE_A_L` enden ebenfalls auf `_L` und werden vom neuen Zweig erfasst — das ist richtig, sie sind LEDs.

- [ ] **Step 4: HW-only im SVG zeichnen**

In `svg()` die Schleife `for c in ALL_HW:` bleibt, wie sie ist — sie erfasst `HW_ONLY` automatisch, weil `ALL_HW` sie enthält und `HwOnly` dieselben Felder trägt. Die CV-Buchsen sollen sich vom Rest abheben; dazu in `svg()` den `IN`/`OUT`-Zweig ersetzen:

```python
        if c.kind in (gp.IN, gp.OUT):
            fill = "#1f4d44" if c.enum.startswith("CV_") else gp.GRAPHITE
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r)}" '
                      f'fill="{fill}" stroke="{gp.LINE}" stroke-width="0.35"/>')
```

- [ ] **Step 5: HW-only in den Header schreiben**

In `header()` nach der `kLightCtls`-Zeile einfügen:

```python
    L2.append("// Hardware-only: no VCV id. Rack does not render SVG text,")
    L2.append("// so these captions must come from here (spec 2026-08-10 §5).")
    L2.append("static const HwOnlyCtl kHwOnlyCtls[] = {")
    for c in HW_ONLY:
        lx, ly, anchor, size, colour = hw_label(c) if c.label else (
            c.x, c.y, "middle", 2.2, gp.INK)
        L2.append(f'    {{{gp.WKMAP[c.kind]}, {{{c.x:.3f}f, {c.y:.3f}f}}, '
                  f'"{c.label}", {{{lx:.3f}f, {ly:.3f}f}}, {ANCHOR_ID[anchor]}, '
                  f'{size:.2f}f, {rgb(colour)}}},')
    L2.append("};")
```

und den Struct-Typ oberhalb der Tabellen, direkt nach `L2.append("using namespace spkyvcv;")`:

```python
    L2.append("struct HwOnlyCtl { WidgetKind kind; XY mm; const char* label;")
    L2.append("                   XY lbl; unsigned char anchor; float lblSize;")
    L2.append("                   unsigned lblRgb; };")
```

Die Feldtypen sind aus `src/generated_panel.hpp:6` übernommen — dort lautet
`PanelCtl` `{ int id; WidgetKind kind; XY mm; const char* label; XY lbl;
unsigned char anchor; float lblSize; unsigned lblRgb; const char* tip; }`.
`HwOnlyCtl` ist dasselbe ohne `id` (es gibt keine) und ohne `tip` (ein
gedrucktes Panel hat keine Tooltips).

- [ ] **Step 6: Grün prüfen**

Run:
```
python res/gen_hw_panel.py
python res/test_hw_panel.py
```
Expected: `PASS -- hw panel guards ok`. Die Ausgabezeile des Generators zeigt weiter `params=68` — HW-only zählt nicht als Parameter.

- [ ] **Step 7: Rot beweisen, dass `test_cv_sits_under_its_target` greift**

Ändere probeweise `X_LVL` von `100.0` auf `99.0` (die Buchse folgt der Konstanten, der Knopf ebenfalls — deshalb stattdessen in `_CV` den Eintrag `("LVL", X_LVL)` auf `("LVL", X_LVL + 3.0)` ändern), dann:
```
python res/gen_hw_panel.py && python res/test_hw_panel.py
```
Expected: FAIL mit `CV_LVL_A at x=103.0 does not share x with COMP_A at x=100.0`. Danach zurückändern und neu generieren.

- [ ] **Step 8: Commit**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py \
        host/vcv/res/FireflowHW.svg host/vcv/src/generated_hw_panel.hpp
git commit -F - <<'EOF'
feat(hw): eight CV inputs land under the knobs they drive

lane_id.h names five target slots the internal modulator already drives. Four
have a big knob -- FILT, TIMB, COLOR, LVL -- and each now has a jack sharing
its exact x, 13 mm below, printed with the target's name. A guard holds the
two together, because a jack that drifts turns its own caption into a lie.
LANE_MOTION gets no jack: SHAPE is small and sits where a cable would cross
the playing surface.

MOD and SHIFT arrive as captioned pads with no behaviour, and the LED field
goes to the top of the envelope spec's 11-20 corridor. None of them enters
HW_PARAMS -- they have no VCV id, and the shared-inventory contract stands.
They do get a C++ table: Rack does not render the SVG's text, so a caption
that lives only in the SVG is invisible in the rehearsal.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 5: SD-Ausschnitt

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py` — `svg()` zeichnet das Rechteck
- Modify: `host/vcv/res/test_hw_panel.py` — neu `test_sd_cutout_is_clear`

**Interfaces:**
- Consumes: `SD_X`, `SD_Y`, `SD_W`, `SD_H` aus Task 2.
- Produces: nichts für spätere Tasks. Der Ausschnitt ist eine SVG-Form; Rack rendert SVG-**Formen** (nur Text nicht), also braucht er keine C++-Tabelle. Spec §6 nennt ein `kHwCutouts` — Task 7 streicht es dort, statt hier eine tote Tabelle zu erzeugen.

- [ ] **Step 1: Die Prüfung schreiben**

In `test_hw_panel.py` vor `def main():` einfügen:

```python
def test_sd_cutout_is_clear():
    """Kein Bedienelement, keine Buchse, keine LED und kein Beschriftungsanker
    liegt im SD-Ausschnitt (spec 2026-08-10 §3/§5). Der Ausschnitt sitzt 3 mm
    tiefer als die Buchsenmitten, weil TONEs Beschriftung sonst darin läge."""
    x0, x1 = hw.SD_X - hw.SD_W / 2, hw.SD_X + hw.SD_W / 2
    y0, y1 = hw.SD_Y - hw.SD_H / 2, hw.SD_Y + hw.SD_H / 2

    def dist_to_rect(px, py):
        dx = max(x0 - px, 0.0, px - x1)
        dy = max(y0 - py, 0.0, py - y1)
        return (dx * dx + dy * dy) ** 0.5

    for c in hw.ALL_HW:
        check(dist_to_rect(c.x, c.y) >= c.r - 1e-6,
              f"{c.enum} overlaps the SD cutout")
    for c in hw.HW_PARAMS + hw.HW_INPUTS + hw.HW_OUTPUTS:
        if not c.label:
            continue
        lx, ly = hw.hw_label(c)[0], hw.hw_label(c)[1]
        check(dist_to_rect(lx, ly) > 1e-6,
              f"caption {c.enum} at ({lx:.1f},{ly:.1f}) is inside the SD cutout")
    check(y1 <= hw.KEEP_BOT + 1e-9, "SD cutout crosses the bottom rail")


def test_sd_cutout_is_drawn():
    svg = open(os.path.join(HERE, "FireflowHW.svg")).read()
    check(f'width="{hw.SD_W:.3f}"' in svg and f'height="{hw.SD_H:.3f}"' in svg,
          "the SD cutout is not in the SVG")
```

- [ ] **Step 2: Rot laufen lassen**

Run: `python res/test_hw_panel.py`
Expected: FAIL mit `the SD cutout is not in the SVG`. (`test_sd_cutout_is_clear` läuft bereits grün — die Geometrie aus Task 2 hält den Ausschnitt frei. Das ist in Ordnung: das Rot kommt von `test_sd_cutout_is_drawn`, und Step 5 beweist, dass auch die Freiheitsprüfung greifen kann.)

- [ ] **Step 3: Den Ausschnitt zeichnen**

In `svg()` unmittelbar nach der Rail-Schleife (`for y in (KEEP_TOP, KEEP_BOT):`) einfügen:

```python
    # SD slot: a cutout, not a control. Drawn as an outline because it is a
    # hole in the plate, and 3 mm below the jack centres because TONE's
    # caption needs the room (spec 2026-08-10 §3).
    P.append(f'<rect x="{mm(SD_X - SD_W / 2)}" y="{mm(SD_Y - SD_H / 2)}" '
              f'width="{mm(SD_W)}" height="{mm(SD_H)}" rx="1" fill="none" '
              f'stroke="{gp.INK}" stroke-width="0.5" stroke-dasharray="1.5,1"/>')
    P.append(f'<text x="{mm(SD_X)}" y="{mm(SD_Y + 1.0)}" fill="{gp.INK}" '
              f'text-anchor="middle" font-family="monospace" '
              f'font-size="2.6">SD</text>')
```

- [ ] **Step 4: Grün prüfen**

Run:
```
python res/gen_hw_panel.py
python res/test_hw_panel.py
```
Expected: `PASS -- hw panel guards ok`.

- [ ] **Step 5: Rot beweisen, dass die Freiheitsprüfung greift**

Ändere probeweise `SD_Y` von `110.0` auf `107.0` (die Position, die die Spec ausdrücklich verwirft), dann:
```
python res/gen_hw_panel.py && python res/test_hw_panel.py
```
Expected: FAIL mit `caption REV_TONE at (152.4,101.9) is inside the SD cutout`. Genau dieser Fall ist der Grund für die 3 mm. Danach zurückändern und neu generieren.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py \
        host/vcv/res/FireflowHW.svg host/vcv/src/generated_hw_panel.hpp
git commit -F - <<'EOF'
feat(hw): the SD slot gets a position instead of a promise

The envelope spec decided a front-accessible 4-bit SD slot in February's §2
and never put it on the plate. It sits centred in the jack row, 3 mm below
the jack centres -- at the jack line TONE's caption would land inside the
cutout, which the new guard demonstrates when you move it back up.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 6: Das VCV-Rehearsal-Widget folgt der Hardware-Größe

**Files:**
- Modify: `host/vcv/src/generated_hw_panel.hpp` (generiert) — neue Tabelle `kParamSize`
- Modify: `host/vcv/res/gen_hw_panel.py` — `header()` schreibt `kParamSize`
- Modify: `host/vcv/res/test_hw_panel.py` — `test_header_contract`
- Modify: `host/vcv/src/Fireflow.cpp:1893-1920` (`FireflowHWWidget`-Knopfschleife), `:1876-1884` (`HwPanelText::draw`)

**Interfaces:**
- Consumes: `hw_class` aus Task 1, `HW_ONLY` aus Task 4.
- Produces: `static const unsigned char kParamSize[]` in `namespace spkyhw` — ein Byte je Eintrag in `kParamCtls`, gleiche Reihenfolge, `1` = groß, `0` = klein/Taster.

**Warum das nötig ist:** `FireflowHWWidget` wählt heute `RoundBlackKnob` für `WK_BIGKNOB`/`WK_KNOBC` und `Trimpot` für `WK_SMKNOB`/`WK_KNOBI`. Nach Task 2 zeichnet das SVG RATE klein, das Rehearsal-Widget in Rack aber weiter groß. Ein Panel, dessen Probelauf andere Knöpfe zeigt als das Blech, taugt nicht zum Beurteilen von Griffweiten.

- [ ] **Step 1: Die Header-Prüfung erweitern — das ist das Rot**

In `test_hw_panel.py` in `test_header_contract` nach der letzten `assert`-Zeile einfügen:

```python
    # The rehearsal widget must pick knob size from the HARDWARE class, not
    # from c.kind -- otherwise Rack shows a big RATE while the plate prints a
    # small one (spec 2026-08-10 §1).
    assert "kParamSize" in src, "kParamSize missing from the hw header"
    body = src.split("kParamSize[] = {")[1].split("};")[0]
    vals = [v.strip() for v in body.replace("\n", "").split(",") if v.strip()]
    assert len(vals) == len(hw.HW_PARAMS), (len(vals), len(hw.HW_PARAMS))
    want = ["1" if hw.hw_class(c.enum) == "G" else "0" for c in hw.HW_PARAMS]
    assert vals == want, "kParamSize disagrees with HW_SIZE"
    assert "kHwOnlyCtls" in src
```

- [ ] **Step 2: Rot laufen lassen**

Run: `python res/test_hw_panel.py`
Expected: FAIL mit `AssertionError: kParamSize missing from the hw header`.

- [ ] **Step 3: Die Tabelle emittieren**

In `gen_hw_panel.py` in `header()` direkt nach der `kParamCtls`-Tabelle einfügen:

```python
    L2.append("// 1 = big cap, 0 = small. Parallel to kParamCtls, same order.")
    L2.append("// The rehearsal widget reads THIS, not c.kind -- kind says")
    L2.append("// bipolar/detented, which is not a diameter.")
    L2.append("static const unsigned char kParamSize[] = {")
    L2.append("    " + ", ".join("1" if hw_class(c.enum) == "G" else "0"
                                 for c in HW_PARAMS) + ",")
    L2.append("};")
```

- [ ] **Step 4: Generieren und die Python-Gates grün prüfen**

Run:
```
python res/gen_hw_panel.py
python res/test_hw_panel.py
```
Expected: `PASS -- hw panel guards ok`.

- [ ] **Step 5: Das Widget umstellen**

In `host/vcv/src/Fireflow.cpp` in `FireflowHWWidget` die Knopfschleife so ändern, dass sie den Index kennt und die Größe daraus liest. Die Zeile

```cpp
        for (const auto& c : spkyhw::kParamCtls) {
            Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            switch (c.kind) {
                case WK_BIGKNOB: case WK_KNOBC:
                    addParam(createParamCentered<RoundBlackKnob>(pos, module, c.id)); break;
                case WK_SMKNOB: case WK_KNOBI:
```

ersetzen durch:

```cpp
        for (size_t i = 0; i < sizeof(spkyhw::kParamCtls) / sizeof(spkyhw::kParamCtls[0]); ++i) {
            const auto& c = spkyhw::kParamCtls[i];
            Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            // Knob size comes from the hardware class, never from c.kind:
            // KNOBC is bipolar and KNOBI is detented, and a centre-detent pot
            // ships in every size (spec 2026-08-10 §1). A rehearsal that
            // shows a big RATE while the plate prints a small one cannot be
            // used to judge grip.
            const bool big = spkyhw::kParamSize[i] != 0;
            switch (c.kind) {
                case WK_BIGKNOB: case WK_KNOBC: case WK_SMKNOB: case WK_KNOBI:
                    if (big) {
                        addParam(createParamCentered<RoundBlackKnob>(pos, module, c.id));
                        break;
                    }
```

Der bisherige `WK_SMKNOB`/`WK_KNOBI`-Zweig (ATTACK/STAGES-Sonderfall plus `Trimpot`) folgt unverändert als Rest dieses zusammengelegten `case`. Die `WK_SW2`-, `WK_LATCH`- und `WK_SMBTN`-Zweige bleiben, wie sie sind.

- [ ] **Step 6: Die HW-only-Beschriftungen zeichnen**

In `HwPanelText::draw` nach der `kOutputCtls`-Schleife einfügen:

```cpp
        for (const auto& c : spkyhw::kHwOnlyCtls)
            if (c.label[0])
                text(c.lbl.x, c.lbl.y, c.lblSize, 0.f, c.lblRgb, c.anchor, c.label);
```

- [ ] **Step 7: Bauen**

Run (aus dem Repo-Root):
```
host/vcv/build-local.sh
```
Expected: Build ohne Fehler. **Nie** `g++` von Hand aufrufen — das System-`g++` ist hier der ARM-Cross-Compiler und meldet „MinGW not found".

- [ ] **Step 8: Im Rack ansehen**

Modul `Fireflow HW` in VCV Rack laden. Erwartung: RATE, SHAPE, SMTH, RANGE, VARY, TUNE und GRIT sind Trimpots; FILT, TIMB, MIX, SEND und LVL sind `RoundBlackKnob`; die CV-Buchsen und MOD/SHIFT sind **gedruckt, aber nicht patch-/klickbar** (sie haben keine VCV-Id — bewusst, siehe Task 4).

- [ ] **Step 9: Commit**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py \
        host/vcv/src/generated_hw_panel.hpp host/vcv/src/Fireflow.cpp \
        host/vcv/res/FireflowHW.svg
git commit -F - <<'EOF'
fix(vcv): the hardware rehearsal shows the hardware's knob sizes

FireflowHWWidget picked RoundBlackKnob for WK_BIGKNOB/WK_KNOBC and Trimpot for
the rest, so after the regroup Rack would have shown a big RATE while the
plate printed a small one. A rehearsal that disagrees with the plate about
grip is the one thing this module exists to get right.

The header now carries kParamSize alongside kParamCtls, and the widget reads
it. HwPanelText also draws the hardware-only captions -- Rack does not render
SVG text, so CV, MOD and SHIFT would otherwise be nameless circles.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 7: Dokumente nachziehen und der Volllauf

**Files:**
- Modify: `docs/superpowers/specs/2026-08-10-hw-panel-regroup-design.md` — §6 Punkt 4 und §6 Punkt 3
- Modify: `docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md` — §1 Tabelle, §4 Rastersatz
- Modify: `docs/hardware/io-budget.md` — §5 Buchsentabelle
- Modify: `docs/roadmap.md` — Neugruppierung erledigt

- [ ] **Step 1: Die zwei Korrekturen an der neuen Spec**

In `docs/superpowers/specs/2026-08-10-hw-panel-regroup-design.md`:

**§6 Punkt 4** — der Absatz endet heute mit „Für diesen Fall gilt **6,0 mm**, als Regel im Generator, nicht als drei benannte Konstanten." Diesen Satz ersetzen durch:

> Eine Verkürzung trägt allerdings nicht: bei MORPH (152,4 / 62) und DECAY
> (152,4 / 79) sind beide groß und 17 mm auseinander, und jeder Abstand, der
> den Nachbarn freihält, liegt innerhalb der **eigenen** Sperrfläche — die
> Beschriftung stünde auf dem Knopf. Die Regel ist deshalb **Ausweichen statt
> Kürzen**: unten, sonst oben, sonst rechts (Anker `start`); die erste
> Position, die jede fremde Sperrfläche um 1,5 mm freihält und außerhalb der
> eigenen liegt, gewinnt. Der Generator wirft, wenn keine passt — er druckt
> kein Wort auf einen Knopf. Betroffen sind pro Deck SHAPE, RANGE und FB
> (weichen nach oben aus), in der Mitte MORPH (oben) und DECAY (rechts).

**§6 Punkt 3** — im Satz „Der Header bekommt dafür eigene Tabellen
(`kHwOnlyCtls`, `kHwCutouts`)." das `, kHwCutouts` streichen und anfügen:

> Der SD-Ausschnitt bekommt **keine** C++-Tabelle: er ist eine SVG-Form, und
> Rack rendert Formen (nur Text nicht). Eine Tabelle ohne Verbraucher wäre
> toter Code.

**§6 bekommt einen sechsten Punkt.** Die Spec behauptet einleitend, nur
Generator und Tests seien betroffen; das ist unvollständig. Anfügen:

> 6. **Das VCV-Rehearsal-Widget muss mit.** `FireflowHWWidget` in
>    `host/vcv/src/Fireflow.cpp` wählt den Knopf bisher über `c.kind`
>    (`WK_BIGKNOB`/`WK_KNOBC` → `RoundBlackKnob`, sonst `Trimpot`) und hätte
>    nach der Neuvergabe ein großes RATE gezeigt, während das Blech ein
>    kleines druckt. Ein Probelauf, der beim Thema Griffweite vom Blech
>    abweicht, ist wertlos. Der Header trägt dafür `kParamSize[]` parallel zu
>    `kParamCtls`. `HwPanelText` zeichnet zusätzlich `kHwOnlyCtls` — Rack
>    rendert den Text des SVG nicht, CV/MOD/SHIFT wären sonst namenlose Kreise.

Den einleitenden Satz von §6 („Nur `host/vcv/res/gen_hw_panel.py`,
`host/vcv/res/test_hw_panel.py` und die beiden generierten Dateien. Kein
Engine-Code, kein `gen_panel.py`.") entsprechend auf
„… und `host/vcv/src/Fireflow.cpp`" erweitern.

- [ ] **Step 2: Die Envelope-Spec nachziehen**

In `docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md` in der Tabelle unter §1 die Zeilen ersetzen:

```
| Potis (64 kontinuierlich: 18 groß, 46 klein) | 62 Positionen (2× Doppelbelegung: BEND teilt sich ATTACKs Knopf) | bis 128 Kanäle | 4067-Kette, 4 Sense-Pins (§2) |
| Taster | 6 (`ENGINE` ×2, `REC` ×2, `MOD`, `SHIFT` — die letzten zwei ohne Funktion) | 24 | 74HC165-Kette (`src/hw/sr_165.h`) |
| Status-LEDs | 20 (festgelegt, oberes Ende des Korridors) | 24 | 3× 74HC595 |
| Buchsen | 18 (10 + 8 CV-Eingänge auf `CV_1..8`) | Main-PCB | Main-PCB |
| SD-Slot | 1 | 1 | SDMMC 4-bit, Main-PCB, frontzugänglich |
```

Direkt unter der Tabelle einfügen:

> **Nachtrag 10. August 2026:** Zählbasis, Buchsenzahl und LED-Zahl kommen aus
> `2026-08-10-hw-panel-regroup-design.md`. Die dort beschlossenen acht
> CV-Eingänge liegen auf `CV_1..8` — genau den Pins, die §2 unten als
> Poti-Sense verwirft, weil sie bipolar konditioniert sind. Als CV-Eingänge
> sind sie richtig und bereits vorhanden; die frühere Kapazitätsangabe „12"
> war eine Schätzung des Buchsenfelds, keine elektrische Grenze.

In §4 den Satz „Zweistufiges Raster: große Knobs ~22 mm, kleine (9-mm-Potis, Mini-Kappen) ~15 mm." ersetzen durch:

> Zweistufiges Raster: große Knobs (Sperrfläche r = 8,0 mm), kleine
> (9-mm-Potis, Mini-Kappen, r = 5,5 mm). Der ausgeführte Zeilenrhythmus steht
> in `2026-08-10-hw-panel-regroup-design.md` §3; die dortige Runde ist die
> erste der hier gedeckelten drei.

- [ ] **Step 3: `io-budget.md` nachziehen**

In `docs/hardware/io-budget.md` §5 die Buchsentabelle um eine Zeile ergänzen und die Einleitung korrigieren. „Aus `gen_panel.py`, 4 Eingänge und 6 Ausgänge:" wird zu:

```
Aus `gen_panel.py` 4 Eingänge und 6 Ausgänge, plus 8 CV-Eingänge, die nur auf
dem Hardware-Panel existieren (Spec 2026-08-10 §4):
```

und die neue Tabellenzeile:

```
| `CV_FILT/TIMB/COLOR/LVL` ×2 | CV in | `CV_1..8`, bipolar konditioniert (`InitBipolarCv`) — genau deshalb als Poti-Sense verworfen und hier richtig |
```

- [ ] **Step 4: Roadmap nachziehen**

In `docs/roadmap.md` den Eintrag zur Neugruppierung auf erledigt setzen und auf die Spec sowie diesen Plan verweisen. Den exakten Wortlaut an die dort vorhandene Zeilenform anpassen — die Datei hat ihr eigenes Format, und dieser Plan schreibt es nicht vor.

- [ ] **Step 5: Der Volllauf**

Run (aus dem Repo-Root, in einer Shell **ohne** die Daisy-Toolchain):
```
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: alle Tests grün, darunter `hw_panel_guard`, `panel_guard` und `flow_panel_guard`. `-DCMAKE_BUILD_TYPE=Release` ist Pflicht — ein frisches Configure fällt sonst auf Debug zurück und `spky_tests`/`ctrl_identity` scheitern mit „SYNTH reference moved".

Run zusätzlich: `host/vcv/build-local.sh`
Expected: Build ohne Fehler.

- [ ] **Step 6: Commit**

```bash
git add docs/
git commit -F - <<'EOF'
docs(hw): the counting base catches up with the panel

The envelope spec's §1 table has been describing a panel that no longer
exists: 17/47 knobs, 10 jacks, 4 buttons, "~20" LEDs. It is now 18/46, 18
jacks, 6 buttons and exactly 20 LEDs, and the eight CV inputs land on the
very pins §2 rejected as pot sense -- bipolar conditioning made them wrong
for sensing and right for CV.

Two corrections to the regroup spec itself. The 6.0 mm caption shortening it
proposed cannot work: for two big knobs 17 mm apart, every offset short
enough to clear the neighbour is inside the control's own footprint. And the
SD cutout gets no C++ table -- Rack renders SVG shapes, so it would have been
dead code.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Selbstprüfung des Plans

**Spec-Abdeckung** — jede Anforderung hat eine Task:

| Spec | Task |
|---|---|
| §1 Größenklassen entkoppelt, 18/44 | 1 (Mechanismus), 2 (Vergabe + `test_size_classes_match_the_spec`) |
| §2 Gruppeninventar | 2 (die Koordinaten setzen die Gruppen räumlich um) |
| §3 Geometrie, Zeilenrhythmus, Koordinaten | 2 |
| §3 SD 3 mm tiefer | 2 (Konstante), 5 (Zeichnung + Beweis) |
| §4 CV unter dem Ziel, Lane-Zuordnung, LANE_MOTION gestrichen | 4 |
| §5 MOD/SHIFT | 4 |
| §5 LED-Feld auf 20 | 2 (die 4 geteilten), 4 (die 16 HW-only) |
| §5 SD-Slot | 5 |
| §6 Punkt 1 `HW_SIZE` | 1 |
| §6 Punkt 2 `DECK_POS`/`CENTER_POS`, Löcher weg | 2 |
| §6 Punkt 3 `HW_ONLY`, Vertrag `HW_PARAMS` bleibt | 4 |
| §6 Punkt 4 Beschriftungsregel | 3 (**korrigiert** — Ausweichen statt Kürzen, Task 7 zieht die Spec nach) |
| §6 Punkt 5 BEND-Doppelbelegung | 3 (`STAGES_LBL_Y_OFFSET` bleibt), 2 (Position) |
| §7 vier geänderte Prüfungen | 1 (`footprints`), 3 (`labels`), 4 (`overlap`, `keepout` über `ALL_HW`) |
| §7 drei neue Prüfungen | 4 (`cv_sits_under_its_target`), 5 (`sd_cutout_is_clear`), 2 (`size_classes_match_the_spec`) |
| §8 Dokumente | 7 |

**Nicht in der Spec, aber nötig:** Task 6 (das VCV-Widget). Die Spec sagt in §6, nur der Generator und die Tests seien betroffen — das ist unvollständig: `FireflowHWWidget` leitet die Knopfgröße ebenfalls aus `c.kind` ab und hätte nach Task 2 eine andere Größe gezeigt als das Blech. Task 6 schließt die Lücke im Code, Task 7 Step 1 ergänzt §6 um den fehlenden sechsten Punkt.

**Typkonsistenz:** `hw_class` wird in Task 1 definiert und in 2, 4, 5, 6 verwendet — überall mit dem vollen Enum-Namen als Argument. `CLASS_R` heißt in allen Tasks so. `HW_ONLY` heißt in Task 4 und 5 so. `HwOnlyCtl` trägt die Feldtypen aus `generated_panel.hpp:6` (`WidgetKind`, `XY`, `unsigned char anchor`). `LBL_MARGIN` ist in Generator und Test getrennt definiert (1,5) — das ist Absicht, der Test darf sich nicht auf den Prüfling verlassen.

**Offen gelassen, bewusst:** Task 7 Step 4 schreibt keinen Wortlaut für `docs/roadmap.md` vor, weil die Datei ihr eigenes Zeilenformat hat und ein erfundener Wortlaut dort mehr Schaden anrichtet als eine Anweisung, sich am Bestand zu orientieren.
