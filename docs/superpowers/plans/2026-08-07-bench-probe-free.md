# Die Bench ohne Probe — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Die Bench lädt über DFU und berichtet über USB-CDC, und der Seed liefert auf dem neuen Weg dieselbe Zahl wie auf dem alten.

**Architecture:** Der Transport wird ein Compile-Schalter mit zwei Zweigen in `bench/report.cpp`; die einzigen zwei Funktionen, die der Rest der Bench kennt, bleiben unverändert. Die Wavetable-Bank räumt die Adresse, die der Daisy-Bootloader für die App beansprucht. `bench/run.py` bekommt neben dem OpenOCD-Weg einen zweiten aus `dfu-util` und einer seriellen Leitung.

**Tech Stack:** ARM GCC über `make`, libDaisy `Logger<LOGGER_INTERNAL>`, `dfu-util`, pyserial, unittest.

**Spec:** `docs/superpowers/specs/2026-08-07-bench-probe-free-design.md`

## Global Constraints

- **Toolchain: ARM GCC über `make`.** Niemals `source env.sh` in derselben Shell.
- **Referenzzahl:** `instrument_worst_bbd_dtcm` bei **96,43 % offline**, Profil `regress`, Layout `axi`, `-O3` — aus `docs/bench/2026-08-04-bd01608-regress-axi-o3.md`. Das ist die Abnahme jeder Stufe.
- **Der Semihosting-Weg bleibt vollständig erhalten** und bleibt der Default. `BENCH_TRANSPORT ?= semihost`.
- **Ausgabe niemals innerhalb eines Messfensters.** Berichtet wird zwischen Workloads, nie darin.
- **`alt_sram.lds` gehört nicht der Bench.** Das Root-`Makefile:21` linkt die ausgelieferte Firmware damit, `bench/audition/Makefile` ebenso. Jede Änderung daran trifft drei Bäume und wird in allen dreien geprüft.
- **Die Bench kann still ein veraltetes Objekt relinken.** Neue Übersetzungseinheiten über `build/bench.map` verifizieren, nie über die Memory-Tabelle.
- **Ein Test, der nicht rot werden kann, wird umgeschrieben.** RED einmal beweisen, immer.
- **Commit-Trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`

## Der Befund, der den Umfang bestimmt

`alt_sram.lds:25` legt `QSPIFLASH` auf `0x90040000`. `lib/libDaisy/core/Makefile:238` setzt bei `APP_TYPE = BOOT_SRAM` die DFU-Zieladresse `FLASH_ADDRESS = QSPI_ADDRESS` — dieselbe Adresse.

Das Root-`Makefile:20` benutzt **ebenfalls** `APP_TYPE = BOOT_SRAM` und **ebenfalls** `alt_sram.lds`. Die Kollision betrifft also nicht nur die Bench: die ausgelieferte Firmware ist heute aus demselben Grund nicht über DFU flashbar. Der Umzug der Bank repariert beides in einer Zeile — und muss deshalb in beiden Bäumen geprüft werden, nicht nur im Messwerkzeug.

## File Structure

| Datei | Verantwortung | Status |
|---|---|---|
| `alt_sram.lds` | `QSPIFLASH`-Origin auf `0x90100000` | ändern |
| `bench/qspi_tools.py` | `QSPI_ADDRESS`, neuer Beleg-Modus, Programmierung über DFU | ändern |
| `bench/test_qspi_guard.py` | Adressen und Beleg-Modus nachziehen | ändern |
| `bench/report.cpp` | Zweiter Transportzweig hinter `BENCH_TRANSPORT_USB` | ändern |
| `bench/Makefile` | `BENCH_TRANSPORT ?= semihost`, Define, USB-Quellen | ändern |
| `bench/runner.cpp` | Rücksprung in den Bootloader nach `BENCH_END` | ändern |
| `bench/run.py` | `--transport usb`: DFU laden, seriell lesen | ändern |
| `bench/test_run_contract.py` | Vertrag für den USB-Weg | ändern |
| `docs/bench/2026-08-XX-transport-semihost-vs-usb.md` | Der Beleg, dass der Transport nichts verändert | neu |

---

### Task 1: Die Bank räumt die App-Adresse

Reine Host-Arbeit, keine Hardware. Zuerst, weil alles andere darauf steht.

**Files:**
- Modify: `alt_sram.lds:25`
- Modify: `bench/qspi_tools.py:10`
- Modify: `bench/test_qspi_guard.py:41,49`

**Interfaces:**
- Produces: `QSPI_ADDRESS = 0x90100000` als einzige Wahrheit über die Bankadresse

- [ ] **Schritt 1: Den Test zuerst auf die neue Adresse stellen**

`bench/test_qspi_guard.py:41`, `test_linked_section_parser_requires_exact_reserved_app_address`, prüft heute gegen `(0x90040000, 0xFE00)`. Der Name des Tests sagt bereits, worum es geht: die App-Adresse ist reserviert. Genau das war bisher falsch belegt.

Die Erwartung im Test wird auf `0x90100000` gezogen, und ein zweiter Fall kommt dazu, der die eigentliche Regel festnagelt:

```python
def test_bank_must_not_sit_in_the_bootloader_app_window(self) -> None:
    # 0x90040000 ist die DFU-Zieladresse des Daisy-Bootloaders fuer
    # APP_TYPE=BOOT_SRAM (libDaisy core/Makefile: FLASH_ADDRESS =
    # QSPI_ADDRESS). Eine Bank dort kollidiert mit dem App-Image und
    # macht das Board ohne Probe unbespielbar.
    self.assertGreaterEqual(qspi_tools.QSPI_ADDRESS, 0x90100000)
```

- [ ] **Schritt 2: Rot bestätigen**

```bash
cd bench && python -m pytest test_qspi_guard.py -v
```

Erwartet: FAIL in beiden Fällen, solange `QSPI_ADDRESS` noch `0x90040000` ist.

- [ ] **Schritt 3: Die Adresse umziehen**

`bench/qspi_tools.py:10` auf `QSPI_ADDRESS = 0x90100000`.

`alt_sram.lds:25` entsprechend:

```
	QSPIFLASH	(RX)  : ORIGIN = 0x90100000, LENGTH = 7168K
```

Mit einem Kommentar darüber, warum die Adresse nicht frei wählbar ist:

```
	/* Nicht auf 0x90040000 zurueckziehen: das ist die DFU-Zieladresse,
	   die der Daisy-Bootloader fuer ein BOOT_SRAM-Image erwartet
	   (libDaisy core/Makefile, FLASH_ADDRESS = QSPI_ADDRESS). Eine Bank
	   dort wird vom naechsten dfu-util-Aufruf ueberschrieben. Die
	   768 KB dazwischen gehoeren dem App-Image. */
```

- [ ] **Schritt 4: Grün bestätigen**

```bash
cd bench && python -m pytest test_qspi_guard.py test_task8_contract.py -v
```

Erwartet: alle grün. `test_task8_contract.py` liest `alt_sram.lds` mit und ist der Grund, warum es hier mitläuft.

- [ ] **Schritt 5: Beide anderen Bäume übersetzen**

Die geteilte Datei verlangt drei Beweise, nicht einen:

```bash
make -C bench -j8 BENCH_FAMILIES="system bbd" BENCH_OPTIMIZATION=o3 build/bench.elf
make -j8                      # die ausgelieferte Firmware, Root-Makefile
make -C bench/audition -j8
```

Erwartet: alle drei übersetzen und linken. Bricht die ausgelieferte Firmware, ist der Umzug zu weit gegangen — dann ist die Länge zu prüfen, nicht der Origin.

> **Nicht `make -C bench build/bench.elf` ohne `BENCH_FAMILIES`.** Das baut das volle Profil, und das linkt seit Langem nicht — `bench/profiles.py` sagt es ausdrücklich: *„Expected to FAIL TO LINK until the engine shrinks or the region grows."* Der Überlauf sieht aus wie ein Schaden dieser Aufgabe und ist keiner. Wer ihn trotzdem sieht, vergleicht gegen den Stand vor der Änderung, bevor er etwas repariert.

- [ ] **Schritt 6: Commit**

```bash
git add alt_sram.lds bench/qspi_tools.py bench/test_qspi_guard.py
git commit -F - <<'EOF'
bench(qspi): the bank stops squatting on the bootloader's app address

0x90040000 is where dfu-util puts a BOOT_SRAM image. The wave bank has
been sitting there since the beginning and nobody noticed, because with
a probe attached openocd loads SRAM directly and QSPI belongs to the
bank alone. Without a probe the two collide.

The bank moves to 0x90100000. The shipping firmware links the same
script and had the same defect.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 2: Der zweite Transportzweig

**Files:**
- Modify: `bench/report.cpp:20-33,47-62`
- Modify: `bench/Makefile`

**Interfaces:**
- Consumes: `daisy::Logger<daisy::LOGGER_INTERNAL>` aus `lib/libDaisy/src/hid/logger.h`
- Produces: unveränderte `bench::log_line(const char*)` und `bench::logf(const char*, ...)`; neuer Schalter `BENCH_TRANSPORT=semihost|usb`

- [ ] **Schritt 1: Den Zweig einziehen**

`report.cpp` behält seine beiden öffentlichen Funktionen. Nur `sh_write0` bekommt ein Gegenstück:

```cpp
#if defined(BENCH_TRANSPORT_USB)
#include "hid/logger.h"

using BenchLogger = daisy::Logger<daisy::LOGGER_INTERNAL>;

// USB-CDC statt Semihosting. Semihosting hielt den Kern an; das war grob,
// aber ehrlich. Diese Leitung ist interruptgetrieben und darf deshalb
// ausschliesslich zwischen Workloads laufen, nie in einem Messfenster --
// dieselbe Regel wie vorher, aber jetzt eine Bedingung und keine
// Bequemlichkeit mehr.
inline void transport_write0(const char* s)
{
    BenchLogger::PrintLine("%s", s);
}

inline void transport_open()
{
    // true: warten, bis der Host den Port geoeffnet hat. Ohne das laeuft
    // die Firmware los, bevor jemand zuhoert, und der BENCH_BEGIN-Kopf
    // fehlt -- ein Lauf, der stattfand und trotzdem wertlos ist.
    BenchLogger::StartLog(true);
}
#else
constexpr int kSysWrite0 = 0x04;

inline void transport_write0(const char* s)
{
    register int         r0 asm("r0") = kSysWrite0;
    register const char* r1 asm("r1") = s;
    asm volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
}

inline void transport_open() {}
#endif
```

`log_line` und `logf` rufen `transport_write0`. In `report.h` kommt `void transport_open();` als öffentliche Funktion dazu, die `bench/main.cpp` **vor** der ersten Ausgabe aufruft.

> **Nicht anfassen:** `g_axi_layout_guard` und die DTCM-Attribute. Sie halten die Adressen der Messobjekte fest. Ob sie das nach dem Umbau noch tun, entscheidet Task 3 — an der Zahl, nicht an der Betrachtung.

- [ ] **Schritt 2: Den Schalter ins Makefile**

`bench/Makefile` bekommt `BENCH_TRANSPORT ?= semihost`. Bei `usb` kommt `-DBENCH_TRANSPORT_USB` in `C_DEFS`.

Über eine Make-Variable, nicht als bares `-D` auf der Kommandozeile — aus dem Grund, den der `BENCH_DECK_BUS`-Kommentar im selben Makefile ausführt: ein bares `-D` ist für Makes Abhängigkeitsgraph unsichtbar, und ein Teil-Rebuild linkt dann ein Objekt mit dem falschen Transport dazu.

- [ ] **Schritt 3: Beide Zweige übersetzen**

```bash
cd bench
make BENCH_TRANSPORT=semihost build/bench.elf
make BENCH_TRANSPORT=usb build/bench.elf
```

Erwartet: beide bauen. Fehlt beim USB-Zweig die USB-Klasse beim Linken, kommen die entsprechenden libDaisy-Quellen dazu — hier und nicht später.

- [ ] **Schritt 4: Die Speicherkarte vergleichen, bevor irgendetwas gemessen wird**

```bash
cd bench
make BENCH_TRANSPORT=semihost build/bench.elf && arm-none-eabi-size build/bench.elf
make BENCH_TRANSPORT=usb build/bench.elf && arm-none-eabi-size build/bench.elf
grep -n "sram_exec_layout_guard\|dtcmram" build/bench.map | head
```

Notieren, wie viel AXI-SRAM der USB-Zweig zusätzlich belegt. Das ist noch kein Urteil — es ist die Zahl, mit der Task 3 erklärt wird, falls die Messung wandert.

- [ ] **Schritt 5: Commit**

```bash
git add bench/report.cpp bench/report.h bench/main.cpp bench/Makefile
git commit -F - <<'EOF'
bench: a second way for a number to leave the board

report.cpp had exactly one line that knew the transport. It now has two,
behind a switch, and the rest of the bench still knows neither.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 3: Abnahmestufe 1 — der alte Weg ist unverändert

Bevor der neue Weg irgendetwas beweisen darf, muss der alte beweisen, dass er nicht beschädigt wurde.

**Files:**
- Create: ein Capture in `docs/bench/`

- [ ] **Schritt 1: Den Seed über Semihosting messen**

```bash
cd bench && python run.py --profile regress --optimization o3
```

- [ ] **Schritt 2: Gegen die Referenz halten**

Erwartet: `instrument_worst_bbd_dtcm` im Wiederholband um **96,43 %** aus `docs/bench/2026-08-04-bd01608-regress-axi-o3.md`.

**Weicht die Zahl ab, ist Task 1 oder 2 schuld.** Der wahrscheinlichste Grund ist die Speicherkarte aus Task 2 Schritt 4 — der USB-Zweig ist zwar nicht übersetzt, aber `report.h` und `main.cpp` haben sich geändert. Zurück, nicht weiter. Ab hier zwei offene Variablen zu haben ist der teuerste Fehler dieses Plans.

- [ ] **Schritt 3: Frisches Objekt verifizieren**

```bash
grep -n "report" bench/build/bench.map | head
```

- [ ] **Schritt 4: Commit**

```bash
git add docs/bench/
git commit -m "bench: the old road still measures what it measured

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: Der Runner lernt DFU und die serielle Leitung

**Files:**
- Modify: `bench/run.py:106-160,1046-1052`
- Modify: `bench/test_run_contract.py`
- Modify: `bench/runner.cpp`

**Interfaces:**
- Consumes: `run_once(interface, timeout)` als Vorbild — gleiche Rückgabe, gleiche Abbruchbedingung
- Produces: `run_once_usb(port, timeout)`, `--transport {semihost,usb}`, `--port`

- [ ] **Schritt 1: Den Vertrag zuerst schreiben**

`bench/test_run_contract.py` hat mit `FakeOpenOcd` bereits das Muster: ein vorgetäuschter Prozess, dessen Zeilen der Runner liest. Dasselbe für eine vorgetäuschte serielle Leitung.

```python
class FakeSerial:
    """Eine serielle Leitung, die eine feste Zeilenfolge ausgibt."""

    def __init__(self, lines):
        self._lines = list(lines)

    def readline(self):
        if self._lines:
            return (self._lines.pop(0) + "\r\n").encode()
        return b""

    def close(self):
        pass


class UsbRunContract(unittest.TestCase):
    def test_run_once_usb_stops_at_bench_end(self):
        port = FakeSerial(["BENCH_BEGIN,families=...", "row,1", "BENCH_END"])
        lines = runner.run_once_usb(port, timeout=5.0)
        self.assertEqual(lines[-1], "BENCH_END")

    def test_run_once_usb_returns_none_on_timeout(self):
        # Eine Leitung, die nie BENCH_END sagt, muss None liefern -- ein
        # Haenger darf kein halbes Capture erzeugen, das wie ein Ergebnis
        # aussieht.
        port = FakeSerial(["BENCH_BEGIN,families=..."])
        self.assertIsNone(runner.run_once_usb(port, timeout=0.2))
```

- [ ] **Schritt 2: Rot bestätigen**

```bash
cd bench && python -m pytest test_run_contract.py -v -k Usb
```

Erwartet: FAIL — `run_once_usb` existiert nicht.

- [ ] **Schritt 3: `run_once_usb` schreiben**

Dieselbe Form wie `run_once`: bis `BENCH_END` lesen, bei Zeitüberschreitung `None`. Kein Prozess-Abbau nötig, dafür der Port geschlossen. Das Laden ist getrennt und passiert davor:

```python
def load_dfu(binary, address):
    """Das Image an seine Adresse schreiben und starten lassen.

    :leave verlaesst DFU und startet die App -- das ersetzt den
    reset-halt-resume-Dreisatz des OpenOCD-Skripts.
    """
    subprocess.run(
        ["dfu-util", "-a", "0", "-s", "%s:leave" % hex(address),
         "-D", binary, "-d", ",0483:df11"],
        check=True,
    )
```

- [ ] **Schritt 4: Grün bestätigen**

```bash
cd bench && python -m pytest test_run_contract.py -v
```

Erwartet: alle grün, auch die bestehenden OpenOCD-Fälle. Danach RED einmal beweisen: die Abbruchbedingung von `BENCH_END` auf `BENCH_ENDE` ändern, Test läuft rot, zurückändern.

- [ ] **Schritt 5: Den Rücksprung in den Bootloader einbauen**

In `bench/runner.cpp`, nach der letzten Ausgabe und nur im USB-Zweig:

```cpp
#if defined(BENCH_TRANSPORT_USB)
    // Ohne diesen Ruecksprung verlangt jede zweite Wiederholung einen
    // Knopfdruck am Board: dfu-util braucht ein Geraet im DFU-Modus.
    // Weit ausserhalb jedes Messfensters -- BENCH_END ist raus.
    daisy::System::Delay(100);          // der Leitung Zeit zum Leeren
    daisy::System::ResetToBootloader(
        daisy::System::BootloaderMode::DAISY);
#endif
```

> Der Modus ist zu prüfen: `sys/system.h:163` führt das `enum BootloaderMode`. Welcher Wert den Daisy-Bootloader in den DFU-Wartezustand bringt statt den STM-ROM-Loader, steht dort — vor dem Übersetzen nachlesen, nicht raten.

- [ ] **Schritt 6: Den Schalter im Runner verdrahten**

`bench/run.py:1048` hat `--transport` bereits mit `choices=["semihost"]`. Erweitern auf `["semihost", "usb"]`, dazu `--port` für die serielle Leitung. Bei `usb`: `BENCH_TRANSPORT=usb` an `make`, dann `load_dfu`, dann `run_once_usb`. Der Transport kommt in Dateinamen und CSV-Zeile, wie das Board.

- [ ] **Schritt 7: Commit**

```bash
git add bench/run.py bench/runner.cpp bench/test_run_contract.py
git commit -m "bench(run): dfu-util loads it, a serial line reads it back

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Abnahmestufe 2 — derselbe Seed, andere Leitung

Der Kern des ganzen Plans. Ein Board, das seine Zahl kennt, auf einem Weg, der sie noch nie transportiert hat.

**Files:**
- Create: `docs/bench/2026-08-XX-<hash>-regress-seed-usb-axi-o3.md` und `.csv`
- Create: `docs/bench/2026-08-XX-transport-semihost-vs-usb.md`

- [ ] **Schritt 1: Den Bootloader auf den Seed bringen**

```bash
make -C bench program-boot
```

Setzt das Board im STM-DFU-Modus voraus (BOOT-Taster halten, RESET tippen). Einmalig.

- [ ] **Schritt 2: Die Bank an ihre neue Adresse programmieren**

Über DFU statt über den Programmer:

```bash
dfu-util -a 0 -s 0x90100000 -D <bank>.bin -d ,0483:df11
dfu-util -a 0 -s 0x90100000:<size> -U readback.bin -d ,0483:df11
```

Zurücklesen und byteweise vergleichen. Das ist der Beleg, der bisher `RECEIPT_MODE = "swd-sram-target-byte-identity"` hieß; er heißt jetzt `dfu-qspi-readback-identity`, damit später erkennbar bleibt, wie er zustande kam.

- [ ] **Schritt 3: Ein Hallo, bevor eine Messung**

Vor dem ersten echten Lauf: ein Image, das nur `transport_open()` und drei Zeilen ausgibt. Der Port wird geöffnet, die Zeilen erscheinen — oder eben nicht.

**Der Grund ist konkret:** die USB-Peripherie erreicht ihre Puffer per DMA, und DTCM ist für DMA nicht erreichbar. Liegt ein CDC-Puffer dort, schweigt die Leitung, ohne zu faulten. Diesen Fall in fünf Minuten mit drei Zeilen zu finden ist erheblich billiger, als ihn in einem 20-Minuten-Messlauf zu suchen.

- [ ] **Schritt 4: Der Lauf**

```bash
cd bench && python run.py --profile regress --transport usb --port <COMx> --optimization o3
```

Erwartet: zwei Wiederholungen ohne Handanlegen, übereinstimmende Zeilenmengen und Checksummen.

- [ ] **Schritt 5: Das Urteil schreiben**

`docs/bench/2026-08-XX-transport-semihost-vs-usb.md`: dasselbe Board, dasselbe Profil, dasselbe Layout, dieselbe Optimierung — nur die Leitung ist anders. Zeile für Zeile, Differenz in Prozentpunkten, erste Zeile das Verdikt.

Liegt `instrument_worst_bbd_dtcm` außerhalb des Wiederholbands, **hat der Transport die Messung verändert**. Dann ist die Ursache in der Speicherkarte zu suchen — die Zahlen aus Task 2 Schritt 4 sind der Einstieg — und `g_axi_layout_guard` wird nachgezogen, bis die Adressen der Messobjekte wieder stimmen. Nicht das Ergebnis akzeptieren und eine Fußnote schreiben.

- [ ] **Schritt 6: Commit**

```bash
git add docs/bench/
git commit -F - <<'EOF'
bench: the same seed, down a wire it has never used

Stage two of three. The board is the constant here, so any drift is the
transport's fault and nothing else's -- which is the whole reason this
runs on the seed before it runs on the submodule.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 6: Den Weg dokumentieren, solange er frisch ist

**Files:**
- Modify: `bench/README.md`
- Modify: `docs/roadmap.md`

- [ ] **Schritt 1: `bench/README.md` ergänzen**

Ein Abschnitt „Zwei Transporte": wann welcher, welche Voraussetzungen (Probe gegen Bootloader plus USB), und die Regel, dass Zahlen aus verschiedenen Transporten nur verglichen werden dürfen, wenn der Beleg aus Task 5 dazu existiert.

Dazu die QSPI-Karte als Tabelle — Bootloader, App, Bank — mit der Warnung, warum `0x90040000` nicht wieder belegt werden darf.

- [ ] **Schritt 2: `docs/roadmap.md`**

Zwei Sätze in den Phase-0-Abschnitt: der Transport ist gebaut, die Zahl ist bestätigt, das Submodule ist ab jetzt ohne Probe messbar.

- [ ] **Schritt 3: Commit**

```bash
git add bench/README.md docs/roadmap.md
git commit -m "docs(bench): two transports, and a memory map that explains itself

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Wie es ausging (2026-08-07)

**Tasks 1 bis 6 sind erledigt, Stufe 3 steht aus.** Sie wartet nicht auf diesen Plan, sondern auf Task 1 des anderen: die Bench ist ein Seed-Programm, und auf dem Submodule würde `DaisySeed::Init()` den falschen Codec hochfahren.

Sechs Fehlschläge auf dem Weg, jeder ein echter Defekt:

| Symptom | Ursache | Commit |
|---|---|---|
| Beleg passt nicht | Beleg an den ELF gebunden, USB-Zweig ist ein anderes Binary | — |
| Rücklese-Digest falsch | **Bootloader kann keinen QSPI-Upload** — Entscheidung 3 der Spec ist halb tot | `981c7f3`, `addebf7` |
| `last page not writeable` | **67-MB-Image**: `.dtcmram_code` ohne `AT`-Klausel | `4aed1bf` |
| Kein `BENCH_BEGIN` | `StartLog(true)` kehrt zurück, bevor der Host liest | `f0ed3c0` |
| Kopfzeile abgeschnitten | **128-Byte-Loggerpuffer** gegen eine 135-Byte-Zeile | `9cc0c2d` |
| Wiederholung 2 stirbt sofort | Board enumeriert nach dem Rücksprung neu | `a74c876` |

**Zwei davon betrafen die ausgelieferte Firmware**, nicht nur das Messwerkzeug — die Bankadresse und der DTCM-Code. Beide waren unsichtbar, solange immer ein Probe dranhing.

**Das Ergebnis von Stufe 2 ist nicht „kein Unterschied", sondern eine Zahl:** USB-CDC kostet 6 370 Zyklen pro Block, 0,66 % des Budgets, verursacht vom Start-of-Frame-Interrupt und nicht von der Speicherkarte. `docs/bench/2026-08-07-transport-semihost-vs-usb.md`.

## Reihenfolge

Streng seriell. Task 3 ist ein Tor: der alte Weg muss vor dem neuen bewiesen sein. Task 5 Schritt 3 ist ein zweites: das Hallo vor der Messung.

## Definition of Done

- [ ] Die Bank liegt auf `0x90100000`, und alle drei Bäume linken
- [ ] `bench/test_qspi_guard.py` verbietet eine Bank im App-Fenster und hat das einmal rot gesehen
- [ ] Beide Transportzweige übersetzen
- [ ] Der Seed liefert über Semihosting nach dem Umbau die alte Zahl
- [ ] Der Seed liefert über USB-CDC dieselbe Zahl, und der Vergleich steht in `docs/bench/`
- [ ] Zwei Wiederholungen laufen ohne Handanlegen am Board
- [ ] `bench/README.md` erklärt beide Wege und die QSPI-Karte

## Was danach möglich ist

Task 3 und 4 aus `2026-08-08-fireflow-bench-second-board.md` — der erste Lauf auf dem Patch Submodule. Der war auf einen Probe angewiesen und ist es ab hier nicht mehr.

## Was dieser Plan nicht tut

Keine Änderung an Workloads, Profilen, Zykluszählung oder Report-Format. Kein Board-Handling — das ist der Board-Schalter im anderen Plan. Keine Shell, kein Mux, kein Ton. Am Ende hat sich an dem, *was* gemessen wird, nichts geändert.
