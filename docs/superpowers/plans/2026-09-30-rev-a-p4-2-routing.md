# Rev A P4-2 Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Route the placed Rev A board completely with our own router on 4 layers, with the audio, L/R, SENSE and pot-keepout rules enforced by the router and gated by checks that can each go red, and commit the routed board byte-stable.

**Architecture:** `hardware/gen/route.py` learns hard per-net-pair clearances with exemption zones, priority tiers and per-net stats (all off by default, so the existing guard stays byte-identical). A new adapter `hardware/reva/route.py` feeds it P4-1's placed board (`place.build()` in memory), stitches, routes, fills and saves. A new `hardware/reva/route_check.py` measures every rule on the saved board in P4-1's pattern (steps, known list, sabotages with `*_missing`), sharing the DRC parser and the step runner with `place_check.py` through a new `hardware/reva/check_kit.py`. `hardware/reva/rules.py` is the one source of the board rules, including the ones P3's `build.py` writes into the committed `reva.kicad_pro`.

**Tech Stack:** Python 3.11 under KiCad 10.0.5 (`pcbnew`), kicad-cli for DRC and renders, plain-script guards wired into ctest.

**Spec:** `docs/superpowers/specs/2026-09-30-rev-a-p4-2-routing-design.md`

## Global Constraints

- Units mm, y down; board coordinates equal panel coordinates. Outline x 2.0–302.8, y 9.25–119.25. 4 copper layers: signals on F.Cu and B.Cu, In1.Cu GND plane, In2.Cu SM_3V3 plane.
- Signal track 0.25 mm; supply track 0.5 mm for `+12V`, `-12V`, `+12V_IN`, `-12V_IN`, `3V3D`; via 0.6 / 0.3 mm; clearance 0.2 mm; copper to edge 0.5 mm; grid pitch 0.2 mm; via cost 8.0; 30 rounds.
- Victims: `OUT_L`, `OUT_R`, `IN_L`, `IN_R`. Aggressors (48): `LED<n>` and `LED<n>_A` for n = 0..18, `SR_CLK`, `SR_DATA`, `SR_LATCH`, `SR_DIN`, `SD_CK`, `SD_CMD`, `SD_D0`, `SD_D1`, `SD_D2`, `SD_D3`.
- Audio × aggressor ≥ 10.0 mm, L × R (`OUT_L`/`OUT_R`, `IN_L`/`IN_R`) ≥ 2.0 mm, both edge to edge, same layer only, measured only outside the module exemption zones.
- Exemption zones: U_SM's pads clustered at 2.54 mm (four groups today), each group's box of pad centres grown by 2.54 mm.
- SENSE_0..3: routed first (tier 0), each ≤ 1.3 × the straight-line MST of its pads. Audio is tier 1, everything else tier 2.
- No F.Cu copper (track or via) inside a pot's body box (`gen/place.body_box`).
- `KNOWN_PANEL` names only the 18 jacks at y 114.0, `SONG_A`, `SONG_B`, `SONG_A_L`, `SONG_B_L`, and the pairs `GATE_A_L`/`SOURCE_A`, `LVL_B_L`/`PAN_B` (each only with its own partner). A connection between two non-panel pads is never listable. A listed item that no longer fails is red.
- Checks own their thresholds and net sets; `route_check.py` imports no limit from `rules.py`, `route.py` or `gen/route.py`. Every gated step has a sabotage and a `<step>_missing` sabotage, and each sabotage carries a phrase only its failure prints. A step that examined nothing is red.
- The coupon (`coupon_*` ctest) and `hw_gen_route_guard`'s existing cases stay byte-identical. `kipcb.new_board()` defaults and `gen/check.write_project_file()`'s default output do not change. The panel generator and the hole list are not touched.
- Findings are not the implementer's to paper over: a rule the router cannot meet, a runtime above 5 minutes, an unreachable J_SD pin, or a known-list entry outside the allowed names is reported verbatim and the task STOPS. The only knobs the implementer may tune alone are via cost and round count.
- Tooling: KIPY = `/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe`. System `python` has no pcbnew. Never prefix a shell command with `cd`; no shell writes to repo files (use Edit/Write); no write behind `&&`. Long compounds go into a scratchpad script. ctest: `source env.sh; cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; ctest --test-dir build -R "hw_gen|reva|coupon" --output-on-failure`, run AFTER committing.
- pcbnew traps: `board.Remove` corrupts the next save (use `board.Delete`); `PCB_VIA.GetWidth()` needs a layer (`GetWidth(pcbnew.F_Cu)`); a flipped footprint's pads report F.Cu from `GetLayerName()` (use `IsOnLayer`); copper landing on a foreign pad is renamed to that net on save; `kipcb.new_board()` seeds the UUIDs and must be the first pcbnew object; `SaveBoard` writes a `.kicad_pro` beside the board.
- Everything written into the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`. Do not commit `hardware/reva/bom-jlc.csv` (line-ending noise) or anything in `hardware/reva/out/`.

---

## File map

| File | Status | Responsibility |
|---|---|---|
| `hardware/gen/route.py` | modify | pair clearances, exemption zones, tiers, per-net stats |
| `hardware/gen/test_route.py` | modify | unit tests for the above, plus a byte-identity pin of the old behaviour |
| `hardware/reva/rules.py` | create | every board rule value, the net sets, the project-file rules block |
| `hardware/gen/check.py` | modify | `write_project_file(..., extra=None)`; default output unchanged |
| `hardware/reva/build.py` | modify | writes `reva.kicad_pro` with the rules block |
| `hardware/reva/check_kit.py` | create | DRC report parser and summary, known-list judge, step runner, key-name rule, MST |
| `hardware/reva/test_check_kit.py` | create | unit tests for check_kit (plain script) |
| `hardware/reva/place_check.py` | modify | imports the moved pieces from check_kit |
| `hardware/reva/test_place.py` | modify | imports key-name rule from check_kit; stops comparing the committed board (Task 8) |
| `hardware/reva/place.py` | modify | reads rule values from rules.py; `--write` stops copying the board (Task 8) |
| `hardware/reva/route.py` | create | the adapter: placed board → router → routed, stitched, filled board |
| `hardware/reva/route_check.py` | create | the checks of spec §5, known list, sabotages |
| `hardware/reva/test_route.py` | create | the guard `reva_route_guard` |
| `hardware/reva/test_build.py` | modify | the orphan exemption names route.py as owner |
| `hardware/reva/kicad/reva.kicad_pcb` | regenerate | the routed board |
| `hardware/reva/kicad/reva.kicad_pro` | regenerate | now carries the board rules |
| `CMakeLists.txt` | modify | `reva_check_kit_guard`, `reva_route_guard` |
| `docs/hardware/routing/` | create | renders of the first green run |
| `docs/roadmap.md`, `docs/hardware/grip-test.md` | modify | entry; freeze sentence |

---

### Task 1: Probes before code

Three facts the plan cannot know. No production code; every probe is a scratchpad script, and the results go into the spec as a dated amendment of §3.

**Files:**
- Create (scratchpad, not committed): `probe_full_route.py`, `probe_jsd.py`, `probe_pro_rules.py`
- Modify: `docs/superpowers/specs/2026-09-30-rev-a-p4-2-routing-design.md` (§3, one bullet per probe, marked "Probed during execution, 2026-MM-DD")

**Interfaces:**
- Consumes: `place.build()`, `gen.route.Router`, `gen.stitch.stitch_plane_pads`, `gen.pcb_proof.drc`.
- Produces: three numbers/facts later tasks read from the spec: full-board runtime and failures with the unmodified router; whether every J_SD pad has a terminal cell at pitch 0.2; which `.kicad_pro` JSON keys kicad-cli honours for the rules.

- [ ] **Step 1: Full-board runtime with the unmodified router**

Write `probe_full_route.py` in the scratchpad. It is the P4a adapter (`git show 359bf662:hardware/reva/spike/own.py`) applied to the whole placed board:

```python
"""P4-2 Task 1 probe 1: the unmodified router on the whole placed board.
Prints nets, failed, conflicts, iterations, vias, length, seconds."""
import math, sys, time
sys.path[:0] = [r"C:\Users\bernd\Documents\AI\FireFlow\hardware",
                r"C:\Users\bernd\Documents\AI\FireFlow\hardware\reva"]
import pcbnew
import place as P
from gen import kipcb, stitch
from gen import route as GR

LAYERS = ("F.Cu", "B.Cu")
SUPPLY_TRACK = {"+12V", "-12V", "+12V_IN", "-12V_IN", "3V3D"}
PLANE_NETS = {"GND", "SM_3V3"}


def mm(v):
    return pcbnew.ToMM(v)


s = P.build()
b = s.board
stitched, unresolved = stitch.stitch_plane_pads(b, PLANE_NETS, netless_blocks=True)
print("stitched", stitched, "unresolved", len(unresolved))
inset = 0.5 + 0.25
r = GR.Router((P.X0 + inset, P.Y0 + inset, P.X1 - inset, P.Y1 - inset), 0.2, 2, 0.2, 0.3, 8.0)
terms = {}
for fp in b.GetFootprints():
    for pad in fp.Pads():
        ls = tuple(i for i, n in enumerate(LAYERS) if pad.IsOnLayer(kipcb.LAYER[n]))
        bb = pad.GetBoundingBox()
        net = pad.GetNetname() or None
        r.add_obstacle(net, ls, ("rect", mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom())))
        if net and net not in PLANE_NETS:
            terms.setdefault(net, []).append((mm(pad.GetPosition().x), mm(pad.GetPosition().y), ls))
for t in b.GetTracks():
    if t.Type() == pcbnew.PCB_VIA_T:
        r.add_obstacle(t.GetNetname(), range(2), ("circle", mm(t.GetPosition().x), mm(t.GetPosition().y),
                                                  mm(t.GetWidth(pcbnew.F_Cu)) / 2.0))
    elif t.GetLayerName() in LAYERS:
        r.add_obstacle(t.GetNetname(), (LAYERS.index(t.GetLayerName()),),
                       ("seg", mm(t.GetStart().x), mm(t.GetStart().y), mm(t.GetEnd().x), mm(t.GetEnd().y),
                        mm(t.GetWidth()) / 2.0))
for fp in b.GetFootprints():
    for z in fp.Zones():
        if z.GetIsRuleArea() and z.GetDoNotAllowTracks():
            ls = tuple(i for i, n in enumerate(LAYERS) if z.IsOnLayer(kipcb.LAYER[n]))
            bb = z.GetBoundingBox()
            r.add_obstacle(None, ls, ("rect", mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom())))
n = 0
for net in sorted(terms):
    if len(terms[net]) >= 2:
        r.add_net(net, (0.5 if net in SUPPLY_TRACK else 0.25) / 2.0, terms[net])
        n += 1
t0 = time.time()
res = r.run(max_iters=30)
sec = time.time() - t0
length = sum(math.hypot(q[0] - p[0], q[1] - p[1]) for g in res.routes.values() for _l, p, q in g["segments"])
vias = sum(len(g["vias"]) for g in res.routes.values())
print("nets %d failed %d %s conflicts %d iterations %d vias %d length %.1f mm seconds %.1f"
      % (n, len(res.failed), res.failed[:20], res.conflicts, res.iterations, vias, length, sec))
```

Run: `KIPY <scratchpad>/probe_full_route.py` (timeout 1800 s, run in the background if needed).
Record the whole last line. If `seconds` exceeds 300, STOP and report it: that is a finding for Bastian (spec §7), and no later task starts.

- [ ] **Step 2: J_SD terminal reachability**

Write `probe_jsd.py`: build the same router input as Step 1 (import it as a function or copy the adapter part), then call the router's terminal search for each J_SD pad without routing:

```python
# after r = ... and all add_obstacle/add_net calls of probe 1, before run():
r._classes = sorted({hw for _n, hw, _t in r._nets}) + [r.via_radius]
r._via_c = len(r._classes) - 1
cells = r.layers * r.plane
from array import array
r._static = [array("i", [GR.FREE]) * cells for _ in r._classes]
ids = {name: k + 1 for k, (name, _hw, _t) in enumerate(r._nets)}
for net, _l, _s in r._obstacles:
    if net is not None and net not in ids:
        ids[net] = len(ids) + 1
for net, layers, shape in r._obstacles:
    nid = GR.BLOCKED if net is None else ids[net]
    for c, hw in enumerate(r._classes):
        r._rasterise(r._static[c], nid, layers, shape, hw + r.clearance + r.s)
jsd = b.FindFootprintByReference("J_SD")
for pad in sorted(jsd.Pads(), key=lambda p: str(p.GetNumber())):
    net = pad.GetNetname()
    if not net or net in PLANE_NETS:
        continue
    k = [i for i, (nm, _h, _t) in enumerate(r._nets) if nm == net][0]
    c = r._classes.index(r._nets[k][1])
    ls = tuple(i for i, n in enumerate(LAYERS) if pad.IsOnLayer(kipcb.LAYER[n]))
    cells_ = r._terminal_cells(mm(pad.GetPosition().x), mm(pad.GetPosition().y), ls, k + 1, c)
    print("J_SD.%s %s: %d terminal cells" % (pad.GetNumber(), net, len(cells_)))
```

Expected: every J_SD signal pad has ≥ 1 terminal cell. If any has 0, STOP and report: spec §4.2.5's finer terminal stub then needs its own task, planned by the controller.

- [ ] **Step 3: Which `.kicad_pro` keys kicad-cli honours**

Write `probe_pro_rules.py`:
1. Load the committed P4-1 board, set `m_TrackMinWidth` 0.25, `m_ViasMinSize` 0.6, `m_MinThroughDrill` 0.3, `m_MinClearance` 0.2, `m_CopperEdgeClearance` 0.5, and call `kipcb.set_netclasses(board, 0.25, {"supply": (0.5, ["+12V", "-12V", "+12V_IN", "-12V_IN", "3V3D"])})`. Save to `<tmp>/a/reva.kicad_pcb`. SaveBoard writes `<tmp>/a/reva.kicad_pro`. Print that file's `board.design_settings.rules` and `net_settings` sections (json).
2. Add one 0.2 mm wide F.Cu track on net `3V3D` somewhere clear (e.g. from (10, 15) to (20, 15)) before saving, so `track_width` can fire against the 0.25 minimum.
3. Copy the `.kicad_pcb` into `<tmp>/b/`, `<tmp>/c/`, `<tmp>/d/` and write beside each a hand-built pro:
   - `b`: only `{"meta": {...}}` (today's committed form);
   - `c`: `meta` + `board.design_settings.rules` copied from `a`;
   - `d`: `meta` + `board.design_settings.rules` + `net_settings` copied from `a`.
4. Run `PP.drc` on each and print per class counts (`track_width`, `clearance`, `copper_edge_clearance`).

Expected: `a` and `d` report the same counts; `b` does not flag the 0.2 mm track under the netclass. Record which minimal key set (`c` or `d`) equals `a`. That set is what Task 4's `rules.project_rules()` writes.

- [ ] **Step 4: Amend the spec §3 with the three results**

Add three bullets under §3, each headed "Probed during execution (Task 1, <date>)": full-board runtime line; J_SD terminal cells per pad; the minimal pro key set, with the JSON key paths.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/2026-09-30-rev-a-p4-2-routing-design.md
git commit -m "docs(spec): P4-2 probes -- full-board runtime, J_SD terminals, project-file rule keys

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: Router — hard pair clearances and exemption zones

**Files:**
- Modify: `hardware/gen/route.py`
- Modify: `hardware/gen/test_route.py`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `Router.add_net(name, half_width, terminals, group=None)` (Task 3 adds `tier=0`)
  - `Router.add_obstacle(net, layers, shape, group=None)` — `group=None` means "the group of `net`, if any"
  - `Router.pair_clearance(group_a, group_b, mm)`
  - `Router.pair_exempt(rect)` with `rect = (left, top, right, bottom)` in mm
  - Semantics: a cell is forbidden to a net of group G if copper of a group paired with G lies within the pair distance on that layer. Copper whose position lies inside an exemption rect marks nothing, and cells inside an exemption rect are never marked. Hard: never a cost.

- [ ] **Step 1: Pin today's behaviour byte for byte**

Add to `hardware/gen/test_route.py`, above `if __name__`:

```python
import hashlib


def _digest(routes):
    return hashlib.sha256(repr(sorted(routes.items())).encode()).hexdigest()


def _pin_scenario():
    """A board that exercises negotiation, vias and a three-terminal tree at
    once; its output is pinned so that every later change to the router must
    leave the default path byte-identical."""
    r = R.Router((0, 0, 12, 10), 0.2, 2, CLR, 0.3)
    for rect in ((5.0, 0.0, 6.0, 2.0), (5.0, 3.2, 6.0, 6.6), (5.0, 7.8, 6.0, 10.0)):
        r.add_obstacle(None, (0,), ("rect",) + rect)
    r.add_obstacle("P", (0, 1), ("circle", 8.0, 5.0, 0.4))
    r.add_net("A", 0.125, [(1.0, 2.0, (0,)), (11.0, 2.0, (0,))])
    r.add_net("B", 0.125, [(1.0, 3.4, (0,)), (11.0, 3.4, (0,))])
    r.add_net("C", 0.25, [(6.0, 1.0, (0, 1)), (6.0, 9.0, (0, 1)), (1.0, 9.0, (1,))])
    return r.run()


PINNED = "<fill in from Step 2>"


def test_defaults_unchanged():
    res = _pin_scenario()
    d = _digest(res.routes)
    print("    pin digest", d)
    check(d == PINNED, "defaults: routes byte-identical to the pinned router output")
```

and add `test_defaults_unchanged` to the tuple in `__main__`.

- [ ] **Step 2: Record the pin on the unmodified router**

Run: `python hardware/gen/test_route.py` (system python is fine: no pcbnew).
Expected: every old test ok, `test_defaults_unchanged` FAIL, and a printed `pin digest <hex>`. Paste that hex into `PINNED`. Run again: all ok. This is the only time the pin may be written.

- [ ] **Step 3: Write the failing pair tests**

Add to `test_route.py`:

```python
def _min_same_layer(res, n1, n2):
    """Edge-free centre-line distance between two nets' same-layer segments."""
    ds = [seg_seg((a1, b1), (a2, b2))
          for l1, a1, b1 in res.routes.get(n1, {"segments": []})["segments"]
          for l2, a2, b2 in res.routes.get(n2, {"segments": []})["segments"] if l1 == l2]
    return min(ds) if ds else None


def _pair_board(rule=None, exempt=None):
    """A straight at y 6; B from (1, 12) to (29, 12) with a netless block at
    x 10-20, y 11-18. Under the block is shorter for B but passes ~4.3 mm
    from A's centre line; over it is far."""
    r = R.Router((0, 0, 30, 20), 0.2, 1, CLR, 0.3)
    r.add_obstacle(None, (0,), ("rect", 10.0, 11.0, 20.0, 18.0))
    r.add_net("A", 0.125, [(1.0, 6.0, (0,)), (29.0, 6.0, (0,))], group="victim")
    r.add_net("B", 0.125, [(1.0, 12.0, (0,)), (29.0, 12.0, (0,))], group="aggr")
    if rule is not None:
        r.pair_clearance("victim", "aggr", rule)
    if exempt is not None:
        r.pair_exempt(exempt)
    return r.run()


def test_pair_binding():
    """Without the rule B passes under the block, closer than 5 mm edge to
    edge: the test board really tests something."""
    res = _pair_board()
    d = _min_same_layer(res, "A", "B")
    check(not res.failed and d is not None and d - 0.25 < 5.0,
          "pair binding: without a rule A-B edge %.3f mm (< 5.0)" % ((d or 0) - 0.25))


def test_pair_keeps_apart():
    res = _pair_board(rule=5.0)
    d = _min_same_layer(res, "A", "B")
    check(not res.failed and res.conflicts == 0, "pair: both routed (%s)" % res.failed)
    check(d is not None and d - 0.25 >= 5.0 - 1e-6,
          "pair: A-B edge %.3f mm (need 5.0)" % ((d or 0) - 0.25))


def test_pair_exempt():
    """An exemption rect over the passage under the block lifts the rule
    there: B goes under again, and every B point closer than 5 mm to A lies
    inside the rect."""
    rect = (9.0, 5.0, 21.0, 12.0)
    res = _pair_board(rule=5.0, exempt=rect)
    check(not res.failed, "exempt: B routed (%s)" % res.failed)
    close = []
    for _l, p, q in res.routes.get("B", {"segments": []})["segments"]:
        for t in (i / 20.0 for i in range(21)):
            x, y = p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])
            if abs(y - 6.0) - 0.25 < 5.0 - 1e-6:
                close.append((x, y))
    check(close, "exempt: B passes closer than 5 mm somewhere (%d points)" % len(close))
    check(all(rect[0] <= x <= rect[2] and rect[1] <= y <= rect[3] for x, y in close),
          "exempt: every close B point lies inside the exemption rect")


def test_pair_static_pad():
    """A static pad of the victim group, 1 mm off B's straight line, pushes B
    away to at least the rule distance."""
    r = R.Router((0, 0, 30, 20), 0.2, 1, CLR, 0.3)
    r.add_obstacle("V", (0,), ("rect", 14.5, 8.5, 15.5, 9.5), group="victim")
    r.add_net("B", 0.125, [(1.0, 10.5, (0,)), (29.0, 10.5, (0,))], group="aggr")
    r.pair_clearance("victim", "aggr", 3.0)
    res = r.run()
    worst = min((seg_rect((p, q), (14.5, 8.5, 15.5, 9.5))
                 for _l, p, q in res.routes.get("B", {"segments": []})["segments"]), default=None)
    check(not res.failed and worst is not None and worst - 0.125 >= 3.0 - 1e-6,
          "static pad: B edge %.3f mm from the victim pad (need 3.0)" % ((worst or 0) - 0.125))


def test_pair_other_layer():
    """The rule is per layer: B crosses A on the other layer without a via."""
    r = R.Router((0, 0, 20, 20), 0.2, 2, CLR, 0.3)
    r.add_net("A", 0.125, [(1.0, 10.0, (0,)), (19.0, 10.0, (0,))], group="victim")
    r.add_net("B", 0.125, [(10.0, 1.0, (1,)), (10.0, 19.0, (1,))], group="aggr")
    r.pair_clearance("victim", "aggr", 5.0)
    res = r.run()
    b = res.routes.get("B", {"segments": [], "vias": []})
    check(not res.failed and not b["vias"] and all(l == 1 for l, _p, _q in b["segments"]),
          "other layer: B routed on layer 1 only, no via (%s)" % res.failed)
```

Add all five to the `__main__` tuple. Run: `python hardware/gen/test_route.py`.
Expected: FAIL (TypeError: unexpected keyword `group`).

- [ ] **Step 4: Implement**

In `hardware/gen/route.py`:

1. Docstring: under "Known limits" add nothing; add a section after "Negotiated congestion":

```
Pair clearances (P4-2 spec §4.2): nets and obstacles may carry a group;
`pair_clearance(A, B, mm)` keeps copper of A at least `mm` from copper of B
on the same layer. It is hard: a cell within reach of a paired group's copper
is forbidden like foreign static copper, never priced. Routed copper marks
its straight runs as capsules (radius own half-width + mm + target
half-width + 2s, s for the via class), vias as discs on every layer. Copper
inside a `pair_exempt` rect marks nothing, and no cell inside one is ever
marked.
```

2. `__init__` gains:

```python
        self._groups = {}          # net -> group
        self._ogroups = []         # per obstacle: explicit group or None
        self._pairs = []           # (group_a, group_b, mm)
        self._exempt = []          # (left, top, right, bottom)
        self._cur_pair = None
```

3. Input methods:

```python
    def add_obstacle(self, net, layers, shape, group=None):
        self._obstacles.append((net, tuple(layers), shape))
        self._ogroups.append(group)

    def add_net(self, name, half_width, terminals, group=None):
        self._nets.append((name, half_width, [(x, y, tuple(ls)) for x, y, ls in terminals]))
        if group is not None:
            self._groups[name] = group

    def pair_clearance(self, group_a, group_b, mm):
        self._pairs.append((group_a, group_b, float(mm)))

    def pair_exempt(self, rect):
        self._exempt.append(tuple(float(v) for v in rect))
```

4. Geometry helpers (module level, after `_shape_box`):

```python
def _clip_outside(a, b, rects):
    """The parts of segment a-b outside every rect, as [(p, q)]."""
    spans = []
    (ax, ay), (bx, by) = a, b
    dx, dy = bx - ax, by - ay
    for l, t, r, btm in rects:
        t0, t1 = 0.0, 1.0
        ok = True
        for p, q in ((-dx, ax - l), (dx, r - ax), (-dy, ay - t), (dy, btm - ay)):
            if p == 0:
                if q < 0:
                    ok = False
                    break
                continue
            u = q / p
            if p < 0:
                t0 = max(t0, u)
            else:
                t1 = min(t1, u)
        if ok and t0 < t1:
            spans.append((t0, t1))
    spans.sort()
    out, cur = [], 0.0
    for s0, s1 in spans:
        if s0 > cur:
            out.append((cur, s0))
        cur = max(cur, s1)
    if cur < 1.0:
        out.append((cur, 1.0))
    return [((ax + u0 * dx, ay + u0 * dy), (ax + u1 * dx, ay + u1 * dy))
            for u0, u1 in out if u1 - u0 > 1e-9]


def _inside_any(x, y, rects):
    return any(l <= x <= r and t <= y <= b for l, t, r, b in rects)
```

5. Grid helpers in `Router`:

```python
    def _capsule_cells(self, out, layers, a, b, radius):
        """Add to `out` every cell index (on each layer) whose centre lies
        within `radius` of segment a-b and outside every exemption rect."""
        shape = ("seg", a[0], a[1], b[0], b[1], 0.0)
        l, t, r, btm = _shape_box(shape)
        ix0 = max(0, int(math.floor((l - radius - self.x0) / self.pitch)))
        ix1 = min(self.nx - 1, int(math.ceil((r + radius - self.x0) / self.pitch)))
        iy0 = max(0, int(math.floor((t - radius - self.y0) / self.pitch)))
        iy1 = min(self.ny - 1, int(math.ceil((btm + radius - self.y0) / self.pitch)))
        for iy in range(iy0, iy1 + 1):
            for ix in range(ix0, ix1 + 1):
                x, y = self._xy(ix, iy)
                if _shape_dist(x, y, shape) >= radius or _inside_any(x, y, self._exempt):
                    continue
                for layer in layers:
                    out.add(self._idx(layer, ix, iy))

    def _shape_cells(self, out, layers, shape, radius):
        """Static obstacles: skipped entirely if their centre lies inside an
        exemption rect; otherwise every cell within `radius` of the shape,
        outside the rects."""
        l, t, r, btm = _shape_box(shape)
        if _inside_any((l + r) / 2.0, (t + btm) / 2.0, self._exempt):
            return
        ix0 = max(0, int(math.floor((l - radius - self.x0) / self.pitch)))
        ix1 = min(self.nx - 1, int(math.ceil((r + radius - self.x0) / self.pitch)))
        iy0 = max(0, int(math.floor((t - radius - self.y0) / self.pitch)))
        iy1 = min(self.ny - 1, int(math.ceil((btm + radius - self.y0) / self.pitch)))
        for iy in range(iy0, iy1 + 1):
            for ix in range(ix0, ix1 + 1):
                x, y = self._xy(ix, iy)
                if _shape_dist(x, y, shape) >= radius or _inside_any(x, y, self._exempt):
                    continue
                for layer in layers:
                    out.add(self._idx(layer, ix, iy))

    def _pair_blocked(self, c, i):
        pb = self._cur_pair
        return pb is not None and (pb[0][c][i] or pb[1][c][i])
```

6. Refactor `_geometry` without changing its output: move the run-splitting loop into `_runs(paths)` that returns the `segments` list (the loop exactly as it stands, `segments` starting empty, ending after the last `segments += self._run_segments(run)`), and have `_geometry` start with `segments = self._runs(self._paths[k])` followed by the unchanged terminal-stub and via code. `used` is built the same way (`for path in self._paths[k]: used.update(path)`). Run the tests: `test_defaults_unchanged` must still be ok before you go on.

7. In `run()`, after the static rasterisation loop and before `n = len(self._nets)`:

```python
        self._net_group = [self._groups.get(name) for name, _hw, _t in self._nets]
        self._pair_src = {}            # source group -> [(target group, mm)]
        for ga, gb, mm in self._pairs:
            self._pair_src.setdefault(ga, []).append((gb, mm))
            self._pair_src.setdefault(gb, []).append((ga, mm))
        targets = sorted({g for lst in self._pair_src.values() for g, _mm in lst})
        self._pstat = {g: [array("b", [0]) * cells for _ in self._classes] for g in targets}
        self._pocc = {g: [array("H", [0]) * cells for _ in self._classes] for g in targets}
        for (net, layers, shape), og in zip(self._obstacles, self._ogroups):
            g = og if og is not None else self._groups.get(net)
            for tgt, mm in self._pair_src.get(g, ()):
                for c, hw in enumerate(self._classes):
                    marked = set()
                    self._shape_cells(marked, layers, shape, hw + mm + self.s)
                    arr = self._pstat[tgt][c]
                    for i in marked:
                        arr[i] = 1
```

and after `self._marks, self._paths, self._tcells = ...`:

```python
        self._pmarks = [None] * n
```

8. Consult the pair arrays. In `_route_net`, first line after `nid, c = ...`:

```python
        g = self._net_group[k] if self._pairs else None
        self._cur_pair = ((self._pstat[g], self._pocc[g]) if g in getattr(self, "_pstat", {}) else None)
```

In `_cost`, before the `return`:

```python
        if self._cur_pair is not None and self._pair_blocked(c, i):
            return None
```

In `_via_step`, inside the layer loop after the static test:

```python
            if self._cur_pair is not None and self._pair_blocked(v, i):
                return None
```

In `_terminal_cells`, after the `st == BLOCKED ...` continue:

```python
                    if self._cur_pair is not None and self._pair_blocked(c, i):
                        continue
```

9. Mark and unmark routed copper. At the end of `_commit`:

```python
        g = self._net_group[k] if self._pairs else None
        pm = []
        for tgt, mm in self._pair_src.get(g, ()) if g is not None else ():
            for c2, hw2 in enumerate(self._classes):
                extra = self.s if c2 == self._via_c else 2 * self.s
                cells = set()
                for layer, a, b in self._runs(paths):
                    for p, q in _clip_outside(a, b, self._exempt):
                        self._capsule_cells(cells, (layer,), p, q, hw + mm + hw2 + extra)
                for ix, iy in self._vias_of(paths):
                    x, y = self._xy(ix, iy)
                    if not _inside_any(x, y, self._exempt):
                        self._capsule_cells(cells, range(self.layers), (x, y), (x, y),
                                            self.via_radius + mm + hw2 + self.s)
                occ = self._pocc[tgt][c2]
                for i in cells:
                    occ[i] += 1
                pm.append((tgt, c2, cells))
        self._pmarks[k] = pm
```

At the start of `_rip`, after the `None` guard, before the existing loop:

```python
        for tgt, c2, cells in self._pmarks[k] or ():
            occ = self._pocc[tgt][c2]
            for i in cells:
                occ[i] -= 1
        self._pmarks[k] = None
```

Note `_rip` is called before the first `_commit` of a net, when `self._marks[k]` is `None` and returns early — `_pmarks[k]` is `None` then too, so the order is safe.

- [ ] **Step 5: Run the tests**

Run: `python hardware/gen/test_route.py`
Expected: every test ok, `test_defaults_unchanged` included, "all route checks passed".

- [ ] **Step 6: Prove the RED once**

Temporarily change `_pair_blocked` to `return False` (back the file up to the scratchpad first). Run: `test_pair_keeps_apart` and `test_pair_static_pad` FAIL, `test_pair_binding` still ok. Restore from the backup with the Write tool. Delete `hardware/gen/__pycache__` before re-running.

- [ ] **Step 7: Commit**

```bash
git add hardware/gen/route.py hardware/gen/test_route.py
git commit -m "hw(gen): router keeps hard per-group pair clearances, lifted in exemption zones

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: Router — priority tiers and per-net stats

**Files:**
- Modify: `hardware/gen/route.py`
- Modify: `hardware/gen/test_route.py`

**Interfaces:**
- Consumes: Task 2's `add_net(..., group=None)`, `_runs(paths)`.
- Produces:
  - `Router.add_net(name, half_width, terminals, group=None, tier=0)`
  - Nets route in ascending tier, then by the existing span order.
  - Cell cost for a net of tier t: `(1 + history) × (1 + pres × (claims_le_t + 0.25 × claims_gt_t))`, where `claims_le_t` counts claims by nets of tier ≤ t and `claims_gt_t` by nets of a higher tier. With one tier in use the formula is evaluated exactly as today.
  - `Result.stats = {net: {"length_mm": float, "vias": int}}` for every routed net; length is the sum of its segments including terminal stubs, rounded to 1e-6.

- [ ] **Step 1: Write the failing tests**

```python
def _gap_board(tier_a, tier_b):
    """The negotiation wall: two gaps, each wide enough for one track; both
    nets want the low gap."""
    r = R.Router((0, 0, 10, 10), 0.2, 1, CLR, 0.3)
    for rect in ((4.5, 0.0, 5.5, 2.0), (4.5, 3.2, 5.5, 6.6), (4.5, 7.8, 5.5, 10.0)):
        r.add_obstacle(None, (0,), ("rect",) + rect)
    r.add_net("A", 0.125, [(1.0, 2.6, (0,)), (9.0, 2.6, (0,))], tier=tier_a)
    r.add_net("B", 0.125, [(1.0, 2.6 + 0.9, (0,)), (9.0, 2.6 + 0.9, (0,))], tier=tier_b)
    return r.run()


def _through_low_gap(res, net):
    return any(4.5 <= (p[0] + q[0]) / 2.0 <= 5.5 and max(p[1], q[1]) < 3.2
               for _l, p, q in res.routes.get(net, {"segments": []})["segments"])


def test_tiers():
    a_first = _gap_board(0, 1)
    b_first = _gap_board(1, 0)
    check(not a_first.failed and not b_first.failed and a_first.conflicts == 0 and b_first.conflicts == 0,
          "tiers: both boards routed without conflicts")
    check(_through_low_gap(a_first, "A") and not _through_low_gap(a_first, "B"),
          "tiers: A (tier 0) takes the low gap when B is tier 1")
    check(_through_low_gap(b_first, "B") and not _through_low_gap(b_first, "A"),
          "tiers: B (tier 0) takes the low gap when A is tier 1")


def test_stats():
    r = R.Router((0, 0, 10, 10), 0.2, 2, CLR, 0.3)
    r.add_obstacle(None, (0,), ("rect", 4.0, 0.0, 6.0, 10.0))
    r.add_net("V", 0.125, [(1.0, 5.0, (0,)), (9.0, 5.0, (0,))])
    res = r.run()
    st = res.stats.get("V", {})
    segs = res.routes.get("V", {"segments": [], "vias": []})
    want = sum(math.hypot(q[0] - p[0], q[1] - p[1]) for _l, p, q in segs["segments"])
    check(abs(st.get("length_mm", -1) - want) < 1e-5 and st.get("vias") == len(segs["vias"]) == 2,
          "stats: length %.3f (want %.3f), vias %s" % (st.get("length_mm", -1), want, st.get("vias")))
```

Add both to `__main__`. Run: `python hardware/gen/test_route.py`. Expected: FAIL (`tier` keyword; no `stats`).

Check the `_gap_board` geometry before implementing: with both nets at tier 0 (call `_gap_board(0, 0)` in a scratch line) both must still route with 0 conflicts, one per gap; if not, adjust only the terminal y values (not the walls) until they do, and say so in the report.

- [ ] **Step 2: Implement**

1. `__init__`: `self._tiers = {}`. `add_net(..., group=None, tier=0)`: `self._tiers[name] = int(tier)`.
2. `Result.__init__`: `self.stats = {}`.
3. In `run()`, after `self._class_of = ...`:

```python
        tier_ids = sorted({self._tiers.get(name, 0) for name, _hw, _t in self._nets}) or [0]
        self._tier_of = [tier_ids.index(self._tiers.get(name, 0)) for name, _hw, _t in self._nets]
        self._ntiers = len(tier_ids)
        self._occ_t = ([[array("i", [0]) * cells for _ in self._classes] for _ in range(self._ntiers)]
                       if self._ntiers > 1 else None)
```

(`self._occ` stays the total over all tiers and keeps its role in `_conflict_cells`.)

4. The route order: replace `order = sorted(range(n), key=span)` with

```python
        order = sorted(range(n), key=lambda k: (self._tier_of[k], span(k)))
```

With one tier the first key is always 0 and the order is today's.

5. `_cost(c, i, nid, pres)` becomes:

```python
    def _cost(self, c, i, nid, pres):
        st = self._static[c][i]
        if st == BLOCKED or (st != FREE and st != nid):
            return None
        if self._cur_pair is not None and self._pair_blocked(c, i):
            return None
        if self._occ_t is None:
            return (1.0 + self._hist[i]) * (1.0 + pres * self._occ[c][i])
        t = self._cur_tier
        le = sum(self._occ_t[u][c][i] for u in range(t + 1))
        return (1.0 + self._hist[i]) * (1.0 + pres * (le + 0.25 * (self._occ[c][i] - le)))
```

and `_via_step` becomes:

```python
    def _via_step(self, ix, iy, nid, pres):
        v = self._via_c
        claims = 0
        for layer in range(self.layers):
            i = self._idx(layer, ix, iy)
            st = self._static[v][i]
            if st == BLOCKED or (st != FREE and st != nid):
                return None
            if self._cur_pair is not None and self._pair_blocked(v, i):
                return None
            if self._occ_t is None:
                claims += self._occ[v][i]
            else:
                le = sum(self._occ_t[u][v][i] for u in range(self._cur_tier + 1))
                claims += le + 0.25 * (self._occ[v][i] - le)
        return self.via_cost * (1.0 + pres * claims)
```

6. `_route_net`: set `self._cur_tier = self._tier_of[k]` next to `self._cur_pair`.
7. `_commit`: after the existing `occ[i] += 1` loop, if `self._occ_t is not None`, do the same increments into `self._occ_t[self._tier_of[k]][c2]`. `_rip`: the matching decrements.
8. In `run()`'s output loop:

```python
        for k in order:
            if k not in failed:
                name = self._nets[k][0]
                geo = self._geometry(k)
                res.routes[name] = geo
                res.stats[name] = {
                    "length_mm": round(sum(math.hypot(q[0] - p[0], q[1] - p[1])
                                           for _l, p, q in geo["segments"]), 6),
                    "vias": len(geo["vias"])}
```

- [ ] **Step 3: Run the tests**

Run: `python hardware/gen/test_route.py`. Expected: all ok, `test_defaults_unchanged` included.

- [ ] **Step 4: Prove the RED once**

Back up, then change `0.25` to `1.0` in `_cost`. Run: `test_tiers` FAIL (both boards route like today, one of the two assertions on who takes the low gap fails). Restore from the backup; clear `__pycache__`; all ok.

- [ ] **Step 5: Commit**

```bash
git add hardware/gen/route.py hardware/gen/test_route.py
git commit -m "hw(gen): router routes in priority tiers and reports length and vias per net

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: One source for the board rules, and the committed project file carries them

**Files:**
- Create: `hardware/reva/rules.py`
- Modify: `hardware/gen/check.py` (`write_project_file`)
- Modify: `hardware/reva/build.py` (`write_all`)
- Modify: `hardware/reva/place.py` (constants read from rules.py)
- Regenerate: `hardware/reva/kicad/reva.kicad_pro` (via `python hardware/reva/build.py`)

**Interfaces:**
- Consumes: Task 1 Step 3's minimal key set (in the spec §3).
- Produces (`hardware/reva/rules.py`):
  - `SIGNAL_W = 0.25`, `SUPPLY_W = 0.5`, `SUPPLY_TRACK_NETS = ("+12V", "-12V", "+12V_IN", "-12V_IN", "3V3D")`, `PLANE_NETS = ("GND", "SM_3V3")`, `VIA_D = 0.6`, `VIA_DRILL = 0.3`, `CLEARANCE = 0.2`, `EDGE_CLEAR = 0.5`, `PITCH = 0.2`, `VIA_COST = 8.0`, `MAX_ITERS = 30`
  - `VICTIMS = ("OUT_L", "OUT_R", "IN_L", "IN_R")`, `AGGRESSORS` (the 48 names, built as below), `AUDIO_MM = 10.0`, `LR_PAIRS = (("OUT_L", "OUT_R"), ("IN_L", "IN_R"))`, `LR_MM = 2.0`, `SENSE = ("SENSE_0", "SENSE_1", "SENSE_2", "SENSE_3")`, `SENSE_FACTOR = 1.3`, `EXEMPT_MARGIN_MM = 2.54`, `TIERS = {"SENSE": 0, "AUDIO": 1, "REST": 2}`
  - `project_rules() -> dict`: the JSON sections (Task 1's minimal set) to merge into a `.kicad_pro`.
  - `gen.check.write_project_file(dest_dir, name, extra=None)`: `extra` is merged into the top-level JSON object; `extra=None` writes exactly today's bytes.

- [ ] **Step 1: Write the failing test**

In `hardware/reva/test_build.py`, add a function and call it from `main()` (append its failures):

```python
def rules_file_failures(kdir):
    """The committed reva.kicad_pro carries the board rules from rules.py
    (P4-2 spec §4.1): the values below are this guard's own, not re-read from
    rules.py."""
    import json
    path = os.path.join(kdir, "reva.kicad_pro")
    try:
        pro = json.load(open(path, encoding="utf-8"))
        r = pro["board"]["design_settings"]["rules"]
    except (OSError, ValueError, KeyError) as exc:
        return ["reva.kicad_pro carries no board rules: %s" % exc]
    want = {"min_track_width": 0.25, "min_via_diameter": 0.6, "min_through_hole_diameter": 0.3,
            "min_clearance": 0.2, "min_copper_edge_clearance": 0.5}
    return ["reva.kicad_pro rule %s is %r, not %r" % (k, r.get(k), v)
            for k, v in sorted(want.items()) if r.get(k) != v]
```

If Task 1 recorded different key names under `rules`, use Task 1's names here and say so in the report.

Run: `python hardware/reva/test_build.py`. Expected: FAIL "reva.kicad_pro carries no board rules".

- [ ] **Step 2: Create `hardware/reva/rules.py`**

```python
"""Rev A board rules, one source (P4-2 spec §4.3). place.py, route.py and
build.py read these values. The checks (place_check.py, route_check.py) keep
their own copies on purpose: a threshold is not re-derived from the thing it
guards."""

SIGNAL_W = 0.25
SUPPLY_W = 0.5
SUPPLY_TRACK_NETS = ("+12V", "-12V", "+12V_IN", "-12V_IN", "3V3D")
PLANE_NETS = ("GND", "SM_3V3")          # In1.Cu, In2.Cu
VIA_D, VIA_DRILL = 0.6, 0.3
CLEARANCE = 0.2
EDGE_CLEAR = 0.5
PITCH = 0.2
VIA_COST = 8.0
MAX_ITERS = 30

VICTIMS = ("OUT_L", "OUT_R", "IN_L", "IN_R")
AGGRESSORS = tuple(["LED%d" % n for n in range(19)] + ["LED%d_A" % n for n in range(19)]
                   + ["SR_CLK", "SR_DATA", "SR_LATCH", "SR_DIN",
                      "SD_CK", "SD_CMD", "SD_D0", "SD_D1", "SD_D2", "SD_D3"])
AUDIO_MM = 10.0
LR_PAIRS = (("OUT_L", "OUT_R"), ("IN_L", "IN_R"))
LR_MM = 2.0
SENSE = ("SENSE_0", "SENSE_1", "SENSE_2", "SENSE_3")
SENSE_FACTOR = 1.3
EXEMPT_MARGIN_MM = 2.54
TIERS = {"SENSE": 0, "AUDIO": 1, "REST": 2}


def tier_of(net):
    if net in SENSE:
        return TIERS["SENSE"]
    if net in VICTIMS:
        return TIERS["AUDIO"]
    return TIERS["REST"]


def project_rules():
    """The .kicad_pro sections kicad-cli reads the board rules from (probed,
    P4-2 Task 1): merged into the project file build.py writes."""
    return {"board": {"design_settings": {"rules": {
        "min_track_width": SIGNAL_W, "min_via_diameter": VIA_D,
        "min_through_hole_diameter": VIA_DRILL, "min_clearance": CLEARANCE,
        "min_copper_edge_clearance": EDGE_CLEAR}}}}
```

If Task 1 found that `net_settings` is also needed (set `d`), extend `project_rules()` with the `net_settings` section exactly as SaveBoard wrote it in probe `a` (classes `Default` 0.25 and `supply` 0.5 with clearance 0.2, via 0.6/0.3, and one `netclass_patterns` entry per `SUPPLY_TRACK_NETS` net), sorted keys.

- [ ] **Step 3: `write_project_file(..., extra=None)`**

In `hardware/gen/check.py`:

```python
def write_project_file(dest_dir, name, extra=None):
    """...(existing docstring)...

    `extra` (P4-2) is merged into the top-level object, e.g. the board rules
    a generated board is judged by. None writes exactly the former bytes."""
    path = os.path.join(dest_dir, name + ".kicad_pro")
    body = {"meta": {"filename": name + ".kicad_pro", "version": 3}}
    if extra:
        body.update(extra)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(json.dumps(body, sort_keys=bool(extra)) + "\n")
    return path
```

(`sort_keys` only when `extra` is given, so the coupon's and today's bytes stay the same.)

In `hardware/reva/build.py` `write_all`: `C.write_project_file(kdir, proj.name, extra=rules.project_rules())` with `import rules` beside the other local imports.

- [ ] **Step 4: `place.py` reads the shared values**

Replace `EDGE_CLEAR = 0.5` and `PAD_CLEAR = 0.2` with `EDGE_CLEAR = RU.EDGE_CLEAR` and `PAD_CLEAR = RU.CLEARANCE` (`import rules as RU`), and `PLANES = (("In1.Cu", RU.PLANE_NETS[0]), ("In2.Cu", RU.PLANE_NETS[1]))`. Keep the names so place_check and the guard need no change. `place_check.py` keeps its own `EDGE_CLEAR_MM`.

- [ ] **Step 5: Regenerate and run**

Run: `python hardware/reva/build.py`, then `python hardware/reva/test_build.py` → "ok: … generated Rev A files match the committed ones". Then `KIPY hardware/reva/place.py` → GREEN, and `git diff --stat hardware/reva/kicad/reva.kicad_pcb` empty (placement unchanged).

- [ ] **Step 6: Prove the RED once**

Back up `rules.py`, set `CLEARANCE = 0.15`, run `python hardware/reva/build.py` then `test_build.py`: FAIL "rule min_clearance is 0.15, not 0.2". Restore, rebuild, green.

- [ ] **Step 7: Commit (after ctest)**

```bash
git add hardware/reva/rules.py hardware/gen/check.py hardware/reva/build.py hardware/reva/place.py hardware/reva/test_build.py hardware/reva/kicad/reva.kicad_pro
git commit -m "hw(reva): board rules in one place; the committed project file carries them

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: `check_kit.py` — the shared check machinery

`route_check.py` needs P4-1's DRC parser, known-list judge, step runner, key-name rule and MST. Move them out of `place_check.py` / `test_place.py` into one module; P4-1's behaviour and output stay identical.

**Files:**
- Create: `hardware/reva/check_kit.py`, `hardware/reva/test_check_kit.py`
- Modify: `hardware/reva/place_check.py`, `hardware/reva/test_place.py`, `CMakeLists.txt`

**Interfaces:**
- Produces (`check_kit`):
  - `drc_blocks(txt) -> [(class, [refs], first_item_line)]` (moved verbatim, with `_REF_RE`, `_CLASS_RE`, `_ITEM_RE` and the probe comment)
  - `drc_items(rpt_path) -> [(class, [refs])]` (moved verbatim)
  - `report_summary(txt) -> {"violations": int|None, "unconnected": int|None, "footprint_errors": int|None, "complete": bool}` (new)
  - `judge(known, found) -> (ok, details, n_known)` — `known` a set, `found` `{key: message}`; body of today's `place_check._judge` after its first line
  - `run_steps(steps, s, pcb_path, prefix) -> bool` — today's `place_check.run` loop without the `s.known` initialisation, with `DETAIL_CAP = 40`
  - `key_names(key, class_words) -> [name]`, `key_allowed(key, allowed, pairs, class_words) -> bool` — today's `test_place` functions with `CLASS_WORDS`/`PAIRS` passed in
  - `mst(pts) -> float` — today's `place_check._mst`
- `place_check.py` keeps `drc_blocks`, `drc_items`, `_mst` as names (assign from check_kit) so nothing else changes; `_judge(s, check, found)` becomes `return CK.judge(s.known.get(check, set()), found)`; `run()` initialises `s.known` then `return CK.run_steps(STEPS, s, pcb_path, prefix)`.

- [ ] **Step 1: Write the failing test `hardware/reva/test_check_kit.py`**

```python
#!/usr/bin/env python3
"""Guard for hardware/reva/check_kit.py (P4-2). Plain script, no pcbnew."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_kit as CK  # noqa: E402

FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


REPORT = """** Drc report for x.kicad_pcb **
** Created on 2026-09-30T11:08:04 **
** Report includes: Errors, Warnings **

** Found 2 DRC violations **
[clearance]: Clearance violation
    Rule: x; error
    @(105.1014 mm, 69.6471 mm): Pad 1 [SM_3V3] of C11 on B.Cu
    @(214.3000 mm, 118.9200 mm): PTH pad T [MOD2_B] of J13
[shorting_items]: Items shorting two nets
    Local override; error
    @(258.1300 mm, 21.7080 mm): PTH pad 1 [GND] of D16
    @(256.8000 mm, 22.0000 mm): PTH pad 2 [M4_CH0] of RV56

** Found 1 unconnected pads **
[unconnected_items]: Missing connection between items
    Local override; error
    @(1.0 mm, 2.0 mm): Pad 1 [LED3] of R12 on B.Cu
    @(3.0 mm, 4.0 mm): PTH pad 2 [LED3] of D4

** Found 0 Footprint errors **

** End of Report **
"""


def main():
    blocks = CK.drc_blocks(REPORT)
    check([(c, r) for c, r, _f in blocks] == [("clearance", ["C11", "J13"]),
                                             ("shorting_items", ["D16", "RV56"]),
                                             ("unconnected_items", ["D4", "R12"])],
          "drc_blocks: classes and refs, SMD pad refs included")
    s = CK.report_summary(REPORT)
    check(s == {"violations": 2, "unconnected": 1, "footprint_errors": 0, "complete": True},
          "report_summary: %r" % s)
    cut = REPORT.split("** Found 1 unconnected")[0]
    check(CK.report_summary(cut)["complete"] is False, "report_summary: a cut report is incomplete")
    ok, details, nk = CK.judge({"a"}, {"a": "m", "b": "n"})
    check(not ok and nk == 1 and any("[NEW]" in d for d in details), "judge: a NEW key is red")
    ok, details, _ = CK.judge({"a", "z"}, {"a": "m"})
    check(not ok and any("no longer fails" in d for d in details), "judge: a stale key is red")
    ok, _d, _n = CK.judge({"a"}, {"a": "m"})
    check(ok, "judge: known only is green")
    cw = {"body", "clearance"}
    pairs = ({"GATE_A_L", "SOURCE_A"},)
    allowed = {"CLOCK", "SONG_A", "GATE_A_L", "SOURCE_A"}
    check(CK.key_allowed("clearance GATE_A_L/SOURCE_A", allowed, pairs, cw), "key: pair allowed")
    check(not CK.key_allowed("clearance GATE_A_L/CLOCK", allowed, pairs, cw), "key: pair with a stranger refused")
    check(not CK.key_allowed("NOT_A_PART CLOCK", allowed, pairs, cw), "key: unknown class word refused")
    check(abs(CK.mst([(0, 0), (3, 4), (3, 0)]) - 7.0) < 1e-9, "mst: 3 + 4")
    print("FAILED: %d" % len(FAILS) if FAILS else "all check_kit checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
```

Run: `python hardware/reva/test_check_kit.py`. Expected: FAIL (ModuleNotFoundError: check_kit).

- [ ] **Step 2: Create `check_kit.py`**

Move the named functions and regexes verbatim (with their comments) from `place_check.py` and `test_place.py`, generalised only as the Interfaces block says. Add:

```python
_FOUND_RE = {"violations": re.compile(r"^\*\* Found (\d+) DRC violations \*\*", re.M),
             "unconnected": re.compile(r"^\*\* Found (\d+) unconnected pads \*\*", re.M),
             "footprint_errors": re.compile(r"^\*\* Found (\d+) Footprint errors \*\*", re.M)}


def report_summary(txt):
    """The report's own counts (probed on P4-1's report, 10.0.5) and whether
    it ran to its end. A reader that loses blocks is caught by comparing
    these counts with the blocks it parsed."""
    out = {}
    for k, rx in _FOUND_RE.items():
        m = rx.search(txt)
        out[k] = int(m.group(1)) if m else None
    out["complete"] = "** End of Report **" in txt and None not in out.values()
    return out
```

`place_check.py` and `test_place.py` import from it (`import check_kit as CK`) and delete the moved bodies; `test_place.py` keeps its `CLASS_WORDS`/`PAIRS` definitions and calls `CK.key_allowed(key, allowed, PAIRS, CLASS_WORDS)`.

- [ ] **Step 3: Wire ctest**

```cmake
# Rev A P4-2: the shared check machinery (DRC parser, judge, step runner).
add_test(NAME reva_check_kit_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_check_kit.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```

- [ ] **Step 4: Run**

`python hardware/reva/test_check_kit.py` → all ok. `KIPY hardware/reva/place.py` → GREEN with step lines identical to before (diff the stdout of a run before and after the move; paste the diff, which must be empty apart from timestamps).

- [ ] **Step 5: Prove the RED once**

Back up `check_kit.py`, delete `"** End of Report **" in txt and` from `report_summary`; the "cut report" check FAILs. Restore.

- [ ] **Step 6: Commit (after ctest)**

```bash
git add hardware/reva/check_kit.py hardware/reva/test_check_kit.py hardware/reva/place_check.py hardware/reva/test_place.py CMakeLists.txt
git commit -m "hw(reva): the check machinery moves to check_kit for P4-2 to share; report summary parser

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: The adapter `route.py`, and the first routed board

**Files:**
- Create: `hardware/reva/route.py`
- Create: `hardware/reva/route_check.py` (steps `routed`, `drc`, `render` only in this task)

**Interfaces:**
- Consumes: `place.build()`, `place.Placed`, `place._hole_index()`, `rules.*`, `gen.route.Router` (Tasks 2–3), `gen.stitch.stitch_plane_pads`, `gen.place.body_box`, `check_kit`.
- Produces:
  - `route.build() -> Routed`, `route.save(s, path)`, `route.main(argv)` with `--out`, `--write`, `--sabotage`
  - `route.module_zones(board, margin) -> [(l, t, r, b)]`
  - `route.pot_refs(s) -> [ref]`
  - `class Routed`: `board`, `ids` (ref → panel id), `front` (front refs), `known`, `result` (router `Result`), `widths` ({net: mm}), `zones`, `stitched`, `unresolved`, `seconds`, `skip_render`; `reload(path)`
  - `route_check.run(s, pcb_path, prefix) -> bool`, `route_check.KNOWN_PANEL`, `SABOTAGES`, `TURNS_RED`, `WHY`, `sabotage(s, name)`

- [ ] **Step 1: Write `route.py`**

```python
#!/usr/bin/env python3
"""P4-2: the placed Rev A board -> gen.route -> the routed board (spec §4.1).

Placement is P4-1's `place.build()`, in memory. SMD pads on the plane nets
are stitched first (their vias are obstacles to the router). Every pad is an
obstacle owned by its net; netless pads (pot tabs, unused contacts) block
every net; footprint rule areas block their layers; each pot's body box
blocks F.Cu (spec §2.7). Every net with two or more pads is routed except
the plane nets. Victims and aggressors carry pair groups, U_SM's pad groups
are exemption zones, SENSE and audio route in earlier tiers. The rules are
judged by route_check.py on the saved board, never here."""
import argparse
import copy
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for _p in (HW, HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import pcbnew               # noqa: E402
import place as P           # noqa: E402
import rules as RU          # noqa: E402
from gen import kipcb       # noqa: E402
from gen import place as PL  # noqa: E402
from gen import route as GR  # noqa: E402  (not `route`: this module is route.py)
from gen import stitch      # noqa: E402

OUT = P.OUT
COMMITTED = P.COMMITTED
DOCS = os.path.normpath(os.path.join(HW, "..", "docs", "hardware", "routing"))
LAYER_NAMES = ("F.Cu", "B.Cu")
AGGR = "aggressor"


def _mm(v):
    return pcbnew.ToMM(v)


class Routed:
    def __init__(self):
        self.board = None
        self.ids = {}
        self.front = []
        self.known = {}
        self.result = None
        self.widths = {}
        self.zones = []
        self.stitched = {}
        self.unresolved = []
        self.seconds = 0.0
        self.skip_render = False

    def reload(self, path):
        new = Routed()
        for k, v in self.__dict__.items():
            if k not in ("board", "result"):
                setattr(new, k, copy.deepcopy(v))
        new.result = self.result          # read-only after the run
        new.board = kipcb.load(path)
        return new


def module_zones(board, margin):
    """One rect per group of U_SM pads whose centres lie within one pin pitch
    (2.54 mm) of a neighbour: the box of the group's pad centres grown by
    `margin` (spec §4.2.2; four groups on the P4-1 board)."""
    sm = board.FindFootprintByReference("U_SM")
    pts = sorted({(round(_mm(p.GetPosition().x), 4), round(_mm(p.GetPosition().y), 4))
                  for p in sm.Pads()})
    parent = list(range(len(pts)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for i, a in enumerate(pts):
        for j in range(i + 1, len(pts)):
            b = pts[j]
            if (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 <= (2.54 + 1e-3) ** 2:
                parent[find(i)] = find(j)
    groups = {}
    for i, p in enumerate(pts):
        groups.setdefault(find(i), []).append(p)
    return sorted((min(x for x, _ in g) - margin, min(y for _, y in g) - margin,
                   max(x for x, _ in g) + margin, max(y for _, y in g) + margin)
                  for g in groups.values())


def pot_refs(placed):
    holes = P._hole_index()
    return sorted(r for r in placed.front if holes.get(placed.ids.get(r), {}).get("kind") == "pot")


def _group(net):
    if net in RU.VICTIMS:
        return net                      # one group per victim: the L/R rule pairs them
    if net in RU.AGGRESSORS:
        return AGGR
    return None


def _router_input(s, placed):
    b = s.board
    inset = RU.EDGE_CLEAR + RU.SUPPLY_W / 2.0
    r = GR.Router((P.X0 + inset, P.Y0 + inset, P.X1 - inset, P.Y1 - inset),
                  RU.PITCH, len(LAYER_NAMES), RU.CLEARANCE, RU.VIA_D / 2.0, RU.VIA_COST)
    for v in RU.VICTIMS:
        r.pair_clearance(v, AGGR, RU.AUDIO_MM)
    for a, bb in RU.LR_PAIRS:
        r.pair_clearance(a, bb, RU.LR_MM)
    for z in s.zones:
        r.pair_exempt(z)
    terms = {}
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            ls = tuple(i for i, n in enumerate(LAYER_NAMES) if pad.IsOnLayer(kipcb.LAYER[n]))
            bb = pad.GetBoundingBox()
            net = pad.GetNetname() or None
            r.add_obstacle(net, ls, ("rect", _mm(bb.GetLeft()), _mm(bb.GetTop()),
                                     _mm(bb.GetRight()), _mm(bb.GetBottom())),
                           group=_group(net))
            if net and net not in RU.PLANE_NETS:
                terms.setdefault(net, []).append((_mm(pad.GetPosition().x), _mm(pad.GetPosition().y), ls))
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            r.add_obstacle(t.GetNetname(), range(len(LAYER_NAMES)),
                           ("circle", _mm(t.GetPosition().x), _mm(t.GetPosition().y),
                            _mm(t.GetWidth(pcbnew.F_Cu)) / 2.0), group=_group(t.GetNetname()))
        elif t.Type() == pcbnew.PCB_TRACE_T and t.GetLayerName() in LAYER_NAMES:
            r.add_obstacle(t.GetNetname(), (LAYER_NAMES.index(t.GetLayerName()),),
                           ("seg", _mm(t.GetStart().x), _mm(t.GetStart().y),
                            _mm(t.GetEnd().x), _mm(t.GetEnd().y), _mm(t.GetWidth()) / 2.0),
                           group=_group(t.GetNetname()))
    for fp in b.GetFootprints():
        for z in fp.Zones():
            if z.GetIsRuleArea() and z.GetDoNotAllowTracks():
                ls = tuple(i for i, n in enumerate(LAYER_NAMES) if z.IsOnLayer(kipcb.LAYER[n]))
                bb = z.GetBoundingBox()
                r.add_obstacle(None, ls, ("rect", _mm(bb.GetLeft()), _mm(bb.GetTop()),
                                          _mm(bb.GetRight()), _mm(bb.GetBottom())))
    for ref in pot_refs(placed):
        l, t, rr, btm = PL.body_box(b.FindFootprintByReference(ref))
        r.add_obstacle(None, (LAYER_NAMES.index("F.Cu"),), ("rect", l, t, rr, btm))
    for net in sorted(terms):
        if len(terms[net]) < 2:
            continue
        w = RU.SUPPLY_W if net in RU.SUPPLY_TRACK_NETS else RU.SIGNAL_W
        s.widths[net] = w
        r.add_net(net, w / 2.0, terms[net], group=_group(net), tier=RU.tier_of(net))
    return r


def build():
    placed = P.build()
    s = Routed()
    s.board = placed.board
    s.ids, s.front = dict(placed.ids), list(placed.front)
    kipcb.set_netclasses(s.board, RU.SIGNAL_W, {"supply": (RU.SUPPLY_W, list(RU.SUPPLY_TRACK_NETS))},
                         clearance_mm=RU.CLEARANCE, via_mm=RU.VIA_D, drill_mm=RU.VIA_DRILL)
    s.zones = module_zones(s.board, RU.EXEMPT_MARGIN_MM)
    s.stitched, s.unresolved = stitch.stitch_plane_pads(s.board, set(RU.PLANE_NETS), netless_blocks=True)
    r = _router_input(s, placed)
    t0 = time.time()
    s.result = r.run(max_iters=RU.MAX_ITERS)
    s.seconds = round(time.time() - t0, 1)
    for net, geo in sorted(s.result.routes.items()):
        for layer, a, bb in geo["segments"]:
            kipcb.add_track(s.board, LAYER_NAMES[layer], s.widths[net], net, [a, bb])
        for xy in geo["vias"]:
            kipcb.add_via(s.board, xy, net)
    kipcb.fill_zones(s.board)
    return s


def save(s, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    kipcb.save(s.board, path)


def main(argv=None):
    import route_check as RC
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--sabotage", default="")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args(argv)
    s = build()
    prefix = os.path.join(a.out, "reva-routed")
    pcb = prefix + ".kicad_pcb"
    if a.sabotage:
        save(s, pcb)
        s = s.reload(pcb)
        RC.sabotage(s, a.sabotage)
    save(s, pcb)
    print("wrote", os.path.relpath(pcb))
    green = RC.run(s, pcb, prefix)
    if a.write and not a.sabotage and not green:
        print("not copied: the run is RED")
    if a.write and not a.sabotage and green:
        shutil.copyfile(pcb, COMMITTED)
        print("copied to", os.path.relpath(COMMITTED))
        os.makedirs(DOCS, exist_ok=True)
        for side in ("top", "bottom"):
            png = "%s-%s.png" % (prefix, side)
            if os.path.exists(png):
                shutil.copyfile(png, os.path.join(DOCS, os.path.basename(png)))
        print("renders copied to", os.path.relpath(DOCS))
    print("GREEN" if green else "RED")
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
```

Probe before relying on them: that `P._hole_index()` rows carry `"kind"` with the value `"pot"` for pots (print the kinds once), and that `s.ids` maps footprint refs to panel ids (P4-1 `place_check._key` uses it that way). `pot_refs` must return 60 ± a few names; print the count in the report.

- [ ] **Step 2: Write `route_check.py` with `routed`, `drc`, `render`**

```python
#!/usr/bin/env python3
"""The P4-2 checks (spec §5). Every step measures the saved board, not what
the router reported. Each step owns its thresholds and net sets (none is
imported from rules.py, route.py or gen/route.py). Known panel violations
(spec §4.4) print as known; an unlisted failure is red, and so is a listed
one that no longer fails. A step that examined nothing is red."""
import math
import os
import re

import pcbnew

import check_kit as CK
from gen import pcb_proof as PP

# Filled from the first full run (Task 6), restricted by the guard to the jack
# row, the SONG clusters and the GATE_A_L/SOURCE_A and LVL_B_L/PAN_B pairs.
KNOWN_PANEL = {
    "routed": set(),
    "drc": set(),
}

GATED_DRC = ("courtyards_overlap", "pth_inside_courtyard", "shorting_items", "clearance",
             "hole_clearance", "hole_to_hole", "copper_edge_clearance", "items_not_allowed",
             "tracks_crossing", "track_dangling", "via_dangling", "track_width",
             "annular_width", "drill_out_of_range", "via_diameter")
FRONT_REPORTED = ("courtyards_overlap", "pth_inside_courtyard")


def _key(s, ref):
    return s.ids.get(ref, ref)


def _judge(s, check, found):
    return CK.judge(s.known.get(check, set()), found)


def _drc(s, pcb_path, prefix):
    """(report text, error line or None). Cached per run on `s`."""
    cache = getattr(s, "_drc_cache", None)
    if cache and cache[0] == pcb_path:
        return cache[1], None
    rpt = prefix + "-drc.rpt"
    try:
        PP.drc(pcb_path + (".missing" if getattr(s, "drc_broken", False) else ""), rpt)
    except RuntimeError as e:
        return None, "kicad-cli wrote no report: %s" % str(e)[:200]
    if getattr(s, "drc_cut", False):
        # sabotage "drc_cut": the report loses its tail, as a crashed writer would
        txt = open(rpt, encoding="utf-8", errors="replace").read()
        with open(rpt, "w", encoding="utf-8") as fh:
            fh.write(txt[: len(txt) // 2])
    txt = open(rpt, encoding="utf-8", errors="replace").read()
    s._drc_cache = (pcb_path, txt)
    return txt, None


def check_routed(s, pcb_path, prefix):
    txt, err = _drc(s, pcb_path, prefix)
    if err:
        return False, err, []
    res = s.result
    if getattr(s, "routed_missing", False):
        txt = ""
    blocks = [(c, refs) for c, refs, _f in CK.drc_blocks(txt) if c == "unconnected_items"]
    summ = CK.report_summary(txt)
    if not summ["complete"]:
        return False, "the DRC report is incomplete, so unconnected items cannot be counted", []
    if summ["unconnected"] != len(blocks):
        return False, ("the report says %s unconnected pads but %d blocks were read"
                       % (summ["unconnected"], len(blocks))), []
    found = {}
    for _c, refs in blocks:
        key = "unrouted %s" % "/".join(sorted(_key(s, r) for r in refs))
        found[key] = "kicad-cli unconnected_items"
    ok, details, nk = _judge(s, "routed", found)
    by_net = PP.unconnected_by_net(prefix + "-drc.rpt") if blocks else {}
    details += ["unrouted on %s: %s" % (n, ", ".join(sorted(p))) for n, p in sorted(by_net.items())]
    if res is None or res.conflicts:
        ok = False
        details.insert(0, "router left %s nets in conflict" % (None if res is None else res.conflicts))
    if res is not None and res.failed:
        details.insert(0, "router failed nets: %s" % ", ".join(res.failed))
    return ok, ("%d unconnected items (%d known), router %s rounds, %s conflicts, %s failed, %.1f s"
                % (len(found), nk, getattr(res, "iterations", "?"), getattr(res, "conflicts", "?"),
                   len(getattr(res, "failed", [])), s.seconds)), details


def check_drc(s, pcb_path, prefix):
    txt, err = _drc(s, pcb_path, prefix)
    if err:
        return False, err, []
    summ = CK.report_summary(txt)
    blocks = CK.drc_blocks(txt)
    n_viol = sum(1 for c, _r, _f in blocks if c != "unconnected_items")
    n_tracks = sum(1 for t in s.board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T)
    if not summ["complete"] or summ["violations"] != n_viol or not n_tracks:
        return False, ("the DRC report was not read: complete %s, report says %s violations, %d parsed, "
                       "%d tracks on the board" % (summ["complete"], summ["violations"], n_viol, n_tracks)), []
    found, front_rep = {}, []
    front = set(s.front)
    for cls, refs, first in blocks:
        if cls not in GATED_DRC:
            continue
        if cls in FRONT_REPORTED and refs and set(refs) <= front:
            front_rep.append("%s %s" % (cls, "/".join(sorted(_key(s, r) for r in refs))))
            continue
        key = "%s %s" % (cls, "/".join(sorted(_key(s, r) for r in refs)))
        found.setdefault(key, "kicad-cli, first item %s" % first)
    ok, details, nk = _judge(s, "drc", found)
    details += ["reported, not gated: " + f for f in sorted(set(front_rep))]
    counts = {}
    for cls, _r, _f in blocks:
        if cls not in GATED_DRC and cls != "unconnected_items":
            counts[cls] = counts.get(cls, 0) + 1
    others = ", ".join("%s %d" % kv for kv in sorted(counts.items())) or "none"
    return ok, ("gated: %d items (%d known), %d front courtyard items reported, %d violations in the "
                "report, %d tracks; not gated: %s" % (len(found), nk, len(front_rep), n_viol, n_tracks,
                                                       others)), details


def render(s, pcb_path, prefix):
    if getattr(s, "skip_render", False):
        return True, "skipped (guard sabotage run)", []
    made = []
    for side in ("top", "bottom"):
        png = "%s-%s.png" % (prefix, side)
        PP.render(pcb_path, png, side)
        if os.path.exists(png):
            made.append(os.path.basename(png))
    return len(made) == 2, "rendered %s" % ", ".join(made), []


STEPS = [("routed", check_routed), ("drc", check_drc), ("render", render)]


def run(s, pcb_path, prefix):
    s.known = {k: set(v) for k, v in KNOWN_PANEL.items()} if not s.known else s.known
    s._drc_cache = None
    return CK.run_steps(STEPS, s, pcb_path, prefix)


def _fp(board, ref):
    return board.FindFootprintByReference(ref)


def _sab_routed(s):
    """One routed track of the first SENSE net removed: a connection opens."""
    t = sorted((t for t in s.board.GetTracks()
                if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() == "SENSE_2"),
               key=lambda t: (t.GetStart().x, t.GetStart().y))[0]
    s.board.Delete(t)


def _sab_routed_missing(s):
    s.routed_missing = True


def _sab_drc(s):
    """A GND track 0.1 mm beside a decoupler's rail pad (P4-1's near miss)."""
    from gen import kipcb
    fp = _fp(s.board, "C1") or sorted(s.board.GetFootprints(), key=lambda f: f.GetReference())[0]
    bb = [p for p in fp.Pads() if str(p.GetNumber()) == "1"][0].GetBoundingBox()
    x = pcbnew.ToMM(bb.GetRight()) + 0.1 + 0.125
    kipcb.add_track(s.board, "B.Cu", 0.25, "GND",
                    [(x, pcbnew.ToMM(bb.GetTop()) - 1.0), (x, pcbnew.ToMM(bb.GetBottom()) + 1.0)])


def _sab_drc_missing(s):
    s.drc_broken = True


def _sab_drc_cut(s):
    s.drc_cut = True


SABOTAGES = {"routed": _sab_routed, "routed_missing": _sab_routed_missing,
             "drc": _sab_drc, "drc_missing": _sab_drc_missing, "drc_cut": _sab_drc_cut}
TURNS_RED = {"routed": "routed", "routed_missing": "routed", "drc": "drc",
             "drc_missing": "drc", "drc_cut": "drc"}
WHY = {"routed": "unrouted on SENSE_2", "routed_missing": "incomplete",
       "drc": "[NEW]", "drc_missing": "wrote no report", "drc_cut": "was not read"}


def sabotage(s, name):
    if name not in SABOTAGES:
        raise SystemExit("unknown sabotage %r; known: %s" % (name, ", ".join(sorted(SABOTAGES))))
    if not s.known:
        s.known = {k: set(v) for k, v in KNOWN_PANEL.items()}
    SABOTAGES[name](s)
    print("SABOTAGED: %s" % name)
```

Adjust `_sab_drc`'s part to a real decoupler of this board if `C1` is not one (P4-1's sabotage used `sorted(s.decouplers)[0]`; `Routed` has no `decouplers` — pick the first 100 nF by value, and say which). `_sab_routed` must remove a track whose removal really opens a connection (SENSE_2 is 40 mm and has no plane): the detail line `unrouted on SENSE_2: ...` from `PP.unconnected_by_net` carries the WHY phrase.

- [ ] **Step 3: First full run**

Run: `KIPY hardware/reva/route.py` (timeout 1800 s).
Paste every step line. Then, for each NEW key:
- `routed`/`drc` keys whose names all pass the known-list rule (Global Constraints) → copy into `KNOWN_PANEL` with a one-line comment naming the P4-1 reason (jack past the edge, SONG lamp on its pot, the two admitted pairs).
- Any other key → STOP. Do not list it, do not commit. Report the key, the DRC item lines behind it, and the router's failed nets.
- A router failure on a victim, a SENSE net or any net whose pads are all non-panel → STOP, same report.

Run again until GREEN with only listed keys. Look at both renders (`hardware/reva/out/reva-routed-top.png`, `-bottom.png`) and describe what they show in the report (tracks under pots? tracks along the board edge? the audio lines' paths?).

- [ ] **Step 4: Prove each sabotage RED**

`KIPY hardware/reva/route.py --sabotage <name>` for each of the five; each must end RED with its `TURNS_RED` step red and its `WHY` phrase in the output. Paste the red line of each.

- [ ] **Step 5: Commit (after ctest)**

```bash
git add hardware/reva/route.py hardware/reva/route_check.py
git commit -m "hw(reva): P4-2 adapter routes the placed board; routed and DRC checks

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 7: The analog and board checks

**Files:**
- Modify: `hardware/reva/route_check.py`

**Interfaces:**
- Consumes: Task 6's `Routed`, `route.module_zones` is NOT used by the check (it computes its own zones), `check_kit.mst`.
- Produces: steps `rules_file`, `audio`, `lr`, `sense`, `pot_keepout`, `planes`, `report` in `STEPS` order `routed, drc, rules_file, audio, lr, sense, pot_keepout, planes, report, render`; one sabotage and one `_missing` sabotage per gated step, each with a `WHY` phrase.

- [ ] **Step 1: The check's own constants and copper model**

Add to `route_check.py`:

```python
# The check's own values (spec §2, §4.3); not imported from rules.py.
AUDIO_MM = 10.0
LR_MM = 2.0
SENSE_FACTOR = 1.3
ZONE_PITCH_MM = 2.54
ZONE_MARGIN_MM = 2.54
PF_PER_MM = 0.1            # estimate, not measured (spec §3)
VICTIMS = ("OUT_L", "OUT_R", "IN_L", "IN_R")
AGGRESSORS = tuple(["LED%d" % n for n in range(19)] + ["LED%d_A" % n for n in range(19)]
                   + ["SR_CLK", "SR_DATA", "SR_LATCH", "SR_DIN",
                      "SD_CK", "SD_CMD", "SD_D0", "SD_D1", "SD_D2", "SD_D3"])
LR = (("OUT_L", "OUT_R"), ("IN_L", "IN_R"))
SENSE = ("SENSE_0", "SENSE_1", "SENSE_2", "SENSE_3")
RULES = {"min_track_width": 0.25, "min_via_diameter": 0.6, "min_through_hole_diameter": 0.3,
         "min_clearance": 0.2, "min_copper_edge_clearance": 0.5}
PIECE_MM = 0.5
LAYERS = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}


def _mm(v):
    return pcbnew.ToMM(v)


def zones(board):
    """The exemption zones, computed here independently of route.py: U_SM's
    pads clustered at one pin pitch, each cluster's box of centres grown by
    ZONE_MARGIN_MM."""
    sm = board.FindFootprintByReference("U_SM")
    pts = sorted({(round(_mm(p.GetPosition().x), 4), round(_mm(p.GetPosition().y), 4)) for p in sm.Pads()})
    groups = []
    for p in pts:
        near = [g for g in groups if any(math.hypot(p[0] - q[0], p[1] - q[1]) <= ZONE_PITCH_MM + 1e-3 for q in g)]
        merged = [p] + [q for g in near for q in g]
        groups = [g for g in groups if g not in near] + [merged]
    return sorted((min(x for x, _ in g) - ZONE_MARGIN_MM, min(y for _, y in g) - ZONE_MARGIN_MM,
                   max(x for x, _ in g) + ZONE_MARGIN_MM, max(y for _, y in g) + ZONE_MARGIN_MM)
                  for g in groups)


def _inside(x, y, rects):
    return any(l <= x <= r and t <= y <= b for l, t, r, b in rects)


def copper(board, nets, rects):
    """{layer: [(net, kind, geom, half)]} for every copper item of `nets`
    outside `rects`: tracks as pieces <= PIECE_MM (a piece is kept if its
    midpoint is outside), vias as points on both layers, pads as their
    bounding boxes on each outer layer they are on (dropped if the centre is
    inside). geom is ((x1, y1), (x2, y2)) for pieces and vias (x2 = x1),
    (l, t, r, b) for pads."""
    out = {"F.Cu": [], "B.Cu": []}
    want = set(nets)
    for t in board.GetTracks():
        n = t.GetNetname()
        if n not in want:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            x, y = _mm(t.GetPosition().x), _mm(t.GetPosition().y)
            if not _inside(x, y, rects):
                for ln in out:
                    out[ln].append((n, "via", ((x, y), (x, y)), _mm(t.GetWidth(pcbnew.F_Cu)) / 2.0))
            continue
        ln = t.GetLayerName()
        if ln not in out:
            continue
        (x1, y1), (x2, y2) = (_mm(t.GetStart().x), _mm(t.GetStart().y)), (_mm(t.GetEnd().x), _mm(t.GetEnd().y))
        k = max(1, int(math.ceil(math.hypot(x2 - x1, y2 - y1) / PIECE_MM)))
        for i in range(k):
            a = (x1 + (x2 - x1) * i / k, y1 + (y2 - y1) * i / k)
            b = (x1 + (x2 - x1) * (i + 1) / k, y1 + (y2 - y1) * (i + 1) / k)
            if not _inside((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0, rects):
                out[ln].append((n, "track", (a, b), _mm(t.GetWidth()) / 2.0))
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            n = pad.GetNetname()
            if n not in want:
                continue
            c = pad.GetPosition()
            if _inside(_mm(c.x), _mm(c.y), rects):
                continue
            bb = pad.GetBoundingBox()
            box = (_mm(bb.GetLeft()), _mm(bb.GetTop()), _mm(bb.GetRight()), _mm(bb.GetBottom()))
            for ln, lid in LAYERS.items():
                if pad.IsOnLayer(lid):
                    out[ln].append((n, "pad", box, 0.0))
    return out


def _seg_pt(p, a, b):
    (px, py), (ax, ay), (bx, by) = p, a, b
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def _seg_seg(s1, s2):
    return min(_seg_pt(s1[0], *s2), _seg_pt(s1[1], *s2), _seg_pt(s2[0], *s1), _seg_pt(s2[1], *s1))


def _box_pts(box, n=4):
    l, t, r, b = box
    return [(l + (r - l) * i / n, y) for i in range(n + 1) for y in (t, b)] + \
           [(x, t + (b - t) * i / n) for i in range(1, n) for x in (l, r)]


def _dist(a, b):
    """Edge-to-edge distance between two items of copper()."""
    (_n1, k1, g1, h1), (_n2, k2, g2, h2) = a, b
    if k1 == "pad" and k2 == "pad":
        l1, t1, r1, b1 = g1
        l2, t2, r2, b2 = g2
        return math.hypot(max(l2 - r1, l1 - r2, 0.0), max(t2 - b1, t1 - b2, 0.0))
    if k1 == "pad":
        a, b = b, a
        (_n1, k1, g1, h1), (_n2, k2, g2, h2) = a, b
    if k2 == "pad":
        l, t, r, btm = g2
        inside = any(l <= x <= r and t <= y <= btm for x, y in g1)
        if inside:
            return 0.0
        return max(0.0, min(_seg_pt(p, *g1) for p in _box_pts(g2)) - h1)
    return max(0.0, _seg_seg(g1, g2) - h1 - h2)


def min_distance(items_a, items_b, reach):
    """(distance, item_a, item_b) of the closest pair, found through 'reach'-
    sized buckets; (inf, None, None) if none lies within reach."""
    def cells(item):
        _n, k, g, _h = item
        xs = [g[0], g[2]] if k == "pad" else [g[0][0], g[1][0]]
        ys = [g[1], g[3]] if k == "pad" else [g[0][1], g[1][1]]
        for cx in range(int(min(xs) // reach) - 1, int(max(xs) // reach) + 2):
            for cy in range(int(min(ys) // reach) - 1, int(max(ys) // reach) + 2):
                yield cx, cy
    grid = {}
    for it in items_b:
        for c in cells(it):
            grid.setdefault(c, []).append(it)
    best = (float("inf"), None, None)
    for a in items_a:
        seen = set()
        for c in cells(a):
            for b in grid.get(c, ()):
                if id(b) in seen:
                    continue
                seen.add(id(b))
                d = _dist(a, b)
                if d < best[0]:
                    best = (d, a, b)
    return best
```

- [ ] **Step 2: The steps**

```python
def _where(item):
    _n, k, g, _h = item
    x, y = ((g[0] + g[2]) / 2.0, (g[1] + g[3]) / 2.0) if k == "pad" else g[0]
    return "%s %s @(%.2f, %.2f)" % (item[0], k, x, y)


def check_audio(s, pcb_path, prefix):
    nets = {n for n in (t.GetNetname() for t in s.board.GetTracks())} | \
           {p.GetNetname() for f in s.board.GetFootprints() for p in f.Pads()}
    victims = [] if getattr(s, "audio_missing", False) else list(VICTIMS)
    missing = [n for n in victims + list(AGGRESSORS) if n not in nets]
    if not victims or missing:
        return False, "victims or aggressors absent from the board: %s" % (missing or "no victims"), []
    rects = zones(s.board)
    bad, details = [], []
    ac = copper(s.board, AGGRESSORS, rects)
    for v in victims:
        vc = copper(s.board, [v], rects)
        worst = (float("inf"), None, None)
        for ln in vc:
            d = min_distance(vc[ln], ac[ln], AUDIO_MM + 1.0)
            if d[0] < worst[0]:
                worst = d
        if worst[1] is None:
            details.append("%s: no aggressor copper within %.1f mm on its layers" % (v, AUDIO_MM + 1.0))
            continue
        line = "%s: %.3f mm from %s (at %s)" % (v, worst[0], _where(worst[2]), _where(worst[1]))
        details.append(line)
        if worst[0] < AUDIO_MM - 1e-6:
            bad.append(line)
    ok = not bad
    return ok, ("%d victims, %d aggressors, %d exemption zones, %d below %.1f mm"
                % (len(victims), len(AGGRESSORS), len(rects), len(bad), AUDIO_MM)), \
        ["audio clearance below %.1f mm: %s" % (AUDIO_MM, b) for b in bad] + details


def check_lr(s, pcb_path, prefix):
    pairs = [] if getattr(s, "lr_missing", False) else list(LR)
    if not pairs:
        return False, "no L/R pair examined", []
    rects = zones(s.board)
    bad, details = [], []
    for a, b in pairs:
        ca, cb = copper(s.board, [a], rects), copper(s.board, [b], rects)
        if not any(ca.values()) or not any(cb.values()):
            bad.append("%s/%s: no copper to measure" % (a, b))
            continue
        worst = min((min_distance(ca[ln], cb[ln], LR_MM + 1.0) for ln in ca), key=lambda d: d[0])
        line = "%s/%s: %s" % (a, b, "farther than %.1f mm everywhere" % (LR_MM + 1.0)
                              if worst[1] is None else "%.3f mm at %s" % (worst[0], _where(worst[1])))
        details.append(line)
        if worst[1] is not None and worst[0] < LR_MM - 1e-6:
            bad.append(line)
    return not bad, "%d pairs, %d below %.1f mm" % (len(pairs), len(bad), LR_MM), \
        ["L/R spacing below %.1f mm: %s" % (LR_MM, b) for b in bad] + details


def check_sense(s, pcb_path, prefix):
    nets = [] if getattr(s, "sense_missing", False) else list(SENSE)
    if not nets:
        return False, "no SENSE net examined", []
    bad, details = [], []
    for n in nets:
        pts = [(_mm(p.GetPosition().x), _mm(p.GetPosition().y))
               for f in s.board.GetFootprints() for p in f.Pads() if p.GetNetname() == n]
        tracks = [t for t in s.board.GetTracks() if t.GetNetname() == n]
        length = sum(_mm(t.GetLength()) for t in tracks if t.Type() == pcbnew.PCB_TRACE_T)
        vias = sum(1 for t in tracks if t.Type() == pcbnew.PCB_VIA_T)
        tree = CK.mst(pts)
        if len(pts) < 2 or tree <= 0 or length <= 0:
            bad.append("%s: %d pads, MST %.1f, copper %.1f mm -- nothing to measure" % (n, len(pts), tree, length))
            continue
        f = length / tree
        line = ("%s: %.1f mm copper, MST %.1f mm, factor %.2f, %d vias, ~%.0f pF track (estimate)"
                % (n, length, tree, f, vias, length * PF_PER_MM))
        details.append(line)
        if f > SENSE_FACTOR + 1e-9:
            bad.append(line)
    return not bad, "%d SENSE nets, %d over %.1f x MST" % (len(nets), len(bad), SENSE_FACTOR), \
        ["SENSE length over %.1f x MST: %s" % (SENSE_FACTOR, b) for b in bad] + details


def check_pot_keepout(s, pcb_path, prefix):
    from gen import place as PL
    pots = [] if getattr(s, "pot_missing", False) else [
        r for r in s.front if s.ids.get(r, "") and r.startswith("RV")]
    if not pots:
        return False, "no pot examined", []
    boxes = {r: PL.body_box(s.board.FindFootprintByReference(r)) for r in pots}
    bad = []
    for t in s.board.GetTracks():
        on_f = t.Type() == pcbnew.PCB_VIA_T or t.GetLayerName() == "F.Cu"
        if not on_f:
            continue
        a = (_mm(t.GetStart().x), _mm(t.GetStart().y))
        b = (_mm(t.GetEnd().x), _mm(t.GetEnd().y))
        half = _mm(t.GetWidth(pcbnew.F_Cu) if t.Type() == pcbnew.PCB_VIA_T else t.GetWidth()) / 2.0
        for r, (l, tp, rr, btm) in boxes.items():
            item = ("x", "track", (a, b), half)
            if _dist(item, ("y", "pad", (l, tp, rr, btm), 0.0)) <= 0.0:
                bad.append("F.Cu copper under a pot body: %s %s at (%.2f, %.2f) in %s (%s)"
                           % (t.GetNetname(), "via" if t.Type() == pcbnew.PCB_VIA_T else "track",
                              a[0], a[1], r, _key(s, r)))
    return not bad, "%d pots, %d F.Cu items under a body" % (len(pots), len(bad)), bad


def check_planes(s, pcb_path, prefix):
    want = {} if getattr(s, "planes_missing", False) else {"In1.Cu": "GND", "In2.Cu": "SM_3V3"}
    if not want:
        return False, "no plane examined", []
    bad, details = [], []
    for z in s.board.Zones():
        if z.GetIsRuleArea():
            continue
        ln = z.GetLayerName()
        if ln in want and z.GetNetname() == want[ln]:
            n = z.GetFilledPolysList(z.GetLayer()).OutlineCount()
            details.append("%s %s: %d filled island(s)" % (ln, want[ln], n))
            if n != 1:
                bad.append("plane %s %s has %d islands, not 1" % (ln, want[ln], n))
            want = {k: v for k, v in want.items() if k != ln}
    bad += ["no filled %s zone on %s" % (v, k) for k, v in sorted(want.items())]
    smd = sum(1 for f in s.board.GetFootprints() for p in f.Pads()
              if p.GetNetname() in ("GND", "SM_3V3") and p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD)
    vias = sum(1 for t in s.board.GetTracks()
               if t.Type() == pcbnew.PCB_VIA_T and t.GetNetname() in ("GND", "SM_3V3"))
    details.append("%d SMD pads on the planes, %d plane vias" % (smd, vias))
    if vias < smd:
        bad.append("stitching: %d plane vias for %d SMD plane pads" % (vias, smd))
    if s.unresolved:
        bad += ["stitch unresolved: %s" % u for u in s.unresolved]
    return not bad, "%d planes checked, %d SMD plane pads, %d plane vias" % (2, smd, vias), bad + details


def check_rules_file(s, pcb_path, prefix):
    """The committed project file carries the rules (read and compared with
    RULES), and kicad-cli judges the routed board the same under it as under
    the project SaveBoard wrote beside `pcb_path`."""
    import json
    import shutil
    import tempfile
    here = os.path.dirname(os.path.abspath(__file__))
    pro = os.path.join(here, "kicad", "reva.kicad_pro")
    if getattr(s, "rules_missing", False):
        pro = pro + ".missing"
    try:
        rules = json.load(open(pro, encoding="utf-8"))["board"]["design_settings"]["rules"]
    except (OSError, ValueError, KeyError) as e:
        return False, "the committed project file carries no rules: %s" % e, []
    bad = ["%s is %r in %s, not %r" % (k, rules.get(k), os.path.basename(pro), v)
           for k, v in sorted(RULES.items()) if rules.get(k) != v]
    d = tempfile.mkdtemp(prefix="rulesfile_")
    try:
        shutil.copyfile(pcb_path, os.path.join(d, "reva.kicad_pcb"))
        shutil.copyfile(pro, os.path.join(d, "reva.kicad_pro"))
        mine = PP.drc(os.path.join(d, "reva.kicad_pcb"), os.path.join(d, "r.rpt"))
    finally:
        shutil.rmtree(d, ignore_errors=True)
    txt, err = _drc(s, pcb_path, prefix)
    if err:
        return False, err, []
    theirs = {}
    for c, _r, _f in CK.drc_blocks(txt):
        theirs[c] = theirs.get(c, 0) + 1
    diff = sorted(c for c in set(mine) | set(theirs) if mine.get(c, 0) != theirs.get(c, 0))
    bad += ["class %s: %d under the committed pro, %d under the saved one"
            % (c, mine.get(c, 0), theirs.get(c, 0)) for c in diff]
    return not bad, "%d rule values checked, %d DRC classes compared" % (len(RULES), len(set(mine) | set(theirs))), bad
```

`check_pot_keepout` picks pots by ref prefix `RV`; confirm in the report that the count equals `route.pot_refs(...)`'s from Task 6 (a known P4-1 minor: selection by prefix, not hole kind — acceptable here only if the counts match; if they do not, use the hole kind via `place._hole_index()` as route.py does).

`report(s, pcb_path, prefix)` (never gated) prints: total track length and via count per width class, router rounds and seconds, per victim the nearest aggressor (reuse `check_audio`'s detail lines by calling it), per SENSE net its line, and the router's `stats` for the victims and SENSE nets. Return `True, "reported, never gates", lines`.

`STEPS = [("routed", check_routed), ("drc", check_drc), ("rules_file", check_rules_file), ("audio", check_audio), ("lr", check_lr), ("sense", check_sense), ("pot_keepout", check_pot_keepout), ("planes", check_planes), ("report", report), ("render", render)]`

- [ ] **Step 3: Sabotages**

```python
def _sab_audio(s):
    """An LED0 track laid 3 mm beside OUT_L's longest track, on its layer."""
    from gen import kipcb
    t = max((t for t in s.board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() == "OUT_L"),
            key=lambda t: t.GetLength())
    (x1, y1), (x2, y2) = (_mm(t.GetStart().x), _mm(t.GetStart().y)), (_mm(t.GetEnd().x), _mm(t.GetEnd().y))
    L = math.hypot(x2 - x1, y2 - y1)
    nx, ny = -(y2 - y1) / L * 3.0, (x2 - x1) / L * 3.0
    kipcb.add_track(s.board, t.GetLayerName(), 0.25, "LED0", [(x1 + nx, y1 + ny), (x2 + nx, y2 + ny)])


def _sab_audio_missing(s):
    s.audio_missing = True


def _sab_lr(s):
    """An OUT_R track laid 1 mm beside OUT_L's longest track."""
    from gen import kipcb
    t = max((t for t in s.board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() == "OUT_L"),
            key=lambda t: t.GetLength())
    (x1, y1), (x2, y2) = (_mm(t.GetStart().x), _mm(t.GetStart().y)), (_mm(t.GetEnd().x), _mm(t.GetEnd().y))
    L = math.hypot(x2 - x1, y2 - y1)
    nx, ny = -(y2 - y1) / L * 1.0, (x2 - x1) / L * 1.0
    kipcb.add_track(s.board, t.GetLayerName(), 0.25, "OUT_R", [(x1 + nx, y1 + ny), (x2 + nx, y2 + ny)])


def _sab_lr_missing(s):
    s.lr_missing = True


def _sab_sense(s):
    """A 100 mm detour added to SENSE_2 (40 mm MST): factor far above 1.3."""
    from gen import kipcb
    pad = [p for f in s.board.GetFootprints() for p in f.Pads() if p.GetNetname() == "SENSE_2"][0]
    x, y = _mm(pad.GetPosition().x), _mm(pad.GetPosition().y)
    kipcb.add_track(s.board, "B.Cu", 0.25, "SENSE_2", [(x, y), (x, y + 0.001), (x + 50.0, y + 0.001), (x, y + 0.002)])


def _sab_sense_missing(s):
    s.sense_missing = True


def _sab_pot_keepout(s):
    """An F.Cu track of a pot's own wiper net laid across its body box."""
    from gen import kipcb
    from gen import place as PL
    r = sorted(x for x in s.front if x.startswith("RV"))[0]
    fp = s.board.FindFootprintByReference(r)
    l, t, rr, b = PL.body_box(fp)
    net = [p.GetNetname() for p in fp.Pads() if str(p.GetNumber()) == "2"][0]
    kipcb.add_track(s.board, "F.Cu", 0.25, net, [(l + 0.5, (t + b) / 2.0), (rr - 0.5, (t + b) / 2.0)])


def _sab_pot_keepout_missing(s):
    s.pot_missing = True


def _sab_planes(s):
    """Plane vias removed until there are fewer than SMD plane pads: at least
    one pad has lost its stitch, whatever the surplus of other plane vias."""
    v = sorted((t for t in s.board.GetTracks()
                if t.Type() == pcbnew.PCB_VIA_T and t.GetNetname() in ("GND", "SM_3V3")),
               key=lambda t: (t.GetPosition().x, t.GetPosition().y))
    smd = sum(1 for f in s.board.GetFootprints() for p in f.Pads()
              if p.GetNetname() in ("GND", "SM_3V3") and p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD)
    surplus = len(v) - smd
    for t in v[: max(0, surplus) + 1]:
        s.board.Delete(t)


def _sab_planes_missing(s):
    s.planes_missing = True


def _sab_rules_file(s):
    """rules_file reads a project file whose clearance says 0.15."""
    import json
    import tempfile
    here = os.path.dirname(os.path.abspath(__file__))
    body = json.load(open(os.path.join(here, "kicad", "reva.kicad_pro"), encoding="utf-8"))
    body["board"]["design_settings"]["rules"]["min_clearance"] = 0.15
    d = tempfile.mkdtemp(prefix="sabpro_")
    path = os.path.join(d, "reva.kicad_pro")
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(body, fh)
    s.rules_pro = path


def _sab_rules_file_missing(s):
    s.rules_missing = True
```

In `check_rules_file`, replace the line `pro = os.path.join(here, "kicad", "reva.kicad_pro")` with `pro = getattr(s, "rules_pro", None) or os.path.join(here, "kicad", "reva.kicad_pro")`, so `_sab_rules_file` takes effect.

Extend `SABOTAGES`, `TURNS_RED` and `WHY`:

| sabotage | step | WHY phrase |
|---|---|---|
| `audio` | audio | `audio clearance below 10.0 mm` |
| `audio_missing` | audio | `absent from the board` |
| `lr` | lr | `L/R spacing below 2.0 mm` |
| `lr_missing` | lr | `no L/R pair examined` |
| `sense` | sense | `SENSE length over 1.3 x MST` |
| `sense_missing` | sense | `no SENSE net examined` |
| `pot_keepout` | pot_keepout | `F.Cu copper under a pot body` |
| `pot_keepout_missing` | pot_keepout | `no pot examined` |
| `planes` | planes | `stitching:` |
| `planes_missing` | planes | `no plane examined` |
| `rules_file` | rules_file | `min_clearance is 0.15` |
| `rules_file_missing` | rules_file | `carries no rules` |

- [ ] **Step 4: Run and prove each RED**

`KIPY hardware/reva/route.py` → GREEN; paste every step line with details for audio, lr, sense, planes. If audio, lr or sense is red on the real board, STOP: that is a finding for Bastian (Global Constraints). Then each of the twelve new sabotages via `--sabotage <name>`: RED, its step red, its phrase present. Paste each red line.

- [ ] **Step 5: Commit (after ctest)**

```bash
git add hardware/reva/route_check.py
git commit -m "hw(reva): P4-2 audio, L/R, SENSE, pot keepout, plane and rules-file checks

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 8: The guard, the ownership move, the committed board, the docs

**Files:**
- Create: `hardware/reva/test_route.py`
- Modify: `hardware/reva/place.py` (`--write` copies renders only), `hardware/reva/test_place.py` (drops the committed-board comparison), `hardware/reva/test_build.py` (exemption comment), `CMakeLists.txt`, `docs/roadmap.md`, `docs/hardware/grip-test.md`
- Regenerate: `hardware/reva/kicad/reva.kicad_pcb` (`route.py --write`), `docs/hardware/routing/reva-routed-top.png`, `-bottom.png`

**Interfaces:**
- Consumes: everything above.
- Produces: ctest `reva_route_guard`.

- [ ] **Step 1: Ownership**

1. `place.py` `main()`: remove the `shutil.copyfile(pcb, COMMITTED)` line and its print; keep the render copy to `docs/hardware/placement/`; the RED/GREEN copy guard stays. Update the module docstring's `--write` sentence.
2. `test_place.py`: remove the committed-board check (the `check(os.path.exists(P.COMMITTED) ...` line) and docstring item 2; keep every other assertion. Add a comment: the committed board is routed since P4-2 and guarded by `reva_route_guard`.
3. `test_build.py`: rename `PLACED_BOARD` to `ROUTED_BOARD` and its comment to "generated by route.py (P4-2), guarded by reva_route_guard"; the failure text "which place.py owns" becomes "which route.py owns".

- [ ] **Step 2: Write `hardware/reva/test_route.py`**

Structure exactly like `hardware/reva/test_place.py` (read it first; same re-exec under KiCad's Python, same `check`, `scratch` temp dirs removed in a `finally`, same `run_text`/`red_steps`/`new_keys` helpers), with these assertions:

1. `build_in(dir)` runs `route.py --out dir`; two runs in separate processes: rc 0 and byte-identical `reva-routed.kicad_pcb`.
2. `open(R.COMMITTED, "rb").read()` equals the first run's board ("rerun route.py --write").
3. Sabotage coverage: every gated step of `RC.STEPS` (all but `report`, `render`) has `TURNS_RED[n] == n` and `TURNS_RED[n + "_missing"] == n`, both in `SABOTAGES`, and a `WHY` entry for every sabotage.
4. Known-list names: `KNOWN_PANEL` keys pass `CK.key_allowed` with `allowed` = the 18 jack ids (holes of kind `"jack"` at y 114.0, asserted to be exactly 18) ∪ SONG ∪ the two pairs; class words = `{"unrouted"} | set(RC.GATED_DRC)`.
5. Baseline: `base = R.build()`; save to a temp dir, reload, `skip_render = True`, run: no RED step.
6. For each sabotage (sorted): save base, reload, `skip_render = True`, sabotage, save, run; `TURNS_RED[name]` in the red steps and `WHY[name]` in the output.

Measure the wall time. If it exceeds 15 minutes, report it with the per-phase times before changing anything.

- [ ] **Step 3: ctest**

```cmake
# Rev A P4-2: the routed board -- two runs byte-identical, committed board
# fresh, every check green at baseline and red under its own sabotage.
add_test(NAME reva_route_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_route.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(reva_route_guard PROPERTIES TIMEOUT 1800)
```

- [ ] **Step 4: Commit the board and run everything**

1. `KIPY hardware/reva/route.py --write` → GREEN; `hardware/reva/kicad/reva.kicad_pcb` replaced; renders in `docs/hardware/routing/`.
2. `KIPY hardware/reva/place.py --write` → GREEN; `git status --short hardware/reva/kicad` shows only `reva.kicad_pcb` (from step 1).
3. RED once for the guard: back up `route_check.py`, add `"routed": {"unrouted CLOCK_BOGUS"}` → the guard FAILs on staleness and on the name rule. Restore.
4. After committing (Step 6), run the ctest subset; paste the summary line and `reva_route_guard`'s time.

- [ ] **Step 5: Docs**

- `docs/roadmap.md`, above the 2026-09-30 P4-1 entry:

  ```
  **<date> — P4-2 routing: Rev A is routed.**
  `KIPY hardware/reva/route.py` routes all <N> nets of the placed board on
  4 layers with our own router in <S> s (<V> vias, <L> mm of track): audio
  ≥ 10 mm from every LED, shift-register and SD line on its layer (worst
  <A> mm), L/R ≥ 2 mm, each SENSE net ≤ 1.3 × its spanning tree (worst
  <F>), no F.Cu under a pot body, GND and SM_3V3 one island each.
  `reva_route_guard` rebuilds it byte for byte and proves every check red.
  The committed `reva.kicad_pro` now carries the board rules. Known panel
  items wait for the panel pass as in P4-1. Spec
  `docs/superpowers/specs/2026-09-30-rev-a-p4-2-routing-design.md`;
  renders `docs/hardware/routing/`.
  ```

  Fill every `<…>` from the final run; replace the P4-1 entry's "Hand-off" sentence about the missing board rules with "(closed in P4-2)".
- `docs/hardware/grip-test.md`: after the P4-1 freeze sentence add "and `KNOWN_PANEL` in `hardware/reva/route_check.py` to be empty (P4-2 spec §5)."

- [ ] **Step 6: Commit**

```bash
git add hardware/reva/test_route.py hardware/reva/place.py hardware/reva/test_place.py hardware/reva/test_build.py hardware/reva/kicad/reva.kicad_pcb docs/hardware/routing CMakeLists.txt docs/roadmap.md docs/hardware/grip-test.md
git commit -m "hw(reva): P4-2 the routed board committed, reva_route_guard

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Self-review notes (for the controller)

- **Spec coverage:**

  | Spec | Task |
  |---|---|
  | §2.1–2.5 audio, per layer, aggressors, victims, L/R | 2 (router), 6 (adapter groups), 7 (audio, lr) |
  | §2.6 SENSE tier and length | 3 (tiers), 6 (tier_of), 7 (sense) |
  | §2.7 pot F.Cu keepout | 6 (obstacles), 7 (pot_keepout) |
  | §2.8 known list | 6 (fill), 8 (name rule in the guard) |
  | §3 probes still open | 1 |
  | §4.1 files, rules.py, committed pro, ownership | 4, 6, 8 |
  | §4.2 router additions, defaults byte-identical | 2, 3 |
  | §4.3 parameters | 4 |
  | §5 steps 1–10 | 6 (routed, drc, render), 7 (rest) |
  | §6 guard, router unit tests, renders, roadmap | 2, 3, 8 |
  | §7 risks: runtime, J_SD, unroutable rules | 1, 6 Step 3, 7 Step 4 |
- **Order dependency:** Task 4's `project_rules()` needs Task 1 Step 3's key set; Task 6 needs Tasks 2–5.
- **Deliberately probed during execution:** full-board runtime, J_SD terminals, pro keys (Task 1); hole-kind field and `s.ids` mapping, decoupler for `_sab_drc` (Task 6); the `_gap_board` geometry (Task 3).
- **Known limit carried from P4a:** off-grid terminal stubs are not in the router's occupancy; the DRC (`clearance`, `shorting_items`) is the arbiter, and the pair rule's check (Task 7) measures stubs as copper too.
