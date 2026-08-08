# FireFlow Hardware-Envelope: Dokument-Nachzug + HW-Panel-Modus — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Die Envelope-Spec vom 8. August (`docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md`) in die bestehenden Dokumente einarbeiten und den `gen_panel.py`-Hardware-Modus bauen: ein spielbares drittes VCV-Modul „FireFlow HW Draft" auf 60 HP mit statischer Beschriftung, echten Poti-Sperrflächen und Rail-Sperrzonen — das Iterations-Werkzeug für die Panel-Neugruppierung.

**Architecture:** Drei Doku-Tasks (Roadmap, Phase-0-Plan, Memory) sind reine Textarbeit mit Grep-Verifikation. Der HW-Modus ist ein **eigener Generator** `host/vcv/res/gen_hw_panel.py`, der `gen_panel.py` importiert (Parameter-Reihenfolge, Kinds, Labels sind geteilt — nichts wird doppelt gepflegt) und `res/FireflowHW.svg` + `src/generated_hw_panel.hpp` (Namespace `spkyhw`) emittiert. Ein drittes Modul `FireflowHW` teilt sich die `Fireflow`-Module-Klasse und bekommt ein eigenes, bewusst dummes Widget: keine LED-Ringe, keine dynamischen Captions. Das bestehende Modul und seine generierten Dateien werden **nicht angefasst**.

**Tech Stack:** Python 3 (Generator + pytest), C++ (VCV Rack Plugin via `build-local.sh`), Markdown.

## Global Constraints

- VCV-Build **ausschließlich** über `host/vcv/build-local.sh` (das System-`g++` ist der ARM-Cross-Compiler; niemals hand-rollen, niemals `env.sh` in derselben Shell).
- Python-Tests laufen aus `host/vcv/`: `python -m pytest res/test_hw_panel.py -q` (bzw. `res/test_panel.py`).
- Commit-Trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>` — nie der Default-Trailer.
- `res/Fireflow.svg`, `src/generated_panel.hpp`, die ParamId-Reihenfolge und alles am bestehenden Modul bleiben **byte-identisch**; `res/test_panel.py` muss nach jedem Task grün sein.
- Dev-Alpha: Patch-Kompatibilität ist ausdrücklich kein Kriterium (Memory `fireflow-dev-alpha-no-patch-compat`) — aber die ParamId-Stabilität des Hauptmoduls bleibt trotzdem unangetastet, weil `FireflowHW` dieselben Ids **liest**.
- Spec-Referenz in allen Doku-Edits: `docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md` (kurz: „Envelope-Spec").
- Alle neuen Doku-Texte auf Deutsch, Code-Kommentare auf Englisch (Repo-Konvention).

---

### Task 1: Roadmap-Spec-Kopfvermerk + `docs/roadmap.md` M6 Schritt 1

**Files:**
- Modify: `docs/superpowers/specs/2026-08-07-fireflow-hardware-roadmap-design.md:1-6`
- Modify: `docs/roadmap.md` (M6-Abschnitt, „So M6 now decomposes into two pieces": die Schritt-1-Beschreibung, ca. Zeile 2260–2273)

**Interfaces:**
- Consumes: Envelope-Spec (Entscheidungs- und §4-Text)
- Produces: nichts Programmatisches — aber Task 3 verweist auf denselben Wortlaut („Neugruppierung bei vollem Satz")

- [ ] **Step 1: Kopfvermerk in die Roadmap-Spec setzen**

In `2026-08-07-fireflow-hardware-roadmap-design.md` direkt nach der `**Ziel:**`-Zeile einfügen:

```markdown
> **In Teilen übersteuert (8. Aug 2026):** Formfaktor, Bedienelement-Budget,
> Glow-Frage, Terminbindung und die „Halbierungs"-Aufgabe regelt jetzt
> [`2026-08-08-fireflow-hardware-envelope-design.md`](2026-08-08-fireflow-hardware-envelope-design.md)
> — 60 HP, voller Bedienelementsatz, Termine als Korridor. Was dort nicht
> genannt ist (Zwei-Board-Architektur, Coupon-Idee, CNY-Regel, Bezugsquellen,
> Superbooth-Stufen), gilt hier unverändert.
```

- [ ] **Step 2: M6 Schritt 1 in `docs/roadmap.md` ersetzen**

Den Absatz `1. **Panel design** — decide the prototype's control surface. …` (der die „defensible reduction … preferring merged and removed controls" fordert) ersetzen durch:

```markdown
1. **Panel design** — decide the prototype's control surface. The reduction
   round is **cancelled**: the envelope spec
   (`docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md`)
   fixes the hardware at **60 HP with the full control set** (82 runtime
   params on 80 physical positions, BEND sharing ATTACK's knob). What remains
   is a **regrouping** pass — bounded, tested in Rack via the hardware-mode
   panel generator (`host/vcv/res/gen_hw_panel.py`), max. 3 rounds, static
   labels enforced from round 1. The parked hardware-placement questions from
   earlier milestones (M4.10's COLOR placement, the BBD deck's contextual
   VOICE row, the per-deck SEND) come due in this round — as placement, not
   as cuts.
```

- [ ] **Step 3: Verifizieren**

```bash
grep -n "übersteuert" docs/superpowers/specs/2026-08-07-fireflow-hardware-roadmap-design.md
grep -n "defensible reduction" docs/roadmap.md
grep -n "envelope spec" docs/roadmap.md
```

Erwartung: Zeile 1 liefert den neuen Vermerk; „defensible reduction" liefert **keinen** Treffer mehr; „envelope spec" liefert den neuen Absatz.

- [ ] **Step 4: Commit**

```bash
git add docs/roadmap.md docs/superpowers/specs/2026-08-07-fireflow-hardware-roadmap-design.md
git commit -m "docs: the reduction round is cancelled, the roadmap says so itself

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: Phase-0-Plan nachziehen (Task-2-Rewrite, 10-nF-Korrektur, WS2812-Zeile)

**Files:**
- Modify: `docs/superpowers/plans/2026-08-07-fireflow-phase-0-hardware-foundation.md:216-294` (Task 2 komplett)
- Modify: ebd. Zeile 688 (10-nF-Empfehlung in Task 6 Schritt 5)
- Modify: ebd. Zeile 711 (WS2812-Behauptung in Task 6 Schritt 7)

**Interfaces:**
- Consumes: Envelope-Spec §1 (Kapazitäten), §2 (Topologie), §5 (Kondensator-Korrektur)
- Produces: die Größen `n_mux_chips`, Sense-Pin-Zuordnung, Ketten-Pinzahl — die PCB-Arbeit ab September liest sie aus dem von Task 2 erzeugten `docs/hardware/io-budget.md`-Update

- [ ] **Step 1: Task-2-Abschnitt ersetzen**

Den gesamten Abschnitt `### Task 2: Die Panel-Reduktion entscheiden und das Pin-Budget rechnen` (bis zum `---` vor Task 3) ersetzen durch:

```markdown
### Task 2: Das Pin-Budget gegen die Envelope-Spec rechnen

**Die Reduktionsentscheidung ist gefallen — durch Wegfall.** Die Envelope-Spec
(`docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md`)
fixiert: volles Instrument, 60 HP, 82 Runtime-Parameter auf 80 physischen
Positionen. Dieser Task ist damit **reine Rechnung, keine Design-Entscheidung
mehr**: die konkrete Pin-Map der Spec-§2-Topologie, als Update von
`docs/hardware/io-budget.md`.

**Files:**
- Modify: `docs/hardware/io-budget.md`

**Interfaces:**
- Consumes: `tools/count_panel_controls.py` (Task 1), `lib/libDaisy/src/daisy_patch_sm.h`, Envelope-Spec §1/§2
- Produces: die Pin-Map (welcher Patch-SM-Pin trägt was), `n_mux_chips`, die Ketten-Topologie — Task 5/6 und die Coupon-Bestellung im September hängen daran

- [ ] **Schritt 1: Die Klassifikation auf den vollen Satz umstellen**

Die 82er-Tabelle bleibt als Prüfsumme, aber die Einstufungen kollabieren:
`KNOB` (eigener Mux-Kanal), `PAD` (Bit auf der 74HC165-Kette), `LAYER`
(die vier HIDDEN_PARAMS: ALT-Zugang oder Default, Entscheidung laut Spec in
der Neugruppierung), `SHARED` (STAGES_A/B teilt ATTACKs Knopf). `CUT`,
`SELECT`, `ENCODER`, `FADER` kommen nicht mehr vor — steht eine solche Zeile
noch da, widerspricht das Dokument der Spec und der Task ist nicht fertig.
Summe muss 82 ergeben.

- [ ] **Schritt 2: Die Topologie-Rechnung**

```
Sense-Pins        = 4 (A2, A3, D9, D8 — die einzigen rohen ADC-Pins)
Poti-Mux-Chips    = aufgerundet(n_KNOB / 16) 74HC4067, je zwei teilen sich
                    einen Sense-Pin; Adress- UND Enable-Leitungen kommen aus
                    der 595-Kette, nicht aus GPIOs
Taster            = aufgerundet(n_PAD / 8) × 74HC165
LEDs              = 3 × 74HC595 (24 Ausgänge, LED-Belegungstabelle der Spec §1)
Ketten-GPIOs      = 4 (Daten-Out, Takt, Latch/Load, Daten-In — bit-bang,
                    SPI2 ist tabu: D8/D9 sind Sense-Pins)
SDMMC 4-bit       = D2–D7 (fix — 1-bit verliert das Bootloader-SD-Update)
CV-In-Buchsen     = 4 von CV_1..8 (bipolar konditioniert, genau richtig für CV)
Gate/CV-Out/Audio = B9/B10, B5/B6, C1/C10, B1–B4 (fest)
```

Ergebniszeile im Dokument: „X GPIOs nötig, Y verfügbar, Reserve steckt in
den Ketten (Spec §2), nicht in Pins." Die alte 20-%-Pin-Reserve-Regel gilt
auf Ketten-Ebene: jede Erweiterung kostet ein Schieberegister, keinen GPIO.

- [ ] **Schritt 3: Die Buchsen festlegen**

Unverändert zur alten Fassung: je Buchse eine Zeile (Audio/CV/Gate, Richtung,
Patch-SM-Anschluss) — jetzt inklusive SD-Slot als eigener Zeile und mit dem
Spec-§2-Vorbehalt an den CV-Outs (0–5 V unipolar, dokumentiert akzeptiert).

- [ ] **Schritt 4: Commit**

```bash
git add docs/hardware/io-budget.md
git commit -m "docs(hardware): the pin budget stops arguing and starts counting

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```
```

- [ ] **Step 2: Die 10-nF-Empfehlung korrigieren (Zeile 688)**

Alt:

```
Dazu **100 nF** direkt zwischen `VCC` und `GND` am Chip, und **10 nF** von `COM` gegen GND als Puffer für das Sample-and-Hold des ADC.
```

Neu:

```
Dazu **100 nF** direkt zwischen `VCC` und `GND` am Chip. An `COM` **höchstens 1 nF** gegen GND — die früher hier empfohlenen 10 nF machen τ ≈ 26 µs und drücken den Vollscan auf ~270 Hz, haarscharf ans Ziel (Envelope-Spec §5); Schritt 5b misst, ob es auch ganz ohne Kondensator ruhig ist.
```

- [ ] **Step 3: Die WS2812-Behauptung ersetzen (Zeile 711)**

Alt:

```
4. Messen mit zusätzlich getriebener WS2812-Kette in voller geplanter LED-Zahl aus Task 2 — **WS2812 ist der teure Posten und der wahrscheinlichste Grund, warum 3,57 Punkte nicht reichen.**
```

Neu:

```
4. Messen mit zusätzlich getriebener 595/165-Kette in voller geplanter Länge aus Task 2. Die WS2812-Kränze sind gestrichen (Envelope-Spec §3) — und zwar wegen Strom, BOM und libDaisy-Fork, nicht wegen CPU: der alte Treiber war DMA-getrieben und fast gratis. Der teure Posten ist damit unbekannt; genau deshalb wird gemessen, gegen die reale Reserve von 2,17 Punkten (patch_sm), nicht 3,57 (Seed).
```

- [ ] **Step 4: Verifizieren**

```bash
grep -n "Reduktion" docs/superpowers/plans/2026-08-07-fireflow-phase-0-hardware-foundation.md | head
grep -cn "10 nF" docs/superpowers/plans/2026-08-07-fireflow-phase-0-hardware-foundation.md
grep -n "WS2812 ist der teure Posten" docs/superpowers/plans/2026-08-07-fireflow-phase-0-hardware-foundation.md
```

Erwartung: kein „Panel-Reduktion entscheiden" mehr im Task-2-Titel; „10 nF" nur noch als Negativ-Erwähnung in der Korrektur; die alte WS2812-Zeile: kein Treffer.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/plans/2026-08-07-fireflow-phase-0-hardware-foundation.md
git commit -m "docs(plan): phase 0 stops planning a reduction that no longer exists

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: Memory-Update `spotykach-hardware-constraint`

**Files:**
- Modify: `C:\Users\bernd\.claude\projects\C--Users-bernd-Documents-AI-FireFlow\memory\spotykach-hardware-constraint.md`
- Modify: `C:\Users\bernd\.claude\projects\C--Users-bernd-Documents-AI-FireFlow\memory\MEMORY.md` (die eine Hook-Zeile)

**Interfaces:**
- Consumes: Envelope-Spec (Entscheidung)
- Produces: nichts — verhindert, dass eine spätere Session die Halbierung re-imponiert

- [ ] **Step 1: Memory-Datei umschreiben**

Body der Datei (Frontmatter-`description` mitziehen) ersetzen durch:

```markdown
---
name: spotykach-hardware-constraint
description: "Hardware target is DEFINED since 2026-08-08: 60 HP, FULL control set — the old 'must reduce controls' rule is dead; the budget rule is one-in-one-out"
metadata:
  node_type: memory
  type: project
---

**Updated 2026-08-08:** the reduction rule is retired. The envelope spec
(`docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md` in
the code repo) fixes the hardware at **60 HP Eurorack with the full control
set**: 82 runtime params on 80 physical positions (BEND shares ATTACK's
knob), 74HC4067 chains on the 4 raw ADC pins, buttons on 74HC165, LEDs on
74HC595, 4-bit SD slot. The Glow/macro hardware path is dead (18 HP
experiment failed by ear, 2026-08-08).

**Why:** "reducible to real hardware" was a guard against a port cliff to a
smaller panel. That panel no longer exists — the hardware IS the full panel.

**How to apply:** The live rule is **one in, one out**: 60 HP carries the
full set with zero spare rows, so any panel change that ADDS a control must
name the control it removes. Regrouping happens via the hardware-mode
generator (`host/vcv/res/gen_hw_panel.py`), max 3 rounds, static labels only.
Related: [[spotykach-milestone-status]], [[spotykach-vcv-panel-layout]].
```

- [ ] **Step 2: Index-Zeile in `MEMORY.md` anpassen**

Alt:

```markdown
- [Hardware constraint](spotykach-hardware-constraint.md) — panel must stay reducible to real hardware, but the target panel is now undesigned (M6 prototype)
```

Neu:

```markdown
- [Hardware constraint](spotykach-hardware-constraint.md) — target is DEFINED: 60 HP, full control set (envelope spec 2026-08-08); rule is one-in-one-out, the old reduction rule is dead
```

- [ ] **Step 3: Verifizieren**

```bash
grep -n "one-in-one-out\|one in, one out" "C:\Users\bernd\.claude\projects\C--Users-bernd-Documents-AI-FireFlow\memory\MEMORY.md" "C:\Users\bernd\.claude\projects\C--Users-bernd-Documents-AI-FireFlow\memory\spotykach-hardware-constraint.md"
```

Erwartung: je ein Treffer in beiden Dateien. (Memory liegt außerhalb des Repos — kein Commit.)

---

### Task 4: `gen_hw_panel.py` + `test_hw_panel.py` — der Hardware-Modus (TDD)

**Files:**
- Create: `host/vcv/res/gen_hw_panel.py`
- Create: `host/vcv/res/test_hw_panel.py`
- Create (generiert, committed): `host/vcv/res/FireflowHW.svg`, `host/vcv/src/generated_hw_panel.hpp`

**Interfaces:**
- Consumes: aus `gen_panel.py`: `RUNTIME_PANEL_PARAMS`, `INPUTS`, `OUTPUTS`, `LIGHTS`, die Kind-Konstanten (`BIGKNOB` …), `MM_PER_HP`, `dynamic_words()`. Der Import ist die DRY-Garantie: Parameter-Menge und -Reihenfolge existieren nur einmal.
- Produces: `spkyhw::kParamCtls / kInputCtls / kOutputCtls / kLightCtls / kPanelTexts` (Typen aus `spkyvcv`, Ids sind `spkyvcv`-Enum-Namen) und `spkyhw::kHwHP = 60` — Task 5 baut sein Widget ausschließlich daraus.

**Iteration-0-Layout (mechanische Übersetzung der heutigen Gruppierung, Spec §4):** Deck A links, Deck B strikt gespiegelt (`x → W − x`), Mitte geteilt. Große Knobs 22-mm-Raster in zwei Reihen (5+4), kleine 15-mm-Raster in drei Reihen, Pads in Reihe 3, Buchsen unten, Rail-Sperrzonen `y ∈ [9.0, 119.5]`. STAGES_A/B liegt **absichtlich** auf ATTACK_A/B (die dokumentierte Doppelbelegung).

- [ ] **Step 1: Die Tests schreiben (rot)**

`host/vcv/res/test_hw_panel.py`:

```python
"""Contract tests for the hardware-mode panel (envelope spec 2026-08-08 §4).
These test CONSTRAINTS, not taste: regrouping iterations may move anything,
but can never violate size, keep-outs, footprints, or static lettering."""
import os, re
import gen_panel as gp
import gen_hw_panel as hw

HERE = os.path.dirname(os.path.abspath(__file__))

def test_panel_is_60hp():
    assert hw.HP == 60
    assert abs(hw.W - 60 * gp.MM_PER_HP) < 1e-9
    assert hw.Hh == 128.5

def test_same_runtime_params_same_order():
    # The hw panel is the SAME instrument: identical enum set, identical order.
    assert [c.enum for c in hw.HW_PARAMS] == [c.enum for c in gp.RUNTIME_PANEL_PARAMS]
    assert [c.enum for c in hw.HW_INPUTS] == [c.enum for c in gp.INPUTS]
    assert [c.enum for c in hw.HW_OUTPUTS] == [c.enum for c in gp.OUTPUTS]
    assert [c.enum for c in hw.HW_LIGHTS] == [c.enum for c in gp.LIGHTS]

def test_static_captions_only():
    # An aluminium panel is printed: every label is the resting word, and the
    # generated header must carry NO dynamic-caption table.
    for c in hw.HW_PARAMS:
        words = gp.dynamic_words(c.enum.rsplit("_", 1)[0])
        if words:
            assert c.label == words[0], c.enum
    src = open(os.path.join(HERE, "..", "src", "generated_hw_panel.hpp")).read()
    assert "DynCaption" not in src

def test_hardware_footprints():
    # Screen-widget radii are meaningless on sheet metal. Minimum clearance
    # radii per kind (mm): big cap 8.0, 9mm pot + mini cap 5.5, pad 4.0,
    # jack 4.0, LED 1.5.
    minima = {gp.BIGKNOB: 8.0, gp.KNOBC: 8.0, gp.SMKNOB: 5.5, gp.KNOBI: 5.5,
              gp.SW2: 4.0, gp.LATCH: 4.0, gp.SMBTN: 4.0,
              gp.IN: 4.0, gp.OUT: 4.0, gp.LIGHT: 1.5}
    for c in hw.ALL_HW:
        assert c.r >= minima[c.kind] - 1e-9, (c.enum, c.r)

def test_rail_keepout():
    # Rails and screws own the top/bottom ~9 mm; VCV's 2 mm rule is not enough.
    for c in hw.ALL_HW:
        assert c.y - c.r >= hw.KEEP_TOP - 1e-9, (c.enum, "top")
        assert c.y + c.r <= hw.KEEP_BOT + 1e-9, (c.enum, "bottom")
        assert c.x - c.r >= 2.0 and c.x + c.r <= hw.W - 2.0, (c.enum, "side")

def test_no_overlap_with_hw_radii():
    # Radius-sum clearance with REAL footprints. Exactly one legal overlap:
    # BEND shares ATTACK's knob (deliberate dual assignment, spec §1).
    SHARED_OK = {frozenset(("ATTACK_A", "STAGES_A")),
                 frozenset(("ATTACK_B", "STAGES_B"))}
    items = hw.ALL_HW
    for i, a in enumerate(items):
        for b in items[i + 1:]:
            if frozenset((a.enum, b.enum)) in SHARED_OK:
                continue
            d = ((a.x - b.x) ** 2 + (a.y - b.y) ** 2) ** 0.5
            assert d >= a.r + b.r - 1e-6, (a.enum, b.enum, round(d, 2))

def test_mirror_symmetry():
    # Deck B is deck A mirrored — the instrument's identity, kept by machine.
    pos_a = {c.enum[:-2]: (c.x, c.y) for c in hw.HW_PARAMS if c.enum.endswith("_A")}
    pos_b = {c.enum[:-2]: (c.x, c.y) for c in hw.HW_PARAMS if c.enum.endswith("_B")}
    assert pos_a.keys() == pos_b.keys()
    for k, (xa, ya) in pos_a.items():
        xb, yb = pos_b[k]
        assert abs((hw.W - xa) - xb) < 1e-6 and abs(ya - yb) < 1e-6, k

def test_header_contract():
    src = open(os.path.join(HERE, "..", "src", "generated_hw_panel.hpp")).read()
    assert "namespace spkyhw" in src
    assert "kHwHP = 60" in src
    for tbl in ("kParamCtls", "kInputCtls", "kOutputCtls", "kLightCtls",
                "kPanelTexts"):
        assert tbl in src, tbl
    # Ids are the MAIN module's enum values — one id space, two layouts.
    assert re.search(r"\{\s*RATE_A\s*,", src)

def test_svg_exists_and_is_60hp():
    svg = open(os.path.join(HERE, "FireflowHW.svg")).read()
    assert f'width="{hw.W:.3f}mm"' in svg and 'height="128.500mm"' in svg
```

- [ ] **Step 2: Tests laufen lassen — sie müssen fehlschlagen**

```bash
cd host/vcv && python -m pytest res/test_hw_panel.py -q
```

Erwartung: `ModuleNotFoundError: No module named 'gen_hw_panel'` (der RED-Beweis).

- [ ] **Step 3: Den Generator schreiben**

`host/vcv/res/gen_hw_panel.py` — vollständige Struktur; die Koordinatentabellen sind das Iteration-0-Layout und werden in den Neugruppierungs-Runden bewegt:

```python
#!/usr/bin/env python3
"""Hardware-mode panel: the 60 HP envelope draft (envelope spec 2026-08-08 §4).

Emits res/FireflowHW.svg + src/generated_hw_panel.hpp (namespace spkyhw).
This is the REGROUPING TOOL: iteration 0 is a mechanical translation of
today's grouping onto the hardware grid. Later rounds move controls freely;
test_hw_panel.py pins what may never move — size, keep-outs, footprints,
mirror symmetry, static lettering.

Shares all parameter identity with gen_panel.py (import); defines only
geometry. Run from host/vcv/:  python3 res/gen_hw_panel.py
"""
import os, copy
import gen_panel as gp

HP = 60
W  = HP * gp.MM_PER_HP            # 304.8 mm
Hh = 128.5
CX = W / 2.0
KEEP_TOP, KEEP_BOT = 9.0, 119.5   # rails + M3 screws own the rest

# Real clearance radii (mm): what a finger and a nut need, not a pixel.
HW_R = {gp.BIGKNOB: 8.0, gp.KNOBC: 8.0, gp.SMKNOB: 5.5, gp.KNOBI: 5.5,
        gp.SW2: 4.0, gp.LATCH: 4.0, gp.SMBTN: 4.0,
        gp.IN: 4.0, gp.OUT: 4.0, gp.LIGHT: 1.5}
LBL_DY_HW = {gp.BIGKNOB: 10.5, gp.KNOBC: 10.5, gp.SMKNOB: 8.0, gp.KNOBI: 8.0,
             gp.SW2: 6.5, gp.LATCH: 6.5, gp.SMBTN: 6.5,
             gp.IN: -6.0, gp.OUT: -6.0, gp.LIGHT: 0.0}   # jack labels ABOVE

# --- iteration-0 slot map: deck-A coordinates per param BASE name -----------
# Big knobs, 22 mm grid, two rows (5 + 4).
BIG_ROW1_Y, BIG_ROW2_Y = 18.0, 40.0
DECK_POS = {
    "RATE":    (14.0, BIG_ROW1_Y), "SHAPE":  (36.0, BIG_ROW1_Y),
    "DENSITY": (58.0, BIG_ROW1_Y), "SMOOTH": (80.0, BIG_ROW1_Y),
    "RANGE":  (102.0, BIG_ROW1_Y),
    "MELODY":  (25.0, BIG_ROW2_Y), "MOD":    (47.0, BIG_ROW2_Y),
    "TUNE":    (69.0, BIG_ROW2_Y), "COLOR":  (91.0, BIG_ROW2_Y),
    # small rows, 15 mm grid; voice cluster | fx cluster
    "ATTACK":   (12.0, 56.0), "FILT":    (27.0, 56.0), "SUB":     (42.0, 56.0),
    "FLUXRATE": (62.0, 56.0), "FLUX":    (77.0, 56.0), "FLUXFB":  (92.0, 56.0),
    "REV_MIX": (107.0, 56.0),
    "DECAY":    (12.0, 71.0), "RES":     (27.0, 71.0), "SOURCE":  (42.0, 71.0),
    "LINK":     (62.0, 71.0), "FLUXTIME":(77.0, 71.0), "GRIT":    (92.0, 71.0),
    "COMP":    (107.0, 71.0),
    # play row: seq knobs + pads
    "STEPS":    (12.0, 86.0), "FORM":    (27.0, 86.0), "SONG":    (42.0, 86.0),
    "ENGINE":   (57.0, 86.0), "GRITMODE":(72.0, 86.0), "STEP":    (87.0, 86.0),
    "NEWPHRASE":(102.0, 86.0), "REC":    (117.0, 86.0),
    # deliberate dual assignment: BEND rides ATTACK's knob (spec §1)
    "STAGES":   (12.0, 56.0),
}
# --- shared centre strip, 15 mm grid, three columns -------------------------
CL, CC, CR = CX - 16.0, CX, CX + 16.0
CENTER_POS = {
    "SYNC":   (CL, 18.0), "MORPH": (CC, 18.0), "TIDE":  (CR, 18.0),
    "TEMPO":  (CL, 36.0), "COUPLE":(CC, 36.0), "SHUFFLE":(CR, 36.0),
    "SCALE":  (CL, 51.0), "DRIFT": (CC, 51.0), "CHOKE": (CR, 51.0),
    "SPOT":   (CL, 66.0), "MASTER_DRIVE": (CC, 66.0), "SETTLE": (CR, 66.0),
    "REV_SIZE":(CL, 81.0), "REV_TONE": (CC, 81.0), "REV_SMEAR": (CR, 81.0),
    "REV_DECAY":(CL, 96.0), "REV_DIFF": (CC, 96.0), "REV_MOD":  (CR, 96.0),
}
JACK_Y = 113.0
JACK_POS = {"PITCH_A": 14.0, "GATE_A": 29.0, "IN_L": 60.0, "IN_R": 75.0,
            "CLOCK": CX - 7.5, "RESET": CX + 7.5,
            "OUT_L": W - 75.0, "OUT_R": W - 60.0,
            "GATE_B": W - 29.0, "PITCH_B": W - 14.0}
LIGHT_POS = {"GATE_A_L": (7.0, 30.0), "GATE_B_L": (W - 7.0, 30.0),
             "REC_A_L": (124.5, 86.0), "REC_B_L": (W - 124.5, 86.0)}

def place(c):
    """Clone a gen_panel control onto the hardware grid."""
    n = copy.copy(c)
    n.r = HW_R[c.kind]
    n.lbl = None                    # no radial orbit labels on the hw grid
    base, x = c.enum, None
    if base.endswith("_A") or base.endswith("_B"):
        stem, side = base[:-2], base[-1]
        if stem in DECK_POS:
            ax, ay = DECK_POS[stem]
            n.x, n.y = (ax, ay) if side == "A" else (W - ax, ay)
            return n
    if base in CENTER_POS:
        n.x, n.y = CENTER_POS[base]
        return n
    if base in JACK_POS:
        n.x, n.y = JACK_POS[base], JACK_Y
        return n
    if base in LIGHT_POS:
        n.x, n.y = LIGHT_POS[base]
        return n
    raise KeyError(f"no hw slot for {base}")

HW_PARAMS  = [place(c) for c in gp.RUNTIME_PANEL_PARAMS]
HW_INPUTS  = [place(c) for c in gp.INPUTS]
HW_OUTPUTS = [place(c) for c in gp.OUTPUTS]
HW_LIGHTS  = [place(c) for c in gp.LIGHTS]
ALL_HW = HW_PARAMS + HW_INPUTS + HW_OUTPUTS + HW_LIGHTS

TEXTS = [(CX, 6.0, 3.2, 0.9, gp.INK, "middle", "FIREFLOW HW DRAFT 60HP")]

def hw_label(c):
    return (c.x, c.y + LBL_DY_HW[c.kind], "middle", 2.2, gp.INK)
```

Dazu im selben File `svg()` (flacher Entwurfsstil: Papier-Hintergrund, gestrichelte Keep-out-Linien bei `KEEP_TOP`/`KEEP_BOT`, ein Kreis/Rechteck pro Glyph nach Kind wie in `gen_panel.svg()`, Labels via `hw_label`) und `header()` — Letzteres emittiert:

```cpp
// GENERATED by res/gen_hw_panel.py -- do not edit by hand.
#pragma once
#include "generated_panel.hpp"
namespace spkyhw {
using namespace spkyvcv;
static constexpr int kHwHP = 60;
static const PanelCtl kParamCtls[] = { /* eine Zeile je HW_PARAMS-Eintrag,
    exakt das emit_table-Format aus gen_panel.header(), Label-Position aus
    hw_label(), Ids sind die spkyvcv-Enum-NAMEN (RATE_A, ...) */ };
static const PanelCtl kInputCtls[]  = { /* HW_INPUTS */ };
static const PanelCtl kOutputCtls[] = { /* HW_OUTPUTS */ };
static const PanelCtl kLightCtls[]  = { /* HW_LIGHTS */ };
static const PanelTxt kPanelTexts[] = { /* TEXTS */ };
} // namespace spkyhw
```

(Die `emit_table`/`rgb`/`ANCHOR_ID`-Helfer aus `gen_panel.py` per Import bzw. lokaler Kopie der drei Zeilen nutzen; **kein** `DynCaption`-Emit, **keine** Enum-Emission — die Ids kommen aus `generated_panel.hpp`.) `__main__`-Block wie in `gen_panel.py`: schreibt `res/FireflowHW.svg` und `src/generated_hw_panel.hpp` und druckt die Zählung.

- [ ] **Step 4: Generieren und Tests grün laufen lassen**

```bash
cd host/vcv && python res/gen_hw_panel.py && python -m pytest res/test_hw_panel.py res/test_panel.py -q
```

Erwartung: alle `test_hw_panel.py`-Tests PASS, und `test_panel.py` **unverändert grün** (das Hauptpanel wurde nicht berührt). Schlägt `test_no_overlap_with_hw_radii` oder `test_rail_keepout` fehl, werden die Slot-Tabellen justiert — nie die Testgrenzen.

- [ ] **Step 5: Commit (Quellen + beide Generate)**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py host/vcv/res/FireflowHW.svg host/vcv/src/generated_hw_panel.hpp
git commit -m "vcv(hw): the sixty-horsepower draft exists on paper that tests itself

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Drittes Modul „FireFlow HW Draft" — spielbar in Rack

**Files:**
- Modify: `host/vcv/plugin.json` (Modul-Eintrag)
- Modify: `host/vcv/src/plugin.hpp:8-9` (`extern Model* modelFireflowHW;`)
- Modify: `host/vcv/src/plugin.cpp:7-8` (`p->addModel(modelFireflowHW);`)
- Modify: `host/vcv/src/Fireflow.cpp` (Menü-Refactor + HW-Widget + Model, ans Dateiende bei Zeile ~1648)

**Interfaces:**
- Consumes: `spkyhw::*`-Tabellen aus Task 4; die bestehende `Fireflow`-Module-Klasse, `SlotVisible<>`, `EngineCycleLatch`, `VCVLatch`, `SamplerOnly<>` (alle in `Fireflow.cpp` vor Zeile 1440 definiert)
- Produces: Modul-Slug `"FireflowHW"` — die Neugruppierungs-Runden ändern danach nur noch Task-4-Koordinaten + Regenerat, nie wieder C++

- [ ] **Step 1: `plugin.json`-Eintrag ergänzen**

In `modules` nach dem Glow-Eintrag:

```json
{
  "slug": "FireflowHW",
  "name": "FireFlow HW Draft",
  "description": "60 HP hardware-envelope draft of the full instrument: static labels, real knob clearances, no LED rings. The regrouping workbench, not a release module.",
  "tags": ["Function generator", "LFO", "Random", "Synth voice", "Effect", "Polyphonic"]
}
```

- [ ] **Step 2: Registrierung**

`plugin.hpp`: nach `extern Model* modelGlow;` die Zeile `extern Model* modelFireflowHW;`. `plugin.cpp`: nach `p->addModel(modelGlow);` die Zeile `p->addModel(modelFireflowHW);`.

- [ ] **Step 3: Kontextmenü teilen (Refactor ohne Verhaltensänderung)**

In `Fireflow.cpp` den kompletten Body von `FireflowWidget::appendContextMenu` (Zeilen 1519–1645) in eine freie Funktion heben:

```cpp
static void appendFireflowMenu(Menu* menu, Fireflow* m) {
    /* the verbatim body of today's appendContextMenu, with getModule<...>()
       replaced by the m parameter */
}
```

und beide Widgets rufen sie:

```cpp
void appendContextMenu(Menu* menu) override {
    appendFireflowMenu(menu, getModule<Fireflow>());
}
```

- [ ] **Step 4: HW-Widget + Model ans Dateiende**

Nach `Model* modelFireflow = …` (Zeile 1648):

```cpp
#include "generated_hw_panel.hpp"

// The 60 HP hardware-envelope draft (envelope spec 2026-08-08 §4): same
// Module, same param ids, different sheet metal. Deliberately dumb — no LED
// rings, no dynamic captions, no engine-aware hiding beyond the shared
// ATTACK/BEND knob and the sampler-only REC pads. An aluminium panel can do
// none of those tricks, so neither does its rehearsal.
struct HwPanelText : Widget {
    void draw(const DrawArgs& args) override {
        auto text = [&](float x, float y, float size, float spacing,
                        unsigned rgb, unsigned char anchor, const char* s) {
            nvgFontFaceId(args.vg, APP->window->uiFont->handle);
            nvgFontSize(args.vg, mm2px(size));
            nvgTextLetterSpacing(args.vg, mm2px(spacing));
            nvgFillColor(args.vg, nvgRGB((rgb >> 16) & 0xFF,
                                         (rgb >> 8) & 0xFF, rgb & 0xFF));
            nvgTextAlign(args.vg, (anchor == 1 ? NVG_ALIGN_LEFT :
                                   anchor == 2 ? NVG_ALIGN_RIGHT :
                                   NVG_ALIGN_CENTER) | NVG_ALIGN_BASELINE);
            nvgText(args.vg, mm2px(x), mm2px(y), s, nullptr);
        };
        for (const auto& c : spkyhw::kParamCtls)
            if (c.label[0])
                text(c.lbl.x, c.lbl.y, c.lblSize, 0.f, c.lblRgb, c.anchor, c.label);
        for (const auto& c : spkyhw::kInputCtls)
            text(c.lbl.x, c.lbl.y, c.lblSize, 0.f, c.lblRgb, c.anchor, c.label);
        for (const auto& c : spkyhw::kOutputCtls)
            text(c.lbl.x, c.lbl.y, c.lblSize, 0.f, c.lblRgb, c.anchor, c.label);
        for (const auto& t : spkyhw::kPanelTexts)
            text(t.mm.x, t.mm.y, t.size, t.spacing, t.rgb, t.anchor, t.str);
    }
};

struct FireflowHWWidget : ModuleWidget {
    FireflowHWWidget(Fireflow* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/FireflowHW.svg")));
        auto* labels = new HwPanelText();
        labels->box.size = box.size;
        addChild(labels);
        for (const auto& c : spkyhw::kParamCtls) {
            Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            switch (c.kind) {
                case WK_BIGKNOB: case WK_KNOBC:
                    addParam(createParamCentered<RoundBlackKnob>(pos, module, c.id)); break;
                case WK_SMKNOB: case WK_KNOBI:
                    if (c.id == ATTACK_A || c.id == ATTACK_B
                            || c.id == STAGES_A || c.id == STAGES_B) {
                        auto* knob = createParamCentered<SlotVisible<Trimpot>>(pos, module, c.id);
                        knob->fireflow = module;
                        knob->ctlId = c.id;
                        addParam(knob);
                    } else {
                        addParam(createParamCentered<Trimpot>(pos, module, c.id));
                    }
                    break;
                case WK_SW2:
                    addParam(createParamCentered<CKSS>(pos, module, c.id)); break;
                case WK_LATCH:
                    if (c.id == ENGINE_A || c.id == ENGINE_B)
                        addParam(createParamCentered<EngineCycleLatch>(pos, module, c.id));
                    else if (c.id == REC_A || c.id == REC_B) {
                        auto* pad = createParamCentered<SlotVisible<VCVLatch>>(pos, module, c.id);
                        pad->fireflow = module;
                        pad->ctlId = c.id;
                        addParam(pad);
                    } else
                        addParam(createParamCentered<VCVLatch>(pos, module, c.id));
                    break;
                case WK_SMBTN:
                    addParam(createParamCentered<VCVButton>(pos, module, c.id)); break;
                default: break;
            }
        }
        for (const auto& c : spkyhw::kInputCtls)
            addInput(createInputCentered<PJ301MPort>(mm2px(Vec(c.mm.x, c.mm.y)), module, c.id));
        for (const auto& c : spkyhw::kOutputCtls)
            addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(c.mm.x, c.mm.y)), module, c.id));
        for (const auto& c : spkyhw::kLightCtls) {
            Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            if (c.id == REC_A_L || c.id == REC_B_L) {
                auto* led = createLightCentered<SamplerOnly<SmallLight<RedLight>>>(pos, module, c.id);
                led->fireflow = module;
                led->engineId = (c.id == REC_A_L) ? ENGINE_A : ENGINE_B;
                addChild(led);
            } else
                addChild(createLightCentered<MediumLight<YellowLight>>(pos, module, c.id));
        }
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    }
    void appendContextMenu(Menu* menu) override {
        appendFireflowMenu(menu, getModule<Fireflow>());
    }
};

Model* modelFireflowHW = createModel<Fireflow, FireflowHWWidget>("FireflowHW");
```

Anmerkung für den Implementierer: falls `HwPanelText` von der Signatur des bestehenden `PanelText` abweichen muss (Font-Handle, `mm2px`-Helfer), am realen `PanelText`-Code in `Fireflow.cpp` orientieren — er ist der bewiesene Weg, NanoSVG-lose Beschriftung zu zeichnen. Die HW-Fassung zeichnet **immer alle** Labels (statisch ist der Punkt), einzig leere Labels (`""`) werden übersprungen.

- [ ] **Step 5: Bauen**

```bash
cd host/vcv && ./build-local.sh
```

Erwartung: Build PASS, Plugin installiert. Bei Compile-Fehlern in `generated_hw_panel.hpp` zuerst prüfen, ob Task 4 die `spkyvcv`-Typen exakt wiederverwendet (kein eigenes `PanelCtl`).

- [ ] **Step 6: Python-Tests gesamt**

```bash
cd host/vcv && python -m pytest res/ -q
```

Erwartung: alles grün — `test_panel.py`, `test_flow_panel.py`, `test_hw_panel.py`, Factory-WAV-Tests.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/plugin.json host/vcv/src/plugin.hpp host/vcv/src/plugin.cpp host/vcv/src/Fireflow.cpp
git commit -m "vcv(hw): a third module rehearses the aluminium before the aluminium exists

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

- [ ] **Step 8: Hand-Check-Liste für Bastian hinterlassen (kein Agent hat ein Rack)**

Als Abschluss-Notiz im Task-Report festhalten — nicht committen:

1. Modul „FireFlow HW Draft" erscheint im Browser und instanziert auf 60 HP.
2. Alle Knobs greifen dieselben Parameter wie das Hauptmodul (ein Patch, beide Module nebeneinander, Werte vergleichen).
3. Beschriftung bleibt bei Engine-Wechsel **statisch** (ATK bleibt ATK, auch auf BODY/BBD); der ATK/BEND-Knopf wechselt sein Widget wie im Hauptmodul.
4. REC-Pad + LED erscheinen nur auf dem Sampler.
5. Keine LED-Ringe, nirgends.

---

### Task 6: Website — Roadmap-Sektion und Eckpunkte nachziehen

**Achtung: anderes Repo.** Arbeitsverzeichnis ist `C:\Users\bernd\Documents\AI\FireFlow_Website` (Branch `master`, kein Remote, kein Push). Commit-Stil dort: knappe englische `docs:`-Zeilen im Stil des bestehenden Logs, gleicher HAL-9000-Trailer.

**Files:**
- Modify: `index.html` — Roadmap-Sektion (`<section class="roadmap" id="roadmap">`, ab ca. Zeile 208): SVG-Chart, `roadmap-marks`, `roadmap-gate` (Entscheidungs-Kasten „Decision · 4 Sep 2026", ca. Zeile 379–421)
- Prüfen (grep `42 HP`, `42hp`, `reduction`, `4 Sep`): `instrument.html`, `prototypes.html` — Treffer, die den alten Stand behaupten, mitziehen

**Interfaces:**
- Consumes: die Fakten der Envelope-Spec — **nur** diese; keine erfundenen Zahlen
- Produces: nichts Programmatisches; Task 7 setzt auf dem aktualisierten Stand auf (gleiche Datei, deshalb strikt nacheinander)

**Fakten, die die Sektion danach erzählt:**
1. Die Entscheidung vom 4. September ist **vorzeitig am 8. August gefallen**: der Macro-Weg (Glow als Hardware) ist raus — zu random, das Tuning griff nicht. Glow bleibt ein VCV-Modul. Die Hardware wird das **volle Instrument: beide Decks, 60 HP, jede Funktion mit eigenem Bedienelement** (82 Runtime-Parameter auf 80 Positionen). Der Gate-Kasten wird von „offene Entscheidung mit zwei Ausgängen" zu „entschieden, so ging es aus" umgebaut — die beiden Ausgangs-Karten werden durch das Ergebnis ersetzt.
2. Die LED-Kränze sind gestrichen (Strom, BOM, libDaisy-Fork — ausdrücklich nicht CPU).
3. Der Zeitplan wird ehrlich: H1/H2/H3 bleiben als Kette samt Logik (CNY-Regel), aber die Daten werden nach dem Panel-Freeze neu verankert; der 23. April ist **Zielkorridor**, kein Termin. Die SVG-Marke „4 SEP 2026" wird zu einer „DECIDED 8 AUG"-Marke; das `aria-label` des Charts zieht mit (kein visueller Umbau des Charts nötig — Balken bleiben, nur die Gate-Beschriftung und der Fließtext ändern sich).

- [ ] **Step 1:** Gate-Kasten, Chart-Beschriftung und `aria-label` umbauen; Fließtexte der Roadmap-Sektion auf 60 HP/Korridor umstellen
- [ ] **Step 2:** `grep -n "42 HP\|42hp\|4 Sep\|4 September\|reduction" index.html instrument.html prototypes.html` — jeden Treffer entscheiden: nachziehen oder er beschreibt Historie (Diary-Einträge sind Historie und bleiben unangetastet)
- [ ] **Step 3:** `bash tools/check-links.sh` und `bash tools/check-words.sh` falls lauffähig; sonst HTML-Sichtprüfung der geänderten Abschnitte
- [ ] **Step 4:** Commit im Website-Repo: `docs: the gate closed early and the instrument stays whole` + HAL-Trailer

### Task 7: Dev-Diary-Eintrag „Tofu is dead"

**Gleiche Datei, gleiches Repo wie Task 6 — strikt danach ausführen.**

**Files:**
- Modify: `index.html` — neuer Diary-Eintrag ZUOBERST der Einträge (das `id="diary-latest"`-Anker-Muster des bisherigen neuesten Eintrags übernehmen und den Anker auf den neuen Eintrag verschieben), `diary-stats` (Entry-Zähler +1), `diary-meta-note` „Latest: …", `hero-status-note` (ca. Zeile 182), `nav-latest` (Zeile 34)

**Interfaces:**
- Consumes: Envelope-Spec-Fakten + der bestehende Eintrag „Glow, alpha 0.1 — it makes sound, and sometimes it makes tofu" (der Aufhänger knüpft daran an)
- Produces: den Abschluss der Website-Arbeit

**Inhaltsvorgabe (vom Auftraggeber):** Titel-Hook **„Tofu is dead"**. So knapp wie möglich, mit Humor: **max. ~250 Wörter, max. 3 Absätze** (Diary-Regel), Englisch wie die übrigen Einträge, `diary-stamp` im Muster der Nachbarn (z. B. `envelope spec · 60 HP · 8 Aug 2026`). Der Eintrag trägt diese Fakten und keine anderen:
- Drei Panelgrößen wurden durchprobiert (12, 18, 42 HP) und alle waren Kompromisse; die Entscheidung ist das volle Instrument auf 60 HP — Tofu (der Macro-Hardware-Traum) ist tot, Glow lebt als VCV-Modul weiter.
- Zwei unabhängige adversariale Reviews haben den ersten Entwurf zerlegt: nur 4 rohe ADC-Pins statt 6, 56 HP war schöngerechnet, und die WS2812-CPU-Angst war ein Mythos (der Treiber war immer DMA) — die Kränze sterben trotzdem, für Strom, BOM und den libDaisy-Fork.
- Der SD-Slot muss 4-bit verdrahtet werden, weil der Daisy-Bootloader dann Firmware per Drag & Drop flasht — ein Gerät ohne SWD-Pins bekommt Updates von der Karte.
- Der 23. April ist jetzt ein Korridor, kein Termin: das richtige Instrument schlägt die Messe.

- [ ] **Step 1:** Eintrag schreiben und einbauen (Anker, Zähler, Latest-Zeilen, Nav)
- [ ] **Step 2:** Wortzahl prüfen (≤ ~250), Zähler-Konsistenz prüfen (`data-diary-count-entries` = reale Eintragszahl)
- [ ] **Step 3:** Commit im Website-Repo: `docs: tofu is dead, long live the whole instrument` + HAL-Trailer

## Self-Review (ausgefüllt)

1. **Spec-Abdeckung:** Alle fünf Zeilen der Nachzieh-Tabelle der Envelope-Spec sind abgedeckt (Roadmap-M6 → Task 1; Phase-0 Task 2 + Schritt 5 → Task 2; Memory → Task 3; Kopfvermerk → Task 1). §4-Hardware-Modus → Tasks 4+5. Die Coupon-/Notausgang-/Kosten-Punkte der Spec sind Hardware-Phasen-Inhalte ohne Code-Anteil und bleiben absichtlich außerhalb dieses Plans.
2. **Platzhalter:** Die `/* … */`-Stellen in Task 4 Schritt 3 (Header-Emit) benennen exakt das wiederzuverwendende `emit_table`-Format aus `gen_panel.header()` — Verweis auf existierenden Repo-Code, kein TBD. Task 5 Schritt 3 zitiert den zu hebenden Body per Zeilenbereich.
3. **Typ-Konsistenz:** `spkyhw` nutzt ausschließlich `spkyvcv`-Typen und -Ids; `HW_PARAMS`/`ALL_HW`/`KEEP_TOP`/`KEEP_BOT`/`HP`/`W` heißen in Generator und Tests identisch.
