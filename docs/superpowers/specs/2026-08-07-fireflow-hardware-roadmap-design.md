# FireFlow — Hardware-Roadmap bis Superbooth 2027

**Stand:** 7. August 2026
**Ziel:** ein spielbares FireFlow-Einzelgerät, 42 HP Eurorack, fertig am 23. April 2027.

Das Datum ist selbst gesetzt und hängt an der Superbooth 27 (6.–8. Mai 2027, FEZ Berlin). Ein Stand ist nicht gebucht und nicht Voraussetzung — siehe *Der Stand*. Superbooth ist der Termin, nicht der Auftrag.

## Ausgangslage

Der Zeitrahmen stammt ursprünglich aus der Synthux Design Residency, die abgesagt hat. Synthux spielt inhaltlich keine Rolle mehr; geblieben ist die Beobachtung, dass neun Monate für den Weg von laufender Firmware zu fertiger Hardware eine realistische Größe sind. Deshalb dieser Zuschnitt: Aug 2026 – Apr 2027.

**Stand am 7. Aug 2026:** Firmware v2.20.0 läuft, fünf Engines (SYNTH, WAVE, SAMPLER, BODY, BBD), zwei Decks, 907 Tests, 39 Releases, spielbar als VCV-Rack-Modul. Das Instrument war nach der Wavetable-Engine schon einmal komplett auf einem Daisy Seed geflasht — die Toolchain ist bewiesen. Der aktuelle Stand ist noch nicht auf Hardware, und das Patch Submodule ist vorhanden, aber noch nie geflasht.

## Rahmenbedingungen

| | |
|---|---|
| Formfaktor | Eurorack 42 HP, Daisy Patch Submodule |
| Zeitbudget | 10–15 h/Woche, Feierabend und Wochenende |
| PCB-Erfahrung | erstes eigenes Board |
| Lieferumfang 23. Apr 2027 | ein spielbares Einzelgerät |
| PCB-Runden | drei erlaubt, zwei eingeplant, eine als Reserve |

## Architekturentscheidung: zwei Boards

Main-PCB und Control-PCB getrennt, verbunden über Stiftleisten.

- **Main-PCB** — Patch SM, Stromversorgung, Audio-I/O, CV-Konditionierung, Jacks
- **Control-PCB** — Potis, Fader, Pads, LEDs, Multiplexer

**Begründung:** Bei einem Fehler in Rev A betrifft dieser meist nur eines der beiden Boards. Nachbestellung kostet dann ~40 € statt ~150 €, und ein Board kann bereits final sein, während das andere in Rev B geht. Bei einem ersten eigenen Layout ist das der Unterschied zwischen einer und zwei teuren Korrekturrunden.

**Verworfen:** Ein-Board-Ansatz (jeder Fehler kostet die volle Runde und drei Wochen Lieferzeit) und „erstes PCB ist der Prototyp" (drei Runden am teuren Board statt an einem billigen).

## Die Lernkurve auf ein 10-€-Board verlagern

Das erste selbst geroutete PCB ist **nicht** das Instrument, sondern ein Testcoupon, ~5×5 cm:

- ein Multiplexer
- acht Potis
- die LEDs
- ein Pad
- Patch-SM-Header

Sonst nichts. Lieferzeit gut eine Woche, Kosten ~10 €, ein zweiter Dreh ist eingeplant und kein Rückschlag. Getestet wird daran, was am 42-HP-Board weh tut: Mux-Adressierung, ADC-Rauschen neben dem Audiopfad, Pad-Ansprache, Einschwingzeit beim Kanalwechsel, und ob die Footprints zu echten Bauteilen passen.

## Die drei Hardware-Meilensteine

Bezeichnung H1–H3, um Verwechslung mit den Firmware-Meilensteinen (M1–M6) zu vermeiden.

| | Datum | Inhalt |
|---|---|---|
| **H1** | 6. Nov 2026 | Grober Prototyp spielt: Testcoupon + gelasertes 42-HP-Acrylpanel mit echten Potis. Panel-Layout friert ein. |
| **H2** | 12. Feb 2027 | Rev A, erstes echtes Board-Set, spielbar, Fehlerliste geschlossen. |
| **H3** | 9. Apr 2027 | Rev B, fertiges Instrument-PCB, aufgebaut und getestet. |

## Terminliste (rückwärts gerechnet)

Alle Termine auf Freitag, Wochenende als Puffer.

- [ ] **21. Aug 2026** — Phase 0 abgeschlossen: Firmware läuft auf Patch SM, CPU neu gemessen, I/O-Inventar steht
- [ ] **4. Sep 2026** — **Glow-Entscheidung** (siehe *Die Glow-Frage*): trägt Glow das Instrument, oder bleibt es ein VCV-Modul
- [ ] **11. Sep 2026** — Testcoupon-Bestellung raus, ICs mitbestellt
- [ ] **25. Sep 2026** — Coupon Runde 1 im Haus
- [ ] **9. Okt 2026** — Coupon Runde 1 ausgewertet, ggf. Runde 2 bestellt
- [ ] **6. Nov 2026** — **H1.** Prototyp spielt, Panel-Layout eingefroren, Firmware-Feature-Freeze
- [ ] **Mitte Nov 2026** — formlose Vorstellungsmail an `ak@superbooth.com` (Stufe 1, unverbindlich)
- [ ] **Nov 2026** — Lieferzeiten für Potis, Buchsen, Encoder geprüft
- [ ] **18. Dez 2026** — Rev-A-Bestellung raus (beide Boards + Alu-Frontplatte), Bauteile bereits im Haus
- [ ] **15. Jan 2027** — Rev-A-Boards im Haus
- [ ] **Ende Jan 2027** — Rev-B-BOM bestellt (**vor** Chinese New Year)
- [ ] **12. Feb 2027** — **H2.** Rev A spielbar, Fehlerliste vollständig, Rev-B-Freeze
- [ ] **12. Feb 2027** — Standentscheidung (Stufe 2): buchen oder nicht
- [ ] **5. Mär 2027** — Rev-B-Bestellung raus (nach CNY)
- [ ] **26. Mär 2027** — Rev-B-Boards + finales Panel im Haus
- [ ] **9. Apr 2027** — **H3.** Rev B aufgebaut, bestückt, getestet
- [ ] **23. Apr 2027** — Hard Freeze: Gerät spielt, Demo-Set sitzt. **Das ist der Termin.**
- [ ] **6.–8. Mai 2027** — Superbooth 27, FEZ Berlin: mit Stand, oder mit Mantel

## Die vier Phasen

### Phase 0 · 7.–21. Aug 2026 · Das Fundament (2 Wochen)

Die kürzeste Phase, und die, an der alles hängt.

**Woche 1 (7.–14. Aug) — auf das Submodul.** Aktuellen Stand v2.20.x auf das Patch SM bringen. Der Wechsel Seed → Patch SM ist überschaubar: gleicher STM32H750, gleiches SDRAM, aber anderer Audio-Codec, anderes Pinout, Onboard-CV-Wandler kommen dazu. Praktisch heißt das `DaisyPatchSM` statt `DaisySeed` in der Board-Init und eine neue Pin-Map.

Danach **CPU auf dem Submodule messen**. Der aktuelle Stand laut `docs/roadmap.md` (4. Aug 2026): `instrument_worst_bbd_dtcm` liegt bei **96,43 % offline / 96,69 % im echten Callback**, mit BBD, bei `-O3`, Layout `axi` — und `axi` ist das Layout, das eine M6-Firmware heute bekäme. Die Engine passt also, mit 3,57 Punkten Luft.

Ungemessen sind zwei Dinge, und beide zählen mehr als die Zahl selbst:

1. **Alle Messungen stammen vom Daisy Seed.** Auf dem Submodule wurde nie etwas gemessen. Gleicher STM32H750, gleiches SDRAM und QSPI — die Zahlen sollten übertragen, aber „sollten" ist keine Messung.
2. **Der Shell-Anteil ist nicht enthalten.** Die Bench bootet in ein Zykluszähl-Harness, nicht in Firmware. Mux-Scan, WS2812-LED-Ausgabe, MPR121-Touch und UI kommen auf die 96,43 % obendrauf, und 3,57 Punkte sind dafür wenig. **Das ist die eigentliche Unbekannte von Phase 0.**

**Woche 2 (14.–21. Aug) — I/O.** Ein Poti über einen Multiplexer an den ADC, hörbar auf einen Parameter. Dann die Inventar-Tabelle füllen und gegen die Patch-SM-Pins rechnen. **CPU erneut messen, diesmal mit laufendem Mux-Scan** — das ist der Wert, der zählt.

**Der eigentliche Inhalt ist die Panel-Reduktion.** Der VCV-Panelgenerator (`host/vcv/res/gen_panel.py`) führt heute **82 Runtime-Parameter** (80 Panel + 2 angehängt; 23 pro Part, 16 shared), dazu 4 Inputs, 6 Outputs, 4 Lights. Auf 42 HP — nutzbar rund 213 × 115 mm — sind realistisch 45–55 Bedienelemente plus Buchsen unterzubringen. Es braucht also grob eine **Halbierung**, und das ist eine Design-Entscheidung, keine Buchhaltung: Lane-Select statt fünf Lane-Reihen, Hold-Layer, ALT-Gesten, geteilte Reihen zwischen den Parts. `docs/roadmap.md` führt das Panel zu Recht als „undefined". Diese Entscheidung verdient eine eigene Brainstorming-Runde und ist der inhaltliche Kern von Phase 0.

**Deliverable ist eine Tabelle, keine Zeichnung:**

| | Anzahl | landet auf |
|---|---|---|
| Potis | | Mux-Kette → 1 ADC-Pin pro Mux |
| Fader (Center) | | eigener ADC oder Mux |
| Pads / Buttons | | Mux oder Matrix |
| LEDs | | Charlieplex / Shift-Register / I²C-Treiber |
| Encoder | | 2 GPIO + Taster |
| Jacks Audio in/out | | fest, Patch SM |
| Jacks CV in/out | | ADC / DAC-Kanäle |
| Gate/Clock in/out | | GPIO |

Dazu ein Blatt mit der Rechnung: *X ADC-Kanäle nötig, Patch SM hat Y, passt mit Z Reserve.* Auf die Zählung 20 % Reserve aufschlagen — im Layout kommt immer noch ein Taster dazu.

**Ausdrücklich nicht Teil von Phase 0:** wo die Knobs sitzen, wie groß sie sind, Beschriftung, Farbe, Ästhetik. Das räumliche Panel-Layout ist H1.

Im August wird festgelegt, **was das Instrument braucht**. Im November, **wie es angeordnet ist**. Im Dezember, **wie es geroutet ist**.

### Phase 1 · 24. Aug – 6. Nov 2026 · Der grobe Prototyp (11 Wochen) → H1

KiCad am kleinen Objekt lernen. Coupon entwerfen, am 11. Sep bestellen, ab 25. Sep messen. Ein zweiter Dreh ist eingeplant.

Parallel ein **gelasertes Acrylpanel in 42 HP** mit echten Potis. Das ist der Ergonomietest, und die Bohrdatei wird später zum Alu-Panel.

**H1 am 6. Nov:** das Ding steht auf dem Tisch, klingt, und man kann es spielen. Am selben Tag friert das Panel-Layout ein.

### Phase 2 · 9. Nov 2026 – 12. Feb 2027 · Rev A, das Beta-Board (14 Wochen) → H2

Sechs Wochen Schaltplan und Layout für beide Boards. Bestellung am 18. Dez, **inklusive Alu-Frontplatte** — Rev A soll sich schon anfühlen wie das Instrument, nicht wie ein Testaufbau. Über Weihnachten fertigt die Fab.

Ab 15. Jan vier Wochen Bringup: bestücken, jeden Kanal durchmessen, Rauschabstand, thermisches Verhalten, Ergonomie am echten Alu.

**Regel für diese Phase:** Alles, was auffällt, kommt in eine Liste. **Nichts wird zwischendurch gefixt** — sonst wird Rev B nie fertig.

**H2 am 12. Feb:** erstes echtes PCB, spielbar, Fehlerliste geschlossen.

### Phase 3 · 15. Feb – 23. Apr 2027 · Rev B, das fertige Board (10 Wochen) → H3

Drei Wochen Korrektur-Layout, Bestellung am 5. März — bewusst **nach Chinese New Year (6. Feb 2027)**, weil die Fabriken davor und danach je gut zwei Wochen stillstehen. Die Rev-B-Bauteile müssen deshalb schon Ende Januar bestellt sein.

Boards am 26. März, zwei Wochen Aufbau. **H3 am 9. April.** Danach nur noch Firmware-Feinschliff, Demo-Material und Reisevorbereitung.

## Die Glow-Frage

Seit dem 5./6. August existiert **Glow**: 12 HP, sechs Macro-Knöpfe und ein
NEW-Taster über `engine/flow/`, gebaut, gemerged und am 6. August in Rack
handverifiziert. Sein Panel ist auf echte Hardwaremaße gezeichnet — 61 × 128,5 mm
— ausdrücklich, damit es als 1:1-Entwurf für M6 Schritt 1 dienen kann.

Damit steht eine Frage im Raum, die der Rest dieser Spec nicht beantwortet:
**welches der beiden Instrumente wird zum 23. April fertig.**

**Was für Glow spricht.** Der Plan hat genau eine unspezifizierte Stelle, und
`docs/roadmap.md` benennt sie selbst: *„Until step 1 has a spec, M6 has no
implementable definition."* 82 Runtime-Controls auf 42 HP zu reduzieren ist
Designarbeit unbekannter Größe. Glow beantwortet diese Frage nicht — es stellt
sie nicht. Sechs Potis, ein Taster, Panel gezeichnet. Ein Viertel der
Boardfläche, kein Multiplexer, eine kleine BOM.

**Was Glow nicht löst, damit die Rechnung ehrlich bleibt:**

1. **Es entschärft das 42-HP-Board kaum.** Sechs Potis brauchen keinen
   Multiplexer — das Patch SM hat genug ADC-Eingänge direkt. Ein Glow-Board
   validiert Power, Audio-I/O, Panelfertigung, Firmware-Shell und Mechanik, aber
   **nicht** die Mux-Kette und **nicht** die LED-Skalierung: genau die Teile, die
   am großen Board wehtun.
2. **Die CPU-Lage ist identisch.** Gleiche Engine, gleiche 96,43 %, gleiche 3,57
   Punkte Reserve. 12 HP macht das nicht billiger.
3. **Das offene Risiko ist musikalisch.** Der House-Seed in
   `engine/flow/taste.h` ist ein gemessener Platzhalter, keine Ohr-Entscheidung;
   die Listening-Runden aus `docs/superpowers/specs/2026-08-05-flow-listening-notes.md`
   stehen aus. Ob ein Macro-Instrument unter der Hand lebt, ist unbeantwortet.

**Die Frist: 4. September 2026.** Nicht später, weil die Testcoupon-Bestellung am
11. September rausgeht und ein Coupon nur für das 42-HP-Panel Sinn ergibt — bei
sechs Potis gibt es keine Mux-Kette zu validieren. Zwischen dem 21. August und
dem 4. September wird Glow gespielt und die ausstehende House-Seed-Runde nach
Gehör gemacht. Die Entscheidung fällt am Instrument, nicht am Zeitplan, und
beantwortet eine einzige Frage: **würde ich das selbst besitzen wollen.**

**Die drei Ausgänge:**

| | Ausgang | Konsequenz für diesen Plan |
|---|---|---|
| **A** | Glow trägt es | Glow wird das Instrument zum 23. April, FireFlow 42 HP wird Produkt zwei. Kein Testcoupon nötig — das erste eigene PCB ist das Glow-Board. Phase 1 verkürzt sich, die Reserve wächst deutlich. |
| **B** | Glow trägt es nicht | Glow bleibt ein VCV-Modul. Dieser Plan gilt unverändert, Coupon-Bestellung am 11. September wie geplant. |
| **C** | Unentschieden | **Wird wie B behandelt.** Ein Plan kann nicht auf ein Vielleicht warten, und im Zweifel gewinnt der Kern. |

Ausgang C ist die eigentliche Regel dieses Abschnitts: die Unentschiedenheit hat
selbst ein Verfallsdatum. Wer am 4. September nicht Ja sagen kann, hat Nein
gesagt.

## Bauteilbeschaffung

**Multiplexer.** KiCad-Verfügbarkeit ist kein Auswahlkriterium — Symbole für die 4000er- und 74xx-Familie (`CD4051`, `74HC4051`, `CD74HC4067`) sind mitgeliefert, die Footprints sind Standardgehäuse. Das echte Kriterium ist die **JLCPCB-Bauteilbibliothek**, weil Rev A bestückt bestellt werden soll. SMT von Hand ist keine Lernkurve, die im Januar Platz hat.

**Typwahl:** `74HC`-Typ, **kein** `CD4051B`. Das Patch SM läuft auf 3,3 V; der klassische CMOS-Typ hat dort einen deutlich höheren Durchlasswiderstand, verschleppt beim Kanalwechsel die Einschwingzeit und erzeugt zappelige ADC-Werte — ein Fehler, den man leicht dem Layout anlastet, obwohl er in der Bauteilwahl saß.

**Bezugsquellen:** Für den Breadboard-Test in Woche 2 reicht ein fertiges Breakout (CD74HC4067-Modul, ~8 €, Prime-Versand) — kein Termin hängt davon ab. Für alles, was aufs PCB geht: Reichelt oder Mouser. Amazon-Drittanbieterware ist oft ungekennzeichnet oder recycelt, und einen Bauteilfehler will man in Rev A nicht suchen.

**Termine:** Mux-Auswahl Anfang September vor dem Coupon-Design, ICs direkt mitbestellen. Der Beschaffungstermin, der wirklich zählt, ist die **Rev-A-BOM im Dezember** — Potis, Buchsen und Encoder in Eurorack-Qualität haben Lieferzeiten.

## Der Stand

Superbooth ist das Datum, nicht der Auftrag. Ein Stand ist weder gebucht noch Voraussetzung dafür, dass der Plan aufgeht — das Instrument ist am 23. April fertig oder nicht, unabhängig davon, wo es danach steht.

**Es gibt keine Anmeldefrist.** Superbooth schreibt: *„There is no deadline and no automated registration process. We welcome inquiries up until beginning of May."* Kein Formular, kein Bewerbungsverfahren — eine Mail an `ak@superbooth.com` mit Beschreibung des Produkts. Es gibt aber ein Kapazitätsende: *„we cannot accommodate all requests due to limited space."* Die Messe positioniert sich ausdrücklich für *small boutique manufacturers of innovative tools* — das Profil passt, heißt aber auch, dass sich viele mit demselben Profil melden.

Deshalb dreistufig:

**Stufe 1 · Mitte Nov 2026, nach H1.** Formlose, unverbindliche Mail. Nicht „ich will einen Stand", sondern „das hier baue ich, so weit ist es, ich melde mich im Februar wieder". Zu dem Zeitpunkt existieren ein spielender Prototyp, ein Panel und ein öffentlicher Dev-Log über mehr als ein Jahr. Kosten: eine halbe Stunde. Nutzen: auf dem Schirm stehen, bevor der Platz knapp wird.

**Stufe 2 · 12. Feb 2027, an H2.** Die eigentliche Entscheidung. Rev A liegt spielbar im Alu-Panel auf dem Tisch — das ist der erste Zeitpunkt, an dem ehrlich beurteilbar ist, ob H3 im April steht. Wenn ja: verbindlich buchen, mit Video vom laufenden Gerät. Wenn nein: kein Stand, kein Verlust, weil bis dahin nichts zugesagt wurde.

**Stufe 3 · Mantel.** Kein Stand, aber ein Ticket: Instrument, Powerbank und Bluetooth-Box passen in eine Umhängetasche. Superbooth ist die Messe, auf der das niemandem übel genommen wird.

## Puffer

Drei, bewusst gestaffelt:

1. **Zweiter Coupon-Dreh** (Okt) — kostet 10 € und eine Woche, kauft Layout-Sicherheit für Rev A.
2. **Reserve-Runde Rev C** — falls Rev B am 26. März noch etwas hat: Bestellung 2. April, Boards am 23. April. Möglich, frisst aber den kompletten Endpuffer. Die dritte Runde ist *eingeplant, aber nicht verplant*.
3. **Zwei Wochen vor Superbooth** — reserviert für Demo und Reise, nicht fürs Löten.

**Zeitbudget:** 38 Wochen × 10–15 h ≈ 380–570 h. Geschätzter Umfang ~500 h, also am oberen Rand. Machbar, heißt aber: die August-Wochen dürfen nicht verrutschen.

## Risiken

**CPU-Budget.** 96,43 % sind gemessen, aber auf dem Seed und ohne den Firmware-Shell. Beides klärt sich in Phase 0, sonst wird es im Januar teuer.

**Bauteilbeschaffung.** Rev-A-Bauteile im November prüfen, im Dezember bestellen. Rev-B-BOM Ende Januar raus, vor CNY.

**Scope-Kriechen in der Firmware.** Die Firmware läuft und ist gut. Ab November ist jede neue Engine eine Woche, die dem Layout fehlt. **Feature-Freeze für alles außer Hardware-Anbindung ab 9. Nov 2026.**

**Zwei Instrumente gleichzeitig.** Das größte Risiko an Glow ist nicht Glow, sondern Glow *und* FireFlow parallel: zwei Panels, zwei BOMs, zwei Layouts, zwei Bring-ups bei 10–15 h die Woche. Die Frist am 4. September existiert, um genau das zu verhindern. Nach ihr wird eines von beiden gebaut, nicht beides.

## Abbruchkriterien

Wenn der Plan in Verzug gerät, wird **der Umfang reduziert, nicht der Termin verschoben.**

- **Reserve nach dem Shell-Aufschlag aufgebraucht:** Engine-Auswahl begrenzen oder Sample-Rate-Entscheidung treffen. Beides ist billig, solange kein Kupfer geflossen ist.
- **Im Oktober absehbar zu wenig Zeit:** Panel verkleinern — 42 HP auf 34 HP, zwei Lanes weniger auf dem Panel bei gleicher Firmware.
- **Rev A hat mehr Fehler als eine Runde tragen kann:** Reserve-Runde ziehen und den Endpuffer opfern, aber H3 nicht nach hinten schieben.

Der Termin ist der 23. April 2027. Alles andere ist verhandelbar.
