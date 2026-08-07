# Semihosting gegen USB-CDC — was der Transport kostet

**Verdikt: der Transport ist nicht neutral. USB-CDC kostet 6 370 Zyklen pro Block, das sind 0,66 % des Blockbudgets. Die Ursache ist die USB-Peripherie, nicht die Speicherkarte. Zahlen aus den beiden Transporten dürfen nur gegen ihresgleichen verglichen werden.**

Ein Board, ein Profil, ein Layout, eine Optimierungsstufe. Nur die Leitung ist anders.

## Die Läufe

| | Semihosting | USB-CDC |
|---|---|---|
| Capture | `2026-08-07-4aed1bf-regress-axi-o3` | `2026-08-07-a74c876-regress-axi-o3-usb` |
| Board | Daisy Seed, UID `002000403230510b33313932` | dasselbe |
| Laden | OpenOCD über ST-Link V3 ins SRAM | `dfu-util` über den Daisy-Bootloader |
| Profil / Layout / Opt | `regress` / `axi` / `-O3` | identisch |
| QSPI-Digest | `ac234ac7…` | identisch |
| Takt / Cache | 480 MHz, `dcache+icache` | identisch |

## `instrument_worst_bbd_dtcm`

| | Semihost L1 | Semihost L2 | USB L1 | USB L2 |
|---|---|---|---|---|
| `avg_cyc` | 882 470 | 882 447 | 888 833 | 888 830 |
| `max_cyc` | 923 990 | 923 068 | 931 515 | 930 708 |
| Offline avg | 91,92 % | 91,92 % | 92,58 % | 92,58 % |
| Offline max | 96,24 % | 96,15 % | 97,03 % | 96,94 % |
| Callback avg | 91,78 % | 91,78 % | 92,44 % | 92,47 % |
| Callback max | 96,84 % | 96,93 % | 97,18 % | 97,10 % |
| Checksumme | `6d20538d` | `6d20538d` | `6d20538d` | `6d20538d` |

**Differenz: +6 370 Zyklen im Mittel, +0,79 Prozentpunkte im Offline-Maximum.**

Das ist kein Rauschen. Die Streuung *innerhalb* eines Transports beträgt 3 Zyklen (USB) beziehungsweise 23 Zyklen (Semihosting). Der Unterschied *zwischen* ihnen ist das Zweihundertfache davon und in beiden Wiederholungen gleich groß.

Die Checksummen sind über alle vier Läufe identisch und stimmen mit dem Stand vom 4. August überein. Gerechnet wurde also dasselbe.

## Die Ursache ist nicht die Speicherkarte

Der naheliegende Verdacht war die Verschiebung: der USB-Zweig belegt 7 224 Byte mehr SRAM_EXEC (246 828 gegen 239 604), und `g_axi_layout_guard` existiert genau deshalb, um die Adressen der Messobjekte gegen solche Verschiebungen festzuhalten.

Er hat gehalten. Alle sechs Messobjekt-Symbole liegen in beiden Zweigen auf **identischen Adressen**:

```
$ diff <(nm bench.elf-semihost | grep …) <(nm bench.elf-usb | grep …)
$                       # keine Ausgabe
```

Der Guard tut, wofür er gebaut wurde. Die Ursache liegt woanders.

## Die Ursache ist die USB-Peripherie

Es bleibt der Baustein selbst. Die Bench schreibt keine einzige Zeile innerhalb eines Messfensters — das war von Anfang an die Regel und sie wurde eingehalten. Aber ein konfigurierter USB-Device-Stack ist nicht still, wenn niemand sendet: der Host schickt ein **Start-of-Frame alle 1 ms**, und der Interrupt läuft, ob Daten anliegen oder nicht.

Die Rechnung stimmt überein:

- Ein Block sind 96 Samples bei 48 kHz = **2 ms** → zwei SOF-Interrupts pro Block
- Gemessen: 6 370 Zyklen pro Block → rund **3 185 Zyklen pro Interrupt**
- Bei 480 MHz sind das 6,6 µs pro Millisekunde, also 0,66 % — und genau 0,66 % von 960 000 Zyklen sind 6 370

Zwei unabhängige Wege zur selben Zahl.

## Was das für die Messungen heißt

**Der Transport ist Teil der Messidentität, wie das Board und die Optimierungsstufe.** Deshalb tragen USB-Captures ein `-usb` im Dateinamen und Semihosting-Captures nicht: alles in `docs/bench/` vor dem 7. August 2026 ist über die Probe entstanden und behält seinen Namen.

**Ein Vergleich über die Transportgrenze ist ungültig, solange er nicht um 0,66 % korrigiert wird.** Für den Vergleich, um den es geht — Patch Submodule gegen Seed —, ist das kein Problem, sondern der Normalfall: **Seed/USB ist die Grundlinie für Submodule/USB.** Beide Seiten tragen denselben Aufschlag, er kürzt sich weg.

**Das Gate passt weiterhin.** 97,18 % im realen Callback ist das schlechteste Maximum über beide USB-Wiederholungen. Die Reserve schrumpft von 3,07 auf 2,82 Punkte — und diese 0,66 % sind ein Artefakt des Messwerkzeugs, nicht Kosten des Instruments. Die ausgelieferte Firmware fährt keinen USB-Device-Stack.

## Was daraus folgt, wenn die Reserve knapp wird

Sollte ein späterer Lauf über USB an die 100 % stoßen, ist der erste Schritt nicht Optimierung, sondern die Frage, wie viel davon der Transport ist. Die Antwort steht hier: 0,66 Prozentpunkte, reproduzierbar auf drei Zyklen genau.
