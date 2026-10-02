# FireFlow Rev A — P4-3: fabrication data

**Date:** 2026-10-02
**Status:** approved in conversation (Bastian), section by section
**Parent:** the Rev A master plan
(`docs/superpowers/specs/2026-09-28-rev-a-master-plan-design.md`), sub-project
P4, the layout generator. Follows P4-1
(`docs/superpowers/specs/2026-09-29-rev-a-p4-1-placement-design.md`) and P4-2
(`docs/superpowers/specs/2026-09-30-rev-a-p4-2-routing-design.md`, merged
2026-10-01).

## 1. What P4-3 delivers

P4-1 §1 cut P4 into three parts; P4-3 is the last one: **silkscreen (master
plan working rule 3), assembly sheet, Gerbers, JLC BOM and CPL.** It also
closes P4-1's hand-off: the placed footprints carry no schematic `(path …)`
links, so "Update PCB from schematic" and parity checks do not match them
(roadmap, 2026-09-30; P4-2 spec §8).

P4-3 has two halves:

- **(a) Board content, inside the existing chain.** The committed
  `hardware/reva/kicad/reva.kicad_pcb` stays the one finished board. It gains
  schematic links, footprint fields, DNP flags and a readable silkscreen, and
  KiCad's schematic parity reports nothing against it.
- **(b) Export, a new stage.** `hardware/reva/fab.py` turns the committed board
  into the order package: Gerbers, drill files, the JLC CPL, the JLC BOM, the
  assembly sheet and renders, with its own checks and guard.

Like P4-1 and P4-2, P4-3 runs now, before the panel freeze (6 Nov). The
pipeline and every check are built and green against today's board. The
order-ready package is **not** committed until the panel pass has emptied the
known lists; it is then one re-run (§9).

## 2. Decisions taken in brainstorming (Bastian, 2026-10-02)

1. **Pipeline now, package later.** Everything in §1 is built and runs on
   today's board. Outputs land in `hardware/reva/out/fab/` (gitignored).
   `fab.py --release` writes the committed package to `hardware/reva/fab/` and
   refuses while a known list is non-empty or a rotation entry is unverified
   (§4.4.3). The refusal is not a ctest gate, so ctest stays green before the
   freeze.
2. **Front silkscreen: outlines and polarity only.** The front disappears
   behind the plate and carries only the 112 hand-soldered panel parts.
   Footprint outlines and polarity marks (LED flat, pin 1) stay; reference
   texts are hidden. Which part goes where is on the assembly sheet.
3. **Back references: search, otherwise a list.** Every back-side reference
   gets a free spot near its part found by search. If none exists, it is
   hidden and listed in `NO_ROOM`, a tight known list (a stale or a new entry
   is red). The assembly sheet shows every part.
4. **Approach 1 of three: board content inside the chain.** Schematic links and
   DNP are set at placement; the silkscreen pass is the last step of
   `route.build()`; export is a separate stage. Rejected: a post-processing
   stage that writes the committed board after routing (two board states, and
   `reva_route_guard`'s byte-identity would move to an intermediate file), and
   silkscreen only in the fabrication copy (the committed board would not be
   what gets made, and KiCad parity would stay broken).
5. **Real parity.** The schematic generator writes every Rev A net label as a
   global label, so KiCad names the nets exactly as the board does; the board
   gives unconnected pins KiCad's `unconnected-(…)` net names. KiCad's parity
   check then reports zero and "Update PCB from schematic" changes nothing.
   Fallback, also approved: if the wider global labels do not fit the A3
   sheets (§7), keep local labels and gate parity with an exact naming rule
   (`/<sheet of the symbol>/NAME` ≡ `NAME`, `unconnected-(…)` ≡ no net).
6. **Rule 6 holds for the assembly sheet.** The general part of
   `hardware/coupon/scripts/assembly_plan.py` moves to `hardware/gen/`; the
   coupon keeps its own classes and its output stays byte-identical.

## 3. Measured facts this spec rests on

Probed 2026-10-02 under KiCad 10.0.5 (`kicad-cli`, KiCad's Python) against the
committed board at `91628e8f`, on a scratch copy of `hardware/reva/kicad/` and
`hardware/lib/`. The probe scripts were scratch; the plan's tasks re-derive
every number through the real code.

**Board as committed** (from the file and `out/reva-routed-drc.rpt`):

- 213 footprints. F.Cu: 112 THT panel parts (70 pots, 19 LEDs, 18 jacks,
  4 keys, J_SD). B.Cu: 101 parts — 84 with attribute `smd` (36 R0603,
  23 C0603, 5 C0805, 1 tantalum, 2 D_SMA, 16 SOIC-16, 1 SOT-223), 15 test
  points (`exclude_from_pos_files exclude_from_bom`), U_SM and J_PWR (THT,
  hand-soldered).
- The 84 `smd` parts include the six DNP options (C_LDO_T, C_SENSE0–3, C_SD2);
  `bom-jlc.csv` places 78. **No footprint carries a DNP attribute.**
- References: all visible, 1.0 × 1.0 mm, 0.15 mm stroke (U_SM 1.27 mm), front
  parts on F.SilkS, back parts on B.SilkS. Values on F.Fab/B.Fab. One
  footprint-internal B.SilkS text: U_SM's "INSTALL ON …". No board-level silk.
- DRC silk classes (none gated today, `GATED_DRC` in `route_check.py`):
  `silk_over_copper` 127 (103 of them reference fields), `silk_overlap` 56
  (55 reference fields; 54 entries involve footprint silk shapes on
  B.Silkscreen), `silk_edge_clearance` 38 (36 jack outline segments past the
  edge, 2 jack references). U_SM's "INSTALL ON" text is in 10 entries.
- Vias are tented on both sides (`(tenting (front yes) (back yes))`), so silk
  over a via is not over exposed copper.
- 0 `(path …)` on any footprint; footprint IDs carry no library nickname
  (`C_0805_2012Metric`, not `Capacitor_SMD:C_0805_2012Metric`).

**Gerbers and drill** (`kicad-cli pcb export gerbers` / `drill`, defaults):

- 27 files: 26 layers plus `reva-job.gbrjob`; drill is one combined
  `reva.drl` (1207 coordinate lines). `--excellon-separate-th` exists.
- Two runs four seconds apart differ in all 27 Gerber files and the drill
  file, only in date lines: `%TF.CreationDate,…*%` and `G04 Created by KiCad
  (PCBNEW 10.0.5) date …*` in every Gerber, `; DRILL file KiCad 10.0.5 date …`
  and `; #@! TF.CreationDate,…` in the drill file, `"CreationDate"` in the job
  file. With those lines removed, 0 of 27 Gerber files and the drill file
  differ. `--no-x2` does not help: the date moves into a `G04 #@!` comment.

**Positions** (`kicad-cli pcb export pos --format csv --units mm --side both`):

- 198 rows (213 − 15 test points), header
  `Ref,Val,Package,PosX,PosY,Rot,Side`, side `bottom` for back parts.
- PosY is negated against the board: C1 is `70.62, -57.48` in the file and
  `(70.62, 57.48)` in the DRC report.
- Filters exist: `--smd-only`, `--exclude-dnp`, `--exclude-fp-th`,
  `--bottom-negate-x`, `--use-drill-file-origin`.

**Schematic parity** (`kicad-cli pcb drc --schematic-parity --format json`):

- Committed board: 597 items, 199 each of `footprint_symbol_mismatch` (the
  library nickname), `footprint_symbol_field_mismatch` (missing `LCSC` and the
  other symbol fields) and `net_conflict`.
- With path, full footprint ID, every symbol field and DNP set on each
  footprint (pcbnew API: `SetPath`, `SetFPID`, `SetField`, `SetDNP`,
  `SetSheetname`, `SetSheetfile`, `SetExcludedFromPosFiles` all exist): 199
  items, all `net_conflict`, all naming:
  - 177: the schematic names a net from a local label `/<sheet>/NAME`
    (sheets `chains`, `mux_sense_0`–`3`, `power`), the board `NAME`.
  - 22: an unconnected pin, which the schematic names `unconnected-(…)` and the
    board leaves without a net.
- The schematic has 120 local label names, **none on more than one sheet**,
  and 180 global labels. Turning every local label global merges no nets.
- **Parity matches footprints to symbols by reference, not by path.** The same
  199 items came back with correct paths, with paths that keep the root sheet
  (wrong) and with R1's and R2's paths swapped. Parity therefore cannot prove
  the paths; §5.1 checks them separately.
- KiCad's netlist (`kicad-cli sch export netlist --format kicadsexpr`) gives
  each component `(sheetpath (tstamps "/<sheet uuid>/"))` and
  `(tstamps "<symbol uuid>")` — C1: sheet `565f096c-…`, symbol `7b439dd9-…`.
  The footprint path is their concatenation; the root sheet does not appear.
  The export prints "the schematic has annotation errors" and still writes the
  file; the cause is probed in the next bullet.
- Probed during execution, 2026-10-02: the annotation warning comes from the
  five references without a trailing digit (`C_LDO_T`, `J_PWR`, `J_SD`,
  `U_REG`, `U_SM`). Appended a 9 to each in a copy of the schematic, the same
  export printed nothing. The export exits 0 while warning and writes the full
  file. The netlist holds 213 components, one per board footprint, and every
  one carries `tstamps`, the five bare-named ones included (U_REG:
  `bde6db50-…`). The other four of the 217 schematic references, J_SM1–J_SM4,
  are `(on_board no)` and absent from the netlist, as they are from the board.
  The netlist is multi-line s-expression text; read it with a parser, not a
  single-line regex. `kicad-cli sch erc` on the same schematic reports 3
  violations (`pin_not_driven` 2, `pin_to_pin` 1) and no annotation class.
- 217 schematic symbols carry references, 213 footprints. J_SM1–J_SM4 (the
  module's sockets) have no footprint, and parity did not report them.

**Line endings.** On the development machine (`core.autocrlf=true`)
`hardware/reva/build.py` writes `bom-hand.csv`, `bom-jlc.csv`, `review.md`,
`kicad/fp-lib-table` and `kicad/sym-lib-table` with LF over a CRLF checkout;
`git status` then lists all five as modified with an empty diff. With
`text eol=lf` for those paths in `.gitattributes`, a fresh checkout writes LF
and `git status` stays empty after `build.py`.

**Router.** `route.py:271` skips any net with fewer than two terminals, so the
22 single-pad `unconnected-(…)` nets add no routing work.

## 4. Design

### 4.1 Pipeline and files

```
python hardware/reva/build.py            schematic (global labels), BOMs, review
KIPY   hardware/reva/place.py            placed board; paths, fields, DNP set here
KIPY   hardware/reva/route.py [--write]  routed board; silk.apply() last; commits
KIPY   hardware/reva/fab.py [--release]  order package from the committed board
```

| File | Role |
|---|---|
| `hardware/gen/sch_writer.py` | option: every label global (Rev A on, coupon off) |
| `hardware/gen/kipcb.py` | `link_part` sets path, full footprint ID, fields, DNP, sheet name/file on a placed footprint; `set_pad_net` |
| `hardware/reva/place.py` | computes each footprint's link from the schematic module; names unconnected pads |
| `hardware/reva/silk.py` (new) | the silkscreen pass, a pure function on a board |
| `hardware/reva/route.py` | calls `silk.apply()` after the zone fill |
| `hardware/reva/route_check.py` | new steps §5.1 |
| `hardware/reva/fab.py` (new) | the export stage |
| `hardware/reva/fab_check.py` (new) | its checks, §5.2 |
| `hardware/gen/assembly.py` (new) | label search, overlap check and SVG, from the coupon |
| `hardware/reva/test_fab.py` (new) | `reva_fab_guard` |
| `.gitattributes` | `text eol=lf` for the five generated text files |

### 4.2 Schematic links (board half)

- **Global labels.** `sch_writer` gains a switch that emits every net label as
  a global label. Rev A's `build.py` sets it; the coupon does not, and its
  regenerated schematic must come out byte-identical to the committed one.
  `hw_gen_coupon_guard` compares nets only, so the plan proves the bytes
  itself. The P3
  files are regenerated; `reva_build_guard` and `reva_check_guard` (ERC
  against the waivers) must stay green on the new files.
- **Footprint links.** `place.py` reads, for each reference, the symbol UUID
  and its sheet's UUID from the module that builds the schematic (the uuid5
  keys in `sch_writer.Uuids`), never from a hand-copied table. After all parts
  are placed, `kipcb.link_part` gives each footprint the path
  `/<sheet uuid>/<symbol uuid>`, the full footprint ID (`nickname:name`),
  every symbol field (LCSC, Description, Datasheet …), DNP, Sheetname and
  Sheetfile. (One pass at the end rather than inside `add_part`, which has
  six call sites in `place.py`; amended with the plan, 2026-10-02.) The 15 test points keep
  `exclude_from_pos_files` and `exclude_from_bom`.
- **Unconnected pads.** `place.py` gives each pad whose pin the schematic
  leaves unconnected KiCad's `unconnected-(<ref>-<pin name>-Pad<n>)` net
  (measured form: `unconnected-(J_PWR-Pin_7-Pad7)`,
  `unconnected-(U_IN1-~{Q7}-Pad7)`). The name is composed in `place.py`;
  parity (§5.1) is the independent check that KiCad names it the same.
- **Docstring debt.** `kipcb.new_board`'s docstring says `SetPath` is never
  called and matching is by reference; it is rewritten with this change.

### 4.3 Silkscreen (`silk.py`)

`silk.apply(board)` runs last in `route.build()`, after the zone fill, so
`reva_route_guard`'s byte-identity covers it. For development it runs alone on
`out/reva-routed.kicad_pcb` in seconds (`KIPY hardware/reva/silk.py`), without
a 223 s route.

- **Front.** Every front footprint's reference is hidden. Outlines and
  polarity marks stay as the footprint draws them.
- **Back.** Every back reference is set to 1.0 mm height, 0.15 mm stroke.
  Candidates are generated around the footprint's courtyard: four sides,
  rotation 0° or 90°, growing distance in fixed steps up to a cap. The first
  candidate wins that lies entirely inside the board edge with margin, over no
  mask opening (pads; vias are tented) and over no other silk (text or
  footprint graphic, including references already placed). Placement order is
  fixed (sorted by reference) so the result is deterministic.
- **No room.** A reference without a free candidate is hidden and must be in
  `NO_ROOM`, a module-level set in `silk.py` with one reason comment per
  entry. The plan's first silk run measures the list; it is not guessed here.
- **Footprint-internal text** (U_SM's "INSTALL ON …"): moved like a
  reference if it is free-standing user text, otherwise hidden and printed on
  the assembly sheet instead. The plan measures which applies.
- **What is gated is text**, as rule 3 says: height, and no text over a pad or
  over other silk. The verdict comes from KiCad's DRC, not from the placer's
  own collision model (§5.1), so the check does not grade its own homework.
  Overlap of pure footprint graphics is counted and printed, not gated; JLC
  clips silk off pads in any case.

### 4.4 Export (`fab.py`)

`KIPY hardware/reva/fab.py [--release] [--sabotage NAME] [--out DIR]` reads
the committed board and its project (schematic next to it) and writes to
`hardware/reva/out/fab/`:

#### 4.4.1 Gerbers and drill

- `kicad-cli pcb export gerbers --layers` with exactly eleven layers: F.Cu,
  In1.Cu, In2.Cu, B.Cu, F.Mask, B.Mask, F.Silkscreen, B.Silkscreen, F.Paste,
  B.Paste, Edge.Cuts. Protel extensions (the default).
- `kicad-cli pcb export drill --excellon-separate-th`: PTH and NPTH files.
- **Dates are normalised, not deleted:** each date field in the lines listed
  in §3 is replaced by one fixed constant, so every file stays a valid Gerber
  or Excellon file and two exports are byte-identical. The job file is not
  part of the package.
- `reva-gerbers.zip` is written with fixed member timestamps and sorted
  members, so the zip is byte-stable too.

#### 4.4.2 CPL

- Source: `kicad-cli pcb export pos --format csv --units mm --side both
  --smd-only --exclude-dnp`. Expected 78 rows, the BOM's placements; the plan
  measures it.
- Rewritten to JLC's columns `Designator,Mid X,Mid Y,Layer,Rotation`: layer
  `Bottom` for KiCad side `bottom`, coordinates as KiCad writes them (the
  Gerbers use the same absolute origin), rotation plus the correction of
  §4.4.3.

#### 4.4.3 Rotation corrections (`ROT_FIX`)

JLC's part library does not always share KiCad's zero orientation. `ROT_FIX`
in `fab.py` has one entry per footprint name in the CPL (today R0603, C0603,
C0805, D_SMA, SOIC-16, SOT-223): a correction in degrees and a `verified`
field. `verified` stays empty until Bastian has looked at JLC's placement
preview, which JLC shows on a quote upload, free and without an order; it then
carries the date. **No machine can prove a rotation**, and the table says so.

A per-footprint offset cannot fix a bottom-side sign error, because that error
depends on each part's own rotation. So `BOTTOM_SIGN` (+1 or −1, with its own
`verified` field) is separate: the CPL rotation of a bottom part is
`(BOTTOM_SIGN × KiCad rotation + ROT_FIX[package]) mod 360` (amended with the
plan, 2026-10-02). `--release` refuses while any entry or `BOTTOM_SIGN` is
unverified.

#### 4.4.4 BOM

`fab.py` copies P3's `hardware/reva/bom-jlc.csv` into the package unchanged.
There is no second BOM writer; §5.2 checks the copy against the board.

#### 4.4.5 Assembly sheet

The general part of `hardware/coupon/scripts/assembly_plan.py` — board
reading, label search, overlap check, SVG writing — moves to
`hardware/gen/assembly.py`; the coupon script keeps its own part classes and
calls it. Gate for the move: the coupon's `proof/coupon-assembly.svg` and
`coupon-overview.svg` come out byte-identical.

Rev A gets two sheets, `reva-assembly-front.svg` and
`reva-assembly-back.svg`:

- **Front:** the 112 panel parts, each labelled with its panel id (the
  footprint Value), LED polarity marked.
- **Back:** the hand-soldered parts (U_SM's sockets, J_PWR with pin 1, J_SD's
  pins), every SMD reference including those in `NO_ROOM`, and the DNP parts
  marked as such.

#### 4.4.6 Renders and release

- Front and back renders of the committed board (`kicad-cli pcb render`),
  looked at every iteration (rule 2).
- `order_ready` prints the open items: every entry of the known lists of
  `place_check.py` and `route_check.py`, every `NO_ROOM` entry (informational
  only — the assembly sheet covers them), and every unverified `ROT_FIX` entry.
- `--release` runs every check, refuses on any open known-list entry or
  unverified rotation, and otherwise writes the package to
  `hardware/reva/fab/`: `gerbers/`, `reva-gerbers.zip`, `bom-jlc.csv`,
  `cpl-jlc.csv`, both assembly sheets, both renders.

### 4.5 Line endings

`.gitattributes` gets `text eol=lf` for `hardware/reva/bom-*.csv`,
`hardware/reva/review.md` and `hardware/reva/kicad/*-lib-table`, with a comment
on why (§3). It is committed with the schematic regeneration (§4.2), which
rewrites those files anyway.

## 5. Checks

Same pattern as P4-1 and P4-2: every gated step has a sabotage `<step>` and a
sabotage `<step>_missing` that proves the step is red when it measures
nothing, plus a WHY phrase that must appear on the step's own lines.
`check_sabotage_coverage` enforces completeness. Net names, footprint names and
references come from the modules that build the board, never copied as
literals; thresholds live in the check file.

### 5.1 Board content (`route_check.py`, `reva_route_guard`)

| Step | Green means | Sabotage | `_missing` |
|---|---|---|---|
| `silk_height` | every visible silk text ≥ 1.0 mm | one reference at 0.8 mm | no text examined |
| `silk_front` | no visible reference on a front footprint | one front reference shown | no front footprint examined |
| `silk_clear` | DRC: no `silk_over_copper`, no `silk_overlap` with a text among its items, except entries in a new `KNOWN_PANEL["silk"]` (expected empty once front references are hidden; the plan measures it) | one back reference moved onto its own pad | the DRC report's silk entries not read |
| `no_room` | hidden back references == `NO_ROOM` (judged by `check_kit.judge`) | one entry added; one hidden reference shown | no back footprint examined |
| `paths` | every footprint's path == KiCad netlist's `sheetpath tstamps` + `tstamps` for that reference; every netlist component except those without a footprint has one | R1's and R2's paths swapped | no component read from the netlist |
| `parity` | `--schematic-parity` reports 0 items, **and** a board copy with one field changed (R1's LCSC) reports ≥ 1 (positive control, every run; a path change would not show, §3) | one footprint field changed | the schematic hidden from the run |

The `paths` check exists because parity matches by reference (§3); its
independent reader is KiCad's own netlist export, not our uuid5 function. The
parity positive control exists because a parity run that does not find the
schematic could report zero.

### 5.2 Export (`fab_check.py`, `reva_fab_guard`)

| Step | Green means | Sabotage |
|---|---|---|
| `gerber_set` | exactly the eleven layer files and two drill files, none empty; Edge.Cuts extent == the board outline (300.8 × 110 mm) | one layer dropped from the export list |
| `drill` | hole count in the PTH and NPTH files == PTH pads + vias and NPTH holes counted by pcbnew | one hole line deleted |
| `cpl` | designators: CPL == BOM == board SMD footprints without DNP; every position within 0.01 mm of pcbnew's footprint position (Y sign per §3); every layer `Bottom`; every point inside the Gerber's Edge.Cuts extent | one row dropped; one row shifted 0.1 mm; one row's Y sign flipped; a DNP flag cleared |
| `rot_table` | every footprint name in the CPL has a `ROT_FIX` entry; `BOTTOM_SIGN` is ±1 | one entry removed; `BOTTOM_SIGN` set to 0 |
| `bom_lcsc` | every BOM line's LCSC == the `LCSC` field of each of its footprints | one footprint field changed |
| `assembly` | every part labelled on its sheet, no label overlaps another | one label forced onto another |

Each has a `_missing` sabotage (empty output directory, empty CPL, empty table,
empty BOM, no part read). Determinism is the guard's (§6), not a step.

## 6. Guard and outputs

- `reva_route_guard` (existing) keeps rebuilding the committed board byte for
  byte, now including links and silkscreen, and proves §5.1 red.
- `reva_fab_guard` (new, `test_fab.py`, re-execs under KiCad's Python like
  `test_route.py`): two exports byte-identical after normalisation; every
  §5.2 step green on the committed board; every sabotage red with its WHY
  phrase on its own lines.
- The coupon: a regenerated `coupon.kicad_sch` and regenerated assembly SVGs
  byte-identical to the committed ones prove the label switch and the
  `assembly.py` move. No existing guard compares those bytes
  (`hw_gen_coupon_guard` compares nets); the plan decides whether the
  comparison becomes a permanent guard or a one-time proof in a task.
- Committed now: the board, the regenerated schematic and BOMs, renders under
  `docs/hardware/fab/`. Committed at release: `hardware/reva/fab/`.

## 7. Risks, and what the plan does about them

- **Global labels are wider.** `sch_writer.label_width` charges a global label
  its arrow (`LABEL_PAD`), so sheets grow. The plan's first task probes whether
  all ten sheets still fit A3. If not, the fallback of decision 5 applies, and
  the spec is amended before work continues.
- **ERC on global labels.** KiCad warns "Global label only appears once in the
  schematic". Every local-label net today has at least two pins on its sheet,
  so this should not fire; `reva_check_guard` decides.
- **Annotation warning.** The netlist export warns of annotation errors that
  ERC does not show. Probed 2026-10-02 (§3): the cause is the five references
  without a trailing digit (`C_LDO_T`, `J_PWR`, `J_SD`, `U_REG`, `U_SM`); the
  export exits 0 and every component carries `tstamps`, so the `paths` step
  can rely on it. The warning stays until those references are renamed, which
  this spec does not do.
- **Named unconnected pads change the board.** Pads that had no net now have
  one. The router skips them (§3), but DRC and the routed checks may see new
  items; one re-route measures it.
- **A back reference may have no room in the dense field west of the
  module.** That is decision 3's list, not a failure.
- **JLC assembly of a 300.8 × 110 mm board on the bottom side** is assumed,
  not confirmed, and the price is not quoted. Both are checked on the first
  quote upload, together with the rotation preview; anything above about 50 €
  is Bastian's call (rule 9).

## 8. Not in P4-3

- The panel pass and its re-run (place → route → fab): after the grip test.
- The aluminium plate: P5.
- JLC stock check: at the schematic freeze (13 Nov) and before the order
  (rule 5).
- Silkscreen artwork beyond references, outlines and polarity marks.

## 9. Timeline

- **Now, before 6 Nov:** the whole pipeline; the committed board gains links
  and silkscreen; ctest green with `reva_fab_guard`.
- **After the panel pass:** `place.py` → `route.py --write` → `fab.py`;
  a quote upload to verify `ROT_FIX` and the assembly assumptions;
  `fab.py --release`; order on 18 Dec.
