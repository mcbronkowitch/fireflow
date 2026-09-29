# Rev A P4a — routing spike

**Date:** 2026-09-29
**Status:** approved in conversation (Bastian), to be reviewed as written
**Parent:** [`2026-09-28-rev-a-master-plan-design.md`](2026-09-28-rev-a-master-plan-design.md), sub-project P4a
**Inputs:** the P3 schematic (`python hardware/reva/build.py`), its mux assignment `hardware/reva/panel-map.json`, the hole list `host/vcv/res/FireflowHW-holes.json`, the coupon layout generator `hardware/coupon/scripts/`
**Deadline:** Fri 30 Oct 2026 — routing method for P4 chosen (master plan window 12–30 Oct)

P4a answers one question before layout starts: **how does P4 route?** One
representative strip of the Rev A board is routed two ways — by a small
router of our own, grown out of the coupon's collision search, and by
Freerouting through KiCad's Specctra export — on identical input and under
an identical proof. The winner is then routed once more on four layers. The
report feeds P4's other open decision, 2 vs 4 layers, together with JLC
prices looked up for both.

## 1. Approach — decided

**A shared harness first, then both routers on it.** One strip generator
builds placement, edge ports, the hand-routed locked nets, the proof chain
and the renders; each router gets exactly the same board and is judged by
exactly the same checks. The harness is P4's skeleton either way.

Rejected:

- **Freerouting first, our own router only if it fails:** cheaper, but the
  master plan asks for two methods compared, and "fails" would be judged
  without a baseline.
- **Our own router only:** no Java dependency, but no comparison either.

**Layer runs:** both methods on **2 layers** (the hard case — if both route
it cleanly, routability is no longer an argument for 4); the winner once
more on **4 layers** with planes. Three runs in total.

## 2. The strip — the common input

### 2.1 Region and outline

The four mux regions are vertical strips over the full panel height
(measured 2026-09-29 from `panel-map.json`). The spike takes the hardest
one, **SENSE_1**: 22 pots on U_MUX3/4/5, pots spanning x 210.6–288.6 mm and
y 14.5–97.0 mm.

**Outline (assumption; P4 owns the real board outline):** x 203–300 mm,
y 6–122 mm, in the frame of `FireflowHW-holes.json`. The west edge is a cut,
not a board edge.

*Amended 2026-09-29 while planning, from a measurement:* the first draft
cut at x 205, but the strip's own RES_B reaches west to x 204.09
(courtyard), so the cut moves to x 203. No straight cut is clean: pads of
neighbouring parts reach east past x 203 whatever the line (by the KiCad
footprints: the mounting tabs of SUB_B, COMP_B, DEPTH_B and PAN_B, the pads
of the MOD3_B jack and the REC_B key). A part's body is on the front and
blocks no routing — only its **pads** do. So every foreign pad that reaches
into the strip becomes a **keepout** (no tracks, no vias) over its pad box
grown by the 0.2 mm clearance, clipped to the strip; the foreign footprint
itself is not placed, so no foreign copper lies on the cut. The harness
computes and prints this list; the one above is the planning probe's.
KiCad's DSN export writes rule areas as `(keepout ...)` per copper layer
(probed 2026-09-29).

### 2.2 Parts

Every part comes from the P3 netlist with its real footprint; no coordinate
is typed by hand (master plan, working rule 1).

- **Front, through-hole, at their panel coordinates:** every hole in the
  strip — 22 pots, 6 jacks (MOD1_B, MOD2_B, GATE_B, PITCH_B, OUT_L, OUT_R),
  7 LEDs, the MODBTN key (list measured 2026-09-29 from the hole list).
  The hole sits on the footprint's F.Fab circle centre (courtyard centre
  where it has none); both are read from the footprint, never typed.
  **Orientation (assumption, P4's call):** pots with their pins pointing
  **north**; jacks, LEDs and the key at 0°. Probed while planning: pins
  south puts five of the strip's LEDs onto pot pins (26 shorts before any
  routing); pins west (0°) pushes RES_B's pads across the cut and its
  13.84 mm courtyard over its 13 mm-pitch neighbours; pins east (180°)
  leaves one decoupler no place within 2 mm of its mux. Pins north puts the
  top row's pins at y ≈ 7 mm — close to the assumed edge and likely in the
  rail zone, which goes to P4 as a finding.
- **Back, SMD, placed by a small search:** U_MUX3/4/5 with one 100 nF each,
  and the 7 LED series resistors. Each mux starts at the centroid of its pot
  group, each resistor at its LED, and walks outward until its courtyard
  clears every through-hole pad (grown by the clearance), every keepout
  and every other SMD courtyard. Front-side bodies do not count: the SMD
  parts are on the back. Each 100 nF sits within
  2.0 mm of its mux's VCC pad (the coupon's decoupling rule). The search exists for the
  spike; for P4 it is a proposal, not a decision.

Parts that sit outside the strip on the real board (shift registers, the
module, the 1 k address/enable series resistors, the key pull-up) are not
placed.

### 2.3 Nets and ports

Every net that reaches into the strip is routed. A net that also leaves
the strip ends at a **port** on the west edge: SENSE_1, MUX_S0-2,
MUX_EN3-5, SM_3V3, GND, the 7 LED nets, the 6 jack nets, KEY_MODBTN. A port
is a small SMD pad on B.Cu, so ratsnest and DRC see it like any other pad.
The ports are one table in the harness, ordered by the y position of their
source in the strip; both methods get this table unchanged.

Net names are imported from `hardware/reva/blocks.py`, never retyped as
string literals (coupon lesson: a checker with duplicated names examined
zero copper and reported green).

### 2.4 Locked nets

The master plan's rule-bearing nets are hand-routed and locked before any
router runs:

- **SENSE_1** — the three mux COM pins to their port.
- **OUT_L, OUT_R** — jack to port.

SR_CLK does not appear in the strip (the shift registers sit elsewhere).

That Freerouting leaves locked tracks from KiCad's DSN export untouched is
an **assumption**. The plan probes it before anything builds on it.

### 2.5 Supply

- **2 layers:** both methods route GND and SM_3V3 as 0.4 mm tracks; after
  routing, both sides get a GND fill.
- **4 layers:** GND on In1.Cu and SM_3V3 on In2.Cu as planes; the SMD pads
  on them are stitched by the via search of the coupon's `build_pcb.py`
  (`_stitch_plane_pads`), which the routers then treat as obstacles.

## 3. The two routers

### 3.1 Common rules

Net classes live on the board and both routers read them from there:
signal 0.25 mm, supply 0.4 mm, clearance 0.2 mm, via 0.6 mm / 0.3 mm drill
(the coupon's values). The locked nets and the ports are already on the
board when a router starts.

### 3.2 Our own router — `hardware/gen/route.py`

- **Core without pcbnew:** obstacle rectangles and pin positions in, paths
  out. Unit-tested like the other `hardware/gen/test_*.py`, each test with a
  demonstrated RED.
- **Grid A\*** on F.Cu and B.Cu, 0.25 mm pitch, 8 neighbours (45° routing),
  a via cost on every layer change. *(Amended 2026-09-29: the plan set
  0.2 mm for finer channels; the spike ran at 0.2 mm.)*
- **Obstacles** are every other-net pad, track and via, inflated by half
  the track width plus clearance — the Minkowski arithmetic of
  `build_pcb.py`'s via search, rasterised.
- **Multi-pin nets** grow as a tree: the next pin connects to the nearest
  point of the tree already routed.
- **Congestion:** negotiated congestion (PathFinder). Overlap is allowed but
  costs; the cost of every contested cell rises each round until no overlap
  is left or the round limit is reached.
- **Output:** grid paths merged into straight segments, written through
  `kipcb`.
- **Deterministic by construction:** no random choice anywhere; ties broken
  by a fixed order.
- **Fast partial check** between full proof runs: only the nets just routed,
  checked against the raster (coupon lesson 2).

### 3.3 Freerouting

The harness saves the board, calls `pcbnew.ExportSpecctraDSN` (present in
KiCad 10.0.5, probed 2026-09-29), runs Freerouting headless from its CLI
with a fixed pass limit — single-threaded if it offers that, for
reproducibility; the exact switches are probed — then
`pcbnew.ImportSpecctraSES`, and hands the board to the same proof chain.

Freerouting and a Java runtime are **not installed** on this machine
(probed 2026-09-29: no `java` on PATH). Both are downloaded — with
Bastian's go-ahead and their exact size — to
`%LOCALAPPDATA%\fireflow-tools\`, outside the repository, and found through
the environment variables `FIREFLOW_JAVA` and `FIREFLOW_FREEROUTING`. Their
versions go into the report. If either is missing, `run.py --method
freerouting` stops with a message naming the variable, never silently.

### 3.4 Time box

Each method gets a fixed budget of agent work — for our own router about
two plan tasks. A method that does not reach ratsnest 0 with zero gated DRC
violations inside its budget has that as its result: it is reported, not
extended.

## 4. Proof, metrics, report

### 4.1 Proof chain — every run

Built on `build_pcb.py`'s steps, shared rather than copied (§5):

1. Board nets equal the strip's slice of the netlist, node for node (ports
   included).
2. Locked nets unchanged: geometry compared before and after routing.
3. Ratsnest 0 — pcbnew and kicad-cli agree.
4. Zero `shorting_items`, `clearance`, `hole_clearance`, `hole_to_hole`,
   `tracks_crossing` — and `track_dangling` (amended while planning: a
   track drawn onto a foreign pad is renamed to that pad's net on save, so
   a short reaches the DRC as a dangling track; probed 2026-09-29).
5. Analog rules, from the coupon's `check_layout.py`: every 100 nF at most
   2.0 mm from its mux's VCC pad (gated — placement controls it). The
   distance from OUT_L/OUT_R to the nearest LED-net track is **measured and
   compared, not gated** (amended while planning): neither router knows the
   coupon's 10.0 mm rule, which the coupon met by hand-routing, so a gate
   would be red for both methods and decide nothing. How each method could
   be *given* the rule — for ours a per-net-pair clearance, for Freerouting
   a Specctra `class_class` clearance if it honours one (probed in the
   plan) — goes into the report as a P4 input. The SENSE_1 COM length is **printed, not gated**: the
   coupon's 15.0 mm limit ran from mux to module, and here COM runs to a
   port whose distance depends on where P4 puts the module. Being locked,
   it cannot differ between the methods anyway; step 2 is its gate.
6. Front and back rendered to PNG.

Every gated rule has a **sabotage mode** and a `*_missing` mode proving its
zero-match guard fires. `pcbnew.KIID.SeedGenerator()` is called once per
process before any board item exists, so a board built twice from the same
source is byte-identical.

### 4.2 What is measured

| Metric | How |
|---|---|
| Routing wall clock | timed inside `run.py` |
| Agent time to the first green run | plan task timestamps |
| Iterations to green | counted |
| Gated DRC classes | proof step 4 |
| Via count, total track length | read off the board |
| Audio clearance (SENSE_1 COM length for the record) | proof step 5 |
| Reproducibility | two runs, `cmp` on the board file |
| The picture | PNGs, looked at |

Renders are sent to Bastian **every iteration that has a result**, not at
the end (master plan, working rule 2).

### 4.3 Price

JLC prices for 2 and 4 layers are looked up on jlcpcb.com's quote page:
5 boards, an **assumed** 295 × 116 mm, FR-4 1.6 mm, default finish, with the
date. Assembly is left out; it costs the same either way. Only the
calculator is used — no login, nothing submitted.

### 4.4 Report

`docs/hardware/routing-spike.md`: the metric table for both methods and the
four-layer run, the renders, the prices, and a **recommendation** for the
routing method and the layer count. Every number carries the command that
printed it. Bastian decides; the layer choice likely crosses the ~50 €
threshold of working rule 9.

## 5. Where the code goes

- **`kipcb.py` moves** to `hardware/gen/kipcb.py` (master plan, working
  rule 6). The DRC, ratsnest and render steps both boards need move out of
  `build_pcb.py` into `hardware/gen/pcb_proof.py`; the coupon imports both
  from there.
  **Guard:** the coupon rebuilds byte-identical after the move. That it
  does so today is a claim from memory, not a measurement — the plan's
  first step probes it.
- **`hardware/gen/route.py`** holds the router core. If it loses, it is
  removed before merge and kept on the tag `attic/route-spike-2026-10`, as
  the repository does with struck work.
- **`hardware/reva/spike/`** holds the harness: `stripe.py` (outline,
  placement search, ports, locked nets) and `run.py` with
  `--method own|freerouting` and `--layers 2|4`.
- **Outputs** go to `hardware/reva/spike/out/`, gitignored — DRC reports and
  exports only churn on timestamps (coupon lesson 8). The PNGs the report
  cites are committed under `docs/hardware/routing-spike/`.
- Branch `reva-p4a-routing-spike`.

## 6. Order

1. Probe: the coupon rebuilds byte-identical today.
2. Move `kipcb.py` and the shared proof steps; the coupon still rebuilds
   byte-identical.
3. Strip harness and proof chain, every rule with its RED.
4. Probes: locked tracks survive DSN export → Freerouting → SES import;
   Freerouting's CLI switches.
5. Our own router, 2 layers.
6. Freerouting, 2 layers.
7. The winner on 4 layers.
8. JLC prices.
9. Report and recommendation.

## 7. Out of scope

The real board outline, the placement of anything outside the strip, the
module and power-header position, the SD socket, silkscreen, BOM/CPL files
and fabrication output — all P4. The spike's placement search is a
proposal for P4, not a decision.
