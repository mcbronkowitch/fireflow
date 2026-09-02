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

Two things need hands on a real board, not a screen, before the fab order
goes out (`docs/roadmap.md`'s M6 section carries the up-to-date status):

1. **The Eurorack bus board** -- pin 1/the red stripe against `-12V`, pins
   3-8 against `GND`, and what actually sits on pins 7/8 (left open here on
   the theory that some bus boards carry +5 V there and grounding it would
   short). This is the one connection on the whole board that comes from
   convention rather than a datasheet.
2. **The Patch Submodule's own land-pattern dimensions** -- vendored from
   Electrosmith's KiCad library
   ([`hardware/lib/README.md`](../lib/README.md)), unproven until a module
   actually seats in a fabricated board.
