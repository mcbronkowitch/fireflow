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

**The line against the other firmware tree**, because the two get confused:
**`bench/`** is the measuring tool — the same linkage, but packed with workload
families, a report transport and measurement arenas, and with full DaisySP
including the LGPL modules, because it never ships. **`shell/`** is the start
of the firmware that will ship. The two share exactly one board-init file,
`src/hw/board.h`, on purpose: if each had its own board init, no comparison
between bench numbers and shell behaviour would mean anything. (The repo root
used to hold a third tree, the upstream Spotykach firmware; it was removed on
2026-09-29 — see [`docs/upstream-firmware.md`](../docs/upstream-firmware.md).)

## Bauen

**Once per clone:** fetch the submodules and build the two libraries, from the
repo root. (This used to be the root `Makefile`'s `make libs` target.)

```bash
git submodule update --init --recursive
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"
make -C lib/libDaisy -j8
make -C lib/DaisySP -j8
```

Eigene Toolchain, ARM GCC über `make`. **Niemals `source env.sh`** — das ist
die Clang-Umgebung für Engine, Tests und Render-Host, und die beiden dürfen
sich nicht mischen.

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"
cd shell && make -j8 images
```

**Never use libDaisy's `make program-dfu` or `all` for this firmware**: they
build and flash the flat, unguarded `shell.bin`. Use `images` and the two
`dfu-util` commands below.

`images` (nicht `all`) ist der richtige Zielname. libDaisys Standardziel
baut ein flaches `shell.bin` über SRAM (`0x24000000`) **und** QSPI
(`0x90100000`) hinweg — rund 17 MB, fast alles Füllbytes, an die falsche
Adresse gezielt. `images` zerlegt stattdessen dieselbe gelinkte ELF in die
zwei physischen Artefakte, genau wie `bench/qspi_tools.py` es für die Bench
tut:

| Artefakt | Inhalt | Ziel |
|---|---|---|
| `build/shell-sram.bin` | everything except the bank and the boot-only code | DFU nach `0x90040000` |
| `build/shell-qspi.bin` | the bank, then the boot-only code | `0x90100000`; flash whenever its md5 changed |

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
dfu-util -a 0 -s 0x90100000 -D build/shell-qspi.bin
dfu-util -a 0 -s 0x90040000:leave -D build/shell-sram.bin
```

Das Board muss dafür im DFU-Modus sein: **RESET drücken, dann BOOT im
Zwei-Sekunden-Fenster.** Anders als die Bench springt der Shell am Ende
nicht selbst in den Bootloader zurück — die Bench tut das, weil sie
wiederholt geflasht wird, der Shell soll laufen. Jedes Neuflashen kostet
also die zwei Tastendrücke.

**Always flash both images, QSPI first**, as above. `shell-qspi.bin` carries
the wavetable bank (`.qspiflash_data`, at `0x90100000`) **and** the boot-only
code that executes in place from QSPI (`.qspi_cold`, at `0x90110000`; the list
and its rules are in `qspi_cold.ld`, the link-time guard is
`qspi_placement.py`). The SRAM image calls into that code at fixed addresses,
so the two images of one build are a pair. **A mismatched pair** -- a new
`shell-sram.bin` over an older shell's or the bench's QSPI contents -- jumps
into whatever lies at those addresses during boot: it hard-faults or hangs
before USB comes up, so the board stays dark and no serial port appears. The
cure is flashing the matching `shell-qspi.bin` (DFU buttons again), then the
SRAM image.

The QSPI command has no `:leave`, so the bootloader stays in DFU mode and the
SRAM command follows directly; only if dfu-util then reports no DFU device,
press RESET, then BOOT within two seconds, and run the SRAM command. The bank
keeps `0x90100000`, where `bench/` and `bench/audition` expect it; coming back
from a bench or audition session, flash both images again before the shell
runs.

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

## Panel scan part 2: Rev A's pin map (P6a)

Spec: `docs/superpowers/specs/2026-10-02-rev-a-p6a-panel-scan-design.md`.

**The step model.** Rev A enables one 4051 per sense pin per step (P2 §3),
so a step reads up to four channels: 24 steps, 48 ms a sweep. `mux_plan.h`
calls this *parallel*. The coupon probes keep the *sequential* model they
were measured with (`kCouponChain`, unchanged). The coupon play image
(`SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1`) runs the coupon's wiring with
Rev A's model (`kCouponPlayChain`): the 4067 and the 4051 live together.

Since P6a, `mux_plan.o` builds with `-Os` and the firmware drops the `kParams`
name strings (`SPKY_NO_PARAM_NAMES`), so a `SHELL_MUX_PROBE` re-run is not
like-for-like with the 2026-08-23 bench numbers.

**The table is generated.** `python shell/gen_panel_map.py` writes
`shell/generated_panel_map.h` from `hardware/reva/panel-map.json`,
`hardware/reva/blocks.py` and `control/params.hpp`. Never edit the header;
`shell_panel_map_guard` regenerates and compares it. Since P6b-1 the 73 rows
split 70 that send the shared control law's id of their own name (the
`FireflowHW` control at that position) and 3 reserved (`ROOT_A`, `ROOT_B`,
`REV_MOD`: pots on the plate that send nothing and have no parameter yet).
P6a's "safe"/"unmapped" split is retired. A pot added to the panel with no
`control/params.hpp` id stops the generator until it gets one or is reserved.

**Keys and LEDs.** Every step reads the 165 in the same pass as the write;
keys debounce over three reads (6 ms). LED bits only ever travel in the latch
that carries the mux address (P2 §4). Since P6b-2 the keys act on their
debounced press and the lamps show the shared LED law (see "Knob ownership,
keys and lamps" below).

**The span** comes from CAL_GND and CAL_3V3 once per sweep; until a sweep has
measured a valid one, no knob reaches the engine.

**Reading it.** The coupon prints part 1's `SHELL_PLAY` line plus
`SHELL_PLAY_IO keys= presses= adc11= adc12=`. Rev A prints eight
`SHELL_PLAY_V r=<first row> <up to ten values>` lines — 73 rows, so the
last line carries three (row order and names are in
the generated header's comments; values ×1000, −1000 = never emitted: no valid span yet, i.e. calibration
never succeeded) and one
`SHELL_PLAY` summary line with key mask and press counts. Both images then
print `SHELL_PLAY_LAW cyc_last=<n> cyc_max=<n> led_last=<n> led_max=<n>
cb_last=<n> cb_max=<n>`, three DWT cycle counters, each as the last block's
value and the worst since the first second (the first 500 blocks are left
out of every maximum, which reads 0 until then, so a cold-cache boot tick
cannot pin it). What each one covers:

- `cyc_*`: `control_tick()` alone, the shared control law (spec
  2026-10-09-rev-a-p6b1 §6: above 9600 cycles, one point, the tick moves to
  every second block).
- `led_*` (since P6b-2): `led_tick()` (the LED law's `fill()`) plus
  `led_word()` and `set_leds()`, counted apart from the law.
- `cb_*` (since the P6b-2 review): the whole audio callback, from before
  `panel_scan_tick()` to after `inst.process()`. It is the only counter that
  sees the scan step and the pot loop (target, ownership, filter), the key
  edges, the law's events, and everything inside `process()` — the engine
  itself, the per-sample deck and input followers behind the lamp observers,
  and FLUX's per-sample phase. `cb_* − cyc_* − led_*` is that remainder; it
  does not include the callback's own entry and exit. **Measured on the coupon, init patch, `-O2`
(2026-10-09): `cyc_last` 11 800, `cyc_max` 13 900** (1.23 and 1.45 points),
over the 9600 budget; the tick stays once per block by decision (spec §6,
"Outcome"). Plan B is the two-block split, triggered by clicks or by
`cyc_max` above 2.9 points on the Rev A board with the full panel.
**Measured on the coupon, P6b-2 image (`7aa0daad`, 2026-10-18, about ten
minutes of playing, `SHELL_PLAY_LAW`):** `cyc_max` 14 976, `cyc_last` about
11 900-12 600; `led_max` 1 580, `led_last` about 1 140-1 280; `cb_last` p50
889 912, p99 916 730, largest seen 922 220 of the 960 000-cycle block. `cb_max`
climbed 945 665, 982 104, 985 308, 1 020 977: at least three overruns past
960 000. The clicks that go with them predate P6b-2 (the P6b-1 image clicked
too, `cyc_max` 13 905), so Plan B's trigger is met and is a round of its own
(`docs/hardware/p6b2-led-and-touch-measured.md`, "Session 2"). Spec §6's own
rule does not fire: 14 976 + 1 580 = 16 556, under the reserve of about 28 000.

**Coupon session** (spec §7) on the coupon play image. The 2026-10-03 session
ran checks 2 and 3 with the P6a gesture "SW1 held, LED_1 lit"; since P6b-2
SW1 is MOD and LED_1 shows `MODBTN_L`, so they read:
1. All three pots reach both stops; at rest their printed values do not change.
2. Press SW1 five times: `presses=5`; each press toggles MOD, and LED_1 shows
   MOD's double pulse while latched, otherwise the master limiter.
3. MOD latched (LED_1 switching), pots untouched: `rv2`/`rv4`/`rv6` do not
   change.
4. Jumper `TP_ADC12` (D9) to `TP_AGND` and `TP_ADC11` (D8) to `TP_A3V3`:
   `adc11` reads near `zero`, `adc12` near `rail`. Swap the jumpers: they
   swap. (The coupon's test points carry its netlist's old D8/D9 names; P2 §2
   corrected them.)

## The shared control law (P6b-1)

Spec: `docs/superpowers/specs/2026-10-09-rev-a-p6b1-shared-control-law-design.md`.

The playing images no longer hand a pot to an engine setter. A pot that
emits writes `lo + v·(hi − lo)` of its parameter's VCV range into a knob
vector (`knob_from_pot()`, rounded for a parameter Rack snaps), and once per
audio block `control_tick()` runs the whole vector through
`control::ControlLaw` — the law `FireflowHW` runs in Rack — so every pot
drives what its VCV twin drives. At boot the vector is the init patch,
depths included, and the law is re-armed as for a patch restore. Rev A's
ATTACK rows carry STAGES as an alternate id (`ControlEntry::alt`, from
`panel-map.json`'s `ids`), written while the deck is on the BBD
(`knob_target()`). The law
builds with `-O2` (`control_tick.o`; it was `-Os` first, like `mux_plan.o`): at `-O3` the Rev A
image kept 2.7 KB of `SRAM_EXEC` free, under the spec's 8 KB floor; with
`-Os` it keeps 10 088 B. The generated tables in `control/params.hpp` being
`inline` and linked once brought that to 11 584 B; the ATTACK alternate and
the `cyc_max` warm-up then cost 376 B, leaving 11 208 B (2026-10-09).
After the retiming setters' unchanged-value early-outs and the repeated
squaring in `ModLane::_update_slew`, which took newlib's double `pow` out of
the image (it had no other caller), 19 592 B are free (2026-10-09). The
law's sent-value cache (a setter is called only when its value changed) and
two reorderings in `mv()` and the depth loop then cost 1 704 B, leaving
17 888 B (2026-10-09). `-O2` for the law instead of `-Os` cost 960 B more
and took the tick from 15 800 to 11 800 cycles: 16 928 B free (2026-10-09).

**Known divergences from VCV (P6b-1, updated in P6b-2)** — spec §8:

- A Sampler deck is silent until REC has recorded something: no factory
  sample. (Rev A's REC A/B keys toggle the record latch; the coupon has no
  REC key.)
- No CLOCK, RESET or CV on the hardware.
- CLK/RST lamps dark until CLOCK/RESET are wired; SHFT has no function.
- The ATTACK pot is two Rack knobs at one position: it writes STAGES
  ("BBD Bend") while its deck's ENGINE is on the BBD and ATTACK otherwise,
  as FireflowHW shows them. Turning ENGINE does not copy the pot's position
  into the newly shown parameter: since P6b-2 that one keeps its last value
  until the pot moves more than `kPotTouch` from where it stood (ownership,
  see "Knob ownership, keys and lamps" below), where Rack shows the other
  knob at its own value.

## Knob ownership, keys and lamps (P6b-2)

Spec: `docs/superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md`.

**Ownership** (`pot_owner.h`). Every parameter has a stored value and the
stored value sounds. A pot writes its knob only after it has moved more than
`kPotTouch` raw counts from its reference reading; then the parameter jumps to
the pot's position and follows it. At boot every pot is released, so the init
patch plays wherever the knobs stand. A pot whose target changes (MOD latched
or unlatched, ATTACK ↔ STAGES) is released by `pot_own()` itself; a `bbd_edge`
from the law sets FLUX to 0 and releases the FLUX pot, and the deck's
`excite_other_deck` turns on, as in VCV. `kPotTouch` (144) is the coupon's
untouched-pot spread through the plan's rule
(`docs/hardware/p6b2-led-and-touch-measured.md`). The per-pot step
(target, ownership, the jump on the taking move, filter, gated write) and the
event application live in `panel_step.h` as pure functions, so ctest runs
them (`tests/test_panel_step.cpp`); `panel_scan.cpp` only calls them.

**Keys** (`apply_key_edges()`), on the debounced press: MOD toggles
`MODBTN`, and while it is latched a pot with a `kModLayer` row edits that
row's depth; REC A / REC B toggle their latches; SHFT is counted and does
nothing. The coupon's one key is MOD.

**Lamps.** After `control_tick()` the firmware runs the shared LED law
(`control/led_law.hpp`, through `led_tick()`) and turns its duties into the
LED word with `led_word()` (`led_out.h`): a lamp of duty d is on d blocks in
every `kLedSteps - 1`, spread rather than bunched. The word reaches the 595s
only through `set_leds()`, once per block. Rev A maps lamps with
`kRevaLedBit`, the coupon puts `MODBTN_L` on LED_1 (`kCouponLedBit`).
`kLedSteps` (8) is the largest step count the coupon showed without visible
flicker (`docs/hardware/p6b2-led-and-touch-measured.md`).

**Memory** (2026-10-18, against the same tree before the wiring):
`SRAM_EXEC` Rev A 246 360 → 250 852 B (12 028 B free, floor 8 192),
coupon 245 840 → 248 964 B (13 916 B free); the LED law's `fill()` is about
2.1 KB of it, and newlib's `powf` (about 1.1 KB) comes in only for the gamma
table's one-time build (in `control_boot()`, before `StartAudio()`, since the
P6b-2 review; it used to happen in the first audio block). `SRAM` Rev A 217 876 → 220 092 B. `.qspi_cold`
16 168 → 16 320 B. The review fixes (`panel_step.cpp`, the depth table, the
`cb_*` counters, the gamma table at boot) then took `SRAM_EXEC` to
251 412 B on Rev A (11 468 B free) and 249 548 B on the coupon (13 332 B
free); `.qspi_cold` 16 312 B (2026-10-18).

## Where the work stands, and where it goes next

*Rewritten 2026-09-28 in English, like everything added to the repo since
mid-August. The section it replaces (German, last touched 2026-08-23) still
pointed at a first pot on a breadboard `74HC4051`; that path was overtaken by
the test coupon. Git history has the old text.*

**What this shell is today: the measuring firmware for the control
hardware.** Since 2026-09-17 it drives the test coupon (`hardware/coupon/`) —
a small board with the real 74HC4067/4051 multiplexers, the 595/165 chain,
seven pots and fixed reference dividers — through a family of probe images,
each behind its own build switch, each with a host reader and a guard, each
written up under `docs/hardware/`:

| switch | what it measures | reader | write-up |
|---|---|---|---|
| `SHELL_COUPON_PROBE=1` (alone) | bring-up: every channel, rescanned live | `read_coupon.py` | `hardware/coupon/README.md` |
| `+ SHELL_SETTLE_PROBE=1` | settle time after a channel change | `read_settle.py` | `settle-measured.md` |
| `+ SHELL_XTALK_PROBE=1` (`SHELL_XTALK_RV4`) | crosstalk from the board's own digital edges | `read_xtalk.py` | `crosstalk-measured.md` |
| `+ SHELL_TONE_PROBE=1` (`SHELL_TONE_DC`) | the codec's own output against a settled channel | `read_tone.py` | `codec-tone-measured.md` |
| `+ SHELL_WAIT_PROBE=1` | the wait between conversions | `read_wait.py` | `wait-measured.md` |
| `+ SHELL_POT_ROUND=1` (on settle or wait) | the pots at mid travel, both questions | `read_pots.py` | `pots-measured.md` |
| `+ SHELL_SCAN_CHECK=1` | the panel scan's pattern: clean at one step per block, with the engine running | `read_scan_check.py` | `scan-measured.md` |
| `SHELL_PANEL_SCAN=1` (± `SHELL_COUPON_PROBE=1`) | the playing image: the scan drives the engine through the board's control table | `SHELL_PLAY` line | — |

The probe switches are mutually exclusive; the Makefile refuses a
combination that makes no sense. Older switches still in the Makefile —
`SHELL_SELFTEST`, `SHELL_CPU_PROBE`, `SHELL_MUX_PROBE`, `SHELL_IDLE_FILL` —
belong to the August engine-on-board work above.

**Two results from August still hold and still steer the design.** The mux
scan goes **in the audio callback**: it costs less than the CPU measurement
resolves, and a foreground scan raised the block-rate tone by 7.3 dB while
the callback left it unmoved (2026-08-23,
[`docs/bench/2026-08-23-978cbaf-shell-mux-placement.md`](../docs/bench/2026-08-23-978cbaf-shell-mux-placement.md)).
And the engine's CPU reserve on this board is **~2.9 points**, measured
directly (`docs/bench/2026-08-19-3def5d5-feed-axi-o2-patch_sm-usb.md`) — no
Seed number may stand in for it.

**What the coupon has settled** (details in the write-ups): the ADC clock is
6.146 MHz, not the 12.29 MHz libDaisy's comments imply; a channel change
settles within 2.0–4.8 µs at pot impedance, measured on real pots; and a
channel read once per audio block at the working sampling rung reads a pot up
to ~650 counts low, which the 387.5-cycle rung removes at every wait.

**And the scan budget is done** (arithmetic,
[`docs/hardware/scan-budget.md`](../docs/hardware/scan-budget.md)): the long
rung is not needed. The ~650 counts come from converting after an idle. The
pattern this shell already runs never idles: libDaisy's free-running DMA,
twelve channels, `SPEED_16CYCLES_5`, `OVS_32`. It reads at pot impedance what
the long rung reads, and it fits one mux step per block, written in the
callback and read one block later, with ~300 µs to spare. Full sweep 64 ms on
the 16:1, 48 ms on the 8:1.

**Next, in order:**

1. **Part 1 of the panel scan is done.** Spec
   [`docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md`](../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md),
   measured on the coupon and written up in
   [`docs/hardware/scan-measured.md`](../docs/hardware/scan-measured.md).
   Board session 2 played `SHELL_PANEL_SCAN` on the coupon, and RV2, RV4 and
   RV6 all moved the engine audibly, and all three reached both stops (RV4's
   low stop in a later read the same day). **Part 2 (P6a) is built**: the
   Rev A scan, the generated table (70 rows at P6a, 73 since the 9 mm panel pass; keyed by global mux 0-9 plus
   channel, unique; the sense pin rides along as a host-tested check field),
   35 pots sending, the four keys on the 165, and the span calibrated from
   two channels. Code space is tight: `SRAM_EXEC` is 99.10 % on the coupon
   play image and 99.35 % on the Rev A play image. The coupon session
   (2026-10-03, `docs/hardware/scan-measured.md`) passed checks 1–3; check 4,
   D8/D9 as ADC_12/ADC_11, is deferred to P7 bring-up by decision.
2. **Skipped on the coupon, by decision (Bastian, 2026-09-28):** round one's
   four RV4 cases (`SHELL_XTALK_RV4=1`) — the scan check already read every
   channel clean in the real pattern, with the engine running — and a second
   populated board, which is worth building only once a control-PCB decision
   hangs on the LED-word finding (`crosstalk-measured.md` row 7). Both remain
   runnable; neither blocks anything.

**The switch headers are written while the Makefile is parsed, not by a
rule** — with a rule, the build produced two byte-identical images for two
switch positions on 2026-08-23, because make has one-second resolution here
and the header landed in the same second as `main.o`. The reasoning sits in
the Makefile above `SWITCH_HEADERS`; whoever tidies it away reopens a
measurement trap. `cmp` two switch positions before flashing either.
