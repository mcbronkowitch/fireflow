# FireFlow — HW-Panel: Neugruppierung, Knopfgrößen, CV-Eingänge

**Stand:** 10. August 2026
**Status:** beschlossen. Dies ist die **Neugruppierungsrunde**, die
`2026-08-08-fireflow-hardware-envelope-design.md` §4 als Prozess vor H1
angekündigt hat. Übersteuert die Envelope-Spec in den unten genannten
Punkten (§1 Buchsenzahl und Zählbasis, §4 Rastermaße und Gruppierung); alles
dort nicht Genannte gilt weiter. Baut auf
`2026-08-09-hw-control-reduction-design.md` auf (68 Parameter) und erhöht
diese Zahl um zwei reservierte Taster.

Betrifft **ausschließlich** das Hardware-Panel (`host/vcv/res/gen_hw_panel.py`,
`res/FireflowHW.svg`, `src/generated_hw_panel.hpp`). Das VCV-Bildschirmpanel
(`gen_panel.py`, Modul `Fireflow`) und das Glow-Panel bleiben unangetastet.

## Entscheidung

Das Panel wird vollständig neu gruppiert. Drei Dinge fallen zusammen und
lassen sich deshalb nicht getrennt beschließen:

1. **Knopfgrößen werden neu vergeben** — nach Rang in der Gruppe, nicht nach
   Bildschirm-Widget-Typ.
2. **Die Gruppen werden neu geschnitten** — nach dem, was in der Engine
   zusammengehört, nicht nach der historisch gewachsenen Orbit-Aufteilung.
3. **Acht CV-Eingänge kommen aufs Panel** und bestimmen die Geometrie mit:
   jede CV-Buchse steht direkt unter dem großen Knopf, den sie steuert.

## §1 Größenklassen — entkoppelt vom VCV-Widget-Typ

Heute leitet `gen_hw_panel.py` die Sperrfläche aus `c.kind` ab
(`HW_R = {BIGKNOB: 8.0, KNOBC: 8.0, SMKNOB: 5.5, KNOBI: 5.5, …}`). Das ist
falsch: `KNOBC` heißt **bipolar**, `KNOBI` heißt **gerastert** — das sind
Verhaltensaussagen über das Bildschirm-Widget, keine Durchmesser. Ein
bipolares Poti mit Mittenrastung ist in jeder Baugröße lieferbar.

**Ab hier trägt jedes Bedienelement seine Hardware-Größe selbst**, in einer
expliziten Tabelle in `gen_hw_panel.py`. `kind` bleibt, was es ist, und
beschreibt weiterhin nur das VCV-Widget.

| Klasse | Sperrflächenradius | Anzahl |
|---|---|---|
| **groß** | 8,0 mm | 18 Positionen |
| **klein** | 5,5 mm | 44 Positionen (46 Parameter — BEND teilt ATTACKs Knopf) |
| Taster | 4,0 mm | 6 |
| Buchse | 4,0 mm | 18 |
| LED | 1,5 mm | 20 |
| SD-Slot | Rechteck 15,0 × 12,0 mm | 1 |

**Die 18 großen:** pro Deck **DENS, MOD, COLOR, FILT, TIMB, MIX, SEND, LVL**
(8 × 2 = 16), in der Mitte **MORPH** und **DECAY** (Reverb). Alles andere ist
klein.

Gegenüber heute wechseln pro Deck sieben Knöpfe die Klasse nach unten (RATE,
SHAPE, SMTH, RANGE, VARY, TUNE, GRIT) und fünf nach oben (FILT, TIMB, MIX,
SEND, LVL). Die Envelope-Spec §1 nennt „17 groß, 47 klein"; diese Zahl ist
damit ersetzt.

## §2 Gruppeninventar

Grundlage ist die Engine, nicht die bisherige Panelgrafik. Zwei Befunde
tragen den Schnitt:

- **RATE, SHAPE, DENS, SMTH, RANGE, VARY und MOD sind ein einziges Objekt** —
  `_parts[p].mod()` (`engine/instrument.h:46–51`), MOD ist dessen Tiefe
  (`set_depth`, `Fireflow.cpp:561`). Die Bildschirm-Sektoren
  MOTION / TIMBRE / PITCH zerschneiden einen Modulator in drei Teile. Genau
  das meint die Envelope-Spec §4 mit „Orbit-Knobs nicht stimmig".
- **COLOR ist Harmonie, nicht Klangfarbe** — `engine/pitch/chord.h:48`:
  0 = Einzelton, aufsteigend bis zum vierstimmigen diatonischen Akkord. COLOR
  gehört zu TUNE, nicht zu FILT.

### Pro Deck: 7 Gruppen, 27 Positionen (8 groß, 17 klein, 2 Taster)

| Gruppe | groß | klein | Begründung |
|---|---|---|---|
| **MOD** | MOD, DENS | RATE, SHAPE, SMTH, RANGE, VARY | ein Engine-Objekt; MOD = Tiefe, DENS = zweite Achse (und Sampler-Grain, `Fireflow.cpp:726`) |
| **PITCH** | COLOR | TUNE, DTUN | Akkordgröße und Stimmung |
| **VOICE** | FILT, TIMB | ATK (+BEND), DEC, RES, SUB | unverändert; die Envelope-Spec nennt VOICE ausdrücklich stimmig |
| **FLUX** | MIX | TIME, FB, LINK | die BBD-/Echo-Strecke |
| **ROOM** | SEND | — | der Weg des Decks in den geteilten Hall |
| **OUT** | LVL | GRIT | Dreck und Pegel, das Ende der Kette |
| **PLAY** | — | STPS, SONG + Taster ENG, REC | die einzige kopflose Gruppe |

### Mitte: 4 Gruppen, 14 Positionen (2 groß, 10 klein, 2 Taster)

| Gruppe | groß | klein |
|---|---|---|
| **BLEND** | MORPH | TIDE, CHOKE |
| **CLOCK** | — | TEMPO, FREE\|GRID, SHUFL |
| **TONALITY** | — | SCALE, DRIFT |
| **ROOM** | DECAY | SIZE, TONE, DIFF |
| **SYS** | — | Taster MOD, SHIFT (§5) |

SCALE liegt in der Mitte und nicht in PITCH, weil es **einmal** existiert
(gemeinsamer Tonvorrat beider Decks), während PITCH pro Deck existiert.
DRIFT steht daneben, weil es derselben Art ist: `engine/parts/part.h:97`
nennt es wörtlich *„DRIFT tune tap; engine pitch only"*.

CLOCK und TONALITY haben keinen großen Knopf, so wie PLAY keinen hat. Das ist
kein Versäumnis: es sind die Gruppen, die man einmal einstellt.

## §3 Geometrie

**Rahmen unverändert:** 60 HP = 304,8 mm × 128,5 mm. Sperrzonen `KEEP_TOP =
9,0`, `KEEP_BOT = 119,5`, seitlich 2,0 mm. Deck B ist Deck A gespiegelt:
**x_B = 304,8 − x_A**, y identisch — maschinell geprüft, unverändert.

### Zeilenrhythmus (y, mm)

| Zeile | y | Inhalt |
|---|---|---|
| Statusleiste | 15,0 | PLAY (STPS, SONG, ENG, REC) + das gesamte LED-Feld; in der Mitte MOD/SHIFT |
| Band 1, klein | 30,0 | MOD (5), FLUX (3) · Mitte: CLOCK (3) + TONALITY (2) |
| Band 1, groß | 46,0 | MOD, DENS, MIX, SEND · Mitte: TIDE, CHOKE (klein) |
| Band 2, klein | 66,0 | VOICE (4), PITCH (2), GRIT |
| Band 2, groß | 86,0 | **FILT, TIMB, COLOR, LVL** — die vier CV-Ziele |
| Buchsenzeile | 107,0 | 18 Buchsen |
| SD-Slot | 110,0 (Mitte) | 15 × 12 mm, mittig |

Die Mitte folgt einem eigenen Rhythmus unterhalb von Band 1: MORPH bei
y = 62, DECAY bei y = 79, ROOM-Kleine bei y = 94.

Der SD-Slot sitzt 3 mm tiefer als die Buchsenmitten. Das ist kein Zufall:
bei y = 107 läge die Beschriftung von TONE (y = 101,9) innerhalb des
Ausschnitts.

### Deck A — normative Koordinaten (mm)

| Gruppe | Klasse | Label | x | y |
|---|---|---|---|---|
| PLAY | klein | STPS | 14,00 | 15,00 |
| PLAY | klein | SONG | 26,50 | 15,00 |
| PLAY | Taster | ENG | 38,50 | 15,00 |
| PLAY | Taster | REC | 69,50 | 15,00 |
| MOD | klein | RATE | 14,00 | 30,00 |
| MOD | klein | SHAPE | 26,50 | 30,00 |
| MOD | klein | SMTH | 39,00 | 30,00 |
| MOD | klein | RANGE | 51,50 | 30,00 |
| MOD | klein | VARY | 64,00 | 30,00 |
| MOD | **groß** | MOD | 26,50 | 46,00 |
| MOD | **groß** | DENS | 51,50 | 46,00 |
| FLUX | klein | TIME | 76,00 | 30,00 |
| FLUX | klein | FB | 88,50 | 30,00 |
| FLUX | klein | LINK | 101,00 | 30,00 |
| FLUX | **groß** | MIX | 88,50 | 46,00 |
| ROOM | **groß** | SEND | 110,00 | 46,00 |
| VOICE | klein | ATK (+BEND) | 14,00 | 66,00 |
| VOICE | klein | DEC | 26,50 | 66,00 |
| VOICE | klein | RES | 39,00 | 66,00 |
| VOICE | klein | SUB | 51,50 | 66,00 |
| VOICE | **groß** | FILT | 23,00 | 86,00 |
| VOICE | **groß** | TIMB | 42,50 | 86,00 |
| PITCH | klein | TUNE | 70,00 | 66,00 |
| PITCH | klein | DTUN | 82,50 | 66,00 |
| PITCH | **groß** | COLOR | 76,25 | 86,00 |
| OUT | klein | GRIT | 100,00 | 66,00 |
| OUT | **groß** | LVL | 100,00 | 86,00 |

**LEDs Deck A**, alle y = 15,0: Engine-Anzeige bei x = 45,5 / 49,5 / 53,5 /
57,5 / 61,5 (fünf diskrete, direkt rechts vom ENG-Taster); Status bei
x = 77 / 81 / 85 / 89 — REC, GATE, FLOW/STEP, CAPTURE, direkt rechts vom
REC-Taster.

**Buchsen Deck A**, alle y = 107,0: CV→FILT bei x = 23,00, CV→TIMB bei
42,50, PITCH bei 54,00, GATE bei 65,00, CV→COLOR bei 76,25, CV→LVL bei
100,00.

### Mitte — normative Koordinaten (mm)

| Gruppe | Klasse | Label | x | y |
|---|---|---|---|---|
| SYS | Taster | MOD | 140,40 | 15,00 |
| SYS | Taster | SHIFT | 164,40 | 15,00 |
| SYS | LED | Tempo | 149,40 | 15,00 |
| SYS | LED | Sync | 155,40 | 15,00 |
| CLOCK | klein | TEMPO | 127,40 | 30,00 |
| CLOCK | klein | FREE\|GRID | 139,90 | 30,00 |
| CLOCK | klein | SHUFL | 152,40 | 30,00 |
| TONALITY | klein | SCALE | 164,90 | 30,00 |
| TONALITY | klein | DRIFT | 177,40 | 30,00 |
| BLEND | klein | TIDE | 140,40 | 46,00 |
| BLEND | klein | CHOKE | 164,40 | 46,00 |
| BLEND | **groß** | MORPH | 152,40 | 62,00 |
| ROOM | **groß** | DECAY | 152,40 | 79,00 |
| ROOM | klein | SIZE | 134,40 | 94,00 |
| ROOM | klein | TONE | 152,40 | 94,00 |
| ROOM | klein | DIFF | 170,40 | 94,00 |
| SYS | Buchse | IN L | 112,00 | 107,00 |
| SYS | Buchse | IN R | 126,00 | 107,00 |
| SYS | Buchse | CLOCK | 140,00 | 107,00 |
| SYS | Buchse | RESET | 164,80 | 107,00 |
| SYS | Buchse | OUT L | 178,80 | 107,00 |
| SYS | Buchse | OUT R | 192,80 | 107,00 |

Der SD-Ausschnitt liegt mittig bei (152,40 / 110,00), 15,0 × 12,0 mm.

### Was die Geometrie kostet

Das steht hier, weil es sonst später als Fehler gelesen wird:

- **VOICE, PITCH und OUT teilen sich Band 2, weil sie CV-Ziele sind** — nicht
  weil sie verwandt sind. Die Gruppierung ist innerhalb des Bandes intakt
  (Kopf unten, Trabanten darüber), aber die Nachbarschaft *zwischen* den drei
  Gruppen trägt keine Bedeutung.
- **SEND steht allein am inneren Rand von Band 1.** Ein einzelner großer Knopf
  ohne Trabanten sieht wichtiger aus, als er ist.
- **Der Transport liegt oben statt in Handhöhe.** Für STPS/SONG/ENG richtig
  (Einstellhandlungen), für REC diskutabel. Bewusst getauscht gegen ein
  zusammenhängendes, lesbares LED-Feld.

## §4 CV-Eingänge

**Acht CV-Eingänge, vier pro Deck, gespiegelt.** Jede Buchse steht in der
Buchsenzeile **direkt unter dem großen Knopf, den sie steuert**, und trägt
dessen Namen. Zwischen Knopfunterkante (y = 94) und Buchsenoberkante
(y = 103) liegen 9 mm freies Blech — die Zuordnung ist ohne Legende lesbar.

Die vier Ziele sind nicht gewählt, sondern abgelesen: `engine/mod/lane_id.h`
definiert fünf feste Ziel-Slots, die der interne Modulator bedient. Vier
davon haben auf diesem Panel einen großen Knopf.

| Buchse | Lane | Knopf |
|---|---|---|
| CV→FILT | `LANE_SIZE` (SIZE/FILTER) | FILT |
| CV→TIMB | `LANE_SOURCE` (POSITION/TIMBRE) | TIMB |
| CV→COLOR | `LANE_PITCH` (Master-Lane) | COLOR |
| CV→LVL | `LANE_LEVEL` | LVL |

**`LANE_MOTION` (Ziel SHAPE) bekommt keine Buchse** — SHAPE ist klein und
steht in Band 1, wo ein Kabel über die Spielfläche hinge. Bewusste Streichung.

**Elektrisch kostet das nichts außer den Buchsen.** Die Envelope-Spec §2 hat
`CV_1..8` des Patch SM ausdrücklich freigeräumt: sie sind hardwareseitig
bipolar (±5 V) konditioniert (`daisy_patch_sm.cpp`, `InitBipolarCv`) und
deshalb als Poti-Sense untauglich — als CV-Eingänge sind sie genau richtig
und bereits vorhanden. Acht Eingänge, acht Kanäle, kein zusätzlicher Baustein.

**Die Engine liest heute keinen CV-Eingang.** FireFlow hat aktuell überhaupt
keinen. Diese Spec reserviert Blech, Buchse und ADC-Kanal; die Anbindung
(Tiefe, Offset, Attenuverter-Verhalten, Zusammenspiel mit dem internen
Modulator auf derselben Lane) ist **nicht** Gegenstand dieser Runde und
braucht eine eigene.

## §5 Neue Bedienelemente ohne Funktion

**MOD und SHIFT**, zwei Taster in der Mitte der Statusleiste. Sie bekommen
Fußabdruck, Beschriftung und **je einen Kanal auf der vorhandenen
74HC165-Kette** (`src/hw/sr_165.h`; Bedarf steigt von 4 auf 6 von 24). Sie
haben **keine Engine-Bindung, keinen VCV-Parameter und kein Verhalten**. Das
ist der ganze Beschluss — reserviert, damit ein späterer Zweitbelegungs- oder
Modus-Layer nicht am Panel-Freeze scheitert.

**Das LED-Feld wächst von 4 auf 20.** Die Envelope-Spec §1 nennt einen
Korridor von 11–20 und lässt die Wahl offen. Diese Spec wählt das
**Maximum**, weil Reservieren gratis ist und Nachrüsten nach dem Freeze nicht:

- 4 pro Deck: GATE, REC, FLOW/STEP, CAPTURE (8)
- 5 pro Deck: Engine-Anzeige, diskret statt Blinkcode (10)
- 2 global: Tempo, Sync

20 LEDs an drei 74HC595 (Kapazität 24) — passt ohne Änderung an der Topologie.
Die diskrete Engine-Anzeige ersetzt die von der Envelope-Spec als Sparvariante
genannte „1 LED + Blinkcode"-Lösung.

**Der SD-Slot** war bereits beschlossen (Envelope-Spec §2, SDMMC 4-bit,
frontzugänglich) und fehlte nur im Panel. Er bekommt hier seine Position.

## §6 Was im Code passiert

Nur `host/vcv/res/gen_hw_panel.py`, `host/vcv/res/test_hw_panel.py`, die
beiden generierten Dateien und `host/vcv/src/Fireflow.cpp`. Kein
Engine-Code, kein `gen_panel.py`.

1. **`HW_R` und `LBL_DY_HW` werden nicht mehr über `c.kind` indiziert.** Eine
   explizite `HW_SIZE`-Tabelle bildet Parameter-Basisnamen auf `"G"`/`"S"` ab;
   Sperrfläche und Beschriftungsabstand folgen der Größenklasse.
2. **`DECK_POS` und `CENTER_POS` werden durch die Tabellen aus §3 ersetzt.**
   Die vier absichtlichen Löcher der Iteration 0 (bei (77, 71), (27, 86),
   (72, 86), (102, 86)) verschwinden ersatzlos.
3. **Neue Tabelle `HW_ONLY`** für alles, was es auf Blech, aber nicht im
   VCV-Modul gibt: 2 Taster, 8 CV-Buchsen, 16 zusätzliche LEDs, der
   SD-Ausschnitt. Sie darf **nicht** in `HW_PARAMS` laufen —
   `test_same_runtime_params_same_order` hält Enum-Menge und -Reihenfolge
   gegen `gp.RUNTIME_PANEL_PARAMS`, und dieser Vertrag bleibt. Der Header
   bekommt dafür eine eigene Tabelle (`kHwOnlyCtls`). Der SD-Ausschnitt
   bekommt **keine** C++-Tabelle: er ist eine SVG-Form, und Rack rendert
   Formen (nur Text nicht). Eine Tabelle ohne Verbraucher wäre toter Code.
4. **Beschriftungsregel statt Einzelfall-Konstanten.** Der heutige
   `FLUXFB_LBL_Y_OFFSET`-Hack existiert, weil GRIT bipolar wurde und
   `KNOBC` die größere Sperrfläche erbte. Die Ursache ist mit §1 weg, das
   Muster nicht: ein kleiner Knopf **direkt über** einem großen (16 mm
   Zeilenabstand) landet mit dem Vorgabeabstand 8,0 mm exakt auf der
   Sperrflächenkante des großen — 0 mm Luft. Betroffen sind pro Deck
   SHAPE, RANGE und FB.

   Eine Verkürzung trägt allerdings nicht: bei MORPH (152,4 / 62) und DECAY
   (152,4 / 79) sind beide groß und 17 mm auseinander, und jeder Abstand, der
   den Nachbarn freihält, liegt innerhalb der **eigenen** Sperrfläche — die
   Beschriftung stünde auf dem Knopf. Die Regel ist deshalb **Ausweichen statt
   Kürzen**: unten, sonst oben, sonst rechts (Anker `start`); die erste
   Position, die jede fremde Sperrfläche um 1,5 mm freihält und außerhalb der
   eigenen liegt, gewinnt. Der Generator wirft, wenn keine passt — er druckt
   kein Wort auf einen Knopf. Betroffen sind pro Deck SHAPE, RANGE und FB
   (weichen nach oben aus), in der Mitte MORPH (oben) und DECAY (rechts).
5. **`STAGES_A/B` (BEND) bleibt Doppelbelegung auf ATTACKs Knopf.** ATK sitzt
   bei (14, 66); BEND schreibt 7,0 mm darüber, ATK 8,0 mm darunter. Die
   Ausnahme in `test_no_overlap_with_hw_radii` bleibt genau eine.
6. **Das VCV-Rehearsal-Widget muss mit.** `FireflowHWWidget` in
   `host/vcv/src/Fireflow.cpp` wählt den Knopf bisher über `c.kind`
   (`WK_BIGKNOB`/`WK_KNOBC` → `RoundBlackKnob`, sonst `Trimpot`) und hätte
   nach der Neuvergabe ein großes RATE gezeigt, während das Blech ein
   kleines druckt. Ein Probelauf, der beim Thema Griffweite vom Blech
   abweicht, ist wertlos. Der Header trägt dafür `kParamSize[]` parallel zu
   `kParamCtls`. `HwPanelText` zeichnet zusätzlich `kHwOnlyCtls` — Rack
   rendert den Text des SVG nicht, CV/MOD/SHIFT wären sonst namenlose Kreise.

## §7 Testvertrag

`test_hw_panel.py` behält alle bestehenden Prüfungen. Vier ändern sich, drei
kommen dazu:

**Geändert**

- `test_hardware_footprints` prüft gegen die **Größenklasse**, nicht gegen
  `c.kind`. Die alten Minima nach Widget-Typ sind falsch geworden und würden
  ein kleines RATE als Fehler melden.
- `test_no_overlap_with_hw_radii` umfasst zusätzlich `HW_ONLY`.
- `test_rail_keepout` umfasst zusätzlich `HW_ONLY`.
- `test_labels_stay_off_neighbour_footprints` fordert **1,5 mm Marge über**
  dem Sperrflächenradius statt bloßer Berührungsfreiheit. Ein Bestehen mit
  0,1 mm — wie es die heutige Fassung zuließe — ist kein Bestehen.

**Neu**

- `test_sd_cutout_is_clear`: kein Bedienelement, keine Buchse, keine LED und
  **kein Beschriftungsanker** liegt im SD-Rechteck.
- `test_cv_sits_under_its_target`: jede CV-Buchse hat exakt dieselbe x-Koordinate
  wie der große Knopf, dessen Namen sie trägt, und liegt darunter. Das ist die
  einzige maschinelle Absicherung der Aussage aus §4 — ohne sie wandert eine
  Buchse in der nächsten Runde weg und die Beschriftung lügt.
- `test_size_classes_match_the_spec`: die 18 großen sind genau die aus §1.

Der bestehende `test_committed_files_match_the_generator` (Byte-Vergleich
gegen einen frischen Generatorlauf) bleibt und deckt alles Übrige ab.

**Jede neue Prüfung wird einmal rot gesehen, bevor sie grün zählt** — Memory
`spotykach-tests-must-be-able-to-fail`, und `fireflow-vacuous-test-gates`
erinnert daran, dass genau diese Datei neun Aufgaben lang ohne Runner
bestanden hat.

## §8 Konsequenzen für andere Dokumente

| Dokument | Was sich ändert |
|---|---|
| `2026-08-08-fireflow-hardware-envelope-design.md` §1 | Buchsen **10 → 18** (Kapazität „12" ist überholt); Potis „17 groß / 47 klein" → **18 / 46**; Taster **4 → 6**; LEDs „~20" wird auf **genau 20** festgelegt; Parameterzahl 68 → **70** (zwei funktionslose Taster) |
| dieselbe Spec §4 | „große Knobs ~22 mm, kleine ~15 mm" ist durch den Zeilenrhythmus aus §3 ersetzt; die Neugruppierung ist mit dieser Runde durchgeführt |
| `docs/hardware/io-budget.md` §5 | Buchsentabelle bekommt die acht CV-Eingänge auf `CV_1..8` |
| `docs/roadmap.md` | Neugruppierung von „offen" auf erledigt; H1 kann auf dieser Geometrie aufsetzen |

Die Runden-Deckelung der Envelope-Spec §4 („maximal 3 Runden") bleibt: dies
ist Runde 1. Der dort eingeplante **eine** Freeze-Bruch durch H1 nach der
ersten Fingerberührung bleibt ebenfalls eingeplant.

## §9 Bewusst nicht entschieden

- **Was ein CV-Eingang tut.** Tiefe, Offset, Verrechnung mit dem internen
  Modulator auf derselben Lane. Eigene Runde.
- **Was MOD und SHIFT tun.** Reserviert, nicht definiert.
- **Die Pin-Map.** Bleibt Task-2-Deliverable von Phase 0; diese Spec fixiert
  Geometrie und Kanalbedarf, keine Pinnummern.
- **Ob REC unten besser läge.** §3 nennt es als Preis; eine spätere Runde darf
  es tauschen, wenn das Spielen es zeigt.
- **Legendendruck und Gruppenrahmen.** Ob die sieben Gruppen auf dem Blech
  einen gedruckten Rahmen bekommen wie im VCV-Panel, ist Grafik und gehört zu
  H1, nicht zur Geometrie.
