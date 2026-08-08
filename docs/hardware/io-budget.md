# I/O-Budget — FireFlow auf 42 HP

> **Stand 2026-08-08, Git `9326fee`. Dieses Dokument ist VORARBEIT, keine
> Entscheidung.** Task 2 des Phase-0-Plans hält ausdrücklich fest, dass die
> Panel-Reduktion Instrumentendesign ist und Bastian gehört. Was hier steht,
> ist alles, was sich ohne diese Entscheidung feststellen lässt: die
> Ausgangszahlen, die tatsächlich vorhandenen Pins, die geometrische
> Kapazität und die Buchsen. Die Spalte **Einstufung** ist leer und bleibt
> leer, bis sie in einer eigenen Sitzung ausgefüllt wird.

## 1. Die Ausgangslage

Ausgabe von `tools/count_panel_controls.py`, das `host/vcv/res/gen_panel.py`
als Autorität liest — eine Handzählung veraltet beim nächsten Panel-Commit.

```
panel        80
appended      2
hidden        4
runtime      82
part_a       23
part_b       23
shared       16
inputs        4
outputs       6
lights        4
```

Nach Bauform der 82 Runtime-Parameter:

| Glyph im VCV-Panel | Anzahl |
|---|---:|
| kleiner Poti (`SMKNOB`) | 43 |
| grosser Poti (`BIGKNOB`) | 17 |
| Rastschalter (`LATCH`) | 8 |
| Rastpoti, ganzzahlig (`KNOBI`) | 7 |
| Taster (`SMBTN`) | 4 |
| Poti bipolar (`KNOBC`) | 2 |
| Kippschalter (`SW2`) | 1 |
| **Summe** | **82** |

Die 4 `HIDDEN_PARAMS` (`DETUNE_A/B`, `DRIVE_A/B`) sind **nicht** Teil der 82
und brauchen kein Bedienelement — sie stehen hier nur, damit niemand sie
später für vergessen hält.

## 2. Die 82 Parameter

**Regel aus dem Plan:** jeder Parameter bekommt genau eine Einstufung aus
`KNOB` / `FADER` / `PAD` / `ENCODER` / `LAYER` / `SELECT` / `MENU` / `CUT`.
Die Summe der Zeilen muss 82 ergeben — das ist die Prüfsumme, die verhindert,
dass etwas stillschweigend verschwindet.

| # | Parameter | Glyph im VCV-Panel | Gruppe | Einstufung |
|---:|---|---|---|---|
| 1 | `RATE_A` | grosser Poti | PART_A | |
| 2 | `SHAPE_A` | grosser Poti | PART_A | |
| 3 | `DENSITY_A` | grosser Poti | PART_A | |
| 4 | `SMOOTH_A` | grosser Poti | PART_A | |
| 5 | `RANGE_A` | grosser Poti | PART_A | |
| 6 | `MELODY_A` | Poti bipolar | PART_A | |
| 7 | `MOD_A` | grosser Poti | PART_A | |
| 8 | `TUNE_A` | grosser Poti | PART_A | |
| 9 | `ATTACK_A` | kleiner Poti | PART_A | |
| 10 | `DECAY_A` | kleiner Poti | PART_A | |
| 11 | `RES_A` | kleiner Poti | PART_A | |
| 12 | `SUB_A` | kleiner Poti | PART_A | |
| 13 | `SOURCE_A` | kleiner Poti | PART_A | |
| 14 | `FLUX_A` | kleiner Poti | PART_A | |
| 15 | `GRIT_A` | kleiner Poti | PART_A | |
| 16 | `COMP_A` | kleiner Poti | PART_A | |
| 17 | `STEPS_A` | Rastpoti (int) | PART_A | |
| 18 | `ENGINE_A` | Rastschalter | PART_A | |
| 19 | `GRITMODE_A` | Rastschalter | PART_A | |
| 20 | `STEP_A` | Rastschalter | PART_A | |
| 21 | `FORM_A` | Rastpoti (int) | PART_A | |
| 22 | `NEWPHRASE_A` | Taster | PART_A | |
| 23 | `SONG_A` | Rastpoti (int) | PART_A | |
| 24 | `RATE_B` | grosser Poti | PART_B | |
| 25 | `SHAPE_B` | grosser Poti | PART_B | |
| 26 | `DENSITY_B` | grosser Poti | PART_B | |
| 27 | `SMOOTH_B` | grosser Poti | PART_B | |
| 28 | `RANGE_B` | grosser Poti | PART_B | |
| 29 | `MELODY_B` | Poti bipolar | PART_B | |
| 30 | `MOD_B` | grosser Poti | PART_B | |
| 31 | `TUNE_B` | grosser Poti | PART_B | |
| 32 | `ATTACK_B` | kleiner Poti | PART_B | |
| 33 | `DECAY_B` | kleiner Poti | PART_B | |
| 34 | `RES_B` | kleiner Poti | PART_B | |
| 35 | `SUB_B` | kleiner Poti | PART_B | |
| 36 | `SOURCE_B` | kleiner Poti | PART_B | |
| 37 | `FLUX_B` | kleiner Poti | PART_B | |
| 38 | `GRIT_B` | kleiner Poti | PART_B | |
| 39 | `COMP_B` | kleiner Poti | PART_B | |
| 40 | `STEPS_B` | Rastpoti (int) | PART_B | |
| 41 | `ENGINE_B` | Rastschalter | PART_B | |
| 42 | `GRITMODE_B` | Rastschalter | PART_B | |
| 43 | `STEP_B` | Rastschalter | PART_B | |
| 44 | `FORM_B` | Rastpoti (int) | PART_B | |
| 45 | `NEWPHRASE_B` | Taster | PART_B | |
| 46 | `SONG_B` | Rastpoti (int) | PART_B | |
| 47 | `MORPH` | grosser Poti | SHARED | |
| 48 | `SYNC` | Kippschalter | SHARED | |
| 49 | `TEMPO` | kleiner Poti | SHARED | |
| 50 | `COUPLE` | kleiner Poti | SHARED | |
| 51 | `SCALE` | Rastpoti (int) | SHARED | |
| 52 | `DRIFT` | kleiner Poti | SHARED | |
| 53 | `SPOT` | Taster | SHARED | |
| 54 | `MASTER_DRIVE` | kleiner Poti | SHARED | |
| 55 | `SETTLE` | Taster | SHARED | |
| 56 | `REV_SIZE` | kleiner Poti | SHARED | |
| 57 | `REV_DECAY` | kleiner Poti | SHARED | |
| 58 | `REV_TONE` | kleiner Poti | SHARED | |
| 59 | `REV_DIFF` | kleiner Poti | SHARED | |
| 60 | `REV_SMEAR` | kleiner Poti | SHARED | |
| 61 | `REV_MOD` | kleiner Poti | SHARED | |
| 62 | `CHOKE` | kleiner Poti | SHARED | |
| 63 | `FILT_A` | kleiner Poti | — | |
| 64 | `FILT_B` | kleiner Poti | — | |
| 65 | `TIDE` | kleiner Poti | — | |
| 66 | `FLUXRATE_A` | kleiner Poti | — | |
| 67 | `FLUXRATE_B` | kleiner Poti | — | |
| 68 | `FLUXFB_A` | kleiner Poti | — | |
| 69 | `FLUXFB_B` | kleiner Poti | — | |
| 70 | `COLOR_A` | grosser Poti | — | |
| 71 | `COLOR_B` | grosser Poti | — | |
| 72 | `LINK_A` | kleiner Poti | — | |
| 73 | `LINK_B` | kleiner Poti | — | |
| 74 | `STAGES_A` | kleiner Poti | — | |
| 75 | `STAGES_B` | kleiner Poti | — | |
| 76 | `REC_A` | Rastschalter | — | |
| 77 | `REC_B` | Rastschalter | — | |
| 78 | `REV_MIX_A` | kleiner Poti | — | |
| 79 | `REV_MIX_B` | kleiner Poti | — | |
| 80 | `SHUFFLE` | kleiner Poti | — | |
| 81 | `FLUXTIME_A` | kleiner Poti | — | |
| 82 | `FLUXTIME_B` | kleiner Poti | — | |

## 3. Was das Patch Submodule wirklich hergibt

Gelesen aus `lib/libDaisy/src/daisy_patch_sm.h` (Pinliste) und
`daisy_patch_sm.cpp:10-21` (ADC-Zuordnung). **Das ist der Teil dieses
Dokuments, der die Reduktion am härtesten einschränkt.**

Das Modul hat 40 Anschlüsse (A1–A10, B1–B10, C1–C10, D1–D10). Davon sind
vergeben und nicht verfügbar:

| Pins | Belegung |
|---|---|
| A1, A4, A5, A6, A7, A10 | −12 V, GND, +12 V, +5 V, GND, +3V3 |
| A8, A9 | USB D−/D+ — **wird für DFU gebraucht**, das Board hat keine SWD-Pins |
| B1–B4 | Audio Out R/L, Audio In R/L |

Bleibt für Bedienelemente und Buchsen:

| Pins | Funktion | brauchbar als |
|---|---|---|
| C2–C9 | CV In 1–8 | 8 ADC-Eingänge **mit bipolarer Eingangsstufe** (±5 V → 0–3,3 V) |
| **A2, A3** | UART1 Rx/Tx | **ADC 9/10 — rohe 0–3,3-V-Eingänge** |
| **D8, D9** | SPI2 MISO/MOSI | **ADC 11/12 — rohe 0–3,3-V-Eingänge** |
| C1, C10 | CV Out 1/2 | 2 DAC-Ausgänge, **0–5 V unipolar** |
| B5, B6 | Gate Out 1/2 | 2 digitale Ausgänge |
| B9, B10 | Gate In 1/2 | 2 digitale Eingänge |
| B7, B8 | I2C1 SCL/SDA | I2C oder 2 GPIO |
| D1, D10 | SPI2 CS/SCK | SPI oder 2 GPIO |
| D2–D7 | SDMMC | SD-Karte oder 6 GPIO |

### Die entscheidende Zahl: vier rohe ADC-Pins

Ein Poti liefert 0–3,3 V. Dafür taugen **A2, A3, D8, D9** — die acht CV-Pins
tragen die bipolare Eingangsstufe des Moduls und würden ein unipolares
Potisignal auf einen Teil des ADC-Bereichs stauchen. Also:

```
Mux-Kanäle = rohe ADC-Pins × Kanäle pro Chip

  4 × 74HC4051  (8:1)  = 32 Kanäle,  3 Adress-GPIO (geteilt)
  4 × CD74HC4067 (16:1) = 64 Kanäle, 4 Adress-GPIO (geteilt)
```

**32 Kanäle reichen für ein Panel mit 45–55 Bedienelementen nicht.** Mit
16:1-Muxen reichen 64 — das ist die Layoutfrage, die der Plan nach Phase 1
verschiebt, aber die Zahl steht jetzt schon fest und sie ist knapp.

`InitMux()` in `lib/libDaisy/src/per/adc.h` treibt die Adressleitungen selbst
und scannt per DMA. Der Mux-Scan kostet damit in der Audioschleife praktisch
nichts — die Warnung im Plan vor blockierendem Scan gilt `src/hw/sr_165.h`,
dem Schieberegister für Taster, nicht dem analogen Mux.

### Kollision, die man kennen muss: SPI2 gegen ADC 11/12

**`D8`/`D9` sind gleichzeitig SPI2 MISO/MOSI und ADC 11/12.** Wer den SPI-Bus
benutzt — etwa für einen `MAX11300` —, verliert damit **die Hälfte der rohen
ADC-Pins** und kommt auf 2 × 16 = 32 Mux-Kanäle. Das ist keine Kleinigkeit,
sondern genau die Stelle, an der sich die zwei I/O-Strategien gegenseitig im
Weg stehen. Zu entscheiden ist es hier nicht, aber zu wissen schon.

## 4. Die geometrische Kapazität

Nutzbare Fläche 42 HP: rund **213 × 115 mm**. Kapazität bei quadratischem
Raster, ohne Abzug für Buchsen und Beschriftung:

| Raster | Spalten × Reihen | Plätze |
|---:|---|---:|
| 15 mm | 14 × 7 | 98 |
| 18 mm | 11 × 6 | 66 |
| 20 mm | 10 × 5 | 50 |
| 22 mm | 9 × 5 | 45 |

Das ist eine **Obergrenze und keine Zielzahl**: die Buchsenreihe, die
Beschriftung und die Ränder gehen davon ab. Der Plan verlangt zusätzlich
**20 % Reserve** auf die Zahl der Bedienelemente, bevor die Rechnung als
bestanden gilt.

## 5. Die Buchsen

Aus `gen_panel.py`, 4 Eingänge und 6 Ausgänge:

| Buchse | Art | Anmerkung |
|---|---|---|
| `IN_L`, `IN_R` | Audio in | direkt an B4/B3 des Submodule |
| `CLOCK`, `RESET` | Gate/Trigger in | B10/B9 (Gate In 1/2) oder CV In |
| `OUT_L`, `OUT_R` | Audio out | direkt an B2/B1 |
| `PITCH_A`, `PITCH_B` | CV out | **hier klemmt es** |
| `GATE_A`, `GATE_B` | Gate out | B5/B6 (Gate Out 1/2) |

Audio-I/O und die CV-Wandler sind auf dem Submodule bereits vorhanden — das
ist der Grund, dieses Modul zu nehmen, und muss in der Rechnung auftauchen.

**Die Lücke:** das Panel will **zwei** Pitch-Ausgänge, das Submodule hat
**zwei** CV-Ausgänge insgesamt — und die liefern **0–5 V unipolar**
(`WriteCvOut`, `daisy_patch_sm.h:170`). Für zwei Pitch-CVs geht das gerade
auf, für Pitch mit negativem Bereich oder für jede weitere CV-Ausgabe nicht.
Das ist die konkrete Lücke, für die ein externer DAC oder ein `MAX11300`
in Frage käme.

## 6. Was hier bewusst NICHT steht

- **Die Einstufung der 82 Parameter.** Eigene Sitzung, eigene Entscheidung.
- **Die Wahl 8:1 gegen 16:1.** Hängt an Verfügbarkeit und Bestückungspreis
  bei JLCPCB und gehört vor den 11. September, nicht in Phase 0.
- **Ob ein `MAX11300` das Buchsenfeld übernimmt.** Der Kandidat ist real
  (ein Modul ist vorhanden und verdrahtet), aber er kostet SPI2 und damit
  zwei rohe ADC-Pins, und seine CPU-Kosten sind ungemessen.
- **Die Einschwingzeit pro Mux-Kanal.** Die misst Task 6 Schritt 5b, und
  ohne sie ist keine Panelgröße gegengerechnet.
