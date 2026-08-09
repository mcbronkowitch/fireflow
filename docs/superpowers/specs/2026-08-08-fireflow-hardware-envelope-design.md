# FireFlow — Hardware-Envelope: 60 HP, volles Instrument

**Stand:** 8. August 2026
**Status:** beschlossen. Übersteuert die Hardware-Roadmap vom 7. August
(`2026-08-07-fireflow-hardware-roadmap-design.md`) in den unten genannten
Punkten; alles dort nicht Genannte gilt weiter. In Teilen übersteuert durch
`2026-08-09-hw-control-reduction-design.md` (§1: Zählbasis, Budget, „one in,
one out" — siehe dortiges §1 und die Korrekturen unten).

## Entscheidung

Die Hardware wird das **volle FireFlow** — beide Decks, jede Funktion mit
eigenem Bedienelement — auf **60 HP**. Der Macro-Weg (Glow als Hardware) ist
tot: das 18-HP-Experiment war zu random, das Tuning griff nicht. Glow bleibt
ein VCV-Modul. Damit entfallen ersatzlos:

- die **42-HP-Rahmenbedingung** und der Halbierungs-Absatz („82 auf 45–55
  reduzieren, Designarbeit unbekannter Größe") der Roadmap-Spec,
- die **Glow-Frage samt Frist 4. September**,
- die **LED-Kränze** (2×32 WS2812) — Begründung siehe §3.

Der 23. April 2027 wird von Termin zu **Zielkorridor**. Was an Zeitdisziplin
übrig bleibt, steht in §5.

Dieses Design wurde von zwei unabhängigen adversarialen Reviews geprüft
(Technik gegen libDaisy/gen_panel, Plan-Kohärenz gegen Roadmap/Memory); alle
CHALLENGED-Punkte sind eingearbeitet.

## §1 Zählbasis und Budget

Grundlage ist `RUNTIME_PANEL_PARAMS` aus `host/vcv/res/gen_panel.py`, seit der
Bedienelement-Reduktion (`2026-08-09-hw-control-reduction-design.md`, Tasks
1–10): **68 Runtime-Parameter auf 66 physischen Positionen**. **BEND
(`STAGES_A/B`) teilt sich seinen Knopf mit ATTACK** — das bleibt auf Hardware
so und ist eine bewusste Doppelbelegung mit Engine-abhängigem Moduswechsel,
keine Kollision. Die MULT-Knobs (`FLUXTIME_A/B`) existieren nicht mehr — FLUX
ist seit Task 6 ein einzelner gerasteter Knopf. Kanallisten werden aus
`RUNTIME_PANEL_PARAMS` generiert (`tools/count_panel_controls.py`), nie aus
`PANEL_PARAMS`.

| Klasse | Bedarf heute | Kapazität | landet auf |
|---|---|---|---|
| Potis (64 kontinuierlich: 17 groß, 47 klein) | 62 Positionen (2× Doppelbelegung: BEND teilt sich ATTACKs Knopf) | bis 128 Kanäle | 4067-Kette, 4 Sense-Pins (§2) |
| Taster | 4 (`ENGINE` ×2, `REC` ×2) | 24 | 74HC165-Kette (`src/hw/sr_165.h`) |
| Status-LEDs | ~20 (siehe LED-Tabelle unten) | 24 | 3× 74HC595 |
| Buchsen | 10 | 12 | Main-PCB |
| SD-Slot | 1 | 1 | SDMMC 4-bit, Main-PCB, frontzugänglich |

**Encoder: gestrichen** — als Entscheidung, nicht als Auslassung. `DETUNE_A/B`
ist zurück auf dem Panel (Task 10, `PLAY`-Reihe, freigewordener `STEP`-Slot);
`DRIVE_A/B` ist ersatzlos gelöscht (BBD-Drive kommt aus `bbd_engine.cpp`, nie
aus einem Panel-Wert — Task 9). Die frühere `HIDDEN_PARAMS`-Frage („ALT-Layer
oder feste Defaults, Entscheidung fällt in der Neugruppierungsrunde") ist
damit erledigt: `HIDDEN_PARAMS` ist leer, d.h. es gibt keine menü-only
*Parameter* mehr. Das Kontextmenü selbst bleibt (Resync to bar, BBD Freeze
Attack, Sampler-Untermenüs, Excitation-Flags, Copy terrain code).

**LED-Belegung (gehört hierher, nicht in H1):** pro Part Gate, REC, FLOW/STEP,
Capture-Replay (= 8; die GRIT-Modus-LED entfällt — GRIT ist seit Task 4 ein
bipolarer Knopf und zeigt Crush/Sat/Silence selbst, keine separate LED
nötig), Engine-Anzeige (1 LED + Blinkcode pro Part oder je 5 diskret = 2–10),
global Tempo/Sync (1–2). Macht 11–20. Zwei
Anzeigen der alten Kränze sterben dabei: **Capture-Playhead** und
**Sampler-Lesekopf**. Ob sie eine Minimal-Form bekommen (je 1 Aktivitäts-LED)
oder ersatzlos fallen, entscheidet die Neugruppierung — so oder so wird es
hier als bewusste Streichung nachgetragen, nicht stillschweigend.

**Budget-Regel: „one in, one out" ist tot.** Die Bedienelement-Reduktion
(`2026-08-09-hw-control-reduction-design.md`) hat das Inventar von 82 auf 68
Parameter gebracht — das Panel hat jetzt **14 Positionen Luft** gegenüber dem
Stand, für den §4 ursprünglich gerechnet wurde. Die Neugruppierung (§4) darf
aus diesem Polster schöpfen; sie muss nicht mehr Eins-für-eins tauschen.

## §2 Elektrik

**Zwei Boards, unverändert aus der Roadmap:** Main-PCB (Patch SM, Power,
Audio-I/O, CV-Konditionierung, alle Buchsen, SD-Slot) + Control-PCB (Potis,
Taster, LEDs, Muxe, Schieberegister), Stiftleisten/Steckverbinder dazwischen.
Bei ~300 mm Board-Länge: **ein** Steckverbinder plus mechanische Standoffs
(zwei weit auseinanderliegende parallele Leisten klemmen bei normaler
Fertigungstoleranz); Stützpunkte gegen Durchbiegung in Boardmitte.

**Die zentrale Korrektur — es gibt nur 4 Mux-Sense-Pins.** Die acht
CV-Eingänge des Patch SM (`CV_1..8`) sind hardwareseitig **bipolar** (±5 V)
konditioniert (`daisy_patch_sm.cpp`, `InitBipolarCv`) und als Poti-Sense
untauglich. Roh nutzbar sind nur **ADC_9..12 = A2, A3, D9, D8**
(`docs/hardware/io-budget.md` §3).

**Topologie:** Adress- **und Enable-Leitungen der 74HC4067 wandern auf die
595-Kette.** Der 4067 hat einen Enable-Pin; mit Adressen+Enables aus den
Schieberegistern teilen sich bis zu acht Muxe die vier Sense-Pins (Kapazität
128 Kanäle) und der Adressbus kostet **null GPIOs**. Adresswechsel über die
Kette ist bei µs-Settle-Zeiten unkritisch. Genau diese Einsparung macht den
4-bit-SD-Anschluss möglich.

**GPIO-Bilanz** (Pool: B7, B8, D1, D10, D2–D7 = 10):

- SDMMC 4-bit: D2–D7 (6) — **Pflicht**, siehe SD-Absatz
- 595/165-Ketten gebit-bangt auf B7, B8, D1, D10 (Takt geteilt; 4 Leitungen:
  Daten-Out, Takt, Latch/Load, Daten-In)
- **SPI2 ist tabu** — es liegt auf D8/D9 und würde zwei Sense-Pins kosten
- Die Pin-Reserve ist damit **null** — bewusst: die Reserve steckt in den
  Ketten, nicht in Pins. Jede künftige Erweiterung (mehr LEDs, mehr Taster,
  mehr Muxe) kostet ein weiteres Schieberegister an der vorhandenen Kette
  und **keinen** GPIO. Die 20-%-Reserve-Regel des Phase-0-Plans gilt hier
  auf Ketten-Ebene, nicht auf Pin-Ebene.
- Die exakte Pin-Map ist das **Task-2-Deliverable** von Phase 0. Dieses Design
  fixiert die Topologie, nicht die Pinnummern.

**SD-Slot: 4-bit, nicht 1-bit.** Die Sampler-Engine lädt User-Samples von SD
(`src/hw/card.h` existiert); und der Daisy-Bootloader flasht Firmware per
Drag & Drop von der Karte — **aber nur bei voll verdrahtetem 4-bit-Bus**
(libDaisy-Doku). Für ein Gerät ohne SWD-Pins ist das der zweite
Update-Pfad neben USB-DFU und allein den Mehraufwand wert.

**CV-Outs, ehrlich notiert:** die zwei DAC-Ausgänge sind **0–5 V unipolar**
(`WriteCvOut`). Für PITCH_A/B reicht das eingeschränkt (keine negativen
Oktaven). Entscheidung: dabei bleiben, dokumentiert — Dev-Alpha, kein
Patch-Kompat-Versprechen. Der Ausweg (MAX11300) würde SPI2 und damit zwei
Sense-Pins kosten und ist verworfen, solange nichts Zwingendes auftaucht.

## §3 LED-Kränze: gestrichen, mit der richtigen Begründung

Die Streichung kauft **nicht** CPU — der vorhandene WS2812-Treiber ist
DMA-getrieben und praktisch gratis. Sie kauft:

1. **Strom:** 64 WS2812 waren der größte +5-V-Verbraucher (realistisch
   gedimmt hunderte mA, theoretisch ~3,8 A); 24 LEDs an 595ern sind ~50–80 mA.
2. **BOM und Layout:** kein 5-V-Pegel-/Timing-Pfad, 595er sind
   JLC-Standardware.
3. **Den libDaisy-Fork:** der WS2812-Treiber brauchte ein modifiziertes
   libDaisy (`ws2812.h`, `docs/upstream-firmware.md`). Diese Abhängigkeit
   entfällt komplett.

**CPU-Vorbehalt (gilt für das ganze Design):** Die gemessene Reserve auf dem
Patch SM beträgt **2,17 Punkte** (`docs/bench/2026-08-07-seed-vs-patch-sm.md`
— nicht die 3,57 Seed-Punkte der Roadmap-Spec; die Seed-Zahlen übertragen
nachweislich nicht). Der Shell-Aufschlag (Mux-Scan, Ketten-I/O, UI) ist
ungemessen. **Alle Kapazitäten gelten vorbehaltlich Task 6 Schritt 8** des
Phase-0-Plans. Fallback bei CPU-Not: **Engine-/Voice-Begrenzung** (ein
Voice-Cut kauft gemessene ~7,9 Punkte) — nie Panel-Änderung.

## §4 Geometrie und Layout-Prozess

**60 HP fest** (304,8 mm × 3U). Nutzhöhe ehrlich: **~105–112 mm** (Rails und
Schrauben fressen oben/unten je ~8 mm; „120 mm nutzbar" existiert in 3U
nicht). Zweistufiges Raster: große Knobs ~22 mm, kleine (9-mm-Potis,
Mini-Kappen) ~15 mm. Reihenrechnung am unteren Inventarrand: 2 Reihen große
(44 mm) + 3 Reihen kleine (45 mm, Taster und LEDs in freie Plätze
eingefaltet) + Buchsenreihe (15 mm) = 104 mm. Das passt — knapp, und es wird
ein **dichtes** Panel (WMD-Performance-Mixer-Klasse, keine Luft-Ästhetik).
56 HP ist rechnerisch widerlegt und kein Ziel mehr.

**Die Neugruppierung** (heutige Gruppierung ist historisch gewachsen;
FX/VOICE stimmig, Orbit-Knobs nicht) läuft als definierter Prozess **vor H1**:

1. `gen_panel.py` bekommt einen **Hardware-Modus**, der von Runde 1 an
   erzwingt: **statische Beschriftung** (keine engine-abhängigen Captions —
   ein Alupanel ist gedruckt; die dynamischen Captions des VCV-Panels sind
   auf Blech ungelöst und dürfen den Freeze nicht kontaminieren), **echte
   Poti-Sperrflächen** statt der VCV-Widget-Radien (`GLYPH_R` beschreibt
   Bildschirm-Knobs; `test_no_overlap` prüft erst mit realen Footprints etwas
   Hardware-Relevantes — die ATK/BEND-Ausnahme bleibt als dokumentierte
   Doppelbelegung), **60-HP-Rahmen mit Rail-Sperrzonen** (nicht die 2-mm-
   VCV-Ränder).
2. Jede Iteration wird als VCV-Panel gebaut und in Rack **gespielt** — exakt
   die Hardware-Modus-Version.
3. **Deckel: maximal 3 Runden oder ein Kalenderdatum** (bei Prozessstart
   festlegen), was zuerst kommt. Maus-Sessions testen Gruppierungslogik,
   nicht Griffweiten — mehr Runden sind Prokrastination mit Werkzeugcharakter.
4. **H1 (Acrylpanel, echte Potis) darf den Freeze einmal brechen** — eine
   budgetierte Revision nach erster Fingerberührung ist eingeplant, keine
   Planabweichung.

## §5 Roadmap-Konsequenzen

- **Der Testcoupon entkoppelt vom Panel-Freeze.** Er testet nichts
  Gruppierungsabhängiges. Sein Termin koppelt an den Abschluss der
  Pin-Rechnung (Task 2), Zielgröße wie gehabt ~Mitte September. Nur Rev A/B
  datieren nach dem Freeze.
- **Coupon-Inhalt (aktualisiert):** 4067-Kette mit Enable-Schema über 595er,
  165er-Tasterkette, ADC-Rauschen neben dem Audiopfad, **Settle-Zeit-Messung
  (Plan Schritt 5b, Pflicht — der 10-nF-COM-Kondensator aus dem Plan ist zu
  groß und fliegt; ≤1 nF oder weg)**, Footprint-Verifikation, und ein
  **Audio-Ausgang mit Messpunkt bei laufendem Scan**: der 28-dB-Störton vom
  8. August wird auf dem Coupon gejagt, mit Analog/Digital-
  Versorgungstrennung als expliziter Entwurfsregel
  (Messstrecke: `fireflow-audio-measurement-rig`).
- **Neuer Notausgang** (die alte Achse „42→34 HP, zwei Lanes weniger" ist mit
  dem vollen Instrument tot): (a) Rev A als akzeptierter Superbooth-Stand,
  Rev B danach; (b) Acryl- statt Alu-Panel als finaler Stand; (c)
  Engine-Begrenzung, falls die CPU es erzwingt. Entscheidungsdatum: an H2.
- **Kosten-Realität:** Gesamtprojekt grob 900–1.500 €. Nicht vergessen:
  ~80 Knopfkappen kosten so viel wie die Potis (40–160 €); JLC bestückt nur
  SMT — ~110 THT-Bauteile pro Board-Revision sind Handarbeit, Regel: **Potis
  lose stecken, Panel aufschrauben, dann verlöten**; die
  Zwei-Board-Nachbestellzahlen der Roadmap (~40/150 €) skalieren auf 60 HP
  eher zu ~100/300 € — das Argument hält, die Zahlen nicht.

**Nachzuziehende Dokumente** (sonst schlägt eine spätere Session die
Halbierung wieder vor):

| Dokument | Änderung |
|---|---|
| `docs/roadmap.md` (M6 Schritt 1) | „defensible reduction" → Neugruppierung bei vollem Satz, Verweis hierher |
| Phase-0-Plan Task 2 | „Reduktionsentscheidung" → reine Pin-Budget-Rechnung gegen §1/§2 |
| Phase-0-Plan Schritt 5 | 10-nF-Empfehlung korrigieren (§5) |
| Memory `spotykach-hardware-constraint` | Ziel-Panel ist definiert: 60 HP, voller Satz; Reduzierbarkeits-Regel erfüllt sich ab jetzt trivial |
| Roadmap-Spec 07.08. | Kopfvermerk „in Teilen übersteuert durch dieses Dokument" |
