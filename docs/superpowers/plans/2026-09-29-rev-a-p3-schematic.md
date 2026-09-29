# Rev A P3 — shared generator tools and check tool (plan 1 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the coupon's schematic tools into a shared `hardware/gen/` package, extend the writer to multi-sheet projects, build the three-level check tool before any Rev A sheet exists, and compute Rev A's pot and LED assignment from the P1 hole list.

**Architecture:** `hardware/gen/` holds everything that knows nothing about Rev A: S-expression reading (`ksexp`), the `Part` bookkeeping (`netlist`), a `Project`/`Sheet` container (`project`), the schematic writer (`sch_writer`) and the check tool (`check`). The coupon is rewired onto it and proves the move by reproducing its committed netlist. A three-sheet demo fixture exercises every writer and check feature through `kicad-cli`. `hardware/reva/assign.py` is the first Rev A file; it needs no research.

**Scope split.** This plan covers P3 spec stages 2, 2b and the assignment part of stage 3. **Plan 2** — `parts.py`, `blocks.py`, `build.py`, `erc-waivers.txt`, the eleven Rev A sheets, `gen/bom.py` with the JLC BOM, the review sheet and the stock check — is written after stage 1 (research, spec §6) lands, because the jack and SD sheets depend on Electrosmith's patch.Init() schematic. Research is controller work in the main session (downloads need Bastian's go-ahead), not a task here.

**Tech Stack:** Python 3.14 (`C:/Python314/python.exe`, the interpreter ctest uses), `kicad-cli` 10.0, PyMuPDF (`fitz`, installed) for PNGs. No `pcbnew` in `hardware/gen/`.

**Spec:** [`docs/superpowers/specs/2026-09-29-rev-a-p3-schematic-design.md`](../specs/2026-09-29-rev-a-p3-schematic-design.md)

## Global Constraints

- Never prefix a shell command with `cd`; use absolute or repo-relative paths. No shell writes (no `>`, `tee`, `sed -i`): files are written with the Write/Edit tools. Never chain `git add` and `git commit`.
- Everything written into the repo is English.
- Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- The coupon's committed artefacts (`hardware/coupon/*.kicad_*`, `hardware/coupon/fab/`, `hardware/coupon/proof/`, `hardware/coupon/*-lib-table`) are **not regenerated or edited**. Writer tests write to temporary directories.
- Tests are plain scripts; the exit code is the verdict (pytest is not installed). Each is wired into ctest and shown RED once before it is trusted. A RED-proof sabotage in a source file is reverted with the Edit tool, never with `git checkout`.
- `kicad-cli` comes from `gen.ksexp.KICAD_CLI`. A missing `kicad-cli` fails a test; it never skips.
- Text on every sheet is 1.27 mm (spec §2: never smaller).
- UUIDs are derived, never random: two writes of the same project are byte-identical.
- Net names never contain `/` (KiCad exports sheet-local nets as `/<sheet>/<net>`; `check.normalize` strips that prefix).
- After adding a ctest entry, reconfigure before running: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, then `ctest --test-dir build -R <name> --output-on-failure`.

## Facts measured for this plan (2026-09-29)

- Coupon intent against its committed `hardware/coupon/fab/coupon.net`: 66 intended nets, **0 differences**.
- All coupon scripts import cleanly under KiCad's Python (`import build_pcb, check_layout, review, assembly_plan, kipcb, build, generate_schematic` → ok).
- `kicad-cli` on the coupon: netlist 0.9 s, ERC 6.1 s, PDF 0.4 s; PyMuPDF renders a PDF page to PNG in 0.2 s.
- Hierarchy probe (scratch schematic, two sheets): a `power:+3V3` symbol with Value `A3V3` and another with Value `3V3D` export as **two nets named `A3V3` and `3V3D`** — the Value names the net. A sheet-local `label "LOCAL_NET"` on sheet `power` exports as **`/power/LOCAL_NET`**. Sub-sheet symbol instances use the path `/<root uuid>/<sheet-node uuid>`; the root carries `(sheet_instances (path "/" (page "1")))`; no `.kicad_pro` is needed for a netlist export.
- ERC JSON (`--format json`): top-level `sheets[].violations[]`, each with `type`, `severity`, `description` (**localized — German here**) and `items[].description` (English, e.g. `Symbol U_IN1 Pin 9 [Q7, Output, Line]`). Waivers therefore match on `type` plus text from an item description, never on `description`.
- `Amplifier_Operational:TL072` extends `LM2904`, units `LM2904_1_1`, `_2_1`, `_3_1` (pins 1–3, 5–7, 4/8); every `power:*` symbol has exactly one pin, numbered `1`.
- The P1 hole list has 70 `pot`, 19 `led`, 18 `jack`, 4 `key`, 4 `mount`, 1 `sd`; every hole carries `ids`. The 70 pots sit in 63 distinct x columns, and 976 contiguous four-band splits fit the mux capacities.

---

### Task 1: `hardware/gen/` package, coupon rewired onto it

**Files:**
- Create: `hardware/gen/__init__.py`
- Move: `hardware/coupon/scripts/ksexp.py` → `hardware/gen/ksexp.py` (`git mv`, content unchanged)
- Create: `hardware/gen/netlist.py`
- Create: `hardware/gen/check.py`
- Create: `hardware/gen/test_coupon_netlist.py`
- Modify: `hardware/coupon/scripts/netlist.py` (lines 9–75 and 360–377: moved parts removed, imports added)
- Modify: `hardware/coupon/scripts/build.py` (imports; `run`, `parse_exported_netlist`, `compare`, `write_lib_tables` removed)
- Modify: `hardware/coupon/scripts/generate_schematic.py`, `build_pcb.py`, `assembly_plan.py` (the `import ksexp` line only)
- Modify: `CMakeLists.txt` (after the `hw_cut_guard` block)

**Interfaces:**
- Produces: package `gen` (importable once `hardware/` is on `sys.path`); `gen.ksexp` (unchanged API); `gen.netlist.Part(ref, lib_id, value, footprint, note="", lcsc="", source="", panel_id="", domain="", strict=False)` with `.by_number(n, net)`, `.by_name(name, net)`, `.no_connect(*numbers)`, `.unconnected()`, attributes `.nets` (pin → net), `.nc` (set of pins), `.sym`; `gen.netlist.load(lib_id)`, `is_virtual(ref)`, `nets_from(parts, include_virtual=True)`; `gen.check.run(args, what, quiet=False) -> (rc, text)`, `parse_exported_netlist(path) -> {net: {(ref, pin)}}`, `compare(intended, exported) -> [str]`, `write_lib_tables(parts, dest_dir, vendored_uri, extra_sym_libs=()) -> (sym_libs, fp_libs)`, `kicad_defines() -> [str]`.

- [ ] **Step 1: Confirm the baseline is green before touching anything**

Run:
```bash
python -c "import sys; sys.path.insert(0,'hardware/coupon/scripts'); import build as B, netlist as N; e=B.parse_exported_netlist('hardware/coupon/fab/coupon.net'); i={k:set(v) for k,v in N.nets_from(N.build(),include_virtual=False).items()}; print(len(i), B.compare(i,e))"
```
Expected: `66 []`. If anything else prints, stop and report — do not regenerate coupon files.

- [ ] **Step 2: Write the failing test**

Create `hardware/gen/test_coupon_netlist.py`:
```python
#!/usr/bin/env python3
"""The coupon's intent still equals its committed, KiCad-exported netlist.

Proof for the move of the shared tools into hardware/gen/ (P3 spec §3.1): if
ksexp, Part or nets_from changed behaviour in the move, a node moves here. The
committed hardware/coupon/fab/coupon.net was exported by KiCad from the
coupon's schematic, so it is an independent read of the same intent.

    python hardware/gen/test_coupon_netlist.py      # exit code is the verdict
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HW)
sys.path.insert(0, os.path.join(HW, "coupon", "scripts"))

from gen import check                      # noqa: E402
import netlist as coupon_netlist           # noqa: E402  (the coupon's own)

COMMITTED = os.path.join(HW, "coupon", "fab", "coupon.net")


def main():
    intended = {k: set(v) for k, v in coupon_netlist.nets_from(
        coupon_netlist.build(), include_virtual=False).items()}
    exported = check.parse_exported_netlist(COMMITTED)
    failures = []
    if len(intended) < 60:
        failures.append("only %d intended nets -- the coupon has 66" % len(intended))
    bad = check.compare(intended, exported)
    failures += bad
    # The comparison must be able to fail: a planted node has to be reported.
    victim = sorted(intended)[0]
    planted = dict(intended)
    planted[victim] = set(intended[victim]) | {("X_PLANTED", "1")}
    if not check.compare(planted, exported):
        failures.append("compare() missed a planted node on net %s" % victim)
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: %d coupon nets match the committed coupon.net node for node"
          % len(intended))
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Run it to verify it fails**

Run: `python hardware/gen/test_coupon_netlist.py`
Expected: `ModuleNotFoundError: No module named 'gen'`.

- [ ] **Step 4: Create the package and move `ksexp`**

Create `hardware/gen/__init__.py`:
```python
"""Generator tools shared by the generated boards (coupon, Rev A).

Import as `from gen import ksexp`, with hardware/ on sys.path. P3 spec
(docs/superpowers/specs/2026-09-29-rev-a-p3-schematic-design.md) §3.1.
"""
```
Run: `git mv hardware/coupon/scripts/ksexp.py hardware/gen/ksexp.py`

- [ ] **Step 5: Create `hardware/gen/netlist.py`**

```python
#!/usr/bin/env python3
"""Parts, pins and nets, shared by the generated boards.

Moved from hardware/coupon/scripts/netlist.py; the coupon's build() stayed
there. Pins are resolved by NAME through the symbol library, never by number
typed in: `sym.by_name("QH'")` raises if the symbol ever changes, where a
hard-coded 11 would quietly wire the wrong leg. Numbers are used only for
symbols whose pins have no names (Device:R, Device:C -- pins "1"/"2").

Added for Rev A (P3 spec §3.1): `lcsc`, `source` and `panel_id` travel into
the schematic as fields; `domain` ("analog"/"digital") and `strict` feed the
fast checks; `no_connect()` marks a pin unconnected on purpose.
"""
import os

from gen import ksexp

EXTRA_SYMBOL_DIRS = [os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "lib", "DaisyKiCad"))]


class Part:
    def __init__(self, ref, lib_id, value, footprint, note="", lcsc="",
                 source="", panel_id="", domain="", strict=False):
        self.ref, self.lib_id = ref, lib_id
        self.value, self.footprint = value, footprint
        self.note = note
        self.lcsc, self.source, self.panel_id = lcsc, source, panel_id
        self.domain = domain
        self.strict = strict       # every pin must carry a net or a no_connect
        self.sym = load(lib_id)
        self.nets = {}             # pin number -> net name
        self.nc = set()            # pins left unconnected on purpose

    def by_number(self, number, net):
        number = str(number)
        self.sym.pin(number)                # raises if absent
        if number in self.nets:
            raise ValueError("%s pin %s assigned twice" % (self.ref, number))
        if number in self.nc:
            raise ValueError("%s pin %s is marked no-connect" % (self.ref, number))
        self.nets[number] = net
        return self

    def by_name(self, pin_name, net):
        return self.by_number(self.sym.by_name(pin_name), net)

    def no_connect(self, *numbers):
        for number in numbers:
            number = str(number)
            self.sym.pin(number)
            if number in self.nets:
                raise ValueError("%s pin %s carries net %s; cannot be no-connect"
                                 % (self.ref, number, self.nets[number]))
            self.nc.add(number)
        return self

    def unconnected(self):
        return sorted(set(self.sym.pins) - set(self.nets), key=ksexp._pin_sort_key)


_cache = {}


def load(lib_id):
    """Symbol lookup that also searches the vendored library directories."""
    if lib_id in _cache:
        return _cache[lib_id]
    try:
        sym = ksexp.load_symbol(lib_id)
    except FileNotFoundError:
        lib, _, name = lib_id.partition(":")
        for extra in EXTRA_SYMBOL_DIRS:
            path = os.path.join(extra, lib + ".kicad_sym")
            if os.path.exists(path):
                root = ksexp.parse_file(path)
                table = {str(s[1]): s for s in ksexp.children(root, "symbol")}
                if name not in table:
                    raise KeyError("%s not in %s" % (name, path))
                sym = ksexp.Symbol(lib, name, table[name], table)
                break
        else:
            raise
    _cache[lib_id] = sym
    return sym


def is_virtual(ref):
    """Power symbols and flags: real in the schematic, absent from the netlist.

    KiCad does not emit a node for a PWR_FLAG pin, so comparing an exported
    netlist against the intent has to leave them out or every rail reports a
    missing node.
    """
    return ref.startswith("#")


def nets_from(parts, include_virtual=True):
    nets = {}
    for p in parts:
        if not include_virtual and is_virtual(p.ref):
            continue
        for pin, net in p.nets.items():
            nets.setdefault(net, []).append((p.ref, pin))
    return nets
```

- [ ] **Step 6: Create `hardware/gen/check.py` (first part: the netlist proof)**

```python
#!/usr/bin/env python3
"""Checks for generated schematics: KiCad's own netlist against the intent.

Moved from hardware/coupon/scripts/build.py (P3 spec §3.1). The comparison is
the load-bearing check: a schematic's connectivity is geometry -- stub
directions, a symbol-space Y flip -- and geometry is exactly what a generator
gets quietly wrong. KiCad's exported netlist is the independent read.
"""
import os
import subprocess
import sys

_HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if _HW not in sys.path:
    sys.path.insert(0, _HW)

from gen import ksexp  # noqa: E402

VENDORED_LIBS = {"Daisy-Boards"}


def run(args, what, quiet=False):
    r = subprocess.run(args, capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    if not quiet:
        print("  %-28s rc=%d" % (what, r.returncode))
        if out:
            for line in out.splitlines()[:25]:
                print("      " + line)
    return r.returncode, out


def parse_exported_netlist(path):
    """{net name: {(ref, pin), ...}} from KiCad's own export."""
    root = ksexp.parse_file(path)
    nets = {}
    for net in ksexp.children(ksexp.child(root, "nets"), "net"):
        name = str(ksexp.child(net, "name")[1])
        nodes = set()
        for node in ksexp.children(net, "node"):
            nodes.add((str(ksexp.child(node, "ref")[1]),
                       str(ksexp.child(node, "pin")[1])))
        nets[name] = nodes
    return nets


def compare(intended, exported):
    """Report every difference. Returns a list of complaints."""
    bad = []
    for name, nodes in sorted(intended.items()):
        want = set(nodes)
        if name not in exported:
            bad.append("net %s is missing from the exported netlist "
                       "(intended nodes: %s)"
                       % (name, ", ".join("%s.%s" % n for n in sorted(want))))
            continue
        got = exported[name]
        missing = want - got
        extra = got - want
        if missing:
            bad.append("net %s is missing %s"
                       % (name, ", ".join("%s.%s" % n for n in sorted(missing))))
        if extra:
            bad.append("net %s carries unintended %s"
                       % (name, ", ".join("%s.%s" % n for n in sorted(extra))))
    # KiCad names unconnected single pins "unconnected-(...)"; those are the
    # deliberate no-connects and are not a difference worth reporting.
    for name in sorted(exported):
        if name not in intended and not name.startswith("unconnected-"):
            bad.append("net %s exists in the schematic but not in the intent "
                       "(nodes: %s)"
                       % (name, ", ".join("%s.%s" % n for n in sorted(exported[name]))))
    return bad


def write_lib_tables(parts, dest_dir, vendored_uri, extra_sym_libs=()):
    """Emit sym-lib-table / fp-lib-table covering exactly what the parts use.

    Derived from the part list rather than hand-kept, so a new part cannot
    silently leave the tables behind. KiCad's own libraries go through its
    ${KICAD10_*_DIR} variables; the vendored Daisy library through
    `vendored_uri` (the coupon passes "${KIPRJMOD}/../lib/DaisyKiCad", so
    nothing machine-specific reaches the repository).
    """
    sym_libs = sorted({p.lib_id.split(":")[0] for p in parts} | set(extra_sym_libs))
    fp_libs = sorted({p.footprint.split(":")[0] for p in parts if p.footprint})

    def rows(libs, kind):
        out = []
        for lib in libs:
            if lib in VENDORED_LIBS:
                uri = "%s/%s.%s" % (vendored_uri, lib,
                                    "kicad_sym" if kind == "sym" else "pretty")
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


def kicad_defines():
    """-D arguments kicad-cli needs to resolve the library tables.

    kicad-cli does not inherit KiCad's own path variables, so the project
    library tables resolve to nothing and every symbol is reported as coming
    from a library "the current configuration does not contain" -- 163 of the
    coupon's first 167 ERC violations were that and nothing else.
    """
    share = os.path.join(ksexp.KICAD_ROOT, "share", "kicad")
    return ["-D", "KICAD10_SYMBOL_DIR=" + os.path.join(share, "symbols"),
            "-D", "KICAD10_FOOTPRINT_DIR=" + os.path.join(share, "footprints")]
```

- [ ] **Step 7: Rewire the coupon**

In `hardware/coupon/scripts/netlist.py`, replace lines 9–75 (from `import os` through the end of `load()`) with:
```python
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..")))
import design as D                                        # noqa: E402
from gen import ksexp                                     # noqa: E402,F401
# Moved to hardware/gen/netlist.py (P3 spec §3.1). Re-exported because
# build_pcb, check_layout and review reach them as N.nets_from, NL.load, ...
from gen.netlist import Part, load, is_virtual, nets_from  # noqa: E402,F401

FP_R = "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder"
FP_C = "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder"
FP_LED = "LED_SMD:LED_0805_2012Metric_Pad1.15x1.40mm_HandSolder"
FP_TP = "TestPoint:TestPoint_Pad_D1.5mm"
FP_JP = "Jumper:SolderJumper-2_P1.3mm_Open_Pad1.0x1.5mm"
FP_POT = "Potentiometer_THT:Potentiometer_Alpha_RD901F-40-00D_Single_Vertical"
FP_SOIC16 = "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm"
FP_SOIC24 = "Package_SO:SOIC-24W_7.5x15.4mm_P1.27mm"
```
and delete the old `is_virtual` and `nets_from` definitions (old lines 360–377). Keep `build()` and the `__main__` block unchanged.

In `generate_schematic.py`, `build_pcb.py` and `assembly_plan.py`, replace the line `import ksexp` with:
```python
sys.path.insert(0, os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..")))
from gen import ksexp  # noqa: E402  (moved to hardware/gen, P3 spec §3.1)
```

In `build.py`: replace `import ksexp` the same way and add `from gen import check as C` below it; delete the definitions of `run`, `parse_exported_netlist`, `compare` and `write_lib_tables`; in `main()` replace the calls: `write_lib_tables(N.build())` → `C.write_lib_tables(N.build(), ROOT, "${KIPRJMOD}/../lib/DaisyKiCad")`, `run(` → `C.run(`, `parse_exported_netlist(` → `C.parse_exported_netlist(`, `compare(` → `C.compare(`, and the `share`/`defines` block → `defines = C.kicad_defines()`. Keep the comment on why the defines exist next to the call.

- [ ] **Step 8: Run the test and the import smoke**

Run: `python hardware/gen/test_coupon_netlist.py`
Expected: `ok: 66 coupon nets match the committed coupon.net node for node`

Run:
```bash
"/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" -c "import sys; sys.path.insert(0,'hardware/coupon/scripts'); import build_pcb, check_layout, review, assembly_plan, kipcb, build, generate_schematic; print('ok')"
```
Expected: `ok`

Run: `git status --short` — expected: only the files listed under **Files** above (no coupon artefact touched).

- [ ] **Step 9: Show the test RED once**

With the Edit tool, change `nets.setdefault(net, []).append((p.ref, pin))` in `hardware/gen/netlist.py` to `nets.setdefault(net, []).append((p.ref, pin + "x"))`. Run the test; expected: exit 1 with `FAIL net ... is missing ...` lines. Revert the edit with the Edit tool; rerun; expected: `ok: 66 ...`.

- [ ] **Step 10: Wire into ctest**

In `CMakeLists.txt`, after the `hw_cut_guard` `add_test(...)` block, add:
```cmake
# Rev A P3 (docs/superpowers/specs/2026-09-29-rev-a-p3-schematic-design.md):
# the generator tools shared under hardware/gen/. Plain scripts; the exit code
# is the verdict. Guards that call kicad-cli fail -- never skip -- without it.
add_test(NAME hw_gen_coupon_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_coupon_netlist.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```
Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `ctest --test-dir build -R hw_gen --output-on-failure`
Expected: `100% tests passed`.

- [ ] **Step 11: Commit**

```bash
git add hardware/gen hardware/coupon/scripts CMakeLists.txt
```
```bash
git commit -m "hw(gen): shared generator package, coupon rewired onto it

ksexp, Part/load/nets_from and the netlist proof move to hardware/gen/;
Part gains lcsc/source/panel_id/domain/strict/no_connect for Rev A. The
coupon's intent still equals its committed coupon.net (66 nets).

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: Shared schematic writer — multi-sheet, power symbols, units, stable UUIDs

**Files:**
- Modify: `hardware/gen/ksexp.py` (`Symbol._collect_pins`, `_add_pin`; new `_unit_of`)
- Create: `hardware/gen/project.py`
- Create: `hardware/gen/sch_writer.py`
- Modify: `hardware/gen/check.py` (append `normalize`, `export_netlist`, `intended_nets`)
- Create: `hardware/gen/fixtures/__init__.py` (empty docstring file), `hardware/gen/fixtures/demo.py`
- Modify: `hardware/coupon/scripts/generate_schematic.py` (becomes a thin caller)
- Modify: `hardware/coupon/scripts/build.py` (step 2 uses the new writer API)
- Create: `hardware/gen/test_sch_writer.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1's `gen.netlist.Part`, `load`, `nets_from`; `gen.check.run`, `parse_exported_netlist`, `compare`.
- Produces:
  - `gen.ksexp`: every pin dict gains `"unit"` (int; 0 = common to all units).
  - `gen.project.Sheet(name, title, parts)`; `gen.project.Project(name, title, sheets, power=None, domain_rails=None, holes=None, waivers=None, paper="A3", flat=False, comments=())` with `.parts()`, `.sheet_of() -> {ref: sheet name}`.
  - `gen.sch_writer.write_project(project, out_dir) -> {sheet name: (placed, height)}` where `placed` is `{(ref, unit): (x, y)}`; `layout(parts, paper, power)`, `overlaps(parts, placed, power, tolerance=0.2) -> [(a, b)]`, `fits(height, paper) -> bool`, `cells(parts) -> [(part, unit)]`, class `Uuids` with class attribute `random` (False).
  - `gen.check.normalize(nets)`, `export_netlist(project, sch_dir) -> {net: {(ref, pin)}}`, `intended_nets(project) -> {net: {(ref, pin)}}`.
  - `hardware/gen/fixtures/demo.py`: `project()` returning the three-sheet demo `Project` named `demo` (sheets `power`, `logic`, `leds`).

- [ ] **Step 1: Pins learn their unit**

In `hardware/gen/ksexp.py`, replace `_collect_pins` and the first line of `_add_pin`, and add `_unit_of` after `_pin_sort_key`:
```python
    def _collect_pins(self, node, unit=0):
        """Pins live in the unit sub-symbols, so recurse rather than assume.

        A sub-symbol is named NAME_UNIT_STYLE ("74HC595_1_1"); its pins belong
        to that unit, and unit 0 means "common to every unit".
        """
        for c in node:
            if not isinstance(c, list) or not c or not isinstance(c[0], Atom):
                continue
            if c[0] == "pin":
                self._add_pin(c, unit)
            elif c[0] == "symbol":
                self._collect_pins(c, _unit_of(str(c[1])))

    def _add_pin(self, node, unit=0):
```
and in `_add_pin` change the stored dict to `dict(name=name, etype=etype, x=x, y=y, angle=angle, length=length, unit=unit)`.
```python
def _unit_of(sub_name):
    """`74HC595_1_1` -> 1. KiCad names unit sub-symbols NAME_UNIT_STYLE."""
    parts = sub_name.rsplit("_", 2)
    if len(parts) != 3 or not parts[1].isdigit():
        raise ValueError("not a unit sub-symbol name: %r" % sub_name)
    return int(parts[1])
```
Run: `python -c "import sys; sys.path.insert(0,'hardware'); from gen import ksexp; s=ksexp.load_symbol('Amplifier_Operational:TL072'); print(sorted((n,p['unit']) for n,p in s.pins.items()))"`
Expected: `[('1', 1), ('2', 1), ('3', 1), ('4', 3), ('5', 2), ('6', 2), ('7', 2), ('8', 3)]`

- [ ] **Step 2: Create `hardware/gen/project.py`**

```python
#!/usr/bin/env python3
"""What a generated schematic is made of: sheets of parts, plus the facts the
checks need -- power nets, rail domains, panel holes, the ERC waiver file.
P3 spec §2-§4.
"""
from collections import Counter


class Sheet:
    def __init__(self, name, title, parts):
        if not name or name != name.lower() or not name.replace("_", "").isalnum():
            raise ValueError("sheet name must be lower_snake_case, got %r" % name)
        self.name, self.title, self.parts = name, title, list(parts)


class Project:
    def __init__(self, name, title, sheets, power=None, domain_rails=None,
                 holes=None, waivers=None, paper="A3", flat=False, comments=()):
        self.name, self.title = name, title
        self.sheets = list(sheets)
        self.power = dict(power or {})            # net -> power symbol lib_id
        self.domain_rails = {k: set(v) for k, v in (domain_rails or {}).items()}
        self.holes = holes                        # [{"id", "kind", ...}] or None
        self.waivers = waivers                    # ERC waiver file path or None
        self.paper = paper
        self.flat = flat                          # one sheet, no overview (the coupon)
        self.comments = list(comments)
        if flat and len(self.sheets) != 1:
            raise ValueError("a flat project has exactly one sheet")
        names = [s.name for s in self.sheets]
        if len(set(names)) != len(names):
            raise ValueError("duplicate sheet names: %s" % names)
        if not flat and name in names:
            raise ValueError("sheet %r would overwrite the overview file" % name)
        refs = Counter(p.ref for s in self.sheets for p in s.parts)
        dup = sorted(r for r, n in refs.items() if n > 1)
        if dup:
            raise ValueError("refs used more than once: %s" % ", ".join(dup))
        slashed = sorted({net for p in self.parts() for net in p.nets.values()
                          if "/" in net})
        if slashed:
            raise ValueError("net names may not contain '/': %s" % slashed)

    def parts(self):
        return [p for s in self.sheets for p in s.parts]

    def sheet_of(self):
        return {p.ref: s.name for s in self.sheets for p in s.parts}
```

- [ ] **Step 3: Create `hardware/gen/sch_writer.py`**

```python
#!/usr/bin/env python3
"""Schematic writer shared by the generated boards (coupon, Rev A).

Moved from hardware/coupon/scripts/generate_schematic.py and extended for
Rev A (P3 spec §2-§3): several sheets under an overview sheet, power symbols,
sheet-local labels, every unit of a multi-unit symbol, and UUIDs that come out
the same on every run.

Style, unchanged from the coupon: every pin gets a short stub and a label; no
wires run between symbols. A generated rat's nest of wires is unreadable and
hard to get right, while labels are exactly as connected and say the net name
at every pin.

Connectivity is NOT trusted to this file's geometry. check.py exports the
netlist with kicad-cli and compares it against the intent; a wrong stub
direction or a Y-flip shows up there as a missing node.
"""
import itertools
import os
import uuid

from gen import ksexp
from gen import netlist as N

GRID = 1.27          # mm. Every placement snaps to it, or ERC reports each pin
                     # as "endpoint off connection grid" -- 344 of them on the
                     # coupon's first pass, purely from float placement.
STUB = 3.81          # pin connection point to label anchor (3 x GRID)
GUTTER_X = 12.0
GUTTER_Y = 10.0
TEXT_LINE = 2.54     # one line of reference/value text above the symbol
MARGIN = 20.0
TITLE_BLOCK_H = 40.0 # content has to end above KiCad's title block
PAPER = {"A2": (594.0, 420.0), "A3": (420.0, 297.0)}
FONT = 1.27          # every text; P3 spec §2 -- never smaller (the coupon used 1.0)
CHAR_W = 0.75 * FONT # KiCad stroke font advance, near enough
LABEL_PAD = 3.0      # a global label's arrow shape
POWER_LEN = 5.08     # a power symbol's graphic plus the gap to its value text


class Uuids:
    """Deterministic UUIDs: uuid5 over a key naming the drawn item.

    Random uuid4s made every coupon board build a 26 000-line diff (memory
    fireflow-pcb-generator-speedups); the schematic gets the fix on day one.
    A key used twice raises -- two items would otherwise share an id.
    `random = True` exists only for the check tool's stability sabotage.
    """
    NS = uuid.UUID("6f1c2a4e-3b7d-5e8f-9a0b-1c2d3e4f5a6b")
    random = False

    def __init__(self, project):
        self.project = project
        self.seen = set()

    def __call__(self, *key):
        k = "/".join(str(x) for x in (self.project,) + key)
        if k in self.seen:
            raise ValueError("uuid key used twice: " + k)
        self.seen.add(k)
        if Uuids.random:
            return str(uuid.uuid4())
        return str(uuid.uuid5(self.NS, k))


def snap(v):
    return round(round(v / GRID) * GRID, 4)


def _n(v):
    """A coordinate as KiCad writes it: no float noise, no trailing zeros."""
    s = "%.4f" % v
    return s.rstrip("0").rstrip(".") if "." in s else s


def _esc(s):
    return str(s).replace("\\", "\\\\").replace('"', '\\"')


# --- units ---------------------------------------------------------------------

def units_of(part):
    """The units to draw: every unit that owns a pin.

    Unit 0 pins (common to all units) are refused on multi-unit symbols --
    nothing on these boards needs them, and drawing them once per unit would
    duplicate nodes.
    """
    units = sorted({p["unit"] for p in part.sym.pins.values()})
    if units == [0]:
        return [1]
    if 0 in units:
        raise ValueError("%s: %s has pins common to all units; not supported"
                         % (part.ref, part.lib_id))
    return units


def unit_pins(part, unit):
    return [n for n, p in part.sym.pins.items() if p["unit"] in (unit, 0)]


def cells(parts):
    return [(p, u) for p in parts for u in units_of(p)]


def _cell_name(part, unit):
    return part.ref if len(units_of(part)) == 1 else "%s:%d" % (part.ref, unit)


# --- geometry ------------------------------------------------------------------

def sym_extent(part, unit):
    pins = [part.sym.pins[n] for n in unit_pins(part, unit)]
    xs = [p["x"] for p in pins]
    ys = [p["y"] for p in pins]
    return (min(xs), max(xs), min(ys), max(ys))


def pin_point(part_x, part_y, pin):
    """Pin connection point in schematic coordinates (+Y down)."""
    return part_x + pin["x"], part_y - pin["y"]


def stub_end(cx, cy, angle):
    """Where the label sits: outward from the body, i.e. along the pin.

    Returns (x, y, rot); rot is also the outward direction in screen degrees
    (0 east, 90 north, 180 west, 270 south).
    """
    if angle == 0:            # pin body lies to the east; free end faces west
        return cx - STUB, cy, 180
    if angle == 180:
        return cx + STUB, cy, 0
    if angle == 90:           # symbol-space north -> schematic north is -Y
        return cx, cy + STUB, 270
    if angle == 270:
        return cx, cy - STUB, 90
    raise ValueError("unexpected pin angle %r" % angle)


def _along(x, y, rot, d):
    return {0: (x + d, y), 180: (x - d, y), 90: (x, y - d), 270: (x, y + d)}[rot]


def label_width(net):
    return len(net) * CHAR_W + LABEL_PAD


def ending_box(ex, ey, rot, net, power):
    """The rectangle a label or power symbol draws beyond its stub end."""
    h = FONT * 1.6
    if net in power:
        tw = len(net) * CHAR_W
        if rot in (0, 180):              # symbol, then the value text, in line
            length, thick = POWER_LEN + tw, max(h, 2.6)
        else:                            # value text centred past the tip
            length, thick = POWER_LEN + h, max(tw, 2.6)
    else:
        length, thick = label_width(net), h
    if rot == 180:
        return (ex - length, ey - thick / 2, ex, ey + thick / 2)
    if rot == 0:
        return (ex, ey - thick / 2, ex + length, ey + thick / 2)
    if rot == 90:
        return (ex - thick / 2, ey - length, ex + thick / 2, ey)
    return (ex - thick / 2, ey, ex + thick / 2, ey + length)


def label_groups(part, unit, px=0.0, py=0.0):
    """One stub and one ending per distinct connection point of `unit`.

    Electrosmith's Daisy_Patch_SM stacks A4 and A7 -- both GND -- on exactly
    the same coordinate. One stub per pin would double-strike the text. Pins
    that share a point but carry DIFFERENT nets are a short, so that raises.
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
    return [(pin_point(px, py, dict(x=k[0], y=k[1])), k[2], net, nums)
            for k, (net, nums) in sorted(groups.items())]


def label_margins(part, unit, power):
    """How far stubs and endings reach beyond the symbol, on all four sides.

    Measured from the same ending_box() the collision check uses, so the
    packing and the check cannot disagree about a label's size. (The coupon
    once charged vertical labels to the width instead of the height, and
    two-pin parts in adjacent rows overlapped while the arithmetic said there
    was room.)
    """
    x0, x1, y0, y1 = sym_extent(part, unit)
    left = right = top = bottom = 0.0
    for (cx, cy), angle, net, _ in label_groups(part, unit):
        ex, ey, rot = stub_end(cx, cy, angle)
        bx0, by0, bx1, by1 = ending_box(ex, ey, rot, net, power)
        left = max(left, x0 - bx0)
        right = max(right, bx1 - x1)
        top = max(top, -y1 - by0)
        bottom = max(bottom, by1 + y0)
    return left, right, top, bottom


def layout(parts, paper, power):
    """Pack every (part, unit) into rows; return ({(ref, unit): (x, y)}, height)."""
    sheet_w, _ = PAPER[paper]
    placed = {}
    x, y, row_h = MARGIN, MARGIN, 0.0
    for p, u in cells(parts):
        x0, x1, y0, y1 = sym_extent(p, u)
        lw, rw, tw, bw = label_margins(p, u, power)
        w = (x1 - x0) + lw + rw + GUTTER_X
        # the two text lines sit above everything the part draws, labels included
        h = (y1 - y0) + tw + bw + GUTTER_Y + 2 * TEXT_LINE
        if w > sheet_w - 2 * MARGIN:
            raise ValueError("%s is %.0f mm wide; %s allows %.0f"
                             % (_cell_name(p, u), w, paper, sheet_w - 2 * MARGIN))
        if x + w > sheet_w - MARGIN:
            x = MARGIN
            y += row_h
            row_h = 0.0
        placed[(p.ref, u)] = (snap(x + lw - x0), snap(y + 2 * TEXT_LINE + tw + y1))
        x += w
        row_h = max(row_h, h)
    return placed, y + row_h + MARGIN


def fits(height, paper):
    return height <= PAPER[paper][1] - TITLE_BLOCK_H


def text_anchor(part, unit, px, py, power):
    """Reference and value go above everything the part draws.

    Not a fixed offset from the origin -- a tall symbol carries its origin in
    the middle, which once printed "U_MUX16" inside the CD74HC4067M's body --
    and not just above the symbol either, because a two-pin part's top label
    runs upwards through exactly that space.
    """
    _, _, _, y1 = sym_extent(part, unit)
    _, _, tw, _ = label_margins(part, unit, power)
    top = py - y1 - tw
    return snap(top - 2 * TEXT_LINE), snap(top - TEXT_LINE)


# --- collision checking --------------------------------------------------------
# Added after the coupon's first PDF: labels from one symbol ran through the
# labels of the next, and reference text sat inside tall symbol bodies. Both
# are obvious in the drawing and invisible in the netlist.

def _boxes(parts, placed, power):
    out = []
    for p, u in cells(parts):
        px, py = placed[(p.ref, u)]
        name = _cell_name(p, u)
        x0, x1, y0, y1 = sym_extent(p, u)
        out.append((px + x0, py - y1, px + x1, py - y0, "%s body" % name))
        if not N.is_virtual(p.ref):           # flags draw no ref/value text
            ref_y, val_y = text_anchor(p, u, px, py, power)
            for text, ty in ((p.ref, ref_y), (p.value, val_y)):
                w = len(str(text)) * CHAR_W
                out.append((px, ty - 0.9, px + w, ty + 0.9,
                            "%s text %r" % (name, text)))
        for (cx, cy), angle, net, nums in label_groups(p, u, px, py):
            ex, ey, rot = stub_end(cx, cy, angle)
            out.append(ending_box(ex, ey, rot, net, power)
                       + ("%s.%s %s" % (p.ref, "/".join(nums), net),))
    return out


def overlaps(parts, placed, power, tolerance=0.2):
    """Pairs of drawn boxes that intersect by more than `tolerance` mm."""
    boxes = sorted(_boxes(parts, placed, power), key=lambda b: b[0])
    hits = []
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            if b[0] >= a[2] - tolerance:
                break                     # sorted by x0: nothing further can hit
            if (a[0] < b[2] - tolerance and b[0] < a[2] - tolerance
                    and a[1] < b[3] - tolerance and b[1] < a[3] - tolerance):
                hits.append((a[4], b[4]))
    return hits


# --- emission ------------------------------------------------------------------

def _points_up(sym):
    """True if a power symbol draws above its pin (+3V3), False below (GND)."""
    ys = []

    def walk(node):
        for c in node:
            if isinstance(c, list) and c and isinstance(c[0], ksexp.Atom):
                if c[0] == "xy":
                    ys.append(float(c[2]))
                else:
                    walk(c)
    walk((sym.base if sym.extends else sym).node)
    if not ys:
        raise ValueError("%s draws nothing" % sym.name)
    return max(ys) >= -min(ys)


def _prop(name, value, x, y, hide=False, justify="left"):
    just = " (justify %s)" % justify if justify else ""
    return ('\t\t(property "%s" "%s"\n\t\t\t(at %s %s 0)\n'
            '\t\t\t(effects (font (size %s %s))%s%s))\n'
            % (name, _esc(value), _n(x), _n(y), FONT, FONT, just,
               " (hide yes)" if hide else ""))


def _symbol(lib_id, ref, unit, x, y, rot, in_bom, sym_uuid, props, pins,
            project, path):
    yn = "yes" if in_bom else "no"
    return ('\t(symbol\n\t\t(lib_id "%s")\n\t\t(at %s %s %d)\n\t\t(unit %d)\n'
            '\t\t(exclude_from_sim no)\n\t\t(in_bom %s)\n\t\t(on_board %s)\n'
            '\t\t(dnp no)\n\t\t(uuid "%s")\n%s%s'
            '\t\t(instances\n\t\t\t(project "%s"\n'
            '\t\t\t\t(path "%s" (reference "%s") (unit %d))))\n\t)\n'
            % (lib_id, _n(x), _n(y), rot, unit, yn, yn, sym_uuid,
               "".join(props),
               "".join('\t\t(pin "%s" (uuid "%s"))\n' % pu for pu in pins),
               project, path, ref, unit))


def _wire(x0, y0, x1, y1, u):
    return ('\t(wire (pts (xy %s %s) (xy %s %s))\n'
            '\t\t(stroke (width 0) (type default)) (uuid "%s"))\n'
            % (_n(x0), _n(y0), _n(x1), _n(y1), u))


def _global_label(net, x, y, rot, u):
    return ('\t(global_label "%s"\n\t\t(shape bidirectional)\n'
            '\t\t(at %s %s %d)\n\t\t(fields_autoplaced yes)\n'
            '\t\t(effects (font (size %s %s)) (justify %s))\n\t\t(uuid "%s")\n'
            '\t\t(property "Intersheetrefs" "${INTERSHEET_REFS}"\n'
            '\t\t\t(at %s %s 0)\n\t\t\t(effects (font (size %s %s)) (hide yes))))\n'
            % (_esc(net), _n(x), _n(y), rot, FONT, FONT,
               "right" if rot == 180 else "left", u, _n(x), _n(y), FONT, FONT))


def _local_label(net, x, y, rot, u):
    just = "right bottom" if rot in (180, 270) else "left bottom"
    return ('\t(label "%s"\n\t\t(at %s %s %d)\n'
            '\t\t(effects (font (size %s %s)) (justify %s))\n\t\t(uuid "%s"))\n'
            % (_esc(net), _n(x), _n(y), rot, FONT, FONT, just, u))


def emit_items(parts, placed, uuids, project, path, kind, power, pwr_counter):
    """Symbols, stubs, labels, power symbols and no-connects for one sheet."""
    out = []
    for p, u in cells(parts):
        px, py = placed[(p.ref, u)]
        virtual = N.is_virtual(p.ref)
        ref_y, val_y = text_anchor(p, u, px, py, power)
        props = [_prop("Reference", p.ref, px, ref_y, hide=virtual),
                 _prop("Value", p.value, px, val_y, hide=virtual),
                 _prop("Footprint", p.footprint, px, py, hide=True),
                 _prop("Datasheet", "", px, py, hide=True),
                 _prop("Description", p.note, px, py, hide=True)]
        for name, val in (("LCSC", p.lcsc), ("Source", p.source),
                          ("PanelId", p.panel_id)):
            if val:
                props.append(_prop(name, val, px, py, hide=True))
        pins = [(n, uuids("pin", p.ref, n))
                for n in sorted(unit_pins(p, u), key=ksexp._pin_sort_key)]
        out.append(_symbol(p.lib_id, p.ref, u, px, py, 0, not virtual,
                           uuids("sym", p.ref, u), props, pins, project, path))
        for (cx, cy), angle, net, nums in label_groups(p, u, px, py):
            ex, ey, rot = stub_end(cx, cy, angle)
            key = (p.ref, "/".join(nums))
            out.append(_wire(cx, cy, ex, ey, uuids("stub", *key)))
            k = kind(net)
            if k == "power":
                lib_id = power[net]
                psym = N.load(lib_id)
                (pnum,) = psym.pins      # a power symbol has exactly one pin
                srot = (rot - (90 if _points_up(psym) else 270)) % 360
                tx, ty = _along(ex, ey, rot, POWER_LEN)
                ref = "#PWR%04d" % next(pwr_counter)
                pprops = [_prop("Reference", ref, ex, ey, hide=True),
                          _prop("Value", net, tx, ty,
                                justify={0: "left", 180: "right"}.get(rot)),
                          _prop("Footprint", "", ex, ey, hide=True),
                          _prop("Datasheet", "", ex, ey, hide=True)]
                out.append(_symbol(lib_id, ref, 1, ex, ey, srot, False,
                                   uuids("pwr", *key), pprops,
                                   [(pnum, uuids("pwrpin", *key))], project, path))
            elif k == "global":
                out.append(_global_label(net, ex, ey, rot, uuids("label", *key)))
            else:
                out.append(_local_label(net, ex, ey, rot, uuids("label", *key)))
        for number in sorted(unit_pins(p, u), key=ksexp._pin_sort_key):
            if number not in p.nets:
                cx, cy = pin_point(px, py, p.sym.pin(number))
                out.append('\t(no_connect (at %s %s) (uuid "%s"))\n'
                           % (_n(cx), _n(cy), uuids("nc", p.ref, number)))
    return out


def lib_symbols(lib_ids):
    chunks = []
    for lib_id in sorted(lib_ids):
        sym = N.load(lib_id)
        node = sym.base.node if sym.extends else sym.node
        body = ksexp.dump(node, 2)
        # Naming, and it is asymmetric in a way that costs an afternoon if
        # guessed: the HEAD carries the library prefix ("74xx:74HC595"), the
        # unit sub-symbols must NOT ("74HC595_0_1"). Putting the prefix on the
        # units makes KiCad refuse the whole file with nothing but "could not
        # load schematic". For a derived symbol the units also move to the
        # DERIVED name: 74xx:74HC165 (extends "74LS165") is drawn by units
        # called 74LS165_1_1, which have to become 74HC165_1_1.
        base = sym.extends or sym.name
        body = body.replace('(symbol "%s_' % base, '(symbol "%s_' % sym.name)
        body = body.replace('(symbol "%s"' % base, '(symbol "%s"' % lib_id, 1)
        chunks.append("\t\t" + body)
    return "\n".join(chunks)


def _document(file_uuid, paper, title, comments, lib_ids, body, root):
    tb = '\t\t(title "%s")\n\t\t(rev "draft")\n' % _esc(title)
    tb += "".join('\t\t(comment %d "%s")\n' % (i + 1, _esc(c))
                  for i, c in enumerate(comments))
    tail = '\t(sheet_instances\n\t\t(path "/" (page "1"))\n\t)\n' if root else ""
    return ('(kicad_sch\n\t(version 20250114)\n\t(generator "fireflow-gen")\n'
            '\t(generator_version "10.0")\n\t(uuid "%s")\n\t(paper "%s")\n'
            '\t(title_block\n%s\t)\n\t(lib_symbols\n%s\n\t)\n%s%s'
            '\t(embedded_fonts no)\n)\n'
            % (file_uuid, paper, tb, lib_symbols(lib_ids), "".join(body), tail))


def _sheet_box(sheet, index, sheet_uuid, text_uuid, project, root_uuid):
    col, row = index % 4, index // 4
    x, y = snap(MARGIN + col * 95.0), snap(MARGIN + 30.0 + row * 45.0)
    w, h = 76.2, 25.4
    return ('\t(sheet\n\t\t(at %s %s)\n\t\t(size %s %s)\n\t\t(exclude_from_sim no)\n'
            '\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(dnp no)\n'
            '\t\t(stroke (width 0) (type solid))\n\t\t(fill (color 0 0 0 0.0000))\n'
            '\t\t(uuid "%s")\n'
            '\t\t(property "Sheetname" "%s"\n\t\t\t(at %s %s 0)\n'
            '\t\t\t(effects (font (size %s %s)) (justify left bottom)))\n'
            '\t\t(property "Sheetfile" "%s.kicad_sch"\n\t\t\t(at %s %s 0)\n'
            '\t\t\t(effects (font (size %s %s)) (justify left top)))\n'
            '\t\t(instances\n\t\t\t(project "%s"\n'
            '\t\t\t\t(path "/%s" (page "%d")))))\n'
            '\t(text "%s"\n\t\t(exclude_from_sim no)\n\t\t(at %s %s 0)\n'
            '\t\t(effects (font (size %s %s)) (justify left))\n\t\t(uuid "%s"))\n'
            % (_n(x), _n(y), _n(w), _n(h), sheet_uuid,
               sheet.name, _n(x), _n(y - 0.75), FONT, FONT,
               sheet.name, _n(x), _n(y + h + 0.6), FONT, FONT,
               project, root_uuid, index + 2,
               _esc(sheet.title), _n(x + 2.54), _n(y + h / 2), FONT, FONT, text_uuid))


def _write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def write_project(project, out_dir):
    """Write <project>.kicad_sch (and one file per sheet unless flat).

    Returns {sheet name: (placed, height)} for the checks.
    """
    os.makedirs(out_dir, exist_ok=True)
    uuids = Uuids(project.name)
    sheet_of = project.sheet_of()
    net_sheets = {}
    for p in project.parts():
        for net in p.nets.values():
            net_sheets.setdefault(net, set()).add(sheet_of[p.ref])

    def kind(net):
        if net in project.power:
            return "power"
        if project.flat or len(net_sheets[net]) > 1:
            return "global"
        return "local"

    pwr = itertools.count(1)
    root_uuid = uuids("root")
    result = {}

    def body_for(sheet, path):
        placed, height = layout(sheet.parts, project.paper, project.power)
        result[sheet.name] = (placed, height)
        body = emit_items(sheet.parts, placed, uuids, project.name, path, kind,
                          project.power, pwr)
        lib_ids = {p.lib_id for p in sheet.parts}
        lib_ids |= {project.power[n] for p in sheet.parts
                    for n in p.nets.values() if n in project.power}
        return body, lib_ids

    if project.flat:
        sheet = project.sheets[0]
        body, lib_ids = body_for(sheet, "/" + root_uuid)
        _write(os.path.join(out_dir, project.name + ".kicad_sch"),
               _document(root_uuid, project.paper, project.title,
                         project.comments, lib_ids, body, root=True))
        return result

    boxes = []
    for i, sheet in enumerate(project.sheets):
        sheet_uuid = uuids("sheet", sheet.name)
        boxes.append(_sheet_box(sheet, i, sheet_uuid, uuids("sheet-text", sheet.name),
                                project.name, root_uuid))
        body, lib_ids = body_for(sheet, "/%s/%s" % (root_uuid, sheet_uuid))
        _write(os.path.join(out_dir, sheet.name + ".kicad_sch"),
               _document(uuids("file", sheet.name), project.paper,
                         "%s: %s" % (project.title, sheet.title),
                         project.comments, lib_ids, body, root=False))
    _write(os.path.join(out_dir, project.name + ".kicad_sch"),
           _document(root_uuid, project.paper, project.title, project.comments,
                     set(), boxes, root=True))
    return result
```

- [ ] **Step 4: Append the export helpers to `hardware/gen/check.py`**

Add `from gen import netlist as N` below the `ksexp` import, then append:
```python
def normalize(nets):
    """Sheet-local nets export as /<sheet>/<name>; the intent knows only <name>."""
    out = {}
    for name, nodes in nets.items():
        short = name.rsplit("/", 1)[-1] if name.startswith("/") else name
        if short in out:
            raise ValueError("two exported nets shorten to %r" % short)
        out[short] = nodes
    return out


def export_netlist(project, sch_dir):
    """kicad-cli's netlist of the written project, names normalized."""
    sch = os.path.join(sch_dir, project.name + ".kicad_sch")
    net = os.path.join(sch_dir, project.name + ".net")
    rc, out = run([ksexp.KICAD_CLI, "sch", "export", "netlist", "-o", net, sch],
                  "kicad-cli sch export netlist", quiet=True)
    if rc != 0:
        raise RuntimeError("netlist export failed (rc=%d):\n%s" % (rc, out))
    return normalize(parse_exported_netlist(net))


def intended_nets(project):
    return {k: set(v) for k, v in
            N.nets_from(project.parts(), include_virtual=False).items()}
```

- [ ] **Step 5: Create the demo fixture**

Create `hardware/gen/fixtures/__init__.py` containing only `"""Fixtures for the hardware/gen tests."""`, and `hardware/gen/fixtures/demo.py`:
```python
#!/usr/bin/env python3
"""A three-sheet fixture for the shared writer and the check tool.

Not a circuit anyone builds. Every writer and check feature has something
KiCad must read back here: two rails drawn with one +3V3 power symbol (A3V3
and 3V3D), a sheet-local net, nets crossing sheets, a multi-unit op-amp, a
strict part with explicit no-connects, panel-mounted parts with PanelIds, and
analog/digital rail domains. LCSC values are placeholders ("C-FIXTURE").
"""
import os
import sys

_HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
if _HW not in sys.path:
    sys.path.insert(0, _HW)

from gen.netlist import Part            # noqa: E402
from gen.project import Project, Sheet  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
POWER = {"GND": "power:GND", "+12V": "power:+12V", "-12V": "power:-12V",
         "A3V3": "power:+3V3", "3V3D": "power:+3V3"}
DOMAINS = {"analog": {"A3V3"}, "digital": {"3V3D"}}
HOLES = [{"id": "DEMO_LED", "kind": "led"}, {"id": "DEMO_POT", "kind": "pot"}]
FX = "C-FIXTURE"
FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_C = "Capacitor_SMD:C_0603_1608Metric"
FP_HDR3 = "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"
FP_HDR2 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"


def _power_sheet():
    parts = []
    j1 = Part("J1", "Connector_Generic:Conn_01x03", "power in", FP_HDR3, source="fixture")
    j1.by_number(1, "+12V").by_number(2, "GND").by_number(3, "-12V")
    j2 = Part("J2", "Connector_Generic:Conn_01x02", "rails in", FP_HDR2, source="fixture")
    j2.by_number(1, "A3V3").by_number(2, "3V3D")
    j3 = Part("J3", "Connector_Generic:Conn_01x02", "data in", FP_HDR2, source="fixture")
    j3.by_number(1, "DEMO_DATA").by_number(2, "DEMO_CLK")
    parts += [j1, j2, j3]
    for ref, rail in (("C1", "3V3D"), ("C2", "A3V3")):
        c = Part(ref, "Device:C", "100n", FP_C, lcsc=FX)
        parts.append(c.by_number(1, rail).by_number(2, "GND"))
    for i, net in enumerate(("+12V", "-12V", "GND", "A3V3", "3V3D")):
        parts.append(Part("#FLG%04d" % (i + 1), "power:PWR_FLAG", "PWR_FLAG", "")
                     .by_number(1, net))
    return Sheet("power", "Power and inputs", parts)


def _logic_sheet():
    u1 = Part("U1", "74xx:74HC595", "74HC595", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm",
              lcsc=FX, domain="digital", strict=True)
    u1.by_name("VCC", "3V3D").by_name("GND", "GND").by_name("~{SRCLR}", "3V3D")
    u1.by_name("~{OE}", "GND").by_name("SER", "DEMO_DATA")
    u1.by_name("SRCLK", "DEMO_CLK").by_name("RCLK", "DEMO_CLK").by_name("QA", "LED_A")
    u1.no_connect(*[u1.sym.by_name(n) for n in
                    ("QB", "QC", "QD", "QE", "QF", "QG", "QH", "QH'")])
    u2 = Part("U2", "Amplifier_Operational:TL072", "TL072",
              "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", lcsc=FX, domain="analog")
    u2.by_number(3, "POT_W").by_number(2, "BUF_OUT").by_number(1, "BUF_OUT")
    u2.by_number(5, "GND").by_number(6, "U2B_FB").by_number(7, "U2B_FB")
    u2.by_number(8, "+12V").by_number(4, "-12V")
    rv1 = Part("RV1", "Device:R_Potentiometer", "10k",
               "Potentiometer_THT:Potentiometer_Alpha_RD901F-40-00D_Single_Vertical",
               source="fixture", panel_id="DEMO_POT", domain="analog")
    rv1.by_number(1, "GND").by_number(2, "POT_W").by_number(3, "A3V3")
    c3 = Part("C3", "Device:C", "100n", FP_C, lcsc=FX, domain="analog")
    c3.by_number(1, "A3V3").by_number(2, "GND")
    return Sheet("logic", "Shift register and buffer", [u1, u2, rv1, c3])


def _led_sheet():
    d1 = Part("D1", "Device:LED", "red", "LED_THT:LED_D3.0mm",
              source="fixture", panel_id="DEMO_LED")
    d1.by_name("A", "LED_A").by_name("K", "LED_A_K")
    r1 = Part("R1", "Device:R", "1k", FP_R, lcsc=FX)
    r1.by_number(1, "LED_A_K").by_number(2, "GND")
    return Sheet("leds", "Panel LED", [d1, r1])


def project():
    return Project("demo", "hardware/gen demo fixture",
                   [_power_sheet(), _logic_sheet(), _led_sheet()],
                   power=POWER, domain_rails=DOMAINS, holes=HOLES,
                   waivers=os.path.join(HERE, "demo-erc-waivers.txt"),
                   paper="A3",
                   comments=["GENERATED by hardware/gen/sch_writer.py -- fixture"])
```

- [ ] **Step 6: Make the coupon a thin caller**

Replace everything in `hardware/coupon/scripts/generate_schematic.py` below the module docstring with:
```python
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..")))
import netlist as N                           # noqa: E402
from gen import sch_writer as W               # noqa: E402  (P3 spec §3.1)
from gen.project import Project, Sheet        # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.normpath(os.path.join(HERE, ".."))
SCH_PATH = os.path.join(OUT_DIR, "coupon.kicad_sch")
PAPER = "A2"


def project():
    return Project("coupon", "FireFlow test coupon",
                   [Sheet("coupon", "all", N.build())], paper=PAPER, flat=True,
                   comments=["GENERATED by hardware/gen/sch_writer.py via "
                             "hardware/coupon/scripts/generate_schematic.py "
                             "-- do not edit by hand",
                             "Source of truth: scripts/design.py and scripts/netlist.py"])


def layout(parts):
    return W.layout(parts, PAPER, {})


def overlaps(parts, placed):
    return W.overlaps(parts, placed, {})


def main(out_dir=OUT_DIR):
    layouts = W.write_project(project(), out_dir)
    _placed, needed_h = layouts["coupon"]
    if not W.fits(needed_h, PAPER):
        print("note: content is %.0f mm tall -- widen or split" % needed_h)
    print("wrote %s (%.0f mm of content)"
          % (os.path.relpath(os.path.join(out_dir, "coupon.kicad_sch")), needed_h))


if __name__ == "__main__":
    main()
```
Keep the module docstring's first line; replace its body with: `Emit coupon.kicad_sch from netlist.build() through the shared writer (hardware/gen/sch_writer.py), which carries the style notes. Running this regenerates the committed schematic -- do so only on purpose.` `build.py` keeps calling `G.main()`, `G.layout(parts)` and `G.overlaps(parts, placed)`; no other change there.

- [ ] **Step 7: Write the failing writer test**

Create `hardware/gen/test_sch_writer.py`:
```python
#!/usr/bin/env python3
"""The shared schematic writer, proven by KiCad's own netlist export.

1. The coupon, written flat to a temp dir, exports exactly its intent.
2. The demo fixture -- three sheets, power symbols, a local net, a multi-unit
   op-amp -- exports exactly its intent.
3. Writing either project twice gives byte-identical files.
Needs kicad-cli (gen.ksexp.KICAD_CLI); without it this fails, never skips.

    python hardware/gen/test_sch_writer.py
"""
import filecmp
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HW)
sys.path.insert(0, os.path.join(HW, "coupon", "scripts"))

from gen import check, ksexp, sch_writer as W   # noqa: E402
from gen.fixtures import demo                   # noqa: E402
import generate_schematic as coupon_sch         # noqa: E402


def check_project(make, failures):
    proj = make()
    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        W.write_project(proj, a)
        W.write_project(make(), b)
        names = sorted(os.listdir(a))
        if names != sorted(os.listdir(b)):
            failures.append("%s: file sets differ between runs" % proj.name)
        _, mismatch, errors = filecmp.cmpfiles(a, b, names, shallow=False)
        for n in mismatch + errors:
            failures.append("%s: %s differs between two writes" % (proj.name, n))
        exported = check.export_netlist(proj, a)
        intended = check.intended_nets(proj)
        for bad in check.compare(intended, exported):
            failures.append("%s: %s" % (proj.name, bad))
        victim = sorted(intended)[0]
        planted = dict(intended)
        planted[victim] = set(intended[victim]) | {("X_PLANTED", "1")}
        if not check.compare(planted, exported):
            failures.append("%s: compare() missed a planted node" % proj.name)
        print("  %s: %d files, %d intended nets" % (proj.name, len(names), len(intended)))


def main():
    if not os.path.exists(ksexp.KICAD_CLI):
        print("FAIL kicad-cli not found at %s" % ksexp.KICAD_CLI)
        return 1
    failures = []
    for make in (coupon_sch.project, demo.project):
        check_project(make, failures)
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: coupon and demo export their intent; writes are byte-stable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 8: Run it and fix until green**

Run: `python hardware/gen/test_sch_writer.py`
Expected once the steps above are in: `coupon: 1 files, 66 intended nets`, `demo: 4 files, N intended nets`, `ok: ...`. If KiCad refuses a file ("could not load schematic") or nodes are missing, compare the failing construct with `C:/Users/bernd/AppData/Local/Programs/KiCad/10.0/share/kicad/demos/complex_hierarchy/` (a working two-level hierarchy) and fix the writer, not the test.

- [ ] **Step 9: Look at the drawings**

Run:
```bash
"/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/kicad-cli.exe" sch export pdf -o "C:/Users/bernd/AppData/Local/Temp/p3-demo.pdf" "<a temp dir holding a written demo>/demo.kicad_sch"
```
(write the demo with `python -c "import sys; sys.path.insert(0,'hardware'); from gen import sch_writer as W; from gen.fixtures import demo; W.write_project(demo.project(), r'C:/Users/bernd/AppData/Local/Temp/p3-demo')"` first), then render pages with `python -c "import fitz; d=fitz.open(r'C:/Users/bernd/AppData/Local/Temp/p3-demo.pdf'); [d[i].get_pixmap(dpi=110).save(r'C:/Users/bernd/AppData/Local/Temp/p3-demo-%d.png' % i) for i in range(d.page_count)]"` and open every PNG with the Read tool. Check: the overview shows three sheet boxes with titles; every power symbol's graphic points **away** from its part along the stub (if +3V3 arrows point into the parts, the rotation sign in `emit_items` is inverted — flip `srot` to `(d0 - rot) % 360` form and re-look); labels read left-to-right; text is not smaller than the pin numbers. Name each PNG and what you saw in the report.

- [ ] **Step 10: Show the test RED once**

With the Edit tool, change `return cx - STUB, cy, 180` in `stub_end` to `return cx - STUB, cy + GRID, 180`. Run the test; expected: exit 1 with `missing` lines for both projects. Revert with the Edit tool; rerun; expected `ok`.

- [ ] **Step 11: Wire into ctest and confirm the coupon is untouched**

Append to the ctest block from Task 1:
```cmake
add_test(NAME hw_gen_writer_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_sch_writer.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```
Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `ctest --test-dir build -R hw_gen --output-on-failure` — expected 2/2 pass. Run `python hardware/gen/test_coupon_netlist.py` — expected `ok`. Run `git status --short hardware/coupon` — expected: only `scripts/generate_schematic.py` modified.

- [ ] **Step 12: Commit**

```bash
git add hardware/gen hardware/coupon/scripts/generate_schematic.py CMakeLists.txt
```
```bash
git commit -m "hw(gen): shared schematic writer -- sheets, power symbols, units, stable UUIDs

Moved from the coupon and extended for Rev A (P3 spec §2-§3): an overview
with one sheet per block, power symbols named by Value, sheet-local labels,
every unit of a multi-unit symbol, 1.27 mm text, uuid5 ids. The coupon and a
three-sheet demo fixture export exactly their intent; two writes are
byte-identical.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The check tool — three levels, sabotage modes, ERC waivers

**Files:**
- Modify: `hardware/gen/check.py` (append rules, levels, CLI)
- Create: `hardware/gen/fixtures/demo-erc-waivers.txt`
- Create: `hardware/gen/fixtures/load.py`
- Create: `hardware/gen/test_check.py`
- Modify: `.gitignore`, `CMakeLists.txt`
- Modify: `docs/superpowers/specs/2026-09-29-rev-a-p3-schematic-design.md` (§4, the measured full-level time)

**Interfaces:**
- Consumes: Task 2's `write_project`, `overlaps`, `fits`, `PAPER`, `TITLE_BLOCK_H`, `Uuids.random`; `export_netlist`, `intended_nets`, `compare`, `write_lib_tables`, `kicad_defines`; the demo's `project()`.
- Produces: `gen.check.Finding(rule, sheet, text)` printing as `FAIL <sheet> [<rule>]: <text>`; `RULES` (dict name → `rule(project) -> (examined, [Finding])`), `fast(project)`, `apply_sabotage(project, spec)`, `sheet_level(project, name, out_dir, sabotage=None)`, `full_level(project, factory, out_dir, sabotage=None)`, `load_waivers(path)`, `match_waivers(violations, waivers)`, `main(argv=None) -> int`. CLI: `python hardware/gen/check.py --project FILE.py (--fast | --sheet NAME | --full) [--sabotage SPEC] [--out DIR]`; the project file defines `project()`. Output ends with `PASS <level>: ...` or `FAIL <level>: N finding(s) ...`; exit code 0/1.

Ruling recorded here: the spec's `--sheet NAME` "writes one sheet"; this plan writes all sheets and checks one, because KiCad cannot export a sub-sheet's netlist on its own and writing all costs milliseconds.

- [ ] **Step 1: Write the failing test**

Create `hardware/gen/test_check.py`:
```python
#!/usr/bin/env python3
"""The check tool can fail, and passes a clean fixture.

For every fast rule: the demo fixture passes it, its sabotage makes it fire,
and its ':empty' sabotage trips the examined-nothing guard. The sheet and
full levels then run on the demo through kicad-cli: clean passes, and the
'overlap', 'erc' and 'stability' sabotages each fail. Waiver bookkeeping is
checked directly. Needs kicad-cli; without it this fails, never skips.

    python hardware/gen/test_check.py
"""
import contextlib
import io
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..")))

from gen import check, ksexp  # noqa: E402

DEMO = os.path.join(HERE, "fixtures", "demo.py")


def run_cli(args, out_dir):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = check.main(["--project", DEMO, "--out", out_dir] + args)
    return rc, buf.getvalue()


def main():
    if not os.path.exists(ksexp.KICAD_CLI):
        print("FAIL kicad-cli not found at %s" % ksexp.KICAD_CLI)
        return 1
    failures = []

    def expect(args, rc_want, needle, out_dir):
        rc, text = run_cli(args, out_dir)
        ok = rc == rc_want and needle in text
        print("  %-44s rc=%d %s" % (" ".join(args), rc, "ok" if ok else "UNEXPECTED"))
        if not ok:
            failures.append("%s: rc=%d (wanted %d), %r %s in output:\n%s"
                            % (" ".join(args), rc, rc_want, needle,
                               "found" if needle in text else "missing", text))
        return text

    with tempfile.TemporaryDirectory() as out:
        expect(["--fast"], 0, "PASS fast", out)
        for rule in check.RULES:
            expect(["--fast", "--sabotage", rule], 1, "[%s]" % rule, out)
            expect(["--fast", "--sabotage", rule + ":empty"], 1,
                   "[%s]: examined nothing" % rule, out)
        expect(["--sheet", "logic"], 0, "PASS sheet logic", out)
        if not os.path.exists(os.path.join(out, "logic.png")):
            failures.append("the sheet level wrote no logic.png")
        expect(["--sheet", "logic", "--sabotage", "overlap"], 1, "[overlap]", out)
        expect(["--full"], 0, "PASS full", out)
        for name in ("demo-overview.png", "power.png", "logic.png", "leds.png",
                     "demo.pdf", "erc.json"):
            if not os.path.exists(os.path.join(out, name)):
                failures.append("the full level wrote no %s" % name)
        expect(["--full", "--sabotage", "erc"], 1, "[erc]", out)
        expect(["--full", "--sabotage", "stability"], 1, "[stability]", out)

    found = check.match_waivers(
        [("pin_to_pin", ["Symbol U1 Pin 9 [Q7, Output, Line]"])],
        [("pin_to_pin", "Symbol U1 Pin 9", "reason"),
         ("pin_not_driven", "Symbol U9", "stale")])
    if [f.rule for f in found] != ["erc_waiver"]:
        failures.append("match_waivers: wanted one stale-waiver finding, got %s"
                        % [str(f) for f in found])
    found = check.match_waivers([("pin_to_pin", ["Symbol U2 Pin 1"])], [])
    if [f.rule for f in found] != ["erc"]:
        failures.append("match_waivers: an unwaived violation went unreported")

    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: every rule passes the demo, fires on its sabotage and guards "
          "against an empty input; sheet and full levels pass and fail as planned")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python hardware/gen/test_check.py`
Expected: `AttributeError: module 'gen.check' has no attribute 'RULES'`.

- [ ] **Step 3: Append the rules, levels and CLI to `hardware/gen/check.py`**

Add to the imports at the top: `import argparse`, `import filecmp`, `import importlib.util`, `import json`, `import shutil`, `import tempfile`, `import time`, and below the `gen` imports `from gen import sch_writer as W`. Then append:
```python
# --- the three check levels (P3 spec §4) ------------------------------------

VENDORED_DIR = os.path.join(_HW, "lib", "DaisyKiCad").replace("\\", "/")
OUTPUT_TYPES = {"output", "tri_state", "power_out"}
PANEL_KINDS = {"pot", "jack", "key", "led"}


class Finding:
    def __init__(self, rule, sheet, text):
        self.rule, self.sheet, self.text = rule, sheet, text

    def __str__(self):
        return "FAIL %s [%s]: %s" % (self.sheet, self.rule, self.text)


def _real_nets(project):
    """{net: [(part, pin)]} without power flags and power symbols."""
    nets = {}
    for p in project.parts():
        if N.is_virtual(p.ref):
            continue
        for pin, net in p.nets.items():
            nets.setdefault(net, []).append((p, pin))
    return nets


def rule_single_pin(project):
    sheet = project.sheet_of()
    nets = _real_nets(project)
    found = [Finding("single_pin", sheet[nodes[0][0].ref],
                     "net %s has one pin (%s.%s)" % (net, nodes[0][0].ref, nodes[0][1]))
             for net, nodes in sorted(nets.items()) if len(nodes) == 1]
    return len(nets), found


def rule_driver_conflict(project):
    sheet = project.sheet_of()
    examined, found = 0, []
    for net, nodes in sorted(_real_nets(project).items()):
        if net in project.power:
            continue
        drivers = [(p, pin) for p, pin in nodes
                   if p.sym.pin(pin)["etype"] in OUTPUT_TYPES]
        if not drivers:
            continue
        examined += 1
        if len(drivers) > 1:
            found.append(Finding("driver_conflict", sheet[drivers[0][0].ref],
                                 "net %s has %d outputs: %s" % (
                                     net, len(drivers),
                                     ", ".join("%s.%s" % (p.ref, pin) for p, pin in drivers))))
    return examined, found


def rule_rail_domain(project):
    """A3V3 feeds pots and muxes only, 3V3D the digital side (P2 decision 3)."""
    rail_domain = {r: d for d, rails in project.domain_rails.items() for r in rails}
    sheet = project.sheet_of()
    examined, found = 0, []
    for p in project.parts():
        if not p.domain:
            continue
        touched = sorted({n for n in p.nets.values() if n in rail_domain})
        if not touched:
            continue
        examined += 1
        if p.domain not in project.domain_rails:
            found.append(Finding("rail_domain", sheet[p.ref],
                                 "%s has unknown domain %r" % (p.ref, p.domain)))
            continue
        wrong = [n for n in touched if rail_domain[n] != p.domain]
        if wrong:
            found.append(Finding("rail_domain", sheet[p.ref], "%s is %s but sits on %s"
                                 % (p.ref, p.domain, ", ".join(wrong))))
    return examined, found


def rule_pins_accounted(project):
    sheet = project.sheet_of()
    strict = [p for p in project.parts() if p.strict]
    found = []
    for p in strict:
        loose = [n for n in p.unconnected() if n not in p.nc]
        if loose:
            found.append(Finding("pins_accounted", sheet[p.ref],
                                 "%s pins %s carry no net and are not marked no-connect"
                                 % (p.ref, ", ".join(loose))))
    return len(strict), found


def rule_panel_ids(project):
    holes = [h for h in (project.holes or []) if h["kind"] in PANEL_KINDS]
    sheet = project.sheet_of()
    by_id = {}
    for p in project.parts():
        if p.panel_id:
            by_id.setdefault(p.panel_id, []).append(p.ref)
    hole_ids = {h["id"] for h in holes}
    found = []
    for h in holes:
        refs = by_id.get(h["id"], [])
        if len(refs) != 1:
            found.append(Finding("panel_ids", "-", "hole %s (%s) has %d parts%s"
                                 % (h["id"], h["kind"], len(refs),
                                    (": " + ", ".join(refs)) if refs else "")))
    for pid, refs in sorted(by_id.items()):
        if pid not in hole_ids:
            found.append(Finding("panel_ids", sheet[refs[0]],
                                 "%s carries PanelId %s, which is no panel hole"
                                 % (", ".join(refs), pid)))
    return len(holes), found


def rule_sourced(project):
    """Every real part has exactly one of an LCSC number (JLC fits it) or a
    Source (bought elsewhere, hand-soldered) -- P3 spec §6."""
    sheet = project.sheet_of()
    parts = [p for p in project.parts() if not N.is_virtual(p.ref)]
    found = [Finding("sourced", sheet[p.ref],
                     "%s needs exactly one of LCSC or Source (LCSC=%r, Source=%r)"
                     % (p.ref, p.lcsc, p.source))
             for p in parts if bool(p.lcsc) == bool(p.source)]
    return len(parts), found


RULES = {"single_pin": rule_single_pin,
         "driver_conflict": rule_driver_conflict,
         "rail_domain": rule_rail_domain,
         "pins_accounted": rule_pins_accounted,
         "panel_ids": rule_panel_ids,
         "sourced": rule_sourced}


def fast(project):
    """Level 1: the intent alone, no KiCad, no file written."""
    findings = []
    for name, rule in RULES.items():
        examined, found = rule(project)
        if examined == 0:
            findings.append(Finding(name, "-", "examined nothing -- a rule with "
                                    "no input cannot pass"))
        findings += found
    return findings


# --- sabotage: every rule shows its RED on demand -----------------------------

def _first(items, what):
    if not items:
        raise ValueError("this sabotage needs %s and the project has none" % what)
    return items[0]


def _sab_add(project, part):
    project.sheets[0].parts.append(part)


def _sab_single_pin(pr):
    _sab_add(pr, N.Part("R_SAB1", "Device:R", "1k", "", lcsc="C-SABOTAGE")
             .by_number(1, "SAB_ALONE").by_number(2, "GND"))


def _sab_driver_conflict(pr):
    driven = [n for n, nodes in sorted(_real_nets(pr).items())
              if n not in pr.power
              and any(p.sym.pin(pin)["etype"] in OUTPUT_TYPES for p, pin in nodes)]
    _sab_add(pr, N.Part("U_SAB1", "74xx:74HC595", "74HC595", "", lcsc="C-SABOTAGE")
             .by_name("QA", _first(driven, "a driven net")))


def _sab_rail_domain(pr):
    domains = sorted(pr.domain_rails)
    if len(domains) < 2:
        raise ValueError("rail_domain sabotage needs two domains")
    rail = sorted(pr.domain_rails[domains[1]])[0]
    _sab_add(pr, N.Part("R_SAB1", "Device:R", "1k", "", lcsc="C-SABOTAGE",
                        domain=domains[0]).by_number(1, rail).by_number(2, "GND"))


def _sab_pins_accounted(pr):
    p = _first([p for p in pr.parts() if p.strict and p.nets], "a strict part")
    del p.nets[sorted(p.nets, key=ksexp._pin_sort_key)[0]]


def _sab_panel_ids(pr):
    _first([p for p in pr.parts() if p.panel_id], "a panel part").panel_id = ""


def _sab_sourced(pr):
    p = _first([p for p in pr.parts() if not N.is_virtual(p.ref)], "a real part")
    p.lcsc = p.source = ""


SABOTAGE = {"single_pin": _sab_single_pin,
            "driver_conflict": _sab_driver_conflict,
            "rail_domain": _sab_rail_domain,
            "pins_accounted": _sab_pins_accounted,
            "panel_ids": _sab_panel_ids,
            "sourced": _sab_sourced,
            "erc": _sab_single_pin}          # a one-pin label: ERC must see it too
LEVEL_SABOTAGE = {"overlap", "stability"}    # handled inside the levels


def _empty(pr, rule):
    if rule in ("single_pin", "driver_conflict", "sourced"):
        for s in pr.sheets:
            s.parts = []
    elif rule == "rail_domain":
        pr.domain_rails = {}
    elif rule == "pins_accounted":
        for p in pr.parts():
            p.strict = False
    elif rule == "panel_ids":
        pr.holes = []


def apply_sabotage(project, spec):
    rule, _, mode = spec.partition(":")
    if mode == "empty" and rule in RULES:
        _empty(project, rule)
    elif not mode and rule in SABOTAGE:
        SABOTAGE[rule](project)
    elif not mode and rule in LEVEL_SABOTAGE:
        pass
    else:
        raise ValueError("unknown sabotage %r; known: %s, or RULE:empty"
                         % (spec, ", ".join(sorted(set(SABOTAGE) | LEVEL_SABOTAGE))))


# --- levels 2 and 3 -------------------------------------------------------------

def _write_all(project, out_dir):
    sch_dir = os.path.join(out_dir, "sch")
    if os.path.isdir(sch_dir):
        shutil.rmtree(sch_dir)     # a renamed sheet's old file must not linger
    layouts = W.write_project(project, sch_dir)
    write_lib_tables(project.parts(), sch_dir, VENDORED_DIR,
                     extra_sym_libs={"power"} if project.power else ())
    return sch_dir, layouts


def _drawing(project, layouts, names, sabotage):
    sheets = {s.name: s for s in project.sheets}
    found = []
    for name in names:
        placed, height = layouts[name]
        if sabotage == "overlap" and len(placed) > 1:
            keys = list(placed)
            placed = dict(placed)
            placed[keys[1]] = placed[keys[0]]
        for a, b in W.overlaps(sheets[name].parts, placed, project.power):
            found.append(Finding("overlap", name, "%s <-> %s" % (a, b)))
        if not W.fits(height, project.paper):
            found.append(Finding("sheet_edge", name,
                                 "content is %.0f mm tall; %s leaves %.0f above the "
                                 "title block" % (height, project.paper,
                                                  W.PAPER[project.paper][1] - W.TITLE_BLOCK_H)))
    return found


def _netlist(project, sch_dir, refs=None):
    exported = {n: v for n, v in export_netlist(project, sch_dir).items()
                if not n.startswith("unconnected-")}
    intended = intended_nets(project)
    if refs is not None:
        def touches(nodes):
            return any(r in refs for r, _ in nodes)
        intended = {n: v for n, v in intended.items() if touches(v)}
        exported = {n: v for n, v in exported.items() if touches(v)}
    return [Finding("netlist", "-", bad) for bad in compare(intended, exported)]


def _pdf_and_pngs(project, sch_dir, out_dir, names, overview):
    import fitz   # PyMuPDF; imported here so the fast level needs nothing extra
    pdf = os.path.join(out_dir, project.name + ".pdf")
    rc, text = run([ksexp.KICAD_CLI, "sch", "export", "pdf", "-o", pdf,
                    os.path.join(sch_dir, project.name + ".kicad_sch")],
                   "kicad-cli sch export pdf", quiet=True)
    if rc != 0:
        raise RuntimeError("PDF export failed (rc=%d):\n%s" % (rc, text))
    doc = fitz.open(pdf)
    order = [s.name for s in project.sheets]
    want_pages = 1 if project.flat else len(order) + 1
    if doc.page_count != want_pages:
        raise RuntimeError("%s has %d pages, expected %d" % (pdf, doc.page_count, want_pages))
    pngs = []
    if overview and not project.flat:
        pngs.append(os.path.join(out_dir, project.name + "-overview.png"))
        doc[0].get_pixmap(dpi=110).save(pngs[-1])
    for name in names:
        pngs.append(os.path.join(out_dir, name + ".png"))
        doc[0 if project.flat else order.index(name) + 1].get_pixmap(dpi=110).save(pngs[-1])
    doc.close()
    return pngs


def load_waivers(path):
    """`type | text found in an item description | reason`, one per line."""
    if path is None:
        return []
    out = []
    with open(path, encoding="utf-8") as fh:
        for i, line in enumerate(fh, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = [f.strip() for f in line.split("|")]
            if len(fields) != 3 or not all(fields):
                raise ValueError("%s:%d: want 'type | item text | reason', got %r"
                                 % (path, i, line))
            out.append(tuple(fields))
    return out


def match_waivers(violations, waivers):
    """violations: [(type, [item descriptions])]. Unwaived ones and waivers
    that match nothing both come back as findings: a stale waiver is a hole
    the next real violation would fall through."""
    used, found = set(), []
    for vtype, items in violations:
        hit = next((k for k, (wt, text, _) in enumerate(waivers)
                    if wt == vtype and any(text in d for d in items)), None)
        if hit is None:
            found.append(Finding("erc", "-", "%s: %s" % (vtype, "; ".join(items) or "(no items)")))
        else:
            used.add(hit)
    for k, (wt, text, _) in enumerate(waivers):
        if k not in used:
            found.append(Finding("erc_waiver", "-", "waiver matches nothing: %s | %s"
                                 % (wt, text)))
    return found


def _erc(project, sch_dir, out_dir):
    """ERC through kicad-cli, JSON report. Waivers match on the violation type
    and on text from an item description ("Symbol U1 Pin 9 ..."), never on
    the top-level description, which KiCad localizes (German on this machine)."""
    report = os.path.join(out_dir, "erc.json")
    if os.path.exists(report):
        os.remove(report)
    rc, text = run([ksexp.KICAD_CLI, "sch", "erc", "--format", "json",
                    "--severity-error", "--severity-warning"] + kicad_defines()
                   + ["-o", report, os.path.join(sch_dir, project.name + ".kicad_sch")],
                   "kicad-cli sch erc", quiet=True)
    if not os.path.exists(report):
        raise RuntimeError("ERC wrote no report (rc=%d):\n%s" % (rc, text))
    with open(report, encoding="utf-8") as fh:
        data = json.load(fh)
    violations = [(v["type"], [i.get("description", "") for i in v.get("items", [])])
                  for s in data["sheets"] for v in s["violations"]]
    return match_waivers(violations, load_waivers(project.waivers)), len(violations)


def _stability(factory, sabotage):
    found = []
    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        W.write_project(factory(), a)
        W.Uuids.random = sabotage == "stability"
        try:
            W.write_project(factory(), b)
        finally:
            W.Uuids.random = False
        names = sorted(os.listdir(a))
        _, mismatch, errors = filecmp.cmpfiles(a, b, names, shallow=False)
        found += [Finding("stability", "-", "%s differs between two writes" % n)
                  for n in mismatch + errors]
    return found


def sheet_level(project, name, out_dir, sabotage=None):
    """Level 2: all sheets written (KiCad cannot export one sub-sheet alone),
    one sheet checked and rendered."""
    names = [s.name for s in project.sheets]
    if name not in names:
        raise ValueError("no sheet %r; sheets: %s" % (name, ", ".join(names)))
    os.makedirs(out_dir, exist_ok=True)
    findings = fast(project)
    sch_dir, layouts = _write_all(project, out_dir)
    findings += _drawing(project, layouts, [name], sabotage)
    refs = {p.ref for s in project.sheets if s.name == name for p in s.parts}
    findings += _netlist(project, sch_dir, refs)
    for png in _pdf_and_pngs(project, sch_dir, out_dir, [name], overview=False):
        print("look at: " + png)
    return findings


def full_level(project, factory, out_dir, sabotage=None):
    """Level 3: everything, at the end of every task."""
    os.makedirs(out_dir, exist_ok=True)
    t = [time.monotonic()]

    def lap():
        t.append(time.monotonic())
        return t[-1] - t[-2]

    findings = fast(project)
    sch_dir, layouts = _write_all(project, out_dir)
    names = [s.name for s in project.sheets]
    findings += _drawing(project, layouts, names, sabotage)
    t_write = lap()
    findings += _netlist(project, sch_dir)
    t_net = lap()
    erc_found, n = _erc(project, sch_dir, out_dir)
    findings += erc_found
    t_erc = lap()
    pngs = _pdf_and_pngs(project, sch_dir, out_dir, names, overview=True)
    t_pdf = lap()
    findings += _stability(factory, sabotage)
    t_stab = lap()
    print("ERC: %d violation(s) reported, %d unwaived"
          % (n, len([f for f in erc_found if f.rule == "erc"])))
    print("timing: fast+write+drawing %.1f s, netlist %.1f s, ERC %.1f s, "
          "PDF+PNG %.1f s, stability %.1f s" % (t_write, t_net, t_erc, t_pdf, t_stab))
    for png in pngs:
        print("look at: " + png)
    return findings


def load_factory(path):
    spec = importlib.util.spec_from_file_location("gen_project_under_check",
                                                  os.path.abspath(path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.project


def main(argv=None):
    ap = argparse.ArgumentParser(description="Check a generated schematic "
                                 "(P3 spec §4).")
    ap.add_argument("--project", required=True,
                    help="a .py file defining project() -> gen.project.Project")
    level = ap.add_mutually_exclusive_group(required=True)
    level.add_argument("--fast", action="store_true", help="intent only, <1 s")
    level.add_argument("--sheet", metavar="NAME", help="check and render one sheet")
    level.add_argument("--full", action="store_true", help="everything, incl. ERC")
    ap.add_argument("--sabotage", metavar="SPEC",
                    help="break one thing on purpose: RULE, RULE:empty, erc, "
                         "overlap or stability")
    ap.add_argument("--out", help="output dir (default: <project dir>/out)")
    args = ap.parse_args(argv)

    factory = load_factory(args.project)
    out = args.out or os.path.join(os.path.dirname(os.path.abspath(args.project)), "out")
    t0 = time.monotonic()
    project = factory()
    if args.sabotage:
        apply_sabotage(project, args.sabotage)
    if args.fast:
        label, findings = "fast", fast(project)
    elif args.sheet:
        label = "sheet " + args.sheet
        findings = sheet_level(project, args.sheet, out, args.sabotage)
    else:
        label, findings = "full", full_level(project, factory, out, args.sabotage)
    dt = time.monotonic() - t0
    for f in findings:
        print(f)
    if findings:
        print("FAIL %s: %d finding(s), %.1f s" % (label, len(findings), dt))
        return 1
    print("PASS %s: %d parts, %.1f s" % (label, len(project.parts()), dt))
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Output directories stay out of git**

Append to `.gitignore`:
```
# P3 check-tool output (hardware/gen/check.py, default --out)
hardware/gen/fixtures/out/
hardware/reva/out/
```

- [ ] **Step 5: The demo's ERC waivers**

Create `hardware/gen/fixtures/demo-erc-waivers.txt` with only its header:
```
# ERC waivers for the hardware/gen demo fixture.
# Format: type | text found in one item description | reason
# Every line must match a violation; a line that matches nothing fails the check.
```
Run: `python hardware/gen/check.py --project hardware/gen/fixtures/demo.py --full`
For each `[erc]` finding: if it is a fixture mistake, fix `demo.py`; otherwise add one waiver line naming the item (e.g. `Symbol U1 Pin 14`) and a reason. Expected candidates: `pin_not_driven` on U1's SER/SRCLK/RCLK (driven from connector J3, a passive pin — reason: "driven from off the fixture through J3") and `lib_symbol_mismatch` on U2 (TL072 extends LM2904 and is embedded flattened — reason: "derived symbol embedded flattened by sch_writer; drawing identical"). Any other type is fixed, not waived. Rerun until `PASS full`, then open every `look at:` PNG with the Read tool and list in the report what each shows.

- [ ] **Step 6: Run the test**

Run: `python hardware/gen/test_check.py`
Expected: every line `ok`, then `ok: every rule passes the demo, ...`.

- [ ] **Step 7: Show the test RED once**

With the Edit tool, change `if examined == 0:` in `fast()` to `if examined < 0:`. Run the test; expected: exit 1 with six `:empty ... UNEXPECTED` lines. Revert with the Edit tool; rerun; expected `ok`.

- [ ] **Step 8: Measure the full level at Rev A's size**

Create `hardware/gen/fixtures/load.py`:
```python
#!/usr/bin/env python3
"""A 264-part, eleven-sheet stand-in for Rev A's size (P3 spec §4).

Used to measure how long the check tool's full level takes before Rev A
exists. Its verdict does not matter -- only the timing line does.
"""
import os
import sys

_HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
if _HW not in sys.path:
    sys.path.insert(0, _HW)

from gen.netlist import Part            # noqa: E402
from gen.project import Project, Sheet  # noqa: E402

FX = "C-FIXTURE"
SOIC16 = "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm"


def project():
    sheets = []
    for i in range(11):
        parts = []
        for k in range(2):
            u = Part("U%d%02d" % (k + 1, i), "74xx:74HC595", "74HC595", SOIC16, lcsc=FX)
            u.by_name("VCC", "3V3D").by_name("GND", "GND").by_name("~{SRCLR}", "3V3D")
            u.by_name("~{OE}", "GND").by_name("SER", "L%d_IN%d" % (i, k))
            u.by_name("SRCLK", "CLK").by_name("RCLK", "LATCH")
            for q, letter in enumerate("ABCDEFGH"):
                u.by_name("Q" + letter, "L%d_Q%d_%d" % (i, k, q))
            u.by_name("QH'", "L%d_IN1" % i if k == 0 else "L%d_IN0" % (i + 1))
            parts.append(u)
        for r in range(20):
            k, q = divmod(r % 16, 8)
            parts.append(Part("R%d%02d" % (i, r), "Device:R", "1k",
                              "Resistor_SMD:R_0603_1608Metric", lcsc=FX)
                         .by_number(1, "L%d_Q%d_%d" % (i, k, q)).by_number(2, "GND"))
        for c in range(2):
            parts.append(Part("C%d%02d" % (i, c), "Device:C", "100n",
                              "Capacitor_SMD:C_0603_1608Metric", lcsc=FX)
                         .by_number(1, "3V3D").by_number(2, "GND"))
        sheets.append(Sheet("s%02d" % i, "load sheet %d" % i, parts))
    return Project("load", "hardware/gen load fixture", sheets,
                   power={"GND": "power:GND", "3V3D": "power:+3V3"}, paper="A3")
```
Run (twice, the second run warm): `python hardware/gen/check.py --project hardware/gen/fixtures/load.py --full --out "C:/Users/bernd/AppData/Local/Temp/p3-load"`
Record the `timing:` line and the final line's total. The FAIL verdict is expected (no holes, one-pin nets at the chain ends) and irrelevant.

In the spec, `docs/superpowers/specs/2026-09-29-rev-a-p3-schematic-design.md` §4, replace the sentence `Rev A has about three times the parts; the full level's time is measured when the multi-sheet skeleton stands (stage 2b), not assumed.` with `**Measured <date>** on the 264-part, eleven-sheet load fixture (hardware/gen/fixtures/load.py): full level <total> s, of which ERC <erc> s.` using the second run's numbers.

- [ ] **Step 9: Wire into ctest**

Append to the ctest block:
```cmake
add_test(NAME hw_gen_check_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_check.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```
Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `ctest --test-dir build -R hw_gen --output-on-failure` — expected 3/3 pass.

- [ ] **Step 10: Commit**

```bash
git add hardware/gen .gitignore CMakeLists.txt docs/superpowers/specs/2026-09-29-rev-a-p3-schematic-design.md
```
```bash
git commit -m "hw(gen): check tool -- fast, sheet and full levels with sabotage modes

Six intent rules (single-pin nets, driver conflicts, rail domains, pin
accounting, PanelIds, LCSC/Source), each with a sabotage and an
examined-nothing guard; the sheet level renders a PNG; the full level adds
ERC against a waiver file, PDF and byte-stability. Full-level time measured
on a 264-part load fixture (P3 spec §4).

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: Rev A pot and LED assignment from the hole list

**Files:**
- Create: `hardware/reva/assign.py`
- Create: `hardware/reva/test_assign.py`
- Create (generated): `hardware/reva/panel-map.json`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `host/vcv/res/FireflowHW-holes.json` (P1): `{"holes": [{"id", "ids", "kind", "x_mm", "y_mm", ...}]}`, kinds `pot`/`led`/...
- Produces: `assign.assign(holes) -> dict` and `hardware/reva/panel-map.json` with keys `generated_by`, `source`, `sense_order`, `muxes` (`{sense: [mux, ...]}`), `pots` (`[{"id", "ids", "sense", "mux", "channel", "x_mm", "y_mm"}]`), `calibration` (`[{"id": "CAL_GND"|"CAL_A3V3", "sense", "mux", "channel"}]`), `spare` (`[{"sense", "mux", "channel"}]`), `leds` (`[{"id", "index", "x_mm", "y_mm"}]`). Mux numbers 0–9 are the enables EN0–EN9 of P2 §4. Plan 2's `blocks.py` and P6's firmware table read this file.

- [ ] **Step 1: Write the failing test**

Create `hardware/reva/test_assign.py`:
```python
#!/usr/bin/env python3
"""Rev A's pot and LED assignment (P3 spec §5) holds its invariants, and the
committed panel-map.json is what assign.py computes from today's hole list.

    python hardware/reva/test_assign.py      # exit code is the verdict
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import assign as A  # noqa: E402


def invariants(m, holes):
    bad = []
    pot_holes = {h["id"] for h in holes if h["kind"] == "pot"}
    led_holes = {h["id"] for h in holes if h["kind"] == "led"}
    ids = [p["id"] for p in m["pots"]]
    if len(ids) != len(set(ids)):
        bad.append("a pot appears twice")
    if set(ids) != pot_holes:
        bad.append("pots differ from the hole list: missing %s, extra %s"
                   % (sorted(pot_holes - set(ids)), sorted(set(ids) - pot_holes)))
    slots = [(r["mux"], r["channel"]) for r in m["pots"] + m["calibration"] + m["spare"]]
    if len(slots) != len(set(slots)):
        bad.append("a (mux, channel) slot is used twice")
    if len(slots) != A.CHANNELS * sum(len(v) for v in A.MUXES.values()):
        bad.append("%d slots accounted for, want 80" % len(slots))
    for r in m["pots"] + m["calibration"] + m["spare"]:
        if r["mux"] not in A.MUXES[r["sense"]] or not 0 <= r["channel"] < A.CHANNELS:
            bad.append("slot %s is not on its sense pin's muxes" % (r,))
    if sorted(c["id"] for c in m["calibration"]) != sorted(A.CALIBRATION):
        bad.append("calibration channels: %s" % m["calibration"])
    bands = [[p["x_mm"] for p in m["pots"] if p["sense"] == s] for s in A.SENSE_ORDER]
    for left, right in zip(bands, bands[1:]):
        if left and right and max(left) >= min(right):
            bad.append("regions overlap in x: %.2f >= %.2f" % (max(left), min(right)))
    leds = m["leds"]
    if sorted(l["id"] for l in leds) != sorted(led_holes):
        bad.append("LEDs differ from the hole list")
    if [l["index"] for l in leds] != list(range(len(leds))):
        bad.append("LED indices are not 0..%d in order" % (len(leds) - 1))
    if [l["x_mm"] for l in leds] != sorted(l["x_mm"] for l in leds):
        bad.append("LED indices do not run left to right")
    return bad


def main():
    holes = A.load_holes()
    failures = []
    m = A.assign(holes)
    failures += invariants(m, holes)
    if A.assign(holes) != m:
        failures.append("assign() is not deterministic")
    with open(A.OUT, encoding="utf-8") as fh:
        if json.load(fh) != m:
            failures.append("panel-map.json is stale -- run python hardware/reva/assign.py")
    # The invariants must be able to fail, and bad input must be refused.
    broken = json.loads(json.dumps(m))
    broken["pots"][1]["mux"], broken["pots"][1]["channel"] = \
        broken["pots"][0]["mux"], broken["pots"][0]["channel"]
    if not invariants(broken, holes):
        failures.append("invariants() missed a doubly used slot")
    pots = [h for h in holes if h["kind"] == "pot"]
    for label, bad_holes in (
            ("a duplicated pot id", holes + [dict(pots[0])]),
            ("81 pots", holes + [dict(pots[0], id="EXTRA_%d" % i, ids=["EXTRA_%d" % i])
                                 for i in range(11)])):
        try:
            A.assign(bad_holes)
            failures.append("assign() accepted %s" % label)
        except ValueError:
            pass
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: %d pots on 10 muxes, %d calibration, %d spare, %d LEDs"
          % (len(m["pots"]), len(m["calibration"]), len(m["spare"]), len(m["leds"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python hardware/reva/test_assign.py`
Expected: `ModuleNotFoundError: No module named 'assign'`.

- [ ] **Step 3: Write `hardware/reva/assign.py`**

```python
#!/usr/bin/env python3
"""Pot -> (sense pin, mux, channel) and LED -> index, computed from the panel.

P3 spec §5. Input: host/vcv/res/FireflowHW-holes.json, P1's hole list and
the single source of positions. Output: hardware/reva/panel-map.json, read by
the schematic blocks (plan 2) and the firmware table (P6). A panel change in
the grip test is one re-run of this script, not a redesign.

    python hardware/reva/assign.py          # rewrite panel-map.json
"""
import itertools
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
HOLES = os.path.join(ROOT, "host", "vcv", "res", "FireflowHW-holes.json")
OUT = os.path.join(HERE, "panel-map.json")

# P2 §3: SENSE_0/1 carry three muxes, SENSE_2/3 two. Mux n is enable ENn.
MUXES = {"SENSE_0": (0, 1, 2), "SENSE_1": (3, 4, 5),
         "SENSE_2": (6, 7), "SENSE_3": (8, 9)}
# Left-to-right order of the four regions -- the one knob P4 may turn.
SENSE_ORDER = ("SENSE_0", "SENSE_2", "SENSE_3", "SENSE_1")
CHANNELS = 8
CALIBRATION = ("CAL_GND", "CAL_A3V3")   # panel-scan spec §8: the scan reads its span


def load_holes(path=HOLES):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)["holes"]


def _split_regions(pots):
    """Four contiguous x-bands, one per sense pin in SENSE_ORDER.

    Pots sharing an x stay in one band. Every split whose bands fit their
    capacity -- both calibration channels going to the band with the most room
    -- is scored by its fullest band's fill ratio; the lowest wins, ties to
    the split whose cuts sit furthest left. Exhaustive: 63 columns give about
    40 000 splits, well under a second.
    """
    columns = {}
    for p in pots:
        columns.setdefault(p["x_mm"], []).append(p)
    cols = [columns[x] for x in sorted(columns)]
    prefix = [0]
    for c in cols:
        prefix.append(prefix[-1] + len(c))
    caps = [CHANNELS * len(MUXES[s]) for s in SENSE_ORDER]
    best = None
    for cuts in itertools.combinations(range(1, len(cols)), len(caps) - 1):
        bounds = (0,) + cuts + (len(cols),)
        sizes = [prefix[bounds[i + 1]] - prefix[bounds[i]] for i in range(len(caps))]
        free = [c - s for c, s in zip(caps, sizes)]
        cal = max(range(len(caps)), key=lambda i: (free[i], -i))
        used = [s + (len(CALIBRATION) if i == cal else 0) for i, s in enumerate(sizes)]
        if any(u > c for u, c in zip(used, caps)):
            continue
        score = (max(u / c for u, c in zip(used, caps)), cuts)
        if best is None or score < best[0]:
            best = (score, bounds, cal)
    if best is None:
        raise ValueError("%d pots do not fit %d channels in four contiguous bands"
                         % (len(pots), sum(caps)))
    _, bounds, cal = best
    regions = {s: [p for c in cols[bounds[i]:bounds[i + 1]] for p in c]
               for i, s in enumerate(SENSE_ORDER)}
    return regions, SENSE_ORDER[cal]


def _mux_groups(sense, pots):
    """Split a region's pots, left to right, into one group per mux."""
    muxes = MUXES[sense]
    ordered = sorted(pots, key=lambda p: (p["x_mm"], p["y_mm"], p["id"]))
    base, extra = divmod(len(ordered), len(muxes))
    groups, i = {}, 0
    for k, mux in enumerate(muxes):
        size = base + (1 if k < extra else 0)
        groups[mux] = sorted(ordered[i:i + size], key=lambda p: (p["y_mm"], p["x_mm"], p["id"]))
        i += size
    return groups


def assign(holes):
    pots = [h for h in holes if h["kind"] == "pot"]
    leds = [h for h in holes if h["kind"] == "led"]
    for kind, items in (("pot", pots), ("led", leds)):
        ids = [h["id"] for h in items]
        if len(ids) != len(set(ids)):
            raise ValueError("duplicate %s ids: %s"
                             % (kind, sorted({i for i in ids if ids.count(i) > 1})))
    regions, cal_sense = _split_regions(pots)
    pot_rows, cal_rows, spare_rows = [], [], []
    for sense in SENSE_ORDER:
        groups = _mux_groups(sense, regions[sense])
        fill = {m: [("pot", p) for p in groups[m]] for m in MUXES[sense]}
        if sense == cal_sense:
            for cal in CALIBRATION:
                m = min(MUXES[sense], key=lambda m: (len(fill[m]), m))
                fill[m].append(("cal", cal))
        for m in MUXES[sense]:
            if len(fill[m]) > CHANNELS:
                raise ValueError("mux %d holds %d inputs" % (m, len(fill[m])))
            for ch in range(CHANNELS):
                if ch >= len(fill[m]):
                    spare_rows.append({"sense": sense, "mux": m, "channel": ch})
                    continue
                kind, item = fill[m][ch]
                if kind == "cal":
                    cal_rows.append({"id": item, "sense": sense, "mux": m, "channel": ch})
                else:
                    pot_rows.append({"id": item["id"], "ids": item.get("ids", [item["id"]]),
                                     "sense": sense, "mux": m, "channel": ch,
                                     "x_mm": item["x_mm"], "y_mm": item["y_mm"]})
    led_rows = [{"id": h["id"], "index": i, "x_mm": h["x_mm"], "y_mm": h["y_mm"]}
                for i, h in enumerate(sorted(leds, key=lambda h: (h["x_mm"], h["y_mm"], h["id"])))]
    return {"generated_by": "hardware/reva/assign.py",
            "source": "host/vcv/res/FireflowHW-holes.json",
            "sense_order": list(SENSE_ORDER),
            "muxes": {s: list(MUXES[s]) for s in SENSE_ORDER},
            "pots": pot_rows, "calibration": cal_rows, "spare": spare_rows,
            "leds": led_rows}


def report(m):
    for sense in m["sense_order"]:
        rows = [p for p in m["pots"] if p["sense"] == sense]
        worst = 0.0
        for mux in m["muxes"][sense]:
            group = [p for p in rows if p["mux"] == mux]
            if not group:
                continue
            cx = sum(p["x_mm"] for p in group) / len(group)
            cy = sum(p["y_mm"] for p in group) / len(group)
            worst = max(worst, max(math.hypot(p["x_mm"] - cx, p["y_mm"] - cy) for p in group))
        xs = [p["x_mm"] for p in rows]
        print("%s: %2d pots, x %.1f..%.1f mm, farthest pot %.1f mm from its mux "
              "group's centre" % (sense, len(rows), min(xs), max(xs), worst))
    print("calibration on %s; %d spare channels; %d LEDs"
          % (", ".join("mux %d ch %d" % (c["mux"], c["channel"]) for c in m["calibration"]),
             len(m["spare"]), len(m["leds"])))


def main():
    m = assign(load_holes())
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(json.dumps(m, indent=1, sort_keys=True) + "\n")
    report(m)
    print("wrote %s" % os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Generate the map and run the test**

Run: `python hardware/reva/assign.py`
Expected: four region lines (each region's pot count within its capacity), a calibration line, `wrote hardware/reva/panel-map.json`. Paste the output into the report.
Run: `python hardware/reva/test_assign.py`
Expected: `ok: 70 pots on 10 muxes, 2 calibration, 8 spare, 19 LEDs`

- [ ] **Step 5: Show the test RED once**

With the Edit tool, change `SENSE_ORDER = ("SENSE_0", "SENSE_2", "SENSE_3", "SENSE_1")` to `SENSE_ORDER = ("SENSE_2", "SENSE_0", "SENSE_3", "SENSE_1")` in `assign.py` (without re-running `assign.py`). Run the test; expected: exit 1 with `panel-map.json is stale`. Revert with the Edit tool; rerun; expected `ok`.

- [ ] **Step 6: Wire into ctest**

Append to the ctest block:
```cmake
add_test(NAME reva_assign_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_assign.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```
Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `ctest --test-dir build --output-on-failure` (the whole suite) — expected all tests pass, including `hw_gen_*`, `reva_assign_guard` and `hw_cut_guard`.

- [ ] **Step 7: Commit**

```bash
git add hardware/reva CMakeLists.txt
```
```bash
git commit -m "hw(reva): pot and LED assignment computed from the P1 hole list

Four contiguous x-bands, one per sense pin, sized to 24/16/16/24 channels;
pots grouped per mux left to right, channels top to bottom; both
calibration channels in the roomiest band; LEDs indexed left to right.
panel-map.json is guarded against drifting from the hole list.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Carried into plan 2

Found while writing this plan; plan 2 has to answer each:

- **`SR_DIN` has two outputs.** The coupon's ERC reports `pin_to_pin` between U_IN1 Q7 and U_SM D10 — the Daisy symbol declares D10 (`SPI_SCK`) an output. Rev A wires the same pair, so `driver_conflict` and ERC both fire. Plan 2 decides between a per-pin type override on the module part and a reasoned waiver.
- **Derived symbols** (`74HC165` extends `74LS165`, `TL072` extends `LM2904`) raise `lib_symbol_mismatch` because the writer embeds them flattened. Plan 2 either embeds them as KiCad does or keeps the waiver.
- **Footprint existence.** Spec §6 lets exactly one footprint stay open (`P4`, the SD socket). No rule checks footprints yet; plan 2 adds one to `RULES`, with its sabotage.
- **Module part is `strict`** so `pins_accounted` covers spec §4's "every module pin used or marked unconnected" (A6, A8, A9 via `no_connect`).

