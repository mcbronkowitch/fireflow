# `shell/` — die erste Firmware, die die Engine enthält

Der Shell ist die kleinste Firmware, die `engine/` auf einem Daisy Patch
Submodule laufen lässt: Board hoch, den Speicher injizieren, den `FxMem`
verdrahten, `spky::Instrument::process()` im Audio-Callback. Er ist der
Beweis, dass der portable Engine-Kern die Werkbank verlässt — bis hierhin
lief `engine/**` ausschließlich unter den Desktop-Hosts, den CMake-Tests und
der Bench. Seit dem 8. August 2026 läuft er auf dem Zielboard und liefert
dort messbar Signal (siehe „Der Selbsttest" unten).

Was er ausdrücklich **nicht** ist: kein UI, keine Panel-Logik, kein
Preset-System, keine Bedienelemente. Er startet an einem festen
Betriebspunkt (`set_rate 0.4`, `set_density 0.6` auf Part A, 96 BPM) und
bleibt dort. Dass dieser Punkt tatsächlich klingt, ist nicht geraten,
sondern vorher auf dem Desktop gemessen: derselbe Betriebspunkt als
Szenario durch `build/render.exe` liefert über 5 s Peak −7,5 dBFS und RMS
−26,9 dBFS. Die Boot-Default-Engine ist `ENGINE_SYNTH`, es muss also keine
Klangquelle erst gewählt werden. Der erste Poti kommt in Task 6 des
Phase-0-Plans dazu, nicht hier.

**Die Abgrenzung gegen die anderen zwei Firmware-Bäume**, weil sich das sonst
garantiert jemand verwechselt: Das **Root-`Makefile`** baut die alte
Upstream-Spotykach-Firmware auf `src/core` und kompiliert `engine/**`
überhaupt nicht — sie ist ein anderes Instrument. **`bench/`** ist das
Messwerkzeug: dieselbe Linkage, aber vollgepackt mit Workload-Familien,
einem Report-Transport und Mess-Arenen, und mit vollem DaisySP inklusive der
LGPL-Module, weil sie nie ausgeliefert wird. **`shell/`** ist der Anfang der
Firmware, die einmal ausgeliefert wird. Geteilt wird zwischen `bench/` und
`shell/` genau eine Datei, `src/hw/board.h` — und das ist Absicht: hätten
beide eine eigene Board-Init, wäre jeder Vergleich zwischen Bench-Zahlen und
Shell-Verhalten wertlos.

## Bauen

Eigene Toolchain, ARM GCC über `make`. **Niemals `source env.sh`** — das ist
die Clang-Umgebung für Engine, Tests und Render-Host, und die beiden dürfen
sich nicht mischen.

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"
cd shell && make -j8 images
```

`images` (nicht `all`) ist der richtige Zielname. libDaisys Standardziel
baut ein flaches `shell.bin` über SRAM (`0x24000000`) **und** QSPI
(`0x90100000`) hinweg — rund 17 MB, fast alles Füllbytes, an die falsche
Adresse gezielt. `images` zerlegt stattdessen dieselbe gelinkte ELF in die
zwei physischen Artefakte, genau wie `bench/qspi_tools.py` es für die Bench
tut:

| Artefakt | Inhalt | Ziel |
|---|---|---|
| `build/shell-sram.bin` | alles außer der Wavetable-Bank | DFU nach `0x90040000` |
| `build/shell-qspi.bin` | nur die Bank | `0x90100000`, liegt schon dort |

Belegung beim ersten grünen Build (2026-08-08, `-O2`):

| Region | belegt | verfügbar | |
|---|---|---|---|
| SRAM_EXEC (Code) | 183 416 B | 262 880 B | 69,8 % |
| SRAM (`.bss`, davon ~131 KB Reverb) | 199 940 B | 261 408 B | 76,5 % |
| SDRAM (Echo, BBD, Sampler) | 36 581 376 B | 64 MiB | 54,5 % |

Läuft eine Region über, **nicht** an Puffergrößen drehen: `build/shell.map`
lesen und den Posten benennen, der überläuft. Die Bench kennt dasselbe
Problem und hat es dokumentiert.

## Flashen

Das Submodule hat **keine SWD-Pins**. Alles geht über DFU (USB), es gibt
keinen Debug-Probe-Weg und keinen Semihosting-Weg auf diesem Board.

```bash
dfu-util -a 0 -s 0x90040000:leave -D build/shell-sram.bin
```

Das Board muss dafür im DFU-Modus sein: **RESET drücken, dann BOOT im
Zwei-Sekunden-Fenster.** Anders als die Bench springt der Shell am Ende
nicht selbst in den Bootloader zurück — die Bench tut das, weil sie
wiederholt geflasht wird, der Shell soll laufen. Jedes Neuflashen kostet
also die zwei Tastendrücke.

Die Wavetable-Bank in QSPI muss normalerweise **nicht** mitgeschrieben
werden: sie liegt seit dem 7. August auf dem Board, und `shell-qspi.bin` ist
byte-identisch zu `bench-qspi.bin`. Wer ein frisches Submodule bespielt,
schreibt sie einmal nach `0x90100000` — ebenfalls über DFU, nicht über einen
Probe.

## Der Selbsttest, und warum es ihn gibt

Das Submodule hat keine Klinkenbuchse. „Macht es Ton" lässt sich ohne
Verdrahtung also nicht hören — aber messen: mit `SHELL_SELFTEST=1` läuft der
Shell seinen eigenen Ausgang eine Sekunde lang (500 Blöcke à 96 Samples)
über einen Peak-Detektor und legt das Urteil auf die User-LED.

| LED | Bedeutung |
|---|---|
| dunkel, bleibt dunkel | `main()` kam nie bis `StartAudio` — Init, SDRAM oder Audio |
| schnelles Flackern, 10 Hz | wir warten, der Callback kommt nicht durch |
| **Dauerlicht** | Callback lief 500 Blöcke, der Ausgang führte Signal |
| langsames Blinken, 2 Hz | Callback lief, der Ausgang war still |

Vier Zustände statt zwei, weil „dunkel" sonst gleichzeitig *still* und
*hängt beim Booten* hieße — das wäre kein Beweis, sondern ein Rätsel.

```bash
make -j8 SHELL_SELFTEST=1 images
```

**Default ist aus, und das ist der wichtige Teil:** der Detektor sitzt im
Audio-Callback und kostet dort Zyklen. Task 6 misst an genau dieser Stelle
den Shell-Aufschlag; ein mitgeschleppter Selbsttest liefe in diese Zahl
hinein.

**Ergebnis 2026-08-08, Submodule `3859386B3330`: Dauerlicht.** Vorher wurde
einmal bewiesen, dass die LED überhaupt „still" sagen *kann* — dasselbe
Image mit `kSilenceFloor` künstlich auf `1.0f` (über dem erwarteten Peak von
0,42) blinkt langsam. Ohne diesen Gegenbeweis wäre Dauerlicht keine Messung,
sondern eine Zusicherung. Was damit **nicht** gezeigt ist: wie es klingt.
Der Ausgang ist nicht verdrahtet, das Hören steht aus.

## Offener Befund: ein Störton auf der Blockrate

**Der Shell klingt, aber er klingt nicht sauber.** Am 8. August 2026 auf dem
zweiten Submodule (Seriennummer `385138563330`, Audio auf 3,5-mm-Buchsen)
gehört und gemessen: ein Störton mit konstanter Amplitude auf der
Audio-Blockrate, dazu hörbares Zerren des Synths. **Der Shell ist damit
nicht fertig, sondern gerade so weit, dass man den Fehler sehen kann.**

Was gemessen ist — Aufnahme über ein Audiointerface, FFT, Vergleich gegen
den Desktop-Render desselben Betriebspunkts:

| Beobachtung | Zahl |
|---|---|
| Störton relativ zum Gesamt-RMS, Board | 4,9 dB darunter |
| dieselbe Größe, Desktop-Render | 33,0 dB darunter |
| **Überschuss** | **28 dB** |

Was dadurch **ausgeschlossen** ist, jeweils durch eine eigene Messung:

- **Die Hardware.** Ein intern erzeugter Sinus und ein reiner Durchleiter
  laufen durch denselben Codec, dieselbe DMA, dieselbe Analogstufe, dieselbe
  Masse und dasselbe USB — und sind sauber (Störton −90 dBFS statt
  −59 dBFS). Kein Lötfehler, kein Brummen, keine Versorgungsfrage.
- **Der Audioeingang.** Der Durchleiter zeigt ihn als praktisch still
  (RMS −73,8 dBFS, kein Anteil auf der Blockrate).
- **Die Optimierung.** Der Ton überlebt den Wechsel von `-O2` auf `-O3`
  unverändert (−58,5 → −59,5 dBFS). `-O2` war trotzdem ein echter Fehler in
  diesem Makefile und ist korrigiert.
- **Das Steuerraster der Engine.** `Center::kCtrlInterval` steht fest auf 96
  Samples und hängt nicht an der Blockgröße. Bei Blockgröße 192 **wandert**
  der Ton von 500 Hz auf 250 Hz mit. Er gehört also zur **Blockgrenze**,
  nicht zum Steuerraster.

- **Eine CPU-Überlast.** Das war die naheliegende Erklärung, und sie ist
  **widerlegt**. `SHELL_CPU_PROBE=1` misst an diesem Betriebspunkt
  **62,78 % avg, 65,30 % max, 48,42 % min** (5 000 Blöcke, `sr=48000`,
  `block=96`, vom Board selbst gemeldet). Das sind 35 Punkte Luft. Zur
  Einordnung: die Bench-Zeile `instrument_init` liegt bei 66,58 / 77,96 %,
  die Messung ist also plausibel. Der Callback hält seine Frist.

Die Sonde hat nebenbei eine falsche Annahme korrigiert: die Blockgröße ist
**96**, nicht 48. Eine frühere Schätzung aus Phasendauern eines
Diagnose-Images war falsch — deshalb meldet die Sonde `sr` und `block` jetzt
selbst mit, denn eine Last in Prozent ist ohne die Blockgröße, gegen die sie
gerechnet wurde, bedeutungslos.

- **Die Blockarithmetik der Engine.** `tests/test_block_size_invariance.cpp`
  rendert denselben Betriebspunkt mit `n=1` und mit `n=96` und verlangt
  Übereinstimmung besser als −60 dB relativ zum Signal. Der Test ist grün.
  (Er hat eine echte Lücke geschlossen: `host/render/main.cpp` ruft
  `process(..., 1)` auf — **ein** Sample pro Aufruf, die gesamte
  Desktop-Historie ist mit `n=1` entstanden, die Firmware ruft mit `n=96`
  auf.)

### Und was es stattdessen ist

**Der Störton steht nicht in den Samples.** Ein Bau, in dem die Engine voll
läuft, ihr Ergebnis aber verworfen wird und der Callback nur Nullen
schreibt, liefert den Ton bei **exakt demselben Pegel**:

| Bau | RMS | 500 Hz |
|---|---|---|
| Engine, Ausgang normal | −54,6 dBFS | −59,5 dBFS |
| Engine läuft, Ausgang auf Stille gezwungen | −59,7 dBFS | **−59,5 dBFS** |
| Engine läuft, Stille, **Blockzeit leergedreht** | −63,9 dBFS | **−67,0 dBFS** |
| Engine aus, Durchleiter (Referenz) | −73,8 dBFS | −90,1 dBFS |

Er erreicht den Ausgang also, **ohne den Signalweg zu benutzen** — eine
Einkopplung, kein Rechenfehler. Und er hängt daran, wie die Rechenaktivität
innerhalb des Blocks verteilt ist: wird die Leerlauflücke mit einer
`nop`-Schleife gefüllt, fällt er um **7,5 dB**.

Nicht um mehr, und das ist die ehrliche Einschränkung dieses Versuchs: die
Füllschleife ist nur *zeitlich* konstant, nicht *inhaltlich*. Der
Engine-Anteil rechnet mit SDRAM und FPU, der Füllanteil dreht `nop` — das
Stromprofil bleibt im Blocktakt moduliert, nur schwächer. Der Versuch stützt
die Erklärung „block-periodisches Aktivitätsprofil", er beweist sie nicht,
und **zwischen Versorgungsrippel, Masseeinkopplung und Abstrahlung trennt er
gar nicht.**

**Konsequenz, und sie ist wichtiger als die Ursache:** das ist ein Befund
über den **Aufbau**, nicht über die Engine. Kein Grund, an `engine/` etwas
zu ändern. Für Phase 1 heißt es dagegen sehr wohl etwas — Entkopplung,
Trennung von analoger und digitaler Versorgung und der Abstand zwischen
Codec-Analogteil und den Schaltströmen des MCU gehören auf dem eigenen PCB
bewusst entworfen und nicht gehofft.

**Der nächste Schritt, wenn jemand die Ursache wirklich will:** denselben
Betriebspunkt auf einem Daisy Seed mit dessen eigenem Audioausgang messen.
Zeigt der es auch, steckt es im Modul; zeigt er es nicht, im Trägerboard.
Das ist eine Messung und keine Vermutung, und sie kostet einen Boardwechsel.

## Wo die nächste Arbeit hingeht

`shell/controls.{h,cpp}` (Task 6 des Phase-0-Plans) bringt den ersten Poti
über einen `74HC4051` herein und misst, was Mux-Scan und LED-Ausgabe auf die
CPU-Reserve kosten. Diese Reserve ist **2,17 Punkte**, gemessen auf diesem
Board — keine Seed-Zahl darf dafür zitiert werden, auch nicht als Näherung
(`docs/bench/2026-08-07-seed-vs-patch-sm.md`).
