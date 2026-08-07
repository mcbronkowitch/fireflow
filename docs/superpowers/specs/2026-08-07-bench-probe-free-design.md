# Die Bench ohne Probe — Design

**Datum:** 7. August 2026
**Status:** Entwurf zur Umsetzung
**Kontext:** Phase 0 der Hardware-Roadmap (`2026-08-07-fireflow-hardware-roadmap-design.md`)

## Das Problem

Das Daisy Patch Submodule liegt auf dem Tisch und hängt an USB. Ein Debug-Probe lässt sich nicht anschließen: die dafür nötigen Stiftleisten sind nicht bestückt und nicht vorhanden.

Die Bench setzt heute zwingend einen Probe voraus, an zwei Stellen gleichzeitig:

- **Laden.** `bench/run.py:113-121` startet OpenOCD, das über `spotykach-sram.cfg` das SRAM-Image direkt nach `0x24000000` schiebt, VTOR und MSP von Hand setzt und die FPU freischaltet — alles Dinge, die sonst der Bootloader täte.
- **Ausgeben.** `bench/report.cpp:33` ist ein `bkpt 0xAB`, ARM-Semihosting. Der Kommentar darüber sagt es selbst: *„This is the whole transport."* Ohne Debugger, der den Breakpoint bedient, bleibt die CPU darauf stehen. Keine Ausgabe, keine Fehlermeldung.

USB allein löst weder das eine noch das andere.

## Das Ziel

Die Bench misst auf einem Board, das nur an USB hängt: geladen über DFU, Ausgabe über USB-CDC. Der bestehende Semihosting-Weg bleibt vollständig erhalten und bleibt der Referenzweg für die Seed-Historie.

**Das ist keine Notlösung.** Ein Instrument, das sich nur mit angelötetem Probe vermessen lässt, ist nach dem PCB-Layout nicht mehr vermessbar — spätestens dann, wenn auf einer bestückten Platine die Debug-Pads unter dem Panel liegen. Diese Arbeit fällt ohnehin an; sie fällt jetzt an, wo sie nichts blockiert.

## Rahmenbedingungen

- **Keine Regression an der Messkette.** `instrument_worst_bbd_dtcm` liegt bei 96,43 % offline (`docs/bench/2026-08-04-bd01608-regress-axi-o3.md`). Diese Zahl ist die Abnahme, nicht ein Nebenprodukt.
- **Der Seed bleibt der Zeuge.** Der neue Transport wird auf dem Seed bewiesen, wo eine bekannte Zahl existiert — nicht auf dem Submodule, wo beide Variablen gleichzeitig neu wären.
- **Kein Umbau der Messlogik.** Workloads, Zykluszählung, Profile, Report-Format und CSV-Vertrag bleiben unverändert. Nur Laden und Ausgeben ändern sich.
- **Kein Handanlegen im Lauf.** Zwei Wiederholungen müssen ohne Knopfdruck durchlaufen, sonst ist der Runner kein Runner mehr.

## Entscheidung 1: Der Transport wird ein Schalter, kein Ersatz

`BENCH_TRANSPORT ?= semihost` im Makefile, Werte `semihost` und `usb`. Bei `usb` kommt `-DBENCH_TRANSPORT_USB` in `C_DEFS`; im Runner wird das vorhandene `--transport` erweitert, das heute schon existiert und mit `choices=["semihost"]` auf genau diese Erweiterung wartet (`bench/run.py:1048`).

**Warum kein glatter Ersatz.** Die gesamte Messhistorie in `docs/bench/` ist über Semihosting entstanden. Fällt der Weg weg, ist keine ältere Zahl je wieder reproduzierbar, und der erste Verdacht bei jeder Abweichung wäre auf immer „lag es am Transport?". Zwei Wege kosten eine `#if`-Verzweigung in einer Datei und einen Zweig im Runner. Das ist billig gegen den Verlust der Vergleichbarkeit.

**Der Seam ist schon da.** `report.cpp` hat genau zwei öffentliche Funktionen, `log_line(const char*)` und `logf(const char*, ...)`, und beide gehen durch ein einziges `sh_write0`. Der Rest der Bench kennt den Transport nicht. Es wird eine Funktion ausgetauscht, keine Schnittstelle.

Verworfen: **UART statt USB.** libDaisy bietet `LOGGER_UART_7` an. Das braucht wieder Pins und einen USB-Seriell-Wandler, also genau das Problem in klein. Fällt aus demselben Grund aus wie der Probe.

## Entscheidung 2: Die Wavetable-Bank zieht um, nicht die App

Das ist die unangenehme Stelle, und sie ist der eigentliche Grund, warum dies eine eigene Runde ist.

Heute liegt die Bank auf **`0x90040000`** — `alt_sram.lds:25` legt `QSPIFLASH` dorthin, `bench/qspi_tools.py:10` bestätigt es als `QSPI_ADDRESS`.

Der Daisy-Bootloader erwartet die **App** unter `APP_TYPE = BOOT_SRAM` an genau derselben Adresse: `lib/libDaisy/core/Makefile:238` setzt `FLASH_ADDRESS = QSPI_ADDRESS`. Mit Probe fiel das nie auf, weil OpenOCD das SRAM-Image direkt lädt und der QSPI ausschließlich der Bank gehört. Ohne Probe kollidieren beide.

**Die Bank zieht auf `0x90100000`.**

| Bereich | von | bis | Inhalt |
|---|---|---|---|
| Bootloader | `0x90000000` | `0x90040000` | reserviert, unverändert |
| App | `0x90040000` | `0x90100000` | 768 KB für das Bench-Image |
| Bank | `0x90100000` | … | Wavetables, heute 65 024 Bytes |
| Ende Chip | | `0x90800000` | 7 MB frei |

**Warum die Bank und nicht die App.** Die App-Adresse ist keine Wahl — sie steht im Bootloader. Man könnte einen eigenen Bootloader bauen; das wäre der Anfang eines zweiten Projekts. Die Bankadresse ist eine Zeile in einem Linker-Skript.

**Warum 768 KB für die App.** Weit über jeder plausiblen Bildgröße, und eine runde Zahl, die man beim Debuggen im Hex-Dump wiedererkennt. Der QSPI hat 8 MB; Geiz lohnt hier nichts.

**Nebenwirkung, die für den Umzug spricht:** `dfu-util` schreibt nur an die App-Adresse. Liegt die Bank oberhalb, überlebt sie jedes Neuflashen. Ohne den Umzug müsste sie nach jedem Lauf neu programmiert werden.

Betroffen sind `alt_sram.lds`, `bench/qspi_tools.py`, `bench/qspi_programmer/`, die Digest-Wächter und `bench/test_qspi_guard.py`.

## Entscheidung 3: Die Bank wird ebenfalls über DFU programmiert

`bench/qspi_programmer/` ist heute ein eigenes Firmware-Image, das über `openocd/qspi-programmer.cfg` geladen wird und den QSPI beschreibt — ein Umweg, den nur der Probe nötig machte.

Über DFU ist das eine Zeile: die Bank an ihre Adresse schreiben und mit `dfu-util -U` zurücklesen. Die Byte-Identität, die `RECEIPT_MODE = "swd-sram-target-byte-identity"` heute über den Probe beweist, wird damit über DFU bewiesen; der Modus bekommt einen eigenen Namen, `dfu-qspi-readback-identity`, damit ein Beleg im Nachhinein erkennen lässt, wie er zustande kam.

Der Programmer bleibt für den Probe-Weg liegen. Er wird nicht gelöscht und nicht gepflegt.

## Entscheidung 4: Die Bench setzt sich selbst in den Bootloader zurück

Ohne diesen Punkt bricht die Automatisierung: `--repeat 2` bedeutet zwei Ladevorgänge, und DFU verlangt ein Gerät im DFU-Modus. Von Hand wäre das ein Knopfdruck zwischen zwei Wiederholungen.

`lib/libDaisy/src/sys/system.h:173` bietet `System::ResetToBootloader(BootloaderMode)`. Nach `BENCH_END` — und nur dort, weit außerhalb jedes Messfensters — springt die Bench zurück in den Bootloader. Der Runner wartet auf das Wiederauftauchen des DFU-Geräts und flasht die nächste Wiederholung.

Verworfen: **beide Wiederholungen in einem Flash-Vorgang.** Das wäre einfacher, misst aber etwas anderes. Der zweite Durchlauf liefe mit warmen Caches und ohne frischen Reset, und die Wiederholung soll gerade beweisen, dass zwei *unabhängige* Läufe dasselbe sagen. Ein billigerer Test, der eine schwächere Aussage macht, ist kein billigerer Test.

Bleibt der Rücksprung in der Praxis unzuverlässig, ist die interne Wiederholung der dokumentierte Rückfallweg — dann aber ausdrücklich im Capture vermerkt.

## Entscheidung 5: Die Speicherkarte ist Teil der Abnahme, nicht nur der Transport

Das ist die Falle, die man sonst erst an einer unerklärlichen Zahl bemerkt.

`report.cpp:38-44` reserviert `g_axi_layout_guard[0xcc8]` mit dem Kommentar, dies halte *„the accepted Task 8 measurement-object addresses and cache alignment"*. Der Report liegt bewusst in DTCM, um knappes AXI-SRAM zu schonen, und die Wächter-Reservierung hält die Adressen der Messobjekte fest.

`Logger<LOGGER_INTERNAL>` bringt den USB-CDC-Stack mit. Das ist erheblich mehr Code und RAM als ein `bkpt`. Wenn dieser Code im AXI-SRAM landet, verschieben sich Messobjekte und Cache-Zeilen — und dann misst die Bench nach dem Umbau etwas anderes als davor, ohne dass irgendwo ein Fehler auftaucht.

Deshalb gilt: **der USB-Zweig muss die Adressen der Messobjekte unverändert lassen.** Report-Code und -Puffer bleiben in DTCM, wo es geht; die Wächter-Reservierung wird nachgezogen, wenn nicht. Nachgewiesen wird das nicht durch Betrachtung der Map-Datei allein, sondern durch die Zahl.

**Ein Vorbehalt, der auf Hardware zu klären ist:** die USB-Peripherie greift per DMA auf ihre Puffer zu, und DTCM ist für DMA nicht erreichbar. Die Puffer des CDC-Stacks müssen daher in einem Bereich liegen, den die Peripherie sieht. Wo libDaisy sie hinlegt, wird beim Bring-up geprüft, bevor Zeit in die Ursachensuche eines stillen Fehlschlags geht.

## Die Abnahme

In dieser Reihenfolge, und keine Stufe wird übersprungen:

1. **Seed, Semihosting, nach dem Umbau.** `instrument_worst_bbd_dtcm` im Wiederholband um 96,43 %. Beweist, dass der Umbau den alten Weg nicht angefasst hat.
2. **Seed, USB-CDC.** Dieselbe Zahl im selben Band. Beweist, dass der Transport die Messung nicht verändert. **Weicht sie ab, ist der Transport schuld** — nicht das Board, denn das Board ist dasselbe.
3. **Submodule, USB-CDC.** Erst jetzt. Jede Abweichung ist ab hier eine Board-Aussage, und genau die soll sie sein.

Stufe 2 ist der Kern dieser ganzen Spec. Ohne sie hat man später eine Zahl vom Submodule und zwei mögliche Erklärungen.

## Risiken

**Der USB-Stack verändert die Messung.** Das wahrscheinlichste Problem und der Grund für Stufe 2 der Abnahme. Gegenmittel: Report in DTCM halten, Wächter nachziehen, an der Zahl prüfen.

**Ausgabe innerhalb eines Messfensters.** Semihosting hält den Kern an und war dadurch grob, aber ehrlich. USB-CDC ist interruptgetrieben und kann *während* einer Messung Zyklen verbrauchen, ohne dass es auffällt. Es wird ausschließlich zwischen Workloads berichtet — so wie heute, aber jetzt ist es eine Bedingung und keine Bequemlichkeit.

**Verlorene erste Zeilen.** Die Firmware läuft los, bevor der Host den Port offen hat. `StartLog(wait_for_pc = true)` wartet darauf. Ohne das fehlt der `BENCH_BEGIN`-Kopf und der Lauf ist wertlos, obwohl er lief.

**Der Bootloader ändert die Startbedingungen.** Bisher sprang OpenOCD am Bootloader vorbei direkt in den Reset-Vektor — daher der `boot_info`-Stempel und das Freischalten der FPU im OpenOCD-Skript. Mit echtem Bootloader macht das alles der Bootloader. `src/hw/board.h` muss beide Fälle vertragen und darf sich auf keinen von beiden verlassen.

**Der QSPI-Umzug trifft die Wächter.** Die Digest-Prüfung ist streng und wird zuschlagen, wenn Bank und Linker-Skript sich uneinig sind. Das ist gewollt — sie ist die Versicherung dagegen, gegen falsche Wavetables zu messen. Sie muss vollständig mit umziehen, inklusive ihrer Tests.

**Zeit.** Ein bis zwei Arbeitstage. Phase 0 hat bis zum 21. August Luft, und der Papier-Strang läuft unabhängig weiter.

## Was ausdrücklich nicht dazugehört

Keine Änderung an Workloads, Profilen, Zykluszählung oder Report-Format. Kein neues Board-Handling — das ist der separate Board-Schalter. Keine Shell, kein Mux, kein Ton. Wenn diese Runde vorbei ist, hat sich an dem, *was* gemessen wird, nichts geändert; nur daran, *wie* die Zahl das Board verlässt.
