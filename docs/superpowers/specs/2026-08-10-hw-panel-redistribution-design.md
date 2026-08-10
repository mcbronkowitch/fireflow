# HW-Panel: Neuverteilung — Design

**Datum:** 10. August 2026
**Status:** Entwurf, abgenommen. Nichts davon ist implementiert.
**Vorgänger:** `2026-08-10-hw-panel-regroup-design.md` (Gruppen gefunden),
`2026-08-08-fireflow-hardware-envelope-design.md` (Rahmen, Klassen, Rundendeckel)

Die Regroup-Runde hat die Gruppen der Engine gefunden und die Knopfgrößen nach
Rang vergeben. Sie hat die Bedienelemente aber dort gelassen, wo der
Bildschirmentwurf sie hatte: gedrängt in die obere Hälfte, mit zwei fast vier
Zentimeter großen Löchern neben der Mitte und einem praktisch leeren unteren
Fünftel. Diese Runde verteilt sie neu.

**Der Rundendeckel der Envelope-Spec §4 („maximal 3 Runden") greift hier nicht.**
Er zählt Freeze-Brüche nach der Hardware-Bestellung; solange nichts bestellt ist,
kostet Umräumen nichts. Diese Spec ist damit weiterhin Vorarbeit zu H1, nicht
Runde 2 des Freezes.

---

## §1 Der Befund, aus dem alles folgt

Die Platte ist zu **28 %** belegt — 108 Fußabdrücke auf 304,8 × 110,5 mm
Nutzfläche. Gemessen, nicht geschätzt:

| Loch | Ort | Radius |
|---|---|---|
| neben der Mittelsäule | 126,0 / 70,5 und 179,0 / 70,5 | **19,5 mm** |
| oben zwischen LED-Feld und MOD/SHIFT | 114 / 20 | 10,9 mm |
| unter den Innensäulen | 118 / 92 | 11,0 mm |
| untere Außenecken | 10,5 / 99,5 | 10,4 mm |

Das unterste Band (y 97–120) liegt bei **15 %** Belegung, die vier darüber bei
26–36 %. Vier benennbare Ursachen:

1. **Ein Deck ist 96 mm breit, obwohl es 127 mm besitzt.** Alles sitzt zwischen
   x = 14 und 110. Der Streifen x = 110…127 ist in *jeder* Zeile leer.
2. **Die Mitte benutzt in zwei Zeilen nur die Achse.** Bei y = 62 steht MORPH
   allein, bei y = 79 DECAY allein — die direkte Ursache der beiden 19,5-mm-Löcher.
3. **Das Deck sortiert nach Größenklasse statt nach Gruppe.** Eine dichte kleine
   Zeile (acht Knöpfe im 12,5-mm-Raster) wechselt mit einer dünnen großen (drei
   bis vier über 100 mm). Vier große Knöpfe über 100 mm können nicht gleichmäßig
   liegen.
4. **Unter y = 112 liegt nichts**, über die volle Breite bis zur Sperrlinie.

---

## §2 Die Regel: das Raster ist fest, die Breite folgt

Der erste Entwurf dieser Runde („A") hat jede Zeile auf Deckbreite gestreckt.
Damit wurde der Abstand zur Restgröße: die PITCH-Zeile hatte vier Elemente für
96 mm, also **32 mm Raster** — TUNE und DTUN rissen auseinander. Die MOD-Zeile
hatte sieben, also 16 mm mit zwei großen Kappen dazwischen, also gequetscht.

**Der Abstand ist aber genau das, was „gehört zusammen" sagt.** Er darf nicht die
Restgröße sein. Also andersherum:

**Innerhalb einer Gruppe ist das Raster konstant:**

| Nachbarn | Raster |
|---|---|
| klein — klein | 13,0 mm |
| klein — groß | 16,0 mm |
| groß — groß | 19,0 mm |
| LED — LED | 4,0 mm |
| LED — Knopf | 9,0 mm |
| LED — Taster | 7,5 mm |
| Taster — Taster | 10,0 mm |

Eine Gruppe ist damit so breit, wie sie ist. **Die Luft sammelt sich zwischen
den Gruppen, als gleichbleibende Gasse** — und die Gasse hat eine Obergrenze von
**12,0 mm**. Was darüber hinaus übrig bleibt, geht an den Rand des Bandes, nicht
in die Gasse. Ohne diesen Deckel riss das dünn besetzte untere Band 27-mm-Lücken
zwischen die Gruppen; gemessen, nicht vermutet.

---

## §3 Eine Gruppe ist ein Block, keine Zeile

VOICE als Zeile wäre **101,5 mm** breit — fast das ganze Deck, und für die
Nachbarn bliebe nichts. Als **Block aus zwei Zeilen** sind es 51,5. Das ist die
zweite Hälfte der Regel und der eigentliche Grund, warum Entwurf A scheiterte.

### Pro Deck

| Block | obere Zeile | untere Zeile | Breite |
|---|---|---|---|
| **TIME** (Statusstreifen) | STPS · SONG · RATE · VARY | — | 51 mm |
| **PLAY** (Statusstreifen) | FLOW-LED · REC · REC-/CAP-/GATE-LED | — | 26 mm |
| **MOD** | SHAPE · SMTH · RANGE | **MOD** · **DENS** | 38,0 mm |
| **VOICE** | ATK · DEC · RES · SUB | ENGINE · **FILT** · **TIMB** | 51,5 mm |
| **PITCH** | TUNE · DTUN | **COLOR** | 25,0 mm |
| **FLUX** | TIME · FB · LINK | **MIX** | 38,0 mm |
| **OUT** | **LVL** | GRIT | 17,0 mm |

Fett = große Kappe. Für die fünf Deck-Blöcke ist es keine Setzung, welche
Blockzeile oben liegt, sondern Ergebnis der Suche aus §8. Der ROOM-Block der
Mitte ist die eine Ausnahme: er wurde von Hand gedreht, aus einem Grund, der erst
im gerenderten Bild sichtbar wurde (§10).

### Mitte

| Zeile | Inhalt |
|---|---|
| Statusstreifen | SCALE · DRIFT (TONALITY) |
| y = 34 | TEMPO-LED · TEMPO · FREE\|GRID · SHUFL · SYNC-LED (CLOCK) |
| y = 53 | TIDE · **MORPH** · CHOKE (BLEND) |
| y = 76 | SIZE · **DECAY** · DIFF (ROOM) |
| y = 95 | **SEND A** · TONE · **SEND B** (ROOM) |
| y = 114 | CLK · SD-Ausschnitt · RST |

DECAY liegt in der **oberen** ROOM-Zeile, nicht in der unteren. Unten fiel seine
Bildunterschrift in den SD-Ausschnitt; nach oben gedrängt las sie sich als
Beschriftung von TONE statt als eigene. Gedreht sitzt jedes Wort der Mitte unter
seinem Knopf. Siehe §10.

**SEND wandert aus dem Deck in die Mitte.** Die Regroup-Spec beschreibt SEND
wörtlich als „der Weg des Decks in den geteilten Hall"; heute steht dieser Weg
40 mm von dem Hall entfernt, den er speist. Neben DECAY, SIZE, TONE und DIFF wird
ROOM zum ersten Mal ein sichtbarer Block: zwei Wege hinein, ein Raum dazwischen.
Die Spiegelung bleibt sauber, SEND A und SEND B ergeben zusammen 304,8.

Damit ändert sich die Gruppentabelle der Regroup-Spec §2: ROOM ist dort eine
Deck-Gruppe mit einem Element, danach eine Mittengruppe mit sechs.

---

## §4 Die Zeitgruppe: RATE und VARY ziehen nach oben

`engine/mod/lane.h:154` schreibt es hin:

```
void _update_inc();   // step-clock: inc = rate/sr * (STEP ? 8/steps : 1)
```

**RATE und STEPS sind die beiden Faktoren derselben Zahl.** STEPS steht nicht
assoziativ neben RATE — es teilt es. Im FLOW-Modus (STEPS = 0) fällt der Teiler
weg und RATE steht allein; im STEP-Modus ist die Zykluszeit `rate × 8/steps`.
Dazu passt `engine/center/center.cpp:163`: für ein STEP-Deck läuft die
Drift-Skalierung der Mitte ins Leere, „a follower has no rate to wander, it
derives its slot from the deck's step count".

Damit ist PLAY nicht mehr, was die Regroup-Spec „die einzige kopflose Gruppe"
nannte. Die **Zeitgruppe** des Decks hat einen Kopf, und der heißt STEPS:
**STPS · SONG · RATE · VARY**.

**Der Preis, offen genannt:** der Modulator wird wieder zerschnitten — genau das
Objekt (`_parts[p].mod()`, `instrument.h:46`), dessen Zerschneidung die
Regroup-Runde repariert hat. Der Unterschied ist, dass die Naht diesmal im Code
steht und nicht in einer alten Bildschirmaufteilung. Dass SONG und VARY im
FLOW-Modus inert sind, ist **nicht verifiziert** — es stammt aus einer früheren
Sitzungsnotiz über `_wrap_events()` bei STEPS = 0. Verifiziert ist nur `RATE × STEPS`.

---

## §5 ENGINE wird ein Rastpoti mit fünf Zonen

Heute ist ENGINE ein Zyklus-Taster mit fünf LEDs pro Deck: von Synth zu BBD sind
es vier Drücker, und auf dem Blech sagt nichts, welcher Punkt Wave ist. Neu:
**ein Rastpoti mit fünf gedruckten Zonen, am Kopf der VOICE-Gruppe.**

- **Das Bauteil existiert schon.** `docs/hardware/io-budget.md` führt „Rastpoti,
  ganzzahlig (`KNOBI`)" mit sieben Stück — zwei davon sind STPS und SONG. Kein
  neuer Typ, sondern derselbe wie bei den Nachbarn.
- **Zehn LEDs entfallen** (fünf pro Deck), 20 → 10.
- **Ein Griff statt vier Drückern.**
- **Die Platte sagt endlich, was die Engines sind.** Das zählt doppelt, weil sich
  sieben Regler je nach Engine umbenennen (`gen_panel.py:216–222`: SOURCE liest
  TIMB, ORG, FRAME, MATL oder DRIVE). Auf Blech ist eine Lesart eingefroren;
  daneben zu drucken, welche Engine läuft, sagt einem wenigstens, welche gilt.
- **ENGINE ist klein, nicht groß.** Groß gemessen wächst VOICE auf 54 mm, und MOD
  daneben lässt nur noch 5,5 mm Gasse — schmaler als Abstände innerhalb mancher
  Gruppe, also genau der Fehler, den §2 abstellt. Klein führt ENGINE die große
  Zeile von VOICE an, wie VARY früher die von MOD: klein, groß, groß.

**Absolutwert statt gespeicherter Zustand:** ein Poti zeigt seine Stellung, kein
geladener Patch. STPS und SONG haben dasselbe Verhalten und das Projekt hat es
dort längst so entschieden; ENGINE folgt der bestehenden Regel.

**Grenze, ehrlich:** erweiterbar ist die Firmware, nicht das Blech. Eine sechste
Engine ist eine sechste Zone — auf der gedruckten Platte hat sie kein Wort, mit
Poti so wenig wie mit LEDs.

---

## §6 LEDs stehen bei dem, worüber sie berichten

Die zehn verbleibenden LEDs sind keine Platzhalter, alle haben eine Funktion.
Neu ist nur ihr Ort:

| LED | meldet | steht jetzt bei |
|---|---|---|
| FLOW | FLOW- oder STEP-Modus | STPS und SONG |
| REC | Aufnahme | REC-Taster |
| CAP | Capture läuft | REC-Taster |
| GATE | Gate liegt an | REC-Taster |
| TEMPO, SYNC | Takt und Sync | TEMPO-Regler |

Das Raster macht den Unterschied sichtbar, ohne einen Strich zu drucken: 4 mm
zwischen zwei LEDs, 9 mm zum nächsten Regler. Aus neun gleichen Punkten in einer
Linie werden Grüppchen mit je einer Bedeutung.

---

## §7 SHIFT und MOD in die unteren Außenecken

Die beiden globalen Modifier verlassen die Kopfzeile der Mitte und sitzen unten
außen, in der Buchsenzeile. Zwei Gründe:

1. **Je einer pro Hand.** SHIFT links halten und rechts drehen geht, MOD rechts
   halten und links drehen auch. Nebeneinander in der Mitte erreicht sie im Spiel
   keine Hand.
2. Sie füllen die beiden Restlöcher, die in den unteren Außenecken übrig blieben.

Die Buchsenzeile rückt dafür auf **11,5 mm Raster** zusammen und nach innen.

**Folge für die Mitte:** MOD und SHIFT waren das Einzige, was deren Kopfzeile
füllte. TONALITY (SCALE, DRIFT) zieht dorthin nach — global gestimmt, globale
Zeile. Bei x = 123,4 in der Zeile y = 53 kollidierte SCALE ohnehin mit 0,4 mm
an RANGE.

---

## §8 Zeilenrhythmus, Bänder und wie sie gewählt wurden

**Zeilen:** 14,5 / 34 / 53 / 76 / 95 / 114. Innerhalb eines Blocks 19 mm,
zwischen zwei Bändern 23 mm, Buchsenzeile 19 mm unter dem letzten Band.

Beide Zahlen sind gerechnet, nicht gewählt. **Innerhalb eines Blocks** trägt die
eine Zeile nur kleine Knöpfe, die andere die großen — der schlechtere der beiden
Fälle ist 18,0 mm (kleine Bildunterschrift 8,0 + großer Radius 8,5 + 1,5 Rand,
oder große Bildunterschrift 10,5 + kleiner Radius 6,0 + 1,5). 19 mm hält einen
Millimeter Abstand zur Grenze. **Zwischen zwei Bändern** können beide Zeilen
große Knöpfe tragen: 10,5 + 8,5 + 1,5 = 20,5 mm, also 23 mm mit 2,5 Reserve.

Ein Millimeter Reserve ist kein Luxus: die Platte saß gestern schon einmal exakt
auf der Grenze, und drei Bildunterschriften flohen in den Statusstreifen.

Die Buchsen liegen bei 114, weil eine 4-mm-Buchse unter der Sperrlinie bei 119,5
höchstens auf 115,5 kann.

**Drei Blockbänder passen nicht.** 19 + 23 + 19 + 23 + 19 = 103 mm, und dann fehlt
die Buchsenzeile in 110,5 mm Nutzhöhe. Der Statusstreifen bleibt deshalb eine
einzelne Zeile.

**Die Anordnung ist gesucht, nicht gesetzt.** 13 824 Varianten — welche Gruppen
sich ein Band teilen, in welcher Reihenfolge, welche Blockzeile oben liegt, wohin
der Überschuss eines Bandes geht — jede bewertet nach ihrem größten leeren Kreis.
Gewählt:

- **oberes Band:** MOD | VOICE
- **unteres Band:** PITCH | FLUX | OUT
- **Statusstreifen:** TIME | PLAY, nach innen ausgerichtet

Die Suche stellte OUT zwischen PITCH und FLUX. Die Leserichtung
Tonhöhe → Echo → Ausgang zu erzwingen kostete gemessen **0,09 mm** und wird
genommen: neun Hundertstel sind kein Argument gegen eine lesbare Kette. Die
Endgeometrie misst danach 11,61 mm.

**Die Zeichnung liegt bei:** `docs/hardware/2026-08-10-hw-panel-redistribution.svg`
(wahre Größe, vektoriell) und `.png` (2400 px breit, aus dem SVG gerendert).

---

## §9 Kappengröße: 5,5 → 6,0 und 8,0 → 8,5 mm

Seit dem ersten Blockentwurf landet **jede** Anordnung zwischen 11,5 und 12,1 mm
größtem Loch — nie deutlich darunter, egal wie umgeräumt wird. Das ist kein
Zufall: 25 Regler pro Deck decken bei 5,5 und 8,0 mm Kappenradius eine halbe
60-HP-Platte nicht dichter ab. Die Grenze von 12,0 lag exakt am Machbaren.

Mit einem Millimeter mehr Kappendurchmesser fällt der Wert auf **11,61 mm** und
das Gassenverhältnis wird **2,0 : 12,0**. Größere Kappen greifen sich außerdem
besser — was das erklärte Ziel dieser Runde war („Benutzbarkeit durch mehr Raum").

Das ersetzt `CLASS_R` aus der Regroup-Spec §1:

| Klasse | alt | neu |
|---|---|---|
| groß | 8,0 mm | **8,5 mm** |
| klein | 5,5 mm | **6,0 mm** |
| Taster, Buchse, LED | 4,0 / 4,0 / 1,5 | unverändert |

**Preis:** im 13-mm-Raster stehen zwei kleine Kappen danach 1,0 mm auseinander
statt 2,0. Das ist für 12-mm-Kappen üblich, muss aber am echten Bauteil geprüft
werden, bevor gefräst wird.

---

## §10 Die Gates

Homogenität wird gemessen, nicht beurteilt. Fünf Gates in `res/test_hw_panel.py`,
und für jedes wird die Rotphase **einmal nachgewiesen** — die Datei ist schon
einmal neun Tasks lang mit Exit 0 durchgelaufen, ohne etwas zu behaupten.

| Gate | Grenze | erreicht |
|---|---|---|
| größter leerer Kreis in der Nutzfläche | ≤ 12,0 mm | **11,61 mm** |
| jeder Abstand in einer Gruppe enger als jede Gasse | — | **2,0 : 12,0** |
| Bildunterschrift auf Bildunterschrift | 0 | **0** |
| Bildunterschrift außerhalb 9,0…119,5 | 0 | **0** |
| Bildunterschrift über dem SD-Ausschnitt | 0 | **0** |

Die letzten drei sind neu und **alle drei durch echte Fehler entstanden**, keiner
davon aus Vorsicht erfunden:

- **Schrift gegen Schrift** fehlte, und in Entwurf A lag `SEND` auf `INR` —
  gerechnet vorhergesagt, dann gemessen bestätigt.
- **Schrift gegen Schienen** fehlte, und SHIFT bekam seine Bildunterschrift bei
  y = 120,5, unterhalb der Sperrlinie bei 119,5, auf dem Streifen unter den
  Rack-Schrauben.
- **Schrift über dem Ausschnitt** fehlte, und `DECAY` stand mitten im SD-Loch.
  Das fand **keine** Messung — die Freiraumprüfung kannte nur Kreise. Es fiel
  auf, als die Platte zum ersten Mal gerendert und angesehen wurde. Der
  Rechenweg allein hätte das Wort auf die Fräsbahn gedruckt.

Repariert wurde jedes Mal **die Regel, nicht die Beschriftung**. Sie verwirft die
schlechte Position jetzt selbst und nimmt die nächste Kandidatin. Dasselbe Muster
wie beim SHAPE-über-SONG-Fix vom Vortag: die Regel war richtig, ihr fehlte eine
Tatsache.

Dazu zwei Ergänzungen an der Setzregel selbst:

1. **Die Regel prüft beim Setzen gegen bereits gesetzte Wörter**, nicht erst
   hinterher. Vorher war „Schrift auf Schrift" nur ein Gate, das nach getaner
   Arbeit meckerte.
2. **Bildunterschriften werden von unten nach oben gesetzt.** Ein Bedienelement,
   dessen Platz unten durch den Ausschnitt blockiert ist, hat die geringste
   Freiheit und muss vor seinem Nachbarn darüber wählen. In der anderen
   Reihenfolge blieb DECAY heimatlos.

**Ein sechstes Gate wurde geprüft und verworfen.** „Belegung pro Zone zwischen
15 % und 45 %" auf einem 12 × 5-Raster misst Fläche — und eine LED mit 1,5 mm
Radius deckt fast keine, obwohl die Stelle voll aussieht. Das Gate hielt eine
LED-Reihe für leer und zwei große Kappen in einer Zelle für überfüllt. Es misst
das Falsche und kommt nicht in die Spec.

---

## §11 Zwei gemessene Sackgassen

Damit sie niemand ein zweites Mal probiert:

- **Zeilen auf Deckbreite strecken** (Entwurf A) verteilt gleichmäßig und
  zerstört dabei die Gruppen: weitester Abstand *innerhalb* einer Gruppe 41,0 mm
  gegen engste Gasse 10,5 mm. Genau verkehrt herum.
- **PITCH in den Statusstreifen ziehen**, um ihn nach dem Wegfall der Engine-LEDs
  zu füllen, lässt nur vier Gruppen für zwei Bänder. Jede Aufteilung macht ein
  Band zu dünn: **14,96 mm** größtes Loch gegen 12,11 im selben Stand. PITCH
  bleibt unten, der Statusstreifen bleibt schmal.

---

## §12 Bestand nach dieser Runde

| | Anzahl |
|---|---|
| Potis groß (8,5 mm) | 18 |
| Potis klein (6,0 mm) | 46 |
| Taster | 4 (REC ×2, SHIFT, MOD) |
| Buchsen | 18 |
| LEDs | 10 (vorher 20) |
| SD-Ausschnitt | 1, bei 152,4 / 111,0 |
| **Fußabdrücke gesamt** | **96** (vorher 108) |

Die Zahl der großen Kappen bleibt bei 18, obwohl SEND in die Mitte gewandert ist
und ENGINE als kleiner Poti dazugekommen ist.

---

## §13 Bewusst nicht entschieden

- **Die CV-Buchse steht nicht mehr unter ihrem Knopf.** Sobald Gruppen Blöcke
  sind, landen die vier CV-Ziele so, dass zwei von ihnen 7,5 mm auseinander
  liegen — zwei Buchsen brauchen 9,5. Die Ausrichtung ist nicht unbequem, sie ist
  geometrisch unmöglich. Die Buchsen stehen deshalb im gleichmäßigen Raster, in
  der Reihenfolge ihrer Knöpfe, und tragen wieder ihren Namen. Das erfüllt
  Regroup-Spec §4 wörtlich, nur aus einem anderen Grund als dort gedacht.
- **BEND ist in dieser Runde nicht modelliert worden.** Die Doppelbelegung
  (BEND über ATTACKs Knopf, Regroup-Spec §6) fehlt in allen Messungen dieser
  Runde. Ihre zweite Bildunterschrift hat das Schrift-gegen-Schrift-Gate nie
  gesehen. **Vor dem Bauen nachrüsten und neu messen.**
- **Was ein CV-Eingang tut.** Unverändert offen aus der Regroup-Runde.
- **Was MOD und SHIFT tun.** Reserviert, nicht definiert.
- **Ob die Gruppen einen gedruckten Rahmen bekommen.** Nach dieser Runde
  wahrscheinlich unnötig: die Gasse trägt die Trennung, ohne einen Strich zu
  drucken. Endgültig entschieden wird das in der Grafikrunde.
- **Farbe, Schrift, Materialwirkung.** Diese Runde ist Verteilung. Die Optik ist
  eine eigene Runde, druckfähig gedacht: begrenzte Farben, Flächen statt
  Verläufe.

---

## §14 Was diese Spec ersetzt

| Dokument | Änderung |
|---|---|
| `2026-08-10-hw-panel-regroup-design.md` §1 | `CLASS_R` groß 8,0 → **8,5**, klein 5,5 → **6,0** |
| dieselbe §2 | ROOM wird Mittengruppe (SEND A/B ziehen um); PLAY wird **TIME** mit STEPS als Kopf und bekommt RATE und VARY |
| dieselbe §3 | Zeilenrhythmus 15/30/48/66/86/107 → **14,5/34/53/76/95/114**; Blöcke statt klassensortierter Zeilen |
| dieselbe §4 | CV-Buchse „benachbart" → **ausgerichtet im Raster, mit Namen** |
| dieselbe §9 | „Legendendruck und Gruppenrahmen" — Vorentscheidung: die Gasse ersetzt den Rahmen |
| `2026-08-08-...-envelope-design.md` §1 | Größenklassen wie oben; ENGINE wechselt von Taster zu `KNOBI` |
| `docs/hardware/io-budget.md` | ENGINE_A/B von Taster zu Rastpoti; LED-Zahl 20 → 10 |
