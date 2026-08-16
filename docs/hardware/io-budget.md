# I/O-Budget — FireFlow auf 60 HP

> **Stand 2026-08-16.** Ursprünglich am 2026-08-08 (Git `9326fee`) als
> **Vorarbeit auf 42 HP** geschrieben, mit leerer Einstufungsspalte in §2, weil
> die Panel-Reduktion Instrumentendesign ist und Bastian gehört.
>
> **Diese Prämisse ist zweimal überholt worden, und beide Male nach oben.** Die
> Envelope-Entscheidung (`2026-08-08-fireflow-hardware-envelope-design.md`)
> setzte 60 HP mit vollem Control-Set, und die Bedienelement-Reduktion
> (`2026-08-09-hw-control-reduction-design.md`) senkte das Inventar von 82 auf
> 68 Parameter. Danach haben **drei Panel-Runden** — Neugruppierung (2.21.1),
> Neuverteilung (2.21.2) und Plattenrunde 2a (2.21.3) — Controls verschoben,
> zusammengelegt und die Bauform geändert.
>
> **Was das für dieses Dokument heißt:** §1, §2, §4 und §5 sind auf das
> **gebaute** Panel nachgezogen und nicht mehr Vorarbeit — die Einstufung ist
> entschieden, sie steht in §2 und sie kommt aus dem Generator, nicht aus einer
> Absicht. §3 (Pins) ist von der Parameterzahl unabhängig und steht unverändert;
> es hat nur einen Nachtrag zur Mux-Topologie bekommen. §6 listet, was heute
> noch offen ist — es ist eine andere Liste als 2026-08-08.

## 1. Die Ausgangslage

**Autorität ist der Generator, nicht dieses Dokument.** Eine Handzählung
veraltet beim nächsten Panel-Commit; jede Zahl unten stammt aus einem Lauf und
ist mit ihm reproduzierbar.

```
python host/vcv/res/gen_hw_panel.py          # aus host/vcv/, idempotent
params=69 inputs=12 outputs=6 lights=4  panel=60HP

python tools/count_panel_controls.py         # aus dem Repo-Wurzelverzeichnis
panel 68 · appended 1 · runtime 69 · part_a 20 · part_b 20 · shared 10
```

Beide Zahlen sind gemessen 2026-08-16. **Die 69 sind dieselben 69:** jeder
Parameter des Hardware-Panels ist auch ein VCV-Runtime-Parameter, es gibt
keinen hardware-only *Parameter* (geprüft gegen `RUNTIME_PANEL_PARAMS`, leere
Differenzmenge). Was nur auf der Hardware existiert, sind Bedienelemente ohne
Parameterwert und die LEDs — siehe unten.

**69 Runtime-Parameter auf 67 physischen Positionen.** Die Differenz sind die
beiden bewussten Doppelbelegungen: `STAGES_A/B` (BEND) teilt sich den Knopf mit
`ATTACK_A/B`, mit engine-abhängigem Moduswechsel. Das ist die einzige
Mehrfachbelegung auf dem Panel.

Nach Bauform **auf der Hardware** — nicht nach dem VCV-Glyph, der eine andere
Größenklasse haben darf und bei mehreren Controls auch hat:

| Bauform | Radius | Anzahl | Positionen |
|---|---:|---:|---:|
| grosser Poti (`G`) | 8,5 mm | 16 | 16 |
| kleiner Poti (`S`) | 6,0 mm | 51 | 49 |
| Taster (`P`) | 4,0 mm | 2 | 2 |
| **Summe** | | **69** | **67** |

Dazu 2 Elemente, die **kein** Runtime-Parameter sind und deshalb in keiner der
Zahlen oben stecken (`HW_ONLY` im Generator):

| Element | Art | Anmerkung |
|---|---|---|
| `MODBTN`, `SHIFTBTN` | Taster | reserviert, ohne Funktion — 2 weitere Pads |

Macht **4 Taster** (`REC_A/B` + die zwei reservierten).

**Die LEDs laufen seit der LED-Feedback-Runde (2026-08-16) nicht mehr über
`HW_ONLY`.** Alle Lampen auf der Platte sind jetzt echte `LightId`s — die
Platte trägt **21**, nicht mehr 10: 8 der bisherigen 10 blieben, `CAP_A_L`/
`CAP_B_L` sind mit der Capture-Sequenz gelöscht (die gibt es seit 2026-07-14
nicht mehr, siehe `docs/roadmap.md`), und 13 sind neu. Sechs davon sind
absichtlich dunkel — `FLOW_A_L`, `FLOW_B_L`, `TEMPO_L`, `SYNC_L` und die zwei
Pad-Lampen `MODBTN_L`/`SHIFTBTN_L` —, aber jeden Block *geschrieben*, nicht
übersprungen; ein Gate prüft das. Herleitung, Platzierung und die offene
Mux-Breite (8:1 gegen 16:1, deshalb nimmt `duty()` den Schrittzähler als
Parameter): Spec
[`2026-08-16-led-feedback-design.md`](../superpowers/specs/2026-08-16-led-feedback-design.md)
§2–§3, und `docs/roadmap.md`.

> **Die LED-Zahl war erst gefallen, und zwar absichtlich.** Die Neugruppierung
> hatte das Feld auf 20 festgelegt (Envelope-Spec §1, oberes Ende des
> Korridors); die Neuverteilung hat `ENGINE_A/B` vom Taster zum
> **fünf-Zonen-Rastpoti** am Kopf der VOICE-Gruppe gemacht und damit zehn
> Anzeige-LEDs eingespart. 20 − 10 = 10, und die Kette blieb dieselbe — bis
> die LED-Feedback-Runde das Feld auf 21 anhob, mehr als das ursprüngliche
> Korridor-Ende, weil kein Rundungsdruck mehr bestand.

`HIDDEN_PARAMS` ist seit der Reduktionsrunde leer — es gibt keinen menü-only
*Parameter* mehr. Das Kontextmenü selbst existiert weiter (Resync to bar, BBD
Freeze Attack, die Sampler-Untermenüs, Excitation-Flags), es trägt nur keinen
Wert mehr, der nicht auch auf dem Panel sitzt.

## 2. Die 69 Parameter

Die Einstufungsspalte dieses Abschnitts war bis 2026-08-09 leer und war die
offene Frage des ganzen Dokuments. **Sie ist entschieden.** Was hier steht, ist
nicht mehr eine Absicht, sondern das gebaute Panel, ausgelesen aus
`host/vcv/res/gen_hw_panel.py` (`HW_PARAMS`, `hw_class()`, Positionen in mm vom
linken oberen Panelrand). Die Prüfsumme der Zeilen ist **69**.

| # | Parameter | Bauform auf der Hardware | Gruppe | Position (mm) |
|---:|---|---|---|---|
| 1 | `RATE_A` | kleiner Poti | PART_A | 61,00 / 14,50 |
| 2 | `SHAPE_A` | kleiner Poti | PART_A | 18,25 / 34,00 |
| 3 | `DENSITY_A` | grosser Poti | PART_A | 40,75 / 53,00 |
| 4 | `SMOOTH_A` | kleiner Poti | PART_A | 31,25 / 34,00 |
| 5 | `RANGE_A` | kleiner Poti | PART_A | 44,25 / 34,00 |
| 6 | `MELODY_A` | kleiner Poti | PART_A | 74,00 / 14,50 |
| 7 | `MOD_A` | grosser Poti | PART_A | 21,75 / 53,00 |
| 8 | `TUNE_A` | kleiner Poti | PART_A | 17,00 / 76,00 |
| 9 | `ATTACK_A` | kleiner Poti | PART_A | 68,25 / 34,00 — geteilt mit `STAGES_A` |
| 10 | `DECAY_A` | kleiner Poti | PART_A | 81,25 / 34,00 |
| 11 | `RES_A` | kleiner Poti | PART_A | 94,25 / 34,00 |
| 12 | `SUB_A` | kleiner Poti | PART_A | 107,25 / 34,00 |
| 13 | `SOURCE_A` | kleiner Poti | PART_A | 102,25 / 50,22 |
| 14 | `FLUX_A` | grosser Poti | PART_A | 67,00 / 76,00 |
| 15 | `GRIT_A` | kleiner Poti | PART_A | 106,50 / 95,00 |
| 16 | `COMP_A` | grosser Poti | PART_A | 106,50 / 76,00 |
| 17 | `STEPS_A` | kleiner Poti | PART_A | 35,00 / 14,50 |
| 18 | `ENGINE_A` | kleiner Poti | PART_A | 70,25 / 50,22 |
| 19 | `DETUNE_A` | kleiner Poti | PART_A | 30,00 / 76,00 |
| 20 | `SONG_A` | kleiner Poti | PART_A | 48,00 / 14,50 |
| 21 | `RATE_B` | kleiner Poti | PART_B | 243,80 / 14,50 |
| 22 | `SHAPE_B` | kleiner Poti | PART_B | 286,55 / 34,00 |
| 23 | `DENSITY_B` | grosser Poti | PART_B | 264,05 / 53,00 |
| 24 | `SMOOTH_B` | kleiner Poti | PART_B | 273,55 / 34,00 |
| 25 | `RANGE_B` | kleiner Poti | PART_B | 260,55 / 34,00 |
| 26 | `MELODY_B` | kleiner Poti | PART_B | 230,80 / 14,50 |
| 27 | `MOD_B` | grosser Poti | PART_B | 283,05 / 53,00 |
| 28 | `TUNE_B` | kleiner Poti | PART_B | 287,80 / 76,00 |
| 29 | `ATTACK_B` | kleiner Poti | PART_B | 236,55 / 34,00 — geteilt mit `STAGES_B` |
| 30 | `DECAY_B` | kleiner Poti | PART_B | 223,55 / 34,00 |
| 31 | `RES_B` | kleiner Poti | PART_B | 210,55 / 34,00 |
| 32 | `SUB_B` | kleiner Poti | PART_B | 197,55 / 34,00 |
| 33 | `SOURCE_B` | kleiner Poti | PART_B | 202,55 / 50,22 |
| 34 | `FLUX_B` | grosser Poti | PART_B | 237,80 / 76,00 |
| 35 | `GRIT_B` | kleiner Poti | PART_B | 198,30 / 95,00 |
| 36 | `COMP_B` | grosser Poti | PART_B | 198,30 / 76,00 |
| 37 | `STEPS_B` | kleiner Poti | PART_B | 269,80 / 14,50 |
| 38 | `ENGINE_B` | kleiner Poti | PART_B | 234,55 / 50,22 |
| 39 | `DETUNE_B` | kleiner Poti | PART_B | 274,80 / 76,00 |
| 40 | `SONG_B` | kleiner Poti | PART_B | 256,80 / 14,50 |
| 41 | `MORPH` | grosser Poti | SHARED | 152,40 / 53,00 |
| 42 | `TEMPO` | kleiner Poti | SHARED | 139,40 / 34,00 |
| 43 | `COUPLE` | kleiner Poti | SHARED | 152,40 / 34,00 |
| 44 | `SCALE` | kleiner Poti | SHARED | 139,40 / 14,50 |
| 45 | `DRIFT` | kleiner Poti | SHARED | 152,40 / 14,50 |
| 46 | `REV_SIZE` | kleiner Poti | SHARED | 136,40 / 76,00 |
| 47 | `REV_DECAY` | grosser Poti | SHARED | 152,40 / 79,00 |
| 48 | `REV_TONE` | kleiner Poti | SHARED | 152,40 / 97,00 |
| 49 | `REV_DIFF` | kleiner Poti | SHARED | 168,40 / 76,00 |
| 50 | `CHOKE` | kleiner Poti | SHARED | 165,40 / 14,50 |
| 51 | `FILT_A` | grosser Poti | — | 86,25 / 53,00 |
| 52 | `FILT_B` | grosser Poti | — | 218,55 / 53,00 |
| 53 | `TIDE` | kleiner Poti | — | 136,40 / 50,22 |
| 54 | `FLUXRATE_A` | kleiner Poti | — | 54,00 / 89,86 |
| 55 | `FLUXRATE_B` | kleiner Poti | — | 250,80 / 89,86 |
| 56 | `FLUXFB_A` | kleiner Poti | — | 67,00 / 95,00 |
| 57 | `FLUXFB_B` | kleiner Poti | — | 237,80 / 95,00 |
| 58 | `COLOR_A` | grosser Poti | — | 23,50 / 95,00 |
| 59 | `COLOR_B` | grosser Poti | — | 281,30 / 95,00 |
| 60 | `LINK_A` | kleiner Poti | — | 80,00 / 89,86 |
| 61 | `LINK_B` | kleiner Poti | — | 224,80 / 89,86 |
| 62 | `STAGES_A` | kleiner Poti | — | 68,25 / 34,00 — geteilt mit `ATTACK_A` |
| 63 | `STAGES_B` | kleiner Poti | — | 236,55 / 34,00 — geteilt mit `ATTACK_B` |
| 64 | `REC_A` | Taster | — | 101,00 / 14,50 |
| 65 | `REC_B` | Taster | — | 203,80 / 14,50 |
| 66 | `REV_MIX_A` | grosser Poti | — | 136,40 / 95,00 |
| 67 | `REV_MIX_B` | grosser Poti | — | 168,40 / 95,00 |
| 68 | `SHUFFLE` | kleiner Poti | — | 165,40 / 34,00 |
| 69 | `PACE` | kleiner Poti | — | 168,40 / 50,22 |

**Zugegangen seit dem Stand 2026-08-08:** `PACE` (Zeile 69), der globale
Modulations-Zeitdehner aus der PACE-Runde — der einzige Parameter, den das
Instrument seit der Reduktion dazubekommen hat, und ein Beleg dafür, dass
„one in, one out" wirklich tot ist: es musste nichts weichen.

**Retiriert und nicht mehr Teil der Zählung** (Begründung in
`2026-08-09-hw-control-reduction-design.md`): `GRITMODE_A/B`, `STEP_A/B`,
`FORM_A/B`, `NEWPHRASE_A/B`; `SYNC`, `SPOT`, `MASTER_DRIVE`, `SETTLE`,
`REV_SMEAR`, `REV_MOD`; `FLUXTIME_A/B` (in FLUX aufgegangen); `DRIVE_A/B`
(ersatzlos, BBD-Drive kommt aus `bbd_engine.cpp`).

**Eine offene Kleinigkeit, die genau hier auffällt:** `STAGES` hat im Generator
ein **leeres Plattenwort** (`HW_CAPTION["STAGES"] = ""`), BEND wird also gar
nicht aufs Panel gedruckt. Das ist bekannt und nicht entschieden — auf der
Hardware ist der Knopf da, die Beschriftung nicht.

## 3. Was das Patch Submodule wirklich hergibt

Gelesen aus `lib/libDaisy/src/daisy_patch_sm.h` (Pinliste) und
`daisy_patch_sm.cpp:10-21` (ADC-Zuordnung). **Das ist der Teil dieses
Dokuments, der die Reduktion am härtesten einschränkt** — und der einzige, den
die drei Panel-Runden nicht berührt haben.

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
Potisignal auf einen Teil des ADC-Bereichs stauchen.

> **Nachtrag 2026-08-08 (Envelope-Spec §2), und er ändert das Ergebnis:** die
> Rechnung unten geht davon aus, dass die Adressleitungen GPIOs kosten. Das tun
> sie nicht mehr — **Adress- *und* Enable-Leitungen der 74HC4067 wandern auf die
> 595-Kette.** Damit teilen sich bis zu **acht** Muxe die vier Sense-Pins:
> **128 Kanäle für null GPIOs.** Genau diese Einsparung ist es, die den
> 4-bit-SD-Anschluss überhaupt möglich macht. Bedarf heute: **67 Positionen** —
> die Mux-Kapazität ist damit nicht mehr die knappe Ressource, die sie 2026-08-08
> war.

```
Mux-Kanäle = rohe ADC-Pins × Kanäle pro Chip

  4 × 74HC4051  (8:1)  = 32 Kanäle,  3 Adress-GPIO (geteilt)
  4 × CD74HC4067 (16:1) = 64 Kanäle, 4 Adress-GPIO (geteilt)
  8 × CD74HC4067 (16:1) = 128 Kanäle, 0 GPIO (Adressen über die 595-Kette)
```

`InitMux()` in `lib/libDaisy/src/per/adc.h` treibt die Adressleitungen selbst
und scannt per DMA. Der Mux-Scan kostet damit in der Audioschleife praktisch
nichts — die Warnung im Plan vor blockierendem Scan gilt `src/hw/sr_165.h`,
dem Schieberegister für Taster, nicht dem analogen Mux.

### libDaisy kann diesen Mux-Entwurf nicht — das ist ungeplante Firmware-Arbeit

Zwei Befunde, beide aus der Bibliothek gelesen (2026-08-16), und der zweite ist
der teure.

**Erstens: die Stock-Init belegt alle zwölf Kanäle.** `DaisyPatchSM::Init()`
legt sie mit `InitSingle` an (`daisy_patch_sm.cpp:318-322`) und konditioniert
die ersten acht mit `InitBipolarCv`, die vier rohen mit dem einfachen `Init`
(`:324-331`). `InitSingle` und `InitMux` schließen sich pro Kanal aus — die
Mux-Kette auf ADC_9..12 bekommt man also nicht dazu, indem man die Stock-Init
laufen lässt und danach ergänzt.

**Zweitens, und das wiegt schwerer: `InitMux` kann nur 8:1, und nur an GPIOs.**
`AdcChannelConfig::InitMux` nimmt drei Adresspins (`MUX_SEL_LAST == 3`,
`adc.h:34-40`) und klemmt die Kanalzahl hart:
`mux_channels_ = mux_channels < 8 ? mux_channels : 8` (`adc.cpp:183`). Die
Adressleitungen werden als `GPIO::Mode::OUTPUT` initialisiert und von libDaisy
selbst getrieben (`adc.cpp:185-191`). **Der Entwurf dieses Dokuments ist damit
mit der Bibliothek, wie sie ist, nicht baubar:**

- 4 Sense-Pins × 8 Kanäle = **32 Kanäle gegen 65 Bedarf** — Faktor zwei zu wenig.
- Die drei Adress-GPIOs müssten aus dem Pool kommen, und der ist nach dem
  SD-Slot leer (GPIO-Bilanz unten).

Was daraus folgt, ist keine Absage an die Topologie — die ist elektrisch
richtig und der Grund, warum der SD-Slot überhaupt passt. Es folgt daraus, dass
**der ADC-Scan für 16:1 mit Adressen aus der 595-Kette selbst geschrieben
werden muss**. Der DMA-Scan der `AdcHandle` bleibt nutzbar; was fehlt, ist die
Kanalumschaltung. Diese Arbeit ist in keiner Spec und in keinem Plan
veranschlagt und gehört ins Bring-up (M6, Schritt 2).

**Die acht CV-Kanäle sind von beidem nicht betroffen** und bleiben, wie
libDaisy sie liefert.

### Kollision, die man kennen muss: SPI2 gegen ADC 11/12

**`D8`/`D9` sind gleichzeitig SPI2 MISO/MOSI und ADC 11/12.** Wer den SPI-Bus
benutzt — etwa für einen `MAX11300` —, verliert damit **die Hälfte der rohen
ADC-Pins** und kommt auf die halbe Mux-Kapazität. Das ist keine Kleinigkeit,
sondern genau die Stelle, an der sich die zwei I/O-Strategien gegenseitig im
Weg stehen. Die Envelope-Spec hat daraus eine Regel gemacht: **SPI2 ist tabu**,
solange nichts Zwingendes auftaucht.

### Die GPIO-Bilanz, vollständig

Pool: B7, B8, D1, D10, D2–D7 = **10**.

| Verbraucher | Pins |
|---|---:|
| SDMMC 4-bit (D2–D7) — Pflicht, siehe §5 | 6 |
| 595/165-Ketten, gebitbangt (Daten-Out, Takt, Latch/Load, Daten-In) | 4 |
| **Rest** | **0** |

**Die Pin-Reserve ist null, und das ist Absicht:** die Reserve steckt in den
Ketten, nicht in den Pins. Jede Erweiterung — mehr LEDs, mehr Taster, mehr Muxe
— kostet ein weiteres Schieberegister an der vorhandenen Kette und **keinen**
GPIO. Die 20-%-Reserve-Regel des Phase-0-Plans gilt hier auf Ketten-Ebene.

**Die Reihenfolge ist wichtig, weil sie oft rückwärts erzählt wird:** der
SD-Slot wurde nicht dazugenommen und dann gehofft, dass es reicht. Die
Adressleitungen sind auf die 595-Kette gewandert, **damit** die sechs
SDMMC-Pins frei bleiben. Hätten die Muxe ihre Adressen an GPIOs, gäbe es
keinen 4-bit-SD-Anschluss.

### Reicht es? Die Bilanz über alle vier Ressourcen

Bedarf ist **65 Mux-Kanäle**, nicht 67: die zwei `REC`-Pads hängen an der
Taster-Kette, und `STAGES` teilt sich seinen Poti mit `ATTACK` — ein Poti, ein
Kanal.

| Ressource | Kapazität | Bedarf | Rest |
|---|---:|---:|---:|
| Sense-Pins (ADC, **nicht** aus dem GPIO-Pool) | 4 | 4 | 0 |
| GPIO-Pool | 10 | 10 | **0** |
| Mux-Kanäle (5 × 16:1 auf 4 Sense-Pins) | 80 | 65 | 15 |
| 595-Ausgänge (4 Register) | 32 | 21 LEDs + 4 Adressen + 5 Enables = 30 | 2 |
| 165-Eingänge | 24 | 4 Taster | 20 |

Die 595-Zeile ist hier **hergeleitet, nicht aus der Spec zitiert**: die
LED-Feedback-Spec bemisst 21 LEDs, 4 Adress- und 5 Enable-Leitungen auf
derselben Kette — 30 von 32 Ausgängen (`2026-08-16-led-feedback-design.md`
§3/§9). Die Rechnung oben nimmt einen Enable je Mux an; mit einem Dekoder
wären es weniger. Bei 16:1-Muxen ist das vierte Register damit **nicht mehr
optional**, sondern gebraucht — die 3-Register-Rechnung von vor der
LED-Feedback-Runde (10 LEDs, 19 von 24 Ausgängen) ist überholt. Fällt die
Mux-Wahl auf 8:1, braucht es neun Muxe statt fünf und damit neun statt fünf
Enables: 21 + 3 Adressen + 9 Enables = 33, ein Ausgang über die vier Register
hinaus — kostet ein **fünftes** 74HC595, kein Pin. Die 8:1-gegen-16:1-Frage
bleibt offen (§6).

**Das Ergebnis ist also: es reicht, mit Luft in jeder Zeile außer den Pins
selbst** — und die Null dort ist die geplante Null. Die zwei Dinge, die es
kippen könnten, sind kein Pin-Problem: die ungemessene Einschwingzeit pro
Kanal (§6) und der selbst zu schreibende Mux-Scan (oben).

### LED-Ausbau: was ein weiteres Register bringt

Gefragt am 2026-08-16, **bevor** die LED-Feedback-Runde lief: RATE, MOD, TIME
und LVL je Deck, dazu TIDE und PACE (TEMPO hatte schon eine LED). Gebaut wurde
am selben Tag mehr als das — **21 Lampen** statt der damals gezählten zehn (§3),
zwei davon frei geworden durch das Löschen der Capture-Anzeigen. Was hier steht,
ist deshalb keine Planung mehr, sondern die Rechnung für den *nächsten* Schritt:
was ein **fünftes** Register brächte.

| | heute (21 Lampen) | mit acht weiteren |
|---|---:|---:|
| 595-Ausgänge nötig (LEDs + 4 Adressen + 5 Enables) | 30 | **38** |
| Register à 8 Ausgänge | 4 (32) | **5 (40)** |
| GPIOs | 4 | **4** |

**Hardwareseitig ist jede Ausbaustufe ein Bauteil.** Ein weiteres 74HC595 in die
bestehende Kette, dazu die LEDs mit Vorwiderständen — **keine GPIOs**, genau
dafür ist die Kette da. Das vierte Register ist dabei kein Ausbau mehr, sondern
Bestand: 30 der 32 Ausgänge sind belegt (§3). Beim Verteilen nicht alle auf ein
Register hängen (Paket-Gesamtstrom);
das ist eine Datenblattfrage beim Bestellen, keine Architekturfrage. Jedes
weitere Register sind wieder 8 LEDs, weiterhin ohne einen einzigen Pin — die
praktische Grenze ist die Kettenlänge und der Strom, nicht das Board.

**CPU-seitig kostet An/Aus nichts.** Eine Schieberegisterkette wird immer ganz
geschrieben, gleich wie viele ihrer Bits LEDs sind; von 32 auf 40 Bit sind acht
zusätzliche Schiebetakte pro Schreibvorgang. Und weil die Mux-Adressen auf
derselben Kette liegen, **wird die Kette ohnehin bei jedem Adressschritt neu
geschrieben** — die LED-Daten fahren mit.

**Daraus folgt, dass auch Helligkeit fast umsonst ist:** der Mux-Scan schreibt
die Kette 16× pro Sweep, einmal je Adresse. Variiert man über diese 16
Schreibvorgänge, welche LED-Bits gesetzt sind, ergibt das **16-stufiges PWM ohne
einen einzigen zusätzlichen Schreibvorgang** — Auflösung = Adressschritte,
Bildwiederholrate = Sweep-Rate. Die LED-Feedback-Runde hat genau darauf gebaut:
`spkyled::duty()` nimmt die Stufenzahl als Parameter, weil die 8:1-gegen-16:1-Wahl
(§6) sie bestimmt, und der Rack-Host quantisiert auf dieselben 16 Stufen, damit
er nicht feiner atmet als das Panel je kann.

**Die Grenze, und sie ist scharf:** wer *feinere* Helligkeit als 16 Stufen will,
muss das PWM vom Scan entkoppeln, und dann sind es echte zusätzliche
Kettenschreibvorgänge pro Block. Das kostet CPU und zahlt auf den Block-Artefakt
ein (§6) — 32 → 40 Bit ist marginal, eine eigene PWM-Schleife wäre es nicht.

**Alles in diesem Abschnitt ist hergeleitet, nicht gemessen.** Zu messen ist die
Kettenschreibzeit bei 40 gegen 32 Bit auf dem Board.

**Engine-seitig war doch etwas zu bauen.** Der Absatz, der hier stand, hielt
`lane_output`, `target_value`, `gate` und `pitch_gate` für ausreichend. Das war
falsch: `target_value` ist Knopf **plus** Modulation und hätte damit die
Knopfstellung angezeigt — genau das, was die Anzeige nicht zeigen soll. Die
LED-Feedback-Runde hat zwei const-Beobachter ergänzt:
`lane_excursion(part, lane)` für den Modulationsanteil allein
(`engine/instrument.h:422`, aus `Part::target_raw` herausgezogen, damit Lampe und
Audiopfad denselben Ausdruck lesen) und `limiter_squash()` für den Master-Former
(`:306`). Die Zuordnung auf Lampen liegt in `host/vcv/src/led_law.hpp`, ist
Rack-frei und damit für die Firmware wiederverwendbar — wo die Datei am Ende
liegt, entscheidet die M6-Inbetriebnahme.

**Die Instrumentenfrage ist entschieden.** Eine Lampe zeigt die
**Lane-Bewegung**, nicht die Knopfstellung — die sieht man am Knopf. Und sie
zeigt sie über **Helligkeit**, nicht als Blitz pro Zyklus: eine Hüllkurve setzt
die Obergrenze, die Momentanauslenkung atmet darin, und dunkel heißt genau eine
Sache, nämlich dass hier nichts moduliert. Siehe
[`2026-08-16-led-feedback-design.md`](../superpowers/specs/2026-08-16-led-feedback-design.md)
§1/§10 und [`docs/roadmap.md`](../roadmap.md).

## 4. Die geometrische Kapazität

**Diese Frage ist beantwortet, und zwar durch Zeichnen statt durch Rechnen.**
Der Abschnitt stand ursprünglich hier, um für 42 HP eine Obergrenze aus einem
Rasterüberschlag zu gewinnen (98 Plätze bei 15 mm, 45 bei 22 mm). Das Panel ist
inzwischen gezeichnet, generiert und gegen seine eigenen Keep-outs geprüft:

- Plattenmaß **304,8 × 128,5 mm** (60 HP, 3 HE) — `HP = 60`,
  `W = HP * MM_PER_HP`, `Hh = 128.5` in `gen_hw_panel.py`.
- Bedienelemente laufen von **y = 14,5 mm** (obere Reihe) bis **y = 97 mm**, auf
  neun Linien (14,5 / 34 / 50,22 / 53 / 76 / 79 / 89,86 / 95 / 97); waagerecht
  von x = 17 bis x = 287,8. Darunter die Buchsenreihe bei **y = 114 mm**, in der
  auch die zwei reservierten Pads und der SD-Slot sitzen.
- Untergebracht sind **67 Poti-/Taster-Positionen, 2 reservierte Pads, 21 LEDs,
  18 Buchsen und der SD-Slot** — geprüft von `hw_panel_guard`: Schienen-Keepout,
  Überlappung nach echten Bauteilradien, ein Beschriftungsabstand für alle
  Klassen, Legenden gegen den 8,03-mm-Rack-Jack-Körper, SD-Ausschnitt frei,
  Spiegelsymmetrie — und dass die eingecheckten Dateien dem entsprechen, was der
  Generator heute erzeugt.

Der Rasterüberschlag von 2026-08-08 ist damit historisch und wird hier nicht auf
60 HP hochgerechnet — er würde eine Zahl liefern, die weniger wert ist als die
gezeichnete Platte.

## 5. Die Buchsen

**18 Buchsen: 12 Eingänge, 6 Ausgänge** (`gen_hw_panel.py`, gemessen
2026-08-16). Die acht MOD-CV-Eingänge existieren nur auf dem Hardware-Panel
(Spec 2026-08-10 §4); im VCV-Modul sind es vier Eingänge.

| Buchse | Art | liegt auf | Anmerkung |
|---|---|---|---|
| `IN_L`, `IN_R` | Audio in | B4/B3 | auf dem Submodule vorhanden |
| `CLOCK`, `RESET` | Gate/Trigger in | B10/B9 (Gate In 1/2) | `GateIn`, **digital** — Gates und Trigger, keine analoge Clock-Spannung |
| `MOD1..4_A`, `MOD1..4_B` | CV in | `CV_1..8` (C2–C9) | bipolar konditioniert (`InitBipolarCv`: ±5 V, invertiert, 2 ms Slew) — als Poti-Sense verworfen, **hier genau richtig** |
| `OUT_L`, `OUT_R` | Audio out | B2/B1 | auf dem Submodule vorhanden |
| `GATE_A`, `GATE_B` | Gate out | B5/B6 (Gate Out 1/2) | |
| `PITCH_A`, `PITCH_B` | CV out | C1/C10 (CV Out 1/2) | **hier klemmt es** |

Audio-I/O und die CV-Wandler sind auf dem Submodule bereits vorhanden — das ist
der Grund, dieses Modul zu nehmen, und es muss in der Rechnung auftauchen. **Das
ganze Buchsenfeld bildet 1:1 auf Modulpins ab, ohne eine einzige externe
Wandlerstufe.**

**Die CV-Eingänge laufen auf Blockrate, nicht auf Audiorate.** libDaisy setzt
`callback_rate_ = AudioSampleRate() / AudioBlockSize()` und aktualisiert die
`AnalogControl`-Objekte damit. **Achtung, gemessen statt angenommen:** die
Config im Quelltext trägt `blocksize = 48` (`daisy_patch_sm.cpp:294`), das Board
hat auf Nachfrage aber **96** gemeldet — bei 48 kHz also **500 Hz**
Aktualisierungsrate. Genau diese Annahme ist am 2026-08-08 einmal schiefgegangen
(aus Phasendauern auf 48 Samples geschlossen); seitdem fragt `shell/main.cpp`
die Blockgröße ab, statt sie zu setzen.

**Die Lücke, unverändert seit 2026-08-08 — und sie ist nicht die, für die man
sie beim Überfliegen hält.** Zwei Pitch- **und** zwei Gate-Ausgänge gehen: die
Gates sind GPIOs auf B5/B6 (`gate_out_1.Init(B5, GPIO::Mode::OUTPUT)`), die
zwei CVs hängen an einem 12-bit-DAC im DMA-Betrieb bei 48 kHz, der den zuletzt
geschriebenen Wert hält. Es ist exakt das Budget, und danach ist es leer. Zwei
Einschränkungen, beide aus `daisy_patch_sm.cpp` gelesen, nicht geschätzt:

- **0–5 V, hart geklemmt.** `VoltageToCode` rechnet `input * 819` und klemmt auf
  0..4095, Vollausschlag also exakt 5,000 V. Bei 1 V/Oktave sind das **fünf
  Oktaven und keine negative Spannung** — was darunter liegt, klebt auf 0 statt
  überzulaufen.
- **Kein dritter CV-Ausgang.** Eine Lane als CV, Velocity oder der STEP-Accent
  als Spannung, ein Clock-Out: dafür ist nichts mehr da.

Die Auflösung ist dabei *nicht* das Problem: 819 Codes pro Volt sind 1,22 mV
pro Schritt, rund **1,47 Cent**. Entscheidung der Envelope-Spec: dabei bleiben,
dokumentiert. Der Ausweg (externer DAC, `MAX11300`) kostet SPI2 und damit zwei
Sense-Pins, also die halbe Poti-Kapazität.

**SD-Slot, 4-bit, nicht 1-bit.** Die Sampler-Engine lädt User-Samples von SD
(`src/hw/card.h`), und der Daisy-Bootloader flasht Firmware per Drag & Drop von
der Karte — **aber nur bei voll verdrahtetem 4-bit-Bus**. Für ein Gerät ohne
SWD-Pins ist das der zweite Update-Pfad neben USB-DFU und allein den Mehraufwand
wert. Auf dem Panel: 11 × 6 mm bei x = 152,4 mm in der Buchsenreihe,
frontzugänglich.

## 6. Was hier bewusst NICHT steht

Die Liste von 2026-08-08 nannte an erster Stelle die Einstufung der Parameter.
**Die ist entschieden und steht in §2.** Offen ist heute:

- **Die Einschwingzeit pro Mux-Kanal.** Task 6 Schritt 5b des Phase-0-Plans.
  Ohne sie ist keine Panelgröße gegen das Audio-Budget gegengerechnet — das ist
  die einzige *Zahl* in diesem Dokument, an der die 67 Positionen noch scheitern
  könnten.
- **Der Mux-Scan selbst.** libDaisys `InitMux` kann 8:1 an GPIOs, gebraucht wird
  16:1 mit Adressen aus der 595-Kette (§3). Die Umschaltung muss geschrieben
  werden, sie steht in keinem Plan, und niemand hat sie in CPU-Zeit veranschlagt.
- **Die Wahl 8:1 gegen 16:1.** Hängt an Verfügbarkeit und Bestückungspreis.

### Ist das eine Zeit- oder eine Machbarkeitsfrage?

Gestellt am 2026-08-16, weil die zwei Posten oben zusammen nach mehr klingen
als nach Fleißarbeit. **Antwort: überwiegend Zeit, aber nicht ausschließlich.**

**Wofür die Machbarkeit spricht:** 16:1-Muxe an einem ADC sind das
Standardverfahren jedes Hardware-Synths. Der Entwurf ist elektrisch richtig,
die Kapazität passt mit Reserve in jeder Zeile (§3), und der DMA-Scan der
`AdcHandle` bleibt nutzbar — es fehlt die Kanalumschaltung, nicht das Konzept.

**Drei Stellen, an denen es trotzdem klemmen kann, nach Ernst sortiert:**

1. **Das CPU-Budget — die reale Gefahr.** Der Gate `instrument_worst_bbd_dtcm`
   steht bei 96,43 % `pct_max`, also 3,57 Punkte Reserve — gemessen auf dem
   **Seed**. Auf dem Submodule ist jede Last teurer; dort bleiben **2,17
   Punkte** (`docs/bench/2026-08-07-seed-vs-patch-sm.md`). Ein pro Block
   gebitbangter Ketten-Write plus Kanalauswertung landet genau in dieser
   Reserve. Beherrschbar — ein voller Sweep muss nicht jeden Block laufen, die
   Potis sind auch bei einem Sweep alle paar Blöcke schnell genug —, aber nicht
   gratis, und **ungemessen**. Wenn etwas den Plan kippt, dann das.
2. **Der ungeklärte Block-Artefakt.** Auf dem Board liegt ein Störton auf der
   Blockrate, 28 dB über dem Desktop-Referenzpunkt. Zwei Messungen vom
   2026-08-08 sagen dazu: den Callback zwingen, nur Nullen zu schreiben, ändert
   den Pegel **gar nicht** (−59,5 dBFS in beiden Fällen); die Leerlaufzeit im
   Block mit einer `nop`-Schleife füllen, senkt ihn um **7,5 dB**. Der Störer
   nimmt also nicht den Signalweg, sondern hängt daran, **wie Rechenaktivität im
   Block verteilt ist** — und die Ursache ist bis heute nicht benannt. Eine
   periodische GPIO-Salve pro Block ist genau eine neue Quelle dieser Sorte.
   Das ist keine Spekulation über einen Zusammenhang, sondern die Fortschreibung
   einer vorliegenden Messung; ob sie eintritt, ist offen.
3. **Die Einschwingzeit pro Kanal.** Sie entscheidet, wie viele Adressschritte
   pro Block gehen — nicht, ob es überhaupt geht. Der harmloseste der drei.

**Der Rückfallweg, falls 1 oder 2 zuschlagen** (Vorschlag, keine Entscheidung,
nirgends spezifiziert): ein Co-Controller auf der Control-PCB, der Potis und
Taster selbst scannt und die Werte seriell schickt. Dann braucht das Audio-MCU
**gar keine Sense-Pins mehr** — weder die vier rohen noch die Mux-Kette —, der
Scan-Aufwand verschwindet vollständig aus dem Audio-Budget, und A2/A3 werden als
UART frei, was sie von Haus aus sind. Kostet ein Bauteil und ein zweites
Firmware-Projekt.

**Fazit für die Planung:** die Machbarkeit steht nicht zur Debatte, es gibt
zwei Wege und der Rückfallweg ist der sauberere. Zur Debatte steht der Aufwand
und ob die dünnste Ressource des Projekts — die CPU-Reserve — ihn mitträgt.
**Das ist messbar, sobald ein Board mit Panel dranhängt, und vorher nicht.**
- **Ob ein `MAX11300` das Buchsenfeld übernimmt.** Der Kandidat ist real (ein
  Modul ist vorhanden und verdrahtet), aber er kostet SPI2 und damit zwei rohe
  ADC-Pins, und seine CPU-Kosten sind ungemessen.
- **Die exakte Pin-Map.** Task-2-Deliverable von Phase 0; die Envelope-Spec
  fixiert die Topologie, nicht die Pinnummern.
- **Das Plattenwort für BEND** (§2, letzter Absatz).

Nicht offen, sondern schlicht nicht begonnen: die Bring-up-Firmware selbst. Sie
hat keine Spec — siehe „M6 — Hardware prototype" in [`docs/roadmap.md`](../roadmap.md).
