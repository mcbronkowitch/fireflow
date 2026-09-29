# Rev A P4a — routing spike Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Route the SENSE_1 strip of Rev A twice — with a grid router of our own and with Freerouting — on identical input under one proof chain, route the winner again on four layers, look up JLC's 2- vs 4-layer price, and write a recommendation for P4.

**Architecture:** `hardware/gen/` gains the board-side tools both boards share: `kipcb.py` (moved from the coupon), `pcb_proof.py` (DRC, ratsnest, render and segment geometry, extracted from the coupon's `build_pcb.py`) and `route.py` (a pcbnew-free grid router). `hardware/reva/spike/` holds the throwaway harness: `stripe.py` builds the strip board from the P3 netlist and the P1 hole list, `locked.py` carries the hand-routed nets, `proof.py` is the chain with sabotage modes, `own.py` and `freerouting.py` are the two methods, `run.py` ties them together. The report is `docs/hardware/routing-spike.md`.

**Tech Stack:** KiCad 10.0.5 (`pcbnew` under KiCad's Python 3.11 at `C:/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe`, `kicad-cli`), system Python 3.14 for ctest (`route.py` core and its test run there), Freerouting 2.x with a Java runtime (Task 4 installs both outside the repo).

**Spec:** [`docs/superpowers/specs/2026-09-29-rev-a-p4a-routing-spike-design.md`](../specs/2026-09-29-rev-a-p4a-routing-spike-design.md)

## Global Constraints

- Never prefix a shell command with `cd`; use absolute or repo-relative paths. No shell writes (no `>`, `tee`, `sed -i`): files are written with the Write/Edit tools. Never chain `git add` and `git commit`.
- Everything written into the repo is English.
- Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Branch `reva-p4a-routing-spike`. Nothing merges to `main` without Bastian.
- `KIPY` below means `"/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe"` (Git Bash). Everything that imports `pcbnew` runs under it; `hardware/gen/route.py` and `hardware/gen/test_route.py` must NOT import `pcbnew` and run under the system `python`.
- Nothing in `hardware/gen/__init__.py` imports `kipcb` or `pcb_proof`: the system-Python tools in `hardware/gen/` must keep importing without `pcbnew`.
- The coupon's committed artefacts (`hardware/coupon/coupon.kicad_pcb`, `coupon.kicad_prl`, `fab/`, `proof/`) are regenerated only by the Task 1 guard, and only from a tree where `git status --short hardware/coupon` printed nothing; afterwards `git restore hardware/coupon` puts back the timestamp churn. `coupon.kicad_pcb` must be byte-identical to `HEAD` (`git hash-object` equals `git rev-parse HEAD:hardware/coupon/coupon.kicad_pcb`).
- A RED-proof sabotage in a source file is reverted with the Edit tool, never with `git checkout`.
- Net names come from `hardware/reva/blocks.py` (`BL.SENSE`, `BL.GND`, `BL.SM3V3`, `BL.MODULE_PINS`, `BL.led_net`, `BL.MUX_S`, `BL.MUX_EN`), never retyped as string literals in a checker.
- `pcbnew.KIID.SeedGenerator()` runs once per process, inside `kipcb.new_board()`. Any scratch `pcbnew.BOARD()` is created **before** `new_board()` so the real board's UUIDs stay reproducible.
- Pads of a flipped footprint: test the layer with `pad.IsOnLayer(pcbnew.B_Cu)`. `pad.GetLayerName()` reports `F.Cu` for a flipped SMD pad (probed) and must not be used.
- Remove board items with `board.Delete(item)`, never `board.Remove(item)` (coupon lesson: a dangling wrapper corrupts the next save).
- Outputs go to `hardware/reva/spike/out/` (gitignored). Only the PNGs the report cites are committed, under `docs/hardware/routing-spike/`.
- Every render that comes out of an iteration with a result is shown to Bastian (the controller sends it with SendUserFile) before the next iteration starts — master plan working rule 2.
- Numbers in the report, comments and commit messages are pasted from a command's output, with the command beside them — never typed from memory.
- Downloads (Java runtime, Freerouting) happen only in the controller session after Bastian says yes to the exact file names and sizes. Freerouting always runs with `-da` (its anonymous analytics off).

## Facts measured for this plan (2026-09-29)

- `KIPY hardware/coupon/scripts/build_pcb.py` on a clean tree: all 10 steps green in 12.8 s; `coupon.kicad_pcb` blob `9f7b8226245deb3c758f5edfaf8418e8a83097c9`, identical to `HEAD`. It rewrites 33 other committed files (gerbers, drill, PNGs, both DRC reports, `coupon.kicad_prl`) with timestamp-only churn.
- `build.project()` from `hardware/reva/build.py` imports and runs under `KIPY` (Python 3.11.5): 220 parts in 4.1 s. Every footprint loads through `kipcb.footprint()` except `Thonk:SW_Push_LP_Button` (vendored in `hardware/lib/Thonk/Thonk.pretty`, not on kipcb's search path yet) and the SD socket (`"P4"`, not in the strip).
- Footprint pads versus symbol pins: pot pads `1 2 3` plus two unnamed mounting-tab pads (`""`, no net); jack pads `S T TN` (`TN` no-connect); key pads `1..6` (`3..6` no-connect); LED `1 2`; SOIC-16 `1..16`; 0603 `1 2`.
- Reference points, footprint-local at 0°: RD901F shaft = F.Fab circle at (7.50, 2.50) from pad 1; PJ398SM hole = F.Fab circle at (0.00, 6.48); LED_D3.0mm = F.Fab circle at (1.27, 0.00); `SW_Push_LP_Button` has no F.Fab circle (courtyard centre (0, 0)).
- Pot at 90°, anchored at (100, 50): shaft (102.5, 42.5), pads 1/2/3 at y 50.0 — pins **south** of the shaft; tabs at x ±4.8 from the shaft; courtyard 12.91 × 13.84 mm.
- SOIC-16 flipped to B at (120, 60): `B_CrtYd` box valid and follows `SetPosition` and `SetOrientationDegrees`; `F_CrtYd` box is empty. Pads are on B.Cu (`IsOnLayer`), persisted across save/reload.
- `pcbnew.ExportSpecctraDSN(board, path)` and `pcbnew.ImportSpecctraSES(board, path)` exist. Locked tracks export as `(wire ... (type fix))`. Net classes set via `board.GetDesignSettings().m_NetSettings` (`SetNetclass`, `SetNetclassPatternAssignment`, then `board.SynchronizeNetsAndNetClasses(True)`) export as `(class <name> <nets> ... (rule (width 400) (clearance 200)))`; `SaveBoard` writes a `.kicad_pro` beside the board. A rule area (`SetIsRuleArea(True)`, no tracks, no vias, F.Cu+B.Cu) exports as one `(keepout "" (polygon <layer> ...))` per layer.
- **Pot orientation (dry run of Task 2's code, scratchpad):** pins south (90°) — the first draft — put five strip LEDs onto pot pins: 26 `shorting_items`, 4 `clearance`, 2 `hole_to_hole` on the unrouted board (e.g. D15 on RV53, D14 on RV54). Pins north (270°): placement-stage proof green in 2.4 s. 0° fails the own-pots check (RES_B's pads cross the cut), 180° leaves C15 no place within 2 mm of its VCC pin. The plan uses 270°; with it the top row's pins sit at y ≈ 7.0 mm (15 `copper_edge_clearance`, ungated) — a rail-zone question for P4.
- **A track that overlaps a pad of another net is renamed to that pad's net when the board is saved and reloaded** — even a track that only crosses the pad (probed: a net-A track across a net-B pad came back as net B; DRC reported `track_dangling` + `unconnected_items`, no `shorting_items`). A track 0.1 mm beside the pad stays net A and DRC reports `clearance`. So: sabotages use near misses, and `track_dangling` is gated.
- kicad-cli overwrites an existing DRC report whenever it runs; a stale report survives only when kicad-cli writes nothing (probed). The stale-report guard is tested on a missing board.
- The `route.py` and `test_route.py` code in Task 5, and `pcb_proof.py` and `test_pcb_proof.py` in Task 1, were extracted from this plan and run while planning: both guards green (route 14 checks, pcb_proof 8), and each planned sabotage turns its guard red with the message quoted in the task. A first draft of `test_route.py` had a clearance check that could examine zero segment pairs; `test_side_by_side` exists because of that. Task 2's `stripe.py`, `proof.py` and `run.py` (with Task 1's and Task 2's `kipcb` changes applied to a scratch copy) were dry-run the same way: placement-stage proof green, and each of the seven Task 2 sabotages turns exactly its step red. Tasks 3 and 6–8 were **not** dry-run: they depend on hand-routed data, the router's behaviour on the real strip and Freerouting.
- Freerouting's documented CLI (github.com/freerouting/freerouting `docs/command_line_arguments.md`, read 2026-09-29): `-de <in.dsn>`, `-do <out.ses>`, `-mp <passes>`, `-mt <threads>` (0 disables optimization), `-inc <classes>` (net classes to skip), `--gui.enabled=false`, `-da` (analytics off). Latest release listed: v2.4.1. Not installed here: no `java` on PATH.
- SENSE_1 pots (from `panel-map.json`): 22 on U_MUX3 (7), U_MUX4 (8), U_MUX5 (7); group centroids (227.6, 33.5), (272.9, 31.4), (256.4, 85.6). In `build.project()`, sheet `mux_sense_1` lists `U_MUX3, C14, U_MUX4, C15, U_MUX5, C16, RV49..RV70` — each mux followed by its 100 nF. Keys carry `panel_id` (`SW3` = `MODBTN`).

## File map

| File | Status | Responsibility |
|---|---|---|
| `hardware/gen/kipcb.py` | moved from `hardware/coupon/scripts/kipcb.py`, extended | pcbnew wrapper: board, parts, tracks, vias, zones, keepouts, net classes |
| `hardware/gen/pcb_proof.py` | new (extracted) | DRC run and parse, ratsnest, render, segment distances |
| `hardware/gen/test_pcb_proof.py` | new | guard for `pcb_proof` (re-runs itself under KIPY) |
| `hardware/gen/route.py` | new | grid router core, no pcbnew |
| `hardware/gen/test_route.py` | new | guard for `route.py` (system Python) |
| `hardware/coupon/scripts/build_pcb.py`, `check_layout.py`, `review.py` | modified | import the moved tools; behaviour unchanged |
| `hardware/reva/spike/stripe.py` | new | the strip board: outline, panel parts, keepouts, SMD placement, ports, net classes |
| `hardware/reva/spike/locked.py` | new | hand-routed SENSE_1, OUT_L, OUT_R as data |
| `hardware/reva/spike/proof.py` | new | the spike's proof chain with sabotage modes |
| `hardware/reva/spike/own.py` | new | board ↔ `route.py` adapter |
| `hardware/reva/spike/freerouting.py` | new | DSN → Freerouting → SES |
| `hardware/reva/spike/run.py` | new | CLI: `--method none|own|freerouting --layers 2|4 [--sabotage RULE]` |
| `hardware/reva/spike/README.md` | new | how to run, what is throwaway |
| `docs/hardware/routing-spike.md` | new | measurement log, then the report |
| `.gitignore`, `CMakeLists.txt` | modified | `spike/out/`; two ctest entries |

---
### Task 1: Shared board tools — `kipcb` and `pcb_proof` into `hardware/gen/`, coupon rewired

Master plan working rule 6: the coupon's tools move to a shared place; they are not copied. The coupon proves the move by rebuilding byte-identical with identical step output.

**Files:**
- Move: `hardware/coupon/scripts/kipcb.py` → `hardware/gen/kipcb.py`
- Create: `hardware/gen/pcb_proof.py`, `hardware/gen/test_pcb_proof.py`
- Modify: `hardware/coupon/scripts/build_pcb.py` (imports, lines 13–18; `count_drc_violations` at 569–582; the DRC calls in `check_courtyards` 299–309 and `check_final_drc` 601–609), `hardware/coupon/scripts/check_layout.py` (import at 45–48; `_seg_point_dist`, `_seg_seg_dist`, `_track_segments` at 138–232), `hardware/coupon/README.md:97`, `CMakeLists.txt` (after the `hw_gen_bom_guard` entry)

**Interfaces:**
- Produces `gen.kipcb` — the unchanged public API of the coupon's `kipcb` (`new_board`, `footprint`, `add_part`, `add_zone`, `fill_zones`, `add_track`, `add_via`, `save`, `load`, `courtyard_boxes`, `board_nets`, `_pt`, `_net`, `KIPY`, `LAYER`).
- Produces `gen.pcb_proof`:
  - `drc(pcb_path: str, rpt_path: str) -> dict[str, int]` — runs `kicad-cli pcb drc` at error+warning severity, deletes a stale report first, raises `RuntimeError` if no report appears, returns `{violation class: count}`.
  - `count_drc_violations(rpt_path: str) -> dict[str, int]`
  - `unconnected_by_net(rpt_path: str) -> dict[str, set[str]]` — `{net: {"REF.PAD", ...}}` from the `[unconnected_items]` blocks.
  - `live_unconnected(board) -> int` — `GetUnconnectedCount(True)` after `BuildConnectivity()`.
  - `render(pcb_path: str, png_path: str, side: str) -> tuple[int, str]` — `side` is `"top"` or `"bottom"`; returns `(rc, output)`.
  - `seg_point_dist(p, a, b) -> float`, `seg_seg_dist(s1, s2) -> float` (segments as `((x1, y1), (x2, y2))`, mm).
  - `track_segments(board, net_pred) -> list[tuple[str, str, tuple, tuple]]` — `(net, layer_name, (x1, y1), (x2, y2))` for every `PCB_TRACE_T` whose net passes `net_pred`.

  Note: the coupon's `_track_segments` returns 3-tuples `(net, start, end)`; `check_layout.py` keeps its own 3-tuple shape by wrapping (step 6).

- [ ] **Step 1: Probe that the coupon build is deterministic in its output, not only its board**

`git status --short hardware/coupon` must print nothing. Then run twice and keep both logs in the scratchpad (`$S` = your scratchpad directory):

Run: `KIPY hardware/coupon/scripts/build_pcb.py` — save the full stdout with the Write tool as `$S/coupon-before-1.txt`; run again → `$S/coupon-before-2.txt`.
Run: `git diff --no-index --stat "$S/coupon-before-1.txt" "$S/coupon-before-2.txt"`
Expected: no output (identical). If the two logs differ, record which lines differ; those lines are excluded from the step-9 comparison, and the report's measurement log names them.
Run: `git hash-object hardware/coupon/coupon.kicad_pcb` and `git rev-parse HEAD:hardware/coupon/coupon.kicad_pcb`
Expected: both `9f7b8226245deb3c758f5edfaf8418e8a83097c9`.
Then: `git restore hardware/coupon` and `git status --short hardware/coupon` → nothing.

- [ ] **Step 2: Move `kipcb.py`**

Run: `git mv hardware/coupon/scripts/kipcb.py hardware/gen/kipcb.py`

In `hardware/gen/kipcb.py`, change the module docstring's first line to
`"""pcbnew wrapper shared by the generated boards (coupon, Rev A). No board knowledge here.`
and replace the `FP_VENDORED = ...` assignment with:

```python
FP_LIB = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
FP_VENDORED = os.path.join(FP_LIB, "DaisyKiCad")
FP_THONK = os.path.join(FP_LIB, "Thonk")
```

and in `footprint()` the search tuple `(FP_SHARE, FP_VENDORED)` with `(FP_SHARE, FP_VENDORED, FP_THONK)`. Update the docstring of `footprint()` to say "or the vendored `hardware/lib/DaisyKiCad` and `hardware/lib/Thonk` `*.pretty`".

- [ ] **Step 3: Write `hardware/gen/pcb_proof.py`**

```python
#!/usr/bin/env python3
"""Proof steps shared by the generated boards (coupon, Rev A routing spike).

Runs under KiCad's Python only (it imports pcbnew). Extracted from the
coupon's build_pcb.py and check_layout.py, which import it from here; the
docstrings there keep the probe history of each choice (why kicad-cli's
report is parsed instead of pcbnew's connectivity API, why a stale report
is deleted first, why GetUnconnectedCount(True)).
"""
import math
import os
import re
import subprocess

import pcbnew

from gen import ksexp

_CLASS_RE = re.compile(r"^\[([a-z0-9_]+)\]", re.M)
_PAD_RE = re.compile(r"(?:PTH pad|Pad) (\S+) \[([^\]]+)\] of (\S+)")


def _mm(vec):
    return pcbnew.ToMM(vec.x), pcbnew.ToMM(vec.y)


def count_drc_violations(rpt_path):
    """Parse a `kicad-cli pcb drc` report into {violation class: count}."""
    txt = open(rpt_path, encoding="utf-8", errors="replace").read()
    counts = {}
    for kind in _CLASS_RE.findall(txt):
        counts[kind] = counts.get(kind, 0) + 1
    return counts


def drc(pcb_path, rpt_path):
    """kicad-cli DRC at error and warning severity into `rpt_path`.

    The report is deleted first and its absence afterwards is the failure:
    a stale report from an earlier run must never be read as this run's
    verdict. The return code cannot stand in for it, since
    --exit-code-violations makes a nonzero rc the normal outcome.
    """
    if os.path.exists(rpt_path):
        os.remove(rpt_path)
    r = subprocess.run([ksexp.KICAD_CLI, "pcb", "drc", "--exit-code-violations",
                        "--severity-error", "--severity-warning",
                        "-o", rpt_path, pcb_path],
                       capture_output=True, text=True)
    if not os.path.exists(rpt_path):
        raise RuntimeError("kicad-cli pcb drc wrote no report (rc=%d)\n%s"
                           % (r.returncode, (r.stdout + r.stderr).strip()))
    return count_drc_violations(rpt_path)


def unconnected_by_net(rpt_path):
    """{net: {"REF.PAD", ...}} for every pad named in an [unconnected_items]
    block. The header line names neither net nor pad; the two pads are on
    the lines under it."""
    txt = open(rpt_path, encoding="utf-8", errors="replace").read()
    by_net = {}
    for block in re.split(r"(?=^\[)", txt, flags=re.M):
        if not block.startswith("[unconnected_items]"):
            continue
        for padnum, net, ref in _PAD_RE.findall(block):
            by_net.setdefault(net, set()).add("%s.%s" % (ref, padnum))
    return by_net


def live_unconnected(board):
    """Unconnected pairs on the live board; agrees with kicad-cli's
    [unconnected_items] count (probed on the coupon, 10.0.5)."""
    board.BuildConnectivity()
    return board.GetConnectivity().GetUnconnectedCount(True)


def render(pcb_path, png_path, side):
    """`kicad-cli pcb render` of one side ("top" or "bottom")."""
    r = subprocess.run([ksexp.KICAD_CLI, "pcb", "render", "--side", side,
                        "-o", png_path, pcb_path], capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()


def seg_point_dist(p, a, b):
    ax, ay = a
    bx, by = b
    px, py = p
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)
    t = max(0.0, min(1.0, t))
    cx, cy = ax + t * dx, ay + t * dy
    return math.hypot(px - cx, py - cy)


def seg_seg_dist(s1, s2):
    """Closest approach between two segments. The min over the four
    endpoint-to-opposite-segment distances is not exact for a crossing pair;
    different-net copper never crosses on a DRC-clean board, so it is exact
    for everything a proof asks."""
    a, b = s1
    c, d = s2
    return min(seg_point_dist(a, c, d), seg_point_dist(b, c, d),
               seg_point_dist(c, a, b), seg_point_dist(d, a, b))


def track_segments(board, net_pred):
    """[(net, layer_name, (x1, y1), (x2, y2))] for every PCB_TRACE_T whose
    net passes `net_pred`. Vias and arcs are excluded."""
    out = []
    for t in board.GetTracks():
        if t.Type() != pcbnew.PCB_TRACE_T:
            continue
        net = t.GetNetname()
        if not net_pred(net):
            continue
        out.append((net, t.GetLayerName(), _mm(t.GetStart()), _mm(t.GetEnd())))
    return out
```

- [ ] **Step 4: Write the failing guard `hardware/gen/test_pcb_proof.py`**

```python
#!/usr/bin/env python3
"""Guard for hardware/gen/pcb_proof.py. Plain script; the exit code is the
verdict. Needs pcbnew, so under the system Python (ctest) it re-runs itself
under KiCad's Python."""
import os
import subprocess
import sys
import tempfile

HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, HW)
try:
    import pcbnew  # noqa: F401
except ImportError:
    from gen import ksexp
    kipy = os.path.join(ksexp.KICAD_ROOT, "bin", "python.exe")
    sys.exit(subprocess.call([kipy, os.path.abspath(__file__)]))

from gen import kipcb            # noqa: E402
from gen import pcb_proof as PP  # noqa: E402

FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def board_with_two_resistors():
    """R1 at (5, 5), R2 at (15, 5); pad 1 of both on net A, pad 2 on net B.
    Net A is routed around the pads at y 7; net B is left unrouted."""
    b = kipcb.new_board(20, 10, 2)
    for ref, x in (("R1", 5.0), ("R2", 15.0)):
        fp = kipcb.footprint("Resistor_SMD:R_0603_1608Metric")
        fp.SetReference(ref)
        fp.SetPosition(kipcb._pt(x, 5.0))
        b.Add(fp)
        for pad in fp.Pads():
            pad.SetNet(kipcb._net(b, {"1": "A", "2": "B"}[pad.GetNumber()]))
    kipcb.add_track(b, "F.Cu", 0.25, "A",
                    [(4.2, 5.0), (4.2, 7.0), (14.2, 7.0), (14.2, 5.0)])
    return b


def main():
    tmp = tempfile.mkdtemp(prefix="pcb_proof_")
    check(abs(PP.seg_point_dist((0, 1), (0, 0), (2, 0)) - 1.0) < 1e-9,
          "seg_point_dist: point 1 mm above a segment")
    check(abs(PP.seg_seg_dist(((0, 0), (2, 0)), ((0, 3), (2, 3))) - 3.0) < 1e-9,
          "seg_seg_dist: parallel segments 3 mm apart")

    b = board_with_two_resistors()
    clean = os.path.join(tmp, "clean.kicad_pcb")
    kipcb.save(b, clean)
    check(PP.live_unconnected(b) == 1, "live_unconnected: net B's one pair")

    rpt = os.path.join(tmp, "clean.rpt")
    counts = PP.drc(clean, rpt)
    check(not counts.get("shorting_items") and not counts.get("clearance"),
          "drc: the clean board has no short and no clearance violation (%s)" % counts)
    by_net = PP.unconnected_by_net(rpt)
    check(by_net == {"B": {"R1.2", "R2.2"}},
          "unconnected_by_net: only net B, both pads (%s)" % by_net)
    segs = PP.track_segments(b, lambda n: n == "A")
    check(len(segs) == 3 and all(s[1] == "F.Cu" for s in segs),
          "track_segments: three F.Cu segments on net A")

    # A near miss, not a touch: a track that overlaps a foreign pad is renamed
    # to the pad's net on save (probed 2026-09-29), so a "short" drawn onto a
    # pad reaches the DRC as a dangling track, never as shorting_items.
    kipcb.add_track(b, "F.Cu", 0.25, "A", [(6.445, 3.0), (6.445, 6.5)])   # 0.1 mm beside R1.2
    near = os.path.join(tmp, "near.kicad_pcb")
    kipcb.save(b, near)
    counts = PP.drc(near, os.path.join(tmp, "near.rpt"))
    check(counts.get("clearance", 0) > 0,
          "drc: a net-A track 0.1 mm from a net-B pad is reported (%s)" % counts)

    # The stale report matters only when kicad-cli writes nothing: kicad-cli
    # overwrites an existing report whenever it runs (probed 2026-09-29).
    stale = os.path.join(tmp, "missing.rpt")
    with open(stale, "w", encoding="utf-8") as fh:
        fh.write("[stale_marker]: left behind by an earlier run\n")
    try:
        got = PP.drc(os.path.join(tmp, "missing.kicad_pcb"), stale)
        check(False, "drc: a missing board raises, even over a stale report (got %s)" % got)
    except RuntimeError:
        check(True, "drc: a missing board raises, even over a stale report")

    print("FAILED: %d" % len(FAILS) if FAILS else "all pcb_proof checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
```

(`pcb_proof.py` already exists from step 3 — it is extracted code, not new behaviour — so this guard's RED is proven by sabotage in step 5, not by a missing module.)

- [ ] **Step 5: Run it green, then prove a RED**

Run: `python hardware/gen/test_pcb_proof.py`
Expected: every line `ok`, last line `all pcb_proof checks passed`, exit 0. Paste the `drc:` count dicts it printed into your report.

RED: in `pcb_proof.drc`, comment out the two lines `if os.path.exists(rpt_path):` / `os.remove(rpt_path)` with the Edit tool.
Run: `python hardware/gen/test_pcb_proof.py`
Expected: `FAIL drc: a missing board raises, even over a stale report (got {'stale_marker': 1})`, exit 1. Revert with the Edit tool; rerun → exit 0.

- [ ] **Step 6: Rewire the coupon**

`hardware/coupon/scripts/build_pcb.py` — replace line 17 `import kipcb` with
```python
from gen import kipcb  # noqa: E402  (moved to hardware/gen, P4a)
from gen import pcb_proof as PP  # noqa: E402
```
(`hardware/` is already on `sys.path` from lines 11–12; keep the existing `import pcbnew`).
Replace the body of `count_drc_violations` (keep its docstring) with `return PP.count_drc_violations(rpt_path)` — `review.py` calls `BP.count_drc_violations`.
In `check_courtyards`, replace the block from `if os.path.exists(rpt):` through the `fail("kicad-cli pcb drc wrote no report ...")` with
```python
    try:
        PP.drc(pcb_path, rpt)
    except RuntimeError as e:
        fail(str(e))
```
In `check_final_drc`, replace the `if os.path.exists(rpt): os.remove(rpt)`, the `subprocess.run(...)` call and the `if not os.path.exists(rpt): fail(...)` with
```python
    try:
        PP.drc(pcb_path, rpt)
    except RuntimeError as e:
        fail(str(e))
```
Leave both docstrings; add one sentence to each: "The DRC run itself is `gen.pcb_proof.drc()`."

`hardware/coupon/scripts/check_layout.py` — after its `sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))` add
```python
sys.path.insert(0, os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..")))
```
replace `import kipcb` with `from gen import kipcb` and add `from gen import pcb_proof as PP`. Replace the bodies of `_seg_point_dist` and `_seg_seg_dist` with `return PP.seg_point_dist(p, a, b)` / `return PP.seg_seg_dist(s1, s2)` (keep the `_seg_seg_dist` docstring), and the body of `_track_segments` with
```python
    return [(net, a, b) for net, _layer, a, b in PP.track_segments(board, net_pred)]
```

`hardware/coupon/README.md:97` — `kipcb.new_board()` → `` `hardware/gen/kipcb.py`'s `new_board()` ``.

Run: `grep -rn "import kipcb\|scripts/kipcb" hardware docs/hardware --include=*.py --include=*.md`
Expected: no hit outside `docs/superpowers/` history.

- [ ] **Step 7: The coupon guard — byte-identical board, identical step output**

`git status --short hardware/coupon` → only your edits to `scripts/` and `README.md`.
Run: `KIPY hardware/coupon/scripts/build_pcb.py` — save stdout as `$S/coupon-after.txt`.
Run: `git diff --no-index "$S/coupon-before-1.txt" "$S/coupon-after.txt"`
Expected: no output (or only the lines step 1 found varying on their own).
Run: `git hash-object hardware/coupon/coupon.kicad_pcb`
Expected: `9f7b8226245deb3c758f5edfaf8418e8a83097c9`.
Run: `KIPY -c "import sys; sys.path[:0]=['hardware/coupon/scripts','hardware']; import review, check_layout, assembly_plan; print('imports ok')"`
Expected: `imports ok`.
Then `git restore hardware/coupon/coupon.kicad_prl hardware/coupon/fab hardware/coupon/proof` and confirm `git status --short hardware/coupon` shows only `scripts/` and `README.md`.

- [ ] **Step 8: ctest entry**

In `CMakeLists.txt`, after the `hw_gen_bom_guard` block:
```cmake
# Rev A P4a: the board-side proof tools shared by coupon and Rev A. The
# script re-runs itself under KiCad's Python (it needs pcbnew).
add_test(NAME hw_gen_pcb_proof_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_pcb_proof.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```
Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `ctest --test-dir build -R "hw_gen|reva|coupon" --output-on-failure`
Expected: all listed tests pass, `hw_gen_pcb_proof_guard` among them.

- [ ] **Step 9: Commit**

```bash
git add hardware/gen/kipcb.py hardware/gen/pcb_proof.py hardware/gen/test_pcb_proof.py hardware/coupon/scripts/build_pcb.py hardware/coupon/scripts/check_layout.py hardware/coupon/README.md CMakeLists.txt
```
```bash
git commit -m "hw(gen): kipcb and pcb_proof shared under hardware/gen; coupon rebuilds byte-identical"
```
(with the HAL 9000 trailer; `git status` must show the `git mv` deletion of `hardware/coupon/scripts/kipcb.py` as staged.)

---

### Task 2: The strip board — placement, keepouts, ports, net classes — and the placement-stage proof

Spec §2. Builds the unrouted strip from the P3 netlist and the P1 hole list, proves what can be proven before routing, renders it, and shows Bastian the picture.

**Files:**
- Modify: `hardware/gen/kipcb.py` (`new_board` gains `origin`; `courtyard_boxes` reads `B_CrtYd` for flipped parts; new `add_keepout`, `set_netclasses`, `lock_tracks`)
- Create: `hardware/reva/spike/stripe.py`, `hardware/reva/spike/proof.py`, `hardware/reva/spike/run.py`, `hardware/reva/spike/README.md`, `docs/hardware/routing-spike.md`
- Modify: `.gitignore` (after the `hardware/reva/out/` line)

**Interfaces:**
- Consumes: `gen.kipcb`, `gen.pcb_proof` (Task 1).
- Produces `gen.kipcb` additions:
  - `new_board(width_mm, height_mm, copper_layers, origin=(0.0, 0.0))` — outline from `origin` to `origin + (w, h)`; the coupon's calls are unchanged.
  - `add_keepout(board, points_mm, layer_names) -> ZONE` — rule area: no tracks, no vias, no zone fill; pads allowed.
  - `set_netclasses(board, default_width_mm, classes, clearance_mm=0.2, via_mm=0.6, drill_mm=0.3)` — `classes = {name: (width_mm, [net, ...])}`.
  - `lock_tracks(board, net_names) -> int` — locks every track and via on those nets, returns the count.
- Produces `stripe`:
  - constants `SENSE, OUT_L, OUT_R, LOCKED_NETS, SUPPLY_NETS, X0, Y0, X1, Y1, CLEARANCE, W_SIGNAL, W_SUPPLY, DECOUPLE_MAX_MM, COPPER`
  - `class Strip` with attributes `board, layers, parts` (every `Part` placed, ports included), `holes` (`{ref: (x, y)}`), `keepouts` (`[(label, (l, t, r, b))]`), `ports` (`{net: ref}`), `decouplers` (`{cap_ref: mux_ref}`), `vcc_pin` (`{mux_ref: pad}`), `led_nets` (`set[str]`), `locked` (`[]` until Task 3)
  - `build(layers: int) -> Strip`
  - `intent(strip) -> dict[str, set[tuple[str, str]]]` — `{net: {(ref, pad), ...}}`
  - `hole_point(fp) -> (x, y)`
- Produces `proof`:
  - `GATED = ("shorting_items", "clearance", "hole_clearance", "hole_to_hole", "tracks_crossing", "track_dangling")`
  - `run(strip, pcb_path, prefix, routed: bool) -> bool` — every step runs and prints; returns `True` only if all gated steps are green.
  - `SABOTAGES: dict[str, callable(strip)]`, `sabotage(strip, name)`
- Produces `run.py` CLI: `KIPY hardware/reva/spike/run.py --method none --layers 2 [--sabotage NAME]`; writes `hardware/reva/spike/out/<method>-<layers>L.kicad_pcb`, `-drc.rpt`, `-top.png`, `-bottom.png`.

- [ ] **Step 1: Extend `hardware/gen/kipcb.py`**

`new_board`: add the parameter `origin=(0.0, 0.0)` and replace the `corners = [...]` line with
```python
    ox, oy = origin
    corners = [(ox, oy), (ox + width_mm, oy),
               (ox + width_mm, oy + height_mm), (ox, oy + height_mm)]
```
Docstring: "rectangular Edge.Cuts outline from `origin` to `origin + (w, h)`".

`courtyard_boxes`: replace `bb = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()` with
```python
        layer = pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd
        bb = fp.GetCourtyard(layer).BBox()
```
and add to its docstring: "A part flipped to the back has its courtyard on B.CrtYd and an empty F.CrtYd box (probed, 10.0.5)."

Append:
```python
def add_keepout(board, points_mm, layer_names):
    """Rule area forbidding tracks, vias and zone fill on `layer_names`;
    pads and footprints stay allowed. KiCad's DSN export writes it as one
    `(keepout ...)` per copper layer (probed, 10.0.5)."""
    zone = pcbnew.ZONE(board)
    zone.SetIsRuleArea(True)
    zone.SetDoNotAllowTracks(True)
    zone.SetDoNotAllowVias(True)
    zone.SetDoNotAllowZoneFills(True)
    zone.SetDoNotAllowPads(False)
    zone.SetDoNotAllowFootprints(False)
    layers = pcbnew.LSET()
    for name in layer_names:
        layers.AddLayer(LAYER[name])
    zone.SetLayerSet(layers)
    chain = pcbnew.SHAPE_LINE_CHAIN()
    for x_mm, y_mm in points_mm:
        chain.Append(_pt(x_mm, y_mm))
    chain.SetClosed(True)
    zone.Outline().AddOutline(chain)
    board.Add(zone)
    return zone


def set_netclasses(board, default_width_mm, classes, clearance_mm=0.2,
                   via_mm=0.6, drill_mm=0.3):
    """The Default class plus `classes = {name: (width_mm, [net, ...])}`.
    Call after every net exists. Probed (10.0.5): the classes reach the DSN
    export as `(class <name> <nets> ... (rule (width ..) (clearance ..)))`,
    and SaveBoard writes them into a .kicad_pro beside the board."""
    ns = board.GetDesignSettings().m_NetSettings

    def fill(nc, width_mm):
        nc.SetTrackWidth(pcbnew.FromMM(width_mm))
        nc.SetClearance(pcbnew.FromMM(clearance_mm))
        nc.SetViaDiameter(pcbnew.FromMM(via_mm))
        nc.SetViaDrill(pcbnew.FromMM(drill_mm))

    fill(ns.GetDefaultNetclass(), default_width_mm)
    for name, (width_mm, nets) in sorted(classes.items()):
        nc = pcbnew.NETCLASS(name)
        fill(nc, width_mm)
        ns.SetNetclass(name, nc)
        for net in nets:
            ns.SetNetclassPatternAssignment(net, name)
    board.SynchronizeNetsAndNetClasses(True)


def lock_tracks(board, net_names):
    """Lock every track and via on `net_names`; KiCad's DSN export then
    writes them as `(type fix)` (probed, 10.0.5). Returns the count."""
    n = 0
    for t in board.GetTracks():
        if t.GetNetname() in net_names:
            t.SetLocked(True)
            n += 1
    return n
```

Run the coupon guard (Task 1 step 7: build, `git hash-object` = `9f7b8226245deb3c758f5edfaf8418e8a83097c9`, `git restore` the churn) — `new_board` and `courtyard_boxes` are on the coupon's path.

- [ ] **Step 2: Write `hardware/reva/spike/stripe.py`**

```python
#!/usr/bin/env python3
"""The SENSE_1 strip of Rev A as a board to route (P4a spec §2).

Runs under KiCad's Python. Positions come from the P1 hole list and from the
footprints themselves; the only typed numbers are the spec's assumptions --
the outline, the port column, the part orientations -- and the coupon's
rules (clearance, widths, the 2.0 mm decoupling distance).

Throwaway harness: P4 builds its own placement. What is meant to outlive the
spike lives in hardware/gen/.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REVA = os.path.normpath(os.path.join(HERE, ".."))
HW = os.path.normpath(os.path.join(REVA, ".."))
for _p in (HW, REVA):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import pcbnew          # noqa: E402
import assign          # noqa: E402
import blocks as BL    # noqa: E402
import build as RB     # noqa: E402
import parts as RP     # noqa: E402
from gen import kipcb  # noqa: E402

SENSE = BL.SENSE[1]
OUT_L, OUT_R = BL.MODULE_PINS["B2"], BL.MODULE_PINS["B1"]
LOCKED_NETS = (SENSE, OUT_L, OUT_R)
SUPPLY_NETS = (BL.GND, BL.SM3V3)
X0, Y0, X1, Y1 = 203.0, 6.0, 300.0, 122.0   # spec §2.1 outline, an assumption
PORT_X = X0 + 1.5          # port pad (1.5 mm round) keeps 0.5 mm from the cut
PORT_PITCH = 2.54
PORT_ACCESS = 8.0          # a port slot needs this much keepout-free run eastward
EDGE_INSET = 1.0           # SMD courtyards stay this far inside the outline
CLEARANCE = 0.2            # the coupon's board minimum
W_SIGNAL, W_SUPPLY = 0.25, 0.4
DECOUPLE_MAX_MM = 2.0      # the coupon's check_layout rule 5
# Spec §2.2: pot pins NORTH (270). Probed while planning: pins south (90)
# puts five strip LEDs on pot pins (26 shorting_items before any routing);
# sideways fails too (0: RES_B crosses the cut; 180: C15 finds no place).
ROT = {"pot": 270, "jack": 0, "led": 0, "key": 0}
COPPER = {2: ("F.Cu", "B.Cu"), 4: ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu")}


class Strip:
    """The board plus the bookkeeping the proof needs."""

    def __init__(self, layers):
        self.board = None
        self.layers = layers
        self.parts = []        # every netlist Part on the board, ports included
        self.holes = {}        # ref -> (x, y) of the panel hole it must sit on
        self.keepouts = []     # (label, (l, t, r, b))
        self.ports = {}        # net -> port ref
        self.decouplers = {}   # cap ref -> mux ref
        self.vcc_pin = {}      # mux ref -> its VCC pad number
        self.led_nets = set()
        self.locked = []       # filled by locked.apply() (Task 3)


def _box(bb):
    return (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()),
            pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))


def _grow(box, d):
    return (box[0] - d, box[1] - d, box[2] + d, box[3] + d)


def _overlaps(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def _rect(box):
    l, t, r, b = box
    return [(l, t), (r, t), (r, b), (l, b)]


def hole_point(fp):
    """Where the panel hole sits on this footprint: the centre of its one
    F.Fab circle, or its front courtyard centre when it has none (the Thonk
    key). Probed 2026-09-29: pot shaft, jack bore and LED body are each the
    footprint's only F.Fab circle."""
    circles = [g for g in fp.GraphicalItems()
               if hasattr(g, "GetShape") and g.GetShape() == pcbnew.SHAPE_T_CIRCLE
               and g.GetLayer() == pcbnew.F_Fab]
    if len(circles) > 1:
        raise ValueError("%s: %d F.Fab circles, expected one or none"
                         % (fp.GetReference(), len(circles)))
    if circles:
        c = circles[0].GetCenter()
        return pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
    l, t, r, b = _box(fp.GetCourtyard(pcbnew.F_CrtYd).BBox())
    return (l + r) / 2.0, (t + b) / 2.0


def _panel_rows(proj):
    """[(part, hole, kind)] for every panel part of a kind in ROT."""
    by_id = {}
    for h in assign.load_holes():
        for i in h.get("ids", [h["id"]]):
            by_id[i] = h
    rows = []
    for part in proj.parts():
        if not part.panel_id:
            continue
        h = by_id.get(part.panel_id)
        if h is None:
            raise ValueError("%s: panel id %s is not in the hole list"
                             % (part.ref, part.panel_id))
        if h["kind"] in ROT:
            rows.append((part, h, h["kind"]))
    return rows


def _survey(rows):
    """{ref: (anchor, [(pad, box)])} with every panel part placed at its hole
    on a scratch board. Must run before kipcb.new_board(): the scratch board
    draws UUIDs, and new_board() reseeds the generator afterwards."""
    scratch = pcbnew.BOARD()
    out = {}
    for part, h, kind in rows:
        fp = kipcb.add_part(scratch, part, 0.0, 0.0, ROT[kind])
        rx, ry = hole_point(fp)
        anchor = (h["x_mm"] - rx, h["y_mm"] - ry)
        fp.SetPosition(kipcb._pt(*anchor))
        out[part.ref] = (anchor, [(str(p.GetNumber()), _box(p.GetBoundingBox()))
                                  for p in fp.Pads()])
    return out


def _classify(survey):
    """own: every pad east of the cut. foreign: some pad reaches past the
    cut, some does not -> {ref: [(pad, box) reaching in]}. Anything else is
    left out."""
    own, foreign = set(), {}
    for ref, (_anchor, pads) in survey.items():
        inside = [pb for pb in pads if pb[1][0] >= X0]
        reaching = [pb for pb in pads if pb[1][2] > X0]
        if len(inside) == len(pads):
            own.add(ref)
        elif reaching:
            foreign[ref] = reaching
    return own, foreign


def _check_own(rows, own, panel_map):
    want = {p["id"] for p in panel_map["pots"] if p["sense"] == SENSE}
    got = {part.panel_id for part, _h, kind in rows
           if kind == "pot" and part.ref in own}
    if want != got:
        raise ValueError("strip pots differ from %s's: missing %s, extra %s"
                         % (SENSE, sorted(want - got), sorted(got - want)))


def _spiral(cx, cy, step, rmax):
    """Candidate centres in square rings around (cx, cy), nearest first
    inside each ring, in a fixed order."""
    yield cx, cy
    n = 1
    while n * step <= rmax:
        ring = ([(i, -n) for i in range(-n, n + 1)]
                + [(n, j) for j in range(-n + 1, n + 1)]
                + [(i, n) for i in range(n - 1, -n - 1, -1)]
                + [(-n, j) for j in range(n - 1, -n, -1)])
        ring.sort(key=lambda ij: (ij[0] ** 2 + ij[1] ** 2, ij))
        for i, j in ring:
            yield cx + i * step, cy + j * step
        n += 1


def _place_smd(board, part, target, blocked, step, rmax, accept=None):
    """Put `part` on the back at the first spiral position around `target`
    whose courtyard clears `blocked` and the outline inset, and that
    `accept(fp)` (if given) approves. Appends the courtyard to `blocked`."""
    fp = kipcb.add_part(board, part, target[0], target[1], 0, side="B")
    rel = {}
    for rot in (0, 90):
        fp.SetOrientationDegrees(rot)
        l, t, r, b = _box(fp.GetCourtyard(pcbnew.B_CrtYd).BBox())
        rel[rot] = (l - target[0], t - target[1], r - target[0], b - target[1])
    inner = (X0 + EDGE_INSET, Y0 + EDGE_INSET, X1 - EDGE_INSET, Y1 - EDGE_INSET)
    for x, y in _spiral(target[0], target[1], step, rmax):
        for rot in (0, 90):
            l, t, r, b = rel[rot]
            box = (x + l, y + t, x + r, y + b)
            if (box[0] < inner[0] or box[1] < inner[1]
                    or box[2] > inner[2] or box[3] > inner[3]):
                continue
            if any(_overlaps(box, o) for o in blocked):
                continue
            fp.SetOrientationDegrees(rot)
            fp.SetPosition(kipcb._pt(x, y))
            if accept is not None and not accept(fp):
                continue
            blocked.append(box)
            return fp
    raise ValueError("no free place for %s within %.1f mm of (%.2f, %.2f)"
                     % (part.ref, rmax, target[0], target[1]))


def _pad_xy(fp, number):
    for p in fp.Pads():
        if str(p.GetNumber()) == str(number):
            return pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)
    raise KeyError("%s has no pad %s" % (fp.GetReference(), number))


def _decouplers(proj, muxes):
    """{cap ref: mux ref}: in sheet mux_sense_1 each mux is followed by its
    100 nF (blocks.mux_region), SM_3V3 on pin 1, GND on pin 2."""
    sheet = [s for s in proj.sheets if s.name == "mux_" + SENSE.lower()][0]
    out, cur = {}, None
    for p in sheet.parts:
        if p.ref in muxes:
            cur = p.ref
        elif cur and p.nets == {"1": BL.SM3V3, "2": BL.GND}:
            out[p.ref], cur = cur, None
    if sorted(out.values()) != sorted(muxes):
        raise ValueError("decouplers %s do not cover muxes %s" % (out, muxes))
    return out


def _leaving_nets(proj, placed_refs):
    """Nets carried both by a placed part and by a board part outside the
    strip -- each of them needs a port."""
    inside, outside = set(), set()
    for p in proj.parts():
        if not p.footprint or not p.on_board:
            continue
        (inside if p.ref in placed_refs else outside).update(p.nets.values())
    return sorted(inside & outside)


def _assign_slots(srcs, slots):
    """Order-preserving assignment of ascending sources to ascending slots
    that minimises the total |dy| (dynamic programme, n x m)."""
    n, m = len(srcs), len(slots)
    inf = float("inf")
    cost = [[0.0] * (m + 1)] + [[inf] * (m + 1) for _ in range(n)]
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            cost[i][j] = min(cost[i][j - 1],
                             cost[i - 1][j - 1] + abs(srcs[i - 1] - slots[j - 1]))
    if cost[n][m] == inf:
        raise ValueError("%d ports, only %d free slots" % (n, m))
    out, j = [], m
    for i in range(n, 0, -1):
        while cost[i][j] == cost[i][j - 1]:
            j -= 1
        out.append(slots[j - 1])
        j -= 1
    return out[::-1]


def _port_rows(board, nets, keepouts):
    """[(net, y)] in the port column: ports in the order of their net's mean
    pad y, each on a 2.54 mm slot whose eastward run of PORT_ACCESS mm is
    clear of every keepout."""
    src = {}
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() in nets:
                src.setdefault(pad.GetNetname(), []).append(
                    pcbnew.ToMM(pad.GetPosition().y))
    order = sorted(nets, key=lambda n: (sum(src[n]) / len(src[n]), n))
    slots, y = [], Y0 + EDGE_INSET + 1.0
    while y <= Y1 - EDGE_INSET - 1.0:
        band = (X0, y - 1.0, X0 + PORT_ACCESS, y + 1.0)
        if not any(_overlaps(band, k) for _label, k in keepouts):
            slots.append(y)
        y = round(y + PORT_PITCH, 3)
    ys = _assign_slots([sum(src[n]) / len(src[n]) for n in order], slots)
    return list(zip(order, ys))


def build(layers=2):
    proj = RB.project()
    pm = RB.load_panel_map()
    rows = _panel_rows(proj)
    survey = _survey(rows)                       # before new_board()
    own, foreign = _classify(survey)
    _check_own(rows, own, pm)

    s = Strip(layers)
    board = s.board = kipcb.new_board(X1 - X0, Y1 - Y0, layers, origin=(X0, Y0))
    by_ref = {p.ref: p for p in proj.parts()}

    tht = []
    leds = []
    for part, h, kind in rows:
        if part.ref not in own:
            continue
        anchor, pads = survey[part.ref]
        kipcb.add_part(board, part, anchor[0], anchor[1], ROT[kind])
        s.parts.append(part)
        s.holes[part.ref] = (h["x_mm"], h["y_mm"])
        tht += [_grow(box, CLEARANCE) for _n, box in pads]
        if kind == "led":
            leds.append(part)

    for ref in sorted(foreign):
        for number, box in foreign[ref]:
            k = _grow(box, CLEARANCE)
            k = (max(k[0], X0), k[1], k[2], k[3])
            kipcb.add_keepout(board, _rect(k), COPPER[layers])
            s.keepouts.append(("%s.%s" % (ref, number or "tab"), k))

    blocked = tht + [k for _label, k in s.keepouts]
    muxes = ["U_MUX%d" % m for m in pm["muxes"][SENSE]]
    for mref in muxes:
        m = int(mref[len("U_MUX"):])
        shafts = [(p["x_mm"], p["y_mm"]) for p in pm["pots"] if p["mux"] == m]
        target = (sum(x for x, _ in shafts) / len(shafts),
                  sum(y for _, y in shafts) / len(shafts))
        _place_smd(board, by_ref[mref], target, blocked, step=0.5, rmax=30.0)
        s.parts.append(by_ref[mref])

    s.decouplers = _decouplers(proj, muxes)
    for cref, mref in sorted(s.decouplers.items()):
        s.vcc_pin[mref] = str(by_ref[mref].sym.by_name("VCC"))
        vcc = _pad_xy(board.FindFootprintByReference(mref), s.vcc_pin[mref])

        def near_vcc(fp, vcc=vcc):
            x, y = _pad_xy(fp, 1)
            return ((x - vcc[0]) ** 2 + (y - vcc[1]) ** 2) ** 0.5 <= DECOUPLE_MAX_MM

        _place_smd(board, by_ref[cref], vcc, blocked, step=0.1, rmax=3.0,
                   accept=near_vcc)
        s.parts.append(by_ref[cref])

    anode = {d.nets[str(d.sym.by_name("A"))]: d for d in leds}
    for p in sorted(proj.parts(), key=lambda p: p.ref):
        d = anode.get(p.nets.get("2")) if p.ref.startswith("R") else None
        if d is None:
            continue
        target = s.holes[d.ref]
        _place_smd(board, p, target, blocked, step=0.25, rmax=10.0)
        s.parts.append(p)
        s.led_nets.update(p.nets.values())

    for k, (net, y) in enumerate(_port_rows(board, _leaving_nets(
            proj, {p.ref for p in s.parts}), s.keepouts), 1):
        port = RP.make("tp", "PORT%d" % k).by_number(1, net)
        kipcb.add_part(board, port, PORT_X, y, 0, side="B")
        s.parts.append(port)
        s.ports[net] = port.ref

    kipcb.set_netclasses(board, W_SIGNAL, {"Supply": (W_SUPPLY, list(SUPPLY_NETS))})
    return s


def intent(strip):
    """{net: {(ref, pad), ...}} the board must carry: every placed part's
    pins, ports included. Pin numbers equal pad numbers for every footprint
    in the strip (probed 2026-09-29)."""
    out = {}
    for part in strip.parts:
        for pin, net in part.nets.items():
            out.setdefault(net, set()).add((part.ref, pin))
    return out
```

Note for the implementer: `led_nets` collects both nets of each LED series resistor (`LED<n>` and `LED<n>_A`) — exactly the LED nets in the strip.

- [ ] **Step 3: Write `hardware/reva/spike/proof.py` (placement-stage steps)**

```python
#!/usr/bin/env python3
"""The P4a proof chain (spec §4.1). Every step runs and prints; the run is
green only if every gated step is. Each gated step has a sabotage that must
turn it red, and a zero-match guard: a step that examined nothing is red,
never green."""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pcbnew                        # noqa: E402
import stripe as ST                  # noqa: E402
from gen import kipcb                # noqa: E402
from gen import pcb_proof as PP      # noqa: E402

# track_dangling is gated beside the coupon's five: a track drawn onto a
# foreign pad is renamed to that pad's net on save (probed 2026-09-29), so a
# router's short arrives here as a dangling track, not as shorting_items.
GATED = ("shorting_items", "clearance", "hole_clearance", "hole_to_hole",
         "tracks_crossing", "track_dangling")


def _fp(board, ref):
    return board.FindFootprintByReference(ref)


def check_nets(s, pcb_path, prefix):
    want = ST.intent(s)
    if not want:
        return False, "examined 0 nets", []
    got = {n: set(v) for n, v in kipcb.board_nets(s.board).items()}
    bad = ["%s want %s got %s" % (n, sorted(want.get(n, ())), sorted(got.get(n, ())))
           for n in sorted(set(want) | set(got)) if want.get(n) != got.get(n)]
    return not bad, "%d nets, %d differ from the intent node for node" % (len(want), len(bad)), bad


def check_anchors(s, pcb_path, prefix):
    if not s.holes:
        return False, "examined 0 panel parts", []
    bad, worst = [], 0.0
    for ref, (hx, hy) in sorted(s.holes.items()):
        x, y = ST.hole_point(_fp(s.board, ref))
        d = math.hypot(x - hx, y - hy)
        worst = max(worst, d)
        if d > 0.01:
            bad.append("%s sits %.3f mm off its hole" % (ref, d))
    return not bad, "%d panel parts, worst %.4f mm off its hole (limit 0.01)" % (len(s.holes), worst), bad


def check_decoupling(s, pcb_path, prefix):
    if not s.decouplers:
        return False, "examined 0 decouplers", []
    bad, worst = [], 0.0
    for cref, mref in sorted(s.decouplers.items()):
        c = [p for p in _fp(s.board, cref).Pads() if str(p.GetNumber()) == "1"][0]
        m = [p for p in _fp(s.board, mref).Pads()
             if str(p.GetNumber()) == s.vcc_pin[mref]][0]
        d = math.hypot(pcbnew.ToMM(c.GetPosition().x - m.GetPosition().x),
                       pcbnew.ToMM(c.GetPosition().y - m.GetPosition().y))
        worst = max(worst, d)
        if d > ST.DECOUPLE_MAX_MM:
            bad.append("%s is %.3f mm from %s's VCC pad" % (cref, d, mref))
    return not bad, "%d decouplers, worst %.3f mm (limit %.1f)" % (
        len(s.decouplers), worst, ST.DECOUPLE_MAX_MM), bad


def check_copper(s, pcb_path, prefix):
    counts = PP.drc(pcb_path, prefix + "-drc.rpt")
    bad = ["%s: %d" % (k, counts[k]) for k in GATED if counts.get(k)]
    others = ", ".join("%s %d" % kv for kv in sorted(counts.items())
                       if kv[0] not in GATED) or "none"
    gated = ", ".join("%s %d" % (k, counts.get(k, 0)) for k in GATED)
    return not bad, "gated: %s (not gated: %s)" % (gated, others), bad


def render(s, pcb_path, prefix):
    lines = []
    for side, suffix in (("top", "-top.png"), ("bottom", "-bottom.png")):
        rc, out = PP.render(pcb_path, prefix + suffix, side)
        if rc:
            lines.append("render %s rc=%d: %s" % (side, rc, out[-300:]))
    return not lines, "rendered %s-top.png, %s-bottom.png" % (
        os.path.basename(prefix), os.path.basename(prefix)), lines


def _steps(routed):
    steps = [("nets", check_nets), ("anchors", check_anchors),
             ("decoupling", check_decoupling), ("copper", check_copper)]
    return steps + [("render", render)]


def run(s, pcb_path, prefix, routed):
    green = True
    for i, (name, fn) in enumerate(_steps(routed), 1):
        ok, line, details = fn(s, pcb_path, prefix)
        print("%s %d. %-10s %s" % ("   " if ok else "RED", i, name, line))
        for d in details[:25]:
            print("        " + d)
        green = green and ok
    return green


def _sab_nets(s):
    fp = _fp(s.board, sorted(s.holes)[0])
    pad = [p for p in fp.Pads() if p.GetNetname()][0]
    pad.SetNet(kipcb._net(s.board, "SABOTAGE"))


def _sab_nets_missing(s):
    s.parts[:] = []


def _sab_anchors(s):
    fp = _fp(s.board, sorted(s.holes)[0])
    fp.Move(kipcb._pt(0.5, 0.0))


def _sab_anchors_missing(s):
    s.holes.clear()


def _sab_decoupling(s):
    cref = sorted(s.decouplers)[0]
    _fp(s.board, cref).Move(kipcb._pt(3.0, 0.0))


def _sab_decoupling_missing(s):
    s.decouplers.clear()


def _sab_copper(s):
    """A GND track 0.1 mm beside a decoupler's SM_3V3 pad -- a near miss, not
    a touch: copper overlapping a foreign pad is renamed to that pad's net on
    save (probed 2026-09-29), so a touch reaches the DRC as a dangling track."""
    fp = _fp(s.board, sorted(s.decouplers)[0])
    bb = [p for p in fp.Pads() if str(p.GetNumber()) == "1"][0].GetBoundingBox()
    x = pcbnew.ToMM(bb.GetRight()) + 0.1 + ST.W_SIGNAL / 2.0
    kipcb.add_track(s.board, "B.Cu", ST.W_SIGNAL, ST.SUPPLY_NETS[0],
                    [(x, pcbnew.ToMM(bb.GetTop()) - 1.0), (x, pcbnew.ToMM(bb.GetBottom()) + 1.0)])


SABOTAGES = {"nets": _sab_nets, "nets_missing": _sab_nets_missing,
             "anchors": _sab_anchors, "anchors_missing": _sab_anchors_missing,
             "decoupling": _sab_decoupling, "decoupling_missing": _sab_decoupling_missing,
             "copper": _sab_copper}


def sabotage(s, name):
    if name not in SABOTAGES:
        raise SystemExit("unknown sabotage %r; known: %s" % (name, ", ".join(sorted(SABOTAGES))))
    SABOTAGES[name](s)
    print("SABOTAGED: %s" % name)
```

Note: `_sab_nets` breaks the board, not the intent: the step must see the board disagree. `_sab_decoupling` moves the cap 3 mm, which also may create copper violations — only the decoupling step's RED is what this sabotage proves.

- [ ] **Step 4: Write `hardware/reva/spike/run.py`**

```python
#!/usr/bin/env python3
"""P4a routing spike: build the SENSE_1 strip, route it, prove it.

    KIPY hardware/reva/spike/run.py --method none|own|freerouting --layers 2|4 [--sabotage NAME]

Writes hardware/reva/spike/out/<method>-<layers>L.* (gitignored). Exit 0 only
when every gated proof step is green.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import proof as PF    # noqa: E402
import stripe as ST   # noqa: E402
from gen import kipcb  # noqa: E402

OUT = os.path.join(HERE, "out")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--method", choices=("none", "own", "freerouting"), required=True)
    ap.add_argument("--layers", type=int, choices=(2, 4), default=2)
    ap.add_argument("--sabotage", default="")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    prefix = os.path.join(OUT, "%s-%dL" % (a.method, a.layers))

    t0 = time.time()
    s = ST.build(a.layers)
    print("built the strip: %d parts, %d keepouts, %d ports in %.1f s"
          % (len(s.parts), len(s.keepouts), len(s.ports), time.time() - t0))
    for label, box in s.keepouts:
        print("   keepout %-10s x %.2f..%.2f y %.2f..%.2f" % (label, box[0], box[2], box[1], box[3]))
    routed = a.method != "none"
    if a.sabotage:
        PF.sabotage(s, a.sabotage)
    pcb = prefix + ".kicad_pcb"
    kipcb.save(s.board, pcb)
    print("wrote", os.path.relpath(pcb))
    t1 = time.time()
    green = PF.run(s, pcb, prefix, routed)
    print("proof took %.1f s -- %s" % (time.time() - t1, "GREEN" if green else "RED"))
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
```

`.gitignore`: after `hardware/reva/out/` add `hardware/reva/spike/out/`.

`hardware/reva/spike/README.md`:
```markdown
# P4a routing spike (throwaway harness)

Spec: `docs/superpowers/specs/2026-09-29-rev-a-p4a-routing-spike-design.md`.
Report: `docs/hardware/routing-spike.md`.

Runs under KiCad's Python:

    "/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe" hardware/reva/spike/run.py --method none --layers 2

Everything here is spike code and goes when P4 starts. What outlives it lives
in `hardware/gen/` (`kipcb.py`, `pcb_proof.py`, and `route.py` if our own
router wins). Outputs land in `out/`, gitignored.
```

- [ ] **Step 5: Run it and look at the picture**

Run: `KIPY hardware/reva/spike/run.py --method none --layers 2`
Expected (the planning dry run of exactly this code printed it): `built the strip: 72 parts, 13 keepouts, 23 ports` (22 pots + 6 jacks + 7 LEDs + 1 key + 3 muxes + 3 caps + 7 resistors = 49, plus 23 ports); 13 keepout lines on J12 (S, T, TN), RV38 (tab), RV40 (tab, 1), RV44 (tab), RV48 (tab, 1), SW2 (2, 3, 5, 6); then `52 nets, 0 differ`, `36 panel parts, worst 0.0000 mm off its hole`, `3 decouplers, worst 1.778 mm`, copper green with `copper_edge_clearance 15` among the ungated classes, render; `proof took ~2.4 s -- GREEN`. Differences from these numbers are recorded, not smoothed over.

The 15 `copper_edge_clearance` are the top pot row's pins (pins north) at y ≈ 7.0 against the assumed edge at y 6 — a finding for P4 (the rail zone), not something to fix here.

If `_check_own` raises, or a `_place_smd` raises "no free place", stop and report the message — do not widen `rmax` or move the outline without the controller.

Open `hardware/reva/spike/out/none-2L-top.png` and `-bottom.png` with the Read tool and describe what you see (mux positions relative to their pot groups, resistor positions, the port column). The controller sends both PNGs to Bastian.

- [ ] **Step 6: Prove every placement-stage RED**

For each of `nets nets_missing anchors anchors_missing decoupling decoupling_missing copper`:
Run: `KIPY hardware/reva/spike/run.py --method none --layers 2 --sabotage <name>`
Expected: exit 1 and a `RED` line on the matching step (`nets`, `anchors`, `decoupling`, `copper`). Paste the RED line of each into the report's measurement log.

- [ ] **Step 7: Start the report's measurement log**

Create `docs/hardware/routing-spike.md`:
```markdown
# Rev A routing spike (P4a)

Spec: [`../superpowers/specs/2026-09-29-rev-a-p4a-routing-spike-design.md`](../superpowers/specs/2026-09-29-rev-a-p4a-routing-spike-design.md)

*Status: measuring. The recommendation is written in the last task.*

## Measurement log

Every number below was printed by the command beside it.

### Strip (Task 2)

<paste: the run.py header lines (parts, keepouts, ports), the five proof lines, the seven sabotage RED lines, each with its command>
```
Replace the `<paste: ...>` line with the actual pasted output before committing.

- [ ] **Step 8: Commit**

```bash
git add hardware/gen/kipcb.py hardware/reva/spike/stripe.py hardware/reva/spike/proof.py hardware/reva/spike/run.py hardware/reva/spike/README.md docs/hardware/routing-spike.md .gitignore
```
```bash
git commit -m "hw(reva/spike): the SENSE_1 strip board with its placement-stage proof"
```

---

### Task 3: Locked nets and the routed-stage proof steps

Spec §2.4 and §4.1 steps 2, 3, 5. SENSE_1, OUT_L and OUT_R are hand-routed as data and locked before any router runs; the proof gains the steps that only make sense on a routed board.

**Files:**
- Create: `hardware/reva/spike/locked.py`
- Modify: `hardware/reva/spike/proof.py`, `hardware/reva/spike/run.py`, `hardware/reva/spike/stripe.py` (two attributes), `docs/hardware/routing-spike.md`

**Interfaces:**
- Consumes: `stripe.Strip`, `stripe.LOCKED_NETS`, `stripe.W_SIGNAL`, `kipcb.add_track`, `kipcb.lock_tracks` (Task 2).
- Produces `locked`:
  - `TRACKS: list[tuple[str, str, float, list]]` — `(net, layer, width_mm, [point, ...])`; a point is `("pad", ref, number)`, `("port", net)` or `(x, y)`.
  - `apply(strip) -> int` — adds and locks the tracks, records `strip.locked = geometry(strip.board)`, returns the number of locked items.
  - `geometry(board) -> list[tuple]` — sorted `(net, layer, x1, y1, x2, y2, width)` (mm, rounded to 0.001) of every track on a locked net.
- Produces `stripe.Strip` attributes `unrouted_before_fill: int | None` (set by the routing methods, Tasks 6–7) and `fill_nets: list[str]` (nets the run fills after routing).
- Produces proof steps `locked` (gated), `ratsnest` (gated, routed only), `audio` (measured, routed only; gated only on having examined something) and sabotages `locked`, `locked_missing`, `ratsnest`, `audio_missing`.
- Produces `run.py --where NET` — prints every pad of `NET` (ref, pad, x, y, layers) and the net's port, then exits 0.

- [ ] **Step 1: `--where` in `run.py`**

Add `ap.add_argument("--where", default="")` and, right after `s = ST.build(a.layers)` and its print:
```python
    if a.where:
        for fp in s.board.GetFootprints():
            for pad in fp.Pads():
                if pad.GetNetname() == a.where:
                    layers = "/".join(n for n in ("F.Cu", "B.Cu")
                                      if pad.IsOnLayer(kipcb.LAYER[n]))
                    print("   %-8s %-3s (%.3f, %.3f) %s" % (
                        fp.GetReference(), pad.GetNumber(),
                        pcbnew.ToMM(pad.GetPosition().x),
                        pcbnew.ToMM(pad.GetPosition().y), layers))
        print("   port:", s.ports.get(a.where, "none"))
        return 0
```
(add `import pcbnew` to run.py's imports).

Run: `KIPY hardware/reva/spike/run.py --method none --where SENSE_1`, then `--where OUT_L` and `--where OUT_R`. Copy the coordinates into your notes; the corners in step 2 are drawn against them and against `out/none-2L-bottom.png`.

- [ ] **Step 2: Write `hardware/reva/spike/locked.py`**

```python
#!/usr/bin/env python3
"""The strip's rule-bearing nets, hand-routed and locked (P4a spec §2.4).

SENSE_1 runs from the three mux COM pins to its port; OUT_L and OUT_R run
from their jack's tip to their port. All on B.Cu (the SMD side), no vias,
W_SIGNAL wide. Data, not a router: every corner was placed by hand against
the pad coordinates `run.py --where <net>` prints, and the proof's `locked`
step checks the routers leave every segment exactly where it is.

Endpoints are named, never numeric: ("pad", ref, number) resolves from the
built board, ("port", net) resolves to that net's port pad. Raw (x, y) is
used only for corners.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pcbnew          # noqa: E402
import stripe as ST    # noqa: E402
from gen import kipcb  # noqa: E402

SENSE, OUT_L, OUT_R = ST.SENSE, ST.OUT_L, ST.OUT_R

# (net, layer, width_mm, [point, ...])
TRACKS = [
    # Written in step 2 against `run.py --where`: one trunk from the SENSE_1
    # port eastward with a branch to each COM pad, then OUT_L and OUT_R
    # from ("pad", <jack ref>, "T") to ("port", OUT_L / OUT_R).
]


def _pad_xy(board, ref, number):
    fp = board.FindFootprintByReference(ref)
    if fp is None:
        raise KeyError("no footprint %s" % ref)
    for pad in fp.Pads():
        if str(pad.GetNumber()) == str(number):
            return pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
    raise KeyError("%s has no pad %s" % (ref, number))


def _resolve(strip, point):
    if point[0] == "pad":
        return _pad_xy(strip.board, point[1], point[2])
    if point[0] == "port":
        return _pad_xy(strip.board, strip.ports[point[1]], "1")
    return point


def geometry(board):
    out = []
    for t in board.GetTracks():
        if t.GetNetname() not in ST.LOCKED_NETS or t.Type() != pcbnew.PCB_TRACE_T:
            continue
        out.append((t.GetNetname(), t.GetLayerName(),
                    round(pcbnew.ToMM(t.GetStart().x), 3), round(pcbnew.ToMM(t.GetStart().y), 3),
                    round(pcbnew.ToMM(t.GetEnd().x), 3), round(pcbnew.ToMM(t.GetEnd().y), 3),
                    round(pcbnew.ToMM(t.GetWidth()), 3)))
    return sorted(out)


def apply(strip):
    for net, layer, width, points in TRACKS:
        kipcb.add_track(strip.board, layer, width, net,
                        [_resolve(strip, p) for p in points])
    n = kipcb.lock_tracks(strip.board, set(ST.LOCKED_NETS))
    strip.locked = geometry(strip.board)
    return n
```

Now author `TRACKS`. The first entry's shape, with the endpoint names you will use (the COM pin is the 74HC4051's pin named `A`; its number comes from `run.py --where SENSE_1`):
```python
    (SENSE, "B.Cu", ST.W_SIGNAL, [("port", SENSE), (<x>, <y>), ..., ("pad", "U_MUX3", "<COM pad>")]),
```
Rules for the corners: B.Cu only; 45° or 90° segments; every segment keeps at least `ST.CLEARANCE` + half a width from every other-net pad (the copper step checks it); nothing passes between two pads of the same SOIC row; each of the three COM pads is reached; OUT_L and OUT_R run as far apart from the LED resistors (`--where LED<n>`) as the geometry lets you. Author one net per Edit (coupon lesson 4: incremental writes).

- [ ] **Step 3: Wire `locked.apply` into `run.py` and add the steps to `proof.py`**

In `run.py`, add `import locked as LK` and, after the `--where` block:
```python
    print("locked %d items on %s" % (LK.apply(s), ", ".join(ST.LOCKED_NETS)))
```

In `stripe.Strip.__init__` add:
```python
        self.unrouted_before_fill = None   # set by a routing method (Tasks 6-7)
        self.fill_nets = []                # nets filled after routing
```

In `proof.py`, add `import locked as LK` and:
```python
def check_locked(s, pcb_path, prefix):
    if not s.locked:
        return False, "examined 0 locked segments", []
    now = LK.geometry(s.board)
    bad = ["moved or added: %s" % (g,) for g in now if g not in s.locked]
    bad += ["gone: %s" % (g,) for g in s.locked if g not in now]
    unlocked = [t for t in s.board.GetTracks()
                if t.GetNetname() in ST.LOCKED_NETS and not t.IsLocked()]
    bad += ["%d unlocked items on locked nets" % len(unlocked)] if unlocked else []
    nets = {g[0] for g in s.locked}
    bad += ["no locked track on %s" % n for n in ST.LOCKED_NETS if n not in nets]
    return not bad, "%d locked segments unchanged on %s" % (
        len(s.locked), ", ".join(ST.LOCKED_NETS)), bad


def check_ratsnest(s, pcb_path, prefix):
    live = PP.live_unconnected(s.board)
    from_drc = PP.count_drc_violations(prefix + "-drc.rpt").get("unconnected_items", 0)
    before = s.unrouted_before_fill
    bad = ["%-12s %s" % (n, ", ".join(sorted(v)))
           for n, v in sorted(PP.unconnected_by_net(prefix + "-drc.rpt").items())]
    if before is None:
        bad.append("the routing method did not record its unrouted count")
    elif before:
        bad.append("%d connections unrouted before the fill" % before)
    ok = live == 0 and from_drc == 0 and before == 0
    return ok, "unconnected: %s before fill, %d live, %d kicad-cli" % (
        before, live, from_drc), bad


def check_audio(s, pcb_path, prefix):
    audio = PP.track_segments(s.board, lambda n: n in (ST.OUT_L, ST.OUT_R))
    leds = PP.track_segments(s.board, lambda n: n in s.led_nets)
    if not audio or not leds:
        return False, "examined %d audio and %d LED segments" % (len(audio), len(leds)), []
    worst = min((PP.seg_seg_dist((a[2], a[3]), (b[2], b[3])), a[0], b[0])
                for a in audio for b in leds)
    return True, "nearest LED track to audio: %.2f mm (%s to %s; coupon rule 10.0, not gated)" % worst, []
```
`check_ratsnest` reads the report `check_copper` wrote, so it runs after it. Replace `_steps` with:
```python
def _steps(routed):
    steps = [("nets", check_nets), ("anchors", check_anchors),
             ("decoupling", check_decoupling), ("locked", check_locked),
             ("copper", check_copper)]
    if routed:
        steps += [("ratsnest", check_ratsnest), ("audio", check_audio)]
    return steps + [("render", render)]
```
and add the sabotages:
```python
def _sab_locked(s):
    t = [t for t in s.board.GetTracks()
         if t.GetNetname() == ST.SENSE and t.Type() == pcbnew.PCB_TRACE_T][0]
    t.SetEnd(kipcb._pt(pcbnew.ToMM(t.GetEnd().x) + 0.5, pcbnew.ToMM(t.GetEnd().y)))


def _sab_locked_missing(s):
    s.locked = []


def _sab_ratsnest(s):
    """Delete one routed segment that is not on a locked net."""
    t = [t for t in s.board.GetTracks()
         if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() not in ST.LOCKED_NETS][0]
    s.board.Delete(t)


def _sab_audio_missing(s):
    s.led_nets = set()
```
and register them in `SABOTAGES` as `"locked"`, `"locked_missing"`, `"ratsnest"`, `"audio_missing"`.

`run.py` applies `--sabotage` after routing (Tasks 6–7 insert routing before it); keep it right before `kipcb.save`.

- [ ] **Step 4: Run and look**

Run: `KIPY hardware/reva/spike/run.py --method none --layers 2`
Expected: `locked <n> items on SENSE_1, OUT_L, OUT_R`; steps `nets`, `anchors`, `decoupling`, `locked`, `copper`, `render` without `RED`; exit 0. A copper RED means a corner is wrong — fix the corner, not the checker. Read both PNGs and describe the three routes; the controller sends them to Bastian.

Run with `--sabotage locked` and `--sabotage locked_missing`
Expected: exit 1, `RED 4. locked` each time. Paste both RED lines into the log. (`ratsnest` and `audio_missing` are proven on the first routed board, Task 6.)

- [ ] **Step 5: Log and commit**

Append to `docs/hardware/routing-spike.md` a `### Locked nets (Task 3)` section with the pasted `locked` line, the segment count per net (count the entries of `LK.TRACKS` per net or read them off the `locked` step), and both RED lines, each with its command.

```bash
git add hardware/reva/spike/locked.py hardware/reva/spike/proof.py hardware/reva/spike/run.py hardware/reva/spike/stripe.py docs/hardware/routing-spike.md
```
```bash
git commit -m "hw(reva/spike): SENSE_1 and the audio pair hand-routed and locked; routed-stage proof steps"
```

---

### Task 4: Freerouting installed, and its behaviour probed (controller + probe)

Spec §3.3 and §2.4. Steps 1–2 are **controller work in the main session** (downloads need Bastian's yes); steps 3–6 can go to a subagent. Nothing in this task is committed except the measurement log.

**Files:**
- Create (outside the repo): `%LOCALAPPDATA%\fireflow-tools\jre\` (unpacked Java runtime), `%LOCALAPPDATA%\fireflow-tools\freerouting.jar`
- Create (scratchpad): `probe_fr.py`
- Modify: `docs/hardware/routing-spike.md`

**Interfaces:**
- Produces, for Task 7: the verified Freerouting argument list `FR_ARGS`, whether `-mt 1` makes two runs byte-identical, whether locked tracks survive DSN → Freerouting → SES unchanged and still locked, whether keepouts are respected, and whether a `class_class` clearance is honoured.

- [ ] **Step 1: Look up the downloads (controller)**

Open `https://github.com/freerouting/freerouting/releases/latest` in the browser pane: note the version, the executable jar's file name and size, and the Java version its README or release notes require. Open `https://adoptium.net/temurin/releases/` and find the Windows x64 **JRE** `.zip` for that Java version: file name and size. Ask Bastian:

> "Für den Freerouting-Vergleich brauche ich zwei Downloads nach `%LOCALAPPDATA%\fireflow-tools\` (außerhalb des Repos): `<jar name>` (<size>) von github.com/freerouting und `<jre zip name>` (<size>) von adoptium.net. Freerouting läuft immer mit `-da` (Telemetrie aus). Einverstanden?"

Stop until he says yes.

- [ ] **Step 2: Install (controller, after the yes)**

Download both with `curl -L -o` into `$LOCALAPPDATA/fireflow-tools/`, unpack the JRE zip there and rename its top folder to `jre`, and rename the jar to `freerouting.jar`. Verify:

Run: `"$LOCALAPPDATA/fireflow-tools/jre/bin/java.exe" -version`
Run: `"$LOCALAPPDATA/fireflow-tools/jre/bin/java.exe" -jar "$LOCALAPPDATA/fireflow-tools/freerouting.jar" -help`
Paste both outputs (the version lines and the complete `-help` text) into the measurement log under `### Freerouting (Task 4)`. If `-help` lists different spellings for `-de -do -mp -mt -da --gui.enabled`, the `-help` text is the authority for Task 7.

- [ ] **Step 3: Write the probe `probe_fr.py` (scratchpad)**

```python
"""P4a Task 4 probe: what Freerouting does with locked tracks, keepouts and
a class_class clearance. Runs under KiCad's Python. Throwaway."""
import filecmp
import os
import re
import subprocess
import sys

sys.path.insert(0, r"C:\Users\bernd\Documents\AI\FireFlow\hardware")
import pcbnew              # noqa: E402
from gen import kipcb      # noqa: E402

S = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(os.environ["LOCALAPPDATA"], "fireflow-tools")
JAVA = os.path.join(TOOLS, "jre", "bin", "java.exe")
JAR = os.path.join(TOOLS, "freerouting.jar")
ARGS = ["--gui.enabled=false", "-da", "-mp", "20", "-mt", "1"]


def board():
    """40 x 20 mm, 2 layers. Nets A (x 5 -> 35 at y 10) and B (y 3 -> 17 at
    x 20) must cross; a locked net L runs at y 15 from x 5 to x 35; a keepout
    covers x 26..30 over the full height except a 2 mm gap at y 18..20."""
    b = kipcb.new_board(40, 20, 2)
    pads = {"A": [(5, 10), (35, 10)], "B": [(20, 3), (20, 17)], "L": [(5, 15), (35, 15)]}
    for net, pts in sorted(pads.items()):
        for i, (x, y) in enumerate(pts):
            fp = kipcb.footprint("TestPoint:TestPoint_Pad_D1.5mm")
            fp.SetReference("TP_%s%d" % (net, i))
            fp.SetPosition(kipcb._pt(x, y))
            b.Add(fp)
            for p in fp.Pads():
                p.SetNet(kipcb._net(b, net))
    kipcb.add_track(b, "F.Cu", 0.25, "L", [(5, 15), (35, 15)])
    kipcb.add_track(b, "B.Cu", 0.25, "L", [(5, 15), (35, 15)])
    kipcb.lock_tracks(b, {"L"})
    kipcb.add_keepout(b, [(26, 0), (30, 0), (30, 12), (26, 12)], ("F.Cu", "B.Cu"))
    kipcb.set_netclasses(b, 0.25, {"Sig": (0.25, ["A"]), "Other": (0.25, ["B"])})
    return b


def locked_geom(b):
    return sorted((t.GetNetname(), t.GetLayerName(), t.GetStart().x, t.GetStart().y,
                   t.GetEnd().x, t.GetEnd().y, t.IsLocked())
                  for t in b.GetTracks() if t.GetNetname() == "L")


def freeroute(dsn, ses):
    r = subprocess.run([JAVA, "-jar", JAR, "-de", dsn, "-do", ses] + ARGS,
                       capture_output=True, text=True, timeout=600)
    print("freerouting rc", r.returncode, "ses written", os.path.exists(ses))
    return r


b = board()
before = locked_geom(b)
dsn = os.path.join(S, "fr.dsn")
print("export", pcbnew.ExportSpecctraDSN(b, dsn))
txt = open(dsn).read()
print("fix wires in DSN:", len(re.findall(r"\(type fix\)", txt)),
      "keepouts:", len(re.findall(r"\(keepout", txt)))

freeroute(dsn, os.path.join(S, "fr1.ses"))
freeroute(dsn, os.path.join(S, "fr2.ses"))
print("two runs byte-identical:", filecmp.cmp(os.path.join(S, "fr1.ses"),
                                               os.path.join(S, "fr2.ses"), shallow=False))

print("import", pcbnew.ImportSpecctraSES(b, os.path.join(S, "fr1.ses")))
after = locked_geom(b)
print("locked geometry unchanged:", [g[:6] for g in before] == [g[:6] for g in after])
print("still locked after import:", all(g[6] for g in after), "count", len(after))
inside = [t for t in b.GetTracks() if t.GetNetname() in ("A", "B")
          and 26 < pcbnew.ToMM(t.GetStart().x) < 30 and pcbnew.ToMM(t.GetStart().y) < 12]
print("tracks starting inside the keepout:", len(inside))
b.BuildConnectivity()
print("unconnected after import:", b.GetConnectivity().GetUnconnectedCount(True))
kipcb.save(b, os.path.join(S, "fr-routed.kicad_pcb"))

# class_class: A and B at least 3 mm apart, inserted by hand into the DSN
cc = txt.replace("(network", "(network\n    (class_class (classes Sig Other) (rule (clearance 3000)))", 1)
open(os.path.join(S, "fr-cc.dsn"), "w").write(cc)
freeroute(os.path.join(S, "fr-cc.dsn"), os.path.join(S, "fr-cc.ses"))
print("class_class SES written:", os.path.exists(os.path.join(S, "fr-cc.ses")))
```
The DSN's unit is set by KiCad's export (the probe in the plan's facts showed `(width 250)` for 0.25 mm, i.e. µm); if `grep -n "(resolution" fr.dsn` shows otherwise, scale `3000` so it means 3 mm.

- [ ] **Step 4: Run the probe and measure the class_class result**

Run: `KIPY "$S/probe_fr.py"`
Then measure the A–B distance in both routed results: load `fr-routed.kicad_pcb` and, after `ImportSpecctraSES` of `fr-cc.ses` into a fresh `board()` (a second process: `new_board()` reseeds, so one board per process), print `min(PP.seg_seg_dist(...))` over A×B segment pairs on the same layer, with `gen.pcb_proof`.

- [ ] **Step 5: Record**

Paste into the log: `fix wires in DSN`, `keepouts`, `two runs byte-identical`, `locked geometry unchanged`, `still locked after import`, `tracks starting inside the keepout`, `unconnected after import`, and the A–B distance with and without `class_class`. Derive, in one line each, for Task 7:
1. `FR_ARGS` as verified.
2. Whether `freerouting.py` must call `kipcb.lock_tracks()` after the import (yes if `still locked after import` is `False`).
3. Whether Freerouting is deterministic with `-mt 1`.
4. Whether it honours `class_class` (distance ≥ 3 mm with it, < 3 mm without).

- [ ] **Step 6: Commit the log**

```bash
git add docs/hardware/routing-spike.md
```
```bash
git commit -m "docs(routing-spike): Freerouting installed and probed"
```

---

### Task 5: `route.py` — the grid router core

Spec §3.2. A pcbnew-free router: obstacles and terminals in, segments and vias out. Proven on synthetic boards with an independent geometric check, not by reading the raster.

**Files:**
- Create: `hardware/gen/route.py`, `hardware/gen/test_route.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `gen.route`:
  - `Router(bounds, pitch, layers, clearance, via_radius, via_cost=8.0)` — `bounds = (x0, y0, x1, y1)` is the area track centres may use (the caller insets the outline); layers are indices `0..layers-1`.
  - `Router.add_obstacle(net, layers, shape)` — `net` a name or `None` (netless: blocks every net); `shape` one of `("rect", l, t, r, b)`, `("circle", cx, cy, radius)`, `("seg", x1, y1, x2, y2, half_width)`.
  - `Router.add_net(name, half_width, terminals)` — `terminals = [(x, y, layers)]`.
  - `Router.run(max_iters=30, pres0=0.5, pres_mult=1.6, hist_inc=1.0) -> Result`
  - `Result.routes: dict[name, {"segments": [(layer, (x1, y1), (x2, y2))], "vias": [(x, y)]}]`, `Result.failed: list[str]`, `Result.conflicts: int`, `Result.iterations: int`.

- [ ] **Step 1: Write the failing test `hardware/gen/test_route.py`**

```python
#!/usr/bin/env python3
"""Guard for hardware/gen/route.py. Plain script; the exit code is the
verdict. Every routed result is checked by exact geometry here -- segment to
segment, segment to obstacle -- never by asking the router's own raster."""
import math
import os
import sys

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")))
from gen import route as R  # noqa: E402

CLR = 0.2
FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def seg_point(p, a, b):
    (px, py), (ax, ay), (bx, by) = p, a, b
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def seg_seg(s1, s2):
    return min(seg_point(s1[0], *s2), seg_point(s1[1], *s2),
               seg_point(s2[0], *s1), seg_point(s2[1], *s1))


def seg_rect(s, rect):
    """Distance from a segment to an axis-aligned rectangle, sampled every
    0.01 mm along the segment (exact enough for a 0.2 mm rule)."""
    (x1, y1), (x2, y2) = s
    l, t, r, b = rect
    n = max(1, int(math.hypot(x2 - x1, y2 - y1) / 0.01))
    best = float("inf")
    for i in range(n + 1):
        x, y = x1 + (x2 - x1) * i / n, y1 + (y2 - y1) * i / n
        best = min(best, math.hypot(max(l - x, 0, x - r), max(t - y, 0, y - b)))
    return best


def length(res, net):
    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for _l, a, b in res.routes[net]["segments"])


def touches(res, net, x, y):
    return any(math.hypot(a[0] - x, a[1] - y) < 1e-6 or math.hypot(b[0] - x, b[1] - y) < 1e-6
               for _l, a, b in res.routes[net]["segments"])


def test_straight():
    r = R.Router((0, 0, 10, 4), 0.2, 1, CLR, 0.3)
    r.add_net("A", 0.125, [(1.0, 2.0, (0,)), (9.0, 2.0, (0,))])
    res = r.run()
    check(not res.failed and res.conflicts == 0, "straight: routed")
    check(abs(length(res, "A") - 8.0) < 0.3, "straight: length %.2f ~ 8" % length(res, "A"))
    check(touches(res, "A", 1.0, 2.0) and touches(res, "A", 9.0, 2.0), "straight: both pads reached")


def test_detour():
    rect = (4.5, 0.0, 5.5, 3.0)
    r = R.Router((0, 0, 10, 6), 0.2, 1, CLR, 0.3)
    r.add_obstacle("X", (0,), ("rect",) + rect)
    r.add_net("A", 0.125, [(1.0, 2.0, (0,)), (9.0, 2.0, (0,))])
    res = r.run()
    d = min(seg_rect((a, b), rect) for _l, a, b in res.routes["A"]["segments"])
    check(not res.failed, "detour: routed")
    check(d >= 0.125 + CLR - 1e-6, "detour: %.3f mm from the obstacle (need %.3f)" % (d, 0.325))


def test_via():
    r = R.Router((0, 0, 10, 4), 0.2, 2, CLR, 0.3)
    r.add_obstacle(None, (0,), ("rect", 4.5, 0.0, 5.5, 4.0))
    r.add_net("A", 0.125, [(1.0, 2.0, (0,)), (9.0, 2.0, (0,))])
    res = r.run()
    check(not res.failed, "via: routed around a full-height wall")
    check(len(res.routes["A"]["vias"]) >= 2, "via: %d vias" % len(res.routes["A"]["vias"]))


def test_own_net_passable():
    r = R.Router((0, 0, 10, 4), 0.2, 1, CLR, 0.3)
    r.add_obstacle("A", (0,), ("rect", 4.0, 0.0, 6.0, 4.0))
    r.add_net("A", 0.125, [(1.0, 2.0, (0,)), (9.0, 2.0, (0,))])
    res = r.run()
    check(not res.failed and abs(length(res, "A") - 8.0) < 0.3,
          "own copper does not block its own net")


def test_crossing_two_layers():
    r = R.Router((0, 0, 10, 10), 0.2, 2, CLR, 0.3)
    r.add_net("A", 0.125, [(1.0, 5.0, (0, 1)), (9.0, 5.0, (0, 1))])
    r.add_net("B", 0.125, [(5.0, 1.0, (0, 1)), (5.0, 9.0, (0, 1))])
    res = r.run()
    check(not res.failed and res.conflicts == 0, "crossing: both nets, no conflict left")
    # Measured while planning: the router may put A and B on different layers
    # entirely, leaving no same-layer pair -- then there is nothing to measure
    # here, and test_side_by_side is the clearance check that cannot be empty.
    pairs = [(seg_seg((a1, b1), (a2, b2)), l1)
             for l1, a1, b1 in res.routes["A"]["segments"]
             for l2, a2, b2 in res.routes["B"]["segments"] if l1 == l2]
    worst = min(pairs) if pairs else (float("inf"), -1)
    check(worst[0] >= 0.25 + CLR - 1e-6,
          "crossing: A-B %.3f mm on layer %d over %d same-layer pairs (need %.3f)"
          % (worst[0], worst[1], len(pairs), 0.45))


def test_side_by_side():
    """One layer, two nets squeezed past the same obstacle: both must share
    the gap above it, so a same-layer pair always exists."""
    r = R.Router((0, 0, 10, 4), 0.2, 1, CLR, 0.3)
    r.add_obstacle(None, (0,), ("rect", 4.0, 0.0, 6.0, 1.8))
    r.add_net("A", 0.125, [(1.0, 1.0, (0,)), (9.0, 1.0, (0,))])
    r.add_net("B", 0.125, [(1.0, 3.0, (0,)), (9.0, 3.0, (0,))])
    res = r.run()
    pairs = [seg_seg((a1, b1), (a2, b2))
             for _l1, a1, b1 in res.routes.get("A", {"segments": []})["segments"]
             for _l2, a2, b2 in res.routes.get("B", {"segments": []})["segments"]]
    check(not res.failed and res.conflicts == 0 and pairs,
          "side by side: both routed, %d pairs to measure" % len(pairs))
    worst = min(pairs) if pairs else 0.0
    check(worst >= 0.25 + CLR - 1e-6,
          "side by side: A-B %.3f mm (need %.3f)" % (worst, 0.45))


def test_tree():
    r = R.Router((0, 0, 10, 10), 0.2, 1, CLR, 0.3)
    pts = [(1.0, 1.0), (9.0, 1.0), (5.0, 9.0)]
    r.add_net("T", 0.2, [(x, y, (0,)) for x, y in pts])
    res = r.run()
    check(not res.failed and all(touches(res, "T", x, y) for x, y in pts),
          "tree: all three terminals reached")


def test_deterministic():
    def once():
        r = R.Router((0, 0, 10, 10), 0.2, 2, CLR, 0.3)
        r.add_net("A", 0.125, [(1.0, 5.0, (0, 1)), (9.0, 5.0, (0, 1))])
        r.add_net("B", 0.125, [(5.0, 1.0, (0, 1)), (5.0, 9.0, (0, 1))])
        return r.run().routes
    check(once() == once(), "deterministic: two runs identical")


if __name__ == "__main__":
    for t in (test_straight, test_detour, test_via, test_own_net_passable,
              test_crossing_two_layers, test_side_by_side, test_tree, test_deterministic):
        t()
    print("FAILED: %d" % len(FAILS) if FAILS else "all route checks passed")
    sys.exit(1 if FAILS else 0)
```

- [ ] **Step 2: Run it to see it fail**

Run: `python hardware/gen/test_route.py`
Expected: `ImportError: cannot import name 'route'` (or `ModuleNotFoundError`), exit ≠ 0.

- [ ] **Step 3: Write `hardware/gen/route.py`**

```python
#!/usr/bin/env python3
"""Grid router for the generated boards (P4a spec §3.2). No pcbnew:
obstacles and terminals in, segments and vias out, millimetres throughout.

The model
---------
The routable area is a grid of cells PITCH apart on L copper layers. A path
is a chain of cell centres joined by 8-neighbour steps (so 0/45/90 degree
segments) and vias (a layer change at one cell).

Clearance is kept by *clearance classes*: one per distinct track half-width,
plus one for vias. A cell is forbidden to a class-c path if its centre is
closer to foreign copper than

    half_width(c) + clearance + slack

`slack` covers the grid: every point of a path lies within s = PITCH*sqrt(2)/2
of one of its cell centres. Against an exact obstacle shape one s suffices;
against another path, which is off-grid by up to s as well, two are needed.
Routed copper is therefore marked with half_width(own) + clearance +
half_width(c) + 2s (s where one side is a via, which sits on a cell centre).
Conservative on purpose: the DRC is the arbiter, and this only has to pass it.

Static obstacles (pads, locked tracks, keepouts) go into one array per class
holding FREE, the owning net's id (a net may cross its own copper), or
BLOCKED (netless, or claimed by two nets). Routed copper goes into a second
array per class that counts how many nets claim each cell.

Negotiated congestion (PathFinder, McMurchie & Ebeling 1995): in each round
every net still in conflict is ripped up and rerouted by A*; a cell other
nets already claim costs (1 + history) * (1 + pressure * claims). Pressure
grows each round and history accumulates where conflicts stay, until no net
overlaps another or the round limit is reached.

Deterministic: fixed net order, fixed neighbour order, a counter as the heap
tie-break, no randomness anywhere.
"""
import heapq
import math
from array import array

FREE, BLOCKED = 0, -1
SQRT2 = math.sqrt(2.0)
_DIRS = ((1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
         (1, 1, SQRT2), (1, -1, SQRT2), (-1, 1, SQRT2), (-1, -1, SQRT2))
_INF = float("inf")


def _seg_point(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def _shape_dist(px, py, shape):
    kind = shape[0]
    if kind == "rect":
        _k, l, t, r, b = shape
        return math.hypot(max(l - px, 0.0, px - r), max(t - py, 0.0, py - b))
    if kind == "circle":
        _k, cx, cy, rad = shape
        return max(0.0, math.hypot(px - cx, py - cy) - rad)
    if kind == "seg":
        _k, x1, y1, x2, y2, hw = shape
        return max(0.0, _seg_point(px, py, x1, y1, x2, y2) - hw)
    raise ValueError("unknown shape %r" % (kind,))


def _shape_box(shape):
    kind = shape[0]
    if kind == "rect":
        return shape[1:5]
    if kind == "circle":
        _k, cx, cy, rad = shape
        return (cx - rad, cy - rad, cx + rad, cy + rad)
    _k, x1, y1, x2, y2, hw = shape
    return (min(x1, x2) - hw, min(y1, y2) - hw, max(x1, x2) + hw, max(y1, y2) + hw)


class Result:
    def __init__(self):
        self.routes = {}
        self.failed = []
        self.conflicts = 0
        self.iterations = 0


class Router:
    def __init__(self, bounds, pitch, layers, clearance, via_radius, via_cost=8.0):
        self.x0, self.y0, x1, y1 = bounds
        self.pitch, self.layers, self.clearance = pitch, layers, clearance
        self.via_radius, self.via_cost = via_radius, via_cost
        self.nx = int(math.floor((x1 - self.x0) / pitch + 1e-9)) + 1
        self.ny = int(math.floor((y1 - self.y0) / pitch + 1e-9)) + 1
        self.plane = self.nx * self.ny
        self.s = pitch * SQRT2 / 2.0
        self._obstacles = []
        self._nets = []
        self._discs = {}

    # --- input -------------------------------------------------------------
    def add_obstacle(self, net, layers, shape):
        self._obstacles.append((net, tuple(layers), shape))

    def add_net(self, name, half_width, terminals):
        self._nets.append((name, half_width, [(x, y, tuple(ls)) for x, y, ls in terminals]))

    # --- grid ----------------------------------------------------------------
    def _idx(self, layer, ix, iy):
        return layer * self.plane + iy * self.nx + ix

    def _split(self, idx):
        layer, rem = divmod(idx, self.plane)
        iy, ix = divmod(rem, self.nx)
        return layer, ix, iy

    def _xy(self, ix, iy):
        return (round(self.x0 + ix * self.pitch, 6), round(self.y0 + iy * self.pitch, 6))

    def _disc(self, radius):
        """Cell offsets (dx, dy) whose centre lies closer than `radius`."""
        key = round(radius, 9)
        if key not in self._discs:
            n = int(math.ceil(radius / self.pitch))
            self._discs[key] = [(dx, dy) for dy in range(-n, n + 1) for dx in range(-n, n + 1)
                                if math.hypot(dx, dy) * self.pitch < radius]
        return self._discs[key]

    def _rasterise(self, arr, nid, layers, shape, radius):
        l, t, r, b = _shape_box(shape)
        ix0 = max(0, int(math.floor((l - radius - self.x0) / self.pitch)))
        ix1 = min(self.nx - 1, int(math.ceil((r + radius - self.x0) / self.pitch)))
        iy0 = max(0, int(math.floor((t - radius - self.y0) / self.pitch)))
        iy1 = min(self.ny - 1, int(math.ceil((b + radius - self.y0) / self.pitch)))
        for iy in range(iy0, iy1 + 1):
            for ix in range(ix0, ix1 + 1):
                x, y = self._xy(ix, iy)
                if _shape_dist(x, y, shape) >= radius:
                    continue
                for layer in layers:
                    i = self._idx(layer, ix, iy)
                    cur = arr[i]
                    if nid == BLOCKED or (cur != FREE and cur != nid):
                        arr[i] = BLOCKED
                    elif cur == FREE:
                        arr[i] = nid

    # --- costs -----------------------------------------------------------------
    def _cost(self, c, i, nid, pres):
        st = self._static[c][i]
        if st == BLOCKED or (st != FREE and st != nid):
            return None
        return (1.0 + self._hist[i]) * (1.0 + pres * self._occ[c][i])

    def _via_step(self, ix, iy, nid, pres):
        v = self._via_c
        claims = 0
        for layer in range(self.layers):
            i = self._idx(layer, ix, iy)
            st = self._static[v][i]
            if st == BLOCKED or (st != FREE and st != nid):
                return None
            claims += self._occ[v][i]
        return self.via_cost * (1.0 + pres * claims)

    # --- A* ----------------------------------------------------------------------
    def _astar(self, sources, targets, target_xy, nid, c, pres):
        tix, tiy = target_xy
        targets = set(targets)

        def h(i):
            _l, ix, iy = self._split(i)
            dx, dy = abs(ix - tix), abs(iy - tiy)
            return (dx + dy) + (SQRT2 - 2.0) * min(dx, dy)

        g, prev, heap, n = {}, {}, [], 0
        for i in sorted(sources):
            g[i], prev[i] = 0.0, None
            heap.append((h(i), 0.0, n, i))
            n += 1
        heapq.heapify(heap)
        while heap:
            _f, gi, _n, i = heapq.heappop(heap)
            if gi > g[i]:
                continue
            if i in targets:
                path = []
                while i is not None:
                    path.append(i)
                    i = prev[i]
                return path[::-1]
            layer, ix, iy = self._split(i)
            steps = []
            for dx, dy, w in _DIRS:
                jx, jy = ix + dx, iy + dy
                if 0 <= jx < self.nx and 0 <= jy < self.ny:
                    j = self._idx(layer, jx, jy)
                    cost = self._cost(c, j, nid, pres)
                    if cost is not None:
                        steps.append((j, w * cost))
            if self.layers > 1:
                vc = self._via_step(ix, iy, nid, pres)
                if vc is not None:
                    for other in range(self.layers):
                        if other != layer:
                            steps.append((self._idx(other, ix, iy), vc))
            for j, w in steps:
                ng = gi + w
                if ng < g.get(j, _INF):
                    g[j], prev[j] = ng, i
                    heapq.heappush(heap, (ng + h(j), ng, n, j))
                    n += 1
        return None

    # --- one net ---------------------------------------------------------------
    def _terminal_cells(self, x, y, layers, nid, c):
        """The nearest cell with a usable centre, per layer of the pad."""
        cx = int(round((x - self.x0) / self.pitch))
        cy = int(round((y - self.y0) / self.pitch))
        out = []
        for layer in layers:
            best = None
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    ix, iy = cx + dx, cy + dy
                    if not (0 <= ix < self.nx and 0 <= iy < self.ny):
                        continue
                    i = self._idx(layer, ix, iy)
                    st = self._static[c][i]
                    if st == BLOCKED or (st != FREE and st != nid):
                        continue
                    px, py = self._xy(ix, iy)
                    key = (math.hypot(px - x, py - y), i)
                    if best is None or key < best:
                        best = key
            if best is not None:
                out.append(best[1])
        return out

    def _route_net(self, k, pres):
        name, hw, terms = self._nets[k]
        nid, c = k + 1, self._class_of[k]
        tcells = [self._terminal_cells(x, y, ls, nid, c) for x, y, ls in terms]
        if any(not tc for tc in tcells):
            return False, []
        tree, done, paths = set(tcells[0]), [0], []
        todo = list(range(1, len(terms)))
        while todo:
            j = min(todo, key=lambda t: (min(math.hypot(terms[t][0] - terms[d][0],
                                                        terms[t][1] - terms[d][1])
                                             for d in done), t))
            _l, tix, tiy = self._split(tcells[j][0])
            path = self._astar(tree, tcells[j], (tix, tiy), nid, c, pres)
            if path is None:
                return False, paths
            paths.append(path)
            tree.update(path)
            tree.update(tcells[j])
            done.append(j)
            todo.remove(j)
        self._tcells[k] = tcells
        return True, paths

    def _vias_of(self, paths):
        out = []
        for path in paths:
            for a, b in zip(path, path[1:]):
                la, ax, ay = self._split(a)
                lb, bx, by = self._split(b)
                if la != lb:
                    out.append((ax, ay))
        return out

    def _commit(self, k, paths):
        _name, hw, _t = self._nets[k]
        marks = [set() for _ in self._classes]
        for path in paths:
            for i in path:
                layer, ix, iy = self._split(i)
                for c2, hw2 in enumerate(self._classes):
                    extra = self.s if c2 == self._via_c else 2 * self.s
                    for dx, dy in self._disc(hw + self.clearance + hw2 + extra):
                        jx, jy = ix + dx, iy + dy
                        if 0 <= jx < self.nx and 0 <= jy < self.ny:
                            marks[c2].add(self._idx(layer, jx, jy))
        for ix, iy in self._vias_of(paths):
            for c2, hw2 in enumerate(self._classes):
                for dx, dy in self._disc(self.via_radius + self.clearance + hw2 + self.s):
                    jx, jy = ix + dx, iy + dy
                    if 0 <= jx < self.nx and 0 <= jy < self.ny:
                        for layer in range(self.layers):
                            marks[c2].add(self._idx(layer, jx, jy))
        for c2, m in enumerate(marks):
            occ = self._occ[c2]
            for i in m:
                occ[i] += 1
        self._marks[k], self._paths[k] = marks, paths

    def _rip(self, k):
        if self._marks[k] is None:
            return
        for c2, m in enumerate(self._marks[k]):
            occ = self._occ[c2]
            for i in m:
                occ[i] -= 1
        self._marks[k], self._paths[k] = None, []

    def _conflict_cells(self, k):
        """Cells where net k's copper sits on another net's claim."""
        if self._marks[k] is None:
            return []
        c, marks, out = self._class_of[k], self._marks[k], []
        for path in self._paths[k]:
            for i in path:
                if self._occ[c][i] - (1 if i in marks[c] else 0) > 0:
                    out.append(i)
        v = self._via_c
        for ix, iy in self._vias_of(self._paths[k]):
            for layer in range(self.layers):
                i = self._idx(layer, ix, iy)
                if self._occ[v][i] - (1 if i in marks[v] else 0) > 0:
                    out.append(i)
        return out

    # --- output ------------------------------------------------------------------
    def _geometry(self, k):
        _name, _hw, terms = self._nets[k]
        segments, used = [], set()
        for path in self._paths[k]:
            used.update(path)
            run = [path[0]]
            for a, b in zip(path, path[1:]):
                la, ax, ay = self._split(a)
                lb, bx, by = self._split(b)
                if la != lb:
                    segments += self._run_segments(run)
                    run = [b]
                    continue
                if len(run) >= 2:
                    _l0, px, py = self._split(run[-2])
                    _l1, qx, qy = self._split(run[-1])
                    if (qx - px, qy - py) != (bx - ax, by - ay):
                        segments += self._run_segments(run)
                        run = [run[-1]]
                run.append(b)
            segments += self._run_segments(run)
        for (x, y, _ls), cells in zip(terms, self._tcells[k]):
            for i in cells:
                if i in used:
                    layer, ix, iy = self._split(i)
                    cx, cy = self._xy(ix, iy)
                    if math.hypot(cx - x, cy - y) > 1e-6:
                        segments.append((layer, (x, y), (cx, cy)))
        vias = sorted(set(self._xy(ix, iy) for ix, iy in self._vias_of(self._paths[k])))
        return {"segments": segments, "vias": vias}

    def _run_segments(self, run):
        if len(run) < 2:
            return []
        la, ax, ay = self._split(run[0])
        _lb, bx, by = self._split(run[-1])
        return [(la, self._xy(ax, ay), self._xy(bx, by))]

    # --- the loop ----------------------------------------------------------------
    def run(self, max_iters=30, pres0=0.5, pres_mult=1.6, hist_inc=1.0):
        ids = {name: k + 1 for k, (name, _hw, _t) in enumerate(self._nets)}
        for net, _l, _s in self._obstacles:
            if net is not None and net not in ids:
                ids[net] = len(ids) + 1
        hws = sorted({hw for _n, hw, _t in self._nets})
        self._classes = hws + [self.via_radius]
        self._via_c = len(self._classes) - 1
        self._class_of = [hws.index(hw) for _n, hw, _t in self._nets]
        cells = self.layers * self.plane
        self._static = [array("i", [FREE]) * cells for _ in self._classes]
        self._occ = [array("i", [0]) * cells for _ in self._classes]
        self._hist = array("d", [0.0]) * cells
        for net, layers, shape in self._obstacles:
            nid = BLOCKED if net is None else ids[net]
            for c, hw in enumerate(self._classes):
                self._rasterise(self._static[c], nid, layers, shape, hw + self.clearance + self.s)
        n = len(self._nets)
        self._marks, self._paths, self._tcells = [None] * n, [[] for _ in range(n)], [[] for _ in range(n)]

        def span(k):
            ts = self._nets[k][2]
            xs, ys = [t[0] for t in ts], [t[1] for t in ts]
            return (max(xs) - min(xs) + max(ys) - min(ys), self._nets[k][0])

        order = sorted(range(n), key=span)
        res, failed, pres, todo = Result(), set(), pres0, list(order)
        for it in range(1, max_iters + 1):
            res.iterations = it
            for k in todo:
                self._rip(k)
                ok, paths = self._route_net(k, pres)
                if not ok:
                    failed.add(k)
                self._commit(k, paths)
            bad = [(k, self._conflict_cells(k)) for k in order if k not in failed]
            bad = [(k, cc) for k, cc in bad if cc]
            res.conflicts = len(bad)
            if not bad:
                break
            for _k, cc in bad:
                for i in cc:
                    self._hist[i] += hist_inc
            pres *= pres_mult
            todo = [k for k, _cc in bad]
        res.failed = sorted(self._nets[k][0] for k in failed)
        for k in order:
            if k not in failed:
                res.routes[self._nets[k][0]] = self._geometry(k)
        return res
```

- [ ] **Step 4: Run it green**

Run: `python hardware/gen/test_route.py`
Expected: every line `ok`, `all route checks passed`, exit 0. Paste the output into your report. If `crossing` or `detour` fails on distance, the fix is in the slack or the mark radius, never in the test's required distance.

- [ ] **Step 5: Prove a RED**

With the Edit tool, in `Router.run`, change `hw + self.clearance + self.s` in the `_rasterise` call to `hw` (obstacles no longer grown by the clearance).
Run: `python hardware/gen/test_route.py`
Expected: `FAIL detour: ...` with a distance below 0.325, exit 1. Revert with the Edit tool; rerun → exit 0.

- [ ] **Step 6: ctest and commit**

`CMakeLists.txt`, after `hw_gen_pcb_proof_guard`:
```cmake
add_test(NAME hw_gen_route_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_route.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```
Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `ctest --test-dir build -R hw_gen_route_guard --output-on-failure`
Expected: pass.

```bash
git add hardware/gen/route.py hardware/gen/test_route.py CMakeLists.txt
```
```bash
git commit -m "hw(gen): grid router core -- A*, clearance classes, negotiated congestion"
```

---

### Task 6: Our own router on the strip, 2 layers (time-boxed)

Spec §3.2, §3.4. Adapter from the strip board to `route.py` and back, the post-routing fill, and the iterations. **Budget:** the first complete run plus at most **six** further runs with a change in between. Allowed changes: `route.py` internals, the `Router(...)`/`run(...)` parameters in `own.py`, the net order. Not allowed: `stripe.py`, `locked.py`, `proof.py` — the input and the judge stay fixed for fairness. The seventh run's result is the method's result, green or not.

**The fast partial check (spec §3.2)** is the router's own conflict count (`conflicts`, `failed` in the `own router:` line), printed before the proof starts. A separate checker is built only if the full proof, timed in Task 2 step 5, takes longer than 30 s on this board — then it is a per-net DRC on a copy of the board holding only the nets just changed. Record the proof's duration in the log either way.

**Files:**
- Create: `hardware/reva/spike/own.py`
- Modify: `hardware/reva/spike/run.py`, `docs/hardware/routing-spike.md`
- Modify (only within the budget rules above): `hardware/gen/route.py`

**Interfaces:**
- Consumes: `route.Router`, `route.Result` (Task 5); `stripe.Strip`, `stripe.LOCKED_NETS`, `stripe.SUPPLY_NETS`, `stripe.W_SIGNAL`, `stripe.W_SUPPLY`, `stripe.CLEARANCE`, `stripe.X0..Y1` (Task 2); `pcb_proof.live_unconnected` (Task 1).
- Produces `own.route(strip) -> dict` — adds tracks and vias to `strip.board`, returns `{"nets": int, "failed": [..], "conflicts": int, "iterations": int, "vias": int, "length_mm": float, "seconds": float}`.
- Produces `run.finish(strip)` — records `strip.unrouted_before_fill`, adds the GND fill on 2 layers, fills zones.

- [ ] **Step 1: Write `hardware/reva/spike/own.py`**

```python
#!/usr/bin/env python3
"""The strip board -> gen.route -> the strip board (P4a spec §3.2).

Every pad becomes an obstacle owned by its net (netless pads -- pot tabs,
unused jack and key contacts -- block every net), every locked track a
segment obstacle, every keepout a netless rectangle. Every net with two or
more pads is routed except the locked nets, and on four layers the planes'
nets. Pads become terminals at their centre, on the copper layers they are on.
"""
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pcbnew          # noqa: E402
import stripe as ST    # noqa: E402
from gen import kipcb  # noqa: E402
from gen import route as GR  # noqa: E402  (not `route`: this module defines route())

PITCH = 0.2
VIA_RADIUS = 0.3        # kipcb.add_via: 0.6 mm
VIA_COST = 8.0
MAX_ITERS = 30
LAYER_NAMES = ("F.Cu", "B.Cu")   # signals on the outer layers; In1/In2 are planes on 4 layers


def _mm(v):
    return pcbnew.ToMM(v)


def route_strip(s):
    board = s.board
    edge = _mm(board.GetDesignSettings().m_CopperEdgeClearance)
    inset = edge + ST.W_SUPPLY / 2.0
    r = GR.Router((ST.X0 + inset, ST.Y0 + inset, ST.X1 - inset, ST.Y1 - inset),
                     PITCH, len(LAYER_NAMES), ST.CLEARANCE, VIA_RADIUS, VIA_COST)
    skip = set(ST.LOCKED_NETS) | (set(ST.SUPPLY_NETS) if s.layers == 4 else set())
    terminals = {}
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            layers = tuple(i for i, n in enumerate(LAYER_NAMES)
                           if pad.IsOnLayer(kipcb.LAYER[n]))
            bb = pad.GetBoundingBox()
            net = pad.GetNetname() or None
            r.add_obstacle(net, layers, ("rect", _mm(bb.GetLeft()), _mm(bb.GetTop()),
                                         _mm(bb.GetRight()), _mm(bb.GetBottom())))
            if net and net not in skip:
                terminals.setdefault(net, []).append(
                    (_mm(pad.GetPosition().x), _mm(pad.GetPosition().y), layers))
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            r.add_obstacle(t.GetNetname(), range(len(LAYER_NAMES)),
                           ("circle", _mm(t.GetPosition().x), _mm(t.GetPosition().y),
                            _mm(t.GetWidth()) / 2.0))
        elif t.Type() == pcbnew.PCB_TRACE_T and t.GetLayerName() in LAYER_NAMES:
            r.add_obstacle(t.GetNetname(), (LAYER_NAMES.index(t.GetLayerName()),),
                           ("seg", _mm(t.GetStart().x), _mm(t.GetStart().y),
                            _mm(t.GetEnd().x), _mm(t.GetEnd().y), _mm(t.GetWidth()) / 2.0))
    for _label, (l, t_, rr, b) in s.keepouts:
        r.add_obstacle(None, range(len(LAYER_NAMES)), ("rect", l, t_, rr, b))
    widths = {}
    for net in sorted(terminals):
        if len(terminals[net]) < 2:
            continue
        w = ST.W_SUPPLY if net in ST.SUPPLY_NETS else ST.W_SIGNAL
        widths[net] = w
        r.add_net(net, w / 2.0, terminals[net])
    return r, widths


def route(s):
    t0 = time.time()
    r, widths = route_strip(s)
    res = r.run(max_iters=MAX_ITERS)
    n_vias, length = 0, 0.0
    for net, geo in sorted(res.routes.items()):
        for layer, a, b in geo["segments"]:
            kipcb.add_track(s.board, LAYER_NAMES[layer], widths[net], net, [a, b])
            length += math.hypot(b[0] - a[0], b[1] - a[1])
        for xy in geo["vias"]:
            kipcb.add_via(s.board, xy, net)
            n_vias += 1
    return {"nets": len(widths), "failed": res.failed, "conflicts": res.conflicts,
            "iterations": res.iterations, "vias": n_vias,
            "length_mm": round(length, 1), "seconds": round(time.time() - t0, 1)}
```

- [ ] **Step 2: Wire routing into `run.py`**

Add `import own as OWN`, `from gen import pcb_proof as PP`, `import blocks as BL` is not needed — the fill net is `ST.SUPPLY_NETS[0]` (GND). Add:
```python
def finish(s):
    """After any routing method: record what it left unrouted, then the
    2-layer GND fill on both sides (spec §2.5), then fill all zones."""
    s.unrouted_before_fill = PP.live_unconnected(s.board)
    if s.layers == 2:
        rect = [(ST.X0, ST.Y0), (ST.X1, ST.Y0), (ST.X1, ST.Y1), (ST.X0, ST.Y1)]
        for layer in ("F.Cu", "B.Cu"):
            kipcb.add_zone(s.board, layer, ST.SUPPLY_NETS[0], rect)
        s.fill_nets = [ST.SUPPLY_NETS[0]]
    kipcb.fill_zones(s.board)
```
and between the `locked` print and the sabotage:
```python
    if a.method == "own":
        print("own router:", OWN.route(s))
    if routed:
        finish(s)
        print("unrouted before fill: %d" % s.unrouted_before_fill)
```

- [ ] **Step 3: First complete run**

Run: `KIPY hardware/reva/spike/run.py --method own --layers 2`
Record in your notes: the `own router:` dict (seconds, failed, conflicts, iterations, vias, length), every proof line, exit code. Read `out/own-2L-top.png` and `out/own-2L-bottom.png` and describe them (where the congestion is, where vias cluster, whether anything looks absurd — tracks hugging the cut, detours around the whole strip). The controller sends both PNGs to Bastian **before** step 4.

- [ ] **Step 4: Iterate within the budget**

Each iteration: one change (name it), rerun, record the same numbers, look at the PNGs. Stop at the first run where every gated step is green, or after the sixth change. Typical levers, in the order worth trying: `PITCH` 0.2 → 0.15 (finer channels, slower); `MAX_ITERS` up; `VIA_COST` down (4) if nets fail for lack of room on one layer; routing supply nets last instead of by span (change the `order` key in `route.py`); a rip-up of failed nets' neighbours. Do not touch the proof, the strip or the locked nets.

- [ ] **Step 5: The routed-only REDs**

On the last board (whatever its state), run with `--sabotage ratsnest` and `--sabotage audio_missing`.
Expected: `RED` on `ratsnest` and on `audio` respectively, exit 1. (If `ratsnest` was already red on the unsabotaged run, the RED proves nothing — say so in the log and prove it on the Freerouting board in Task 7 instead.)

- [ ] **Step 6: Reproducibility**

Run the final configuration twice, copying `out/own-2L.kicad_pcb` to the scratchpad in between.
Run: `cmp "$S/own-2L-first.kicad_pcb" hardware/reva/spike/out/own-2L.kicad_pcb`
Expected: no output (identical). Record the result either way.

- [ ] **Step 7: Log and commit**

Append `### Our own router, 2 layers (Task 6)` to the log: one table row per run (change, seconds, failed, conflicts, iterations, vias, length, unrouted before fill, gated reds), the final run's full proof output, the two RED lines, the `cmp` result, and two sentences on what the pictures showed. Copy the final `own-2L-top.png` and `own-2L-bottom.png` to `docs/hardware/routing-spike/`.

```bash
git add hardware/reva/spike/own.py hardware/reva/spike/run.py hardware/gen/route.py docs/hardware/routing-spike.md docs/hardware/routing-spike/own-2L-top.png docs/hardware/routing-spike/own-2L-bottom.png
```
```bash
git commit -m "hw(reva/spike): our own router on the SENSE_1 strip, 2 layers -- <green|N reds> after <k> runs"
```
(Fill in the result from the log.)

---

### Task 7: Freerouting on the strip, 2 layers (time-boxed)

Spec §3.3, §3.4. Same budget as Task 6: the first complete run plus at most six changed runs. Allowed changes: `FR_ARGS` (passes, threads, optimisation) and the DSN handling in `freerouting.py`. Not allowed: `stripe.py`, `locked.py`, `proof.py`.

**Files:**
- Create: `hardware/reva/spike/freerouting.py`
- Modify: `hardware/reva/spike/run.py`, `docs/hardware/routing-spike.md`

**Interfaces:**
- Consumes: Task 4's verified `FR_ARGS`, its lock-after-import finding; `stripe.Strip`; `kipcb.lock_tracks`.
- Produces `freerouting.route(strip, prefix) -> dict` — `{"seconds": float, "rc": int, "vias": int, "length_mm": float, "log": str}`; tracks and vias land on `strip.board`.

- [ ] **Step 1: Write `hardware/reva/spike/freerouting.py`**

```python
#!/usr/bin/env python3
"""The strip board -> Specctra DSN -> Freerouting -> SES -> the strip board
(P4a spec §3.3).

Freerouting and its Java runtime live outside the repository (Task 4):
%LOCALAPPDATA%/fireflow-tools/, or wherever FIREFLOW_JAVA and
FIREFLOW_FREEROUTING point. A missing one stops the run with its name.
Freerouting always runs with -da: its anonymous analytics stay off.
"""
import math
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pcbnew          # noqa: E402
import stripe as ST    # noqa: E402
from gen import kipcb  # noqa: E402

TOOLS = os.path.join(os.environ.get("LOCALAPPDATA", ""), "fireflow-tools")
JAVA = os.environ.get("FIREFLOW_JAVA", os.path.join(TOOLS, "jre", "bin", "java.exe"))
JAR = os.environ.get("FIREFLOW_FREEROUTING", os.path.join(TOOLS, "freerouting.jar"))
# Verified against `-help` in Task 4; change only with a probe.
FR_ARGS = ["--gui.enabled=false", "-da", "-mp", "100", "-mt", "1"]
RELOCK_AFTER_IMPORT = True   # Task 4 step 5, line 2: set to the probed answer
TIMEOUT_S = 3600


def route(s, prefix):
    for path, var in ((JAVA, "FIREFLOW_JAVA"), (JAR, "FIREFLOW_FREEROUTING")):
        if not os.path.exists(path):
            raise SystemExit("freerouting: %s not found (set %s)" % (path, var))
    dsn, ses, log = prefix + ".dsn", prefix + ".ses", prefix + "-freerouting.log"
    for p in (dsn, ses):
        if os.path.exists(p):
            os.remove(p)
    if not pcbnew.ExportSpecctraDSN(s.board, dsn):
        raise SystemExit("freerouting: DSN export failed")
    args = list(FR_ARGS)
    if s.layers == 4:
        args += ["-inc", "Supply"]     # GND and SM_3V3 are planes on 4 layers
    t0 = time.time()
    r = subprocess.run([JAVA, "-jar", JAR, "-de", dsn, "-do", ses] + args,
                       capture_output=True, text=True, timeout=TIMEOUT_S)
    seconds = round(time.time() - t0, 1)
    with open(log, "w", encoding="utf-8") as fh:
        fh.write(r.stdout + r.stderr)
    if not os.path.exists(ses):
        raise SystemExit("freerouting: no SES written (rc=%d), see %s" % (r.returncode, log))
    if not pcbnew.ImportSpecctraSES(s.board, ses):
        raise SystemExit("freerouting: SES import failed")
    if RELOCK_AFTER_IMPORT:
        kipcb.lock_tracks(s.board, set(ST.LOCKED_NETS))
    vias, length = 0, 0.0
    for t in s.board.GetTracks():
        if t.GetNetname() in ST.LOCKED_NETS:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            vias += 1
        elif t.Type() == pcbnew.PCB_TRACE_T:
            length += math.hypot(pcbnew.ToMM(t.GetEnd().x - t.GetStart().x),
                                 pcbnew.ToMM(t.GetEnd().y - t.GetStart().y))
    return {"seconds": seconds, "rc": r.returncode, "vias": vias,
            "length_mm": round(length, 1), "log": os.path.relpath(log)}
```
Set `FR_ARGS` and `RELOCK_AFTER_IMPORT` to what Task 4 measured before the first run.

- [ ] **Step 2: Wire it into `run.py`**

Add `import freerouting as FR` and, next to the `own` branch:
```python
    if a.method == "freerouting":
        print("freerouting:", FR.route(s, prefix))
```

- [ ] **Step 3: First complete run, then iterate within the budget**

Run: `KIPY hardware/reva/spike/run.py --method freerouting --layers 2`
Record the same numbers as Task 6 (seconds, vias, length, unrouted before fill, every proof line), read both PNGs, and the controller sends them to Bastian before the next change. Iterate as in Task 6 step 4 with the Freerouting levers only (`-mp`, `-mt`, optimisation on/off).

- [ ] **Step 4: REDs and reproducibility**

If Task 6 could not prove the `ratsnest` RED, prove it here. Run the final configuration twice and `cmp` the boards as in Task 6 step 6.

- [ ] **Step 5: Log and commit**

Append `### Freerouting, 2 layers (Task 7)` in the same shape as Task 6's section, including the Freerouting version and the exact command line (from the log file's first lines or from `FR_ARGS`). Copy the final PNGs to `docs/hardware/routing-spike/freerouting-2L-top.png` / `-bottom.png`.

```bash
git add hardware/reva/spike/freerouting.py hardware/reva/spike/run.py docs/hardware/routing-spike.md docs/hardware/routing-spike/freerouting-2L-top.png docs/hardware/routing-spike/freerouting-2L-bottom.png
```
```bash
git commit -m "hw(reva/spike): Freerouting on the SENSE_1 strip, 2 layers -- <green|N reds> after <k> runs"
```

- [ ] **Step 6: The winner (controller)**

The controller puts both Task 6 and Task 7 tables side by side and names the winner by these criteria, in this order: (1) all gated steps green; (2) fewer unrouted connections before the fill; (3) reproducible; (4) the picture — Bastian's reading of the renders; (5) routing time. A tie on (1)–(3) goes to Bastian with both picture pairs. Record the decision and its reason in the log; it decides Task 8's method.

---

### Task 8: The winner on 4 layers

Spec §2.5, §1 (layer runs). GND on In1.Cu and SM_3V3 on In2.Cu as planes, the SMD pads on them stitched by the coupon's via search — moved to `hardware/gen/` and taught to avoid locked tracks. Budget: first complete run plus at most three changed runs, same rules as Tasks 6/7.

**Files:**
- Create: `hardware/gen/stitch.py`
- Modify: `hardware/coupon/scripts/build_pcb.py` (lines 50–224 move out), `hardware/reva/spike/stripe.py`, `hardware/reva/spike/run.py`, `docs/hardware/routing-spike.md`

**Interfaces:**
- Produces `gen.stitch.stitch_plane_pads(board, plane_nets, segments=()) -> (dict[str, int], list[str])` — one via plus a 0.5 mm F.Cu track per SMD pad on a plane net (on the pad's own layer — see step 1); returns `({net: vias placed}, [unresolved lines])`. `segments = [(net, (x1, y1), (x2, y2), half_width)]` are extra obstacles (locked tracks); the coupon passes none.

- [ ] **Step 1: Move the stitching into `hardware/gen/stitch.py`**

Move from `build_pcb.py`, verbatim with their docstrings and comments: the keepout constants (`CLEARANCE_MM` … `STANDOFFS_MM`), `_pad_obstacles`, `_clear_of_pads`, `_clear_of_vias`, `_candidate_clear`, `_find_via_offset`, `_stitch_plane_pads`. Module header:
```python
#!/usr/bin/env python3
"""Plane stitching shared by the generated boards: one via and a short track
per SMD pad on a plane net, placed by a collision search. Moved from the
coupon's build_pcb.py (P4a); the docstrings keep the coupon's probe history.
Runs under KiCad's Python."""
import pcbnew

from gen import kipcb
from gen import pcb_proof as PP
```
Changes, and only these:
1. `_stitch_plane_pads(board)` becomes `stitch_plane_pads(board, plane_nets, segments=())`; inside it `PLANE_NETS` becomes `plane_nets`; it no longer prints — it returns `stitched, unresolved`, where `unresolved` is the list of the `"%s.%s [%s] at ..."` strings it used to print.
2. The stitching track goes on the pad's own copper layer: replace `kipcb.add_track(board, "F.Cu", 0.5, net, ...)` with
   ```python
            layer = "B.Cu" if pad.IsOnLayer(pcbnew.B_Cu) else "F.Cu"
            kipcb.add_track(board, layer, 0.5, net, [(px, py), (via_x, via_y)])
   ```
   (every coupon SMD pad is on F.Cu, so the coupon's board is unchanged; the strip's SMD parts are on the back).
3. `_find_via_offset(...)` and `_candidate_clear(...)` gain a trailing `segments=()` parameter, passed through; `_candidate_clear` adds, before its final `return`:
   ```python
    for snet, a, b, hw in segments:
        if snet == net:
            continue
        if PP.seg_point_dist((vx, vy), a, b) < hw + PAD_KEEPOUT_MM:
            return False
        if PP.seg_point_dist((mx, my), a, b) < hw + TRACK_KEEPOUT_MM:
            return False
   ```
   (compute `mx, my` before this block; the existing final `return _clear_of_pads(mx, my, ...)` stays last).

In `build_pcb.py`: `from gen import stitch as STITCH`, and in `build()` replace `stitched = _stitch_plane_pads(board)` with
```python
    stitched, unresolved = STITCH.stitch_plane_pads(board, PLANE_NETS)
    if unresolved:
        print("   UNRESOLVED via placements (collision search found nothing clear within 3.0 mm):")
        for line in unresolved:
            print("     " + line)
```
Run the coupon guard (Task 1 step 7): identical stdout to `$S/coupon-before-1.txt`, `git hash-object` = `9f7b8226245deb3c758f5edfaf8418e8a83097c9`, then `git restore` the churn.

- [ ] **Step 2: Planes and stitching in the strip**

In `stripe.build`, right after `new_board(...)`:
```python
    if layers == 4:
        rect = _rect((X0, Y0, X1, Y1))
        kipcb.add_zone(board, "In1.Cu", SUPPLY_NETS[0], rect)   # GND
        kipcb.add_zone(board, "In2.Cu", SUPPLY_NETS[1], rect)   # SM_3V3
```
In `run.py`, add `from gen import stitch as STITCH` and, right after the `locked` print:
```python
    if a.layers == 4:
        segs = [(g[0], (g[2], g[3]), (g[4], g[5]), g[6] / 2.0) for g in s.locked]
        stitched, unresolved = STITCH.stitch_plane_pads(s.board, set(ST.SUPPLY_NETS), segs)
        print("stitched %s; unresolved %d" % (stitched, len(unresolved)))
        for line in unresolved:
            print("   " + line)
```
Stitching vias are obstacles for both routers already (own.py reads every via; the DSN carries them).

- [ ] **Step 3: Run the winner on 4 layers, iterate within the budget**

Run: `KIPY hardware/reva/spike/run.py --method <winner> --layers 4`
Record the same numbers as Task 6/7 plus the `stitched`/`unresolved` line; read both PNGs; the controller sends them to Bastian. An unresolved stitch is a finding for the log, not something to hide by moving parts.

- [ ] **Step 4: Log and commit**

Append `### <winner>, 4 layers (Task 8)`, same shape; copy `<winner>-4L-top.png` / `-bottom.png` to `docs/hardware/routing-spike/`.

```bash
git add hardware/gen/stitch.py hardware/coupon/scripts/build_pcb.py hardware/reva/spike/stripe.py hardware/reva/spike/run.py docs/hardware/routing-spike.md docs/hardware/routing-spike/
```
```bash
git commit -m "hw(gen): plane stitching shared; the P4a winner on 4 layers"
```

---

### Task 9: Prices, the report, and the recommendation (controller)

Spec §4.3, §4.4. Controller work: the price lookup uses the browser, and the recommendation is argued from the whole log.

**Files:**
- Modify: `docs/hardware/routing-spike.md`, `docs/roadmap.md`

- [ ] **Step 1: JLC prices**

In the browser pane open `https://jlcpcb.com/quote`. Decline non-essential cookies. Enter, without logging in and without adding anything to a cart: FR-4, 2 layers, 295 × 116 mm, quantity 5, 1.6 mm, everything else at its default. Note the board price and build time. Change to 4 layers, same otherwise; note again. Record both with today's date and the exact inputs under `### JLC prices (Task 9)` in the log. Assembly is left out (spec §4.3).

- [ ] **Step 2: Write the report above the log**

Replace the `*Status: measuring...*` line with these sections, every number taken from the log below (link to the log section it came from):
- **Question** — the two P4 decisions this answers (routing method; input to 2 vs 4 layers).
- **Setup** — the strip (outline, parts, keepouts, ports, locked nets) in five lines.
- **Results** — one table, one column per run (own 2L, Freerouting 2L, winner 4L): gated reds, unrouted before fill, vias, track length, routing seconds, runs to green, agent time to the first green run (the controller's task start/finish times), reproducible, audio distance.
- **Pictures** — the six committed PNGs, each with a one-line caption saying what to look at.
- **Prices** — the two JLC numbers and their difference per board.
- **Findings for P4** — at least: how the audio rule could be given to each method (Task 4's `class_class` result, and for ours a per-net-pair clearance); what the SMD placement search did well or badly; whether the port column approach carries to the full board; anything the renders showed that no check caught.
- **Recommendation** — routing method, and 2 vs 4 layers with the price. Mark it as a recommendation: Bastian decides (working rule 9).
- **What stays** — `hardware/gen/kipcb.py`, `pcb_proof.py`, `stitch.py` stay; `route.py` stays if ours wins, otherwise it goes to the tag `attic/route-spike-2026-10` after Bastian's decision; `hardware/reva/spike/` goes when P4 starts.

- [ ] **Step 3: Roadmap**

In `docs/roadmap.md`, in the Rev A section, add one dated line: "2026-MM-DD — P4a routing spike done: <recommendation in one clause>; report `docs/hardware/routing-spike.md`." (Use the actual date.)

- [ ] **Step 4: Verify every number**

For each number in the report's upper sections, find it in the log below with its command. Any number without a source is removed or re-measured. Run `ctest --test-dir build -R "hw_gen|reva|coupon" --output-on-failure` once more — all green.

- [ ] **Step 5: Commit, and hand over**

```bash
git add docs/hardware/routing-spike.md docs/roadmap.md
```
```bash
git commit -m "docs(routing-spike): report and recommendation for P4"
```
Send Bastian the report's link and the six PNGs, with the recommendation in two sentences. The branch is not merged until he decides; if Freerouting is chosen, removing `route.py` (with the attic tag) is a follow-up commit on this branch.
