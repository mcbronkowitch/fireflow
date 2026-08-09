# I/O-Budget — FireFlow auf 42 HP

> **Stand 2026-08-08, Git `9326fee`. Dieses Dokument ist VORARBEIT, keine
> Entscheidung.** Task 2 des Phase-0-Plans hält ausdrücklich fest, dass die
> Panel-Reduktion Instrumentendesign ist und Bastian gehört. Was hier steht,
> ist alles, was sich ohne diese Entscheidung feststellen lässt: die
> Ausgangszahlen, die tatsächlich vorhandenen Pins, die geometrische
> Kapazität und die Buchsen. Die Spalte **Einstufung** ist leer und bleibt
> leer, bis sie in einer eigenen Sitzung ausgefüllt wird.
>
> **Nachtrag 2026-08-09** (`2026-08-09-hw-control-reduction-design.md`, Tasks
> 1–10): die Ausgangszahl von 82 Runtime-Parametern, auf der §1/§2 unten
> aufbauen, existiert nicht mehr — das Instrument ist auf **68** gesunken.
> §1/§2 sind unten auf den aktuellen Stand nachgezogen; §3–§6 (Pins,
> geometrische Kapazität, Buchsen) sind von der Parameterzahl nicht direkt
> abhängig und bleiben unverändert. Die 60-HP-Entscheidung
> (`2026-08-08-fireflow-hardware-envelope-design.md`) hat die 42-HP-Prämisse
> dieses Dokuments ohnehin bereits überholt; das ist weiterhin Task-2-Vorarbeit
> für die Neurechnung, keine Behauptung, dass 42 HP noch das Ziel ist.

## 1. Die Ausgangslage

Ausgabe von `tools/count_panel_controls.py`, das `host/vcv/res/gen_panel.py`
als Autorität liest — eine Handzählung veraltet beim nächsten Panel-Commit.

```
panel        68
appended      0
hidden        0
runtime      68
part_a       20
part_b       20
shared       10
inputs        4
outputs       6
lights        4
```

Nach Bauform der 68 Runtime-Parameter:

| Glyph im VCV-Panel | Anzahl |
|---|---:|
| kleiner Poti (`SMKNOB`) | 36 |
| grosser Poti (`BIGKNOB`) | 17 |
| Rastschalter (`LATCH`) | 4 |
| Rastpoti, ganzzahlig (`KNOBI`) | 7 |
| Taster (`SMBTN`) | 0 |
| Poti bipolar (`KNOBC`) | 4 |
| Kippschalter (`SW2`) | 0 |
| **Summe** | **68** |

`HIDDEN_PARAMS` ist seit Task 10 **leer** — es gibt keine menü-only
*Parameter* mehr. Das Kontextmenü selbst existiert weiterhin (Resync to
bar, BBD Freeze Attack, die Sampler-Untermenüs, Excitation-Flags, Copy
terrain code) — es trägt nur keinen einzigen Parameterwert mehr, der nicht
auch auf dem Panel sitzt. `DETUNE_A/B` ist zurück auf dem Panel
(`PLAY`-Reihe); `DRIVE_A/B` ist ersatzlos gelöscht (BBD-Drive kommt aus
`bbd_engine.cpp`, nie aus einem Panel-Wert). Die ursprüngliche Fußnote hier
betraf 4 `HIDDEN_PARAMS`, die es so nicht mehr gibt.

## 2. Die 68 Parameter

**Regel aus dem Plan:** jeder Parameter bekommt genau eine Einstufung aus
`KNOB` / `FADER` / `PAD` / `ENCODER` / `LAYER` / `SELECT` / `MENU` / `CUT`.
Die Summe der Zeilen muss 68 ergeben — das ist die Prüfsumme, die verhindert,
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
| 15 | `GRIT_A` | Poti bipolar | PART_A | |
| 16 | `COMP_A` | kleiner Poti | PART_A | |
| 17 | `STEPS_A` | Rastpoti (int) | PART_A | |
| 18 | `ENGINE_A` | Rastschalter | PART_A | |
| 19 | `DETUNE_A` | kleiner Poti | PART_A | |
| 20 | `SONG_A` | Rastpoti (int) | PART_A | |
| 21 | `RATE_B` | grosser Poti | PART_B | |
| 22 | `SHAPE_B` | grosser Poti | PART_B | |
| 23 | `DENSITY_B` | grosser Poti | PART_B | |
| 24 | `SMOOTH_B` | grosser Poti | PART_B | |
| 25 | `RANGE_B` | grosser Poti | PART_B | |
| 26 | `MELODY_B` | Poti bipolar | PART_B | |
| 27 | `MOD_B` | grosser Poti | PART_B | |
| 28 | `TUNE_B` | grosser Poti | PART_B | |
| 29 | `ATTACK_B` | kleiner Poti | PART_B | |
| 30 | `DECAY_B` | kleiner Poti | PART_B | |
| 31 | `RES_B` | kleiner Poti | PART_B | |
| 32 | `SUB_B` | kleiner Poti | PART_B | |
| 33 | `SOURCE_B` | kleiner Poti | PART_B | |
| 34 | `FLUX_B` | kleiner Poti | PART_B | |
| 35 | `GRIT_B` | Poti bipolar | PART_B | |
| 36 | `COMP_B` | kleiner Poti | PART_B | |
| 37 | `STEPS_B` | Rastpoti (int) | PART_B | |
| 38 | `ENGINE_B` | Rastschalter | PART_B | |
| 39 | `DETUNE_B` | kleiner Poti | PART_B | |
| 40 | `SONG_B` | Rastpoti (int) | PART_B | |
| 41 | `MORPH` | grosser Poti | SHARED | |
| 42 | `TEMPO` | kleiner Poti | SHARED | |
| 43 | `COUPLE` | kleiner Poti | SHARED | |
| 44 | `SCALE` | Rastpoti (int) | SHARED | |
| 45 | `DRIFT` | kleiner Poti | SHARED | |
| 46 | `REV_SIZE` | kleiner Poti | SHARED | |
| 47 | `REV_DECAY` | kleiner Poti | SHARED | |
| 48 | `REV_TONE` | kleiner Poti | SHARED | |
| 49 | `REV_DIFF` | kleiner Poti | SHARED | |
| 50 | `CHOKE` | kleiner Poti | SHARED | |
| 51 | `FILT_A` | kleiner Poti | — | |
| 52 | `FILT_B` | kleiner Poti | — | |
| 53 | `TIDE` | kleiner Poti | — | |
| 54 | `FLUXRATE_A` | Rastpoti (int) | — | |
| 55 | `FLUXRATE_B` | Rastpoti (int) | — | |
| 56 | `FLUXFB_A` | kleiner Poti | — | |
| 57 | `FLUXFB_B` | kleiner Poti | — | |
| 58 | `COLOR_A` | grosser Poti | — | |
| 59 | `COLOR_B` | grosser Poti | — | |
| 60 | `LINK_A` | kleiner Poti | — | |
| 61 | `LINK_B` | kleiner Poti | — | |
| 62 | `STAGES_A` | kleiner Poti | — | |
| 63 | `STAGES_B` | kleiner Poti | — | |
| 64 | `REC_A` | Rastschalter | — | |
| 65 | `REC_B` | Rastschalter | — | |
| 66 | `REV_MIX_A` | kleiner Poti | — | |
| 67 | `REV_MIX_B` | kleiner Poti | — | |
| 68 | `SHUFFLE` | kleiner Poti | — | |

Retiriert seit Stand 2026-08-08 (nicht mehr Teil der 68, Begründung siehe
`2026-08-09-hw-control-reduction-design.md`): `GRITMODE_A/B`, `STEP_A/B`,
`FORM_A/B`, `NEWPHRASE_A/B` (PART_A/B); `SYNC`, `SPOT`, `MASTER_DRIVE`,
`SETTLE`, `REV_SMEAR`, `REV_MOD` (SHARED); `FLUXTIME_A/B` (in FLUX
aufgegangen); `DRIVE_A/B` (war `HIDDEN_PARAMS`, ersatzlos gelöscht).
`DETUNE_A/B` war `HIDDEN_PARAMS` und ist jetzt Zeile 19/39 oben — kein
Verlust, ein Umzug.

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
