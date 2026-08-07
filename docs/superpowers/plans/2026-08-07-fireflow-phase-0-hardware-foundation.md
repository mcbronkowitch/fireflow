# FireFlow Phase 0 — Hardware Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bis zum 21. August 2026 steht fest, wie viele Bedienelemente das FireFlow-Panel auf 42 HP trägt, ob das I/O auf ein Daisy Patch Submodule passt, und was die Firmware-Shell an CPU kostet — gemessen auf echtem Submodule-Silizium, nicht geschätzt.

**Architecture:** Vier voneinander weitgehend unabhängige Stränge. Der Papier-Strang (Task 1–2) entscheidet die Panel-Reduktion und das Pin-Budget und braucht keine Hardware; er entsperrt die gesamte PCB-Arbeit ab September. Der Mess-Strang (Task 3–6) bringt zum ersten Mal Engine-Code auf ein Submodule und beziffert den Shell-Aufschlag auf die 3,57 Punkte CPU-Reserve. Task 1 und 3 können am selben Tag beginnen.

**Tech Stack:** C++17, libDaisy (bleeptools-Fork), DaisySP, ARM GCC via `make`, Python 3 für Bench-Runner und Panelgenerator, doctest für Host-Tests, OpenOCD + Debug-Probe für Semihosting.

## Global Constraints

- **Zieltermin Phase 0: 21. August 2026.** Danach beginnt Phase 1 (KiCad, Testcoupon).
- **Formfaktor: Eurorack 42 HP**, nutzbare Panelfläche rund 213 × 115 mm.
- **Zielhardware: Daisy Patch Submodule.** Vorhanden, noch nie geflasht.
- **Multiplexer für Task 6: ein `74HC4051` (8 Kanäle), vorhanden.** Nichts zu bestellen, Phase 0 ist bauteilseitig vollständig. Task 6 misst **Kosten pro Kanal**, nicht Kosten pro Chip — acht Kanäle genügen, die Hochrechnung auf die reale Kanalzahl aus Task 2 ist Teil des Ergebnisses. Die Layoutfrage *wenige 16:1 gegen mehrere 8:1* gehört nicht in Phase 0; sie entscheidet sich an JLCPCB-Verfügbarkeit und Bestückungspreis vor dem 11. September.
- **Kein Heap in der Engine.** `engine/instrument.h` fordert injizierten Speicher über `FxMem`; auf Daisy kommt der aus SDRAM.
- **Zwei getrennte Toolchains, niemals mischen.** Engine/Tests/Render-Host: clang + Ninja, `source env.sh`, `-DCMAKE_BUILD_TYPE=Release` ist **nicht optional**. Firmware/Bench: ARM GCC über `make`.
- **VCV-Host immer über `host/vcv/build-local.sh`**, nie von Hand — das System-`g++` ist der ARM-Cross-Compiler.
- **Commit-Trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **Ein Test, der nicht rot werden kann, wird umgeschrieben.** RED einmal beweisen, immer.
- **Die Bench kann still ein veraltetes Objekt relinken.** Neue Zeilen über `build/bench.map` verifizieren, nie über die Memory-Tabelle.
- **`bench/` fasst die Produktions-Firmware nicht an** und umgekehrt. Getrennte Makefiles, getrennte `main.cpp`.
- **Alle bisherigen Bench-Zahlen stammen vom Daisy Seed.** Keine Zahl aus `docs/bench/` darf als Submodule-Messung zitiert werden, bis Task 4 sie dort reproduziert hat.

## File Structure

| Datei | Verantwortung | Status |
|---|---|---|
| `docs/hardware/io-budget.md` | Das Ergebnis von Task 1–2: Control-Inventar, Reduktionsentscheidung, Pin-Rechnung | neu |
| `tools/count_panel_controls.py` | Zählt und klassifiziert die Panel-Parameter aus `gen_panel.py`, reproduzierbar | neu |
| `tools/test_count_panel_controls.py` | Test für den Zähler | neu |
| `src/hw/board.h` | Board-Abstraktion: Seed vs. Patch SM hinter einem Typ-Alias und einer `init()`. Neutraler Ort, den Bench **und** Shell einbinden dürfen | neu |
| `bench/main.cpp` | Board-Init über `src/hw/board.h` statt direkt `DaisySeed` | ändern |
| `bench/Makefile` | `BENCH_BOARD=seed\|patch_sm` als Auswahlschalter, `-I../src/` neu auf dem Include-Pfad | ändern |
| `bench/run.py` | `--board` durchreichen, Board in Dateiname und CSV-Zeile | ändern |
| `shell/` | Die erste Firmware, die `engine/` kompiliert. Eigenes Makefile, eigene `main.cpp` | neu |
| `shell/main.cpp` | Board-Init, SDRAM-`FxMem`, Audio-Callback mit `Instrument::process()` | neu |
| `shell/Makefile` | Firmware-Build für Patch SM, `APP_TYPE = BOOT_SRAM` | neu |
| `shell/sdram_mem.h` | Die SDRAM-Allokation für `FxMem` an einem Ort | neu |
| `shell/controls.h` / `.cpp` | Mux-Scan → normalisierte Werte → Engine-Setter | neu |
| `docs/bench/` | Neue Captures, jetzt mit Board-Kennung im Namen | wächst |
| `docs/roadmap.md` | Phase-0-Ergebnis in den Status | ändern |

---

### Task 1: Das Control-Inventar reproduzierbar zählen

Bevor irgendetwas reduziert wird, muss die Ausgangszahl belastbar und wiederholbar sein. Eine Handzählung veraltet beim nächsten Panel-Commit.

**Files:**
- Create: `tools/count_panel_controls.py`
- Test: `tools/test_count_panel_controls.py`

**Interfaces:**
- Consumes: `host/vcv/res/gen_panel.py` — die Listen `PANEL_PARAMS`, `APPENDED_PANEL_PARAMS`, `HIDDEN_PARAMS`, `PART_A`, `PART_B`, `SHARED`, `INPUTS`, `OUTPUTS`, `LIGHTS`
- Produces: `count_controls() -> dict[str, int]` mit den Schlüsseln `panel`, `appended`, `hidden`, `runtime`, `part_a`, `part_b`, `shared`, `inputs`, `outputs`, `lights`

- [ ] **Schritt 1: Den fehlschlagenden Test schreiben**

```python
# tools/test_count_panel_controls.py
import count_panel_controls as c


def test_runtime_is_panel_plus_appended():
    counts = c.count_controls()
    assert counts["runtime"] == counts["panel"] + counts["appended"]


def test_parts_are_symmetric():
    counts = c.count_controls()
    assert counts["part_a"] == counts["part_b"]


def test_known_baseline_2026_08_07():
    # Baseline am Tag der Phase-0-Planung. Wenn diese Zeile rot wird, hat
    # sich das Panel geaendert -- dann io-budget.md nachziehen, nicht den
    # Test aufweichen.
    counts = c.count_controls()
    assert counts["runtime"] == 82
    assert counts["part_a"] == 23
    assert counts["shared"] == 16
```

- [ ] **Schritt 2: Test laufen lassen, Fehlschlag bestätigen**

```bash
cd tools && python -m pytest test_count_panel_controls.py -v
```

Erwartet: FAIL mit `ModuleNotFoundError: No module named 'count_panel_controls'`

- [ ] **Schritt 3: Den Zähler schreiben**

```python
# tools/count_panel_controls.py
"""Zaehlt die Bedienelemente des VCV-Panels.

Der Panelgenerator host/vcv/res/gen_panel.py ist die Autoritaet darueber,
welche Controls das Instrument hat. Dieses Skript liest ihn, statt die
Zahlen von Hand zu pflegen -- eine Handzaehlung veraltet beim naechsten
Panel-Commit, und die Hardware-Reduktion haengt an der Zahl.
"""
import sys
from pathlib import Path

_RES = Path(__file__).resolve().parent.parent / "host" / "vcv" / "res"


def _panel_module():
    sys.path.insert(0, str(_RES))
    import gen_panel
    return gen_panel


def count_controls():
    g = _panel_module()
    return {
        "panel": len(g.PANEL_PARAMS),
        "appended": len(g.APPENDED_PANEL_PARAMS),
        "hidden": len(g.HIDDEN_PARAMS),
        "runtime": len(g.RUNTIME_PANEL_PARAMS),
        "part_a": len(g.PART_A),
        "part_b": len(g.PART_B),
        "shared": len(g.SHARED),
        "inputs": len(g.INPUTS),
        "outputs": len(g.OUTPUTS),
        "lights": len(g.LIGHTS),
    }


def main():
    for key, value in count_controls().items():
        print(f"{key:12} {value}")


if __name__ == "__main__":
    main()
```

- [ ] **Schritt 4: Test laufen lassen, GRÜN bestätigen**

```bash
cd tools && python -m pytest test_count_panel_controls.py -v
```

Erwartet: 3 passed

- [ ] **Schritt 5: RED einmal beweisen**

`test_known_baseline_2026_08_07` muss rot werden können. Kurz `82` auf `83` ändern, Test laufen lassen (erwartet: FAIL), zurückändern, Test laufen lassen (erwartet: PASS).

- [ ] **Schritt 6: Commit**

```bash
git add tools/count_panel_controls.py tools/test_count_panel_controls.py
git commit -m "tools: the panel counts itself, because the hardware has to fit it

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: Die Panel-Reduktion entscheiden und das Pin-Budget rechnen

**Das ist der inhaltliche Kern von Phase 0 und eine Design-Entscheidung, keine Implementierung.** 82 Runtime-Parameter müssen auf die 45–55 Bedienelemente, die 42 HP tragen. Diese Aufgabe erzeugt ein Dokument, keinen Code.

> **Vor diesem Task: eigene Brainstorming-Runde.** Die Reduktionsmechanismen (Lane-Select statt fünf Lane-Reihen, Hold-Layer, ALT-Gesten, geteilte Reihen zwischen Parts) sind Instrumentendesign, und die Entscheidung gehört Bastian. Ein Implementierer soll sie **nicht** allein treffen. Was hier steht, ist die Struktur des Ergebnisdokuments und die Rechnung, die es enthalten muss.

**Files:**
- Create: `docs/hardware/io-budget.md`

**Interfaces:**
- Consumes: `tools/count_panel_controls.py` aus Task 1, die Patch-SM-Pinbelegung aus `lib/libDaisy/src/daisy_patch_sm.h`
- Produces: die Zahlen `n_pots`, `n_pads`, `n_leds`, `n_encoders`, `n_jacks`, `n_mux_chips` — Task 5 und die gesamte PCB-Arbeit ab September hängen daran

- [ ] **Schritt 1: Die Ausgangslage in das Dokument schreiben**

```bash
cd tools && python count_panel_controls.py
```

Die Ausgabe kommt wörtlich als erste Tabelle nach `docs/hardware/io-budget.md`, mit Datum und Git-Hash.

- [ ] **Schritt 2: Jeden der 82 Parameter klassifizieren**

Eine Tabelle mit einer Zeile pro Parameter und genau einer Einstufung:

| Einstufung | Bedeutung | Hardware-Konsequenz |
|---|---|---|
| `KNOB` | eigener Poti auf dem Panel | 1 Mux-Kanal |
| `FADER` | eigener Fader | 1 Mux-Kanal |
| `PAD` | Taster oder Touch-Pad | 1 Mux-Kanal oder MPR121-Kanal |
| `ENCODER` | Endlosgeber | 2 GPIO + 1 Taster |
| `LAYER` | erreichbar nur über Hold/ALT-Geste | 0 zusätzliche Kanäle |
| `SELECT` | über Lane-/Deck-Auswahl geteilt | teilt sich einen Kanal mit n anderen |
| `MENU` | nur über Settings/Preset erreichbar | 0 Kanäle |
| `CUT` | auf Hardware nicht vorhanden | 0 Kanäle, Firmware-Default |

**Regel:** Jeder Parameter bekommt genau eine Einstufung. Die Summe der Zeilen muss 82 ergeben — das ist die Prüfsumme, die verhindert, dass etwas stillschweigend verschwindet.

- [ ] **Schritt 3: Die Bedienelemente zählen und gegen die Fläche prüfen**

```
n_pots     = Anzahl KNOB
n_faders   = Anzahl FADER
n_pads     = Anzahl PAD
n_encoders = Anzahl ENCODER
n_bedien   = n_pots + n_faders + n_pads + n_encoders
```

Gegenrechnung, die im Dokument stehen muss: nutzbare Fläche 213 × 115 mm, gewähltes Raster in mm, daraus die geometrische Kapazität. **`n_bedien` muss darunter liegen, mit Rand für Buchsen und Beschriftung.** Liegt es darüber, ist die Reduktion nicht fertig — zurück zu Schritt 2.

- [ ] **Schritt 4: Das Pin-Budget gegen das Patch Submodule rechnen**

```
Mux-Kanäle nötig     = n_pots + n_faders + (PADs auf Mux)
Mux-Chips            = aufgerundet(Mux-Kanäle / 8)   bei 74HC4051
                     = aufgerundet(Mux-Kanäle / 16)  bei CD74HC4067
ADC-Pins             = Anzahl Mux-Chips (1 Signal-Pin je Chip)
Adress-GPIO          = 3 bei 4051, 4 bei 4067  (chipübergreifend geteilt)
LED-Pins             = 1 bei WS2812-Kette (src/hw/ws2812.cpp existiert)
Encoder-GPIO         = n_encoders * 3
Gate/Clock-GPIO      = Anzahl digitaler Buchsen
```

Die Verfügbarkeit gegenprüfen an `lib/libDaisy/src/daisy_patch_sm.h` — dort stehen die tatsächlich herausgeführten Pins. **Ergebniszeile im Dokument: „X ADC-Pins nötig, Y verfügbar, Z Reserve."** Auf `n_bedien` sind **20 % Reserve** aufzuschlagen, bevor die Rechnung als bestanden gilt; im Layout kommt erfahrungsgemäß noch ein Taster dazu.

- [ ] **Schritt 5: Die Buchsen festlegen**

Aus `gen_panel.py`: 4 Inputs (`IN_L`, `IN_R`, `CLOCK`, `RESET`), 6 Outputs (`OUT_L`, `OUT_R`, `PITCH_A`, `GATE_A`, …). Für jede Buchse eine Zeile: Audio oder CV oder Gate, Richtung, und auf welchen Patch-SM-Anschluss sie geht. Audio-I/O und CV-Wandler sind auf dem Submodule bereits vorhanden — das ist der Grund, dieses Modul zu nehmen, und muss in der Rechnung auftauchen.

- [ ] **Schritt 6: Commit**

```bash
git add docs/hardware/io-budget.md
git commit -m "docs(hardware): eighty-two controls meet forty-two horizontal pitch

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: Die Bench auf ein zweites Board vorbereiten

Die Bench kennt heute nur `daisy::DaisySeed`. Bevor auf dem Submodule gemessen werden kann, braucht sie eine Board-Auswahl — ohne die bestehenden Seed-Messungen zu verändern, denn die sind die Vergleichsbasis.

**Files:**
- Create: `src/hw/board.h`
- Modify: `bench/main.cpp:1-46`
- Modify: `bench/Makefile`
- Modify: `bench/run.py`

> **Warum `src/hw/` und nicht `bench/`:** Task 5 baut die Produktions-Firmware und braucht dieselbe Board-Initialisierung. Läge sie unter `bench/`, würde ausgelieferte Firmware auf das Messwerkzeug zeigen — genau die Trennung, die `bench/README.md` ausdrücklich zieht. Eine Kopie in beiden Bäumen wäre schlimmer: der `boot_info`-Sonderfall darunter ist eine Falle, die man kein zweites Mal pflegen will. `src/hw/` ist neutral, steht im Root-Makefile schon auf dem Include-Pfad, und die Bench bekommt `-I../src/` dazu.

**Interfaces:**
- Consumes: libDaisy — `daisy::DaisySeed` und `daisy::patch_sm::DaisyPatchSM`
- Produces: `bench::Board` (Typ-Alias), `bench::board_init()` (bringt Takt, Caches, SDRAM hoch), `bench::board_name()` → `"seed"` oder `"patch_sm"`

- [ ] **Schritt 1: Die Board-Abstraktion schreiben**

Der bestehende `main.cpp` enthält einen kommentierten Sonderfall, der **erhalten bleiben muss**: `System::InitBackupSram()` plus das Stempeln von `boot_info.version` auf `v6_1`, damit `Init()` Takt und SDRAM wirklich hochbringt statt sie als „vom Bootloader erledigt" zu überspringen. Ohne das gibt es einen HardFault, sobald der erste Workload SDRAM anfasst. Dieser Block wandert unverändert in `src/hw/board.h`.

```cpp
// src/hw/board.h
#pragma once

// Die Bench kennt zwei Boards. Der Seed traegt die gesamte Messhistorie in
// docs/bench/; das Patch Submodule ist das M6-Ziel. Gleicher STM32H750,
// gleiches SDRAM und QSPI -- aber bis Task 4 ist das eine Erwartung und
// keine Messung, und deshalb steht das Board in jeder Zeile des Reports.

#if defined(BENCH_BOARD_PATCH_SM)
#include "daisy_patch_sm.h"
#else
#include "daisy_seed.h"
#endif

namespace bench {

#if defined(BENCH_BOARD_PATCH_SM)
using Board = daisy::patch_sm::DaisyPatchSM;
inline const char* board_name() { return "patch_sm"; }
#else
using Board = daisy::DaisySeed;
inline const char* board_name() { return "seed"; }
#endif

// BOOT_SRAM-Loads springen ueber den Debug-Probe direkt in den Reset-Vektor
// und umgehen den Daisy-Bootloader. Init() schliesst aus einem leeren
// boot_info-Marker, ein alter Bootloader (<v6.0) haette SDRAM schon
// hochgebracht, und ueberspringt daraufhin ConfigureClocks(),
// ConfigureMpu() UND sdram_handle.Init(). Ein bekannter Bootloader-Stempel
// vor Init() setzt diese Annahme ausser Kraft. Ohne das: HardFault beim
// ersten SDRAM-Zugriff (bestaetigt auf Hardware 2026-07-18).
inline void board_init(Board& hw)
{
    daisy::System::InitBackupSram();
    daisy::boot_info.version = daisy::System::BootInfo::Version::v6_1;

#if defined(BENCH_BOARD_PATCH_SM)
    hw.Init();
#else
    hw.Init(true);              // 480 MHz boost, caches on, SDRAM up
#endif
    hw.SetAudioBlockSize(96);
    hw.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
}

} // namespace bench
```

> **Untersuchungsschritt, nicht überspringen:** `DaisyPatchSM::Init()` nimmt kein Boost-Argument und konfiguriert Takt und SDRAM selbst. Vor dem Übersetzen in `lib/libDaisy/src/daisy_patch_sm.cpp` nachlesen, ob es tatsächlich auf 480 MHz geht und ob `SetAudioBlockSize`/`SetAudioSampleRate` mit derselben Signatur existieren. Falls nicht: die Abweichung in `src/hw/board.h` kommentieren, nicht stillschweigend anpassen — jede Abweichung im Takt macht den Vergleich mit den Seed-Zahlen ungültig.

- [ ] **Schritt 2: `main.cpp` auf die Abstraktion umstellen**

In `bench/main.cpp` ersetzen: `#include <daisy_seed.h>` durch `#include "hw/board.h"`, `static daisy::DaisySeed hw;` durch `static bench::Board hw;`, und die sechs Zeilen von `InitBackupSram()` bis `SetAudioSampleRate(...)` durch `bench::board_init(hw);`. Die Signatur `void run_anchors(daisy::DaisySeed& hw)` in `bench/anchor.cpp` wird zu `void run_anchors(bench::Board& hw)`.

- [ ] **Schritt 3: Den Board-Schalter in Makefile und Runner**

`bench/Makefile`: Variable `BENCH_BOARD ?= seed`, und bei `patch_sm` sowohl `-DBENCH_BOARD_PATCH_SM` in `C_DEFS` als auch die Patch-SM-Quellen von libDaisy in den Build. `bench/run.py`: `--board {seed,patch_sm}` mit Default `seed`, durchgereicht an `make`, aufgenommen in den Dateinamen (`YYYY-MM-DD-<hash>-<profile>-<board>-<layout>-<optimization>.md`) und als Spalte in jede CSV-Zeile.

- [ ] **Schritt 4: Regression auf dem Seed prüfen — das ist der eigentliche Test**

Vor jeder Submodule-Messung muss bewiesen sein, dass der Umbau die bestehende Messkette nicht verändert hat.

```bash
cd bench && python run.py --profile regress --optimization o3
```

Erwartet: Der Lauf gelingt, und `instrument_worst_bbd_dtcm` liegt innerhalb des Wiederholbands der Referenz aus `docs/bench/2026-08-04-bd01608-regress-axi-o3.md` (96,43 % offline). **Weicht die Zahl ab, ist der Umbau schuld, nicht das Board.** Dann zurück zu Schritt 1, nicht weiter.

- [ ] **Schritt 5: Frisches Objekt verifizieren**

Die Bench kann still ein veraltetes Objekt relinken. Nicht der Memory-Tabelle glauben:

```bash
grep -n "board" bench/build/bench.map | head
```

Erwartet: die neue Übersetzungseinheit taucht auf.

- [ ] **Schritt 6: Commit**

```bash
git add src/hw/board.h bench/main.cpp bench/anchor.cpp bench/Makefile bench/run.py
git commit -m "bench: the harness learns there is a second board

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: Die erste Messung auf dem Patch Submodule

Der Moment, in dem aus „sollte übertragen" eine Zahl wird.

**Files:**
- Create: `docs/bench/YYYY-MM-DD-<hash>-regress-patch_sm-axi-o3.md` und `.csv` (vom Runner erzeugt)
- Create: `docs/bench/2026-08-XX-seed-vs-patch-sm.md` (der Vergleich, von Hand)

**Interfaces:**
- Consumes: `bench::Board`, `bench::board_init()`, `--board` aus Task 3
- Produces: die Antwort auf „übertragen die Seed-Zahlen?" — Eingang für Task 6

- [ ] **Schritt 1: Den QSPI-Wavetable-Bank auf das Submodule programmieren**

Die Bench prüft beim Start den SHA-256 des QSPI-Payloads gegen das gelinkte Image. Ein frisches Board hat einen leeren QSPI, und der Lauf schlägt an dieser Prüfung fehl — das ist gewollt, kein Fehler. Vorgehen nach `bench/qspi_programmer/`.

- [ ] **Schritt 2: Den Lauf ausführen**

```bash
cd bench && python run.py --profile regress --board patch_sm --optimization o3
```

Erwartet: zwei Wiederholungen, übereinstimmende Zeilenmengen und Checksummen, Capture in `../docs/bench/`.

- [ ] **Schritt 3: Bei Fehlschlag — die drei bekannten Fallen zuerst**

Nicht ins Blaue debuggen. In dieser Reihenfolge prüfen:
1. **HardFault beim ersten SDRAM-Zugriff** → der `boot_info`-Stempel greift auf diesem Board nicht; `bench::board_init()` prüfen.
2. **QSPI-Digest-Mismatch** → Schritt 1 nicht oder mit falschem Bank-Stand ausgeführt.
3. **OpenOCD verbindet nicht** → `bench/openocd/` erwartet eine Seed-Konfiguration; das Submodule braucht möglicherweise eine eigene `.cfg`.

Jede Abweichung, die eine Änderung an `src/hw/board.h` erzwingt, wird dort kommentiert und Task 3 Schritt 4 wird erneut gefahren.

- [ ] **Schritt 4: Den Vergleich schreiben**

`docs/bench/2026-08-XX-seed-vs-patch-sm.md`: Zeile für Zeile Seed gegen Patch SM, dieselbe Profil-, Layout- und Optimierungsidentität, Differenz in Prozentpunkten. Die Kopfzeile beantwortet genau eine Frage: **übertragen die Zahlen, ja oder nein.** Bewegt sich `instrument_worst_bbd_dtcm` um mehr als das Wiederholband, gilt: **keine Seed-Zahl darf mehr für Submodule-Aussagen zitiert werden**, und `docs/roadmap.md` bekommt eine entsprechende Warnung.

- [ ] **Schritt 5: Commit**

```bash
git add docs/bench/
git commit -m "bench(patch-sm): the first cycles ever counted on the target board

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Die erste Firmware, die die Engine enthält

Das Root-`Makefile` baut die Upstream-Spotykach-Firmware; `engine/**` steht nicht in ihren Quellen. Diese Aufgabe erzeugt den kleinsten Shell, der die Engine auf dem Submodule hörbar macht — **kein UI, keine Panel-Logik, kein Preset-System.** Nur: Board hoch, SDRAM-Speicher injizieren, `process()` im Callback.

**Files:**
- Create: `shell/main.cpp`
- Create: `shell/sdram_mem.h`
- Create: `shell/Makefile`
- Create: `shell/README.md`

**Interfaces:**
- Consumes: `spky::Instrument` aus `engine/instrument.h` — `init(float sample_rate, const FxMem& mem)`, `set_tempo_bpm(float)`, `set_rate(int, float)` und die übrigen normalisierten Setter, `process(const float* inL, const float* inR, float* outL, float* outR, size_t n)`
- Produces: ein flashbares `shell.bin`, das auf dem Submodule Ton macht — Basis für Task 6

- [ ] **Schritt 1: Die SDRAM-Allokation an einen Ort legen**

`FxMem` verlangt laut `engine/instrument.h` pro Part zwei Echo-Puffer, einen Sampler-Puffer von rund 42 s bei 48 kHz (≈ 16 MB pro Part) und zwei BBD-Leitungen à `BbdEngine::kCells` Floats (32 KB je Leitung, 128 KB fürs Instrument), dazu die Reverb-Instanz.

```cpp
// shell/sdram_mem.h
#pragma once

// Die Engine hat keinen Heap (engine/instrument.h, "No heap"): sie bekommt
// ihren Speicher injiziert. Auf Daisy kommt er aus SDRAM. Der Sampler ist
// der grosse Posten -- ~16 MB pro Part -- und passt nur, weil das Submodule
// 64 MB traegt. Wer hier eine Zahl aendert, aendert das Instrument.

#include "instrument.h"

namespace shell {

constexpr size_t kSamplerFrames = 42 * 48000;   // Spec-Sizing, 42 s @ 48 kHz

spky::FxMem& sdram_fx_mem();   // einmalig gebaut, lebt bis zum Reset

} // namespace shell
```

Die Definition legt jeden Puffer als `DSY_SDRAM_BSS`-Array an und verdrahtet die Zeiger in ein statisches `FxMem`. Die Reverb-Instanz kommt ebenfalls nach SDRAM.

- [ ] **Schritt 2: Den Shell schreiben**

```cpp
// shell/main.cpp
#include "../src/hw/board.h"
#include "sdram_mem.h"
#include "instrument.h"

static bench::Board hw;
static spky::Instrument inst;

static void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           size)
{
    inst.process(in[0], in[1], out[0], out[1], size);
}

int main(void)
{
    bench::board_init(hw);
    inst.init(48000.0f, shell::sdram_fx_mem());
    inst.set_tempo_bpm(96.0f);
    // Fester Betriebspunkt, damit ohne Bedienelemente ueberhaupt etwas
    // klingt. Task 6 ersetzt die feste Zeile durch einen echten Poti.
    inst.set_rate(spky::PART_A, 0.4f);
    inst.set_density(spky::PART_A, 0.6f);
    hw.StartAudio(AudioCallback);
    while(1) {}
}
```

- [ ] **Schritt 3: Das Makefile schreiben**

Vorlage ist `bench/Makefile`, nicht das Root-`Makefile`: gleiche `APP_TYPE = BOOT_SRAM`, gleiches `LDSCRIPT = ../alt_sram.lds`, gleiche Include-Pfade auf `../engine/`, `../third_party/`, `../lib/DaisySP/Source/`, gleiche `C_USR_FLAGS = -ffast-math -funroll-loops`, `BENCH_BOARD_PATCH_SM` gesetzt. **`USE_DAISYSP_LGPL` bleibt aus** — anders als die Bench ist das hier auslieferbare Firmware.

- [ ] **Schritt 4: Bauen und flashen**

```bash
cd shell && make -j8
```

Erwartet: `build/shell.bin` entsteht. Ein Link-Fehler mit SRAM- oder SDRAM-Region-Overflow ist an dieser Stelle plausibel — die Bench kennt dasselbe Problem beim `full`-Profil. Dann **nicht** Puffergrößen raten, sondern `build/shell.map` lesen und den Posten benennen, der überläuft.

- [ ] **Schritt 5: Hören**

Board flashen, Kopfhörer oder Monitor an den Audioausgang. Erwartet: ein hörbares, sich bewegendes Signal. Das ist der Beweis, dass die Engine auf dem Submodule läuft — der erste in diesem Projekt.

- [ ] **Schritt 6: `shell/README.md` schreiben**

Drei Absätze: was der Shell ist (die erste Firmware mit `engine/`), was er ausdrücklich nicht ist (kein UI, kein Preset, kein Panel), und wie man ihn baut und flasht. Dazu die Abgrenzung gegen `bench/` und gegen die Upstream-Firmware im Root, damit der nächste Leser die drei nicht verwechselt.

- [ ] **Schritt 7: Commit**

```bash
git add shell/
git commit -m "shell: the engine leaves the computer

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: Ein Poti über den Mux, und der Shell-Aufschlag in Zahlen

Die Zahl, für die Phase 0 existiert: was kosten Mux-Scan und LED-Ausgabe auf die 3,57 Punkte Reserve.

**Files:**
- Create: `shell/controls.h`, `shell/controls.cpp`
- Create: `tests/test_controls_map.cpp`
- Modify: `shell/main.cpp`
- Modify: `docs/roadmap.md`

**Interfaces:**
- Consumes: `shell::sdram_fx_mem()`, `spky::Instrument`, `infrasonic::ShiftRegister165` aus `src/hw/sr_165.h`, `infrasonic::Ws2812` aus `src/hw/ws2812.h`
- Produces: `shell::Controls::scan()` (blockierend, außerhalb des Audio-Callbacks), `shell::Controls::value(int idx) -> float` normalisiert 0..1, `shell::map_control(int idx, float v, spky::Instrument&)`

- [ ] **Schritt 1: Den Test für die Zuordnungstabelle schreiben**

Die Abbildung Kanal → Engine-Setter ist reine Datenlogik und gehört auf den Host getestet, bevor Hardware im Spiel ist. Vorbild sind die bestehenden doctest-Tests in `tests/`.

```cpp
// tests/test_controls_map.cpp
#include "doctest.h"
#include "../shell/controls.h"
#include "instrument.h"

TEST_CASE("channel zero drives part A rate") {
    spky::Instrument inst;
    inst.init(48000.0f);
    shell::map_control(0, 0.75f, inst);
    CHECK(inst.rate(spky::PART_A) == doctest::Approx(0.75f));
}

TEST_CASE("an out-of-range channel changes nothing") {
    spky::Instrument inst;
    inst.init(48000.0f);
    const float before = inst.rate(spky::PART_A);
    shell::map_control(9999, 1.0f, inst);
    CHECK(inst.rate(spky::PART_A) == doctest::Approx(before));
}
```

> Falls `Instrument` keinen Lese-Zugriff auf `rate` hat, wird der Test über einen vorhandenen Observer geführt oder ein `const`-Getter analog zu `form(int)` und `song(int)` ergänzt — **nicht** über `SPKY_TESTING`-Sonderpfade, und nicht durch Aufweichen des Tests.

- [ ] **Schritt 2: Test laufen lassen, Fehlschlag bestätigen**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure -R controls
```

Erwartet: FAIL — `shell/controls.h` existiert nicht.

- [ ] **Schritt 3: Die Zuordnung minimal implementieren**

`shell/controls.h` deklariert `map_control(int idx, float v, spky::Instrument& inst)`; `controls.cpp` implementiert sie als `switch` über den Kanalindex, zunächst mit genau einem belegten Kanal (0 → `set_rate(PART_A, v)`) und einem `default:`, der nichts tut. Die vollständige Tabelle folgt erst, wenn Task 2 entschieden hat, welche Kanäle es überhaupt gibt.

- [ ] **Schritt 4: Test laufen lassen, GRÜN bestätigen**

```bash
ctest --test-dir build --output-on-failure -R controls
```

Erwartet: 2 passed. Danach RED einmal beweisen: `PART_A` im Mapping auf `PART_B` ändern, Test läuft rot, zurückändern.

- [ ] **Schritt 5: Den Mux physisch anschließen**

Verwendet wird der vorhandene **`74HC4051`**, 8 Kanäle. Der Aufbau steht auf dem Breadboard, es wird nichts bestellt.

| Pin | geht an |
|---|---|
| `VCC` | 3V3 des Submodule |
| `GND`, `VEE`, `INH` | GND (`VEE` auf GND, weil nur unipolare Signale 0–3,3 V geschaltet werden; `INH` low hält den Chip dauerhaft freigegeben) |
| `A`, `B`, `C` | drei GPIO — das sind die Adressleitungen, die sich später **alle** Muxe teilen |
| `COM` | ein ADC-Pin des Submodule |
| `Y0` | Schleifer eines 10-k-Potis, dessen Enden an 3V3 und GND |
| `Y1`…`Y7` | vorerst über je 10 k gegen GND oder 3V3, abwechselnd |

Die abwechselnd auf die Extreme gezogenen Nachbarkanäle sind kein Beiwerk, sondern der Prüfaufbau für Schritt 5b: nur wenn links und rechts vom gemessenen Kanal das Gegenteil anliegt, wird ein zu kurzes Settling überhaupt sichtbar.

Dazu **100 nF** direkt zwischen `VCC` und `GND` am Chip, und **10 nF** von `COM` gegen GND als Puffer für das Sample-and-Hold des ADC.

Nicht verwenden: **`CD4051B`** oder andere klassische CMOS-Typen. Das Submodule läuft auf 3,3 V, wo diese Typen einen deutlich höheren Durchlasswiderstand haben — das verschleppt die Einschwingzeit beim Kanalwechsel und erzeugt zappelige ADC-Werte, die man dann für einen Software-Fehler hält. Das `HC` im Namen ist der Unterschied; der Aufdruck auf dem Chip zählt, nicht die Artikelbeschreibung.

- [ ] **Schritt 5b: Die Einschwingzeit messen**

Das ist die zweite Zahl, die Phase 0 liefern muss, und sie ist mehr wert als die CPU-Prozente: **wie lange muss nach dem Adresswechsel gewartet werden, bevor der Wert stabil ist.** Sie multipliziert sich mit der Kanalzahl des gesamten Instruments.

Vorgehen: Adresse auf Kanal 0 setzen, definiert warten, lesen — die Wartezeit von großzügig (z. B. 50 µs) in Schritten halbieren und beobachten, ab wann der gelesene Wert vom Nachbarkanal beeinflusst wird. Das Poti dabei auf eine Mittelstellung, weil ein Übersprechen dort am deutlichsten auffällt.

Festgehalten wird die kürzeste Wartezeit, bei der über 1000 Messungen die Streuung unter einem LSB-Rauschband bleibt, plus 50 % Sicherheit. Diese Zahl geht in `docs/hardware/io-budget.md` und ist ab dann die Konstante, mit der jede Panelgröße gegengerechnet wird.

- [ ] **Schritt 6: Scan in den Shell einbauen und hören**

`Controls::scan()` im Hauptloop aufrufen (nicht im Audio-Callback — der 74HC165-Treiber in `src/hw/sr_165.h` ist ausdrücklich blockierend), Wert über `map_control` an die Engine. Erwartet: der Poti verändert hörbar die Rate von Part A.

- [ ] **Schritt 7: Den Shell-Aufschlag messen**

Das ist der Kern. Die CPU-Last mit und ohne Shell-Arbeit im selben Bild vergleichen:

1. Zykluszähler um den Audio-Callback legen (`bench/cycles.h` liefert das Verfahren).
2. Messen mit reinem `process()`.
3. Messen mit zusätzlich laufendem Mux-Scan über alle 8 Kanäle. Daraus **Kosten pro Kanal** bilden und auf die Kanalzahl aus Task 2 hochrechnen. Zusätzlich mit 4 statt 8 Kanälen messen: skaliert die Zeit linear, ist die Hochrechnung tragfähig; bleibt ein Fixanteil stehen, gehört der separat ausgewiesen, weil er sich bei mehreren Chips **nicht** vervielfacht.
4. Messen mit zusätzlich getriebener WS2812-Kette in voller geplanter LED-Zahl aus Task 2 — **WS2812 ist der teure Posten und der wahrscheinlichste Grund, warum 3,57 Punkte nicht reichen.**

Ergebnis als Tabelle nach `docs/bench/`, mit derselben Sorgfalt wie ein Bench-Capture: Board, Git-Hash, Optimierung, zwei Wiederholungen.

- [ ] **Schritt 8: Das Urteil in die Roadmap schreiben**

`docs/roadmap.md` bekommt einen Phase-0-Abschnitt mit fünf Zeilen: Seed-gegen-Submodule-Verdikt aus Task 4, Einschwingzeit pro Kanal aus Schritt 5b, Shell-Aufschlag in Prozentpunkten hochgerechnet auf die reale Kanalzahl, verbleibende Reserve, und — falls die Reserve aufgebraucht ist — welche Engine-Auswahl oder Sample-Rate-Entscheidung ansteht. **Diese Entscheidung fällt im August und nicht im Januar**, weil sie jetzt nichts kostet und nach dem PCB-Layout alles.

- [ ] **Schritt 9: Commit**

```bash
git add shell/ tests/test_controls_map.cpp docs/bench/ docs/roadmap.md
git commit -m "shell: one knob, and the price of the shell in points

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Reihenfolge und Parallelität

Task 1 → 2 ist der Papier-Strang und entsperrt die PCB-Arbeit. Task 3 → 4 → 5 → 6 ist der Mess-Strang. Beide Stränge können am selben Tag beginnen; nur Task 6 Schritt 7 braucht die LED-Zahl aus Task 2.

**Wenn die Zeit knapp wird, hat der Papier-Strang Vorrang.** Task 1–2 entscheidet, ob im September ein Testcoupon entworfen werden kann; Task 3–6 entscheidet nur, wie viel Engine am Ende darauf läuft. Ein verspäteter Mess-Strang kostet Erkenntnis, ein verspäteter Papier-Strang kostet den ganzen Zeitplan.

## Definition of Done für Phase 0

- [ ] `docs/hardware/io-budget.md` existiert, die Klassifikation summiert sich auf 82, und die Pin-Rechnung geht mit 20 % Reserve auf
- [ ] Ein Bench-Capture mit `board=patch_sm` liegt in `docs/bench/`
- [ ] `shell/` baut, flasht und macht auf dem Submodule Ton
- [ ] Ein Poti verändert über den `74HC4051` hörbar einen Engine-Parameter
- [ ] Die Einschwingzeit pro Kanal ist gemessen und steht in `docs/hardware/io-budget.md`
- [ ] Der Shell-Aufschlag steht als Zahl in `docs/roadmap.md`, hochgerechnet auf die reale Kanalzahl, mit Urteil über die verbleibende Reserve
