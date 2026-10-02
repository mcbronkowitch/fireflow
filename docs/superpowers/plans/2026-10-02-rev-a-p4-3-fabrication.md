# Rev A P4-3 Fabrication Data Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the committed Rev A board schematic links with real KiCad parity and a readable silkscreen, and build the export stage that turns it into JLC's order package (Gerbers, drill, CPL, BOM, assembly sheets), every step gated by checks that can each go red.

**Architecture:** The schematic writer gains an all-global-labels switch so KiCad names nets as the board does. Placement links every footprint to its symbol (path, full footprint ID, fields, DNP) and names unconnected pads. A new pure pass `hardware/reva/silk.py` runs last in `route.build()`. `route_check.py` gains `paths`, `parity` and four silk steps. A new stage `hardware/reva/fab.py` with `fab_check.py` and the guard `reva_fab_guard` exports the package to `hardware/reva/out/fab/`, and to `hardware/reva/fab/` only on `--release` with empty known lists and verified rotations. The coupon's assembly-sheet code moves to `hardware/gen/assembly.py` and draws Rev A's two sheets.

**Tech Stack:** Python 3.11 under KiCad 10.0.5 (`pcbnew`), `kicad-cli` (netlist, DRC with schematic parity, Gerber/drill/position export, render), plain-script guards wired into ctest.

**Spec:** `docs/superpowers/specs/2026-10-02-rev-a-p4-3-fabrication-design.md`

## Global Constraints

- Work happens in the worktree `C:\Users\bernd\Documents\AI\FireFlow\.claude\worktrees\reva-p4-3-fab` on branch `reva-p4-3-fab`. Another agent works in the main checkout at the same time: never write there, never `git stash` (the stack is shared), never merge or push.
- Units mm, y down; board coordinates equal panel coordinates. Outline x 2.0–302.8, y 9.25–119.25 (`place.X0, Y0, X1, Y1`).
- Silkscreen (master plan rule 3): every visible silk text ≥ **1.0 mm** high, stroke **0.15 mm**; no text over a pad (mask opening) or over other silk. Front references hidden; front outlines and polarity marks stay. Back references placed by search; a reference with no free spot is hidden and listed in `silk.NO_ROOM`.
- Parity: `kicad-cli pcb drc --schematic-parity` reports **0** items on the committed board. Footprint paths are `/<sheet uuid>/<symbol uuid>` (no root), checked against KiCad's own netlist export, because parity matches by reference (spec §3).
- Gerbers: exactly F.Cu, In1.Cu, In2.Cu, B.Cu, F.Mask, B.Mask, F.Silkscreen, B.Silkscreen, F.Paste, B.Paste, Edge.Cuts. Drill with `--excellon-separate-th`. Every date field replaced by `FIXED_DATE = "2000-01-01T00:00:00+00:00"` (Gerber/drill ISO form) or `"2000-01-01 00:00:00"` (the `G04 Created by` line). Zip with fixed member timestamps, sorted members.
- CPL: from `kicad-cli pcb export pos --format csv --units mm --side both --smd-only --exclude-dnp`; columns `Designator,Mid X,Mid Y,Layer,Rotation`; Layer `Bottom`; rotation `(BOTTOM_SIGN * rot + ROT_FIX[package]) % 360`. `ROT_FIX` entries and `BOTTOM_SIGN` each carry a `verified` date that stays `None` until Bastian has looked at JLC's placement preview.
- BOM: P3's `hardware/reva/bom-jlc.csv`, copied unchanged. No second BOM writer.
- `fab.py --release` refuses while any `KNOWN_PANEL` list (place_check, route_check) is non-empty or any rotation entry is unverified. The refusal is not a ctest gate.
- Checks: every gated step has a sabotage `<step>` and `<step>_missing`, and each sabotage has a phrase (WHY) only its failure prints, on that step's own lines. A step that examined nothing is red. Thresholds live in the check file. Net names, footprint names and references are imported from the module that builds them, never copied as literals. Overlap of pure silk graphics is counted, never gated.
- The coupon: `hardware/coupon/coupon.kicad_sch`, `proof/coupon-assembly.svg` and `proof/coupon-overview.svg` stay byte-identical.
- Findings are not the implementer's to paper over. STOP and report verbatim when: a Rev A sheet does not fit A3 with global labels; parity leaves an item that is not one of the spec's two naming forms; `NO_ROOM` would exceed 25 references; the CPL does not have 78 rows; drill hole counts disagree with pcbnew; a `KNOWN_PANEL` list would need a new entry.
- Never commit `hardware/reva/bom-hand.csv`, `bom-jlc.csv`, `review.md`, `kicad/fp-lib-table` or `kicad/sym-lib-table` (Bastian's rule; their content does not change in this plan — if it does, STOP). Never commit anything under `hardware/reva/out/`.
- Tooling: KIPY = `/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe`; system `python` has no pcbnew. KICAD_CLI = `/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/kicad-cli.exe`. Never prefix a shell command with `cd`; no shell writes to repo files (Edit/Write only — no `sed -i`, `>`, `>>`, `tee`); no write (`rm`, `mv`, `git add`, `git commit`) behind `&&` or `;`. Long compounds go into a scratchpad script.
- ctest (after committing, from the worktree): `source env.sh` then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `ctest --test-dir build -R "hw_gen|reva|coupon" --output-on-failure`. **The controller re-runs the suite at every task boundary; on this project implementers have reported green suites that were red. Paste the real summary line.**
- Before sabotaging a file by hand for a RED proof, copy it to the scratchpad and restore from there — never `git checkout <file>` (it discards uncommitted implementation). Python files edited by a script come back LF; use Edit.
- pcbnew traps: `board.Remove` corrupts the next save (use `board.Delete`); a flipped footprint's pads report F.Cu from `GetLayerName()` (use `IsOnLayer`); `kipcb.new_board()` seeds the UUIDs and must be the first pcbnew object; `SaveBoard` writes a `.kicad_pro` beside the board; delete `__pycache__` before a size-neutral RED proof.
- Everything written into the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.

## Review Focus

1. **JLC's bottom-side rotation convention.** A per-footprint offset cannot fix a sign error that depends on the part's own rotation, so `BOTTOM_SIGN` is separate from `ROT_FIX` and verified on its own. Pinned in Task 6: `rot_table` red when `BOTTOM_SIGN` is not ±1, and the guard proves `--release` refuses an unverified `BOTTOM_SIGN`.
2. **The panel pass re-run.** After the hole list changes, stale `NO_ROOM` or `KNOWN_PANEL` entries must turn red rather than ride along, and `--release` must accept once the lists are empty. Pinned in Task 8: the guard runs `release_blockers()` against emptied lists and verified rotations and expects none.
3. **CPL and Gerbers on one origin.** JLC overlays the CPL on the Gerbers; a sign or origin slip puts every part off the board. Pinned in Task 6: `cpl` requires every CPL point inside the Edge.Cuts extent read from the exported Gerber, proven red by the `cpl_outside` sabotage.
4. **DNP and test points leaking into the order.** DNP parts and the 15 test points have footprints but must not be placed by JLC. Pinned in Task 6: `cpl` compares against the board's `smd` footprints minus DNP minus `exclude_from_pos_files`, and a sabotage clears C_LDO_T's DNP flag.
5. **Dates in another time zone or locale.** The normaliser must catch every date form, not just today's. Pinned in Task 6: `normalise()` unit cases with `+02:00`, `Z`, `-05:00` and both `G04` forms.

## File map

| File | Status | Responsibility |
|---|---|---|
| `.gitattributes` | modify | `text eol=lf` for the five generated text files (spec §4.5) |
| `hardware/gen/project.py` | modify | `Project(..., global_labels=False)` |
| `hardware/gen/sch_writer.py` | modify | `kind()` honours `global_labels`; `symbol_uuid()`, `sheet_uuid()`, `symbol_path()` pure helpers |
| `hardware/gen/test_sch_writer.py` | modify | the switch and the helpers |
| `hardware/reva/build.py` | modify | sets `global_labels=True` |
| `hardware/reva/kicad/*.kicad_sch` | regenerate | global labels |
| `hardware/gen/kipcb.py` | modify | `link_part()`, `set_pad_net()`; `new_board` docstring |
| `hardware/reva/place.py` | modify | `link_footprints()` at the end of `build()` |
| `hardware/reva/route.py` | modify | calls `silk.apply()` after the fill |
| `hardware/reva/silk.py` | create | the silkscreen pass and `NO_ROOM` |
| `hardware/reva/route_check.py` | modify | steps `paths`, `parity`, `silk_height`, `silk_front`, `silk_clear`, `no_room` |
| `hardware/reva/test_route.py` | modify | sections for the new steps (coverage is automatic) |
| `hardware/reva/kicad/reva.kicad_pcb` | regenerate | linked, silkscreened board |
| `hardware/gen/assembly.py` | create | board reader, label search, overlap check, SVG (from the coupon) |
| `hardware/coupon/scripts/assembly_plan.py` | modify | imports the moved code |
| `hardware/gen/test_assembly.py` | create | coupon SVGs byte-identical; `hw_gen_assembly_guard` |
| `hardware/reva/fab.py` | create | the export stage |
| `hardware/reva/fab_check.py` | create | the export checks and sabotages |
| `hardware/reva/test_fab.py` | create | `reva_fab_guard` |
| `CMakeLists.txt` | modify | `hw_gen_assembly_guard`, `reva_fab_guard` |
| `docs/hardware/fab/` | create | renders and assembly sheets of the first green run |
| `docs/superpowers/specs/2026-10-02-rev-a-p4-3-fabrication-design.md` | modify | dated amendments from the probes |
| `docs/roadmap.md` | modify | entry |

---

### Task 1: Worktree setup, baseline, and the annotation warning

No production code. Sets up the worktree's build, records the baseline, and finds the cause of the netlist export's annotation warning (spec §7) before Task 3 relies on that export.

**Files:**
- Create (untracked, gitignored): `env.sh` in the worktree root, a copy of the main checkout's `env.sh`
- Create (scratchpad, not committed): `probe_annotation.py`
- Modify: `docs/superpowers/specs/2026-10-02-rev-a-p4-3-fabrication-design.md` (§3, a bullet "Probed during execution, 2026-MM-DD")

**Interfaces:**
- Consumes: nothing.
- Produces: a working `source env.sh`; the baseline ctest line; the annotation warning's cause, and whether `kicad-cli sch export netlist` exits 0 while printing it.

- [ ] **Step 1: Copy env.sh into the worktree**

Read `C:\Users\bernd\Documents\AI\FireFlow\env.sh` with the Read tool and write the identical content to `C:\Users\bernd\Documents\AI\FireFlow\.claude\worktrees\reva-p4-3-fab\env.sh` with the Write tool. It is gitignored (`.gitignore:10`). Then:

Run: `git status --short`
Expected: empty (env.sh does not show).

- [ ] **Step 2: Baseline ctest**

Run: `source env.sh` then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `cmake --build build` then `ctest --test-dir build -R "hw_gen|reva|coupon" --output-on-failure`
Expected: every listed test passes. Write the summary line (`100% tests passed, 0 tests failed out of N`) into your report. If anything is red on the untouched branch, STOP and report it.

- [ ] **Step 3: Write the annotation probe**

`<scratchpad>/probe_annotation.py` (system Python):

```python
"""Why does kicad-cli's netlist export warn of annotation errors? Throwaway."""
import os, re, shutil, subprocess, tempfile
WT = r"C:\Users\bernd\Documents\AI\FireFlow\.claude\worktrees\reva-p4-3-fab"
CLI = r"C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe"
K = os.path.join(WT, "hardware", "reva", "kicad")

def export(kdir):
    out = os.path.join(kdir, "probe.net")
    r = subprocess.run([CLI, "sch", "export", "netlist", "--format", "kicadsexpr",
                        "-o", out, os.path.join(kdir, "reva.kicad_sch")],
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, (r.stdout + r.stderr).strip(), out

tmp = tempfile.mkdtemp()
kd = os.path.join(tmp, "hardware", "reva", "kicad")
shutil.copytree(K, kd)
shutil.copytree(os.path.join(WT, "hardware", "lib"), os.path.join(tmp, "hardware", "lib"))
rc, msg, out = export(kd)
print("as committed: rc", rc, "|", msg)
refs = set()
for f in os.listdir(kd):
    if f.endswith(".kicad_sch"):
        refs |= set(re.findall(r'\(reference "([^"#][^"]*)"\)', open(os.path.join(kd, f), encoding="utf-8").read()))
bare = sorted(r for r in refs if not r[-1].isdigit())
print("references without a trailing digit:", bare)
# hypothesis: KiCad treats a reference without a trailing number as unannotated
for f in os.listdir(kd):
    if f.endswith(".kicad_sch"):
        p = os.path.join(kd, f)
        t = open(p, encoding="utf-8").read()
        for r in bare:
            t = t.replace('"%s"' % r, '"%s9"' % r)
        open(p, "w", encoding="utf-8", newline="\n").write(t)
rc2, msg2, _ = export(kd)
print("with a digit appended: rc", rc2, "|", msg2)
net = open(out, encoding="utf-8").read()
for r in bare:
    m = re.search(r'\(comp \(ref "%s"\).*?\(tstamps "([^"]+)"\)\s*\(units' % re.escape(r), net, re.S)
    print("netlist", r, "tstamps", m.group(1) if m else "MISSING")
shutil.rmtree(tmp, ignore_errors=True)
```

- [ ] **Step 4: Run it**

Run: `python <scratchpad>/probe_annotation.py`
Expected: three answers. (a) Does the export exit 0 while warning? (b) Does appending a digit silence the warning (hypothesis)? (c) Does every bare-named part still carry `tstamps` in the netlist? If the warning has another cause, find it (try `kicad-cli sch erc --format json` on the same copy and look for annotation classes) and report it. If any part is missing from the netlist or lacks `tstamps`, STOP: Task 3's `paths` step cannot rely on the export.

- [ ] **Step 5: Amend the spec and commit**

Add a bullet to spec §3 under "Schematic parity": "Probed during execution, 2026-MM-DD: the annotation warning comes from …; the export exits …; every component carries `tstamps`." Replace the matching §7 risk bullet's guess with the finding.

```bash
git add docs/superpowers/specs/2026-10-02-rev-a-p4-3-fabrication-design.md
git commit -m "docs(spec): P4-3 the netlist export's annotation warning, probed" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: Global labels for Rev A, and line endings

**Files:**
- Modify: `hardware/gen/project.py` (constructor), `hardware/gen/sch_writer.py` (`kind()` in `write_project`, new helpers after `class Uuids`), `hardware/gen/test_sch_writer.py`, `hardware/reva/build.py` (`project()`), `.gitattributes`
- Regenerate: `hardware/reva/kicad/*.kicad_sch` (and whatever else `build.py` writes — see the never-commit list)

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `Project(..., global_labels=False)`; attribute `project.global_labels: bool`.
  - `sch_writer.sheet_uuid(project_name: str, sheet_name: str) -> str`
  - `sch_writer.symbol_uuid(project_name: str, ref: str, unit: int = 1) -> str`
  - `sch_writer.symbol_path(project_name: str, sheet_name: str, ref: str, unit: int = 1) -> str` — `"/<sheet uuid>/<symbol uuid>"`, the footprint path KiCad uses (root sheet omitted).
  All three compute the same uuid5 as `Uuids.__call__` without touching its used-key set.

- [ ] **Step 1: Write the failing tests**

Append to `hardware/gen/test_sch_writer.py`, before `main()`:

```python
def check_global_labels(failures):
    """Spec P4-3 §4.2: with global_labels every net label is global, and
    KiCad's netlist names the demo's sheet-local net without a sheet prefix."""
    proj = demo.project()
    local_nets = [n for n in {net for p in proj.parts() for net in p.nets.values()}
                  if n not in proj.power
                  and len({s.name for s in proj.sheets for p in s.parts if n in p.nets.values()}) == 1]
    if not local_nets:
        failures.append("global_labels: the demo fixture has no sheet-local net to test")
        return
    proj.global_labels = True
    with tempfile.TemporaryDirectory() as d:
        W.write_project(proj, d)
        text = "".join(open(os.path.join(d, f), encoding="utf-8").read()
                       for f in sorted(os.listdir(d)) if f.endswith(".kicad_sch"))
        if "(label " in text:
            failures.append("global_labels: a local label was written")
        # Raw names on purpose: check.export_netlist() normalizes the
        # "/sheet/" prefix away, which is exactly what this test must see.
        sch = os.path.join(d, proj.name + ".kicad_sch")
        net = os.path.join(d, proj.name + ".net")
        rc, _out = check.run([ksexp.KICAD_CLI, "sch", "export", "netlist", "-o", net, sch],
                             "kicad-cli sch export netlist", quiet=True)
        names = check.parse_exported_netlist(net) if rc == 0 else {}
        for n in local_nets:
            if n not in names:
                failures.append("global_labels: KiCad does not name %r without a prefix (has %s)"
                                % (n, sorted(k for k in names if n in k)))


def check_path_helpers(failures):
    """symbol_path() equals the instance path the writer puts in the file,
    minus the root sheet -- the form KiCad's netlist gives a footprint."""
    proj = demo.project()
    with tempfile.TemporaryDirectory() as d:
        W.write_project(proj, d)
        seen = 0
        for s in proj.sheets:
            text = open(os.path.join(d, s.name + ".kicad_sch"), encoding="utf-8").read()
            for p in s.parts:
                if p.ref.startswith("#"):
                    continue
                # The file carries the instance path "/<root>/<sheet>" and the
                # symbol's own (uuid ...); the footprint path joins sheet and symbol.
                want = W.symbol_path(proj.name, s.name, p.ref)
                sheet, sym = W.sheet_uuid(proj.name, s.name), W.symbol_uuid(proj.name, p.ref)
                inst = '(path "/%s/%s" (reference "%s")' % (W.Uuids(proj.name)("root"), sheet, p.ref)
                if want != "/%s/%s" % (sheet, sym) or inst not in text or '(uuid "%s")' % sym not in text:
                    failures.append("symbol_path(%s) %s does not match %s.kicad_sch" % (p.ref, want, s.name))
                seen += 1
        if not seen:
            failures.append("path helpers: no symbol examined")
```

Add `check_global_labels(failures)` and `check_path_helpers(failures)` to `main()` next to the existing calls. `check.run(args, what, quiet)` returns `(rc, output)` and `check.parse_exported_netlist(path)` returns `{net name: {(ref, pin)}}` (`hardware/gen/check.py:28`, `:39`). The demo fixture (`hardware/gen/fixtures/demo.py`) has a sheet-local net by its docstring; the test fails loudly if it does not.

**RED proof for `check_global_labels`:** it must fail before Step 3 for the right reason — the output must contain "a local label was written" (the demo writes its local net as a local label today), not an AttributeError from the attribute assignment.

- [ ] **Step 2: Run them to verify they fail**

Run: `python hardware/gen/test_sch_writer.py`
Expected: FAIL — `AttributeError: module 'gen.sch_writer' has no attribute 'symbol_path'`, or the global-label failure lines.

- [ ] **Step 3: Implement**

`hardware/gen/project.py`, constructor signature and body:

```python
    def __init__(self, name, title, sheets, power=None, domain_rails=None,
                 holes=None, waivers=None, paper="A3", flat=False, comments=(),
                 lib_dirs=None, ground="GND", global_labels=False):
        ...
        self.ground = ground
        # P4-3: every net label global, so KiCad names each net exactly as
        # the board does (no "/sheet/" prefix) and schematic parity holds.
        self.global_labels = global_labels
```

`hardware/gen/sch_writer.py`, in `write_project`'s `kind()`:

```python
    def kind(net):
        if net in project.power:
            return "power"
        if project.flat or project.global_labels or len(net_sheets[net]) > 1:
            return "global"
        return "local"
```

and after `class Uuids`:

```python
def _uuid5(project_name, *key):
    return str(uuid.uuid5(Uuids.NS, "/".join(str(x) for x in (project_name,) + key)))


def sheet_uuid(project_name, sheet_name):
    """The UUID write_project gives sheet `sheet_name` (key "sheet", name)."""
    return _uuid5(project_name, "sheet", sheet_name)


def symbol_uuid(project_name, ref, unit=1):
    """The UUID write_project gives unit `unit` of symbol `ref` (key "sym")."""
    return _uuid5(project_name, "sym", ref, unit)


def symbol_path(project_name, sheet_name, ref, unit=1):
    """A footprint's schematic link: "/<sheet uuid>/<symbol uuid>". KiCad's
    netlist omits the root sheet (probed 2026-10-02, P4-3 spec §3)."""
    return "/%s/%s" % (sheet_uuid(project_name, sheet_name), symbol_uuid(project_name, ref, unit))
```

`Uuids.__call__` keeps its own body (it also honours `Uuids.random` for the check tool's sabotage); the helpers must produce the same strings, which `check_path_helpers` proves.

`hardware/reva/build.py`, in `project()`: add `global_labels=True,` to the `Project(...)` call.

- [ ] **Step 4: Run the writer test**

Run: `python hardware/gen/test_sch_writer.py`
Expected: PASS (no failure lines, exit 0).

- [ ] **Step 5: Regenerate Rev A and measure the A3 fit**

Run: `python hardware/reva/build.py`
Then: `python hardware/gen/check.py --project hardware/reva/build.py --full --out <scratchpad>/reva-check`
Expected: the check tool's full level is green, including the paper-fit rule (`W.fits`). If any sheet no longer fits A3, STOP: report the sheet and its height. The spec's fallback (decision 5) needs Bastian before it is built.

Run: `git status --short`
Expected: only `hardware/reva/kicad/*.kicad_sch` (and `.gitattributes` after Step 6). If `bom-*.csv`, `review.md` or a lib table show up, run `git diff --ignore-cr-at-eol --stat -- <file>`; a real content change is a STOP.

- [ ] **Step 6: Line endings**

Append to `.gitattributes`:

```
# 2026-10-02 (P4-3 spec §4.5): build.py writes these five files with LF. With
# core.autocrlf=true a checkout writes CRLF, the index keeps the CRLF size, and
# every build.py run then leaves them "modified" with an empty diff. eol=lf
# makes the checkout write LF too. Proven: fresh checkout + build.py, status empty.
hardware/reva/bom-*.csv          text eol=lf
hardware/reva/review.md          text eol=lf
hardware/reva/kicad/*-lib-table  text eol=lf
```

Then prove it: delete the five files with the shell (`rm hardware/reva/bom-hand.csv hardware/reva/bom-jlc.csv hardware/reva/review.md hardware/reva/kicad/fp-lib-table hardware/reva/kicad/sym-lib-table`, one call), restore them (`git checkout -- <the five paths>`, one call — safe here: they carry no uncommitted work), run `python hardware/reva/build.py`, then `git status --short`.
Expected: only `.gitattributes` and the regenerated `.kicad_sch` files.

- [ ] **Step 7: Prove the coupon untouched**

`generate_schematic.main(out_dir)` takes the output directory (`hardware/coupon/scripts/generate_schematic.py:41`). Put this in `<scratchpad>/coupon_sch.py` and run it with system `python`:

```python
import filecmp, os, sys, tempfile
WT = r"C:\Users\bernd\Documents\AI\FireFlow\.claude\worktrees\reva-p4-3-fab"
sys.path[:0] = [os.path.join(WT, "hardware"), os.path.join(WT, "hardware", "coupon", "scripts")]
import generate_schematic as g
d = tempfile.mkdtemp()
g.main(d)
same = filecmp.cmp(os.path.join(d, "coupon.kicad_sch"),
                   os.path.join(WT, "hardware", "coupon", "coupon.kicad_sch"), shallow=False)
print("coupon schematic byte-identical:", same)
sys.exit(0 if same else 1)
```

Expected: `True` (the coupon is flat, so `kind()` already returned "global" for it).

- [ ] **Step 8: Guards, then commit**

Run: `python hardware/reva/test_build.py` and `python hardware/gen/test_coupon_netlist.py`
Expected: both exit 0.

```bash
git add .gitattributes hardware/gen/project.py hardware/gen/sch_writer.py hardware/gen/test_sch_writer.py hardware/reva/build.py hardware/reva/kicad/*.kicad_sch
git commit -m "hw(reva): P4-3 Rev A writes every net label global; generated text files are LF" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

Then ctest as in Global Constraints; the controller re-runs it.

---

### Task 3: Schematic links on the board, gated by `paths` and `parity`

**Files:**
- Modify: `hardware/gen/kipcb.py` (new `link_part`, `set_pad_net`; `new_board` docstring), `hardware/reva/place.py` (new `link_footprints`, called last in `build()`), `hardware/reva/route_check.py` (steps `paths`, `parity`, their sabotages), `hardware/reva/test_route.py` (only if a new step needs an `EXTRA`/`PROBES` entry — coverage of the sabotage tables is automatic)
- Regenerate: `hardware/reva/kicad/reva.kicad_pcb` (via `route.py --write`), `docs/hardware/routing/*.png`

**Interfaces:**
- Consumes: `sch_writer.symbol_path(project_name, sheet_name, ref, unit=1)` (Task 2); `build.project()` with `global_labels=True`.
- Produces:
  - `kipcb.link_part(fp, part, path: str, sheet_name: str, sheet_file: str) -> None`
  - `kipcb.set_pad_net(board, fp, number: str, net_name: str) -> int` (pads changed)
  - `place.link_footprints(s, proj) -> list[str]` (the unconnected net names it created, sorted)
  - `route_check` steps `"paths"` and `"parity"`; module constants `KICAD_DIR`, `LIB_DIR`, `SCH`; `PATHS_EMPTY`, `PARITY_EMPTY` phrases.
  - On the board: every footprint has `GetPath().AsString() == "/<sheet uuid>/<symbol uuid>"`, `GetFPIDAsString() == part.footprint`, fields `Datasheet`, `Description`, plus `LCSC`/`Source`/`PanelId` where the part has them, `IsDNP() == part.dnp`.

- [ ] **Step 1: Write the failing steps in `route_check.py`**

Add near the other module constants (after `PLANE_NETS`):

```python
HERE_RC = os.path.dirname(os.path.abspath(__file__))
KICAD_DIR = os.path.join(HERE_RC, "kicad")
LIB_DIR = os.path.normpath(os.path.join(HERE_RC, "..", "lib"))
SCH = os.path.join(KICAD_DIR, "reva.kicad_sch")
PATHS_EMPTY = "paths measured nothing: no component read from KiCad's netlist"
PARITY_EMPTY = "parity positive control found nothing"
PARITY_CONTROL = ("R1", "LCSC", "C0")   # this field, changed on a copy, must show up
_NETLIST_PATHS = None                    # {ref: path}, per process: the schematic does not change in a run
_PARITY_CACHE = {}                       # link key -> parity items
```

Add the steps (before `STEPS`):

```python
def _netlist_paths():
    """{ref: footprint path} from KiCad's own netlist export of the committed
    schematic: the component's sheetpath tstamps joined with its tstamps
    (P4-3 spec §3). Components without a footprint (J_SM1..4) are left out.
    This is the independent reader: it never calls sch_writer's uuid5."""
    global _NETLIST_PATHS
    if _NETLIST_PATHS is None:
        tmp = tempfile.mkdtemp(prefix="reva_net_")
        try:
            net = os.path.join(tmp, "reva.net")
            subprocess.run([ksexp.KICAD_CLI, "sch", "export", "netlist", "--format", "kicadsexpr",
                            "-o", net, SCH], capture_output=True)
            out = {}
            if os.path.exists(net):
                root = ksexp.parse_file(net)
                for c in ksexp.children(ksexp.child(root, "components"), "comp"):
                    if not ksexp.children(c, "footprint"):
                        continue
                    sheet = str(ksexp.child(ksexp.child(c, "sheetpath"), "tstamps")[1])
                    out[str(ksexp.child(c, "ref")[1])] = sheet + str(ksexp.child(c, "tstamps")[1])
            _NETLIST_PATHS = out
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    return dict(_NETLIST_PATHS)


def check_paths(s, pcb_path, prefix):
    want = {} if getattr(s, "paths_missing", False) else _netlist_paths()
    if not want:
        return False, PATHS_EMPTY, []
    have = {fp.GetReference(): fp.GetPath().AsString() for fp in s.board.GetFootprints()}
    bad = []
    for ref in sorted(set(want) | set(have)):
        if ref not in have:
            bad.append("%s: in KiCad's netlist, no footprint on the board" % ref)
        elif ref not in want:
            bad.append("%s: footprint with no component in KiCad's netlist" % ref)
        elif have[ref] != want[ref]:
            bad.append("%s: path %s, KiCad's netlist says %s" % (ref, have[ref] or "(none)", want[ref]))
    return not bad, "%d footprints against KiCad's netlist, %d wrong" % (len(have), len(bad)), bad


def _link_key(board):
    """What schematic parity depends on: footprint ids, fields, DNP, pad nets."""
    key = []
    for fp in sorted(board.GetFootprints(), key=lambda f: f.GetReference()):
        fields = tuple(sorted((f.GetName(), f.GetText()) for f in fp.GetFields()))
        nets = tuple(sorted((str(p.GetNumber()), p.GetNetname()) for p in fp.Pads()))
        key.append((fp.GetReference(), fp.GetFPIDAsString(), fp.IsDNP(), fields, nets))
    return tuple(key)


def _parity_items(pcb_path, with_schematic=True):
    """kicad-cli's schematic-parity items for a copy of `pcb_path` laid out as
    the committed project (schematic, project file, lib tables, hardware/lib),
    or None when kicad-cli wrote no report."""
    tmp = tempfile.mkdtemp(prefix="reva_parity_")
    try:
        kd = os.path.join(tmp, "hardware", "reva", "kicad")
        os.makedirs(kd)
        for name in sorted(os.listdir(KICAD_DIR)):
            sch = name.endswith(".kicad_sch")
            if (sch and with_schematic) or name.endswith(".kicad_pro") or name.endswith("-lib-table"):
                shutil.copyfile(os.path.join(KICAD_DIR, name), os.path.join(kd, name))
        shutil.copytree(LIB_DIR, os.path.join(tmp, "hardware", "lib"))
        board = os.path.join(kd, "reva.kicad_pcb")
        shutil.copyfile(pcb_path, board)
        out = os.path.join(tmp, "parity.json")
        subprocess.run([ksexp.KICAD_CLI, "pcb", "drc", "--schematic-parity", "--format", "json",
                        "-o", out, board], capture_output=True)
        if not os.path.exists(out):
            return None
        with open(out, encoding="utf-8") as fh:
            return json.load(fh).get("schematic_parity", [])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


_PARITY_REF_RE = re.compile(r"(?:Footprint|of) (\S+)")


def _parity_lines(items):
    out = []
    for it in items:
        refs = sorted({m for x in it.get("items", []) for m in _PARITY_REF_RE.findall(x.get("description", ""))})
        out.append("parity item %s on %s" % (it.get("type"), "/".join(refs) or "?"))
    return sorted(out)


def check_parity(s, pcb_path, prefix):
    with_sch = not getattr(s, "parity_missing", False)
    key = (with_sch, _link_key(s.board))
    if key not in _PARITY_CACHE:
        items = _parity_items(pcb_path, with_sch)
        # positive control: the same board with one field changed must show it
        ctrl_pcb = prefix + "-parity-control.kicad_pcb"
        b = pcbnew.LoadBoard(pcb_path)
        ref, field, val = PARITY_CONTROL
        b.FindFootprintByReference(ref).SetField(field, val)
        pcbnew.SaveBoard(ctrl_pcb, b)
        control = _parity_items(ctrl_pcb, with_sch)
        _PARITY_CACHE[key] = (items, control)
    items, control = _PARITY_CACHE[key]
    if items is None or not control:
        return False, "%s (control %s)" % (PARITY_EMPTY, "no report" if control is None else "0 items"), []
    lines = _parity_lines(items)
    return not lines, "schematic parity: %d items (positive control %d)" % (len(lines), len(control)), lines
```

Add the imports the module lacks (`json`, `shutil`, `subprocess`, `tempfile`, `from gen import ksexp`) — check the existing import block first. Add `("paths", check_paths), ("parity", check_parity),` to `STEPS` before `("report", report)`.

Sabotages (next to the others) and their table entries:

```python
def _sab_paths(s):
    """R1's and R2's schematic links swapped: parity cannot see it (it matches
    by reference, spec §3), only the netlist comparison can."""
    a, b = s.board.FindFootprintByReference("R1"), s.board.FindFootprintByReference("R2")
    pa, pb = a.GetPath(), b.GetPath()
    a.SetPath(pb)
    b.SetPath(pa)


def _sab_paths_missing(s):
    s.paths_missing = True


def _sab_parity(s):
    """C2's LCSC field changed: one footprint_symbol_field_mismatch."""
    s.board.FindFootprintByReference("C2").SetField("LCSC", "C1")


def _sab_parity_missing(s):
    """The schematic left out of the parity copy: the positive control finds nothing."""
    s.parity_missing = True
```

```python
SABOTAGES.update({"paths": _sab_paths, "paths_missing": _sab_paths_missing,
                  "parity": _sab_parity, "parity_missing": _sab_parity_missing})
TURNS_RED.update({"paths": "paths", "paths_missing": "paths",
                  "parity": "parity", "parity_missing": "parity"})
WHY.update({"paths": "KiCad's netlist says", "paths_missing": PATHS_EMPTY,
            "parity": "parity item footprint_symbol_field_mismatch on C2",
            "parity_missing": PARITY_EMPTY})
```

Write these into the existing dict literals rather than with `.update()` if that reads better next to the others; the content is what matters.

- [ ] **Step 2: Run the route checks on today's committed board to see them RED**

Put in `<scratchpad>/rc_on_committed.py` (KIPY) — it runs the checks on a copy of the committed board without routing:

```python
import os, shutil, sys
WT = r"C:\Users\bernd\Documents\AI\FireFlow\.claude\worktrees\reva-p4-3-fab"
sys.path[:0] = [os.path.join(WT, "hardware"), os.path.join(WT, "hardware", "reva")]
import route as R, route_check as RC, check_kit as CK
out = os.path.join(WT, "hardware", "reva", "out", "probe")
os.makedirs(out, exist_ok=True)
pcb = os.path.join(out, "committed.kicad_pcb")
shutil.copyfile(R.COMMITTED, pcb)
s = R.Routed().reload(pcb)
s.known = {k: set(v) for k, v in RC.KNOWN_PANEL.items()}
s.skip_render = True
names = sys.argv[1:] or ["paths", "parity"]
CK.run_steps([st for st in RC.STEPS if st[0] in names], s, pcb, os.path.join(out, "committed"))
```

Run: `"$KIPY" <scratchpad>/rc_on_committed.py paths parity`
Expected: `RED ... paths` with 213 "path (none)" lines and `RED ... parity` with ~199 items. If either is green, the check is vacuous — fix it before going on. Note the wall time of one parity call (two DRC runs).

- [ ] **Step 3: Implement the links**

`hardware/gen/kipcb.py`, after `add_part`:

```python
def link_part(fp, part, path, sheet_name, sheet_file):
    """The footprint's schematic link (P4-3 spec §4.2): KiCad path, the full
    footprint ID with its library nickname, the symbol's fields, DNP and the
    sheet it sits on -- everything schematic parity compares."""
    lib, _, name = part.footprint.partition(":")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    fp.SetPath(pcbnew.KIID_PATH(path))
    fp.SetSheetname(sheet_name)
    fp.SetSheetfile(sheet_file)
    # the same properties sch_writer gives the symbol, in the same order
    fp.SetField("Datasheet", "")
    fp.SetField("Description", part.note)
    for key, val in (("LCSC", part.lcsc), ("Source", part.source), ("PanelId", part.panel_id)):
        if val:
            fp.SetField(key, val)
    fp.SetDNP(bool(part.dnp))


def set_pad_net(board, fp, number, net_name):
    """Put every pad of `fp` numbered `number` on `net_name`; returns the count."""
    n = 0
    for pad in fp.Pads():
        if str(pad.GetNumber()) == str(number):
            pad.SetNet(_net(board, net_name))
            n += 1
    return n
```

Rewrite the `new_board` docstring paragraph that says `SetPath`/`GetPath` are never called: paths are now set by `link_part` from the schematic's deterministic uuid5s, and the seeded KIID generator is unrelated to them (a second board in one process would still collide on KIIDs, which is the remaining reason for the warning).

`hardware/reva/place.py`:

```python
from gen import sch_writer as W   # with the other gen imports


def unconnected_net(part, number):
    """KiCad's name for the net of a pin the schematic leaves unconnected
    (forms probed 2026-10-02: unconnected-(J_PWR-Pin_7-Pad7),
    unconnected-(U_IN1-~{Q7}-Pad7)); schematic parity checks it."""
    return "unconnected-(%s-%s-Pad%s)" % (part.ref, part.sym.pin(number)["name"], number)


def link_footprints(s, proj):
    """P4-3 spec §4.2: every footprint linked to its symbol, every unconnected
    pin on KiCad's unconnected-(...) net. Returns the net names created."""
    sheet_of = proj.sheet_of()
    made = []
    for p in proj.parts():
        fp = s.board.FindFootprintByReference(p.ref)
        if fp is None:
            continue
        sheet = sheet_of[p.ref]
        kipcb.link_part(fp, p, W.symbol_path(proj.name, sheet, p.ref), sheet, sheet + ".kicad_sch")
        for number in p.unconnected():
            name = unconnected_net(p, number)
            if kipcb.set_pad_net(s.board, fp, number, name):
                made.append(name)
    return sorted(made)
```

and at the end of `build()`, after `place_smd(s, proj)`: `s.unconnected = link_footprints(s, proj)` (add `self.unconnected = []` to `Placed.__init__`).

- [ ] **Step 4: Probe the placed board's parity before routing**

Run: `"$KIPY" hardware/reva/place.py`
Expected: GREEN as before (place_check does not look at net names it does not know; if a step goes red over the new nets, report which and why before changing the check).

Then run the parity part of Step 2's script against `hardware/reva/out/reva-placed.kicad_pcb` (copy the script, swap the path). Expected: 0 parity items and a positive control ≥ 1; `paths` green. If `unconnected-` items remain, compare the names KiCad expects (item text) with `unconnected_net()`: pins carrying a no-connect flag (`p.nc`) are the likely difference. Fix the rule so KiCad agrees and say what you changed. Any other residue is a STOP (spec §2.5).

- [ ] **Step 5: Re-route and commit the board**

Run: `"$KIPY" hardware/reva/route.py --write`
Expected: GREEN in every step including `paths` and `parity`; "copied to hardware/reva/kicad/reva.kicad_pcb". Report the router line (nets, vias, length, seconds) against P4-2's (580 vias, 11803.5 mm): the 22 single-pad nets are skipped by the router (`route.py:271`), so routing should be unchanged; a change is reported, not hidden.

- [ ] **Step 6: The guard, including the RED proofs**

Run: `python hardware/reva/test_route.py`
Expected: "all routing checks passed"; the four new sabotages are each red on their own step with their own phrase. Note the wall time and compare it to the 3600 s ctest TIMEOUT; if it exceeds 2400 s, report it (the parity cache keys on the link state, so only the parity sabotages should pay for new DRC runs).

- [ ] **Step 7: Commit**

```bash
git add hardware/gen/kipcb.py hardware/reva/place.py hardware/reva/route_check.py hardware/reva/test_route.py hardware/reva/kicad/reva.kicad_pcb docs/hardware/routing
git commit -m "hw(reva): P4-3 footprints carry their schematic links; paths and parity checks" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

ctest; the controller re-runs it.

---

### Task 4: The silkscreen pass, gated by four silk steps

**Files:**
- Create: `hardware/reva/silk.py`
- Modify: `hardware/reva/route.py` (`Routed.__init__`, `build()`), `hardware/reva/route_check.py` (steps `silk_height`, `silk_front`, `silk_clear`, `no_room`; `KNOWN_PANEL["silk"]`; sabotages), `hardware/reva/test_route.py` (only if `check_known_names`' name rule must learn the `"silk"` key — it is empty, so it should not)
- Regenerate: `hardware/reva/kicad/reva.kicad_pcb`, `docs/hardware/routing/*.png`

**Interfaces:**
- Consumes: the linked board of Task 3; `kipcb.courtyard_boxes(board) -> {ref: (l, t, r, b)}` in mm.
- Produces:
  - `silk.apply(board) -> silk.Report` with `report.placed: dict[str, tuple]` (ref → (side, gap_mm, angle)), `report.hidden: list[str]` (sorted; a hidden U_SM user text appears as `"U_SM:text"`).
  - `silk.NO_ROOM: dict[str, str]` (ref → measured reason).
  - `Routed.silk` (the report) set by `route.build()`.
  - `route_check` steps `"silk_height"`, `"silk_front"`, `"silk_clear"`, `"no_room"`; phrases `SILK_EMPTY`, `FRONT_EMPTY`, `CLEAR_EMPTY`, `ROOM_EMPTY`; constant `SILK_MIN_MM = 1.0`.

- [ ] **Step 1: Write the failing steps in `route_check.py`**

```python
SILK_MIN_MM = 1.0       # master plan working rule 3 (the check's own copy, not silk.py's)
SILK_TEXT_CLASSES = ("silk_over_copper", "silk_overlap")
# kicad-cli 10.0.5 item lines for silk text (probed 2026-10-02 on the routed board):
#   @(281.9400 mm, 73.6100 mm): Reference field of RV64
#   @(...): Footprint text of U_SM (INSTALL ON
_TEXT_ITEM_RE = re.compile(r"^\s*@\([^)]*\): (?:Reference field|Value field|Footprint text|Text) ", re.M)
SILK_EMPTY = "silk_height measured nothing: no visible silkscreen text"
FRONT_EMPTY = "silk_front measured nothing: no front footprint"
CLEAR_EMPTY = "silk_clear measured nothing: no silk text judged"
ROOM_EMPTY = "no_room measured nothing: no back footprint"


def _silk_texts(board):
    """(ref, kind, height mm, side) of every visible text on F/B.Silkscreen."""
    out = []
    for fp in board.GetFootprints():
        items = [("reference", fp.Reference()), ("value", fp.Value())]
        items += [("text", it) for it in fp.GraphicalItems() if isinstance(it, pcbnew.PCB_TEXT)]
        for kind, t in items:
            if t.IsVisible() and t.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                out.append((fp.GetReference(), kind, pcbnew.ToMM(t.GetTextSize().y),
                            "B" if t.GetLayer() == pcbnew.B_SilkS else "F"))
    return out


def check_silk_height(s, pcb_path, prefix):
    texts = [] if getattr(s, "silk_height_missing", False) else _silk_texts(s.board)
    if not texts:
        return False, SILK_EMPTY, []
    low = sorted(t for t in texts if t[2] < SILK_MIN_MM - 1e-6)
    return (not low, "%d silk texts, %d below %.1f mm" % (len(texts), len(low), SILK_MIN_MM),
            ["silk text below %.1f mm: %s %s %.2f mm" % (SILK_MIN_MM, r, k, h) for r, k, h, _ in low])


def check_silk_front(s, pcb_path, prefix):
    front = [] if getattr(s, "silk_front_missing", False) else \
        [fp for fp in s.board.GetFootprints() if not fp.IsFlipped()]
    if not front:
        return False, FRONT_EMPTY, []
    shown = sorted(fp.GetReference() for fp in front if fp.Reference().IsVisible())
    return (not shown, "%d front footprints, %d with a visible reference" % (len(front), len(shown)),
            ["visible front reference: %s" % r for r in shown])


def check_silk_clear(s, pcb_path, prefix):
    texts = [] if getattr(s, "silk_clear_missing", False) else _silk_texts(s.board)
    if not texts:
        return False, CLEAR_EMPTY, []
    txt, err = _drc(s, pcb_path, prefix)
    if err:
        return False, err, []
    found = {}
    for block in re.split(r"(?=^\[)", txt, flags=re.M):
        m = re.match(r"^\[([a-z0-9_]+)\]", block)
        if not m or m.group(1) not in SILK_TEXT_CLASSES or not _TEXT_ITEM_RE.search(block):
            continue
        refs = sorted(set(CK._REF_RE.findall(block)))
        found["%s %s" % (m.group(1), "/".join(refs))] = "silk text over a pad or other silk"
    ok, details, n_known = _judge(s, "silk", found)
    return ok, "%d silk texts judged, %d text entries (%d known)" % (len(texts), len(found), n_known), details
```

Add `"silk": set()` to `KNOWN_PANEL` with the comment `# silk text entries the panel pass must clear; expected none (P4-3 spec §5.1)`. Check `CK._REF_RE` matches "Reference field of RV64" and "Footprint text of U_SM (INSTALL ON" (it ends at line end with an optional layer; the U_SM line has a trailing "(INSTALL ON" — if `_REF_RE` misses it, use a local regex `r"(?:field|text|pad \S+ \[[^\]]*\]|PTH pad \S* ?\[[^\]]*\]|Pad \S+ \[[^\]]*\]) of ([A-Za-z_]+[A-Za-z_0-9]*)"` and say so).

```python
def check_no_room(s, pcb_path, prefix):
    import silk as SK
    back = [] if getattr(s, "no_room_missing", False) else \
        [fp for fp in s.board.GetFootprints() if fp.IsFlipped()]
    if not back:
        return False, ROOM_EMPTY, []
    found = {fp.GetReference(): "reference hidden, no free spot"
             for fp in back if not fp.Reference().IsVisible()}
    for fp in back:
        for it in fp.GraphicalItems():
            if isinstance(it, pcbnew.PCB_TEXT) and it.GetLayer() == pcbnew.B_SilkS and not it.IsVisible():
                found[fp.GetReference() + ":text"] = "footprint text hidden, no free spot"
    known = set(SK.NO_ROOM) | set(getattr(s, "extra_no_room", ()))
    ok, details, n_known = CK.judge(known, found)
    details = [d.replace("[NEW]", "[NEW] hidden back reference not in NO_ROOM") for d in details]
    return ok, "%d back footprints, %d hidden (%d listed)" % (len(back), len(found), n_known), details
```

Add the four steps to `STEPS` before `("report", report)`, after `parity`.

Sabotages:

```python
def _sab_silk_height(s):
    """R1's reference at 0.8 mm."""
    s.board.FindFootprintByReference("R1").Reference().SetTextSize(
        pcbnew.VECTOR2I(pcbnew.FromMM(0.8), pcbnew.FromMM(0.8)))


def _sab_silk_height_missing(s):
    s.silk_height_missing = True


def _sab_silk_front(s):
    s.board.FindFootprintByReference("RV1").Reference().SetVisible(True)


def _sab_silk_front_missing(s):
    s.silk_front_missing = True


def _sab_silk_clear(s):
    """R1's reference moved onto its own pad 1 and shown."""
    fp = s.board.FindFootprintByReference("R1")
    pad = [p for p in fp.Pads() if str(p.GetNumber()) == "1"][0]
    fp.Reference().SetVisible(True)
    fp.Reference().SetPosition(pad.GetPosition())


def _sab_silk_clear_missing(s):
    s.silk_clear_missing = True


def _sab_no_room(s):
    """R1's reference hidden although it had room: a NEW hidden reference."""
    s.board.FindFootprintByReference("R1").Reference().SetVisible(False)


def _sab_no_room_stale(s):
    """R2 listed although its reference is shown: a stale entry."""
    s.extra_no_room = ("R2",)


def _sab_no_room_missing(s):
    s.no_room_missing = True
```

Table entries: `TURNS_RED` maps each to its step (`no_room_stale` → `no_room`); `WHY`: `silk_height` → `"silk text below 1.0 mm: R1"`, `silk_height_missing` → `SILK_EMPTY`, `silk_front` → `"visible front reference: RV1"`, `silk_front_missing` → `FRONT_EMPTY`, `silk_clear` → `"silk_over_copper R1"`, `silk_clear_missing` → `CLEAR_EMPTY`, `no_room` → `"hidden back reference not in NO_ROOM"`, `no_room_stale` → `"listed as known but no longer fails"`, `no_room_missing` → `ROOM_EMPTY`. If R1 turns out to be in `NO_ROOM` after Step 4, pick another listed-free resistor for the three R1 sabotages and say which.

Create `hardware/reva/silk.py` with only `NO_ROOM = {}` for now (so `check_no_room` imports).

- [ ] **Step 2: See the steps RED on today's board**

Run: `"$KIPY" <scratchpad>/rc_on_committed.py silk_height silk_front silk_clear no_room`
Expected: `silk_height` green (all references are 1.0 mm today, spec §3); `silk_front` RED with 112 lines; `silk_clear` RED with many `silk_over_copper …`/`silk_overlap …` NEW entries; `no_room` green (nothing hidden yet). A green `silk_front` or `silk_clear` here means the check is vacuous.

- [ ] **Step 3: Write `silk.py`**

```python
#!/usr/bin/env python3
"""Rev A silkscreen pass (P4-3 spec §4.3). A pure function on a board:

    silk.apply(board) -> Report

route.build() calls it last, after the zone fill. Standalone, to iterate in
seconds on a saved board without routing:

    KIPY hardware/reva/silk.py [board.kicad_pcb]   (default out/reva-routed.kicad_pcb)

writes out/reva-silk.kicad_pcb, runs kicad-cli DRC on it and prints the silk
text entries, and renders the back to out/reva-silk-bottom.png.

Front: references hidden (the plate covers the front; the assembly sheet says
what goes where). Back: every reference 1.0 mm, placed by a search around its
courtyard; a reference with no free spot is hidden and must be in NO_ROOM.
Collision boxes are deliberately simple (KiCad's own text bounding box, pad
and silk-graphic bounding boxes grown by a gap); KiCad's DRC is the judge
(route_check silk_clear), so a miss here shows up there, never silently.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for _p in (HW, HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import pcbnew               # noqa: E402
from gen import kipcb      # noqa: E402

TEXT_MM = 1.0                                   # rule 3's floor, used as the size
STROKE_MM = 0.15
PAD_GAP_MM = 0.15                               # text box to a mask opening
SILK_GAP_MM = 0.15                              # text box to other silk
EDGE_GAP_MM = 0.5                               # text box to the board edge
GAPS_MM = (0.2, 0.5, 0.9, 1.4, 2.0, 2.7, 3.5)   # from the courtyard, nearest first
SLIDES_MM = (0.0, 0.8, -0.8)
SIDES = ("S", "N", "E", "W")
ANGLES = (0, 90)

# Back references (and footprint texts, "<ref>:text") with no free spot,
# measured by the first run (P4-3 Task 4); each entry says what blocked it.
NO_ROOM = {
}


class Report:
    def __init__(self):
        self.placed = {}
        self.hidden = []


def _natural(ref):
    m = re.match(r"([A-Za-z_]+?)(\d*)$", ref)
    return (m.group(1), int(m.group(2)) if m.group(2) else -1) if m else (ref, -1)


def _grow(bb, mm):
    g = pcbnew.FromMM(mm)
    return (bb.GetLeft() - g, bb.GetTop() - g, bb.GetRight() + g, bb.GetBottom() + g)


def _hits(a, boxes):
    return any(a[0] < b[2] and a[2] > b[0] and a[1] < b[3] and a[3] > b[1] for b in boxes)


def _inside(a, area):
    return a[0] >= area[0] and a[1] >= area[1] and a[2] <= area[2] and a[3] <= area[3]


def _obstacles(board):
    """Back mask openings and back silk graphics, as grown boxes (nm)."""
    out = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.IsOnLayer(pcbnew.B_Mask):
                out.append(_grow(pad.GetBoundingBox(), PAD_GAP_MM))
        for it in fp.GraphicalItems():
            if it.GetLayer() == pcbnew.B_SilkS and not isinstance(it, pcbnew.PCB_TEXT):
                out.append(_grow(it.GetBoundingBox(), SILK_GAP_MM))
    return out


def _candidates(box_mm, w_mm, h_mm):
    """Text centres around a box (l, t, r, b), nearest ring first."""
    l, t, r, b = box_mm
    cx, cy = (l + r) / 2, (t + b) / 2
    for gap in GAPS_MM:
        for side in SIDES:
            for slide in SLIDES_MM:
                if side == "S":
                    yield side, gap, (cx + slide, b + gap + h_mm / 2)
                elif side == "N":
                    yield side, gap, (cx + slide, t - gap - h_mm / 2)
                elif side == "E":
                    yield side, gap, (r + gap + w_mm / 2, cy + slide)
                else:
                    yield side, gap, (l - gap - w_mm / 2, cy + slide)


def _search(text, box_mm, area, blocked):
    """Move `text` to the first free candidate; returns (side, gap, angle) or None."""
    for angle in ANGLES:
        text.SetTextAngleDegrees(angle)
        text.SetPosition(kipcb._pt((box_mm[0] + box_mm[2]) / 2, (box_mm[1] + box_mm[3]) / 2))
        bb = text.GetBoundingBox()
        w, h = pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())
        for side, gap, (x, y) in _candidates(box_mm, w, h):
            text.SetPosition(kipcb._pt(x, y))
            tb = _grow(text.GetBoundingBox(), 0.0)
            if _inside(tb, area) and not _hits(tb, blocked):
                return side, gap, angle
    return None


def apply(board):
    rep = Report()
    edge = board.GetBoardEdgesBoundingBox()
    area = _grow(edge, -EDGE_GAP_MM)
    blocked = _obstacles(board)
    size = pcbnew.VECTOR2I(pcbnew.FromMM(TEXT_MM), pcbnew.FromMM(TEXT_MM))
    court = kipcb.courtyard_boxes(board)
    fps = sorted(board.GetFootprints(), key=lambda f: _natural(f.GetReference()))
    for fp in fps:
        if not fp.IsFlipped():
            fp.Reference().SetVisible(False)
    # footprint texts on the back first (U_SM's "INSTALL ON THIS SIDE"), so the
    # references avoid them; searched around their own box, starting in place
    for fp in fps:
        for it in fp.GraphicalItems():
            if not (isinstance(it, pcbnew.PCB_TEXT) and it.GetLayer() == pcbnew.B_SilkS):
                continue
            key = fp.GetReference() + ":text"
            here = _grow(it.GetBoundingBox(), 0.0)
            if _inside(here, area) and not _hits(here, blocked):
                rep.placed[key] = ("in place", 0.0, it.GetTextAngleDegrees())
            else:
                b = it.GetBoundingBox()
                spot = _search(it, (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()),
                                    pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom())), area, blocked)
                if spot is None:
                    it.SetVisible(False)
                    rep.hidden.append(key)
                    continue
                rep.placed[key] = spot
            blocked.append(_grow(it.GetBoundingBox(), SILK_GAP_MM))
    for fp in fps:
        if not fp.IsFlipped():
            continue
        ref = fp.GetReference()
        t = fp.Reference()
        t.SetVisible(True)
        t.SetTextSize(size)
        t.SetTextThickness(pcbnew.FromMM(STROKE_MM))
        spot = _search(t, court[ref], area, blocked)
        if spot is None:
            t.SetVisible(False)
            rep.hidden.append(ref)
            continue
        rep.placed[ref] = spot
        blocked.append(_grow(t.GetBoundingBox(), SILK_GAP_MM))
    rep.hidden.sort(key=_natural)
    return rep


def main(argv=None):
    from gen import pcb_proof as PP
    import check_kit as CK
    argv = sys.argv[1:] if argv is None else argv
    src = argv[0] if argv else os.path.join(HERE, "out", "reva-routed.kicad_pcb")
    board = kipcb.load(src)
    rep = apply(board)
    out = os.path.join(HERE, "out", "reva-silk.kicad_pcb")
    kipcb.save(board, out)
    print("placed %d, hidden %d: %s" % (len(rep.placed), len(rep.hidden), ", ".join(rep.hidden)))
    rpt = os.path.join(HERE, "out", "reva-silk-drc.rpt")
    try:
        PP.drc(out, rpt)
    except RuntimeError as e:
        print("drc:", e)
    txt = open(rpt, encoding="utf-8", errors="replace").read() if os.path.exists(rpt) else ""
    n = 0
    for block in re.split(r"(?=^\[)", txt, flags=re.M):
        if block.startswith(("[silk_over_copper]", "[silk_overlap]")) and \
                re.search(r"\): (?:Reference field|Value field|Footprint text|Text) ", block):
            n += 1
            print("  " + " | ".join(l.strip() for l in block.splitlines()[:4]))
    print("silk text entries:", n)
    PP.render(out, os.path.join(HERE, "out", "reva-silk-bottom.png"), "bottom")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Check before running: `kipcb._pt` exists (it is used by `add_part`); `kipcb.courtyard_boxes` returns mm tuples for every footprint, flipped ones included; `pcb_proof.drc(pcb, rpt)` raises `RuntimeError` on a missing report and `pcb_proof.render(pcb, png, side)` has that signature (`hardware/gen/pcb_proof.py:36`, `:77`). `PCB_TEXT.GetTextAngleDegrees()` and `SetTextAngleDegrees()` exist (probed 2026-10-02).

- [ ] **Step 4: Iterate standalone until KiCad's DRC shows no silk text entry**

First produce the routed board without committing: `"$KIPY" hardware/reva/route.py` (no `--write`; writes `out/reva-routed.kicad_pcb`).
Then, as often as needed: `"$KIPY" hardware/reva/silk.py`
Expected at the end: "silk text entries: 0", and a hidden list. **Look at `out/reva-silk-bottom.png`** (rule 2) and describe it in the report: are references next to their parts and readable?

If KiCad still reports text entries the placer thought free, grow the placer's gaps (`PAD_GAP_MM`, `SILK_GAP_MM`, `EDGE_GAP_MM`) in `silk.py` — never filter the check. If more than 25 references end up hidden, STOP and report with the PNG.

Fill `NO_ROOM` with the hidden list, one entry per line, the value naming what blocked it (e.g. `"R12": "every ring to 3.5 mm meets a pad or silk (dense field west of U_SM)"`). U_SM's text appears as `"U_SM:text"` only if it was hidden.

- [ ] **Step 5: Wire it into the route**

`hardware/reva/route.py`: `import silk` with the other local imports; in `Routed.__init__` add `self.silk = None`; at the end of `build()`, after `kipcb.fill_zones(s.board)`: `s.silk = silk.apply(s.board)`. In `main()`, after the router line, print `"silk: placed %d, hidden %d" % (len(s.silk.placed), len(s.silk.hidden))`.

- [ ] **Step 6: Route, check, commit the board**

Run: `"$KIPY" hardware/reva/route.py --write`
Expected: GREEN in every step, including the four silk steps; router line unchanged against Task 3 (silk does not touch copper). Look at `docs/hardware/routing/reva-routed-bottom.png` and `-top.png`.

Run: `python hardware/reva/test_route.py`
Expected: "all routing checks passed"; each of the nine new sabotages red on its own step with its own phrase. Report the wall time.

- [ ] **Step 7: Commit**

```bash
git add hardware/reva/silk.py hardware/reva/route.py hardware/reva/route_check.py hardware/reva/test_route.py hardware/reva/kicad/reva.kicad_pcb docs/hardware/routing
git commit -m "hw(reva): P4-3 readable silkscreen: front references hidden, back references placed by search" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

ctest; the controller re-runs it.

---

### Task 5: The assembly-sheet code moves to `hardware/gen/assembly.py`

A refactor with one gate: the coupon's two committed sheets come out byte-identical (line endings normalised — the working tree is CRLF, the script writes LF). Independent of Tasks 3–4.

**Files:**
- Create: `hardware/gen/assembly.py`, `hardware/gen/test_assembly.py`
- Modify: `hardware/coupon/scripts/assembly_plan.py`, `CMakeLists.txt` (new `hw_gen_assembly_guard`)

**Interfaces:**
- Consumes: `gen.ksexp`.
- Produces (`gen/assembly.py`):
  - constants `ADVANCE, ASCENDER, DESCENDER`; palette `INK, MUTED, RULE, PAPER, BOARD, ALERT`
  - `read_board(path, classify) -> (parts: list[dict], size: tuple)` — the coupon's reader with `classify(value, footprint) -> str` passed in; every part dict additionally carries `"side": "F" | "B"` (from the footprint's `(layer ...)`) and `"dnp": bool` (from `(attr ... dnp)`); a part with `dnp` gets `cls = "dnp"`.
  - `boxes_hit(box, others) -> bool`, `segment_hits(x0, y0, x1, y1, boxes) -> bool`
  - `class View(parts, ox, oy, scale, region=None, font=9.5, margin=135, big=frozenset(), style=None, bounds=None)` with `place_labels() -> list`, `check() -> list[str]`, `draw(out, grid=10, moat=True)`; `bounds=(x0, y0, x1, y1)` replaces the hard-wired `(0.0, 0.0, 100.0, 80.0)` full-view default (default `None` keeps it); `style` maps a class to `(fill, stroke)`; `big` is the set of refs that carry their name inside their outline.
  - `open_svg(out, width, height)`, `scale_bar(out, x, y, scale, mm=10)`

- [ ] **Step 1: Write the guard first**

`hardware/gen/test_assembly.py`:

```python
#!/usr/bin/env python3
"""hw_gen_assembly_guard: the assembly-sheet code lives in hardware/gen/
(P4-3 spec §4.4.5); the coupon's two committed sheets still come out
byte-identical. Line endings are normalised: the script writes LF, a Windows
checkout holds CRLF.

    python hardware/gen/test_assembly.py      # exit code is the verdict
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
SCRIPT = os.path.join(HW, "coupon", "scripts", "assembly_plan.py")
PROOF = os.path.join(HW, "coupon", "proof")
SHEETS = ("coupon-assembly.svg", "coupon-overview.svg")


def lf(path):
    with open(path, "rb") as fh:
        return fh.read().replace(b"\r\n", b"\n")


def main():
    fails = []
    with tempfile.TemporaryDirectory() as d:
        rc = subprocess.call([sys.executable, SCRIPT, "--out-dir", d, "--prefix", "coupon"])
        if rc:
            fails.append("assembly_plan.py exited %d" % rc)
        for name in SHEETS:
            got = os.path.join(d, name)
            if not os.path.exists(got):
                fails.append("%s was not written" % name)
            elif lf(got) != lf(os.path.join(PROOF, name)):
                fails.append("%s differs from hardware/coupon/proof/%s" % (name, name))
    try:
        sys.path.insert(0, HW)
        from gen import assembly  # noqa: F401
    except ImportError as e:
        fails.append("gen.assembly does not import: %s" % e)
    for f in fails:
        print("FAIL", f)
    print("FAILED: %d" % len(fails) if fails else "assembly sheets unchanged")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
```

Check `assembly_plan.py --prefix coupon` really names its outputs `coupon-assembly.svg` / `coupon-overview.svg` (read its `main()`); adjust the prefix if the default naming differs.

- [ ] **Step 2: Run it — RED for the right reason**

Run: `python hardware/gen/test_assembly.py`
Expected: exactly one failure, "gen.assembly does not import"; the two sheets already match. (If the sheets do not match on an untouched tree, STOP: the committed proof is stale, and that is not this task's to fix.)

- [ ] **Step 3: Move the code**

Create `hardware/gen/assembly.py` with the module docstring "Assembly sheets drawn from a KiCad board file -- moved from hardware/coupon/scripts/assembly_plan.py for Rev A (P4-3 spec §4.4.5)" and move, verbatim, from `assembly_plan.py`: the palette constants, `ADVANCE/ASCENDER/DESCENDER`, `read_board`, `boxes_hit`, `segment_hits`, `class View`, `open_svg`, `scale_bar`. Then make exactly these changes in the moved code:

1. `read_board(path)` → `read_board(path, classify)`; it calls the passed `classify` where it called the module's. Add `"side"` and `"dnp"` to each part dict; when `dnp` is true set `cls = "dnp"`.
2. `View.__init__` gains `big=frozenset(), style=None, bounds=None`; it stores `self.big`, `self.style`; every use of `BIG` becomes `self.big`, every `STYLE[...]` becomes `self.style[...]`; `region or (0.0, 0.0, 100.0, 80.0)` becomes `region or bounds or (0.0, 0.0, 100.0, 80.0)`, and `self.windowed` stays `region is not None`.

`assembly_plan.py` keeps `STYLE`, `CLASS_ORDER`, `CLASS_NAME`, `BIG`, `classify`, `ZONES`, `write_assembly`, `write_overview`, `write`, `main`, imports the rest (`from gen.assembly import ...`), and passes `classify` to `read_board` and `big=BIG, style=STYLE` to every `View(...)`.

- [ ] **Step 4: Run the guard — GREEN**

Run: `python hardware/gen/test_assembly.py`
Expected: "assembly sheets unchanged", exit 0.

- [ ] **Step 5: Prove it can go red**

Copy `hardware/gen/assembly.py` to the scratchpad. Change `ADVANCE = 0.602` to `ADVANCE = 0.61` with Edit, delete `hardware/gen/__pycache__`, run the guard. Expected: FAIL naming both sheets. Restore the file from the scratchpad copy (not with git), run again: green.

- [ ] **Step 6: Register and commit**

`CMakeLists.txt`, next to `hw_gen_coupon_guard`:

```cmake
add_test(NAME hw_gen_assembly_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_assembly.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```

```bash
git add hardware/gen/assembly.py hardware/gen/test_assembly.py hardware/coupon/scripts/assembly_plan.py CMakeLists.txt
git commit -m "hw(gen): the assembly-sheet code moves to hardware/gen for Rev A; coupon sheets unchanged" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

ctest (re-configure first so the new test registers); the controller re-runs it.

---

### Task 6: The export stage `fab.py` and its checks

**Files:**
- Create: `hardware/reva/fab.py`, `hardware/reva/fab_check.py`
- Modify: `docs/superpowers/specs/2026-10-02-rev-a-p4-3-fabrication-design.md` (§3 amendment: the measured package names, drill file names and counts, CPL row count)

**Interfaces:**
- Consumes: the committed board (`route.COMMITTED`) with links and DNP (Task 3); `hardware/reva/bom-jlc.csv`; `gen.pcb_proof.render`; `gen.ksexp.KICAD_CLI`.
- Produces (`fab.py`):
  - constants `OUT`, `RELEASE`, `DOCS`, `LAYERS` (11 names), `FIXED_DATE`, `FIXED_DATE_SP`, `ZIP_TIME`, `ROT_FIX: dict[str, dict]` (`{"deg": int, "verified": str | None}`), `BOTTOM_SIGN: dict` (`{"sign": int, "verified": str | None}`)
  - `normalise(text: str) -> str`
  - `class Fab` (state: `board_path`, `out`, `rot_fix`, `bottom_sign`, sabotage flags)
  - `prepare(board_src, out, sabotage="") -> Fab` (loads the board, applies a board-level sabotage, saves `out/board/reva.kicad_pcb`)
  - `export(s) -> None` (gerbers, drill, CPL, BOM copy, zip, renders; Task 7 adds the sheets)
  - `release_blockers(place_known=None, route_known=None, rot_fix=None, bottom_sign=None) -> list[str]`
  - `main(argv=None) -> int` with `--board`, `--out`, `--sabotage`, `--write`, `--release`, `--release-dir`
- Produces (`fab_check.py`): `STEPS`, `SABOTAGES`, `TURNS_RED`, `WHY`, `BOARD_SABOTAGES` (applied before export), `run(s) -> bool`, `sabotage(s, name)`, phrases `GERBER_EMPTY`, `DRILL_EMPTY`, `CPL_EMPTY`, `ROT_EMPTY`, `BOM_EMPTY`.

- [ ] **Step 1: Measure the export's names on today's board**

Run, writing into the scratchpad:
`"$KICAD_CLI" pcb export gerbers --layers F.Cu,In1.Cu,In2.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,F.Paste,B.Paste,Edge.Cuts -o <scratchpad>/g hardware/reva/kicad/reva.kicad_pcb`
`"$KICAD_CLI" pcb export drill --excellon-separate-th -o <scratchpad>/g/ hardware/reva/kicad/reva.kicad_pcb`
`"$KICAD_CLI" pcb export pos --format csv --units mm --side both --smd-only --exclude-dnp -o <scratchpad>/pos.csv hardware/reva/kicad/reva.kicad_pcb`
Then `ls <scratchpad>/g`, `head -3 <scratchpad>/pos.csv`, `cut -d, -f3 <scratchpad>/pos.csv | sort | uniq -c`, `grep -c . <scratchpad>/pos.csv`.
Expected: 11 Gerber files plus (probably) a job file; two drill files; the CPL has 78 data rows (+ header). Record the exact file names, the six package names and the row count. If the row count is not 78, STOP and report which designators differ from `bom-jlc.csv`.

- [ ] **Step 2: Write `fab_check.py` (the checks first)**

```python
#!/usr/bin/env python3
"""Checks on the Rev A order package (P4-3 spec §5.2), in P4-1/P4-2's pattern:
every gated step has a sabotage and a `_missing` one with its own phrase, and
a step that examined nothing is red. Thresholds live here, not in fab.py."""
import csv
import os
import re

import pcbnew
import check_kit as CK

POS_TOL_MM = 0.01
EDGE_TOL_MM = 0.01
BOARD_W_MM, BOARD_H_MM = 300.8, 110.0          # P4-1 spec §2.1, the outline
EXPECTED_LAYERS = ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "F.Mask", "B.Mask", "F.Silkscreen",
                   "B.Silkscreen", "F.Paste", "B.Paste", "Edge.Cuts")
DRILL_FILES = ("reva-PTH.drl", "reva-NPTH.drl")  # Step 1 measured; correct here if they differ

GERBER_EMPTY = "gerber_set measured nothing: no file in the gerber directory"
DRILL_EMPTY = "drill measured nothing: no drill file read"
CPL_EMPTY = "cpl measured nothing: no CPL row read"
ROT_EMPTY = "rot_table measured nothing: no package examined"
BOM_EMPTY = "bom_lcsc measured nothing: no BOM line read"

_COORD_RE = re.compile(r"X(-?\d+)Y(-?\d+)D0[123]\*")
_HOLE_RE = re.compile(r"^X-?[\d.]+Y-?[\d.]+", re.M)


def _gerber_name(layer):
    return "reva-%s." % layer.replace(".", "_")


def edge_extent(gerber_dir):
    """(x0, y0, x1, y1) in mm of Edge.Cuts, read from the exported Gerber
    (format 4.6, so coordinates are in 1e-6 mm; Gerber y points up)."""
    names = [n for n in os.listdir(gerber_dir) if n.startswith(_gerber_name("Edge.Cuts"))]
    if len(names) != 1:
        return None
    pts = [(int(x) / 1e6, int(y) / 1e6) for x, y in
           _COORD_RE.findall(open(os.path.join(gerber_dir, names[0]), encoding="utf-8").read())]
    if not pts:
        return None
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def check_gerber_set(s, _a=None, _b=None):
    gdir = os.path.join(s.out, "gerbers")
    names = sorted(os.listdir(gdir)) if os.path.isdir(gdir) else []
    if not names:
        return False, GERBER_EMPTY, []
    bad = []
    for layer in EXPECTED_LAYERS:
        hits = [n for n in names if n.startswith(_gerber_name(layer))]
        if len(hits) != 1:
            bad.append("missing layer %s" % layer if not hits else "layer %s twice: %s" % (layer, hits))
        elif os.path.getsize(os.path.join(gdir, hits[0])) == 0:
            bad.append("empty layer file %s" % hits[0])
    for d in DRILL_FILES:
        if d not in names or os.path.getsize(os.path.join(gdir, d)) == 0:
            bad.append("missing or empty drill file %s" % d)
    known = {n for n in names for l in EXPECTED_LAYERS if n.startswith(_gerber_name(l))} | set(DRILL_FILES)
    bad += ["unexpected file %s" % n for n in names if n not in known]
    ext = edge_extent(gdir)
    if ext is None:
        bad.append("Edge.Cuts has no coordinates")
    else:
        w, h = ext[2] - ext[0], ext[3] - ext[1]
        if abs(w - BOARD_W_MM) > EDGE_TOL_MM or abs(h - BOARD_H_MM) > EDGE_TOL_MM:
            bad.append("Edge.Cuts extent %.3f x %.3f mm, outline is %.1f x %.1f" % (w, h, BOARD_W_MM, BOARD_H_MM))
    return not bad, "%d files, edge %s" % (len(names), "%.2f x %.2f mm" % (ext[2] - ext[0], ext[3] - ext[1]) if ext else "?"), bad


def _board_holes(board):
    pth = sum(1 for t in board.GetTracks() if t.Type() == pcbnew.PCB_VIA_T)
    npth = 0
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetDrillSize().x <= 0:
                continue
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                npth += 1
            elif p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                pth += 1
    return {"reva-PTH.drl": pth, "reva-NPTH.drl": npth}


def check_drill(s, _a=None, _b=None):
    gdir = os.path.join(s.out, "gerbers")
    files = {} if getattr(s, "drill_missing", False) else \
        {d: open(os.path.join(gdir, d), encoding="utf-8").read()
         for d in DRILL_FILES if os.path.exists(os.path.join(gdir, d))}
    if not files:
        return False, DRILL_EMPTY, []
    want = _board_holes(s.board)
    bad, parts = [], []
    for d in DRILL_FILES:
        n = len(_HOLE_RE.findall(files.get(d, "")))
        parts.append("%s %d" % (d, n))
        if n != want[d]:
            bad.append("%s holes: file %d, board %d" % (d.split("-")[1].split(".")[0], n, want[d]))
    return not bad, ", ".join(parts), bad


def _read_csv(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh))


def _bom_refs(rows):
    return {r.strip() for row in rows for r in row["Designator"].split(",") if r.strip()}


def _placeable(board):
    """Board footprints JLC places: SMD, not DNP, not excluded from position files."""
    return {fp.GetReference(): fp for fp in board.GetFootprints()
            if fp.GetAttributes() & pcbnew.FP_SMD and not fp.IsDNP() and not fp.IsExcludedFromPosFiles()}


def check_cpl(s, _a=None, _b=None):
    rows = [] if getattr(s, "cpl_missing", False) else _read_csv(os.path.join(s.out, "cpl-jlc.csv"))
    if not rows:
        return False, CPL_EMPTY, []
    bom = _bom_refs(_read_csv(os.path.join(s.out, "bom-jlc.csv")))
    board = _placeable(s.board)
    cpl = {r["Designator"]: r for r in rows}
    bad = []
    for ref in sorted(set(board) | bom | set(cpl)):
        where = [n for n, src in (("the board", board), ("the BOM", bom), ("the CPL", cpl)) if ref in src]
        if len(where) != 3:
            bad.append("%s: in %s, not in %s" % (ref, " and ".join(where),
                       " and ".join(n for n in ("the board", "the BOM", "the CPL") if n not in where)))
    ext = edge_extent(os.path.join(s.out, "gerbers"))
    for ref, r in sorted(cpl.items()):
        x, y = float(r["Mid X"]), float(r["Mid Y"])
        if r["Layer"] != "Bottom":
            bad.append("%s: layer %s, every placed part is on the bottom" % (ref, r["Layer"]))
        fp = board.get(ref)
        if fp is not None:
            bx, by = pcbnew.ToMM(fp.GetPosition().x), pcbnew.ToMM(fp.GetPosition().y)
            if abs(x - bx) > POS_TOL_MM or abs(y + by) > POS_TOL_MM:   # KiCad's CPL negates y (spec §3)
                bad.append("%s: CPL position %.3f, %.3f, board %.3f, %.3f" % (ref, x, y, bx, -by))
        if ext and not (ext[0] <= x <= ext[2] and ext[1] <= y <= ext[3]):
            bad.append("%s: CPL point %.3f, %.3f outside the Edge.Cuts extent" % (ref, x, y))
    return not bad, "%d CPL rows, %d BOM designators, %d placeable on the board" % (len(cpl), len(bom), len(board)), bad


def check_rot_table(s, _a=None, _b=None):
    packages = set() if getattr(s, "rot_missing", False) else \
        {fp.GetFPID().GetLibItemName().wx_str() for fp in _placeable(s.board).values()}
    if not packages:
        return False, ROT_EMPTY, []
    bad = ["no ROT_FIX entry for %s" % p for p in sorted(packages) if p not in s.rot_fix]
    if s.bottom_sign.get("sign") not in (1, -1):
        bad.append("BOTTOM_SIGN is %r, must be 1 or -1" % s.bottom_sign.get("sign"))
    unverified = sorted(p for p in packages if p in s.rot_fix and not s.rot_fix[p]["verified"])
    line = "%d packages, %d unverified%s" % (len(packages), len(unverified),
                                             "" if s.bottom_sign.get("verified") else ", BOTTOM_SIGN unverified")
    return not bad, line, bad


def check_bom_lcsc(s, _a=None, _b=None):
    rows = [] if getattr(s, "bom_missing", False) else _read_csv(os.path.join(s.out, "bom-jlc.csv"))
    if not rows:
        return False, BOM_EMPTY, []
    bad, n = [], 0
    for row in rows:
        for ref in [r.strip() for r in row["Designator"].split(",") if r.strip()]:
            n += 1
            fp = s.board.FindFootprintByReference(ref)
            have = fp.GetFieldText("LCSC") if fp is not None and fp.HasField("LCSC") else None
            if have != row["LCSC"]:
                bad.append("%s: BOM says %s, the footprint's LCSC field %s" % (ref, row["LCSC"], have))
    return not bad, "%d BOM placements against the board's LCSC fields" % n, bad


STEPS = [("gerber_set", check_gerber_set), ("drill", check_drill), ("cpl", check_cpl),
         ("rot_table", check_rot_table), ("bom_lcsc", check_bom_lcsc)]


def run(s):
    # the step functions take (s, _a, _b) to fit check_kit's runner; there is
    # no board-file path or report prefix to hand them here
    return CK.run_steps(STEPS, s, None, None)


# -- sabotages. BOARD_SABOTAGES change the board or the tables before the
# export; the others change the exported files after it. -----------------------

def _sab_gerber_set(s):
    gdir = os.path.join(s.out, "gerbers")
    os.remove(os.path.join(gdir, [n for n in os.listdir(gdir) if n.startswith(_gerber_name("F.Paste"))][0]))


def _sab_gerber_set_missing(s):
    gdir = os.path.join(s.out, "gerbers")
    for n in os.listdir(gdir):
        os.remove(os.path.join(gdir, n))


def _sab_drill(s):
    p = os.path.join(s.out, "gerbers", "reva-PTH.drl")
    txt = open(p, encoding="utf-8", newline="").read()
    first = _HOLE_RE.search(txt)
    end = txt.index("\n", first.start()) + 1
    open(p, "w", encoding="utf-8", newline="").write(txt[:first.start()] + txt[end:])


def _sab_drill_missing(s):
    s.drill_missing = True


def _rewrite_cpl(s, fn):
    p = os.path.join(s.out, "cpl-jlc.csv")
    rows = _read_csv(p)
    rows = fn(rows)
    with open(p, "w", encoding="utf-8", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["Designator", "Mid X", "Mid Y", "Layer", "Rotation"], lineterminator="\n")
        w.writeheader()
        w.writerows(rows)


def _sab_cpl(s):
    _rewrite_cpl(s, lambda rows: [r for r in rows if r["Designator"] != "C1"])


def _sab_cpl_shift(s):
    def shift(rows):
        for r in rows:
            if r["Designator"] == "R1":
                r["Mid X"] = "%.4f" % (float(r["Mid X"]) + 0.1)
        return rows
    _rewrite_cpl(s, shift)


def _sab_cpl_outside(s):
    """R1's Mid Y with its sign flipped: the point leaves the Gerber outline
    (Review Focus 3 -- the one-origin rule needs its own RED)."""
    def flip(rows):
        for r in rows:
            if r["Designator"] == "R1":
                r["Mid Y"] = "%.4f" % -float(r["Mid Y"])
        return rows
    _rewrite_cpl(s, flip)


def _sab_cpl_dnp(s):
    """C_LDO_T loses its DNP flag on the board: the CPL now places it."""
    s.board.FindFootprintByReference("C_LDO_T").SetDNP(False)


def _sab_cpl_missing(s):
    s.cpl_missing = True


def _sab_rot_table(s):
    s.rot_fix.pop(next(k for k in sorted(s.rot_fix) if k.startswith("SOIC")))


def _sab_rot_sign(s):
    s.bottom_sign["sign"] = 0


def _sab_rot_table_missing(s):
    s.rot_missing = True


def _sab_bom_lcsc(s):
    s.board.FindFootprintByReference("C2").SetField("LCSC", "C1")


def _sab_bom_lcsc_missing(s):
    s.bom_missing = True


SABOTAGES = {"gerber_set": _sab_gerber_set, "gerber_set_missing": _sab_gerber_set_missing,
             "drill": _sab_drill, "drill_missing": _sab_drill_missing,
             "cpl": _sab_cpl, "cpl_shift": _sab_cpl_shift, "cpl_outside": _sab_cpl_outside,
             "cpl_dnp": _sab_cpl_dnp, "cpl_missing": _sab_cpl_missing,
             "rot_table": _sab_rot_table, "rot_sign": _sab_rot_sign,
             "rot_table_missing": _sab_rot_table_missing,
             "bom_lcsc": _sab_bom_lcsc, "bom_lcsc_missing": _sab_bom_lcsc_missing}
BOARD_SABOTAGES = {"cpl_dnp", "rot_table", "rot_sign", "bom_lcsc"}
TURNS_RED = {"gerber_set": "gerber_set", "gerber_set_missing": "gerber_set",
             "drill": "drill", "drill_missing": "drill",
             "cpl": "cpl", "cpl_shift": "cpl", "cpl_outside": "cpl", "cpl_dnp": "cpl",
             "cpl_missing": "cpl",
             "rot_table": "rot_table", "rot_sign": "rot_table", "rot_table_missing": "rot_table",
             "bom_lcsc": "bom_lcsc", "bom_lcsc_missing": "bom_lcsc"}
WHY = {"gerber_set": "missing layer F.Paste", "gerber_set_missing": GERBER_EMPTY,
       "drill": "PTH holes: file", "drill_missing": DRILL_EMPTY,
       "cpl": "C1: in the board and the BOM, not in the CPL",
       "cpl_shift": "R1: CPL position",
       "cpl_outside": "outside the Edge.Cuts extent",
       "cpl_dnp": "C_LDO_T: in the board and the CPL, not in the BOM",
       "cpl_missing": CPL_EMPTY,
       "rot_table": "no ROT_FIX entry for SOIC", "rot_sign": "BOTTOM_SIGN is 0",
       "rot_table_missing": ROT_EMPTY,
       "bom_lcsc": "C2: BOM says", "bom_lcsc_missing": BOM_EMPTY}


def sabotage(s, name):
    if name not in SABOTAGES:
        raise SystemExit("unknown sabotage %r; known: %s" % (name, ", ".join(sorted(SABOTAGES))))
    SABOTAGES[name](s)
    print("SABOTAGED: %s" % name)
```

Note how `check_cpl` words a missing designator: `"C1: in the board and the BOM, not in the CPL"` — the WHY phrases above are written to match that wording exactly; keep them in step if you change one. `fp.GetFPID().GetLibItemName().wx_str()` — if the binding returns a `UTF8` object without `wx_str`, use `str(...)`; check once in KIPY.

- [ ] **Step 3: Write `fab.py`**

```python
#!/usr/bin/env python3
"""Rev A order package (P4-3 spec §4.4).

    KIPY hardware/reva/fab.py [--board PCB] [--out DIR] [--sabotage NAME]
                              [--write] [--release] [--release-dir DIR]

Reads the committed board (route.py owns it) and writes to hardware/reva/out/fab/
(gitignored): gerbers/ (11 layers + PTH/NPTH drill, dates normalised),
reva-gerbers.zip, cpl-jlc.csv, bom-jlc.csv (P3's, copied), renders. Runs
fab_check. --write copies renders and assembly sheets to docs/hardware/fab/
when green. --release writes the package to hardware/reva/fab/ and refuses
while a known list is open or a rotation is unverified (release_blockers()).
"""
import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for _p in (HW, HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import pcbnew                      # noqa: E402
from gen import kipcb, ksexp       # noqa: E402
from gen import pcb_proof as PP    # noqa: E402

COMMITTED = os.path.join(HERE, "kicad", "reva.kicad_pcb")
BOM = os.path.join(HERE, "bom-jlc.csv")
OUT = os.path.join(HERE, "out", "fab")
RELEASE = os.path.join(HERE, "fab")
DOCS = os.path.normpath(os.path.join(HW, "..", "docs", "hardware", "fab"))
LAYERS = ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "F.Mask", "B.Mask", "F.Silkscreen",
          "B.Silkscreen", "F.Paste", "B.Paste", "Edge.Cuts")
FIXED_DATE = "2000-01-01T00:00:00+00:00"
FIXED_DATE_SP = "2000-01-01 00:00:00"
ZIP_TIME = (2000, 1, 1, 0, 0, 0)

# JLC's part library does not always share KiCad's zero orientation (spec
# §4.4.3). "deg" is added to KiCad's rotation; "verified" stays None until
# Bastian has looked at JLC's placement preview (a quote upload, no order) and
# then carries that date. No machine can prove a rotation.
ROT_FIX = {
    # Task 6 Step 1 measured these package names; starting values are 0.
}
# Bottom-side parts: rotation = (sign * KiCad rotation + deg) % 360. A sign
# error depends on each part's own rotation, so it is verified on its own.
BOTTOM_SIGN = {"sign": 1, "verified": None}

_DATE_RES = [
    (re.compile(r"(%TF\.CreationDate,)[^*]*(\*%)"), r"\g<1>" + FIXED_DATE + r"\g<2>"),
    (re.compile(r"(G04 #@! TF\.CreationDate,)[^*]*(\*)"), r"\g<1>" + FIXED_DATE + r"\g<2>"),
    (re.compile(r"(G04 Created by KiCad \([^)]*\) date )[^*]*(\*)"), r"\g<1>" + FIXED_DATE_SP + r"\g<2>"),
    (re.compile(r"(; DRILL file .* date )\S+"), r"\g<1>" + FIXED_DATE),
    (re.compile(r"(; #@! TF\.CreationDate,)\S+"), r"\g<1>" + FIXED_DATE),
]


def normalise(text):
    """Every date field kicad-cli 10.0.5 writes (probed 2026-10-02, spec §3)
    replaced by one constant, so two exports are byte-identical."""
    for rx, rep in _DATE_RES:
        text = rx.sub(rep, text)
    return text


class Fab:
    def __init__(self):
        self.board_path = None
        self.board = None
        self.out = None
        self.rot_fix = {k: dict(v) for k, v in ROT_FIX.items()}
        self.bottom_sign = dict(BOTTOM_SIGN)


def _cli(*args):
    r = subprocess.run([ksexp.KICAD_CLI] + list(args), capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if r.returncode:
        raise RuntimeError("kicad-cli %s rc=%d: %s" % (args[0:3], r.returncode, (r.stdout + r.stderr)[-300:]))


def prepare(board_src, out, sabotage=""):
    import fab_check as FC
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "board"))
    s = Fab()
    s.out = out
    s.board = kipcb.load(board_src)
    if sabotage in FC.BOARD_SABOTAGES:
        FC.sabotage(s, sabotage)
    s.board_path = os.path.join(out, "board", "reva.kicad_pcb")
    kipcb.save(s.board, s.board_path)
    return s


def _export_gerbers(s):
    gdir = os.path.join(s.out, "gerbers")
    os.makedirs(gdir, exist_ok=True)
    _cli("pcb", "export", "gerbers", "--layers", ",".join(LAYERS), "-o", gdir, s.board_path)
    _cli("pcb", "export", "drill", "--excellon-separate-th", "-o", gdir + os.sep, s.board_path)
    for n in sorted(os.listdir(gdir)):
        p = os.path.join(gdir, n)
        if n.endswith(".gbrjob"):
            os.remove(p)              # the job file is not part of the package
            continue
        with open(p, encoding="utf-8", newline="") as fh:
            txt = fh.read()
        with open(p, "w", encoding="utf-8", newline="") as fh:
            fh.write(normalise(txt))


def _export_cpl(s):
    tmp = os.path.join(s.out, "board", "pos.csv")
    _cli("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
         "--smd-only", "--exclude-dnp", "-o", tmp, s.board_path)
    with open(tmp, encoding="utf-8", newline="") as fh:
        rows = list(csv.DictReader(fh))
    out = []
    for r in rows:
        fix = s.rot_fix.get(r["Package"], {"deg": 0})
        sign = s.bottom_sign["sign"] if r["Side"] == "bottom" else 1
        rot = (sign * float(r["Rot"]) + fix["deg"]) % 360
        out.append({"Designator": r["Ref"], "Mid X": "%.4f" % float(r["PosX"]),
                    "Mid Y": "%.4f" % float(r["PosY"]),
                    "Layer": "Bottom" if r["Side"] == "bottom" else "Top",
                    "Rotation": "%g" % rot})
    with open(os.path.join(s.out, "cpl-jlc.csv"), "w", encoding="utf-8", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["Designator", "Mid X", "Mid Y", "Layer", "Rotation"],
                           lineterminator="\n")
        w.writeheader()
        w.writerows(out)


def _zip(s):
    gdir = os.path.join(s.out, "gerbers")
    with zipfile.ZipFile(os.path.join(s.out, "reva-gerbers.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for n in sorted(os.listdir(gdir)):
            info = zipfile.ZipInfo(n, date_time=ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(os.path.join(gdir, n), "rb") as fh:
                z.writestr(info, fh.read())


def export(s, renders=True):
    _export_gerbers(s)
    _export_cpl(s)
    shutil.copyfile(BOM, os.path.join(s.out, "bom-jlc.csv"))
    _zip(s)
    if renders:
        for side in ("top", "bottom"):
            PP.render(s.board_path, os.path.join(s.out, "reva-%s.png" % side), side)


def release_blockers(place_known=None, route_known=None, rot_fix=None, bottom_sign=None):
    import place_check as PC
    import route_check as RC
    pk = PC.KNOWN_PANEL if place_known is None else place_known
    rk = RC.KNOWN_PANEL if route_known is None else route_known
    rf = ROT_FIX if rot_fix is None else rot_fix
    bs = BOTTOM_SIGN if bottom_sign is None else bottom_sign
    out = ["place_check KNOWN_PANEL[%s]: %s" % (k, e) for k, v in sorted(pk.items()) for e in sorted(v)]
    out += ["route_check KNOWN_PANEL[%s]: %s" % (k, e) for k, v in sorted(rk.items()) for e in sorted(v)]
    out += ["ROT_FIX %s unverified" % k for k, v in sorted(rf.items()) if not v.get("verified")]
    if not rf:
        out.append("ROT_FIX is empty")
    if not bs.get("verified"):
        out.append("BOTTOM_SIGN unverified")
    return out


def main(argv=None):
    import fab_check as FC
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", default=COMMITTED)
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--sabotage", default="")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--release", action="store_true")
    ap.add_argument("--release-dir", default=RELEASE)
    a = ap.parse_args(argv)
    s = prepare(a.board, a.out, a.sabotage)
    export(s, renders=not a.sabotage)
    if a.sabotage and a.sabotage not in FC.BOARD_SABOTAGES:
        FC.sabotage(s, a.sabotage)
    green = FC.run(s)
    blockers = release_blockers()
    print("order_ready: %s" % ("yes" if not blockers else "no, %d open items" % len(blockers)))
    for b in blockers:
        print("        " + b)
    if a.write and green and not a.sabotage:
        os.makedirs(DOCS, exist_ok=True)
        for n in sorted(os.listdir(a.out)):
            if n.endswith((".png", ".svg")):
                shutil.copyfile(os.path.join(a.out, n), os.path.join(DOCS, n))
        print("renders and sheets copied to", os.path.relpath(DOCS))
    if a.release:
        if not green or blockers or a.sabotage:
            print("not released: %s" % ("the run is RED" if not green else "%d open items" % len(blockers)))
            return 1
        if os.path.isdir(a.release_dir):
            shutil.rmtree(a.release_dir)
        shutil.copytree(a.out, a.release_dir, ignore=shutil.ignore_patterns("board"))
        print("released to", os.path.relpath(a.release_dir))
    print("GREEN" if green else "RED")
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
```

Fill `ROT_FIX` with the six package names Step 1 measured, each `{"deg": 0, "verified": None}`. Fill `fab_check.DRILL_FILES` with the drill names Step 1 measured. `normalise()` must also cover any date form Step 1's files show that the five patterns miss — grep the exported files for `20[0-9][0-9]-` after normalising; nothing may remain.

- [ ] **Step 4: Run it**

Run: `"$KIPY" hardware/reva/fab.py`
Expected: every step green; the `drill` line shows two counts that match pcbnew; `order_ready: no` with the known items and the unverified rotations listed. If `drill` disagrees with pcbnew, STOP and report both numbers (slots and oval holes are the likely cause).

Run it a second time into another directory (`--out <scratchpad>/fab2`) and compare every file except `*.png`: `diff -rq --exclude=*.png --exclude=board hardware/reva/out/fab <scratchpad>/fab2`.
Expected: no output. (`board/` holds the saved copy; it is byte-stable too, but it is not part of the package.)

- [ ] **Step 5: Every sabotage RED by hand, once**

For each name in `fab_check.SABOTAGES`: `"$KIPY" hardware/reva/fab.py --sabotage NAME --out <scratchpad>/sab`
Expected: exit 1, its step `RED`, and its WHY phrase on that step's lines. Task 8's guard repeats this automatically; this step is the first RED proof.

- [ ] **Step 6: Amend the spec and commit**

Spec §3, a bullet "Probed during execution, 2026-MM-DD": the eleven file names, the drill file names and hole counts (PTH/NPTH), the CPL row count and the six package names.

```bash
git add hardware/reva/fab.py hardware/reva/fab_check.py docs/superpowers/specs/2026-10-02-rev-a-p4-3-fabrication-design.md
git commit -m "hw(reva): P4-3 export stage: Gerbers, drill, JLC CPL with rotation table, BOM, checks" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 7: Rev A's two assembly sheets, gated by `assembly`

**Files:**
- Modify: `hardware/reva/fab.py` (classifier, style, `write_sheets`, called from `export`), `hardware/reva/fab_check.py` (step `assembly`, sabotages)

**Interfaces:**
- Consumes: `gen.assembly` (Task 5): `read_board(path, classify)`, `View(..., big=, style=, bounds=)`, `open_svg`, `scale_bar`, palette; `fab.Fab`, `fab.export` (Task 6).
- Produces: `fab.classify(value, footprint) -> str`; `fab.write_sheets(s) -> None` writing `out/reva-assembly-front.svg` and `out/reva-assembly-back.svg` and setting `s.sheets = {"front": (n_parts, problems), "back": (n_parts, problems)}`; `fab_check` step `"assembly"`, phrase `ASSEMBLY_EMPTY`.

- [ ] **Step 1: The failing step**

`fab_check.py`:

```python
ASSEMBLY_EMPTY = "assembly measured nothing: no part on a sheet"


def check_assembly(s, _a=None, _b=None):
    sheets = {} if getattr(s, "assembly_missing", False) else getattr(s, "sheets", {})
    if not sheets or not any(n for n, _p in sheets.values()):
        return False, ASSEMBLY_EMPTY, []
    want = {"front": sum(1 for fp in s.board.GetFootprints() if not fp.IsFlipped()),
            "back": sum(1 for fp in s.board.GetFootprints() if fp.IsFlipped())}
    bad = []
    for name in sorted(want):
        path = os.path.join(s.out, "reva-assembly-%s.svg" % name)
        if name not in sheets or not os.path.exists(path) or os.path.getsize(path) == 0:
            bad.append("%s sheet missing" % name)
            continue
        n, problems = sheets[name]
        if n != want[name]:
            bad.append("%s sheet: %d parts, the board has %d" % (name, n, want[name]))
        bad += ["%s sheet: %s" % (name, p) for p in problems]
    return not bad, "front %s, back %s parts" % (sheets.get("front", ("?",))[0], sheets.get("back", ("?",))[0]), bad


def _sab_assembly(s):
    """Two labels on the back sheet forced onto one spot."""
    s.assembly_overlap = True


def _sab_assembly_missing(s):
    s.assembly_missing = True
```

Add `("assembly", check_assembly)` to `STEPS`; add both sabotages to `SABOTAGES` and `TURNS_RED` (→ `"assembly"`), and to `BOARD_SABOTAGES` (they must be set before `export` draws); `WHY`: `"assembly"` → `"two labels overlap"` (the phrase `View.check()` prints), `"assembly_missing"` → `ASSEMBLY_EMPTY`.

Run: `"$KIPY" hardware/reva/fab.py`
Expected: `RED ... assembly` with `ASSEMBLY_EMPTY` (no sheets drawn yet). Green here means the check is vacuous.

- [ ] **Step 2: Draw the sheets**

`fab.py`:

```python
from gen import assembly as A                       # with the other gen imports
from place import X0, Y0, X1, Y1                    # the outline, P4-1 spec §2.1

SHEET_SCALE = 4.0      # px per mm; raise to 6.0 if the label search cannot clear the sheet
SHEET_MARGIN = 135     # px around the board, room for leaders (the coupon's value)
STYLE = {"pot": ("#ddd2be", "#6b5f44"), "jack": ("#ded7c9", "#4a463c"),
         "led": ("#bde294", "#4a7c2b"), "key": ("#e8dcbd", "#8a7430"),
         "res": ("#f2ceae", "#b96532"), "cap": ("#cbe3f0", "#0f6e99"),
         "diode": ("#ccd4da", "#5a6a75"), "chip": ("#3a3a34", A.INK),
         "reg": ("#3a3a34", A.INK), "module": ("#e4ded1", "#4a463c"),
         "conn": ("#ded7c9", "#4a463c"), "probe": ("#ecd07a", "#8a6d18"),
         "dnp": (A.PAPER, A.ALERT)}
_CLASSES = (("Potentiometer", "pot"), ("PJ398", "jack"), ("LED", "led"), ("SW_Push", "key"),
            ("SOIC", "chip"), ("SOT-223", "reg"), ("DAISY", "module"), ("IDC", "conn"),
            ("TestPoint", "probe"), ("D_SMA", "diode"), ("R_0", "res"), ("C_0", "cap"),
            ("CP_", "cap"))
BACK_BIG = frozenset({"U_SM", "J_PWR"})     # carry their name inside their outline


def classify(value, footprint):
    """The Rev A part classes of the sheets, from the land pattern's name."""
    fam = footprint.split(":")[-1].upper()
    for key, cls in _CLASSES:
        if key.upper() in fam:
            return cls
    return "conn"


def _mirror(p):
    """A back part as seen from the back: every x mirrored about the board's centre."""
    c = X0 + X1
    q = dict(p)
    q["x"], q["x0"], q["x1"] = c - p["x"], c - p["x1"], c - p["x0"]
    if "pads" in p:
        q["pads"] = [dict(pd, x=c - pd["x"]) for pd in p["pads"]]
    return q


def write_sheets(s):
    parts, _size = A.read_board(s.board_path, classify)
    front = [dict(p, ref=p["value"]) for p in parts if p["side"] == "F"]   # panel ids
    back = [_mirror(p) for p in parts if p["side"] == "B"]
    s.sheets = {}
    w, h = (X1 - X0) * SHEET_SCALE, (Y1 - Y0) * SHEET_SCALE
    for name, sheet_parts, big, note in (
            ("front", front, frozenset(), "FRONT: panel parts, labelled with their panel id; LED cathode marked"),
            ("back", back, BACK_BIG, "BACK, seen from the back: JLC fits the SMD parts; hand-solder U_SM's sockets and J_PWR")):
        out = []
        A.open_svg(out, int(w + 2 * SHEET_MARGIN), int(h + 2 * SHEET_MARGIN + 60))
        v = A.View(sheet_parts, SHEET_MARGIN, SHEET_MARGIN, SHEET_SCALE,
                   bounds=(X0, Y0, X1, Y1), big=big, style=STYLE)
        v.place_labels()
        if getattr(s, "assembly_overlap", False) and name == "back" and len(v.label_boxes) > 1:
            v.label_boxes[1] = v.label_boxes[0]
        s.sheets[name] = (len(sheet_parts), v.check())
        v.draw(out, moat=False)
        _mark_pin1(out, v, sheet_parts)
        out.append('<text x="%d" y="%d" font-size="14" fill="%s">%s</text>'
                   % (SHEET_MARGIN, int(h + SHEET_MARGIN + 45), A.INK, note))
        A.scale_bar(out, SHEET_MARGIN, int(h + SHEET_MARGIN + 20), SHEET_SCALE)
        out.append("</svg>")
        with open(os.path.join(s.out, "reva-assembly-%s.svg" % name), "w",
                  encoding="utf-8", newline="\n") as fh:
            fh.write("\n".join(out) + "\n")
```

```python
def _mark_pin1(out, v, parts):
    """A filled square on pad 1 of every LED (KiCad's LED_D3.0mm: pad 1 is the
    cathode) and of J_PWR (pin 1, the -12 V stripe)."""
    for p in parts:
        if p.get("cls") != "led" and p.get("ref") != "J_PWR":
            continue
        for pd in p.get("pads", []):
            if str(pd.get("num")) == "1":
                x, y = v.px(pd["x"], pd["y"])
                out.append('<rect x="%.1f" y="%.1f" width="6" height="6" fill="%s"/>'
                           % (x - 3, y - 3, A.ALERT))
```

Before running it, read `gen/assembly.read_board` for the exact keys of a part dict and of its pads (the code above assumes `x`, `y`, `x0`, `x1`, `cls`, `ref`, and pads as dicts with `num`, `x`, `y`; pad positions may be stored relative to the part) and the way the coupon's `write()` closes an SVG. Fix `_mirror`, `_mark_pin1` and the closing line to match what is really there, and say what you changed. Confirm pad 1 is the cathode in `LED_THT.pretty/LED_D3.0mm.kicad_mod` under KiCad's footprint directory. DNP parts come out in the `dnp` class from `read_board` (Task 5).

Call `write_sheets(s)` at the end of `export()` (before the renders).

- [ ] **Step 3: Run until green, and look**

Run: `"$KIPY" hardware/reva/fab.py`
Expected: every step green including `assembly` (front 112, back 101). If `View.check()` reports unlabelled parts or overlaps at `SHEET_SCALE = 4.0`, try `6.0`; if the dense field west of U_SM still fails, add an enlarged window of that field to the back sheet (a second `View` with `region=(x0, y0, x1, y1)` in board mm, as the coupon enlarges its analog corner) and give its parts their labels there. Report what was needed. **Open both SVGs in a browser and describe them** (rule 2): every part named, LED cathodes marked, the back mirrored.

- [ ] **Step 4: The two sabotages by hand**

Run: `"$KIPY" hardware/reva/fab.py --sabotage assembly --out <scratchpad>/sab` and `--sabotage assembly_missing`.
Expected: exit 1, `RED ... assembly`, the WHY phrase on its lines.

- [ ] **Step 5: Commit**

```bash
git add hardware/reva/fab.py hardware/reva/fab_check.py
git commit -m "hw(reva): P4-3 assembly sheets: front by panel id, back mirrored, DNP and pin 1 marked" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 8: `reva_fab_guard`, the renders in docs, the roadmap

**Files:**
- Create: `hardware/reva/test_fab.py`, `docs/hardware/fab/` (via `fab.py --write`)
- Modify: `CMakeLists.txt`, `docs/roadmap.md`, `docs/superpowers/specs/2026-10-02-rev-a-p4-3-fabrication-design.md` (status line)

**Interfaces:**
- Consumes: `fab.main`, `fab.normalise`, `fab.release_blockers`, `fab.FIXED_DATE`, `fab.FIXED_DATE_SP`, `fab_check.STEPS/SABOTAGES/TURNS_RED/WHY`.
- Produces: ctest `reva_fab_guard`.

- [ ] **Step 1: Write the guard**

`hardware/reva/test_fab.py`:

```python
#!/usr/bin/env python3
"""Guard for the Rev A order package (P4-3 spec §6). Plain script; exit code
is the verdict. Re-runs itself under KiCad's Python.

1. Two fab.py runs in separate processes are green and byte-identical
   (renders excluded: PNGs carry renderer noise; board/ is not the package).
2. Every gated step of fab_check has a sabotage and a `_missing` one, and
   every sabotage names its step and its phrase.
3. Every sabotage exits 1, turns its step red, and its phrase is printed on
   that step's own lines.
4. normalise() catches every date form, in any offset (Review Focus 5), and
   leaves coordinates alone.
5. release_blockers(): today it names the known items and BOTTOM_SIGN; with
   empty lists and verified rotations it is empty (Review Focus 2); with only
   BOTTOM_SIGN unverified it names exactly that (Review Focus 1).
6. --release refuses today and leaves its target directory absent."""
import filecmp
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for _p in (HW, HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)
try:
    import pcbnew  # noqa: F401
except ImportError:
    from gen import ksexp
    kipy = os.path.join(ksexp.KICAD_ROOT, "bin", "python.exe")
    sys.exit(subprocess.call([kipy, os.path.abspath(__file__)]))

import fab as F            # noqa: E402
import fab_check as FC     # noqa: E402

FAILS = []
STEP_RE = re.compile(r"^(RED|   ) \d+\. (\S+)")


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        FAILS.append(what)


def run_fab(*args):
    r = subprocess.run([sys.executable, os.path.join(HERE, "fab.py")] + list(args),
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout + r.stderr


def sections(text):
    out, cur = {}, None
    for ln in text.splitlines():
        m = STEP_RE.match(ln)
        if m:
            cur = m.group(2)
            out[cur] = [ln]
        elif cur and ln.startswith("        "):
            out[cur].append(ln)
        else:
            cur = None
    return out


def red_steps(text):
    return {m.group(2) for m in (STEP_RE.match(l) for l in text.splitlines()) if m and m.group(1) == "RED"}


def same_tree(a, b):
    bad = []
    for root, dirs, files in os.walk(a):
        dirs[:] = [d for d in dirs if d != "board"]
        for n in files:
            if n.endswith(".png"):
                continue
            pa = os.path.join(root, n)
            pb = os.path.join(b, os.path.relpath(pa, a))
            if not os.path.exists(pb) or not filecmp.cmp(pa, pb, shallow=False):
                bad.append(os.path.relpath(pa, a))
    return bad


def check_normalise():
    fx, fs = F.FIXED_DATE, F.FIXED_DATE_SP
    cases = [("%TF.CreationDate,2026-10-02T09:34:20+02:00*%", "%TF.CreationDate," + fx + "*%"),
             ("%TF.CreationDate,2026-12-01T23:59:59Z*%", "%TF.CreationDate," + fx + "*%"),
             ("%TF.CreationDate,2026-12-01T08:00:00-05:00*%", "%TF.CreationDate," + fx + "*%"),
             ("G04 #@! TF.CreationDate,2026-10-02T09:34:21+02:00*", "G04 #@! TF.CreationDate," + fx + "*"),
             ("G04 Created by KiCad (PCBNEW 10.0.5) date 2026-10-02 09:34:20*",
              "G04 Created by KiCad (PCBNEW 10.0.5) date " + fs + "*"),
             ("; DRILL file KiCad 10.0.5 date 2026-10-02T09:34:22", "; DRILL file KiCad 10.0.5 date " + fx),
             ("; #@! TF.CreationDate,2026-10-02T09:34:22+02:00", "; #@! TF.CreationDate," + fx),
             ("X2000000Y-9250000D02*", "X2000000Y-9250000D02*")]
    for raw, want in cases:
        check(F.normalise(raw) == want, "normalise %r -> %r" % (raw, F.normalise(raw)))


def check_release_rules():
    today = F.release_blockers()
    check(any("unrouted SONG_A" in b for b in today), "release_blockers names the known items today")
    check("BOTTOM_SIGN unverified" in today, "release_blockers names BOTTOM_SIGN today")
    ok_rot = {"X": {"deg": 0, "verified": "2026-12-01"}}
    check(F.release_blockers({}, {}, ok_rot, {"sign": 1, "verified": "2026-12-01"}) == [],
          "release_blockers is empty once the lists are empty and rotations verified")
    check(F.release_blockers({}, {}, ok_rot, {"sign": 1, "verified": None}) == ["BOTTOM_SIGN unverified"],
          "release_blockers names exactly an unverified BOTTOM_SIGN")


def check_coverage():
    gated = [n for n, _f in FC.STEPS]
    check(bool(gated), "the gated step list was examined (%s)" % ", ".join(gated))
    for n in gated:
        check(FC.TURNS_RED.get(n) == n and n in FC.SABOTAGES, "step %s has a sabotage" % n)
        check(FC.TURNS_RED.get(n + "_missing") == n, "step %s has a %s_missing sabotage" % (n, n))
    for name in sorted(FC.SABOTAGES):
        check(bool(FC.WHY.get(name)) and name in FC.TURNS_RED, "sabotage %s names its step and phrase" % name)


def main():
    root = tempfile.mkdtemp(prefix="reva_fab_guard_")
    try:
        a, b = os.path.join(root, "a"), os.path.join(root, "b")
        rc1, out1 = run_fab("--out", a)
        rc2, _ = run_fab("--out", b)
        check(rc1 == 0, "a fresh fab.py run is green (rc %d)" % rc1)
        if rc1:
            print(out1[-3000:])
        check(rc2 == 0, "the second fab.py run is green (rc %d)" % rc2)
        diff = same_tree(a, b)
        check(not diff, "two runs are byte-identical (differ: %s)" % diff[:5])
        check(set(n for n, _f in FC.STEPS) <= set(sections(out1)), "every step printed its line")
        check_coverage()
        check_normalise()
        check_release_rules()
        for name in sorted(FC.SABOTAGES):
            rc, text = run_fab("--sabotage", name, "--out", os.path.join(root, "sab"))
            step = FC.TURNS_RED[name]
            check(rc == 1 and step in red_steps(text), "sabotage %s turns %s red (red: %s)"
                  % (name, step, sorted(red_steps(text))))
            hits = [l for l in sections(text).get(step, []) if FC.WHY[name] in l]
            check(bool(hits), "sabotage %s prints %r on %s's own lines" % (name, FC.WHY[name], step))
        rel = os.path.join(root, "release")
        rc, text = run_fab("--release", "--out", os.path.join(root, "r"), "--release-dir", rel)
        check(rc == 1 and "not released" in text and not os.path.exists(rel),
              "--release refuses today and writes nothing (rc %d)" % rc)
    finally:
        shutil.rmtree(root, ignore_errors=True)
    print("FAILED: %d" % len(FAILS) if FAILS else "all fab checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it**

Run: `python hardware/reva/test_fab.py`
Expected: "all fab checks passed". Note the wall time.

- [ ] **Step 3: Prove the determinism gate can go red**

Copy `fab.py` to the scratchpad; in `normalise()` comment out the first `_DATE_RES` entry with Edit; delete `hardware/reva/__pycache__`; run the guard. Expected: FAIL "two runs are byte-identical" (the Gerbers differ in their `%TF.CreationDate`) and FAIL on the first normalise case. Restore from the scratchpad copy; green again.

- [ ] **Step 4: Register**

`CMakeLists.txt`, after `reva_route_guard`:

```cmake
# The Rev A order package (P4-3): Gerbers, drill, CPL, BOM, assembly sheets,
# every check proven red, two exports byte-identical.
add_test(NAME reva_fab_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_fab.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(reva_fab_guard PROPERTIES TIMEOUT 1800)
```

(Match the way `reva_route_guard` sets its TIMEOUT.)

- [ ] **Step 5: Renders and sheets into docs, and look at them**

Run: `"$KIPY" hardware/reva/fab.py --write`
Expected: GREEN; `docs/hardware/fab/` holds `reva-top.png`, `reva-bottom.png`, `reva-assembly-front.svg`, `reva-assembly-back.svg`. Look at all four and describe them in the report.

- [ ] **Step 6: Roadmap and spec status**

`docs/roadmap.md`: a new entry above "2026-10-01 — P4-2 routing", headed "**2026-MM-DD — P4-3 fabrication data: the pipeline stands; the package waits for the panel pass.**" Every number in it comes from this branch's runs (memory: prose drifts from data twice as fast as code): parity items (0), the `paths` count, hidden references (`NO_ROOM` size), the CPL rows, the PTH/NPTH counts, the package names in `ROT_FIX`, the guard wall times. Say what is still open: `ROT_FIX`/`BOTTOM_SIGN` wait for a JLC quote upload; the release waits for the panel pass; the board-size/bottom-assembly assumption is unconfirmed. Spec header: `**Status:** approved (Bastian, 2026-10-02); implemented on branch reva-p4-3-fab`.

- [ ] **Step 7: Commit, then the full suite**

```bash
git add hardware/reva/test_fab.py CMakeLists.txt docs/hardware/fab docs/roadmap.md docs/superpowers/specs/2026-10-02-rev-a-p4-3-fabrication-design.md
git commit -m "hw(reva): P4-3 reva_fab_guard; renders and assembly sheets in docs; roadmap" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

Run: `source env.sh` then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `ctest --test-dir build -R "hw_gen|reva|coupon" --output-on-failure`
Expected: all pass, `hw_gen_assembly_guard` and `reva_fab_guard` among them. Paste the summary line. Do not merge or push.

**Note for the report to Bastian:** after this branch is merged, the main checkout's four "modified" files (`bom-hand.csv`, `bom-jlc.csv`, `review.md`, `kicad/fp-lib-table`) keep showing as modified until their index entries are refreshed once, because the index still carries the CRLF size from the old checkout. `git add` on them stages no content (the blobs are identical) and clears it; so does a fresh checkout of the four paths. That is Bastian's call in his checkout, not this branch's.
