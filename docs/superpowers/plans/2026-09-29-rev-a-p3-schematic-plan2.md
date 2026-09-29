# Rev A P3 — the Rev A schematic (plan 2 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate the complete Rev A schematic — ten sheets under an overview — with a clean ERC, a proven netlist, a JLC BOM and a review sheet, all from Python and all guarded in ctest.

**Architecture:** Plan 1 built the shared tools (`hardware/gen/`) and the pot assignment (`hardware/reva/assign.py`). This plan first closes the gaps plan 1's final review found in the writer and the checks (Tasks 1–2), then writes Rev A's part catalogue and circuit blocks (Tasks 3–4), and finally the BOM and review sheet (Task 5). `hardware/reva/build.py` is both the check tool's project file and the command that writes the committed outputs.

**Tech Stack:** Python 3.14 (`C:/Python314/python.exe`, ctest's interpreter), `kicad-cli` 10.0, PyMuPDF (`fitz`).

**Spec:** [`docs/superpowers/specs/2026-09-29-rev-a-p3-schematic-design.md`](../specs/2026-09-29-rev-a-p3-schematic-design.md) — read its two 2026-09-29 addenda at the end; they override §6 where they differ. Plan 1 ([`2026-09-29-rev-a-p3-schematic.md`](2026-09-29-rev-a-p3-schematic.md)) ends with the "Carried into plan 2" list this plan works through. Pin map and circuit blocks: [P2 spec](../specs/2026-09-29-rev-a-p2-pin-map-design.md) §2–§5.

## Global Constraints

- Never prefix a shell command with `cd`; no shell writes (`>`, `tee`, `sed -i`, `cp`, python heredocs that write files) — files are written with the Write/Edit tools. Never chain `git add` and `git commit`.
- Everything written into the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Coupon artefacts (`hardware/coupon/*.kicad_*`, `fab/`, `proof/`, `*-lib-table`) are not regenerated or edited.
- Tests are plain scripts; the exit code is the verdict. Each new guard goes into ctest and is shown RED once; a RED sabotage is reverted with the Edit tool, never `git checkout`.
- `kicad-cli` comes from `gen.ksexp.KICAD_CLI`; a missing `kicad-cli` fails a test, never skips it.
- Text on every sheet 1.27 mm; paper A3; UUIDs derived, never random.
- Net names come from P2 and live once, in `hardware/reva/blocks.py`. Checks never retype a Rev A net name.
- Every SMD part carries an LCSC number from the spec addendum's table; panel parts carry `Source="Thonk"`; exactly one part (the SD socket) carries the open footprint `P4`.
- Generated files (`hardware/reva/kicad/`, `panel-map.json`, the BOMs, `review.md`) are never edited by hand; `python hardware/reva/build.py` writes them and a guard compares them byte for byte.
- After adding a ctest entry: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, then `ctest --test-dir build -R <name> --output-on-failure`.
- The check loop while building a sheet: `python hardware/gen/check.py --project hardware/reva/build.py --fast` after every edit, `--sheet <name>` after every finished sheet (then open the printed PNG with the Read tool), `--full` at the end of the task.

## Facts measured for this plan (2026-09-29)

- Symbols and pins (probed with `gen.netlist.load`):
  - `Connector_Audio:AudioJack2_SwitchT` pins are numbered `T`, `S`, `TN`, with **empty names**. Use `by_number`.
  - `Switch:SW_Push_DPDT` pins 1–6 are named A/B/C twice. Use `by_number`.
  - `Connector:Micro_SD_Card`: 1 DAT2, 2 DAT3/CD, 3 CMD, 4 VDD, 5 CLK, 6 VSS, 7 DAT0, 8 DAT1, SH SHIELD.
  - `Regulator_Linear:AMS1117-3.3` extends `AP1117-15`: 1 GND, 2 VO (power_out), 3 VI.
  - `Diode:SS14` extends `SB120`: 1 K, 2 A.
  - `74xx:74HC165` extends `74LS165`.
  - `74xx:74HC4051` single unit: COM is named `A`, the channels `A0`–`A7`, VEE is pin 7 and GND pin 8. Both are on the bottom edge, 2.54 mm apart.
- `Daisy-Boards:Daisy_Patch_SM` pin types:
  - A10 `+3V3` is **power_in**, so the `SM_3V3` net needs a PWR_FLAG.
  - A4 GND is power_out, so GND needs no flag.
  - B7 is open_collector.
  - D10 `SPI_SCK` is **output**, which clashes with the 165's Q7 on `SR_DIN`.
  - A6, A8 and A9 stay unconnected.
- Footprint pads:
  - Thonkiconn `Jack_3.5mm_QingPu_WQP-PJ398SM_Vertical_CircularHoles` has pads T, S, TN, which match the symbol.
  - `LED_THT:LED_D3.0mm` has pads 1 and 2 (1 = K).
  - `IDC-Header_2x05_P2.54mm_Vertical` has pads 1–10.
  - The SMD footprints all exist in KiCad 10: `SOIC-16_3.9x9.9mm_P1.27mm`, `SOT-223-3_TabPin2`, `D_SMA`, `R_0603_1608Metric`, `C_0603_1608Metric`, `C_0805_2012Metric` and `TestPoint_Pad_D1.5mm`.
  - `hardware/lib/DaisyKiCad/Daisy-Boards.pretty/DAISY_PATCH_SM.kicad_mod` exists.
- Thonk button footprint:
  - Downloaded 2026-09-29 to `C:/Users/bernd/AppData/Local/Temp/claude/C--Users-bernd-Documents-AI-FireFlow/79a8a5ca-0ce5-4c6c-87ce-4372f548562f/scratchpad/thonk-lp/SW_Push_LP_Button.kicad_mod`.
  - Source: https://www.thonk.co.uk/wp-content/uploads/2024/08/THONK-SW-Push-LP-Button.zip. Bastian allowed the download.
  - Six pads, 0.7 mm drills. Pushed, 1–2 and 4–5 close.
- The P1 hole ids are the P2 net names for the jacks (`IN_L` … `OUT_R`). The keys are `REC_A`, `REC_B`, `MODBTN`, `SHIFTBTN`. There are 19 LED ids, and the SD slot is id `SD`, kind `sd`.
- The coupon's ERC reported `pin_not_driven` on the 595's SER, which was fed by the module's open-collector B7. It also reported `pin_to_pin` between the 165's Q7 and the module's D10. Rev A wires both pairs the same way.
- ERC probe (scratch project, KiCad 10):
  - A 74HC4051 S0 input fed only through a 1 k resistor from a 595 output raises **no** violation, because KiCad counts a passive pin as a driver.
  - A 595 SER fed only by the Patch SM's open-collector B7 **raises `pin_not_driven`** (`Symbol U1 Pin 14 [SER, Input, Line]`).
- JLC types are from the spec addendum. One addition, checked 2026-09-29: C45783, 22 µF 25 V X5R 0805, Samsung CL21A226MAQNNNE, Basic.

---

### Task 1: Writer — adjacent power pins, DNP/BOM/board flags, derived symbols

**Files:**
- Modify: `hardware/gen/netlist.py` (`Part.__init__`, new `Part.etype`)
- Modify: `hardware/gen/sch_writer.py` (`label_groups`, `label_margins`, `_boxes`, `_symbol`, `emit_items`, `lib_symbols`)
- Modify: `hardware/gen/fixtures/demo.py` (a 74HC4051 and a 2×5 header)
- Modify: `hardware/gen/fixtures/demo-erc-waivers.txt` (only if the derived-symbol fix makes its line stale)
- Modify: `hardware/gen/test_sch_writer.py` (drawing assertions)

**Interfaces:**
- Produces:
  - `Part(..., panel=False, dnp=False, in_bom=True, on_board=True, pin_types=None)` and `Part.etype(number) -> str`.
  - `label_groups(part, unit, px=0.0, py=0.0, power=())` returns a list of `((cx, cy), angle, net, pin_numbers, other_points)`. `other_points` lists the connection points of adjacent same-power-net pins joined to this one.
  - The writer emits `(in_bom ..)`, `(on_board ..)` and `(dnp ..)` from the part.

- [ ] **Step 1: The demo gets the collision case (RED first)**

In `hardware/gen/fixtures/demo.py`:
- In `_logic_sheet()`, give `u1`'s QB the net `DEMO_EN`, which drops `"QB"` from the `no_connect` list.
- Add after `c3`:
```python
    u3 = Part("U3", "74xx:74HC4051", "74HC4051", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm",
              lcsc=FX, domain="analog", strict=True)
    u3.by_name("VCC", "A3V3").by_name("VEE", "GND").by_name("GND", "GND")
    u3.by_name("A", "DEMO_SENSE").by_name("~{E}", "DEMO_EN")
    for i in range(3):
        u3.by_name("S%d" % i, "GND")
    u3.by_name("A0", "POT_W")
    for ch in range(1, 8):
        u3.by_name("A%d" % ch, "GND")
```
  and return `[u1, u2, rv1, c3, u3]`.
- In `_power_sheet()`, add a sense output and a Eurorack-style header next to `j3`:
```python
    j4 = Part("J4", "Connector_Generic:Conn_01x02", "sense out", FP_HDR2, source="fixture")
    j4.by_number(1, "DEMO_SENSE").by_number(2, "GND")
    j5 = Part("J5", "Connector_Generic:Conn_02x05_Odd_Even", "bus",
              "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical", source="fixture", strict=True)
    for n in (1, 2):
        j5.by_number(n, "-12V")
    for n in (3, 4, 5, 6):
        j5.by_number(n, "GND")
    for n in (9, 10):
        j5.by_number(n, "+12V")
    j5.no_connect(7, 8)
```
  and add `j4, j5` to `parts`.

Run: `python hardware/gen/check.py --project hardware/gen/fixtures/demo.py --sheet logic --out "C:/Users/bernd/AppData/Local/Temp/p3p2-demo"`
Expected: FAIL with at least one `[overlap]` finding naming `U3.7 GND` and `U3.8 GND`, the plan-1 final review's "GNGND". Open the printed PNG and confirm the two GND texts overlap. Record the output in the report.

- [ ] **Step 2: `Part` learns panel, DNP, BOM, board and pin-type overrides**

In `hardware/gen/netlist.py`, extend `Part.__init__`'s signature and body:
```python
    def __init__(self, ref, lib_id, value, footprint, note="", lcsc="",
                 source="", panel_id="", domain="", strict=False, panel=False,
                 dnp=False, in_bom=True, on_board=True, pin_types=None):
        ...existing assignments...
        self.panel = panel         # mounted in a panel hole; needs a PanelId
        self.dnp = dnp             # footprint fitted, part not populated
        self.in_bom = in_bom       # False: a pad, not a part (test points)
        self.on_board = on_board   # False: bought, but has no footprint (sockets)
        self.pin_types = {str(k): v for k, v in (pin_types or {}).items()}
        for number in self.pin_types:
            self.sym.pin(number)   # raises if the override names no pin
```
and add the method:
```python
    def etype(self, number):
        """The pin's electrical type, with this part's override applied.

        Overrides exist for symbols whose pin types name a default role the
        board does not use: the Patch SM declares D10 (SPI_SCK) an output, and
        Rev A reads it as the 165's serial input (P2 §4).
        """
        number = str(number)
        return self.pin_types.get(number, self.sym.pin(number)["etype"])
```

- [ ] **Step 3: One power symbol for adjacent same-net power pins**

In `hardware/gen/sch_writer.py`, add `ADJACENT = 2 * GRID` below `POWER_LEN`, and replace `label_groups` with:
```python
def label_groups(part, unit, px=0.0, py=0.0, power=()):
    """One stub and one ending per distinct connection point of `unit`.

    Electrosmith's Daisy_Patch_SM stacks A4 and A7 -- both GND -- on exactly
    the same coordinate. One stub per pin would double-strike the text. Pins
    that share a point but carry DIFFERENT nets are a short, so that raises.

    Pins on the same power net that sit next to each other on one edge -- the
    74HC4051's VEE and GND, 2.54 mm apart, both GND on a single supply --
    share ONE power symbol: their stub ends are chained by wires. Two symbols
    there print their values on top of each other ("GNGND").

    Returns [((cx, cy), angle, net, pin numbers, other points)], where other
    points are the connection points whose stubs join this group's ending.
    """
    groups = {}
    for number in unit_pins(part, unit):
        if number not in part.nets:
            continue
        net = part.nets[number]
        pin = part.sym.pin(number)
        key = (round(pin["x"], 4), round(pin["y"], 4), pin["angle"])
        if key in groups and groups[key][0] != net:
            raise ValueError(
                "%s: pins %s and %s share a connection point but carry "
                "different nets (%s vs %s) -- that is a short"
                % (part.ref, groups[key][1][0], number, groups[key][0], net))
        groups.setdefault(key, (net, []))[1].append(number)

    out, runs = [], {}
    for key, (net, nums) in sorted(groups.items()):
        if net in power:
            vertical = key[2] in (90, 270)          # pins on the top/bottom edge
            line = key[1] if vertical else key[0]
            runs.setdefault((net, key[2], line), []).append((key, nums))
        else:
            out.append(((key[0], key[1]), key[2], net, nums, []))
    for (net, angle, _line), members in sorted(runs.items()):
        along = 0 if angle in (90, 270) else 1
        members.sort(key=lambda m: m[0][along])
        run = [members[0]]
        for m in members[1:] + [None]:
            if m is not None and m[0][along] - run[-1][0][along] <= ADJACENT + 1e-6:
                run.append(m)
                continue
            head = run[0][0]
            out.append(((head[0], head[1]), angle, net,
                        [n for _, ns in run for n in ns],
                        [(k[0], k[1]) for k, _ in run[1:]]))
            run = [m] if m is not None else []
    out.sort(key=lambda g: (g[0][0], g[0][1], g[1]))
    return [(pin_point(px, py, dict(x=pt[0], y=pt[1])), angle, net, nums,
             [pin_point(px, py, dict(x=o[0], y=o[1])) for o in others])
            for pt, angle, net, nums, others in out]
```
Update the three callers:
- **`label_margins`:** change `for (cx, cy), angle, net, _ in label_groups(part, unit):` to `for (cx, cy), angle, net, _nums, _others in label_groups(part, unit, power=power):`.
- **`_boxes`:** change `for (cx, cy), angle, net, nums in label_groups(p, u, px, py):` to `for (cx, cy), angle, net, nums, _others in label_groups(p, u, px, py, power):`.
- **`emit_items`:** replace the loop head and the stub wire with:
```python
        for (cx, cy), angle, net, nums, others in label_groups(p, u, px, py, power):
            ex, ey, rot = stub_end(cx, cy, angle)
            key = (p.ref, "/".join(nums))
            out.append(_wire(cx, cy, ex, ey, uuids("stub", *key)))
            prev = (ex, ey)
            for i, (ox, oy) in enumerate(others, 1):
                oex, oey, _ = stub_end(ox, oy, angle)
                out.append(_wire(ox, oy, oex, oey, uuids("stub", p.ref, key[1], i)))
                # chain end to end, so no wire end lands mid-segment
                out.append(_wire(prev[0], prev[1], oex, oey, uuids("join", p.ref, key[1], i)))
                prev = (oex, oey)
```
Keep the rest of the loop body (the power, global and local branches) unchanged.

- [ ] **Step 4: The part's flags reach the file**

Replace `_symbol` with:
```python
def _symbol(lib_id, ref, unit, x, y, rot, in_bom, on_board, dnp, sym_uuid,
            props, pins, project, path):
    def yn(b):
        return "yes" if b else "no"
    return ('\t(symbol\n\t\t(lib_id "%s")\n\t\t(at %s %s %d)\n\t\t(unit %d)\n'
            '\t\t(exclude_from_sim no)\n\t\t(in_bom %s)\n\t\t(on_board %s)\n'
            '\t\t(dnp %s)\n\t\t(uuid "%s")\n%s%s'
            '\t\t(instances\n\t\t\t(project "%s"\n'
            '\t\t\t\t(path "%s" (reference "%s") (unit %d))))\n\t)\n'
            % (lib_id, _n(x), _n(y), rot, unit, yn(in_bom), yn(on_board),
               yn(dnp), sym_uuid, "".join(props),
               "".join('\t\t(pin "%s" (uuid "%s"))\n' % pu for pu in pins),
               project, path, ref, unit))
```
In `emit_items`:
- The part call becomes `_symbol(p.lib_id, p.ref, u, px, py, 0, p.in_bom and not virtual, p.on_board and not virtual, p.dnp, uuids("sym", p.ref, u), props, pins, project, path)`.
- The power-symbol call becomes `_symbol(lib_id, ref, 1, ex, ey, srot, False, False, False, uuids("pwr", *key), pprops, [(pnum, uuids("pwrpin", *key))], project, path)`.

- [ ] **Step 5: Run the writer test and the demo sheet check**

Run: `python hardware/gen/test_sch_writer.py` → expected `ok: ...` (coupon and demo export their intent).
Run: `python hardware/gen/check.py --project hardware/gen/fixtures/demo.py --sheet logic --out "C:/Users/bernd/AppData/Local/Temp/p3p2-demo"` → expected `PASS sheet logic`. Open the PNG: U3 has one GND symbol below pins 7/8 with a joining wire, and the text is readable. Do the same for `--sheet power`. J5's pins 3 and 5 (GND) now share one symbol.

- [ ] **Step 6: Derived symbols carry their own fields (probe, then keep or revert)**

In `lib_symbols`, embed derived symbols with the derived symbol's own `property` nodes replacing the base's. Add above `lib_symbols`:
```python
def _derived_node(sym):
    """The base's drawing with the derived symbol's own fields.

    KiCad compares the embedded copy with the library's flattened symbol, in
    which a derived symbol's properties (Value, Description, Datasheet,
    footprint filters) override its base's. Embedding the base's fields made
    ERC report lib_symbol_mismatch on every derived part (TL072 in the demo;
    AMS1117, SS14 and 74HC165 on Rev A).
    """
    own = {str(c[1]): c for c in ksexp.children(sym.node, "property")}
    out, placed_extra = [], False
    for c in sym.base.node:
        is_prop = isinstance(c, list) and c and c[0] == "property"
        if is_prop and str(c[1]) in own:
            out.append(own.pop(str(c[1])))
            continue
        if not is_prop and not placed_extra and isinstance(c, list) and c and c[0] == "symbol":
            out.extend(own.values())       # derived-only fields before the units
            own, placed_extra = {}, True
        out.append(c)
    return out
```
and in `lib_symbols` change `node = sym.base.node if sym.extends else sym.node` to `node = _derived_node(sym) if sym.extends else sym.node`.
Run: `python hardware/gen/check.py --project hardware/gen/fixtures/demo.py --full --out "C:/Users/bernd/AppData/Local/Temp/p3p2-demo"`.
- **If** the only finding is `[erc_waiver]: waiver matches nothing: lib_symbol_mismatch | Symbol U2 [TL072]`, the fix works. Delete that waiver line from `demo-erc-waivers.txt`, keeping its three header comment lines, and rerun. Expected: `PASS full`.
- **If** `lib_symbol_mismatch` is still reported, revert `_derived_node` and its call with the Edit tool, and keep the waiver. Record in the report which case happened. Plan 2's Task 3 waivers depend on it.
Either way, `python hardware/gen/test_sch_writer.py` stays `ok`.

- [ ] **Step 7: The writer test asserts the drawing too**

In `hardware/gen/test_sch_writer.py`, inside `check_project` after `W.write_project(proj, a)`, capture the layouts, `layouts = W.write_project(proj, a)`, replacing the plain call. Then add:
```python
        sheets = {s.name: s for s in proj.sheets}
        for name, (placed, height) in sorted(layouts.items()):
            for hit in W.overlaps(sheets[name].parts, placed, proj.power):
                failures.append("%s/%s: overlap %s <-> %s" % (proj.name, name, hit[0], hit[1]))
            if not W.fits(height, proj.paper):
                failures.append("%s/%s: %.0f mm tall, does not fit %s"
                                % (proj.name, name, height, proj.paper))
```
Run it: expected `ok`. Show it RED once by temporarily setting `ADJACENT = 0` in `sch_writer.py`. Expected: `FAIL demo/logic: overlap ...`. Revert with Edit and rerun green.

- [ ] **Step 8: Commit**

Run `python hardware/gen/test_check.py` (expected ok) and `ctest --test-dir build -R hw_gen --output-on-failure` (expected 3/3).
```bash
git add hardware/gen
```
```bash
git commit -m "hw(gen): adjacent power pins share one symbol; dnp/bom/board flags; derived fields

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: Checks — footprints, panel parts, pin-type overrides, exact waivers, located errors; vendor the Thonk footprint

**Files:**
- Modify: `hardware/gen/project.py` (`lib_dirs`, `ground`)
- Modify: `hardware/gen/check.py`
- Modify: `hardware/coupon/scripts/build.py` (the one `write_lib_tables` call)
- Modify: `hardware/gen/fixtures/demo.py` (`panel=True` on RV1 and D1)
- Modify: `hardware/gen/fixtures/demo-erc-waivers.txt` (new four-field format, if a line remains)
- Modify: `hardware/gen/test_check.py`
- Create: `hardware/lib/Thonk/Thonk.pretty/SW_Push_LP_Button.kicad_mod`, `hardware/lib/Thonk/README.md`

**Interfaces:**
- Consumes: Task 1's `Part.panel`, `Part.in_bom`, `Part.on_board`, `Part.etype`.
- Produces:
  - `Project(..., lib_dirs=None, ground="GND")`, where `lib_dirs` is `{lib name: absolute dir holding <lib>.pretty / <lib>.kicad_sym}`.
  - `check.write_lib_tables(parts, dest_dir, lib_dirs, extra_sym_libs=())`, where `lib_dirs` is `{lib: dir uri}`.
  - `check.OPEN_FOOTPRINT = "P4"`.
  - A new rule `RULES["footprints"]`, with sabotages `footprints`, `footprints:empty` and `panel_orphan`.
  - Waiver lines of the form `type | exact item description | count | reason`.
  - `check.main` prints `FAIL - [error]: ...` instead of a traceback for ValueError, RuntimeError and KeyError.

- [ ] **Step 1: Vendor the Thonk footprint**

Read `C:/Users/bernd/AppData/Local/Temp/claude/C--Users-bernd-Documents-AI-FireFlow/79a8a5ca-0ce5-4c6c-87ce-4372f548562f/scratchpad/thonk-lp/SW_Push_LP_Button.kicad_mod` with the Read tool. Write it unchanged to `hardware/lib/Thonk/Thonk.pretty/SW_Push_LP_Button.kicad_mod`. If the scratch file is gone, stop and report NEEDS_CONTEXT; the controller re-downloads it. Create `hardware/lib/Thonk/README.md`:
```markdown
# Thonk footprints (vendored)

`Thonk.pretty/SW_Push_LP_Button.kicad_mod` — Thonk's low-profile push button
(DPDT; momentary OFF-(ON) or latching), from Thonk's own KiCad package
https://www.thonk.co.uk/wp-content/uploads/2024/08/THONK-SW-Push-LP-Button.zip,
downloaded 2026-09-29 and committed unchanged. The 3D model in that ZIP is
not vendored; the footprint's model line points at `${THONK_3D_MODELS}`.

Contacts (Thonk datasheet, LOW-PROFILE-PUSH-BUTTONS.pdf): free, 2–3 and 5–6
are closed; pushed, 1–2 and 4–5. Rev A uses pin 2 as common and pin 1 as the
contact that closes when pressed; 3–6 stay unconnected. Panel cutout 6.2 mm;
16.5 mm above the board unlatched.
```

- [ ] **Step 2: Write the failing tests**

In `hardware/gen/test_check.py`:
- Replace the two `match_waivers` checks at the end with:
```python
    item = "Symbol U1 Pin 9 [Q7, Output, Line]"
    found = check.match_waivers([("pin_to_pin", [item])],
                                [("pin_to_pin", item, 1, "reason"),
                                 ("pin_not_driven", "Symbol U9 Pin 1 [A, Input, Line]", 1, "stale")])
    if [f.rule for f in found] != ["erc_waiver"]:
        failures.append("match_waivers: wanted one stale-waiver finding, got %s"
                        % [str(f) for f in found])
    found = check.match_waivers([("pin_to_pin", ["Symbol U2 Pin 1"])], [])
    if [f.rule for f in found] != ["erc"]:
        failures.append("match_waivers: an unwaived violation went unreported")
    found = check.match_waivers([("pin_to_pin", [item]), ("pin_to_pin", [item])],
                                [("pin_to_pin", item, 1, "reason")])
    if [f.rule for f in found] != ["erc_waiver"]:
        failures.append("match_waivers: a waiver absorbed more violations than its count")
    found = check.match_waivers([("pin_to_pin", ["Symbol U1 Pin 90 [X, Output, Line]"])],
                                [("pin_to_pin", "Symbol U1 Pin 9", 1, "prefix only")])
    if sorted(f.rule for f in found) != ["erc", "erc_waiver"]:
        failures.append("match_waivers: a waiver matched by substring, not exactly")
```
- In `main()`, after the fast-rule loop, add:
```python
        expect(["--fast", "--sabotage", "panel_orphan"], 1, "[panel_ids]", out)
        expect(["--sheet", "no_such_sheet"], 1, "[error]", out)
```
Run: `python hardware/gen/test_check.py`. Expected: FAIL. It fails with `unknown sabotage 'footprints'`, or with the waiver checks failing on the old three-field format.

- [ ] **Step 3: `Project` learns library dirs and the ground net**

In `hardware/gen/project.py`, add `lib_dirs=None, ground="GND"` to `Project.__init__`'s parameters. Add `self.lib_dirs = dict(lib_dirs or {})` and `self.ground = ground` below `self.comments`.

- [ ] **Step 4: Library tables from a map; the open footprint is skipped**

In `hardware/gen/check.py`, delete `VENDORED_LIBS` and replace `write_lib_tables` with:
```python
def write_lib_tables(parts, dest_dir, lib_dirs, extra_sym_libs=()):
    """Emit sym-lib-table / fp-lib-table covering exactly what the parts use.

    Derived from the part list rather than hand-kept, so a new part cannot
    silently leave the tables behind. KiCad's own libraries go through its
    ${KICAD10_*_DIR} variables; vendored ones through `lib_dirs`, {library:
    directory uri holding <lib>.kicad_sym and/or <lib>.pretty} -- the coupon
    passes "${KIPRJMOD}/../lib/DaisyKiCad" for Daisy-Boards, so nothing
    machine-specific reaches the repository. A footprint without a library
    (the open "P4") gets no row.
    """
    sym_libs = sorted({p.lib_id.split(":")[0] for p in parts} | set(extra_sym_libs))
    fp_libs = sorted({p.footprint.split(":")[0] for p in parts if ":" in p.footprint})

    def rows(libs, kind):
        out = []
        for lib in libs:
            ext = "kicad_sym" if kind == "sym" else "pretty"
            if lib in lib_dirs:
                uri = "%s/%s.%s" % (lib_dirs[lib], lib, ext)
            elif kind == "sym":
                uri = "${KICAD10_SYMBOL_DIR}/%s.kicad_sym" % lib
            else:
                uri = "${KICAD10_FOOTPRINT_DIR}/%s.pretty" % lib
            out.append('  (lib (name "%s")(type "KiCad")(uri "%s")'
                       '(options "")(descr ""))' % (lib, uri))
        return "\n".join(out)

    for kind, libs, fname in (("sym", sym_libs, "sym-lib-table"),
                              ("fp", fp_libs, "fp-lib-table")):
        head = "sym_lib_table" if kind == "sym" else "fp_lib_table"
        body = "(%s\n  (version 7)\n%s\n)\n" % (head, rows(libs, kind))
        with open(os.path.join(dest_dir, fname), "w",
                  encoding="utf-8", newline="\n") as fh:
            fh.write(body)
    return sym_libs, fp_libs
```
In `hardware/coupon/scripts/build.py`, change the call to `C.write_lib_tables(N.build(), ROOT, {"Daisy-Boards": "${KIPRJMOD}/../lib/DaisyKiCad"})`. Its tables come out byte-identical, since `Daisy-Boards` is still the only vendored library there.

In `check.py`, add below `VENDORED_DIR`:
```python
OPEN_FOOTPRINT = "P4"       # the SD socket, chosen in P4 (spec §6); allowed once
KICAD_FP_DIR = os.path.join(ksexp.KICAD_ROOT, "share", "kicad", "footprints")


def _lib_dirs(project):
    """Vendored library dirs: Daisy-Boards always, plus the project's own."""
    dirs = {"Daisy-Boards": VENDORED_DIR}
    dirs.update({k: v.replace("\\", "/") for k, v in project.lib_dirs.items()})
    return dirs
```
and in `_write_all` change the table call to `write_lib_tables(project.parts(), sch_dir, _lib_dirs(project), extra_sym_libs={"power"} if project.power else ())`.

- [ ] **Step 5: The footprints rule, panel parts, pin-type overrides, BOM-less parts**

Add `"sd"` to `PANEL_KINDS`. Then:
- **`rule_driver_conflict`:** replace `p.sym.pin(pin)["etype"]` with `p.etype(pin)`. Do the same in `_sab_driver_conflict`.
- **`rule_sourced`:** skip parts kept out of the BOM. Change the `parts = ...` line to `parts = [p for p in project.parts() if not N.is_virtual(p.ref) and p.in_bom]`.
- **`rule_panel_ids`:** before `return len(holes), found`, add:
```python
    for p in project.parts():
        if p.panel and not p.panel_id:
            found.append(Finding("panel_ids", sheet[p.ref],
                                 "%s is a panel part without a PanelId" % p.ref))
```
Add the new rule below `rule_sourced`:
```python
def _footprint_exists(project, footprint):
    lib, _, name = footprint.partition(":")
    if not lib or not name:
        return False
    base = _lib_dirs(project).get(lib, KICAD_FP_DIR)
    return os.path.exists(os.path.join(base, lib + ".pretty", name + ".kicad_mod"))


def rule_footprints(project):
    """Every part on the board names a footprint that exists; parts off the
    board (sockets bought for the BOM) name none; exactly one open "P4" is
    allowed (spec §6)."""
    sheet = project.sheet_of()
    examined, found, open_refs = 0, [], []
    for p in project.parts():
        if N.is_virtual(p.ref):
            continue
        if not p.on_board:
            if p.footprint:
                found.append(Finding("footprints", sheet[p.ref], "%s is off the board "
                                     "but names footprint %s" % (p.ref, p.footprint)))
            continue
        examined += 1
        if p.footprint == OPEN_FOOTPRINT:
            open_refs.append(p.ref)
        elif not _footprint_exists(project, p.footprint):
            found.append(Finding("footprints", sheet[p.ref], "%s: footprint %r not found"
                                 % (p.ref, p.footprint)))
    if len(open_refs) > 1:
        found.append(Finding("footprints", "-", "only one open %r footprint is allowed: %s"
                             % (OPEN_FOOTPRINT, ", ".join(open_refs))))
    return examined, found
```
and add `"footprints": rule_footprints,` to `RULES`.

- [ ] **Step 6: Sabotages for the new paths; the ground net is no longer retyped**

Replace `_sab_single_pin` and `_sab_rail_domain`'s `"GND"` with `pr.ground`. Add:
```python
def _sab_footprints(pr):
    p = _first([p for p in pr.parts() if not N.is_virtual(p.ref) and p.on_board],
               "a part on the board")
    p.footprint = "Nope:Missing"


def _sab_panel_orphan(pr):
    _sab_add(pr, N.Part("SW_SAB1", "Switch:SW_Push", "orphan", "",
                        source="C-SABOTAGE", panel=True)
             .by_number(1, pr.ground).by_number(2, pr.ground))
```
Add `"footprints": _sab_footprints, "panel_orphan": _sab_panel_orphan,` to `SABOTAGE`, and in `_empty` add:
```python
    elif rule == "footprints":
        for p in pr.parts():
            p.on_board, p.footprint = False, ""
```

- [ ] **Step 7: Exact waivers with counts**

Replace `load_waivers` and `match_waivers`:
```python
def load_waivers(path):
    """`type | exact item description | count | reason`, one per line."""
    if path is None:
        return []
    out = []
    with open(path, encoding="utf-8") as fh:
        for i, line in enumerate(fh, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = [f.strip() for f in line.split("|")]
            if len(fields) != 4 or not all(fields) or not fields[2].isdigit() \
                    or int(fields[2]) < 1:
                raise ValueError("%s:%d: want 'type | exact item | count | reason', "
                                 "got %r" % (path, i, line))
            out.append((fields[0], fields[1], int(fields[2]), fields[3]))
    return out


def match_waivers(violations, waivers):
    """violations: [(type, [item descriptions])]. A waiver names one exact item
    description and how many violations it covers. Unwaived violations, stale
    waivers and waivers that cover more or fewer than their count are all
    findings, so one line cannot quietly absorb a new violation on the same
    part, and `Pin 9` never matches `Pin 90`."""
    hits, found = [0] * len(waivers), []
    for vtype, items in violations:
        k = next((k for k, (wt, item, _, _) in enumerate(waivers)
                  if wt == vtype and item in items), None)
        if k is None:
            found.append(Finding("erc", "-", "%s: %s" % (vtype, "; ".join(items) or "(no items)")))
        else:
            hits[k] += 1
    for k, (wt, item, count, _) in enumerate(waivers):
        if hits[k] == 0:
            found.append(Finding("erc_waiver", "-", "waiver matches nothing: %s | %s"
                                 % (wt, item)))
        elif hits[k] != count:
            found.append(Finding("erc_waiver", "-", "waiver %s | %s covers %d violation(s), "
                                 "its count says %d" % (wt, item, hits[k], count)))
    return found
```
If `demo-erc-waivers.txt` still has a waiver line after Task 1, rewrite it in the new format. Use the exact item text from `erc.json` of a demo `--full` run and the count of violations it covers. Update the file's header comment to the four-field format.

- [ ] **Step 8: Stability compares everything written; errors are located**

Add below `_write_all`:
```python
def _write_tree(project, dest):
    W.write_project(project, dest)
    write_lib_tables(project.parts(), dest, _lib_dirs(project),
                     extra_sym_libs={"power"} if project.power else ())
```
In `_stability`, replace both `W.write_project(factory(), a|b)` calls with `_write_tree(factory(), a|b)`. Change `names = sorted(os.listdir(a))` to `names = sorted(set(os.listdir(a)) | set(os.listdir(b)))`.

In `main`, after the `except SabotageError as e: ap.error(str(e))` clause, add:
```python
    except (ValueError, RuntimeError, KeyError) as e:
        print(Finding("error", "-", "%s: %s" % (type(e).__name__, e)))
        print("FAIL %s: the check could not run, %.1f s"
              % (level_name, time.monotonic() - t0))
        return 1
```

- [ ] **Step 9: Demo panel parts; run everything**

In `demo.py`, add `panel=True` to `RV1` and `D1`.
Run: `python hardware/gen/test_check.py` → expected `ok`, now including `footprints`, `footprints:empty`, `panel_orphan` and the `[error]` line.
Run: `python hardware/gen/test_sch_writer.py` and `python hardware/gen/test_coupon_netlist.py` → expected `ok`.
Show the footprints rule RED once: temporarily make `_footprint_exists` return `True` always. Expected: `--fast --sabotage footprints` is UNEXPECTED. Revert with Edit.

- [ ] **Step 10: Commit**

Run: `ctest --test-dir build -R hw_gen --output-on-failure` → 3/3.
```bash
git add hardware/gen hardware/lib/Thonk hardware/coupon/scripts/build.py
```
```bash
git commit -m "hw(gen): footprint rule, panel parts, pin-type overrides, exact counted waivers

Also vendors Thonk's low-profile button footprint and prints check errors as
located findings.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: Rev A catalogue and the digital-side sheets

**Files:**
- Modify: `hardware/reva/assign.py` (`CAL_A3V3` → `CAL_3V3`), regenerate `hardware/reva/panel-map.json`
- Modify: `hardware/reva/test_assign.py` (duplicate-LED case)
- Create: `hardware/reva/parts.py`, `hardware/reva/blocks.py`, `hardware/reva/build.py`, `hardware/reva/erc-waivers.txt`

**Interfaces:**
- Consumes: Tasks 1–2 (`Part` flags, `Project(lib_dirs=...)`, the check tool); `assign.load_holes()`; `panel-map.json` keys `pots`, `calibration`, `spare`, `leds`, `muxes`, `sense_order`.
- Produces:
  - `parts.make(kind, ref, value=None, **kw) -> Part` and `parts.CATALOGUE` (`{kind: PartType(lib_id, value, footprint, lcsc, source, jlc_type, note)}`).
  - `blocks.Refs()`.
  - Net constants `blocks.GND, P12, N12, SM3V3, D3V3, SENSE, MUX_S, MUX_EN`, and `blocks.POWER`, `blocks.DOMAINS`.
  - Sheet builders `blocks.power(refs)`, `module(refs)`, `chains(refs)`, `leds(refs, led_rows)`, `jacks(refs, jack_holes)`, `sd(refs)`, each returning a `Sheet`.
  - `build.project()`, the check tool's project file.

- [ ] **Step 1: The calibration net follows the rail rename**

The module's 3V3 is the module's own rail, not an "analog" one (spec addendum), so the net becomes `SM_3V3`. In `hardware/reva/assign.py` change `CALIBRATION = ("CAL_GND", "CAL_A3V3")` to `CALIBRATION = ("CAL_GND", "CAL_3V3")`. In `test_assign.py`, add a third bad-input case to the tuple in `main()`:
```python
            ("a duplicated LED id", holes + [dict(next(h for h in holes if h["kind"] == "led"))]),
```
Run `python hardware/reva/test_assign.py`. Expected: `FAIL panel-map.json is stale`, which is the RED. Then run `python hardware/reva/assign.py`, and `python hardware/reva/test_assign.py` again. Expected: `ok: 70 pots on 10 muxes, 2 calibration, 8 spare, 19 LEDs`.

- [ ] **Step 2: Create `hardware/reva/parts.py`**

```python
#!/usr/bin/env python3
"""Rev A part catalogue (P3 spec §3.2): one row per part type.

LCSC numbers and Basic/Extended types are the spec addendum's, checked on
jlcpcb.com 2026-09-29 (C45783 the same day); stock is checked at the freeze.
Panel parts carry Source="Thonk" and are hand-soldered with the panel on;
THT connectors are hand-soldered too. Exactly one part, the SD socket, has
the open footprint "P4" (chosen in P4, spec §6).
"""
import os
import sys
from collections import namedtuple

_HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if _HW not in sys.path:
    sys.path.insert(0, _HW)

from gen.netlist import Part  # noqa: E402

PartType = namedtuple("PartType", "lib_id value footprint lcsc source jlc_type note")

FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_C0603 = "Capacitor_SMD:C_0603_1608Metric"
FP_C0805 = "Capacitor_SMD:C_0805_2012Metric"
FP_SOIC16 = "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm"
THONK = "Thonk"
HAND = "hand-soldered"

CATALOGUE = {
    "mux": PartType("74xx:74HC4051", "74HC4051", FP_SOIC16, "C9386", "", "Extended",
                    "Nexperia 74HC4051D,653"),
    "sr_out": PartType("74xx:74HC595", "74HC595", FP_SOIC16, "C5947", "", "Basic",
                       "Nexperia 74HC595D,118"),
    "sr_in": PartType("74xx:74HC165", "74HC165", FP_SOIC16, "C5613", "", "Extended",
                      "Nexperia 74HC165D,653"),
    "ldo": PartType("Regulator_Linear:AMS1117-3.3", "AMS1117-3.3",
                    "Package_TO_SOT_SMD:SOT-223-3_TabPin2", "C6186", "", "Basic",
                    "3V3D from +12 V"),
    "schottky": PartType("Diode:SS14", "SS14", "Diode_SMD:D_SMA", "C2480", "", "Basic",
                         "series reverse protection"),
    "r1k": PartType("Device:R", "1k", FP_R, "C21190", "", "Basic", "1 %"),
    "r10k": PartType("Device:R", "10k", FP_R, "C25804", "", "Basic", "1 %"),
    "c100n": PartType("Device:C", "100n", FP_C0603, "C14663", "", "Basic", "50 V X7R"),
    "c10u": PartType("Device:C", "10u 25V", FP_C0805, "C15850", "", "Basic", "X5R"),
    "c22u": PartType("Device:C", "22u 25V", FP_C0805, "C45783", "", "Basic",
                     "X5R; AMS1117 output"),
    "pot": PartType("Device:R_Potentiometer", "10k",
                    "Potentiometer_THT:Potentiometer_Alpha_RD901F-40-00D_Single_Vertical",
                    "", THONK, "", "Alpha RD901F-40 9 mm, T18 shaft, B10K"),
    "jack": PartType("Connector_Audio:AudioJack2_SwitchT", "Thonkiconn",
                     "Connector_Audio:Jack_3.5mm_QingPu_WQP-PJ398SM_Vertical_CircularHoles",
                     "", THONK, "", "Thonkiconn PJ398SM mono"),
    "key": PartType("Switch:SW_Push_DPDT", "LP button", "Thonk:SW_Push_LP_Button",
                    "", THONK, "", "Thonk low-profile button, momentary; pin 1 closes to 2"),
    "led": PartType("Device:LED", "LED 3mm", "LED_THT:LED_D3.0mm", "", THONK, "",
                    "3 mm flat-top LED"),
    "header": PartType("Connector_Generic:Conn_02x05_Odd_Even", "Eurorack power",
                       "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical", "", HAND, "",
                       "shrouded keyed 2x5 box header; pin 1 = -12 V"),
    "sm_socket": PartType("Connector_Generic:Conn_02x05_Odd_Even", "2x5 socket", "",
                          "", HAND, "", "2x5 female socket for the Patch SM, cut from "
                          "a 2x10 strip; its holes belong to U_SM's footprint"),
    "module": PartType("Daisy-Boards:Daisy_Patch_SM", "Daisy Patch SM",
                       "Daisy-Boards:DAISY_PATCH_SM", "", "Electrosmith", "",
                       "Patch Submodule, socketed"),
    "sd": PartType("Connector:Micro_SD_Card", "microSD", "P4", "", "P4", "",
                   "socket chosen in P4 (spec §6)"),
    "tp": PartType("Connector:TestPoint", "TP", "TestPoint:TestPoint_Pad_D1.5mm", "", "",
                   "", "probe pad"),
}


def make(kind, ref, value=None, **kw):
    """A Part of catalogue type `kind`; keyword arguments go to Part."""
    t = CATALOGUE[kind]
    kw.setdefault("note", t.note)
    if kind == "tp":
        kw.setdefault("in_bom", False)          # a pad, nothing to buy
    return Part(ref, t.lib_id, t.value if value is None else value, t.footprint,
                lcsc=t.lcsc, source=t.source, **kw)


def flag(ref, net):
    """A PWR_FLAG: the rail is fed by a pin no symbol declares an output."""
    return Part(ref, "power:PWR_FLAG", "PWR_FLAG", "").by_number(1, net)
```

- [ ] **Step 3: Create `hardware/reva/blocks.py` (all sheets except the mux regions)**

```python
#!/usr/bin/env python3
"""Rev A circuit blocks, one function per sheet (P3 spec §2, §3.2).

P2's tables live here, once: the module pin map (P2 §2), the 595 bit table and
the 165 inputs (§4), power (§5). Positions never appear: which pot sits on
which mux and which LED gets which index comes from panel-map.json
(assign.py), and P4 places the parts.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_HW = os.path.normpath(os.path.join(HERE, ".."))
for p in (_HW, HERE):
    if p not in sys.path:
        sys.path.insert(0, p)

from gen.project import Sheet  # noqa: E402
from parts import flag, make   # noqa: E402

# --- nets (P2) ----------------------------------------------------------------
GND, P12, N12 = "GND", "+12V", "-12V"
SM3V3 = "SM_3V3"          # the module's 3V3 on A10: pots, muxes, SD card
D3V3 = "3V3D"             # AMS1117 from +12 V: shift registers, LEDs
P12_IN, N12_IN = "+12V_IN", "-12V_IN"      # bus side of the reverse diodes
POWER = {GND: "power:GND", P12: "power:+12V", N12: "power:-12V",
         SM3V3: "power:+3V3", D3V3: "power:+3V3"}
DOMAINS = {"analog": {SM3V3}, "digital": {D3V3}}

SENSE = ("SENSE_0", "SENSE_1", "SENSE_2", "SENSE_3")
SR_DATA, SR_CLK, SR_LATCH, SR_DIN = "SR_DATA", "SR_CLK", "SR_LATCH", "SR_DIN"
# P2 calls the address lines A0-A2; here MUX_S0-S2, because "A0" is also the
# name of a 4051 channel pin.
MUX_S = ("MUX_S0", "MUX_S1", "MUX_S2")
MUX_EN = tuple("MUX_EN%d" % n for n in range(10))


def sr(net):
    """The shift-register side of a 1 k address/enable series resistor (P2 §3)."""
    return net + "_SR"


def led_net(index):
    return "LED%d" % index


def key_net(key):
    return "KEY_" + key


# --- P2 §2: every module pin ----------------------------------------------------
MODULE_PINS = {
    "A1": N12, "A2": "SENSE_0", "A3": "SENSE_1", "A4": GND, "A5": P12, "A7": GND,
    "A10": SM3V3,
    "B1": "OUT_R", "B2": "OUT_L", "B3": "IN_R", "B4": "IN_L",
    "B5": "GATE_A", "B6": "GATE_B", "B7": SR_DATA, "B8": SR_CLK,
    "B9": "RESET", "B10": "CLOCK",
    "C1": "PITCH_A", "C2": "MOD4_A", "C3": "MOD3_A", "C4": "MOD2_A", "C5": "MOD1_A",
    "C6": "MOD1_B", "C7": "MOD2_B", "C8": "MOD3_B", "C9": "MOD4_B", "C10": "PITCH_B",
    "D1": SR_LATCH, "D2": "SD_D3", "D3": "SD_D2", "D4": "SD_D1", "D5": "SD_D0",
    "D6": "SD_CK", "D7": "SD_CMD", "D8": "SENSE_3", "D9": "SENSE_2", "D10": SR_DIN,
}
MODULE_NC = ("A6", "A8", "A9")      # +5 V out (the module's analog rail), USB
JACKS = ("IN_L", "IN_R", "OUT_L", "OUT_R", "GATE_A", "GATE_B", "RESET", "CLOCK",
         "PITCH_A", "PITCH_B", "MOD1_A", "MOD2_A", "MOD3_A", "MOD4_A",
         "MOD1_B", "MOD2_B", "MOD3_B", "MOD4_B")
JACK_NORMAL = {"IN_R": "IN_L"}      # patch.Init(): unpatched, R takes L

# --- P2 §4: the chains ------------------------------------------------------------
SR_OUTPUTS = (      # chip order from SR_DATA; outputs QA..QH
    tuple(sr(n) for n in MUX_S) + tuple(sr(n) for n in MUX_EN[0:5]),
    tuple(sr(n) for n in MUX_EN[5:10]) + (led_net(0), led_net(1), led_net(2)),
    tuple(led_net(i) for i in range(3, 11)),
    tuple(led_net(i) for i in range(11, 19)),
    tuple("SR_SPARE%d" % i for i in range(8)),
)
KEYS = ("REC_A", "REC_B", "MODBTN", "SHIFTBTN")      # 165 inputs D0..D3


class Refs:
    """Sequential references per prefix, in build order -- deterministic
    because the sheets are always built in the same order."""

    def __init__(self):
        self.count = {}

    def __call__(self, prefix):
        self.count[prefix] = self.count.get(prefix, 0) + 1
        return "%s%d" % (prefix, self.count[prefix])


def _decouple(refs, rail, domain):
    return make("c100n", refs("C"), domain=domain).by_number(1, rail).by_number(2, GND)


def power(refs):
    hdr = make("header", "J_PWR", strict=True)
    for n in (1, 2):
        hdr.by_number(n, N12_IN)
    for n in (3, 4, 5, 6):
        hdr.by_number(n, GND)
    for n in (9, 10):
        hdr.by_number(n, P12_IN)
    hdr.no_connect(7, 8)        # GND on the A-100 bus; open, as on the coupon
    d_p = make("schottky", "D_P12").by_name("A", P12_IN).by_name("K", P12)
    d_n = make("schottky", "D_N12").by_name("A", N12).by_name("K", N12_IN)
    reg = make("ldo", "U_REG", domain="digital", strict=True)
    reg.by_name("VI", P12).by_name("VO", D3V3).by_name("GND", GND)
    parts = [hdr, d_p, d_n, reg]
    for kind, rail in (("c10u", P12), ("c100n", P12), ("c10u", N12), ("c100n", N12),
                       ("c10u", P12), ("c22u", D3V3)):
        parts.append(make(kind, refs("C")).by_number(1, rail).by_number(2, GND))
    parts += [flag("#FLG0001", P12), flag("#FLG0002", N12)]
    return Sheet("power", "Eurorack power, reverse protection, 3V3D", parts)


def module(refs):
    sm = make("module", "U_SM", strict=True, pin_types={"D10": "input"})
    for pin, net in sorted(MODULE_PINS.items()):
        sm.by_number(pin, net)
    sm.no_connect(*MODULE_NC)
    parts = [sm]
    parts += [make("sm_socket", "J_SM%d" % i, on_board=False) for i in range(1, 5)]
    for i, net in enumerate(SENSE):
        parts.append(make("c100n", "C_SENSE%d" % i, dnp=True,
                          note="COM pad, unpopulated -- the coupon verdict (P2 §3)")
                     .by_number(1, net).by_number(2, GND))
    for net in SENSE + (SM3V3, D3V3, GND):
        parts.append(make("tp", refs("TP"), value=net).by_number(1, net))
    parts.append(flag("#FLG0003", SM3V3))
    return Sheet("module", "Patch Submodule, sense pads, test points", parts)


def chains(refs):
    parts, prev = [], SR_DATA
    for i, outs in enumerate(SR_OUTPUTS, 1):
        u = make("sr_out", "U_SR%d" % i, domain="digital", strict=True)
        u.by_name("VCC", D3V3).by_name("GND", GND).by_name("~{SRCLR}", D3V3)
        u.by_name("~{OE}", GND).by_name("SRCLK", SR_CLK).by_name("RCLK", SR_LATCH)
        u.by_name("SER", prev)
        for letter, net in zip("ABCDEFGH", outs):
            u.by_name("Q" + letter, net)
        if i < len(SR_OUTPUTS):
            prev = "SR_CHAIN%d" % i
            u.by_name("QH'", prev)
        else:
            u.no_connect(u.sym.by_name("QH'"))
        parts += [u, _decouple(refs, D3V3, "digital")]
    inp = make("sr_in", "U_IN1", domain="digital", strict=True)
    inp.by_name("VCC", D3V3).by_name("GND", GND).by_name("CP", SR_CLK)
    inp.by_name("~{PL}", SR_LATCH).by_name("~{CE}", GND).by_name("DS", GND)
    inp.by_name("Q7", SR_DIN)
    inp.no_connect(inp.sym.by_name("~{Q7}"))
    for i, key in enumerate(KEYS):
        inp.by_name("D%d" % i, key_net(key))
    for i in range(len(KEYS), 8):
        inp.by_name("D%d" % i, GND)
    parts += [inp, _decouple(refs, D3V3, "digital")]
    for net in MUX_S + MUX_EN:
        parts.append(make("r1k", refs("R"), note="series; P2 §3 power-up clamp")
                     .by_number(1, sr(net)).by_number(2, net))
    for key in KEYS:
        parts.append(make("r10k", refs("R"), domain="digital", note="key pull-up")
                     .by_number(1, D3V3).by_number(2, key_net(key)))
        sw = make("key", refs("SW"), value=key, panel_id=key, panel=True, strict=True)
        sw.by_number(1, key_net(key)).by_number(2, GND).no_connect(3, 4, 5, 6)
        parts.append(sw)
    for net in SR_OUTPUTS[-1]:
        parts.append(make("tp", refs("TP"), value=net).by_number(1, net))
    return Sheet("chains", "Shift-register chains and keys", parts)


def leds(refs, led_rows):
    parts = []
    for row in sorted(led_rows, key=lambda r: r["index"]):
        i = row["index"]
        parts.append(make("r1k", refs("R"), note="LED series resistor")
                     .by_number(1, led_net(i)).by_number(2, led_net(i) + "_A"))
        d = make("led", refs("D"), value=row["id"], panel_id=row["id"], panel=True,
                 strict=True)
        parts.append(d.by_name("A", led_net(i) + "_A").by_name("K", GND))
    return Sheet("leds", "Panel LEDs", parts)


def jacks(refs, jack_holes):
    parts = []
    for h in sorted(jack_holes, key=lambda h: (h["x_mm"], h["id"])):
        if h["id"] not in JACKS:
            raise ValueError("panel jack %s is not in P2's pin map" % h["id"])
        j = make("jack", refs("J"), value=h["id"], panel_id=h["id"], panel=True,
                 strict=True)
        j.by_number("T", h["id"]).by_number("S", GND)
        if h["id"] in JACK_NORMAL:
            j.by_number("TN", JACK_NORMAL[h["id"]])
        else:
            j.no_connect("TN")
        parts.append(j)
    return Sheet("jacks", "Jacks, straight to the module (patch.Init())", parts)


def sd(refs):
    j = make("sd", "J_SD", panel_id="SD", panel=True, strict=True)
    for number, net in (("1", "SD_D2"), ("2", "SD_D3"), ("3", "SD_CMD"), ("4", SM3V3),
                        ("5", "SD_CK"), ("6", GND), ("7", "SD_D0"), ("8", "SD_D1"),
                        ("SH", GND)):
        j.by_number(number, net)
    return Sheet("sd", "SD card, straight to the module (patch.Init())", [j])
```

- [ ] **Step 4: Create `hardware/reva/build.py` (project only, for now)**

```python
#!/usr/bin/env python3
"""Rev A schematic: blocks -> project.

The check tool loads this file:
    python hardware/gen/check.py --project hardware/reva/build.py --fast|--sheet NAME|--full
Task 4 of plan 2 adds main(), which writes the committed files.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for p in (HW, HERE):
    if p not in sys.path:
        sys.path.insert(0, p)

import assign                      # noqa: E402
import blocks                      # noqa: E402
from gen.project import Project    # noqa: E402

PANEL_MAP = os.path.join(HERE, "panel-map.json")
WAIVERS = os.path.join(HERE, "erc-waivers.txt")
THONK_DIR = os.path.join(HW, "lib", "Thonk")
COVERED = {"jack", "key", "led", "sd"}          # Task 4 adds "pot"


def load_panel_map():
    with open(PANEL_MAP, encoding="utf-8") as fh:
        return json.load(fh)


def project():
    pm = load_panel_map()
    holes = [h for h in assign.load_holes() if h["kind"] in COVERED]
    refs = blocks.Refs()
    sheets = [blocks.power(refs), blocks.module(refs), blocks.chains(refs),
              blocks.leds(refs, pm["leds"]),
              blocks.jacks(refs, [h for h in holes if h["kind"] == "jack"]),
              blocks.sd(refs)]
    return Project("reva", "FireFlow Rev A", sheets, power=blocks.POWER,
                   domain_rails=blocks.DOMAINS, holes=holes, waivers=WAIVERS,
                   paper="A3", lib_dirs={"Thonk": THONK_DIR},
                   comments=["GENERATED by hardware/reva/build.py -- do not edit",
                             "Spec: docs/superpowers/specs/2026-09-29-rev-a-p3-schematic-design.md"])
```
Create `hardware/reva/erc-waivers.txt` with only the header:
```
# ERC waivers for Rev A. Format: type | exact item description | count | reason
# Only two kinds belong here: a Patch SM symbol pin type that names a default
# role Rev A does not use, and (if plan 2 Task 1's probe kept it) a
# derived-symbol lib_symbol_mismatch. Everything else is fixed in blocks.py.
```

- [ ] **Step 5: Fast level, then each sheet, looking at every PNG**

Run: `python hardware/gen/check.py --project hardware/reva/build.py --fast`
Expected: `PASS fast`. If a finding appears, fix `blocks.py`, not the check.
For each sheet in `power module chains leds jacks sd`, run `python hardware/gen/check.py --project hardware/reva/build.py --sheet <name>`. Expected: `PASS sheet <name>`. Open the printed PNG with the Read tool and note in the report what it shows: all labels readable, no text on text, power symbols pointing away from their parts.
**If `chains` fails `[sheet_edge]`:** move the four keys and their pull-ups into a new sheet builder `keys(refs)`, titled "Keys", placed after `chains` in `project()`. Record this as a deviation from spec §2, since the keys sheet becomes its own sheet. No other layout change is allowed.

- [ ] **Step 6: Full level and the waivers**

Run: `python hardware/gen/check.py --project hardware/reva/build.py --full`.
Every `[erc]` finding is either fixed in `blocks.py` or waived:
- **Waive only** a Patch SM pin type naming an unused default role, and a derived-symbol `lib_symbol_mismatch` if Task 1 kept that waiver. Inputs fed through the 1 k series resistors need no waiver: KiCad counts a passive pin as a driver (measured, see Facts).
- Expected Patch SM lines, one violation each, with the exact item text copied from `hardware/reva/out/erc.json`:
  - `pin_to_pin` on `U_SM` pin D10, the SPI_SCK output. Reason: "D10 is a GPIO input in firmware (165 serial out); the symbol names its SPI_SCK default".
  - `pin_not_driven` on the 595's SER, fed from `U_SM` B7, which is open_collector. Reason: "B7 drives SR_DATA push-pull as GPIO; the symbol names its I2C1_SCL default".
- Anything else is a defect to fix, and the report lists it with its fix.
Rerun until `PASS full`. Open the overview PNG and every sheet PNG.

- [ ] **Step 7: Commit**

Run: `python hardware/reva/test_assign.py`, `python hardware/gen/test_check.py` → ok.
```bash
git add hardware/reva
```
```bash
git commit -m "hw(reva): part catalogue and the power, module, chain, LED, jack and SD sheets

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The mux regions, the committed schematic and its guards

**Files:**
- Modify: `hardware/reva/blocks.py` (add `mux_region`)
- Modify: `hardware/reva/build.py` (all sheets, `write_all`, `main`)
- Create: `hardware/reva/kicad/` (generated: `reva.kicad_sch`, one `.kicad_sch` per sheet, `sym-lib-table`, `fp-lib-table`)
- Create: `hardware/reva/test_build.py`
- Modify: `hardware/reva/erc-waivers.txt` (only if the mux sheets bring a waivable violation)
- Modify: `CMakeLists.txt`
- Modify: `docs/superpowers/specs/2026-09-29-rev-a-p3-schematic-design.md` (§4, Rev A's measured full-level time)

**Interfaces:**
- Consumes: Task 3's `blocks`, `parts.make`, `build.project`; `panel-map.json`.
- Produces:
  - `blocks.mux_region(refs, panel_map, sense) -> Sheet`, named `mux_sense_<n>`.
  - `build.write_all(root) -> [written paths relative to root]`, which writes `root/kicad/...`. Task 5 adds the BOMs and `review.md`.
  - `build.main()`.

- [ ] **Step 1: `mux_region`**

Append to `hardware/reva/blocks.py`:
```python
CAL_NETS = {"CAL_GND": GND, "CAL_3V3": SM3V3}     # panel-scan spec §8


def mux_region(refs, panel_map, sense):
    """One sense pin's muxes and pots (P2 §3), assigned by assign.py."""
    muxes = {}
    parts = []
    for mux in panel_map["muxes"][sense]:
        u = make("mux", "U_MUX%d" % mux, domain="analog", strict=True)
        u.by_name("VCC", SM3V3).by_name("VEE", GND).by_name("GND", GND)
        u.by_name("A", sense)                  # the 4051 symbol calls COM "A"
        u.by_name("~{E}", MUX_EN[mux])
        for i, net in enumerate(MUX_S):
            u.by_name("S%d" % i, net)
        muxes[mux] = u
        parts += [u, _decouple(refs, SM3V3, "analog")]
    for c in panel_map["calibration"]:
        if c["sense"] == sense:
            muxes[c["mux"]].by_name("A%d" % c["channel"], CAL_NETS[c["id"]])
    for s in panel_map["spare"]:
        if s["sense"] == sense:
            muxes[s["mux"]].by_name("A%d" % s["channel"], GND)
    for p in sorted((p for p in panel_map["pots"] if p["sense"] == sense),
                    key=lambda p: (p["mux"], p["channel"])):
        wiper = "M%d_CH%d" % (p["mux"], p["channel"])
        muxes[p["mux"]].by_name("A%d" % p["channel"], wiper)
        rv = make("pot", refs("RV"), value=p["id"], panel_id=p["id"], panel=True,
                  domain="analog", strict=True)
        parts.append(rv.by_number(1, GND).by_number(2, wiper).by_number(3, SM3V3))
    return Sheet("mux_" + sense.lower(), "Pot scan on %s: muxes %s" % (
        sense, ", ".join("U_MUX%d" % m for m in panel_map["muxes"][sense])), parts)
```

- [ ] **Step 2: All sheets in the project**

In `build.py`, set `COVERED = {"jack", "key", "led", "sd", "pot"}`, and in `project()` build the sheet list as:
```python
    sheets = [blocks.power(refs), blocks.module(refs)]
    sheets += [blocks.mux_region(refs, pm, s) for s in pm["sense_order"]]
    sheets += [blocks.chains(refs), blocks.leds(refs, pm["leds"]),
               blocks.jacks(refs, [h for h in holes if h["kind"] == "jack"]),
               blocks.sd(refs)]
```
If Task 3 added a `keys` sheet, keep it right after `chains`. Run `--fast`, then `--sheet mux_sense_0` and the other three. Open each PNG: the U_MUXn GND and VEE share one symbol, the pots are readable, and nothing overlaps. Then run `--full` until it passes, applying Task 3's waiver rule unchanged.

- [ ] **Step 3: The committed files**

Append to `build.py`:
```python
from gen import check as C          # noqa: E402
from gen import sch_writer as W     # noqa: E402

KICAD = "kicad"
# In the committed tables vendored libraries resolve relative to the
# project, so nothing machine-specific reaches the repository.
LIB_URIS = {"Daisy-Boards": "${KIPRJMOD}/../../lib/DaisyKiCad",
            "Thonk": "${KIPRJMOD}/../../lib/Thonk"}


def write_all(root=HERE):
    """Write every generated Rev A file under `root`; return their paths
    relative to `root`, sorted. The guard writes to a temp dir and compares."""
    proj = project()
    kdir = os.path.join(root, KICAD)
    os.makedirs(kdir, exist_ok=True)
    for name in os.listdir(kdir):                # a renamed sheet must not linger
        if name.endswith(".kicad_sch"):
            os.remove(os.path.join(kdir, name))
    W.write_project(proj, kdir)
    C.write_lib_tables(proj.parts(), kdir, LIB_URIS, extra_sym_libs={"power"})
    return sorted(os.path.relpath(os.path.join(kdir, n), root).replace("\\", "/")
                  for n in os.listdir(kdir))


def main():
    for path in write_all():
        print("wrote hardware/reva/" + path)


if __name__ == "__main__":
    main()
```
Run: `python hardware/reva/build.py`. It lists 11 `.kicad_sch` files and the two tables.

- [ ] **Step 4: The guard (RED first)**

Create `hardware/reva/test_build.py`:
```python
#!/usr/bin/env python3
"""The committed Rev A files are exactly what build.py writes today.

Byte for byte, except git's own autocrlf (a Windows checkout turns LF into
CRLF). A stale schematic, BOM or review sheet goes red here; the check tool's
full level (ctest reva_check_guard) proves the schematic itself.

    python hardware/reva/test_build.py
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build  # noqa: E402


def read(path):
    with open(path, encoding="utf-8", newline="") as fh:
        return fh.read().replace("\r\n", "\n")


def main():
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        written = build.write_all(tmp)
        if len(written) < 13:
            failures.append("build.write_all wrote only %d files" % len(written))
        for rel in written:
            committed = os.path.join(HERE, rel)
            if not os.path.exists(committed):
                failures.append("%s is not committed -- run python hardware/reva/build.py" % rel)
            elif read(committed) != read(os.path.join(tmp, rel)):
                failures.append("%s is stale -- run python hardware/reva/build.py" % rel)
        kdir = os.path.join(HERE, build.KICAD)
        extra = sorted(set("%s/%s" % (build.KICAD, n) for n in os.listdir(kdir))
                       - set(written))
        failures += ["%s is committed but no longer generated" % e for e in extra]
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: %d generated Rev A files match the committed ones" % len(written))
    return 0


if __name__ == "__main__":
    sys.exit(main())
```
Run it. Expected: `ok: 13 generated Rev A files ...`. Show it RED: temporarily change a comment string in `build.py`'s `comments=[...]` without rerunning `build.py`. Expected: `FAIL kicad/... is stale`. Revert with Edit and rerun ok.

- [ ] **Step 5: ctest**

Append to the ctest block in `CMakeLists.txt`:
```cmake
add_test(NAME reva_build_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_build.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# The Rev A schematic at the check tool's full level: every rule, the
# netlist against the intent, ERC against hardware/reva/erc-waivers.txt.
add_test(NAME reva_check_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/check.py
                 --project ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/build.py
                 --full --out ${CMAKE_BINARY_DIR}/reva-check
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```
Reconfigure, then run `ctest --test-dir build -R reva --output-on-failure`. Expected: 3/3 (assign, build, check).

- [ ] **Step 6: Measure, record, commit**

Run the full level twice; the second run is warm. In spec §4, replace the clause `so Rev A's real figure is measured again in plan 2` with `Rev A itself (measured <date>, <N> parts, 11 sheets): full level <total> s, ERC <erc> s`, using the second run's `timing:` line and total.
```bash
git add hardware/reva CMakeLists.txt docs/superpowers/specs/2026-09-29-rev-a-p3-schematic-design.md
```
```bash
git commit -m "hw(reva): mux regions and the committed Rev A schematic, guarded in ctest

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: BOMs and the review sheet

**Files:**
- Create: `hardware/gen/bom.py`, `hardware/gen/test_bom.py`
- Create: `hardware/reva/review.py`
- Modify: `hardware/reva/build.py` (`write_all` also writes `bom-jlc.csv`, `bom-hand.csv`, `review.md`)
- Create (generated): `hardware/reva/bom-jlc.csv`, `hardware/reva/bom-hand.csv`, `hardware/reva/review.md`
- Modify: `CMakeLists.txt`, `docs/roadmap.md`

**Interfaces:**
- Consumes: `Part` fields (`lcsc`, `source`, `note`, `dnp`, `in_bom`), `parts.CATALOGUE`, `check.load_waivers`, the panel map.
- Produces:
  - `bom.jlc_rows(parts) -> [dict]`, with keys `JLC_FIELDS = ("Comment", "Designator", "Footprint", "LCSC")`.
  - `bom.hand_rows(parts) -> [dict]`, with keys `HAND_FIELDS = ("Source", "Qty", "Part", "Footprint", "Designators")`.
  - `bom.to_csv(rows, fields) -> str`.
  - `review.review_md(project, panel_map, waivers, catalogue) -> str`.

- [ ] **Step 1: Write the failing BOM test**

Create `hardware/gen/test_bom.py`:
```python
#!/usr/bin/env python3
"""BOM grouping: JLC lines by (value, footprint, LCSC), natural designator
order; DNP parts, flags and parts kept out of the BOM appear nowhere; hand
parts group by (source, footprint, note).

    python hardware/gen/test_bom.py
"""
import os
import sys

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")))
from gen import bom                  # noqa: E402
from gen.netlist import Part         # noqa: E402

R = "Resistor_SMD:R_0603_1608Metric"


def parts():
    return [Part("R10", "Device:R", "1k", R, lcsc="C21190"),
            Part("R2", "Device:R", "1k", R, lcsc="C21190"),
            Part("R3", "Device:R", "10k", R, lcsc="C25804"),
            Part("C9", "Device:C", "100n", "Capacitor_SMD:C_0603_1608Metric",
                 lcsc="C14663", dnp=True),
            Part("TP1", "Connector:TestPoint", "TP", "TestPoint:TestPoint_Pad_D1.5mm",
                 in_bom=False),
            Part("#FLG0001", "power:PWR_FLAG", "PWR_FLAG", ""),
            Part("D2", "Device:LED", "B", "LED_THT:LED_D3.0mm", source="Thonk", note="3 mm LED"),
            Part("D1", "Device:LED", "A", "LED_THT:LED_D3.0mm", source="Thonk", note="3 mm LED")]


def main():
    failures = []
    jlc = bom.jlc_rows(parts())
    want = [{"Comment": "1k", "Designator": "R2,R10", "Footprint": "R_0603_1608Metric",
             "LCSC": "C21190"},
            {"Comment": "10k", "Designator": "R3", "Footprint": "R_0603_1608Metric",
             "LCSC": "C25804"}]
    if jlc != want:
        failures.append("jlc_rows: got %s" % jlc)
    hand = bom.hand_rows(parts())
    if hand != [{"Source": "Thonk", "Qty": 2, "Part": "3 mm LED",
                 "Footprint": "LED_D3.0mm", "Designators": "D1,D2"}]:
        failures.append("hand_rows: got %s" % hand)
    text = bom.to_csv(jlc, bom.JLC_FIELDS)
    if not text.startswith('"Comment","Designator","Footprint","LCSC"\n'):
        failures.append("jlc csv header: %r" % text.splitlines()[0])
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: BOM grouping, exclusions and CSV header")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```
Run it. Expected: `ImportError: cannot import name 'bom'`.

- [ ] **Step 2: `hardware/gen/bom.py`**

```python
#!/usr/bin/env python3
"""Bills of materials from a part list (P3 spec §3.1).

jlc_rows: what JLCPCB assembles -- every real, populated part with an LCSC
number, one line per (value, footprint, LCSC). hand_rows: what is bought
elsewhere and hand-soldered -- parts with a Source, one line per (source,
footprint, note). DNP parts, power symbols and flags, and parts kept out of
the BOM (test points) appear in neither.
"""
import csv
import io
import re

from gen import netlist as N

JLC_FIELDS = ("Comment", "Designator", "Footprint", "LCSC")
HAND_FIELDS = ("Source", "Qty", "Part", "Footprint", "Designators")


def _natural(ref):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", ref)]


def _bought(parts):
    return [p for p in parts if not N.is_virtual(p.ref) and p.in_bom and not p.dnp]


def jlc_rows(parts):
    groups = {}
    for p in _bought(parts):
        if p.lcsc:
            groups.setdefault((p.value, p.footprint, p.lcsc), []).append(p.ref)
    rows = [{"Comment": value, "Designator": ",".join(sorted(refs, key=_natural)),
             "Footprint": fp.split(":", 1)[-1], "LCSC": lcsc}
            for (value, fp, lcsc), refs in groups.items()]
    return sorted(rows, key=lambda r: _natural(r["Designator"].split(",")[0]))


def hand_rows(parts):
    groups = {}
    for p in _bought(parts):
        if p.source and not p.lcsc:
            groups.setdefault((p.source, p.footprint, p.note), []).append(p)
    rows = []
    for (source, fp, note), ps in groups.items():
        rows.append({"Source": source, "Qty": len(ps), "Part": note or ps[0].value,
                     "Footprint": fp.split(":", 1)[-1] if fp else "(no footprint)",
                     "Designators": ",".join(sorted((p.ref for p in ps), key=_natural))})
    return sorted(rows, key=lambda r: (r["Source"], r["Part"]))


def to_csv(rows, fields):
    buf = io.StringIO()
    w = csv.DictWriter(buf, fieldnames=fields, lineterminator="\n",
                       quoting=csv.QUOTE_ALL)
    w.writeheader()
    w.writerows(rows)
    return buf.getvalue()
```
Run the test. Expected: `ok`. Show it RED once by removing `and not p.dnp` from `_bought`. Expected: a `jlc_rows` FAIL, because C9 appears. Revert with Edit.

- [ ] **Step 3: `hardware/reva/review.py`**

```python
#!/usr/bin/env python3
"""The Rev A review sheet (P3 spec §7 stage 5), generated -- never edited.

It summarises what a reviewer checks before the layout starts: the sheets,
what JLC fits (and which Extended types it charges a loading fee for), what
is hand-soldered, the pot scan regions, every ERC waiver with its reason, and
the open items. The drawing itself is the PDF from the check tool.
"""
import math
import os
import sys

_HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if _HW not in sys.path:
    sys.path.insert(0, _HW)

from gen import bom as B            # noqa: E402
from gen import netlist as N        # noqa: E402

OPEN_ITEMS = (
    "SD socket footprint is open (`P4`); J_SD's part and LCSC number come with it.",
    "P2's pin table calls C1 'CV out 1' and C10 'CV out 2'; the Patch SM datasheet "
    "names C1 CV_OUT_2 and C10 CV_OUT_1. The wiring (C1 = PITCH_A, C10 = PITCH_B) "
    "follows P2 by pin; P6's firmware table must follow the pins, not the names.",
    "AMS1117-3.3 output: 22 µF X5R MLCC (C45783). Stability with a low-ESR output "
    "capacitor is a bring-up check (P7), with the regulator's temperature at full LED load.",
    "SD card on SM_3V3 (A10): pot-scan noise while the card streams is a bring-up check.",
    "COM pads C_SENSE0-3 are fitted unpopulated (P2 §3).",
    "JLC stock is checked at the schematic freeze (13 Nov) and again before the order.",
)


def _worst(rows):
    cx = sum(r["x_mm"] for r in rows) / len(rows)
    cy = sum(r["y_mm"] for r in rows) / len(rows)
    return max(math.hypot(r["x_mm"] - cx, r["y_mm"] - cy) for r in rows)


def review_md(project, panel_map, waivers, catalogue):
    parts = project.parts()
    real = [p for p in parts if not N.is_virtual(p.ref)]
    nets = {n for p in real for n in p.nets.values()}
    out = ["# FireFlow Rev A -- schematic review", "",
           "Generated by `hardware/reva/build.py`; do not edit. The drawing: "
           "`python hardware/gen/check.py --project hardware/reva/build.py --full` "
           "writes `hardware/reva/out/reva.pdf` and one PNG per sheet.", "",
           "%d parts, %d nets, %d sheets." % (len(real), len(nets), len(project.sheets)), "",
           "## Sheets", "", "| Sheet | Title | Parts |", "|---|---|---|"]
    for s in project.sheets:
        out.append("| %s | %s | %d |" % (s.name, s.title,
                                        len([p for p in s.parts if not N.is_virtual(p.ref)])))
    jlc = B.jlc_rows(parts)
    types = {t.lcsc: t for t in catalogue.values() if t.lcsc}
    extended = sorted({r["LCSC"] for r in jlc if types[r["LCSC"]].jlc_type == "Extended"})
    out += ["", "## JLC assembly", "",
            "%d lines, %d parts. Extended types (one loading fee each, price read on "
            "the JLC page on the day): %s." % (
                len(jlc), sum(len(r["Designator"].split(",")) for r in jlc),
                ", ".join("%s (%s)" % (l, types[l].note) for l in extended) or "none"),
            "", "| LCSC | Type | Value | Qty |", "|---|---|---|---|"]
    for r in jlc:
        out.append("| %s | %s | %s | %d |" % (r["LCSC"], types[r["LCSC"]].jlc_type,
                                              r["Comment"], len(r["Designator"].split(","))))
    out += ["", "## Hand-soldered", "", "| Source | Part | Qty |", "|---|---|---|"]
    for r in B.hand_rows(parts):
        out.append("| %s | %s | %d |" % (r["Source"], r["Part"], r["Qty"]))
    out += ["", "## Pot scan", "", "| Sense pin | Muxes | Pots | Farthest pot from its "
            "mux group's centre |", "|---|---|---|---|"]
    for sense in panel_map["sense_order"]:
        rows = [p for p in panel_map["pots"] if p["sense"] == sense]
        worst = max(_worst([r for r in rows if r["mux"] == m])
                    for m in panel_map["muxes"][sense] if any(r["mux"] == m for r in rows))
        out.append("| %s | %s | %d | %.1f mm |" % (
            sense, ", ".join("U_MUX%d" % m for m in panel_map["muxes"][sense]),
            len(rows), worst))
    out += ["", "## ERC waivers", ""]
    out += (["| Type | Item | Count | Reason |", "|---|---|---|---|"]
            + ["| %s | `%s` | %d | %s |" % w for w in waivers]) if waivers else ["None."]
    out += ["", "## Open items", ""] + ["- " + item for item in OPEN_ITEMS]
    return "\n".join(out) + "\n"
```

- [ ] **Step 4: `write_all` writes the BOMs and the review sheet**

In `build.py`, add `import review` and `from gen import bom as B` to the imports. Then in `write_all`, before the `return`, write the three files:
```python
    files = {"bom-jlc.csv": B.to_csv(B.jlc_rows(proj.parts()), B.JLC_FIELDS),
             "bom-hand.csv": B.to_csv(B.hand_rows(proj.parts()), B.HAND_FIELDS),
             "review.md": review.review_md(proj, load_panel_map(),
                                           C.load_waivers(WAIVERS), parts.CATALOGUE)}
    for name, text in files.items():
        with open(os.path.join(root, name), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
```
Add `import parts` too. Return the kicad paths plus the three names, sorted:
```python
    written = [os.path.relpath(os.path.join(kdir, n), root).replace("\\", "/")
               for n in os.listdir(kdir)] + list(files)
    return sorted(written)
```
Run `python hardware/reva/build.py`, then `python hardware/reva/test_build.py`. Expected: `ok: 16 generated Rev A files ...`. Read `review.md` and both CSVs.
- Every LCSC in `bom-jlc.csv` is one of the spec addendum's table or C45783.
- The hand list holds the Thonk parts (70 pots, 18 jacks, 4 keys, 19 LEDs), the header and the four sockets (hand-soldered), the module (Electrosmith), and J_SD (`P4`).

- [ ] **Step 5: ctest, roadmap, commit**

Append to the ctest block:
```cmake
add_test(NAME hw_gen_bom_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_bom.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```
Reconfigure, then run the whole suite: `ctest --test-dir build --output-on-failure`. Expected: all pass.
In `docs/roadmap.md`, add a dated entry at the top of the living-status section, in the style of the 2026-09-29 P1 entry. Content: Rev A P3 schematic generated, eleven A3 sheets (overview + ten), ERC clean against N waivers, JLC BOM with the Extended types named, review sheet at `hardware/reva/review.md`, guarded by `reva_build_guard` and `reva_check_guard`. Use the real numbers from `review.md`.
```bash
git add hardware/gen/bom.py hardware/gen/test_bom.py hardware/reva CMakeLists.txt docs/roadmap.md
```
```bash
git commit -m "hw(reva): JLC and hand-solder BOMs, generated review sheet

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Not in this plan

- **Placement, routing, board outline and the SD socket's part** belong to P4, after the P4a routing spike.
- **The stock check** happens at the freeze (13 Nov) and before the order, in the browser, dated in `hardware/reva/stock-check.md`.
- **The firmware tables** belong to P6 and are read from `panel-map.json`.
