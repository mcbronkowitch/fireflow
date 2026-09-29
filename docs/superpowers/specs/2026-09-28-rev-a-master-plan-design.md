# FireFlow Rev A — master plan

**Date:** 2026-09-28
**Status:** approved in conversation (Bastian), to be reviewed as written
**Scope:** the order in which Rev A — the first real FireFlow board — gets
built, from today to H2 (12 Feb 2027). This document decomposes the work; it
designs none of it. Each of the seven sub-projects below gets its own spec and
its own plan.

## Context

- **Rev A is one board** (decided 2026-09-28, roadmap entry of that date):
  everything panel-mounted — pots, jacks, keycaps, LEDs, SD slot — on the
  front of one 60 HP board, the Daisy Patch Submodule plugged into its back.
  The two-board split of the August specs is superseded.
- **The test coupon is done.** Round one answered what it was built for; the
  second coupon turn is dropped. Its measured facts carry into Rev A (see P2).
- **The panel inventory**, measured 2026-09-28 by running the generator
  (`python res/gen_hw_panel.py` from `host/vcv/`):
  `params=75 inputs=12 outputs=6 lights=19 panel=60HP` — 70 pot positions
  plus three keycaps (`docs/hardware/io-budget.md` §3, addendum 2026-09-28).
- **Depth:** the socketed module stands **15 mm** above the board at its
  highest point, USB socket included (measured 2026-09-28). The planned desktop
  case, an Intellijel Palette 62, takes 45.5 mm behind the panel (37.4 mm at the
  outermost HP). Panel-to-board gap and power header are unmeasured.
- **Dates** come from the hardware roadmap
  (`2026-08-07-fireflow-hardware-roadmap-design.md`): H1 6 Nov 2026, Rev A
  order 18 Dec 2026, H2 12 Feb 2027, H3 9 Apr 2027.

## Decisions taken in this round

1. **H1 keeps a laser-cut acrylic plate**, mechanical only: pots, jacks and
   keycaps screwed into it from behind, knob caps on, patch cables plugged
   in, no electronics. It tests reach, cap spacing, and what patch cables
   cover. The panel parts bought for it move onto Rev A afterwards, since
   Rev A's panel parts are hand-soldered anyway.
   Price, quoted 2026-09-28 at formulor.de with a neutral test file of the
   same size and hole count (304.8 × 128.5 mm, 111 round holes, 4
   rectangular cutouts): **20.49 €** incl. VAT for 3 mm opaque black acrylic
   GS (plate P2 4.75 €, laser time 15.74 €), shipping extra and not shown
   before checkout. Acrylic comes in 3, 5 and 8 mm; 3 mm is the choice.
2. **Rev A is generated, as the coupon was** — schematic, placement, routing
   and proofs from Python — with two corrections from the coupon: rendered
   images are looked at every iteration, and the silkscreen must be readable
   (working rules 2 and 3).
3. **JLCPCB assembles the SMD parts.** Bastian hand-solders only the
   panel-mounted parts, with the panel screwed on.
4. **Two tracks in parallel.** The schematic does not depend on where a pot
   sits, only on which pots exist, so the electrical track runs while the
   mechanical track waits for parts and the grip test. Layout starts at the
   freeze.

## The seven sub-projects

| # | Sub-project | Output | Decisions it owns |
|---|---|---|---|
| **P1** | Panel parts and drill file (mechanical track) | Panel-parts BOM; drill file (SVG/DXF) exported from `gen_hw_panel.py`; acrylic and one set of panel parts ordered; grip test; **freeze** | exact pot, jack, keycap/switch and LED types; hole diameters from datasheets; whether pot anti-rotation tabs get a hole or are broken off |
| **P2** | Pin map and circuit blocks | Every module pin assigned; block diagram | 74HC4067 vs 74HC4051; shift-register chain length; MIDI in yes/no (costs a pin); Eurorack power header and reverse protection; which coupon findings carry over (below) |
| **P3** | Schematic generator | Generated schematic, clean ERC, proven netlist, JLC parts only with LCSC numbers | basic vs. extended JLC parts |
| **P4a** | Routing spike (added 2026-09-29) | A proven routing method before P4 starts: one representative Rev A region (e.g. a mux region — 3 muxes, ~24 pots, address lines, COM) routed two ways — the coupon's collision search in `build_pcb.py` extended into a simple router, and Freerouting via KiCad's Specctra export, with the rule-bearing nets (COM, audio, clock) hand-routed and locked. Time, DRC and rendered PNG compared; report in `docs/hardware/routing-spike.md` | which routing method P4 uses; input to 2 vs 4 layers (JLC price looked up, not assumed) |
| **P4** | Layout generator | Placement from the frozen panel coordinates, routing, DRC, JLC BOM and CPL files | module and power-header position (depth); ~~2 vs 4 layers~~ decided 2026-09-29 after P4a: 4 layers, our own router |
| **P5** | Aluminium front panel for Rev A | Print-ready plate from the same generator, ordered with Rev A on 18 Dec | manufacturer; print vs. engraving |
| **P6** | Firmware: panel scan part 2 and control mapping | Scan over the real pin map, every control on its parameter; prepared on the coupon where possible | none beyond P2's |
| **P7** | Rev A bring-up | Checklist (power, every channel, noise, ergonomics); **fault list** | none; P7 only measures |

**Coupon findings P2 carries over** (measured; details in
`docs/hardware/scan-measured.md`, `crosstalk-measured.md`,
`settle-measured.md`, `pots-measured.md`): 10k pots; no capacitor on the mux
`COM`; pots referenced to the module's 3V3 (A10); `SPEED_16CYCLES_5`; one mux
step per audio block (64 ms / 48 ms sweeps); hysteresis H = 16; the LED-word
crosstalk finding on row 7. The 16:1-vs-8:1 choice was left open by the
coupon on purpose — price and availability decide it in P2.

**Dependencies:**

```
P1 ──► freeze (6 Nov) ──► P4 ──► order (18 Dec) ──► P7
P2 ──► P3 ──────────────► P4
P4a (routing spike) ─────► P4
P2 ──► P6 ─────────────────────────────────────────► P7
freeze + P4 ──► P5 ──► order (18 Dec)
```

P5 depends on P4 as well as on the freeze because plate and board must carry
identical hole positions.

## Timeline

All dates Fridays, as in the hardware roadmap.

| Date | Milestone | Sub-project |
|---|---|---|
| **Fri 2 Oct** | This master plan committed | — |
| **Fri 9 Oct** | Panel parts chosen, drill file exported, **acrylic and one set of panel parts ordered** | P1 |
| **Fri 16 Oct** | **Pin map done** — unblocks P3 and P6 | P2 |
| ~Fri 23 Oct | Parts and plate in the house (assumption; P1 checks lead times) | P1 |
| 12 – 30 Oct | **Routing spike**: method for P4 chosen by Fri 30 Oct, while the parts are on their way | P4a |
| 23 Oct – 5 Nov | Grip test; **one** acrylic correction round budgeted (~20 €, a few days) | P1 |
| **Fri 6 Nov** | **H1: panel freeze**; engine feature freeze from 9 Nov (roadmap) | P1 |
| **Fri 13 Nov** | **Schematic done**: clean ERC, every part from JLC's catalogue and in stock | P3 |
| 9 Nov – 11 Dec | Layout, five weeks, rendered every iteration | P4 |
| Fri 27 Nov | Aluminium plate quoted, manufacturer chosen | P5 |
| **Fri 11 Dec** | Layout and aluminium plate done | P4, P5 |
| 11 – 18 Dec | Review week: renders, BOM/CPL, depth against the Palette, second set of panel parts ordered | P4 |
| **Fri 18 Dec** | **Rev A ordered**: JLC with SMD assembly, plus the aluminium plate | — |
| ~Fri 15 Jan | Boards in the house | — |
| 15 Jan – 12 Feb | Bring-up against the fault list | P7 |
| **Fri 12 Feb** | **H2** | P7 |

P6 runs alongside from 16 Oct until bring-up.

**Buffer:** the review week is the only one. If the schematic slips, the
review week shrinks first; 18 Dec holds. **The second set of panel parts** is
there because bring-up should build two boards — one may carry an assembly
fault — and the grip-test parts cover only one.

## Working rules

These bind every sub-project.

1. **One source for coordinates.** `gen_hw_panel.py` is the only source of
   positions. The acrylic drill file, the aluminium plate and the board
   placement are all generated from it, and a guard asserts that every panel
   hole sits over its part's footprint. No coordinate is typed by hand.
2. **Images before numbers.** Every layout iteration renders front and back;
   the renders are looked at before the next iteration starts, and Bastian
   sees them. A green check does not replace looking at the picture.
3. **Readable silkscreen, checked by machine.** Text height at least
   1.0 mm; no text over pads or over other text; reference designators on
   the SMD side at the back, where there is room — the front disappears
   behind the plate. An assembly sheet to solder from, as the coupon had.
4. **Probe rule.** Measured coupon facts carry over and are marked measured.
   Everything else — panel-to-board gap, lead times, prices not yet quoted —
   is written as an assumption until something measures it.
5. **JLC parts only.** Every SMD part carries an LCSC number; stock is
   checked at the schematic freeze (13 Nov) and again before the order.
6. **Reuse the coupon's code, don't copy it.** Its tools (`ksexp.py`,
   `kipcb.py`, the proof steps) move to a shared place; Rev A lives in
   `hardware/reva/`.
7. **Nothing is fixed during bring-up.** Everything found goes on the fault
   list for Rev B (hardware roadmap, Phase 2).
8. **When late, scope goes, not the date.** 18 Dec holds; optional items
   (MIDI, the second board set) go first.
9. **Bastian decides** panel design, anything by ear, and anything costing
   more than about 50 € — presented with a price, not settled for him.

## What this document does not decide

Everything listed under "Decisions it owns" above belongs to its
sub-project's spec. Rev B, H3 and the Superbooth stages are unchanged from the
hardware roadmap.
