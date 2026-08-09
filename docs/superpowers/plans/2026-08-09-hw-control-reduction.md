# Bedienelement-Reduktion — Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Das VCV-Panel von 82 Parametern auf 68 bringen, indem sieben
Bedienelement-Paare zu Zonen- und Rastknöpfen verschmelzen, drei Werte fest
werden und zwei Funktionen sterben — spielbar in Rack am Ende jeder Task.

**Architecture:** `host/vcv/res/gen_panel.py` ist die einzige Quelle der
ParamId-Reihenfolge; alles andere (`generated_panel.hpp`,
`generated_hw_panel.hpp`, das HW-Draft-Panel) wird daraus erzeugt.
Verhaltensänderungen liegen entweder in der Engine (doctest in `tests/`) oder
in `Fireflow.cpp`s `pushParams()`/`configControls()` (Quelltext-scrapende
Guards in `res/test_panel.py`, dem Hausmuster). Jede Task ist ein
Bedienelement, end-to-end.

**Tech Stack:** C++17 (clang+Ninja fürs Engine-Ziel, MinGW GCC 15.2 für das
VCV-Plugin), Python 3 für Panel-Generator und Panel-Guards, doctest für
Engine-Tests, VCV Rack SDK 2.6.6.

## Global Constraints

- **Grundlage ist die Spec** `docs/superpowers/specs/2026-08-09-hw-control-reduction-design.md`. Bei Widerspruch gewinnt die Spec.
- **Die Änderungen landen im geteilten `gen_panel.py`**, also bekommen **beide** Module den reduzierten Satz — `Fireflow` (das gespielte Modul) und `FireflowHW` (der 60-HP-Entwurf). Das ist Absicht: Bastian will den neuen Satz in Rack spielen.
- **Patch-Kompatibilität ist ausdrücklich kein Thema.** ParamIds dürfen frei verrutschen (Memory `fireflow-dev-alpha-no-patch-compat`). Es werden **keine** Migrationen geschrieben.
- **Engine-Build:** `source env.sh` und dann `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. `-DCMAKE_BUILD_TYPE=Release` ist **nicht optional** — ohne den Flag scheitern `spky_tests` und `ctrl_identity` mit „SYNTH reference moved".
- **VCV-Build:** immer `host/vcv/build-local.sh`, **nie** direkt `g++` oder `make`. Das System-`g++` auf dieser Maschine ist der Daisy-ARM-Cross-Compiler und stirbt mit „MinGW not found" (Memory `spotykach-vcv-host-build-env`).
- **Niemals `source env.sh` in einer Shell, die danach `make` für Daisy aufruft.** Die beiden Toolchains dürfen sich nicht mischen. Dieser Plan fasst Daisy nicht an.
- **Nach jeder Änderung an `res/gen_panel.py`** aus `host/vcv/` heraus regenerieren: `python3 res/gen_panel.py && python3 res/gen_flow_panel.py && python3 res/gen_hw_panel.py`.
- **Ein Test, der nicht rot werden kann, wird repariert.** Jeder Test wird einmal rot vorgeführt, bevor die Implementierung kommt (Memory `spotykach-tests-must-be-able-to-fail`).
- **Commit-Trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>` — nicht der Anthropic-Standard.
- **Keine Bit-Exaktheits-Gates.** Renders sind Plausibilitätsprüfungen, keine Prüfsummen.
- **Nicht anfassen:** die Umgruppierung der Knopfpositionen. Der Plan ändert *welche* Bedienelemente existieren, nicht *wo* sie sitzen. Positionen werden nur so weit angepasst, wie das Entfernen eines Nachbarn es erzwingt.

## Standard-Verifikationsblock

Mehrere Tasks enden mit derselben Prüfung. Wo unten „**Vollprüfung**" steht, ist genau das gemeint:

```bash
cd /c/Users/bernd/Documents/AI/FireFlow/host/vcv
python3 res/gen_panel.py && python3 res/gen_flow_panel.py && python3 res/gen_hw_panel.py
python3 res/test_panel.py && python3 res/test_flow_panel.py && python3 res/test_hw_panel.py
./build-local.sh
```

Erwartet: die drei Generatoren melden ihre Zählungen, die drei Guard-Läufe
enden je mit `PASS`, und `build-local.sh` erzeugt `plugin.dll` ohne Fehler.

Und für die Engine (eigene Shell, wegen `env.sh`):

```bash
cd /c/Users/bernd/Documents/AI/FireFlow
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
```

## Datei-Landkarte

| Datei | Rolle | Tasks |
|---|---|---|
| `host/vcv/res/gen_panel.py` | Quelle der ParamId-Reihenfolge, Labels, Tooltips, Geometrie | alle |
| `host/vcv/res/test_panel.py` | Panel- und Host-Wiring-Guards (scrapt `Fireflow.cpp`) | alle |
| `host/vcv/src/Fireflow.cpp` | `configControls()` (Ranges/Quantities) und `pushParams()` (Engine-Aufrufe) | alle |
| `host/vcv/src/init_patch.hpp` | Init-Defaults, heute ein positionsindiziertes Array | 1, dann automatisch |
| `host/vcv/res/gen_hw_panel.py` | 60-HP-Entwurf, importiert `gen_panel` | 11 |
| `engine/mod/song_ladder.h` | **neu** — die kuratierte FORM×SONG-Leiter | 3 |
| `engine/center/center.h/.cpp` | MORPH-Gains, COUPLE, DRIFT, SETTLE | 5, 7, 8 |
| `engine/synth/synth_engine.h` | `kDetuneCeilCt` | 10 |
| `engine/body/body_voice.cpp` | `kDetuneScale` (Gegenkompensation) | 10 |
| `engine/sampler/sampler_config.h` | eigene Detune-Decke, nur Kommentar | 10 |
| `tests/test_song_ladder.cpp` | **neu** | 3 |
| `tests/test_center.cpp` | Center-Verhalten | 5, 7, 8 |
| `tests/test_synth_engine.cpp` | Detune-Spread | 10 |

## Reihenfolge und warum

Task 1 ist ein Enabler: `init_patch.hpp` ist heute ein positionsindiziertes
Array **plus** eine Kopie derselben Zahlen als Literal in
`test_panel.py::test_sampler_preset_init_snapshot`. Ohne Task 1 müsste jede
der zehn folgenden Tasks beide von Hand umsortieren — zehn Gelegenheiten, eine
Zeile zu verschieben und es nicht zu merken. Danach ist die Reihenfolge
2 → 11 frei wählbar; sie ist so sortiert, dass die hörbarsten Änderungen früh
kommen.

---

### Task 1: Init-Defaults werden namensbasiert und generiert

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (neuer `INIT_DEFAULTS`-Dict + Emitter)
- Modify: `host/vcv/src/init_patch.hpp` (wird ab jetzt generiert)
- Modify: `host/vcv/res/test_panel.py:1985` (`test_sampler_preset_init_snapshot`)

**Interfaces:**
- Consumes: nichts.
- Produces: `INIT_DEFAULTS: dict[str, float]` in `gen_panel.py`, Schlüssel sind
  die `Ctl.enum`-Namen. `gen_panel.py` schreibt `src/init_patch.hpp` mit
  unverändertem C++-Interface: `static constexpr float kInitParamDefaults[]`,
  `static_assert` auf `NUM_PARAMS`, `inline float initParamDefault(int id)`.
  Alle späteren Tasks fügen nur noch Dict-Einträge hinzu oder entfernen sie.

- [ ] **Step 1: Den Guard schreiben, der die Doppelpflege verbietet**

In `host/vcv/res/test_panel.py` ans Ende der Testfunktionen:

```python
def test_init_defaults_are_generated_from_names():
    """init_patch.hpp is emitted from INIT_DEFAULTS, keyed by param name, so
    removing a param cannot leave a stale positional value behind."""
    import gen_panel as gp
    here = os.path.dirname(os.path.abspath(__file__))
    header = open(os.path.join(here, "..", "src", "init_patch.hpp")).read()
    check("GENERATED by res/gen_panel.py" in header,
          "init_patch.hpp is not marked generated")
    missing = [c.enum for c in gp.PARAMS if c.enum not in gp.INIT_DEFAULTS]
    check(not missing, f"params without an INIT_DEFAULTS entry: {missing}")
    extra = [k for k in gp.INIT_DEFAULTS if k not in {c.enum for c in gp.PARAMS}]
    check(not extra, f"INIT_DEFAULTS entries for params that do not exist: {extra}")
    for c in gp.PARAMS:
        line = f"{gp.INIT_DEFAULTS[c.enum]!r}f, // {c.enum}"
        check(f"// {c.enum}\n" in header or f"// {c.enum}" in header,
              f"{c.enum} missing from the emitted table")
```

- [ ] **Step 2: Rot vorführen**

```bash
cd /c/Users/bernd/Documents/AI/FireFlow/host/vcv && python3 res/test_panel.py
```

Erwartet: FAIL mit „init_patch.hpp is not marked generated" (der Header ist
heute handgepflegt und trägt die Marke nicht).

- [ ] **Step 3: `INIT_DEFAULTS` aus dem heutigen Header ableiten**

Einmalig aus `src/init_patch.hpp` erzeugen — die Werte tragen dort bereits
ihren Namen als Kommentar:

```bash
cd /c/Users/bernd/Documents/AI/FireFlow/host/vcv
python3 - <<'PY' > /tmp/init_defaults.py
import re
src = open("src/init_patch.hpp").read()
body = re.search(r"kInitParamDefaults\[\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
print("INIT_DEFAULTS = {")
for line in body.splitlines():
    m = re.match(r"\s*([-0-9.eE]+)f?,\s*//\s*(\w+)", line)
    if m:
        print(f'    "{m.group(2)}": {m.group(1)},')
print("}")
PY
head -3 /tmp/init_defaults.py && wc -l /tmp/init_defaults.py
```

Erwartet: 86 Zeilen (84 Werte + Klammern). Sollten Zeilen ohne
Namenskommentar existieren, werden sie hier sichtbar, weil die Zählung nicht
aufgeht — dann von Hand ergänzen, **nicht** raten.

- [ ] **Step 4: Dict in `gen_panel.py` einsetzen**

Den erzeugten Block direkt vor `RUNTIME_PANEL_PARAMS = ...` einfügen (er darf
erst nach `PARAMS` stehen — also unmittelbar nach der `PARAMS = ...`-Zeile),
mit diesem Kopf:

```python
# Approved init snapshot, keyed by param NAME rather than by position: adding
# or removing a control must not be able to shift somebody else's default.
# Provenance unchanged -- drone.vcvm (2026-07-28), with LINK_B zeroed and
# STAGES_B's 1.0 deliberately kept (see the notes that used to live in
# src/init_patch.hpp).
INIT_DEFAULTS = {
    ...
}
```

- [ ] **Step 5: Den Emitter schreiben**

In `gen_panel.py`, neben die anderen `write`-Aufrufe in `__main__`:

```python
def init_patch_header():
    L = ["// GENERATED by res/gen_panel.py -- do not edit by hand.",
         "#pragma once", "", "namespace spkyvcv {", "",
         "static constexpr float kInitParamDefaults[] = {"]
    for c in PARAMS:
        L.append(f"    {INIT_DEFAULTS[c.enum]:.9g}f, // {c.enum}")
    L.append("};")
    L.append("static_assert(sizeof(kInitParamDefaults) / "
             "sizeof(kInitParamDefaults[0])")
    L.append("              == NUM_PARAMS, "
             '"init snapshot must cover every ParamId");')
    L.append("")
    L.append("inline float initParamDefault(int id) {")
    L.append("    return kInitParamDefaults[id];")
    L.append("}")
    L.append("")
    L.append("} // namespace spkyvcv")
    return "\n".join(L) + "\n"
```

und im `__main__`-Block schreiben lassen:

```python
    with open(os.path.join(root, "src", "init_patch.hpp"), "w") as f:
        f.write(init_patch_header())
```

Achtung: der generierte Header referenziert `NUM_PARAMS` aus
`generated_panel.hpp`. Prüfen, ob `init_patch.hpp` dort bereits nach
`generated_panel.hpp` inkludiert wird; falls nicht, `#include
"generated_panel.hpp"` in den Emitter aufnehmen.

- [ ] **Step 6: Den alten Snapshot-Test auf Namen umstellen**

`test_sampler_preset_init_snapshot` in `res/test_panel.py` prüft heute eine
Literalliste in Positionsreihenfolge. Die Liste bleibt inhaltlich, wird aber
gegen `gp.INIT_DEFAULTS` geprüft statt gegen die Datei — Werte, deren Param
verschwindet, fallen mit ihrem Param weg statt einen Index zu verschieben.
Den Funktionsrumpf ersetzen durch:

```python
def test_sampler_preset_init_snapshot():
    """The approved init values, pinned by NAME. A control that leaves the
    panel takes its entry with it; nothing below it moves."""
    import gen_panel as gp
    approved = {
        # generated once by the command in the plan's Step 6; a second,
        # independent copy of the same numbers -- that is what makes this a test
        "RATE_A": 0.116716892,
        ...
    }
    for name, want in approved.items():
        if name not in gp.INIT_DEFAULTS:
            continue          # control retired by a later task -- see the plan
        check(abs(gp.INIT_DEFAULTS[name] - want) < 1e-6,
              f"{name} init default drifted: {gp.INIT_DEFAULTS[name]} != {want}")
```

Die `approved`-Tabelle wird mechanisch erzeugt, nicht abgetippt — dieselben
Zahlen, andere Datei, damit der Test überhaupt etwas prüfen kann:

```bash
cd /c/Users/bernd/Documents/AI/FireFlow/host/vcv
python3 - <<'PY'
import sys; sys.path.insert(0, "res")
import gen_panel as gp
for k, v in gp.INIT_DEFAULTS.items():
    print(f'        "{k}": {v!r},')
PY
```

Die Ausgabe ersetzt das `...` im Testrumpf. Erwartet: 84 Zeilen.

- [ ] **Step 7: Regenerieren und grün prüfen**

```bash
cd /c/Users/bernd/Documents/AI/FireFlow/host/vcv
python3 res/gen_panel.py && python3 res/test_panel.py
git diff --stat src/init_patch.hpp
```

Erwartet: `PASS`, und der Diff an `init_patch.hpp` enthält **keine
Wertänderung** — nur Kopfzeile und Formatierung. Falls ein Wert kippt, ist
Step 3 schiefgegangen; nicht weitermachen.

- [ ] **Step 8: Vollprüfung und Commit**

Vollprüfung (siehe oben), dann:

```bash
cd /c/Users/bernd/Documents/AI/FireFlow
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/src/init_patch.hpp
git commit -m "vcv: the init snapshot learns its parameters by name

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: `STEPS` schluckt den `STEP`-Taster

**Files:**
- Modify: `host/vcv/res/gen_panel.py:301-305` (Pad-Liste), `:294` (STEPS-Ctl)
- Modify: `host/vcv/src/Fireflow.cpp` (`configControls` STEPS-Zweig, `pushParams` `set_step`)
- Modify: `host/vcv/res/test_panel.py`

**Interfaces:**
- Consumes: `INIT_DEFAULTS` (Task 1).
- Produces: `STEPS_A/B` ist ein `KNOBI` mit Range `0..16`, wobei 1 nie erzeugt
  wird. Host-Kontrakt: `inst.set_step(p, steps > 0, steps)`. `STEP_A/B`
  existiert nicht mehr.

- [ ] **Step 1: Den Guard schreiben**

In `res/test_panel.py`:

```python
def test_steps_knob_carries_the_mode():
    """STEP's pad is gone: 0 on the STEPS knob IS flow mode, and the host
    derives the boolean from the count instead of reading a second control."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    check("STEP_A" not in names and "STEP_B" not in names,
          "STEP pads still exist")
    steps = [c for c in gp.PARAMS if c.enum == "STEPS_A"]
    check(len(steps) == 1 and steps[0].kind == gp.KNOBI,
          "STEPS_A missing or no longer an integer knob")
    cpp = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "src", "Fireflow.cpp")).read()
    check("configParam(c.id, 0.f, 16.f" in cpp or
          "0.f, 16.f, initParamDefault" in cpp,
          "STEPS is not configured over 0..16")
    check("inst.set_step(p, steps > 0, steps)" in cpp,
          "set_step no longer derives its mode from the count")
    check("STEP_A" not in cpp, "Fireflow.cpp still references STEP_A")
```

- [ ] **Step 2: Rot vorführen**

```bash
cd /c/Users/bernd/Documents/AI/FireFlow/host/vcv && python3 res/test_panel.py
```

Erwartet: FAIL mit „STEP pads still exist".

- [ ] **Step 3: Das Pad aus dem Generator nehmen**

In `res/gen_panel.py`, in `part_controls()`, die `pads`-Liste kürzen:

```python
    pads = [("ENGINE", LATCH, "ENG", None),
            ("GRITMODE", LATCH, dynamic_words("GRITMODE")[0], "Grit mode")]
```

`("STEP", LATCH, "STEP", None)` entfällt ersatzlos. Der freigewordene
`PAD_X[2]`-Platz bleibt vorerst leer — Positionen sind Sache der
Umgruppierungs-Session, nicht dieses Plans.

Und in `INIT_DEFAULTS` die beiden Zeilen `"STEP_A": ...` und `"STEP_B": ...`
löschen.

- [ ] **Step 4: Host-Range und -Aufruf umstellen**

In `Fireflow.cpp`, `configControls()`, im `else  // STEPS_A / STEPS_B`-Zweig
(um `:355`) die Range von `2.f, 16.f` auf `0.f, 16.f` ändern. Die Rastung
liefert `KNOBI` bereits; damit sind die Positionen 0 und 2..16 erreichbar, die
1 wird durch die Leiter nie getroffen — das ist gewollt und entspricht dem
heutigen Verhalten.

In `pushParams()` (um `:714`):

```cpp
            const int steps = (int)std::round(pp(STEPS_A, p));
            inst.set_step(p, steps > 0, steps);
```

Die alte Zeile `inst.set_step(p, ppb(STEP_A, p), (int)std::round(pp(STEPS_A, p)));`
entfällt.

- [ ] **Step 5: Regenerieren, Guards, Build**

Vollprüfung. Erwartet: `PASS` und ein sauberer `plugin.dll`-Build. Der
`static_assert` auf `PART_STRIDE` in `Fireflow.cpp:205-210` bleibt gültig — er
prüft nur, dass A- und B-Block gleich lang sind, und beide verlieren `STEP`.

- [ ] **Step 6: Commit**

```bash
cd /c/Users/bernd/Documents/AI/FireFlow
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py \
        host/vcv/src/Fireflow.cpp host/vcv/src/generated_panel.hpp \
        host/vcv/src/generated_hw_panel.hpp host/vcv/src/init_patch.hpp \
        host/vcv/res/Fireflow.svg host/vcv/res/FireflowHW.svg
git commit -m "vcv: the step count says the mode out loud, so the pad stops repeating it

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: `SONG` schluckt `FORM` und `NEW`

**Files:**
- Create: `engine/mod/song_ladder.h`
- Create: `tests/test_song_ladder.cpp`
- Modify: `CMakeLists.txt` (neue Testdatei registrieren)
- Modify: `host/vcv/res/gen_panel.py` (FORM/NEWPHRASE raus, SONG-Range)
- Modify: `host/vcv/src/Fireflow.cpp`
- Modify: `host/vcv/res/test_panel.py`

**Interfaces:**
- Consumes: `Instrument::set_form(int, int)`, `set_song(int, int)`,
  `new_phrase(int)` — unverändert.
- Produces:
  ```cpp
  namespace spky {
  struct SongRung { uint8_t form; uint8_t song; };
  inline constexpr int kSongLadderCount = 14;
  const SongRung& song_ladder_at(int idx);          // clamped
  int hyst_step(int cur, float norm, int count);    // 0..count-1
  }
  ```
  `hyst_step` ist die freistehende Zwillingsfunktion zu `Flow::quantize_hyst`
  (`engine/flow/flow.cpp:234`) für Regler, die nicht durch die Flow-Schicht
  laufen; sie hält `cur`, bis der Wert die Naht um eine halbe Stufe
  überschreitet.

- [ ] **Step 1: Den Engine-Test schreiben**

`tests/test_song_ladder.cpp`:

```cpp
#include "doctest.h"
#include "mod/song_ladder.h"
#include "mod/song_form.h"
#include "mod/phrase_gen.h"

using namespace spky;

TEST_CASE("song ladder covers every rung with legal enum values") {
    for (int i = 0; i < kSongLadderCount; ++i) {
        const SongRung& r = song_ladder_at(i);
        CHECK(r.form < static_cast<uint8_t>(Principle::kCount));
        CHECK(r.song < static_cast<uint8_t>(SongMode::kCount));
    }
}

TEST_CASE("rung 0 is the one that does not alternate") {
    CHECK(song_ladder_at(0).song == static_cast<uint8_t>(SongMode::Off));
}

TEST_CASE("no rung repeats another rung") {
    for (int i = 0; i < kSongLadderCount; ++i)
        for (int j = i + 1; j < kSongLadderCount; ++j)
            CHECK_FALSE(song_ladder_at(i).form == song_ladder_at(j).form &&
                        song_ladder_at(i).song == song_ladder_at(j).song);
}

TEST_CASE("the ladder clamps instead of reading out of bounds") {
    CHECK(song_ladder_at(-5).form == song_ladder_at(0).form);
    CHECK(song_ladder_at(999).form == song_ladder_at(kSongLadderCount - 1).form);
}

TEST_CASE("hysteresis holds a rung until the value clears the seam") {
    // 14 rungs span x = 0..13. Holding rung 3 means the value must pass 4.0
    // (a full step beyond the rung, = flow.cpp's 0.5 seam + 0.5 kHysteresisFrac)
    // before anything moves. norm = x / 13.
    const int   n = kSongLadderCount;
    const float d = 1.f / float(n - 1);
    CHECK(hyst_step(3, 3.4f * d, n) == 3);   // drifting inside the rung: hold
    CHECK(hyst_step(3, 3.9f * d, n) == 3);   // past the seam but not the guard
    CHECK(hyst_step(3, 4.2f * d, n) == 4);   // clears the guard: move
    CHECK(hyst_step(3, 1.8f * d, n) == 2);   // and the same downward
}

TEST_CASE("a large jump lands in one move, not one rung at a time") {
    CHECK(hyst_step(0, 1.f, kSongLadderCount) == kSongLadderCount - 1);
}
```

- [ ] **Step 2: Rot vorführen**

```bash
cd /c/Users/bernd/Documents/AI/FireFlow && source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build 2>&1 | tail -20
```

Erwartet: Compilerfehler `mod/song_ladder.h: No such file or directory`.
(Falls die Datei nicht gebaut wird, fehlt der Eintrag in `CMakeLists.txt` —
dann erst registrieren, dann erneut rot vorführen.)

- [ ] **Step 3: Den Header schreiben**

`engine/mod/song_ladder.h`:

```cpp
#pragma once

#include <cstdint>
#include "mod/phrase_gen.h"
#include "mod/song_form.h"

namespace spky {

// The hardware SONG knob is one axis through the 5x7 (Principle, SongMode)
// grid -- 35 combinations do not fit on a 9 mm pot and nobody learns them.
// The path runs tame -> churning, modelled on M_WANDER (engine/flow/taste.h:891),
// which already sweeps FORM and SONG together and is tuned in Glow.
//
// One deliberate difference from M_WANDER: it excludes SongMode::Off because a
// WANDER macro must never disable wandering. This knob is a structure SELECTOR,
// not a wander amount, so "no alternation at all" is a legitimate destination
// and owns rung 0.
//
// This table is TASTE. The tests below pin its structure (legal values, no
// duplicates, Off at rung 0) and deliberately do NOT pin the order -- retune it
// by ear without fighting a test.
struct SongRung { uint8_t form; uint8_t song; };

inline constexpr int kSongLadderCount = 14;

inline const SongRung& song_ladder_at(int idx) {
    static constexpr SongRung kLadder[kSongLadderCount] = {
        {0, 6}, {1, 6},                  // no alternation, two generators
        {0, 0}, {1, 0},                  // AAAB: the sparsest alternation
        {0, 1}, {1, 1},                  // ABAB
        {2, 0}, {2, 1}, {2, 2},          // hierarchical, opening up
        {3, 1}, {3, 3}, {3, 4},          // call/response, then Build, Rotate
        {4, 4}, {4, 5},                  // ostinato against Rotate, then Mirror
    };
    if (idx < 0) idx = 0;
    if (idx >= kSongLadderCount) idx = kSongLadderCount - 1;
    return kLadder[idx];
}

// Free-standing twin of Flow::quantize_hyst (engine/flow/flow.cpp:234) for
// controls that do not run through the flow layer. Without it a pot parked on
// a seam re-quantises every tick and the engine gets a new FORM/SONG dozens of
// times a minute -- flow.cpp's own comment measured 14 flips over one hover
// sweep. Hold `cur` until the value passes the seam by a further half step,
// then snap to whatever step is nearest, so a big turn still lands in one move.
inline int hyst_step(int cur, float norm, int count) {
    if (count < 2) return 0;
    if (norm < 0.f) norm = 0.f;
    if (norm > 1.f) norm = 1.f;
    const float x = norm * static_cast<float>(count - 1);
    int nearest = static_cast<int>(x + 0.5f);
    if (nearest < 0) nearest = 0;
    if (nearest > count - 1) nearest = count - 1;
    const float n = static_cast<float>(cur);
    if (x > n + 1.0f || x < n - 1.0f) return nearest;
    return cur;
}

} // namespace spky
```

- [ ] **Step 4: Test registrieren und grün prüfen**

`tests/test_song_ladder.cpp` in `CMakeLists.txt` neben die anderen
`tests/test_*.cpp` eintragen (dem dortigen Muster folgen — falls die Liste
per Glob entsteht, ist nichts zu tun).

```bash
cd /c/Users/bernd/Documents/AI/FireFlow && source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
```

Erwartet: alle Tests grün, inklusive der sechs neuen Fälle.

- [ ] **Step 5: Den Panel-Guard schreiben**

In `res/test_panel.py`:

```python
def test_song_knob_swallows_form_and_new():
    """FORM and the NEW pad are gone; SONG walks the curated ladder and
    re-rolls the phrase on every rung change."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    for dead in ("FORM_A", "FORM_B", "NEWPHRASE_A", "NEWPHRASE_B"):
        check(dead not in names, f"{dead} still exists")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("song_ladder.h" in cpp, "the host does not include the ladder")
    check("spky::hyst_step(" in cpp, "the host does not debounce the SONG pot")
    check("inst.new_phrase(p)" in cpp, "the host never re-rolls")
    check("FORM_A" not in cpp, "Fireflow.cpp still references FORM_A")
```

- [ ] **Step 6: Rot vorführen**

```bash
cd /c/Users/bernd/Documents/AI/FireFlow/host/vcv && python3 res/test_panel.py
```

Erwartet: FAIL mit „FORM_A still exists".

- [ ] **Step 7: Generator ändern**

In `part_controls()` die beiden Zeilen für `FORM` und `NEWPHRASE` löschen und
`SONG` als einzigen Rest behalten:

```python
    out.append(Ctl("SONG", KNOBI, fx(PAD_X[4]), PLAY_Y, "SONG"))
```

In `INIT_DEFAULTS` die vier Zeilen `FORM_A`, `FORM_B`, `NEWPHRASE_A`,
`NEWPHRASE_B` löschen.

- [ ] **Step 8: Host verdrahten**

In `Fireflow.cpp` oben `#include "mod/song_ladder.h"` ergänzen, im
`Fireflow`-Struct zwei Latches:

```cpp
    int songRung[spky::PART_COUNT] = {0, 0};
```

`configControls()`: der `SONG_A/SONG_B`-Zweig (`:351-354`) ist heute ein
`configSwitch(c.id, 0.f, 6.f, init, "Song", {sieben Labels})`. Rack braucht
genau so viele Labels wie Stufen, also müssen es 14 werden — und sie werden
**aus der Leiter komponiert**, nicht danebengeschrieben, sonst driften Tabelle
und Beschriftung auseinander. Der `FORM_A/FORM_B`-Zweig darüber (`:347-350`)
entfällt; seine fünf Wörter ziehen als Namenstabelle hierher um:

```cpp
                    else if (c.id == SONG_A || c.id == SONG_B) {
                        // Composed from the ladder itself: one source, no drift.
                        static const char* kFormWords[] = {
                            "TWO MOTIFS", "ONE + VAR", "HIERARCHICAL",
                            "CALL / RESPONSE", "OSTINATO"};
                        static const char* kSongWords[] = {
                            "AAAB", "ABAB", "ABBB", "BUILD",
                            "ROTATE", "MIRROR", "OFF"};
                        std::vector<std::string> rungs;
                        for (int i = 0; i < spky::kSongLadderCount; ++i) {
                            const spky::SongRung& r = spky::song_ladder_at(i);
                            rungs.push_back(std::string(kFormWords[r.form]) +
                                            " / " + kSongWords[r.song]);
                        }
                        configSwitch(c.id, 0.f,
                                     float(spky::kSongLadderCount - 1),
                                     init, "Song", rungs);
                    }
```

In `pushParams()` den heutigen FORM/SONG-Block (um `:716-717`) ersetzen:

```cpp
            const float songNorm = pp(SONG_A, p) /
                                   float(spky::kSongLadderCount - 1);
            const int rung = spky::hyst_step(songRung[p], songNorm,
                                             spky::kSongLadderCount);
            if (rung != songRung[p]) {
                songRung[p] = rung;
                inst.new_phrase(p);          // turn the knob, get a new melody
            }
            const spky::SongRung& r = spky::song_ladder_at(rung);
            inst.set_form(p, r.form);
            inst.set_song(p, r.song);
```

- [ ] **Step 9: Vollprüfung und Commit**

Vollprüfung, dann Engine-Prüfung. Danach:

```bash
cd /c/Users/bernd/Documents/AI/FireFlow
git add engine/mod/song_ladder.h tests/test_song_ladder.cpp CMakeLists.txt \
        host/vcv/res/gen_panel.py host/vcv/res/test_panel.py \
        host/vcv/src/Fireflow.cpp host/vcv/src/generated_panel.hpp \
        host/vcv/src/generated_hw_panel.hpp host/vcv/src/init_patch.hpp \
        host/vcv/res/Fireflow.svg host/vcv/res/FireflowHW.svg
git commit -m "song: one knob walks the grid and rolls a new melody on the way

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: `GRIT` wird bipolar, `SAT` verschwindet

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (GRITMODE raus, GRIT auf `KNOBC`, `DYNAMIC_CAPTIONS`-Zeile `:221`)
- Modify: `host/vcv/src/Fireflow.cpp`
- Modify: `host/vcv/res/test_panel.py`

**Interfaces:**
- Consumes: `Instrument::set_grit_mode(int, GritMode)`, `set_grit_mix(int, float)`.
- Produces: `GRIT_A/B` ist `KNOBC` mit Range `-1..1`. Vorzeichen wählt den
  Modus (`GritMode::Reduce` links, `GritMode::Drive` rechts — Achtung: das
  Panelwort ist SAT, das Enum heißt `Drive`, `engine/fx/grit.h:10`), Betrag
  ist der Mix mit einer Totzone von 0.03.

- [ ] **Step 1: Guard schreiben**

```python
def test_grit_is_one_bipolar_knob():
    """The SAT pad is gone: sign picks the mode, magnitude is the mix, and
    a dead zone around zero makes 'off' reachable on a real pot."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    check("GRITMODE_A" not in names and "GRITMODE_B" not in names,
          "GRITMODE pads still exist")
    grit = [c for c in gp.PARAMS if c.enum == "GRIT_A"][0]
    check(grit.kind == gp.KNOBC, "GRIT_A is not a bipolar knob")
    check(all(row[0] != "GRITMODE" for row in gp.DYNAMIC_CAPTIONS),
          "GRITMODE still has a dynamic caption")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("spky::GritMode::Reduce" in cpp and "spky::GritMode::Drive" in cpp,
          "the host no longer names both grit modes")
    check("kGritDead" in cpp, "no dead zone around grit zero")
    check("GRITMODE_A" not in cpp, "Fireflow.cpp still references GRITMODE_A")
```

- [ ] **Step 2: Rot vorführen** — `python3 res/test_panel.py`, erwartet FAIL „GRITMODE pads still exist".

- [ ] **Step 3: Generator ändern**

In `part_controls()` die `pads`-Liste auf `ENGINE` reduzieren:

```python
    pads = [("ENGINE", LATCH, "ENG", None)]
```

und den GRIT-Eintrag auf `KNOBC` heben:

```python
    out.append(Ctl("GRIT", KNOBC, fx(FX_BOT[2]), ROW_V2, "GRIT"))
    out.append(Ctl("COMP", SMKNOB, fx(FX_BOT[3]), ROW_V2, "COMP"))
```

(die bisherige `for enum, lbl, i in (("GRIT", ...), ("COMP", ...))`-Schleife
wird durch diese zwei Zeilen ersetzt, damit GRIT eine andere `kind` bekommt).

Die Zeile `("GRITMODE", "GRITMODE", ("SAT", "CRSH")),` aus `DYNAMIC_CAPTIONS`
(`:221`) löschen. `INIT_DEFAULTS`: `GRITMODE_A`/`GRITMODE_B` löschen, und
`GRIT_A`/`GRIT_B` von ihrem heutigen unipolaren Wert auf denselben Betrag mit
positivem Vorzeichen setzen (heutiger Wert bleibt, er war schon 0..1 = SAT).

- [ ] **Step 4: Host verdrahten**

`configControls()`: GRIT braucht `-1.f, 1.f`. Im `WK_KNOBC`-Zweig prüfen, ob
`MELODY` dort hart verdrahtet ist; falls ja, die Bedingung auf „`MELODY` oder
`GRIT`" erweitern.

In `pushParams()` den heutigen Block (um `:712`) ersetzen:

```cpp
            // GRIT is one bipolar knob: sign is the mode, magnitude the mix.
            // The dead zone exists because a 9 mm pot on an ADC cannot hit an
            // exact zero -- without it "off" would be unreachable on hardware.
            static constexpr float kGritDead = 0.03f;
            const float gritKnob = params[p ? GRIT_B : GRIT_A].getValue();
            inst.set_grit_mode(p, gritKnob < 0.f ? spky::GritMode::Reduce
                                                 : spky::GritMode::Drive);
            const float gritMag = std::fabs(gritKnob);
            inst.set_grit_mix(p, gritMag <= kGritDead ? 0.f
                                 : (gritMag - kGritDead) / (1.f - kGritDead));
```

Die alten Zeilen `inst.set_grit_mode(p, ppb(GRITMODE_A, p) ? ...)` und
`inst.set_grit_mix(p, pp(GRIT_A, p));` entfallen.

- [ ] **Step 5: Vollprüfung und Commit**

```bash
git commit -m "grit: crush on the left, saturate on the right, silence in between

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: `COMP` wird `LVL/COMP`

**Files:**
- Modify: `engine/center/center.h`, `engine/center/center.cpp`
- Modify: `engine/instrument.h`
- Modify: `tests/test_center.cpp`
- Modify: `host/vcv/res/gen_panel.py` (Label), `host/vcv/src/Fireflow.cpp`, `host/vcv/res/test_panel.py`

**Interfaces:**
- Produces: `Center::set_level(int part, float lvl)` (0..1, geglättet) und
  `Instrument::set_part_level(int p, float lvl)`. `gain_a()`/`gain_b()` liefern
  ab jetzt `equal-power(MORPH) * level[part]`.
- Host-Kontrakt für den Knopfwert `v` in 0..1:
  `level = min(1, v / 0.8)`, `comp = v <= 0.8 ? 0 : (v - 0.8) / 0.2 * 0.7`.

- [ ] **Step 1: Engine-Test schreiben**

In `tests/test_center.cpp` anfügen:

Die Datei hat oben ein `Rig`-Struct (`Center c; SuperModulator a, b; Part pa, pb;`
mit `r.init()`, `tests/test_center.cpp:13-17`). Die neuen Fälle benutzen es
genauso wie die bestehenden:

```cpp
TEST_CASE("part level scales the morph gain without touching the other deck") {
    Rig r; r.init();
    r.c.set_morph(0.5f);
    r.c.set_level(0, 1.f);
    r.c.set_level(1, 1.f);
    for (int i = 0; i < 4000; ++i) r.c.update(r.a, r.b, r.pa, r.pb);
    const float full_a = r.c.gain_a();
    const float full_b = r.c.gain_b();
    r.c.set_level(0, 0.5f);
    for (int i = 0; i < 4000; ++i) r.c.update(r.a, r.b, r.pa, r.pb);
    CHECK(r.c.gain_a() == doctest::Approx(full_a * 0.5f).epsilon(0.01));
    CHECK(r.c.gain_b() == doctest::Approx(full_b).epsilon(0.01));
}

TEST_CASE("level zero is silence") {
    Rig r; r.init();
    r.c.set_level(0, 0.f);
    for (int i = 0; i < 4000; ++i) r.c.update(r.a, r.b, r.pa, r.pb);
    CHECK(r.c.gain_a() == doctest::Approx(0.f).epsilon(0.001));
}
```

`Center::init` nimmt zwei Argumente (`float sample_rate, uint32_t seed`,
`center.h:22`) — das erledigt `Rig::init()` bereits.

- [ ] **Step 2: Rot vorführen** — Build erwartet `no member named 'set_level'`.

- [ ] **Step 3: Center erweitern**

`center.h`, bei den anderen Settern:

```cpp
    // LVL: the per-deck output level the LVL/COMP knob's lower zone drives.
    // It multiplies the equal-power MORPH gain rather than replacing it, so
    // the crossfade keeps its constant-power sum at any pair of levels.
    void set_level(int part, float lvl) {
        _lvl_target[part & 1] = clampf(lvl, 0.f, 1.f);
    }
```

Member dazu:

```cpp
    float   _lvl_target[2] = {1.f, 1.f};
    OnePole _lvl_smooth[2];
```

In `Center::init()` neben `_morph_smooth`:

```cpp
    for (int i = 0; i < 2; ++i) {
        _lvl_target[i] = 1.f;
        _lvl_smooth[i].init(_cr, 0.03f);
        _lvl_smooth[i].reset(1.f);
    }
```

In `Center::update()` direkt nach der MORPH-Zeile (`center.cpp:126-128`):

```cpp
    _g_a = std::cos(_morph * kQuarter) * _lvl_smooth[0].process(_lvl_target[0]);
    _g_b = std::sin(_morph * kQuarter) * _lvl_smooth[1].process(_lvl_target[1]);
```

`instrument.h`, neben `set_morph`:

```cpp
    void set_part_level(int p, float lvl) { _center.set_level(p, lvl); }
```

- [ ] **Step 4: Grün prüfen** — `ctest --test-dir build --output-on-failure`.

- [ ] **Step 5: Panel-Guard schreiben**

```python
def test_comp_knob_is_level_then_compressor():
    """COMP was a volume control in practice. The knob says so now, and the
    compressor lives in its top fifth with make-up."""
    import gen_panel as gp
    comp = [c for c in gp.PARAMS if c.enum == "COMP_A"][0]
    check(comp.label == "LVL", f"COMP_A still prints {comp.label!r}")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("kLvlCompSplit" in cpp, "no zone split constant in the host")
    check("inst.set_part_level(" in cpp, "the host never sets a part level")
```

- [ ] **Step 6: Rot vorführen**, dann Generator und Host ändern.

`gen_panel.py`: das COMP-Label auf `"LVL"` setzen (Tooltip `"Level / Comp"`).

`Fireflow.cpp`, `pushParams()` — die Zeile `inst.set_comp(p, pp(COMP_A, p));`
ersetzen:

```cpp
            // LVL/COMP: the lower zone is pure output gain (Comp::set_amount(0)
            // is a bit-exact bypass, so it costs no compressor CPU); the top
            // fifth engages the compressor with make-up, ending at the 0.7 that
            // used to be the knob's working value.
            static constexpr float kLvlCompSplit = 0.8f;
            static constexpr float kCompTop      = 0.7f;
            const float lvlKnob = pp(COMP_A, p);
            inst.set_part_level(p, std::min(1.f, lvlKnob / kLvlCompSplit));
            inst.set_comp(p, lvlKnob <= kLvlCompSplit ? 0.f
                             : (lvlKnob - kLvlCompSplit) /
                               (1.f - kLvlCompSplit) * kCompTop);
```

`INIT_DEFAULTS`: `COMP_A`/`COMP_B` auf `0.8` setzen — voller Pegel, kein
Kompressor. Das entspricht der heutigen Wirkung von 0.5–0.7 näher als der
rohe Altwert, der jetzt eine andere Bedeutung hätte.

- [ ] **Step 7: Vollprüfung, Engine-Prüfung, Commit**

```bash
git commit -m "comp: the knob admits it was always the volume, and keeps the squeeze on top

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: `TIME` ersetzt `DIV` und `MULT`

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (`APPENDED_PANEL_PARAMS` leeren, `FLUXRATE` umbenennen)
- Modify: `host/vcv/src/Fireflow.cpp`
- Modify: `host/vcv/res/test_panel.py` (`test_bbd_pitch_flux_time_collections`, `test_flux_time_host_wiring`, `test_flux_time_guard_rejects_representative_regressions`)

**Interfaces:**
- Consumes: `Instrument::set_flux_rate(int, int)`,
  `set_fx_target_base(int, FXT_FLUX_TIME, float)`.
- Produces: `FLUXRATE_A/B` behält seine ParamId, heißt auf dem Panel `TIME` und
  rastet über 12 Teiler. `FLUXTIME_A/B` existiert nicht mehr; die
  Modulationssenke `FXT_FLUX_TIME` bleibt und bekommt den neutralen Basiswert
  `0.5` (denn `tape_time_mult(0.5) == 2^0 == 1`, `engine/fx/tape_echo.h:143`).

- [ ] **Step 1: Guard schreiben**

```python
def test_time_knob_replaces_div_and_mult():
    """DIV and MULT described one quantity. One notched knob does it, and the
    modulation sink keeps a neutral base so CV can still bend the tape."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    check("FLUXTIME_A" not in names and "FLUXTIME_B" not in names,
          "the MULT knobs still exist")
    rate = [c for c in gp.PARAMS if c.enum == "FLUXRATE_A"][0]
    check(rate.label == "TIME", f"FLUXRATE_A still prints {rate.label!r}")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("spky::FXT_FLUX_TIME, 0.5f" in cpp,
          "the flux-time modulation base is not pinned to neutral")
    check("FLUXTIME_A" not in cpp, "Fireflow.cpp still references FLUXTIME_A")
```

- [ ] **Step 2: Rot vorführen.**

- [ ] **Step 3: Generator ändern**

```python
APPENDED_PANEL_PARAMS = []
```

und die beiden `FLUXRATE`-Zeilen auf das neue Wort setzen:

```python
    Ctl("FLUXRATE_A", KNOBI, FX_TOP[0],     ROW_V1, "TIME", "FLUX time"),
    Ctl("FLUXRATE_B", KNOBI, W - FX_TOP[0], ROW_V1, "TIME", "FLUX time"),
```

`KNOBI` statt `SMKNOB`, weil der Knopf jetzt rastet. `INIT_DEFAULTS`:
`FLUXTIME_A`/`FLUXTIME_B` löschen.

- [ ] **Step 4: Host verdrahten**

`configControls()`: `FLUXRATE_A/B` als 12-stufigen Rastknopf konfigurieren
(`0.f, 11.f`), analog zum bestehenden `flux_division_index`-Pfad.

`pushParams()`:

```cpp
            inst.set_flux_rate(p, (int)std::lround(
                params[p ? FLUXRATE_B : FLUXRATE_A].getValue()));
            // The tape multiplier keeps its modulation sink but loses its knob:
            // 0.5 is the neutral multiplier (tape_time_mult(0.5) == 1), so CV
            // and the mod lanes still bend the tape while the panel does not.
            inst.set_fx_target_base(p, spky::FXT_FLUX_TIME, 0.5f);
```

Die alten `flux_division_index(...)`- und `FLUXTIME`-Zeilen (`:499-504`)
entfallen.

- [ ] **Step 5: Die drei bestehenden FLUX-Time-Tests nachziehen**

`test_bbd_pitch_flux_time_collections`, `test_flux_time_host_wiring` und
`test_flux_time_guard_rejects_representative_regressions` prüfen heute die
MULT-Oberfläche. Sie werden **nicht** gelöscht, sondern auf den neuen
Kontrakt umgeschrieben: die Modulationssenke existiert weiter, nur ohne
Panel-Knopf. Wo ein Test ausschließlich die Knopfexistenz prüfte und danach
nichts mehr aussagt, wird er entfernt und sein Wegfall im Commit benannt.

- [ ] **Step 6: Vollprüfung und Commit**

```bash
git commit -m "flux: one notched knob for a delay time that was always one number

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 7: `COUPL` schluckt den `SYNC`-Schalter

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (SYNC raus, COUPLE-Label)
- Modify: `host/vcv/src/Fireflow.cpp`
- Modify: `host/vcv/res/test_panel.py`
- Modify: `tests/test_center.cpp`

**Interfaces:**
- Consumes: `Instrument::set_sync(bool)`, `set_couple(float)` — unverändert.
- Produces: Host-Kontrakt für den Knopfwert `v` in 0..1:
  `v < 0.5` → `set_sync(false)`, `set_couple(v / 0.5)`;
  `v >= 0.5` → `set_sync(true)`, `set_couple((v - 0.5) / 0.5)`.

- [ ] **Step 1: Guard schreiben**

```python
def test_couple_knob_carries_both_worlds():
    """SYNC was the right-hand end of COUPLE's own axis. Two zones, each
    sweeping couple 0..1, so neither world loses its spread."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    check("SYNC" not in names, "the SYNC switch still exists")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("kCoupleZoneSplit" in cpp, "no zone split constant in the host")
    check("inst.set_sync(" in cpp, "the host never sets sync any more")
    check("params[SYNC]" not in cpp, "Fireflow.cpp still reads a SYNC param")
```

- [ ] **Step 2: Rot vorführen.**

- [ ] **Step 3: Generator ändern**

Die Zeile `Ctl("SYNC", SW2, CX - 9.0, ROW_TIME1, "SYNC"),` aus `SHARED`
löschen und COUPLEs Label auf das Zonenwort setzen:

```python
    Ctl("COUPLE", SMKNOB, CX - 9.0, ROW_TIME2, "FREE|GRID"),
```

`INIT_DEFAULTS`: `SYNC` löschen; `COUPLE` auf `0.5` setzen (unterer Rand der
GRID-Zone = heutiges „sync an, couple 0").

Falls `test_quiet_technical_tokens` oder `test_every_printed_word_is_unique`
über dem Pipe-Zeichen stolpert, das Wort auf `GRID` kürzen und die Zonen auf
dem Panel später beschriften — die Beschriftung ist Sache der
Umgruppierungs-Session.

- [ ] **Step 4: Host verdrahten**

`pushParams()`, die heutigen SYNC- und COUPLE-Zeilen ersetzen:

```cpp
        // COUPLE runs both worlds on one axis. Below the split SYNC is off and
        // couple drives the Kuramoto lock; above it SYNC is on and couple sets
        // how tightly the texture lanes follow. Each half sweeps 0..1, so the
        // grid world keeps its full spread -- "on the grid but breathing" is a
        // real state and must stay reachable.
        static constexpr float kCoupleZoneSplit = 0.5f;
        const float coupleKnob = params[COUPLE].getValue();
        const bool  grid = coupleKnob >= kCoupleZoneSplit;
        inst.set_sync(grid);
        inst.set_couple(grid
            ? (coupleKnob - kCoupleZoneSplit) / (1.f - kCoupleZoneSplit)
            : coupleKnob / kCoupleZoneSplit);
```

- [ ] **Step 5: Einen Engine-Test dazu, der die Zonen nicht kennt**

In `tests/test_center.cpp`: prüfen, dass `set_sync(false)` mit
`set_couple(1.f)` und `set_sync(true)` mit `set_couple(0.f)` **verschiedene**
`rate_scale`-Ergebnisse produzieren — das belegt, dass die beiden Zonen zwei
Welten sind und nicht dieselbe. Das bestehende `test_center.cpp` hat bereits
Fälle für beide Welten; das neue Beispiel an deren Muster anlehnen.

- [ ] **Step 6: Vollprüfung, Engine-Prüfung, Commit**

```bash
git commit -m "couple: the switch was the far end of the knob all along

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 8: `DRIFT` schluckt den `SETL`-Taster

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (SETTLE raus)
- Modify: `host/vcv/src/Fireflow.cpp`
- Modify: `host/vcv/res/test_panel.py`

**Interfaces:**
- Consumes: `Instrument::set_drift(float)`, `Instrument::settle()`.
- Produces: Host-Kontrakt: `v <= 0.02` → `set_drift(0)` und **einmalig** beim
  Eintritt in die Zone `settle()`; sonst `set_drift((v - 0.02) / 0.98)`.
  Der Eintritt ist flankengetriggert, kein Dauerfeuer.

- [ ] **Step 1: Guard schreiben**

```python
def test_drift_knob_settles_at_its_left_stop():
    """SETL was drift-to-zero plus a glide. It lives at the end of the axis
    it always belonged to, and fires once on entry, not every tick."""
    import gen_panel as gp
    check("SETTLE" not in {c.enum for c in gp.PARAMS},
          "the SETL pad still exists")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("kDriftSettleZone" in cpp, "no settle zone constant in the host")
    check("driftSettled" in cpp, "settle is not edge-triggered")
    check("params[SETTLE]" not in cpp, "Fireflow.cpp still reads a SETTLE param")
```

- [ ] **Step 2: Rot vorführen.**

- [ ] **Step 3: Generator ändern** — `Ctl("SETTLE", SMBTN, R, ROW_DUO2, "SETL"),`
aus `SHARED` löschen, `SETTLE` aus `INIT_DEFAULTS` löschen.

- [ ] **Step 4: Host verdrahten**

Member im `Fireflow`-Struct:

```cpp
    bool driftSettled = false;
```

In `pushParams()`, die heutige DRIFT-Zeile und `settleTrig`-Zeile ersetzen:

```cpp
        // The left stop IS the old SETL pad: Center::settle() is drift_target = 0
        // plus a ~1 s glide of EVOLVE and kick, so the button always lived at
        // the end of this axis. Edge-triggered -- a knob parked at the stop must
        // not re-fire the glide on every control tick.
        static constexpr float kDriftSettleZone = 0.02f;
        const float driftKnob = params[DRIFT].getValue();
        if (driftKnob <= kDriftSettleZone) {
            if (!driftSettled) { inst.settle(); driftSettled = true; }
            inst.set_drift(0.f);
        } else {
            driftSettled = false;
            inst.set_drift((driftKnob - kDriftSettleZone) /
                           (1.f - kDriftSettleZone));
        }
```

Die Deklaration von `settleTrig` mit entfernen.

- [ ] **Step 5: Vollprüfung und Commit**

```bash
git commit -m "drift: the panic button was the left stop of the knob it panicked about

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 9: Feste Werte, `SPOT` und `DRIVE_A/B` gestrichen

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (`MASTER_DRIVE`, `SPOT`, `REV_MOD`, `REV_SMEAR` aus `SHARED`; `HIDDEN_PARAMS` leeren)
- Modify: `host/vcv/src/Fireflow.cpp` (feste Werte, `DriveQuantity` und Menüeinträge raus)
- Modify: `host/vcv/res/test_panel.py`

**Interfaces:**
- Produces: `HIDDEN_PARAMS = []`. Die Setter `set_master_drive`,
  `set_reverb_mod`, `set_reverb_smear` werden in `pushParams()` mit
  Konstanten aufgerufen; die Engine-API bleibt unverändert, damit
  Render-Host und Szenarien sie weiter fahren können.

- [ ] **Step 1: Guard schreiben**

```python
def test_fixed_values_and_dead_controls():
    """PUSH, WOBL and SMEAR become constants; SPOT dies; DRIVE_A/B was never
    wired to anything and leaves with its menu entry."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    for dead in ("MASTER_DRIVE", "SPOT", "REV_MOD", "REV_SMEAR",
                 "DRIVE_A", "DRIVE_B"):
        check(dead not in names, f"{dead} still exists")
    check(gp.HIDDEN_PARAMS == [], "HIDDEN_PARAMS is not empty")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("set_master_drive(0.40f)" in cpp, "PUSH is not pinned to 0.40")
    check("DriveQuantity" not in cpp, "the dead Drive quantity is still here")
    check("inst.spot()" not in cpp, "SPOT is still wired in the host")
```

- [ ] **Step 2: Rot vorführen.**

- [ ] **Step 3: Generator ändern**

Aus `SHARED` löschen: `MASTER_DRIVE`, `SPOT`, `REV_MOD`, `REV_SMEAR`.
`HIDDEN_PARAMS = []`. Aus `INIT_DEFAULTS` die sechs zugehörigen Zeilen
löschen.

- [ ] **Step 4: Host ändern**

In `pushParams()` die vier Regler durch Konstanten ersetzen:

```cpp
        // Fixed by ear (spec 2026-08-09 §2): PUSH sat at 0.40 in every patch,
        // and once the limiter rides, DRIVE stops controlling dirt anyway.
        inst.set_master_drive(0.40f);
        inst.set_reverb_smear(0.30f);
        inst.set_reverb_mod(0.15f);
```

Die Setter heißen so (`engine/instrument.h:130-131, 136`):
`set_master_drive(float)`, `set_reverb_smear(float)` (dahinter
`set_diffuser_mod_depth`), `set_reverb_mod(float)` (dahinter `set_mod_depth`).

Weiter entfernen: die `spotTrig`/`settleTrig`-Zeile für SPOT (`:747`), die
`configParam<DriveQuantity>`-Aufrufe (`:388-391`), die `DriveQuantity`-Klasse
und die DRIVE-Submenü-Schleife (`:1467-1475`).

- [ ] **Step 5: Vollprüfung und Commit**

```bash
git commit -m "panel: three knobs become numbers, and a menu slider that did nothing leaves

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 10: `DETUNE` aufs Panel, weitere Reichweite für Synth und Wave

**Files:**
- Modify: `engine/synth/synth_engine.h:40` (`kDetuneCeilCt`)
- Modify: `engine/body/body_voice.cpp:57` (`kDetuneScale`, Gegenkompensation)
- Modify: `engine/sampler/sampler_config.h:316` (nur Kommentar)
- Modify: `tests/test_synth_engine.cpp`, `tests/test_body_voice.cpp`
- Modify: `host/vcv/res/gen_panel.py`, `host/vcv/src/Fireflow.cpp`, `host/vcv/res/test_panel.py`

**Interfaces:**
- Produces: `SynthEngineT::kDetuneCeilCt == 105.f`,
  `BodyVoice::kDetuneMaxCt` unverändert `140.f`. Host: `DETUNE_A/B` ist ein
  Panel-`SMKNOB` und der Knopfwert geht **quadratisch** in die Engine:
  `inst.set_voice_detune(p, v * v)`.

- [ ] **Step 1: Engine-Tests schreiben**

In `tests/test_synth_engine.cpp`:

```cpp
TEST_CASE("detune reaches 105 cents at full for the synth engines") {
    spky::SynthEngine e;
    e.init(48000.f);                      // match the file's existing init call
    e.set_detune(1.f);
    CHECK(e.applied_detune_ct() == doctest::Approx(105.f).epsilon(0.001));
    e.set_detune(0.f);
    CHECK(e.applied_detune_ct() == doctest::Approx(0.f).epsilon(0.001));
}
```

In `tests/test_body_voice.cpp`:

```cpp
TEST_CASE("body keeps its 140 cent rail after the shared ceiling moved") {
    // BODY reads DETUNE wider than the synths do (spec §5, "how broken is this
    // material"). The ceiling below it grew from 35 to 105, so the scale here
    // shrank from 4 to 4/3 -- the rail must be exactly where it was.
    CHECK(spky::body_detune_max_ct() == doctest::Approx(140.f).epsilon(0.001));
}
```

`kDetuneMaxCt` liegt heute im anonymen Namespace von `body_voice.cpp` und ist
von außen unsichtbar. Dafür kommt ein schmaler Observer nach `body_voice.h`:

```cpp
// The 140 ct rail, readable so a test can pin it. Not a test-only path: it
// reports a constant of the public behaviour, the same way detune_cents() does.
float body_detune_max_ct();
```

mit der Definition in `body_voice.cpp` unterhalb der Konstanten:

```cpp
float body_detune_max_ct() { return kDetuneMaxCt; }
```

- [ ] **Step 2: Rot vorführen** — erwartet FAIL mit `105.0 != 35.0`.

- [ ] **Step 3: Die beiden Konstanten ändern**

`engine/synth/synth_engine.h:40`:

```cpp
    // Raised from 35 ct (spec 2026-08-09 §4): DETUNE came back onto the panel
    // as a performance control and drones want the reach. SynthEngine and
    // WaveEngine are the intended beneficiaries; BodyEngine is the same
    // template and pays it back at BodyVoice::kDetuneScale, so its own
    // 140 ct rail is unchanged. BbdEngine and the sampler have their own
    // paths and keep 35 ct.
    static constexpr float kDetuneCeilCt = 105.f;
```

`engine/body/body_voice.cpp:57`:

```cpp
constexpr float kDetuneScale = 4.f / 3.f;   // 105 * 4/3 == the same 140 ct
constexpr float kDetuneMaxCt = 140.f;
```

`engine/sampler/sampler_config.h:316`: den Kommentar von „matches the synth"
auf „deliberately NOT the synth's ceiling any more (spec 2026-08-09 §4)"
ändern.

- [ ] **Step 4: Grün prüfen** — `ctest --test-dir build --output-on-failure`.
Ein Render-Sanity-Test kann sich hier bewegen; das ist erlaubt (keine
Bit-Exaktheits-Gates). Bewegt sich ein Test, der **nicht** mit Detune zu tun
hat, ist das ein Befund und wird untersucht, nicht durchgewinkt.

- [ ] **Step 5: Panel-Guard schreiben**

```python
def test_detune_is_a_panel_control_with_a_square_taper():
    """DETUNE leaves the context menu. The taper keeps the first ~20 cents
    usable instead of squeezing them into a fifth of the travel."""
    import gen_panel as gp
    names = {c.enum for c in gp.PARAMS}
    check("DETUNE_A" in names and "DETUNE_B" in names, "DETUNE is missing")
    det = [c for c in gp.PARAMS if c.enum == "DETUNE_A"][0]
    check(det.label != "", "DETUNE_A still has the menu-only empty label")
    check((det.x, det.y) != (0.0, 0.0), "DETUNE_A still sits at the menu origin")
    here = os.path.dirname(os.path.abspath(__file__))
    cpp = open(os.path.join(here, "..", "src", "Fireflow.cpp")).read()
    check("detKnob * detKnob" in cpp, "the detune taper is not quadratic")
```

- [ ] **Step 6: Rot vorführen**, dann Generator und Host ändern.

`gen_panel.py`: `DETUNE_A/B` aus `HIDDEN_PARAMS` (in Task 9 geleert) in die
Part-Blöcke holen — als reguläre `Ctl("DETUNE", SMKNOB, ...)` in
`part_controls()`, mit Label `"DTUN"`. Der freie Platz aus Task 2 (`PAD_X[2]`)
nimmt sie auf. `INIT_DEFAULTS`: die vorhandenen Werte behalten.

`Fireflow.cpp`: die expliziten `configParam<DetuneQuantity>`-Aufrufe
(`:382-384`) entfernen — `DETUNE` läuft jetzt über `kParamCtls` wie jedes
andere Panel-Element; falls die `DetuneQuantity`-Anzeige erhalten bleiben
soll, sie im `configControls()`-Switch an der `c.id == DETUNE_A || c.id ==
DETUNE_B`-Stelle einhängen. Die Detune-Submenü-Einträge (`:1453-1463`)
entfernen.

`pushParams()`:

```cpp
            // Quadratic taper: the first ~20 ct is where the fine beating
            // lives, and a linear map would squeeze it into a fifth of the
            // travel now that the ceiling is 105 ct.
            const float detKnob = pp(DETUNE_A, p);
            inst.set_voice_detune(p, detKnob * detKnob);
```

- [ ] **Step 7: Vollprüfung, Engine-Prüfung, Commit**

```bash
git commit -m "detune: out of the menu, onto the panel, and far enough for a drone

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 11: HW-Entwurf nachziehen, bauen, installieren

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py` (`DECK_POS`/`CENTER_POS`)
- Modify: `host/vcv/res/test_hw_panel.py`
- Modify: `docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md`

**Interfaces:**
- Consumes: die 68 Parameter aus Tasks 2–10.
- Produces: `FireflowHW.svg` und `generated_hw_panel.hpp` mit 66 Positionen;
  ein installiertes, spielbares Plugin.

- [ ] **Step 1: Den HW-Guard schreiben**

In `res/test_hw_panel.py`:

```python
def test_hw_slot_map_matches_the_reduced_inventory():
    """Every runtime param has a hardware slot and no slot is left pointing
    at a control that no longer exists."""
    import gen_panel as gp, gen_hw_panel as hw
    live = {c.enum for c in gp.RUNTIME_PANEL_PARAMS}
    stems = set(hw.DECK_POS) | set(hw.CENTER_POS)
    dead = [s for s in stems
            if s not in live and f"{s}_A" not in live and s not in hw.JACK_POS]
    check(not dead, f"hw slots for controls that no longer exist: {dead}")
    check(len(hw.HW_PARAMS) == len(gp.RUNTIME_PANEL_PARAMS),
          "hw param count drifted from the shared inventory")
```

- [ ] **Step 2: Rot vorführen**

```bash
cd /c/Users/bernd/Documents/AI/FireFlow/host/vcv && python3 res/gen_hw_panel.py
```

Erwartet: bereits vor dem Test ein `KeyError: no hw slot for DETUNE_A` (oder
umgekehrt ein toter Slot-Eintrag), weil `place()` jeden Parameter zuordnen
muss. Das ist der eigentliche rote Beweis.

- [ ] **Step 3: Slot-Map anpassen**

In `gen_hw_panel.py` aus `DECK_POS` streichen: `FORM`, `NEWPHRASE`, `STEP`,
`GRITMODE`, `FLUXTIME`. Aus `CENTER_POS` streichen: `SYNC`, `SPOT`, `SETTLE`,
`MASTER_DRIVE`, `REV_SMEAR`, `REV_MOD`. Für `DETUNE` einen Platz in der
`PLAY`-Reihe eintragen (der von `STEP` freigewordene `(87.0, 86.0)`).

Die Lücken werden **nicht** aufgefüllt und die übrigen Koordinaten **nicht**
verschoben — Iteration 0 bleibt die mechanische Übersetzung, das Umgruppieren
ist eine eigene Session.

- [ ] **Step 4: Vollprüfung**

Erwartet: `python3 res/gen_hw_panel.py` meldet `params=68 ... panel=60HP`, und
alle drei Guard-Läufe enden mit `PASS`. `test_no_overlap_with_hw_radii` kann
jetzt leichter grün werden, weil Elemente fehlen — das ist kein Anlass,
Toleranzen zu ändern.

- [ ] **Step 5: Die Envelope-Spec nachziehen**

In `docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md`:

- §1-Tabelle: Potis/Taster/Menü-Zeilen auf 68 Parameter, 66 Positionen,
  4 Taster korrigieren.
- §1: die Zeile `13 + ALT + 2 PLAY-Pads = 16` ersetzen — **ALT und die PLAY-Pads
  existieren nicht** (kein Vorkommen in `gen_panel.py`, `gen_hw_panel.py`,
  `host/vcv/src/`, `shell/`).
- §1: den „one in, one out"-Absatz streichen; das Panel hat 14 Positionen Luft.
- §1: den `HIDDEN_PARAMS`-Absatz ersetzen — DETUNE ist auf dem Panel, DRIVE ist
  gelöscht, die offene Entscheidung ist gefallen.
- §2: die GRIT-Modus-LED aus der LED-Tabelle nehmen (der bipolare Knopf zeigt
  den Modus selbst).
- Kopfvermerk: „in Teilen übersteuert durch
  `2026-08-09-hw-control-reduction-design.md`".

In `docs/roadmap.md`: die M6-Prosa mit ALT-Gesten (`:430`, `:469`, `:486`,
`:519`, `:542`) als überholt markieren — sie beschreibt die 42-HP-Fader-Welt,
in der COUPLE „ALT + fader" war. Ein Satz pro Fundstelle mit Verweis hierher
genügt; die Absätze werden nicht gelöscht, sie sind Projekthistorie.

Und zwei Memory-Dateien in
`~/.claude/projects/C--Users-bernd-Documents-AI-FireFlow/memory/`:

- `spotykach-reverb-mod-split.md` — SMEAR und WOBL sind auf dem Panel
  Konstanten (0.30 / 0.15); nur DIFF bleibt regelbar. Ohne diese Korrektur
  schlägt eine spätere Session die Dreiteilung wieder vor.
- `spotykach-hardware-constraint.md` — „one in, one out" ist tot; der Zielsatz
  ist 68 Parameter auf 66 Positionen mit 14 Positionen Luft.

- [ ] **Step 6: Bauen und installieren**

```bash
cd /c/Users/bernd/Documents/AI/FireFlow/host/vcv
./build-local.sh install
```

Erwartet: der Skriptschwanz druckt Archivpfad, den entpackten
`plugin.dll`-Pfad mit Größe und Datum, und `Restart Rack.` Wichtig: `make
install` kopiert nur das `.vcvplugin`-Archiv; das Skript synchronisiert
zusätzlich den bereits entpackten `Fireflow/`-Ordner, weil Rack **den** lädt.
Ohne diesen Schritt lädt still die alte Version.

- [ ] **Step 7: In Rack gegenprüfen**

Rack neu starten, `Fireflow` und `FireflowHW` einfügen. Prüfen:

1. `STEPS` ganz links lässt das Deck frei laufen, ab 2 rastet der Sequencer ein.
2. `SONG` drehen ändert die Melodie bei jedem Rastpunkt — und **nur** dann.
3. `GRIT` links crunched, rechts sättigt, Mitte ist still.
4. `LVL` ganz links ist stumm, MORPH blendet weiterhin gleichmäßig über.
5. `COUPL` unter der Mitte läuft frei, darüber am Raster.
6. `DRIFT` am linken Anschlag beruhigt sich hörbar über etwa eine Sekunde.
7. `DTUN` bei Drones öffnet weit.

Was sich falsch anfühlt, ist ein Tuning-Befund für die nächste Runde, kein
Implementierungsfehler — die Zahlen in der Spec sind Startwerte.

**By-Ear-Checkliste — was sich über die neun Implementierungs-Tasks hörbar
geändert hat, gesammelt an einer Stelle statt verstreut in gitignored
Reports:**

1. **LVL/COMP-Init.** `COMP_A = 0.629666805`, `COMP_B = 0.561333418` steuerten
   den Kompressor, dessen Makeup-Gain Teil der Werkslautstärke war. Beide
   sind jetzt `0.8` — volle Lautstärke, Kompressor aus. Das Werkspatch kann
   dadurch **leiser** wirken als vorher, und die relative Balance der beiden
   Decks hat sich geändert (früher unterschiedlich, jetzt gleich).
2. **BODYs unangetastetes Detune.** `SynthEngineT`s Boot-Default
   `_detune_spread_ct = 18.f` ist ein absoluter Cent-Wert, der nicht mit der
   Ceiling skaliert. Mit `BodyVoice::kDetuneScale` kompensiert von 4 auf 4/3
   bootet ein BODY-Deck, dessen DETUNE-Knopf nie angefasst wurde, jetzt bei
   **24 ct statt 72 ct** — weniger inharmonisch ab Werk.
3. **DETUNE-Init unterscheidet sich jetzt pro Deck** — `DETUNE_A =
   0.239045722`, `DETUNE_B = 0.414039341` — gewählt, damit jedes Deck genau
   das Detune reproduziert, das seine eigene Engine vorher erzeugte (Deck A
   bootet SYNTH, Deck B bootet BODY). Konsequenz: Deck B auf SYNTH umgestellt
   gibt jetzt 18 ct, wo es früher 6 ct waren.
4. **Drei feste Werte weichen vom gespeicherten Patch ab.** `MASTER_DRIVE`
   0.482666761 → 0.40, `REV_SMEAR` 0.484000504 → **0.30 (fast halbiert)**,
   `REV_MOD` 0.237000003 → 0.15. Diese kommen aus Bastians geäußerter
   Spielpraxis, nicht aus dem Snapshot. SMEAR ist der Wert, der zuerst
   angehört werden sollte.
5. **`sampler_punch` feuert jetzt pro SONG-Rastpunkt**, geerbt vom
   retirierten NEW-Pad. Das Durchdrehen des Knopfes überstreicht viele
   Rastpunkte. Die Feuerrate ist eine offene By-Ear-Frage.
6. **Panel-Beschriftung:** die Mitte-Gruppen-Legende wurde von `TIME` zu
   `TIMING` umbenannt, damit der FLUX-Knopf das Wort `TIME` bekommen konnte.
   Reversibel; die Alternative wäre gewesen, den Knopf anders zu nennen.
7. **`COUPLE`s Label ist `FREE|GRID`** — neun Zeichen, wo das bisherige
   Maximum auf dem Panel fünf war. Gemessen passend (ca. 10 mm Abstand zu
   `SHUFL`, 6.4 mm innerhalb der Gruppenbox), aber es ist das längste Label
   am Instrument. Der kürzere Fallback wäre schlicht `GRID` gewesen.

- [ ] **Step 8: Commit**

```bash
cd /c/Users/bernd/Documents/AI/FireFlow
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py \
        host/vcv/res/FireflowHW.svg host/vcv/src/generated_hw_panel.hpp \
        docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md
git commit -m "hw: the aluminium draft catches up, and the envelope stops counting ghosts

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Was dieser Plan bewusst nicht tut

- **Keine Umgruppierung.** Positionen werden nur so weit angefasst, wie das
  Entfernen eines Nachbarn es erzwingt. Die Umgruppierung ist eine eigene
  Session mit eigener Spec (Envelope-Spec §4).
- **Keine Migrationen.** Alte `.vcv`-Patches laden verschoben. Das ist
  abgesegnet.
- **Kein Anfassen von `shell/` oder `bench/`.** Die Daisy-Toolchain bleibt
  außen vor; die Firmware kompiliert `engine/**` heute ohnehin noch nicht
  vollständig.
- **Keine Änderung an Glow.** Die Flow-Schicht fährt `set_form`/`set_song`
  weiter direkt; die Leiter aus Task 3 ist eine Panel-Oberfläche, keine
  Engine-Umstellung.
