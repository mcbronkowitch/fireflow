# FireFlow — Bedienelement-Reduktion für das 60-HP-Panel

**Stand:** 9. August 2026
**Status:** beschlossen. Ergänzt die Hardware-Envelope-Spec vom 8. August
(`2026-08-08-fireflow-hardware-envelope-design.md`); deren §1 (Zählbasis,
Budget, „one in, one out") wird durch dieses Dokument übersteuert, alles
Übrige gilt weiter.

## Entscheidung

Das Panel behält jede Funktion, die Bastian benutzt, und verliert die, die er
nicht benutzt. Wo zwei Bedienelemente dieselbe Größe beschreiben — einmal
gerastert und einmal frei, oder einmal als Modus und einmal als Betrag —
werden sie zu einem Zonen- oder Rastknopf zusammengelegt. Das Kriterium war
**tatsächlicher Gebrauch**, nicht Symmetrie.

| | heute | danach |
|---|---|---|
| Runtime-Parameter | 82 | **68** |
| Physische Positionen | 80 | **66** |
| Taster/Schalter | 13 | **4** (`ENGINE` ×2, `REC` ×2) |
| Menü-Parameter (`HIDDEN_PARAMS`) | 4 | **0** |

16 Positionen fallen weg, 2 kommen in §4 zurück — **netto 14**. Diese 14
werden **nicht** wieder aufgefüllt. Sie sind das Ergebnis, nicht ein Budget.

## §1 Zusammenlegungen

### 1.1 `STEPS` ×2 schluckt den `STEP`-Taster

Ein Rastknopf mit 17 Positionen: **0 = FLOW**, **1…16 = STEP**. Die 1 IST
erreichbar (`configParam(STEPS_A, 0.f, 16.f, …)`, Implementierung, nicht der
ursprüngliche Plan hier) -- harmlos, weil `ModLane::set_step` jeden Wert
`< 1` ohnehin auf 1 klemmt, ein STEP-Deck mit einer einzigen Schrittzahl
also dasselbe Verhalten hätte wie eine explizite 1.

Die Engine-API bleibt unverändert; der Host ruft
`set_step(p, steps > 0, steps)`. Den Modus zeigt die pro Part ohnehin
budgetierte FLOW/STEP-LED.

**Verlust:** das Umschalten bei gemerkter Schrittzahl. Heute kann man die
Schrittzahl im FLOW-Modus vorwählen und dann auf den Takt genau einsteigen;
danach muss man durch die kleinen Schrittzahlen drehen.

### 1.2 `SONG` ×2 schluckt `FORM` und den `NEW`-Taster

`FORM` (5 `Principle`) und `SONG` (7 `SongMode`) spannen 35 Kombinationen —
zu fein für einen 9-mm-Poti und nicht lernbar. Stattdessen eine **kuratierte
eindimensionale Leiter** durch das Raster, von „keine Struktur" nach
„Form und Song zerfallen".

Vorbild und Bauplan ist `M_WANDER` in `engine/flow/taste.h:891`, das FORM und
SONG bereits gemeinsam über fünf Bänder fährt und in Glow abgestimmt ist:

```c
{ P_FORM_A, {{0.f,0.f},{0.f,0.f},{0.f,1.f},{1.f,2.f},{2.f,4.f}} },
{ P_SONG_A, {{0.f,0.f},{0.f,0.f},{0.f,1.f},{1.f,2.f},{2.f,5.f}} } } },
```

**Ein Unterschied zu `M_WANDER`:** dort ist `SongMode::Off` (Index 6) bewusst
ausgeschlossen, weil ein Wander-Regler das Wandern nicht abschalten darf. Der
Hardware-Knob ist kein Wander-Betrag, sondern ein Struktur-Wähler — er
bekommt `Off` auf **Position 0**. „Gar keine Alternation" ist ein legitimer
Ambient-Zustand und muss erreichbar bleiben.

**Entprellung:** `Flow::quantize_hyst` mit `kHysteresisFrac = 0.5`
(`engine/flow/flow.cpp:234`). Die Funktion existiert und wurde gegen genau
dieses Problem gebaut — ihr Kommentar beschreibt, wie die ungeschützte
Variante `P_FORM_A` über einen Hover-Sweep 14-mal umschaltet. Ein ADC an
einer Rastungsnaht macht dasselbe.

**Würfeln:** jeder Rastungswechsel setzt zusätzlich `new_phrase()`. Man dreht
weiter, und die Melodie ist eine andere. Der Mechanismus ist bewusst nicht
erklärend — er muss nicht vorhersagbar sein, nur reproduzierbar reagieren.

**Befund, der die Gruppierung bestimmt:** `ModLane::_wrap_events()` steigt bei
`_melodic && !_step_mode` sofort aus (`engine/mod/lane.cpp:563`), und
`_apply_preroll_work()` verlangt `_step_mode`. **FORM, SONG und NEW wirken
ausschließlich im STEP-Modus.** Bei `STEPS = 0` ist der SONG-Knob wirkungslos.
Das ist kein Defekt der Zusammenlegung, sondern heute schon so — es macht
aber die Panelgruppierung verbindlich: `STEPS` und `SONG` gehören
nebeneinander, und `STEPS = 0` schaltet beide ab. Selbstheilend ist es auch:
`set_step()` setzt beim Eintritt in den STEP-Modus `new_pending = true`
(`lane.cpp:156`), falls noch kein Pattern B existiert.

### 1.3 `GRIT` ×2 schluckt den `SAT`-Taster

Bipolar: **← Reduce · 0 = aus · Saturate →**. `set_grit_mode()` aus dem
Vorzeichen, `set_grit_mix(|x|)` aus dem Betrag.

Braucht eine Mittenrastung oder eine Totzone von ±3 %; „genau 0" ist auf
einem 9-mm-Poti am ADC sonst nicht zuverlässig zu treffen. Die Entscheidung
zwischen Rastung und Totzone fällt am Testcoupon, nicht hier.

### 1.4 `COMP` ×2 wird `LVL/COMP`

`COMP` war faktisch immer ein Lautstärkeregler (Betriebswert 0.5–0.7), weil
die Engines für sich relativ leise sind. Der Knob heißt jetzt, was er tut,
und behält den Kompressor am oberen Ende:

| Knopfweg | Wirkung |
|---|---|
| 0 | still |
| 0 … 0.8 | reines Ausgangs-Gain, kein Kompressor |
| 0.8 … 1.0 | Kompressor mit Makeup, oben der heutige 0.7-Klang |

Die Zonengrenze fällt auf eine Grenze, die die Engine schon kennt:
`Comp::set_amount(0)` ist **bit-exakter Bypass** (`engine/fx/comp.h:17`) — die
untere Zone kostet keine Kompressor-CPU.

Das Gain braucht **keine neue Verstärkerstufe**: `Center` hält bereits
`_g_a`/`_g_b`. Heute kommen sie aus dem MORPH-Viertelkreis; künftig aus
MORPH **mal** dem jeweiligen LVL.

### 1.5 `TIME` ×2 ersetzt `DIV` und `MULT`

Beide beschreiben dieselbe Größe:
`_dt_target = _delay_time * _time_mult` (`engine/fx/flux.cpp:65`), wobei
`DIV` (`set_rate`) die Basiszeit aus der 12-stufigen Tempo-Leiter holt und
`MULT` (`set_time_mod`) sie kontinuierlich um 0.25×…4× dehnt.

Künftig **ein Knob mit 12 Rastungen auf der Teiler-Leiter**. Das Pitch-Bending
bleibt vollständig erhalten — es entsteht ohnehin nur daraus, dass
`_dt_current` zu `_dt_target` gleitet, also biegt jeder Rastungswechsel die
Tonhöhe genau wie heute.

`FXT_FLUX_TIME` bleibt als **Modulationsziel** bestehen; der Panel-Knob
setzte bisher nur dessen Basiswert (`Fireflow.cpp:503`). Der Basiswert wird
auf **0.5** festgenagelt, weil `tape_time_mult(0.5) = 2^0 = 1` der neutrale
Multiplikator ist. Damit erreichen CV und Mod-Lanes die Zeitachse weiterhin —
verloren geht nur das *manuelle, dauerhafte* Verstimmen neben dem Raster.

`LINK` bleibt eigenständig. Es ist trotz seiner Nachbarschaft **keine
Zeitachse**, sondern rhythmisches Ausdünnen: `set_link()` setzt `_thin` und
überspringt Repeats, damit das Echo auf dem Sequencer-Raster sitzt
(`engine/fx/flux.cpp:126`).

### 1.6 `COUPL` schluckt den `SYNC`-Schalter

`SYNC` ist kein zweiter Parameter, sondern das rechte Ende der COUPLE-Achse.
Der Beleg steht in `Center::update()`:

- **SYNC aus (freie Welt):** Kuramoto-PLL — geometrische Mittelung plus
  Phasenzug zwischen den Decks. Ab `_couple > 0.5` kommt Grid-Gravitation
  dazu, das Paar wird auf die nächste Teiler-Sprosse und aufs Transport-Raster
  gezogen.
- **SYNC an (Grid-Welt):** Melodie und Steps hängen am Transport. `COUPLE`
  regelt nur noch, wie streng die Textur-Lanes folgen: `texture = 1 - _couple`,
  bei vollem COUPLE ist das DRIFT-Ratenwandern vollständig unterdrückt.

Der Knob bekommt deshalb **zwei Zonen, beschriftet `FREE | GRID`**, und jede
Hälfte fährt `couple` von 0 nach 1:

```
|<--- FREE:  couple 0 … 1 --->|<--- GRID:  couple 0 … 1 --->|
 unabhängig   PLL   Gravitation    Textur frei      Lockstep
```

Die Achse insgesamt liest sich als „wie viel gehört dem Transport". Der
Übergang in der Mitte ist weich, weil links davon „frei, aber hart verkoppelt"
und rechts davon „am Raster, aber Texturen frei" beides Lock-Zustände sind.

**Ausdrücklich verworfen** wurde die naive Variante „ganz rechts = SYNC an":
sie tötet die gesamte Grid-Welt unterhalb von COUPLE-Maximum, also den
Zustand „Steps sitzen auf dem Raster, aber die Texturen atmen". Mit der
Zonenlösung geht kein musikalischer Zustand verloren.

### 1.7 `DRIFT` schluckt den `SETL`-Taster

`DRIFT` bleibt ein eigener Knob. **Ganz links (unter ~2 %) feuert einmalig
`Center::settle()`.**

`settle()` ist bereits zu großen Teilen „DRIFT auf null": es setzt
`_drift_target = 0.f`, lässt den OU-Walk über ~1,5 s ausgleiten und ruft auf
beiden Decks `SuperModulator::settle()`, das EVOLVE und Kick über ~1 s
herunterfährt (`engine/center/center.cpp:324`). Der Taster lag also
ohnehin am Ende dieser Achse.

**DRIFT wird NICHT mit COUPLE zusammengelegt**, obwohl das ursprünglich
vorgeschlagen war. Der Code zeigt, dass die beiden orthogonal sind:
`w = _weather * _drift` speist Shape- und Tune-Offsets unabhängig von COUPLE
und SYNC; nur das Raten-Wandern dämpft COUPLE in der Grid-Welt. Auf einem
Knob würde das rechts doppelt gezählt, und die beiden nützlichsten Ecken —
*lose aber verkoppelt* und *fest aber driftend* — fielen weg. Der eine
gesparte Knob ist das nicht wert.

## §2 Feste Werte statt Bedienelement

| Parameter | fest auf | Begründung |
|---|---|---|
| `PUSH` (`MASTER_DRIVE`) | 0.40 | steht im Betrieb immer dort; sobald der Limiter reitet, steuert DRIVE ohnehin keinen Dreck mehr (Memory `spotykach-master-drive-is-a-threshold`) |
| `WOBL` (`REV_MOD`) | 0.15 | nach Gehör festgelegt |
| `SMEAR` (`REV_SMEAR`) | 0.30 | nach Gehör festgelegt |

## §3 Ersatzlos gestrichen

- **`SPOT`** — der einzige echte Feature-Verlust dieses Designs.
  `SuperModulator::spot()` stößt jede Lane außer PITCH an; die Geste
  verschwindet vollständig, auch aus der Engine-Oberfläche des Panels. Die
  Engine-Methode bleibt (Render-Host und Szenarien nutzen sie).
- **`DRIVE_A` / `DRIVE_B`** — Parameter, Kontextmenü-Eintrag und Patch-Feld
  entfallen. **Sie tun heute nichts:** angelegt in `Fireflow.cpp:389`, im
  Patch gespeichert, im Menü als Slider angeboten (`:1471`) — und nie an die
  Engine gegeben. `Instrument` hat keinen Per-Part-Drive; `set_master_drive`
  ist der Limiter. Der Kommentar bei `:508` sagt es selbst („no FLUX
  destination in movement 3"), `:1467` nennt den Grund: DRIVE verlor seinen
  Panelplatz an DRAG (Spec 2026-07-28) und wurde nie neu verdrahtet.
  Die Streichung spart **keine CPU** — sie kostet schon keine. Sie entfernt
  ein Bedienelement ohne Wirkung, also eine Falle für spätere Sessions.

**Es gibt keine ALT-Ebene und keine PLAY-Pads.** Beide stehen nur in der
Budget-Tabelle der Envelope-Spec §1 und in älterer M6-Prosa von
`docs/roadmap.md`, die aus der 42-HP-Fader-Welt stammt. In `gen_panel.py`,
`gen_hw_panel.py`, `host/vcv/src/` und `shell/` kommt `ALT` null mal vor. Auf
einem Knob-per-Function-Panel wäre eine ALT-Ebene auch ein Fremdkörper: sie
führt einen Modus in eine Oberfläche ein, deren Punkt es ist, keine zu haben.

## §4 Zurück aufs Panel: `DETUNE` ×2

`DETUNE_A/B` verlässt das Kontextmenü und wird ein echtes Performance-Element
— bei Drones ist eine größere Reichweite stark. Damit ist `HIDDEN_PARAMS` leer.

Die Reichweite wächst **nur für Synth und Wave**. Der Mechanismus folgt aus der
Typstruktur: `SynthEngine`, `WaveEngine` **und** `BodyEngine` sind drei
Instanzen desselben `SynthEngineT` (`engine/synth/synth_engine.h:167-179`) und
teilen sich damit `SynthEngineT::kDetuneCeilCt`. Diese Decke ist also bereits
der Faktor von Synth und Wave — sie wird angehoben, und BODY wird an seiner
eigenen Stelle gegenkompensiert:

```c
SynthEngineT::kDetuneCeilCt   35.f -> 105.f   // synth_engine.h:40
BodyVoice::kDetuneScale        4.f ->   4/3   // body_voice.cpp:57
BodyVoice::kDetuneMaxCt      140.f -> 140.f   // unverändert: 105 * 4/3
```

`BodyVoice` wendet seinen Faktor an der Verwendungsstelle an und nicht in
`set_detune_cents()` — begründet in `body_voice.cpp:53`, damit
`detune_cents()` auf jeder Engine dieselbe Zahl meldet. Genau deshalb ist die
Gegenkompensation dort eine Ein-Konstanten-Änderung.

**BBD und Sampler bleiben bei 35 ct.** Beide haben ihre eigenen Wege —
`BbdEngine::set_detune()` und `sampler_config.h:316` — und werden nicht
angefasst. Der dortige Kommentar „matches the synth" wird dabei falsch und
muss mitgeändert werden. Das ist eine Entscheidung, keine Auslassung: die
größere Reichweite ist für die beiden Synth-Engines gedacht.

Der Panel-Knob bekommt eine **quadratische Kennlinie**, damit die ersten
~20 ct — wo das feine Schweben lebt — nicht auf ein Fünftel Knopfweg
zusammengedrückt werden.

Kollisionsprüfung: `set_voice_detune()` (Knob → Synth/Wave/Body/BBD) und
`Part::set_detune_cents()` (DRIFTs Wetter, `center.cpp:139`) sind
verschiedene Wege und stören sich nicht.

## §5 Bewusste Verluste

Damit sie dokumentiert sind und nicht später als Fehler gemeldet werden:

1. **Sofort-Gesten.** Von 13 Tastern bleiben 4, und die Hälfte davon ist
   Moduswahl (`ENGINE` ×2). `NEW`, `SPOT`, `SETL`, das STEP-Toggle und das
   SAT-Toggle sind keine Ein-Klick-Aktionen mehr.
2. **`SPOT` ganz** (§3).
3. **Dauerhaftes Verstimmen des Flux-Delays neben dem Raster** — die
   MULT-Oberfläche der Spec vom 2. August. Als Modulationsziel bleibt sie
   erreichbar (§1.5), als Handgriff nicht.
4. **Die Reverb-Dreiteilung schrumpft auf `DIFF`.** SMEAR und WOBL werden
   Konstanten. Das widerspricht der bewussten Aufteilung, die Memory
   `spotykach-reverb-mod-split` festhält — der Eintrag wird korrigiert, sonst
   schlägt eine spätere Session die Aufteilung wieder vor.
5. **STEP-Umschalten bei gemerkter Schrittzahl** (§1.1).
6. **Zonenknöpfe brauchen Rastung oder Totzonen.** Betrifft `GRIT` (Mitte),
   `COUPL` (Zonengrenze), `LVL/COMP` (0.8), `DRIFT` (linker Anschlag),
   `STEPS` (0). Auf 9-mm-Potis am ADC ist das ein Coupon-Thema, kein
   Panel-Thema.
7. **`LVL/COMP`s oberes Fünftel deckelt die Kompressor-Menge.** Die obere
   Zone (ab 0.8) mappt linear auf 0..`kCompTop` (0.7), nicht auf 0..1 --
   Kompressor-Beträge über 0.7 sind vom Panel aus nicht mehr erreichbar.
   Beabsichtigt (0.7 war die Arbeits-Obergrenze des alten Knopfs), aber bis
   jetzt hier nicht aufgeführt.

Patch-Kompatibilität ist ausdrücklich **kein** Thema
(Memory `fireflow-dev-alpha-no-patch-compat`).

## §6 Was dieses Design nicht entscheidet

- **Die Neugruppierung.** Dieses Dokument sagt, *welche* Bedienelemente es
  gibt, nicht *wo* sie sitzen. Der Prozess der Envelope-Spec §4 (Hardware-Modus
  in `gen_panel.py`, Iterationen in Rack gespielt, Deckel bei 3 Runden) läuft
  unverändert — jetzt nur mit 66 statt 80 Positionen und damit erheblich mehr
  Luft.
- **Rastung oder Totzone** bei den Zonenknöpfen (§5.6) — gehört auf den
  Testcoupon.
- **Ob die 14 freien Positionen Luft bleiben.** Vorgabe dieses Dokuments:
  ja. Wer sie füllen will, braucht ein eigenes Argument.

## §7 Nachzuziehende Dokumente

| Dokument | Änderung |
|---|---|
| Envelope-Spec 08.08. §1 | Zählbasis auf 68/66 korrigieren; „one in, one out" streichen (gegenstandslos, das Panel hat Luft); ALT und die zwei PLAY-Pads aus der Taster-Zeile entfernen — sie existieren nicht |
| Envelope-Spec 08.08. §1 | `HIDDEN_PARAMS`-Absatz ersetzen: DETUNE kommt aufs Panel, DRIVE wird gelöscht; die „Entscheidung fällt in der Neugruppierungsrunde" ist damit gefallen |
| Envelope-Spec 08.08. §2 | LED-Tabelle prüfen: die GRIT-Modus-LED wird durch den bipolaren Knob überflüssig |
| `docs/roadmap.md` M6 | ALT-Gesten-Prosa als überholt markieren (42-HP-Fader-Welt) |
| Memory `spotykach-reverb-mod-split` | SMEAR und WOBL sind auf Hardware Konstanten |
| Memory `spotykach-hardware-constraint` | „one in, one out" ersetzen durch den Stand dieses Dokuments |
