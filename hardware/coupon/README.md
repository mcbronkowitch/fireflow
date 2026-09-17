# The FireFlow test coupon

A 100 x 80 mm, 4-layer board built around one **Daisy Patch Submodule**, whose
sole job is answering the eight measurement points the M6 hardware envelope
spec puts before panel layout can start —
[`docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md`](../../docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md)
§5, addendum 2026-08-30 — chief among them the ADC mux settle time
(`docs/hardware/settle-budget.md`'s prediction, confirmed here rather than on
a breadboard, because the model is linear in node capacitance and a
breadboard's jumper-wire capacitance has no defined value). It is not a
prototype of the instrument: both mux chip footprints, an unpopulated `COM`
capacitor pad, a probe point on `COM`, two pot values side by side, hard-tied
rail neighbours, fixed-divider reference channels, the real 74HC595/74HC165
chain, and the supply topology as a switchable 0R link are the whole circuit.
The full layout act — placement, zone plan, routing — and its reasoning live
in [`docs/superpowers/specs/2026-08-31-coupon-layout-design.md`](../../docs/superpowers/specs/2026-08-31-coupon-layout-design.md).

## Building it

Two generators, run in order, each under a different interpreter:

```
"C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\python.exe" scripts\build.py
"C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\python.exe" scripts\build_pcb.py
```

Both need KiCad's own Python (`pcbnew` is not on the system interpreter's
path) — `build.py` first, because `build_pcb.py` places parts from
`netlist.build()`, the same intent `build.py`'s own proof chain already
checked against the exported schematic netlist. `build.py` writes
`coupon.kicad_sch`, the ERC report and the schematic PDF; `build_pcb.py`
writes `coupon.kicad_pcb` and runs ten proof steps in order, aborting on the
first red one — net comparison, courtyard/shadow placement sanity, plane
connectivity, stitch-copper hygiene, ratsnest, the five analog rules of the
layout spec's Section 4, a final DRC pass (`proof/drc.rpt`), and render +
Gerber/drill export. `scripts/review.py` (same interpreter, once
`build_pcb.py` has written a board — Section 6 of its output reads
`coupon.kicad_pcb` through `check_layout.py`) writes `proof/review.md`, the
sheet meant for actually reading: requirements, channel plan, the Eurorack
connector, BOM, known ERC violations, and the layout section — placement
table, the four numeric analog-rule measurements, and the two families of
measured-but-not-gated deviations (the long 0R ties and the moat-crossing
address bus, both below).

## Stuffing it

`scripts/assembly_plan.py` draws the sheet you actually solder from, because
the fabricated board cannot be read: 0805 parts sit on a 2 mm pitch and their
reference designators need about 5 mm, so the silkscreen overlaps itself and
the analog corner is one grey smear. The board renders under `proof/` have the
same problem for the same reason.

```
python scripts\assembly_plan.py
```

**This one runs under the system interpreter** — it reads `coupon.kicad_pcb`
and nothing else, so no `pcbnew`, no symbol libraries, no KiCad install. Every
number in it is read rather than transcribed: placement, rotation, courtyard
and pad positions come from the footprints, and the part values come from each
footprint's own `Value` property — the same field the BOM in `proof/review.md`
reports. Re-run it after any placement change and the drawing follows; there is
no hand-kept table to drift.

It writes two sheets:

- **`proof/coupon-assembly.svg`** — the working sheet. The whole board, the
  analog corner enlarged, and a stuffing list with one row per reel. All 78
  parts named, coloured by value, with pad 1 marked at its measured position
  (the notch end on the SOIC chips) and a bar on each LED's `_K` pad.
- **`proof/coupon-overview.svg`** — the same geometry at a width that survives
  a narrow column, with eight numbered zones instead of 78 labels. This is the
  one the journal prints, with the assembly sheet linked beside it.

Labels are placed by search, and the script checks its own work: a slot is
rejected if it touches a part body, another label, a leader already drawn or
the board outline, and a leader may not cross a part unless nothing legal is
left. It prints how many labels needed that fallback (one, as of this writing)
and exits non-zero if any part ends up unlabelled or any two labels overlap.

The website keeps its own copies under different names:

```
python scripts\assembly_plan.py --out-dir ..\..\..\FireFlow_Website\public\media\site --prefix fireflow-hw-coupon
```

**`coupon.kicad_pcb` is generated fresh by every `build_pcb.py` run, but the
file is byte-reproducible.** `pcbnew` orders every UUID-keyed item —
footprints, the individual track/via segments sharing a net, and the zone
filler's own bookkeeping — by whatever random UUIDs that run happened to
mint, so an unseeded build reorders on every run even with no source edit
at all (measured, fix-round probe, 2026-09-02: two unseeded builds from the
identical committed source diffed at 26226 lines — `git diff --no-index`,
default context, `| wc -l`; the exact count varies run to run since it
depends on which random UUIDs each pair of builds draws, but it is
reliably five figures).
`kipcb.new_board()` seeds `pcbnew`'s UUID generator with a fixed constant
before creating anything, which makes that whole order reproduce identically
— two from-scratch builds under the same seed produced a 0-diff
`coupon.kicad_pcb`. Regenerating and committing the file is therefore safe
and expected; a real content change is the only thing that should still show
up in `git diff`.

**Every other generated file still shows as modified after each run — that
is expected, and it is timestamps only.** All 32 of them (every gerber,
`coupon.drl`, both board PNGs, `drc.rpt` and `drc-placement.rpt`) embed a
`CreationDate`/wall-clock line KiCad regenerates on every export; the
content underneath is unchanged (confirmed by diffing a gerber with its
`CreationDate`/`Created by ... date` lines excluded: 0 lines differ). Discard
this churn with `git checkout -- hardware/coupon/fab` and, under `proof/`,
every file EXCEPT `review.md` (`coupon-board-front.png`,
`coupon-board-back.png`, `drc.rpt`, `drc-placement.rpt`) — `review.md` is
`review.py`'s own real content, not timestamp noise, and committing it is
the point of that step.

## The two solder jumpers switch one way only

Envelope spec §5 point 8 asks for the supply and ground topology "as a
switchable 0R link, not as a second board variant: the same copy with and
without is the better measurement." **On this board the "without" half of
that does not exist.** `AGND` and `A+3V3` reach the rest of the world
through `JP_GND` and `JP_3V3` and through nothing else — the Eurorack header
grounds pins 3..6 to `GND` (`scripts/netlist.py:139`) and the submodule's A10
feeds `+3V3` (`:97`). Open both jumpers and the analog island is unpowered
and unreferenced; both muxes lose VCC. Point 8 is not met as built, and no
ordering of the measurements recovers it. A second board does not either:
its open state is the same off state.

**What the open state is still good for, and only before the jumpers are
soldered:** meter `TP_AGND` against `TP_GND`, and `TP_A3V3` against
`TP_3V3`. Both must read open. Continuity there means copper crosses the
1.0 mm moat somewhere it should not — a defect in exactly the separation
point 8 is about, and one that is invisible for good once the jumpers are
bridged. A continuity check is an unpowered measurement anyway, so this costs
nothing but the order it is done in.

**Do not power the board with the jumpers open.** It is tempting — the
digital half needs neither of them — but the six address and enable nets run
from `U_SR1` straight into both muxes, whose `VCC` is `A+3V3` and whose `GND`
is `AGND` (`scripts/netlist.py:149`). With `JP_3V3` open that `VCC` floats
while `U_SR1` drives those inputs at 3.3 V, which forward-biases the muxes'
input clamp diodes and powers the chips parasitically through their own
address pins — above the absolute maximum rating of `VCC + 0.5 V`, with
latch-up as the failure mode. The current involved is microamps and it will
very probably survive, but nothing is gained by finding out. Nor can it be
avoided by not running the scan: `~OE` is tied to `GND` and `~SRCLR` to
`+3V3` (`:258`), so the 595 outputs are driven from the instant power is
applied, with whatever the shift register powered up holding. **Meter first,
then bridge both jumpers, then apply power** — in that order, once.

**What does not work is a second join by wire.** The only two places where
both planes are probeable are `TP_AGND` at (10.00, 66.00) and `TP_GND` at
(94.00, 3.00) — 105.0 mm apart, the board's full diagonal. A wire that long
is a component and not a connection, the same trap the `COM` rule below
names.

**A substitute for the lost A/B, neither reviewed nor measured:** change the
link's impedance instead of removing it. A ferrite bead or a small resistor
across `JP_GND`'s pads answers "how good does the single join have to be" at
the designed star point. It costs one rework cycle on a pad, and whether an
0603 lands cleanly on `SolderJumper-2_P1.3mm_Open_Pad1.0x1.5mm`'s 1.3 mm
pitch is unchecked.

**A second board for point 2's "capacitor at `COM`" case need not be fully
stuffed.** It needs the submodule and its sockets, `J_PWR`, the bulk caps,
both jumpers, `U_SR1`/`U_SR2`, one mux, `R_REFA1`/`R_REFA2`, the decoupling
caps and the capacitor under test. No pots, no button, no 165, no LEDs —
LEDs only if the round is chasing noise rather than time, which is the load
they are there for (`scripts/design.py:115`).

## Layer and zone map

4 copper layers, y growing downward from the board's top-left corner:

| layer | carries |
|---|---|
| `F.Cu` | signal routing, both plane nets' stitching vias/tracks |
| `In1.Cu` | ground -- **two filled zones**, `GND` under the digital half of the board and `AGND` under the analog half, joined only at `JP_GND` |
| `In2.Cu` | supply -- **two filled zones**, `+3V3` under digital and `A+3V3` under analog, joined only at `JP_3V3`, sourced from the module's own A10 (+3V3 OUT) |
| `B.Cu` | signal overflow where `F.Cu` is blocked, plus the address/enable bus (see below) |

`±12V` is never a plane -- it runs as short, wide (0.8 mm) point-to-point
track, IDC header -> SM supply pins -> bulk caps, per the layout spec's
Section 3.

The board splits into two domains, full board width each, with a 1.0 mm moat
between them (narrow enough that both solder jumpers' fixed 1.3 mm pad pitch
still straddles it): **digital**, y 1..47.6 mm (the shift-register chain,
LEDs, Eurorack header, bulk caps), and **analog**, y 48.6..79 mm (both
muxes, the pots, reference dividers, the audio jack). This is a horizontal
split, not the left/right one the layout spec's Section 3 describes on
paper -- the Patch Submodule's own pad layout decided it: rotated 180 deg so
its sense pins (A2/A3) and audio pins (B1/B2) face the board's bottom edge,
both analog anchors end up on the module's south side, so analog is
everything below it and digital is everything else. `placement.py`'s module
docstring has the full measured reasoning (probed pad coordinates, why 90/270
deg were never an option).

**Two known deviations are measured, printed by `check_layout.py` and
`review.py`'s Section 6, and deliberately not gated** -- fixing either is a
placement-level rework, not something Task 6 (or any task before an order)
attempts:

- **Six of the eight 0R neighbour ties sit well away from the mux pin they
  carry**, against "directly at the mux pin" of the layout spec's Section 4
  rule 3. The measured spread is a continuum, not a few outliers — only
  `R_LO3` (5.99 mm) and `R_HI1` (8.88 mm) are genuinely at their pin, and
  the three longest (`R_HI3` 20.56 mm, `R_HI4` 22.53 mm, `R_LO4` 22.77 mm)
  are barely further out than `R_HI2` (18.97 mm) and `R_LO2` (20.05 mm).
  Read the table in `proof/review.md`, not a count. The longest three sit
  where they do because the only F.Cu approach within reach also carries the
  8:1 mux's own enable pin, which cannot lose its route.

  **What this is expected to cost — reasoned, not measured.** These ties
  hold the *neighbour* channels at a rail while the measured channel settles
  (`docs/hardware/settle-budget.md` §6). A neighbour tied through 0 R is a
  static node: 22 mm of copper leaves it essentially at the rail, and a node
  held at constant DC is a poor aggressor. Charge injection at the address
  switch decays through the extra trace inductance in nanoseconds, against a
  1.6 µs window. So this is expected to be irrelevant to the experiment the
  coupon exists to run — rule 3 is good hygiene here, not a hard
  requirement. The coupon itself is the instrument that would settle it;
  settle-budget.md §5 names crosstalk as what its model is blindest to.
- **Five of the address/enable bus's six nets cross the moat on the board's
  west edge, 70-74 mm from the star point** `JP_GND`, against Section 3's
  "beside the star point." All six originate at `U_SR1`, which sits in the
  far-west digital column (beside the LEDs) rather than beside `JP_GND` on
  the far east -- the crossing follows the source pin, not a routing choice
  that could have moved it without re-placing `U_SR1`.

The same fix (re-pitching the 8:1's column and its west approach) would
touch both. Nobody has done that work yet.

## Before this board is ordered

**Populate copies, never rework one board** -- the fab ships a handful of
identical boards either way, and any question a stuffed board could answer
by soldering something differently is answered by populating a second copy,
not by reworking the first. **Jumper the digital side freely, but never the
`COM` node or the audio path** -- everywhere else a wire is a connection;
on `COM` or on `AUDIO_OUT_L`/`AUDIO_OUT_R` a wire is a component, and the
whole point of measuring on a board instead of a breadboard is that neither
of those paths carries an unmodelled jumper-wire capacitance.

One thing still needs hands on a real board, and one that used to is now
closed (`docs/roadmap.md`'s M6 section carries the up-to-date status):

1. ~~**The Eurorack bus board**~~ -- **closed 2026-09-02, metered.** This
   was the one connection on the whole board with no part datasheet behind
   it: an IDC 2x5 box header's own drawing is mechanical only, and the
   pinout comes from Doepfer's A-100 standard instead.

   **The standard was read first, and it does answer pins 7/8** -- the
   16-pin ribbon runs in pairs (-12V, GND, GND, GND, +12V, +5V, CV, Gate)
   and the 10-pin connector is its first ten conductors, so 3..8 are all
   `GND` and +5 V starts no earlier than 11/12. `review.py`'s Section 3
   carries the sources. **7/8 stay open regardless**, as insurance against a
   non-standard board rather than against a +5 V that the standard does not
   put there; it costs two ground pins.

   **Then the bench answered the part no document could:** Bastian metered
   the bus board and it follows the standard -- red stripe on `-12V`, `GND`
   on 3..8, `+12V` on 9/10. Polarity was the only failure mode that costs
   the board, and it is confirmed the right way round, so this stops being
   an order blocker. Pins 7/8 stop mattering altogether rather than merely
   reading as expected: they are open here, so whatever the bus carries
   there cannot arrive.

   **The shroud deviates from Doepfer's own advice on purpose.** Doepfer
   recommends against keyed headers on bus boards, because mis-keyed boards
   and cables are common. This board keeps one anyway: a rotated or
   row-shifted plug is +/-12 V into the Patch SM, and the notch is what
   makes that impossible. Its one cost cannot be tested until the board
   exists -- a cable keyed the other way will not seat at all -- so that is
   an assembly-time check, not an order blocker. If the plug does not go in,
   the fault is the key and the answer is a different cable, never a cut-off
   nose.

   **Where the notch actually points, measured from the committed
   `coupon.kicad_pcb` on 2026-09-02** -- so nobody re-measures it: the header
   sits at (88.00, 22.00), rotation 0 deg, its pins running south to
   (90.54, 32.16), so the long axis is north-south. The shroud's outer wall
   is x 84.82..93.72, y 16.90..37.26; its **west** inner wall (x 86.02)
   breaks between y 25.03 and y 29.13, with both break ends running out to
   x 84.82. That gap is the polarising notch: 4.10 mm wide, centred on
   y 27.08, which is the pin-5/6 row and the header's own centre. Pin 1 is
   in the x 88.00 column, the notch side. **So the notch faces west, into
   the board.** That was never a free choice -- the footprint fixes the
   notch against the pin numbering and the numbering comes from the
   netlist; the only free parameter was the rotation, and it is 0 deg.

   **Mechanically this is clear on both sides.** Nothing is placed west of
   the header in the y 17..37 band (the nearest neighbour that way is `SW1`
   at y 8, well north). East of it, `C_BN12`/`C_B3V3`/`C_BP12`/`TP_3V3` sit
   on x 96.5 between the shroud and the board's east edge at x 100, but they
   are flat 0805/testpoint parts passing under a plug that rests on the
   shroud. The ribbon can therefore fold either way: 6.3 mm to the east
   edge, or out over open board to the west.
2. **The Patch Submodule's own land-pattern dimensions** -- vendored from
   Electrosmith's KiCad library
   ([`hardware/lib/README.md`](../lib/README.md)), unproven until a module
   actually seats in a fabricated board.
