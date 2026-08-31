# Coupon layout — design

**Date:** 2026-08-31
**Status:** approved in conversation; this document is the written form
**Depends on:** `hardware/coupon/` schematic side (commit `2bd89b6`), the
envelope spec §5 addendum 2026-08-30, `docs/hardware/settle-budget.md`

## 1. What this is

The PCB layout for the test coupon, produced the same way its schematic was:
**generated, not drawn**, with a proof chain that measures every claim the
layout makes about itself. The deliverable is a committed `coupon.kicad_pcb`
plus Gerber/drill files in `fab/`, so that ordering is one upload once the two
hands-on blockers (bus-board measurement, Patch SM land-pattern risk) are
cleared. Ordering itself is **not** part of this design.

Decided up front, in conversation 2026-08-31:

- **Fully generated.** Placement and routing are data in Python files, built
  into a board by a generator running under KiCad's own Python
  (`pcbnew`, verified present: KiCad 10.0.5 at
  `C:\Users\bernd\AppData\Local\Programs\KiCad\10.0`). No interactive layout
  step, no external autorouter. Routing will be functional rather than pretty;
  the four-layer stackup keeps that honest.
- **JLCPCB 4-layer standard rules** as the design-rule floor (see §4).
- **Hand assembly.** All footprints on the board are already the HandSolder
  variants; no assembly service constraints (rotations, position files) apply.

## 2. Board and stackup

From `scripts/design.py`, already committed: **80 × 60 mm, 4 layers.**

| layer | carries |
|---|---|
| F.Cu | components, signal routing |
| In1.Cu | ground planes: **AGND zone** (analog region) and **GND zone** (rest), joined only at `JP_GND` |
| In2.Cu | supply planes: **A3V3 zone** and **+3V3 zone**, joined only at `JP_3V3`; ±12 V is *not* a plane |
| B.Cu | overflow signal routing |

Measured constraint (bounding boxes read from the footprint libraries, not
estimated): the `DAISY_PATCH_SM` landing pattern spans **61.4 × 36.2 mm** of
pads. On 80 × 60 that leaves a ~22 mm strip below the module and ~18 mm at the
sides. The seven pots therefore sit in **two groups of 3–4**, not one row
(a single row at ~12 mm pitch would need 84 mm). If placement turns out
cramped, the board grows toward 100 × 80 — same JLCPCB price tier (≤100 × 100),
one line in `design.py`. Growing the board is a data change, not a design
change.

## 3. Zone plan

The Patch SM sits top-centre, its two pin rows left and right. Below it the
board splits into two domains:

- **Analog, left:** both muxes, the pots in two groups, reference dividers,
  the COM clusters, and the audio jack on the left board edge.
- **Digital, right:** the two 595s, the 165, the eight LEDs with their series
  resistors, the button, and the Eurorack power header at the right upper
  edge near the SM's supply pins.

Ground and supply follow the two solder jumpers that the schematic already
carries (envelope spec §5 point 8):

- In1 has two zones, AGND under the analog domain and GND under everything
  else, connected **only** at `JP_GND`, which is placed as the star point next
  to the SM's ground pins.
- In2 mirrors that: A3V3 under analog, +3V3 under digital, joined only at
  `JP_3V3`. The +3V3 source is the SM's A10 (+3V3 OUT), per the envelope
  spec's ratiometric argument.
- ±12 V runs as short, wide tracks: IDC header → SM supply pins → bulk caps.

**No signal crosses the zone split mid-gap.** Every net that changes domain
(the four address lines, the two enables, COM→sense) crosses at a defined
place beside the star point, so its return current does not have to walk
around the moat.

## 4. Rules the generator enforces

Design-rule floor, JLCPCB 4-layer standard: signal tracks 0.25 mm, supply
0.5 mm, ±12 V 0.8 mm, vias 0.3 mm drill / 0.6 mm outer diameter, clearance 0.2 mm.

Coupon-specific rules — each is a number in the scripts and each is
**measured on the finished board** by `check_layout.py`, not asserted:

1. **COM is sacred.** `MUX16_COM` and `MUX8_COM` each stay under ~15 mm total
   copper, via-free, on F.Cu only. The cluster order along the track is
   mux pin → test point → DNP cap pad → 0R to the sense pin, so the probe
   sees the node rather than the end of a stub. (The settle budget is linear
   in this node's capacitance; the layout must not add an unknown to it.)
2. **Audio gets distance.** `AUDIO_OUT_L`/`AUDIO_OUT_R` run along the left
   edge over the AGND zone, ≥10 mm from every switched digital net
   (`SR_CLK`, LED nets). The 28 dB scan tone is the thing being hunted; the
   layout must not build it in.
3. **Measured channels are controlled.** Pot-wiper tracks to the mux channels
   are routed alike (same layer, similar length); the 0R neighbour ties sit
   directly at the mux pins so "neighbour hard at the rail" is true
   physically, not only electrically.
4. **SR clock is disciplined.** `SR_CLK` is one run, SM → 595 → 595 → 165,
   no stubs, entirely over the GND zone.
5. **Decoupling is placement.** Every 100n within 2 mm of its VCC pin; bulk
   caps directly at the SM supply pins and the IDC header respectively.

## 5. Generator architecture

Three new files under `hardware/coupon/scripts/`, following the schematic
side's pattern (data file → generator → proof):

- **`placement.py`** — pure data: every reference with x/y/rotation/side,
  grouped by the zones of §3. No optimiser; the placement is a readable,
  reviewable table.
- **`routing.py`** — every signal track as an explicit point sequence with
  layer and width, plus small helpers (pad approach, via drop, bus offset).
  Ground and supply pins are not routed here; they connect pin → via → zone.
- **`build_pcb.py`** — the generator. Runs under KiCad's bundled Python,
  builds the board from outline + netlist + placement + zones + routing, then
  runs the proof chain (§6). Any red step aborts the build — the proof chain
  **is** the gate; there is deliberately no separate test runner to leave
  unwired (the `tools/` guards-without-a-runner trap).

`netlist.build()` stays the single source of connection truth for both the
schematic and the board.

## 6. Proof chain

1. **Net comparison, board ↔ intent,** node for node against
   `netlist.build()` — the same load-bearing check the schematic has.
2. **Ratsnest = 0**: no unconnected net, read from pcbnew itself.
3. **`kicad-cli pcb drc --exit-code-violations`** with the §4 rule values.
4. **`check_layout.py`**: the five analog rules of §4, measured from the
   finished `.kicad_pcb` (COM length and via count, audio clearance, zone
   membership of every part, decoupling distances).
5. **Render** to `proof/` (`kicad-cli pcb render`), and `review.py` grows a
   layout section: placement table, measured COM lengths, measured audio
   clearance — the sheet a human reviews.
6. **Gerber + drill export** to `fab/`.

`coupon.kicad_pcb` is committed like `coupon.kicad_sch`: generated, but
checked in, so the repository carries the orderable state.

## 7. Out of scope

- Ordering (blocked on the bus-board measurement; the land-pattern risk is
  documented and accepted — it is what the coupon exists to burn down).
- Scan firmware (Phase-0 Task 6 step 5's bring-up rig, separate work).
- Any panel or mechanical question — the coupon has no panel; pot shaft
  choice explicitly does not matter here.
- Silkscreen artistry: references and the channel numbers of the measured
  channels, nothing more.
