# Bench auf dem Patch Submodule — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Die Bench misst auf dem Daisy Patch Submodule, und der Vergleich gegen die Seed-Historie steht als Zahl auf Papier.

**Architecture:** Eine Board-Abstraktion in `src/hw/board.h` versteckt die Unterschiede zwischen `daisy::DaisySeed` und `daisy::patch_sm::DaisyPatchSM` hinter einem Typ-Alias und einer `board_init()`. Bench-Makefile und Runner bekommen einen Board-Schalter; jedes Capture trägt das Board im Dateinamen und in jeder CSV-Zeile. Der eigentliche Test des Umbaus ist eine Seed-Regression: dieselbe Zahl wie vorher, sonst ist der Umbau schuld.

**Tech Stack:** ARM GCC über `make`, libDaisy, OpenOCD mit ST-Link V3 über `dapdirect_swd`, Semihosting als Transport, `bench/run.py` als Runner.

**Umfang:** Ein Arbeitstag, 8. August 2026. Das sind Task 3 und Task 4 aus `2026-08-07-fireflow-phase-0-hardware-foundation.md`, hier auf Schrittgröße aufgelöst.

## Global Constraints

- **Toolchain: ARM GCC über `make`.** Niemals `source env.sh` in derselben Shell — das ist die clang-Umgebung für Engine und Tests, und die beiden werden nicht gemischt.
- **`bench/` fasst die Produktions-Firmware nicht an** und umgekehrt. Getrennte Makefiles, getrennte `main.cpp`.
- **Die Bench kann still ein veraltetes Objekt relinken.** Neue Übersetzungseinheiten über `build/bench.map` verifizieren, nie über die Memory-Tabelle.
- **Alle bisherigen Bench-Zahlen stammen vom Daisy Seed.** Keine Zahl aus `docs/bench/` darf als Submodule-Messung zitiert werden, bis Task 4 sie dort reproduziert hat.
- **Referenzzahl:** `instrument_worst_bbd_dtcm` bei **96,43 % offline**, Profil `regress`, Layout `axi`, `-O3` — aus `docs/bench/2026-08-04-bd01608-regress-axi-o3.md`.
- **Commit-Trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **`--repeat` bleibt auf dem Default 2.** Ein Capture aus einem einzelnen Lauf ist kein Capture.

## Vorab nachgeschlagen — vier Annahmen, geprüft

Der Phase-0-Plan führt diese als Untersuchungsschritte. Sie sind erledigt; zwei fallen anders aus als dort geschätzt.

| Frage | Befund | Konsequenz |
|---|---|---|
| Geht `DaisyPatchSM::Init()` auf 480 MHz? | **Ja.** `daisy_patch_sm.cpp:230` ruft `syscfg.Boost()` unbedingt auf, ohne Argument und ohne Ausweg. | Taktgleichheit mit `DaisySeed::Init(true)` ist gegeben, der Vergleich ist gültig. |
| Existieren `SetAudioBlockSize` / `SetAudioSampleRate`? | **Ja**, `daisy_patch_sm.h:92` und `:100`, mit `size_t` und `SaiHandle::Config::SampleRate`. | Der `board.h`-Entwurf übersetzt unverändert. |
| Braucht das Submodule eine eigene OpenOCD-Konfiguration? | **Nein.** `bench/run.py:117` lädt `target/stm32h7x.cfg` — das ist MCU-Ebene, nicht Board-Ebene. `spotykach-sram.cfg` setzt VTOR fest auf `0x24000000`, den SRAM_EXEC-Sockel, und der ist bei beiden Boards derselbe H750. | Die dritte der „drei bekannten Fallen" aus dem Phase-0-Plan entfällt. Bleiben zwei. |
| Müssen Patch-SM-Quellen in den Bench-Build? | **Nein.** `daisy_patch_sm` steht in `lib/libDaisy/Makefile:30` bei den `CPP_MODULES` und liegt bereits in `libdaisy.a`. | Das Makefile braucht nur `-DBENCH_BOARD_PATCH_SM` und `-I../src/`. Keine Quellenliste anfassen. |

Der `boot_info`-Sonderfall gilt auf dem Submodule **gleichermaßen**: `Init()` fragt `System::GetBootloaderVersion()` ab und überspringt bei `LT_v6_0` außerhalb des internen Flash sowohl `ConfigureClocks()` als auch `sdram.Init()`. Der Stempel auf `v6_1` vor `Init()` ist damit auf beiden Boards nötig, aus demselben Grund.

## File Structure

| Datei | Verantwortung | Status |
|---|---|---|
| `src/hw/board.h` | Board-Abstraktion: Typ-Alias, `board_init()`, `board_name()`. Neutraler Ort, den Bench **und** die spätere Shell einbinden dürfen | neu |
| `bench/main.cpp` | Board-Init über `src/hw/board.h` statt direkt `DaisySeed` | ändern |
| `bench/anchor.cpp` | Signatur `run_anchors` auf `bench::Board&` | ändern |
| `bench/Makefile` | `BENCH_BOARD ?= seed` als Auswahlschalter, `-I../src/` auf den Include-Pfad | ändern |
| `bench/run.py` | `--board` durchreichen, Board in Dateiname und CSV-Zeile | ändern |
| `bench/test_run_contract.py` | Test für den neuen Dateinamen und die neue Spalte | ändern |
| `docs/bench/2026-08-08-<hash>-regress-patch_sm-axi-o3.md` / `.csv` | Das erste Capture vom Zielboard | neu, vom Runner |
| `docs/bench/2026-08-08-seed-vs-patch-sm.md` | Der Vergleich, von Hand | neu |

**Warum `src/hw/` und nicht `bench/`:** Die Produktions-Firmware braucht dieselbe Board-Initialisierung. Läge sie unter `bench/`, würde ausgelieferte Firmware auf das Messwerkzeug zeigen — genau die Trennung, die `bench/README.md` zieht. Eine Kopie in beiden Bäumen wäre schlimmer: der `boot_info`-Sonderfall darunter ist eine Falle, die man kein zweites Mal pflegen will.

---

### Task 1: Die Board-Abstraktion

**Files:**
- Create: `src/hw/board.h`

**Interfaces:**
- Consumes: libDaisy — `daisy::DaisySeed`, `daisy::patch_sm::DaisyPatchSM`
- Produces: `bench::Board` (Typ-Alias), `bench::board_init(Board&)`, `bench::board_name() -> const char*` mit den Werten `"seed"` und `"patch_sm"`

- [ ] **Schritt 1: Die Datei anlegen**

```cpp
// src/hw/board.h
#pragma once

// Die Bench kennt zwei Boards. Der Seed traegt die gesamte Messhistorie in
// docs/bench/; das Patch Submodule ist das M6-Ziel. Gleicher STM32H750,
// gleiches SDRAM und QSPI -- aber bis der erste Lauf auf dem Submodule
// durch ist, ist das eine Erwartung und keine Messung. Deshalb steht das
// Board in jeder Zeile des Reports.

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
// boot_info-Marker, ein alter Bootloader (<v6.0) haette Takt und SDRAM
// schon hochgebracht, und ueberspringt daraufhin ConfigureClocks() UND
// sdram.Init(). Ein bekannter Bootloader-Stempel vor Init() setzt diese
// Annahme ausser Kraft. Ohne das: HardFault beim ersten SDRAM-Zugriff
// (bestaetigt auf Seed-Hardware 2026-07-18).
//
// Auf dem Submodule gilt dasselbe: daisy_patch_sm.cpp:233 prueft
// GetBootloaderVersion() == LT_v6_0 und setzt dann syscfg.skip_clocks,
// :245 haengt sdram.Init() an dieselbe Bedingung.
//
// Der Takt ist auf beiden Boards 480 MHz: DaisySeed::Init(true) boostet
// per Argument, DaisyPatchSM::Init() ruft syscfg.Boost() unbedingt auf
// (daisy_patch_sm.cpp:230). Ohne diese Gleichheit waere jeder Vergleich
// gegen die Seed-Historie ungueltig.
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

- [ ] **Schritt 2: Übersetzbarkeit beider Zweige prüfen, bevor irgendwas umgestellt wird**

Der `patch_sm`-Zweig war noch nie im Compiler. Ihn zuerst allein zu prüfen trennt Übersetzungsfehler von Umbaufehlern:

```bash
cd bench
make BENCH_BOARD=patch_sm build/bench.elf 2>&1 | head -40
```

Erwartet an dieser Stelle: ein Fehler, weil `BENCH_BOARD` im Makefile noch nicht existiert und `main.cpp` den Header noch nicht einbindet. Das ist in Ordnung — was hier zählt, ist, dass **kein** Fehler aus `board.h` selbst kommt. Falls doch, ist es ein Namens- oder Include-Problem in libDaisy und wird hier behoben, nicht später zwischen zwei anderen Baustellen.

- [ ] **Schritt 3: Commit**

```bash
git add src/hw/board.h
git commit -m "hw: a header that knows there are two boards

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: Die Bench auf die Abstraktion umstellen

Der Umbau selbst. Er darf an den Seed-Zahlen nichts ändern — das ist sein Abnahmekriterium, nicht die Übersetzbarkeit.

**Files:**
- Modify: `bench/main.cpp`
- Modify: `bench/anchor.cpp`
- Modify: `bench/Makefile`
- Modify: `bench/run.py`
- Modify: `bench/test_run_contract.py`

**Interfaces:**
- Consumes: `bench::Board`, `bench::board_init()`, `bench::board_name()` aus Task 1
- Produces: `make BENCH_BOARD=seed|patch_sm`, `run.py --board {seed,patch_sm}`, Board als Namensbestandteil und CSV-Spalte

- [ ] **Schritt 1: `main.cpp` umstellen**

Vier Ersetzungen:

| vorher | nachher |
|---|---|
| `#include <daisy_seed.h>` | `#include "hw/board.h"` |
| `static daisy::DaisySeed hw;` | `static bench::Board hw;` |
| die sechs Zeilen `InitBackupSram()` … `SetAudioSampleRate(...)` | `bench::board_init(hw);` |
| jede weitere `daisy::DaisySeed`-Erwähnung | `bench::Board` |

In `bench/anchor.cpp` wird `void run_anchors(daisy::DaisySeed& hw)` zu `void run_anchors(bench::Board& hw)`.

> Falls `main.cpp` oder ein Workload eine Seed-spezifische Methode aufruft, die das Submodule nicht hat — typisch `hw.SetLed(...)` oder ein direkter Pin-Zugriff — wird das **nicht** in `board.h` nachgebaut. Es kommt hinter `#if defined(BENCH_BOARD_PATCH_SM)` an der Aufrufstelle heraus, mit einer Zeile Kommentar, warum. `board.h` bleibt klein; eine Board-Abstraktion, die anfängt, Peripherie zu emulieren, ist der Anfang einer zweiten libDaisy.

- [ ] **Schritt 2: Den Board-Schalter ins Makefile**

`bench/Makefile` bekommt neben `BENCH_FAMILIES`, `BENCH_ITCM_HOT` und `BENCH_OPTIMIZATION` ein `BENCH_BOARD ?= seed`. Bei `patch_sm` kommt `-DBENCH_BOARD_PATCH_SM` in `C_DEFS`. Unabhängig vom Board kommt `-I../src/` in `C_INCLUDES`, damit `#include "hw/board.h"` auflöst.

**Keine Quellenliste anfassen.** `daisy_patch_sm` steckt bereits in `libdaisy.a`.

> Das Define muss über eine Make-Variable laufen, nicht als bares `-D` auf der Kommandozeile — aus demselben Grund, den der Kommentar bei `BENCH_DECK_BUS` im Makefile ausführt: ein bares `-D` ist für Makes Abhängigkeitsgraph unsichtbar, und ein Teil-Rebuild unter altem Wert linkt dann still ein Objekt vom falschen Board dazu.

- [ ] **Schritt 3: Den Board-Schalter in den Runner**

`bench/run.py`:
- `ap.add_argument("--board", default="seed", choices=["seed", "patch_sm"])`
- an `make` durchreichen, neben `BENCH_OPTIMIZATION` und den anderen
- in das Namensschema aufnehmen: `YYYY-MM-DD-<hash>-<profile>-<board>-<layout>-<optimization>.md`
- als Spalte in jede CSV-Zeile

- [ ] **Schritt 4: Den Kontrakt-Test nachziehen und RED beweisen**

`bench/test_run_contract.py` prüft heute Dateinamen und CSV-Kopf. Beide ändern sich, also ändert sich der Test mit — **zuerst**, und einmal rot gesehen:

```bash
cd bench && python -m pytest test_run_contract.py -v
```

Erwartet: FAIL, solange `run.py` das Board noch nicht in den Namen schreibt. Dann `run.py` anpassen, GRÜN. Ohne den roten Durchlauf ist nicht bewiesen, dass der Test die Spalte überhaupt ansieht.

- [ ] **Schritt 5: Die Seed-Regression — das eigentliche Abnahmekriterium**

Bevor das Submodule auch nur angeschlossen wird:

```bash
cd bench && python run.py --profile regress --board seed --optimization o3
```

Erwartet: `instrument_worst_bbd_dtcm` innerhalb des Wiederholbands von **96,43 % offline** aus `docs/bench/2026-08-04-bd01608-regress-axi-o3.md`.

**Weicht die Zahl ab, ist der Umbau schuld, nicht das Board.** Zurück zu Schritt 1. Nicht weitergehen, nicht „das schauen wir uns nach dem Submodule-Lauf an" — ab dort sind zwei Variablen gleichzeitig offen und der ganze Tag ist unbrauchbar.

- [ ] **Schritt 6: Frisches Objekt verifizieren**

Der Memory-Tabelle wird nicht geglaubt:

```bash
grep -n "board" bench/build/bench.map | head
```

Erwartet: die neue Übersetzungseinheit taucht auf.

- [ ] **Schritt 7: Commit**

```bash
git add bench/main.cpp bench/anchor.cpp bench/Makefile bench/run.py bench/test_run_contract.py
git commit -m "bench: the harness learns there is a second board

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: Der erste Lauf auf dem Submodule

Der Moment, in dem aus „sollte übertragen" eine Zahl wird. Das Board war nie geflasht.

**Files:**
- Create: `docs/bench/2026-08-08-<hash>-regress-patch_sm-axi-o3.md` und `.csv` (vom Runner)

**Interfaces:**
- Consumes: `--board patch_sm` aus Task 2
- Produces: das erste Capture mit `board=patch_sm`

- [ ] **Schritt 1: Übersetzen, bevor die Hardware im Spiel ist**

```bash
cd bench && python run.py --profile regress --board patch_sm --optimization o3 --build-only
```

Zwei Fehlerquellen auf einmal zu debuggen ist die teuerste Art, diesen Tag zu verbringen. `--build-only` trennt sie.

- [ ] **Schritt 2: Den QSPI-Wavetable-Bank programmieren**

Die Bench prüft beim Start den SHA-256 des QSPI-Payloads gegen das gelinkte Image. Ein frisches Board hat einen leeren QSPI, und der Lauf schlägt an dieser Prüfung fehl — **das ist gewollt und kein Fehler**, es ist der Wächter, der verhindert, dass eine Messung gegen falsche Wavetables läuft.

Vorgehen nach `bench/qspi_programmer/`, Konfiguration `bench/openocd/qspi-programmer.cfg`.

- [ ] **Schritt 3: Den Lauf ausführen**

```bash
cd bench && python run.py --profile regress --board patch_sm --optimization o3
```

Erwartet: zwei Wiederholungen, übereinstimmende Zeilenmengen und Checksummen, Capture in `../docs/bench/`.

- [ ] **Schritt 4: Bei Fehlschlag — die zwei bekannten Fallen zuerst**

Nicht ins Blaue debuggen. In dieser Reihenfolge:

1. **HardFault beim ersten SDRAM-Zugriff, `BENCH_BEGIN` erscheint nie** → der `boot_info`-Stempel greift nicht. `bench::board_init()` prüfen. Unterscheidungsmerkmal zur FPU-Falle: die ist in `spotykach-sram.cfg` durch das `mww 0xE000ED88 0x00F00000` bereits erschlagen und board-unabhängig — wenn die Zeile steht und es trotzdem faultet, ist es SDRAM.
2. **QSPI-Digest-Mismatch** → Schritt 2 nicht oder mit falschem Bank-Stand ausgeführt.

Die OpenOCD-Konfiguration ist als dritte Falle **ausgeschieden**: `target/stm32h7x.cfg` gilt für den H750, nicht für ein Board, und `0x24000000` ist bei beiden derselbe SRAM_EXEC-Sockel.

Jede Abweichung, die eine Änderung an `src/hw/board.h` erzwingt, wird dort kommentiert, und **Task 2 Schritt 5 wird danach erneut gefahren** — eine Änderung an `board.h` fasst beide Boards an.

- [ ] **Schritt 5: Commit**

```bash
git add docs/bench/
git commit -m "bench(patch-sm): the first cycles ever counted on the target board

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: Der Vergleich

Ein Capture ist kein Urteil. Das Urteil ist eine Seite, die genau eine Frage beantwortet.

**Files:**
- Create: `docs/bench/2026-08-08-seed-vs-patch-sm.md`
- Modify: `docs/roadmap.md` (nur bei negativem Ausgang)

**Interfaces:**
- Consumes: das Seed-Capture aus Task 2 Schritt 5 und das Submodule-Capture aus Task 3
- Produces: die Antwort auf „übertragen die Seed-Zahlen?" — Eingang für die Shell-Messung in Phase 0 Task 6

- [ ] **Schritt 1: Die Vergleichsseite schreiben**

Zeile für Zeile Seed gegen Patch SM, **dieselbe Profil-, Layout- und Optimierungsidentität**, Differenz in Prozentpunkten. Beide Git-Hashes im Kopf.

Die erste Zeile der Seite beantwortet die Frage, nicht die letzte:

```markdown
# Seed gegen Patch Submodule — regress / axi / -O3

**Verdikt: <übertragen | übertragen nicht>.**
<ein Satz, warum.>
```

- [ ] **Schritt 2: Das Verdikt fällen**

Bewegt sich `instrument_worst_bbd_dtcm` um **mehr als das Wiederholband** der zwei Läufe, gilt: die Zahlen übertragen nicht. Dann bekommt `docs/roadmap.md` eine Warnung, und ab dort darf **keine Seed-Zahl mehr für Submodule-Aussagen zitiert werden** — auch nicht mit Sternchen, auch nicht als Näherung.

Bewegt sie sich innerhalb des Bands, wird das ebenso ausdrücklich hingeschrieben. Ein stillschweigendes „passt schon" ist kein Ergebnis; die nächste Sitzung muss es nachlesen können.

> Der interessante Ausgang ist der unangenehme. Fällt die Reserve unter 3,57 Punkte, ist das der Grund, warum diese Messung im August steht und nicht im Januar — sie kostet jetzt einen Tag und nach dem PCB-Layout ein Layout.

- [ ] **Schritt 3: Commit**

```bash
git add docs/bench/2026-08-08-seed-vs-patch-sm.md
git commit -m "bench: the verdict on whether seed numbers travel

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Reihenfolge

Streng seriell. Task 2 Schritt 5 ist das Tor: davor wird das Submodule nicht angeschlossen, damit bei einer Abweichung immer nur eine Variable offen ist.

## Definition of Done

- [ ] `src/hw/board.h` existiert und übersetzt in beiden Zweigen
- [ ] Ein Seed-Lauf nach dem Umbau liegt innerhalb des Wiederholbands von 96,43 %
- [ ] `bench/test_run_contract.py` ist grün und hat den neuen Namen und die neue Spalte einmal rot gesehen
- [ ] Ein Capture mit `board=patch_sm` liegt in `docs/bench/`
- [ ] `docs/bench/2026-08-08-seed-vs-patch-sm.md` existiert und trägt das Verdikt in der ersten Zeile
- [ ] Bei negativem Verdikt: `docs/roadmap.md` warnt vor der Zitierung von Seed-Zahlen

## Was dieser Tag ausdrücklich nicht tut

Keine Shell, keine Engine-Firmware, kein Mux, kein Ton. Das Submodule zählt am Ende des Tages Zyklen und sonst nichts. Wer hier anfängt, nebenbei `shell/` aufzusetzen, hat am Abend zwei halbfertige Dinge und keine Zahl.
