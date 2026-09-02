#!/usr/bin/env python3
"""The coupon's analog rules, measured on the saved board -- never asserted.

Runs under KiCad's own Python (see kipcb.py). Loads `coupon.kicad_pcb` FRESH
via `kipcb.load` for every call -- this checks the artifact that ships, not
whatever a caller happens to still have in memory.

Five GATED rules (plan Task 5, spec Section 4):

  1. COM is sacred: MUX16_COM/MUX8_COM <=15.0 mm of copper, 0 vias, F.Cu only.
  2. Audio gets distance: AUDIO_* tracks >=10.0 mm from SR_CLK/LED_* tracks,
     measured OUTSIDE the SM footprint's own pad field (controller Ruling C --
     inside it the pin spacing is Electrosmith's, not a layout choice).
  3. Every part sits inside its placement.DOMAIN zone rect (analog inside the
     AGND rect, digital inside GND's; `seam` parts exempt). This is the
     plan's rule 3, which controller Ruling A confirms is NOT the spec's rule
     3 -- see the tie measurement below for that one.
  4. SR_CLK is one run, SM -> 595 -> 595 -> 165: every point its copper
     touches has degree <=2, i.e. no stubs.
  5. Decoupling is placement: every 100n <=2.0 mm from its chip's VCC pad
     (pad-centre distance); every bulk cap <=8.0 mm from the nearest OTHER
     pad on the rail it decouples (pad-centre distance) -- for the two
     plane-fed rails (+3V3, A+3V3) that is whatever real load sits closest,
     since the filled zone already makes the whole region one node; for the
     two track-only rails (+-12 V, never poured -- see build_pcb.PLANE_NETS)
     it is necessarily the SM pin or the IDC pin, because those are the only
     other pads +-12 V ever reaches.

Plus one UNGATED measurement, per controller Ruling A: the eight 0R
neighbour-tie distances (spec Section 4 rule 3, which the plan's rule 3 does
not cover). `R_HI1..4`/`R_LO1..4` pad 2 (the channel-net side) to the mux pin
carrying that same channel net -- the only other pad on it, so "nearest other
same-net pad" is exactly "the mux channel pin it ties," no special-casing
needed. Measured and reported in `measurements()`, never gated: three of the
eight are known to be far (R_HI3 ~20.6 mm, R_HI4/R_LO4 ~22 mm) for reasons
recorded in `placement.py`'s own comments and task-4-report.md Sections 8 and
11 -- a placement-level rework this task must not attempt.
"""
import argparse
import math
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D
import kipcb
import netlist as NL
import placement as P
import pcbnew

# Net names come from design.py -- the single source of truth build_pcb.py
# itself builds the board from -- not from a second hardcoded copy in this
# file. A rename in design.py now changes what this checker looks for too,
# instead of quietly leaving it looking for a net that no longer exists.
COM_NETS = (D.COM16, D.COM8)
COM_MAX_MM = 15.0

AUDIO_MIN_MM = 10.0

# chip ref -> (its footprint's symbol lib_id, for the VCC PIN NUMBER, not net).
# Net-based pad lookup is not safe here: 74HC595's SRCLR~ (pin 10) is tied to
# +3V3 right alongside the real VCC (pin 16, see netlist.py's by_name calls),
# so "any pad on +3V3" finds two candidates and, depending on `Pads()`
# iteration order, can silently pick the wrong one -- probed and confirmed:
# it picked pin 10, 9.458 mm from C_SR1, instead of pin 16's real 1.84 mm.
# Resolving the pin NUMBER through the same symbol lookup netlist.py used to
# build the board (`sym.by_name("VCC")`) removes the ambiguity entirely.
DECOUPLE_CHIP = {
    "C_M16": ("U_MUX16", "74xx:CD74HC4067M"),
    "C_M8": ("U_MUX8", "74xx:74HC4051"),
    "C_SR1": ("U_SR1", "74xx:74HC595"),
    "C_SR2": ("U_SR2", "74xx:74HC595"),
    "C_IN1": ("U_IN1", "74xx:74HC165"),
}
DECOUPLE_MAX_MM = 2.0

BULK_CAPS = ("C_BP12", "C_BN12", "C_B3V3", "C_BA3V3")
BULK_MAX_MM = 8.0

TIES = ("R_HI1", "R_LO1", "R_HI2", "R_LO2", "R_HI3", "R_LO3", "R_HI4", "R_LO4")

DOMAIN_RECT = {
    "analog": P.ZONE_RECTS[("In1.Cu", "AGND")],
    "digital": P.ZONE_RECTS[("In1.Cu", "GND")],
}


def _bbox(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return min(xs), min(ys), max(xs), max(ys)


DOMAIN_BBOX = {name: _bbox(pts) for name, pts in DOMAIN_RECT.items()}


# --- small board-reading helpers --------------------------------------------

def _mm(vec):
    return pcbnew.ToMM(vec.x), pcbnew.ToMM(vec.y)


def _footprint(board, ref):
    return board.FindFootprintByReference(ref)


def _pad_pos(board, ref, padnum):
    fp = _footprint(board, ref)
    if fp is None:
        return None
    for pad in fp.Pads():
        if pad.GetNumber() == padnum:
            return _mm(pad.GetPosition())
    return None


def _nearest_other_pad_mm(board, ref, net, origin):
    """Straight-line distance from `origin` (mm) to the nearest pad of a
    DIFFERENT footprint that carries `net`, plus that pad's label. `None,
    None` if no other pad on the net exists at all."""
    best, best_label = None, None
    for fp in board.GetFootprints():
        other_ref = fp.GetReference()
        if other_ref == ref:
            continue
        for pad in fp.Pads():
            if pad.GetNetname() != net:
                continue
            x, y = _mm(pad.GetPosition())
            d = math.hypot(x - origin[0], y - origin[1])
            if best is None or d < best:
                best, best_label = d, "%s.%s" % (other_ref, pad.GetNumber())
    return best, best_label


def _seg_point_dist(p, a, b):
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


def _seg_seg_dist(s1, s2):
    """Closest approach between two segments. A min over the four
    endpoint-to-opposite-segment distances is not exact for a crossing pair,
    but different-net copper on this board never crosses (DRC's own
    clearance gate already forbids it), so it is exact for everything this
    checker is ever asked to measure."""
    a, b = s1
    c, d = s2
    return min(_seg_point_dist(a, c, d), _seg_point_dist(b, c, d),
               _seg_point_dist(c, a, b), _seg_point_dist(d, a, b))


def _inside_shadow_interval(seg):
    """Liang-Barsky: the segment-parameter range `t in [0,1]` (start=0,
    end=1) over which `seg` is inside `placement.SM_SHADOW`, or `None` if it
    never enters. Standard box-clip algorithm; only the direction of use is
    unusual here -- callers want the OUTSIDE portion, i.e. the complement of
    this interval, not the clipped-to-box segment the algorithm is normally
    used to produce."""
    x0, y0, x1, y1 = P.SM_SHADOW
    (ax, ay), (bx, by) = seg
    dx, dy = bx - ax, by - ay
    tmin, tmax = 0.0, 1.0
    for p, q in ((-dx, ax - x0), (dx, x1 - ax), (-dy, ay - y0), (dy, y1 - ay)):
        if p == 0:
            if q < 0:
                return None
            continue
        r = q / p
        if p < 0:
            if r > tmax:
                return None
            tmin = max(tmin, r)
        else:
            if r < tmin:
                return None
            tmax = min(tmax, r)
    if tmin > tmax:
        return None
    return tmin, tmax


def _clip_outside_shadow(seg):
    """`[sub-segments]` of `seg` lying outside `placement.SM_SHADOW` --
    controller Ruling C: inside the module's own pad field the pin spacing
    is Electrosmith's, not a layout choice, so rule 2 must not gate on it.
    A segment entirely inside returns `[]`; entirely outside returns `[seg]`
    unchanged; one that leaves the module (audio/SR_CLK both have segments
    starting at an SM pad) is clipped AT the shadow boundary, keeping only
    the part this layout actually controls -- not the whole segment, which
    would still let the excluded portion's inside-shadow endpoint dominate
    the distance measurement (probed: it did, 8.46 mm from an SM pad to a
    digital track's own inside-shadow endpoint, before this clip)."""
    interval = _inside_shadow_interval(seg)
    if interval is None:
        return [seg]
    tmin, tmax = interval
    (ax, ay), (bx, by) = seg
    dx, dy = bx - ax, by - ay
    out = []
    if tmin > 0.0:
        out.append(((ax, ay), (ax + tmin * dx, ay + tmin * dy)))
    if tmax < 1.0:
        out.append(((ax + tmax * dx, ay + tmax * dy), (bx, by)))
    return out


def _track_segments(board, net_pred):
    """`[(net, (x1,y1), (x2,y2))]` for every `PCB_TRACE_T` whose net passes
    `net_pred`. Vias and arcs excluded on purpose -- callers that need vias
    ask `board.GetTracks()` themselves and filter on `PCB_VIA_T`."""
    out = []
    for t in board.GetTracks():
        if t.Type() != pcbnew.PCB_TRACE_T:
            continue
        net = t.GetNetname()
        if not net_pred(net):
            continue
        out.append((net, _mm(t.GetStart()), _mm(t.GetEnd())))
    return out


# --- rule 1: COM ------------------------------------------------------------

def _com_lengths(board):
    """`{net: (total_mm, via_count, [non-F.Cu layer names], item_count)}`."""
    out = {net: (0.0, 0, [], 0) for net in COM_NETS}
    for t in board.GetTracks():
        net = t.GetNetname()
        if net not in COM_NETS:
            continue
        total, vias, bad_layers, count = out[net]
        count += 1
        if t.Type() == pcbnew.PCB_VIA_T:
            vias += 1
        elif t.Type() == pcbnew.PCB_TRACE_T:
            total += pcbnew.ToMM(t.GetLength())
            if t.GetLayerName() != "F.Cu":
                bad_layers = bad_layers + [t.GetLayerName()]
        out[net] = (total, vias, bad_layers, count)
    return out


def check_com(board, violations, meas):
    lengths = _com_lengths(board)
    for net in COM_NETS:
        total, vias, bad_layers, count = lengths[net]
        if count == 0:
            # A net matching zero items reports a passing 0.0 mm / 0 vias
            # by construction -- the same silent-pass shape as rule 4's
            # zero-match bug. Flag it explicitly instead of letting it look
            # identical to a genuinely clean, fully-measured net.
            violations.append(
                "%s has no copper/vias on the board to measure -- net "
                "missing, unrouted, or renamed away from design.py" % net)
            continue
        if total > COM_MAX_MM:
            violations.append("%s copper %.3f mm exceeds %.1f mm"
                               % (net, total, COM_MAX_MM))
        if vias:
            violations.append("%s has %d via(s), must be via-free" % (net, vias))
        if bad_layers:
            violations.append("%s has segment(s) off F.Cu: %s"
                               % (net, ", ".join(sorted(set(bad_layers)))))
    meas["com16_mm"] = lengths[D.COM16][0]
    meas["com8_mm"] = lengths[D.COM8][0]


# --- rule 2: audio clearance -------------------------------------------------

AUDIO_NETS = (D.AUDIO_L, D.AUDIO_R)


def _is_audio(net):
    return net in AUDIO_NETS


def _is_digital_switch(net):
    # LED_n has no single design.py constant to source from -- it is built
    # as "LED_%d" % n directly in build_pcb.py's SR-chain wiring, not a
    # named net like SR_CLK/COM/AUDIO. The prefix stays a literal here.
    return net == D.SR_CLK or net.startswith("LED_")


def check_audio_clearance(board, violations, meas):
    audio = [sub for _n, a, b in _track_segments(board, _is_audio)
             for sub in _clip_outside_shadow((a, b))]
    digital = [sub for _n, a, b in _track_segments(board, _is_digital_switch)
               for sub in _clip_outside_shadow((a, b))]
    worst = None
    for a_seg in audio:
        for d_seg in digital:
            d = _seg_seg_dist(a_seg, d_seg)
            if worst is None or d < worst:
                worst = d
    meas["audio_clearance_mm"] = worst
    if worst is None:
        # No audio segment, no digital segment, or both -- outside the SM
        # courtyard -- were found to measure between. `run()`'s violation
        # list is the actual gate interface; leaving this silent (as
        # opposed to build_pcb.py's proof print, which happened to crash
        # on `%.3f` % None) let the rule pass with nothing examined.
        violations.append(
            "audio clearance has no AUDIO_*/SR_CLK/LED_* track segments "
            "outside the SM courtyard to measure between")
    elif worst < AUDIO_MIN_MM:
        violations.append("audio clearance %.3f mm below %.1f mm" % (worst, AUDIO_MIN_MM))


# --- rule 3: DOMAIN containment ---------------------------------------------

def check_domain(board, violations):
    """Footprint CENTRE against the zone rect, not the courtyard box.

    `check_shadow()` in build_pcb.py uses courtyards on purpose -- it is
    asking "does any copper reach under the module's body" -- but rule 3 asks
    a coarser question, "is this part broadly in the right ground-plane
    region," and the courtyard box makes that too strict on this board: the
    audio jack's shell is drawn hanging off the board edge by design (its own
    footprint marks where the physical edge cutout belongs), and the two 0R
    COM-to-sense links (R_S16, R_S8) sit deliberately close to the moat --
    6.73 mm of straight vertical run to the module's sense pins, placement.py
    Sec. "analog: the bottom band, right" -- so their fat hand-solder-pad
    courtyard peeks 1.5 mm across the boundary even though the part itself is
    correctly on its own side. Probed: courtyard-box containment flags all
    three on the real board; centre containment does not, and does still
    catch a part placed in the wrong region outright.
    """
    for ref, domain in sorted(P.DOMAIN.items()):
        if domain == "seam":
            continue
        fp = _footprint(board, ref)
        if fp is None:
            # Matches check_bulk's/check_decoupling's own "missing means a
            # violation, not a skip" -- a stale or hand-edited artifact
            # that dropped a DOMAIN ref's footprint must not look like a
            # part that is simply, correctly, inside its zone.
            violations.append(
                "%s (%s) has no footprint on the board to check DOMAIN "
                "containment" % (ref, domain))
            continue
        x, y = _mm(fp.GetPosition())
        x0, y0, x1, y1 = DOMAIN_BBOX[domain]
        if not (x0 <= x <= x1 and y0 <= y <= y1):
            violations.append(
                "%s (%s) centred at (%.2f,%.2f) is outside the %s zone (%.2f,%.2f)-(%.2f,%.2f)"
                % (ref, domain, x, y, domain, x0, y0, x1, y1))


# --- rule 4: SR_CLK single path ----------------------------------------------

def check_sr_clk_path(board, violations, meas):
    segments = _track_segments(board, lambda n: n == D.SR_CLK)
    meas["sr_clk_segments"] = len(segments)
    if not segments:
        # The vacuous-gate shape this rule used to have: a net rename, a
        # routing regression that drops all SR_CLK copper, or any drift
        # between D.SR_CLK and reality left `degree` empty and `branches`
        # empty, so the rule reported green having examined nothing. An
        # explicit zero-match check turns that silence into a violation.
        violations.append(
            "SR_CLK has no track segments to measure -- net missing, "
            "unrouted, or renamed away from design.SR_CLK")
        return
    degree = {}
    for net, a, b in segments:
        for pt in (a, b):
            key = (round(pt[0], 3), round(pt[1], 3))
            degree[key] = degree.get(key, 0) + 1
    branches = sorted(k for k, v in degree.items() if v > 2)
    if branches:
        violations.append("SR_CLK has %d branch point(s): %s" % (
            len(branches), ", ".join("(%.3f,%.3f)" % k for k in branches[:5])))


# --- rule 5: decoupling ------------------------------------------------------

def check_decoupling(board, violations, meas):
    worst = None
    for cap_ref, (chip_ref, lib_id) in sorted(DECOUPLE_CHIP.items()):
        cap_pos = _pad_pos(board, cap_ref, "1")
        vcc_pin = str(NL.load(lib_id).by_name("VCC"))
        chip_pos = _pad_pos(board, chip_ref, vcc_pin)
        if cap_pos is None or chip_pos is None:
            violations.append("%s or %s missing a VCC pad to measure" % (cap_ref, chip_ref))
            continue
        d = math.hypot(cap_pos[0] - chip_pos[0], cap_pos[1] - chip_pos[1])
        if worst is None or d > worst:
            worst = d
        if d > DECOUPLE_MAX_MM:
            violations.append("%s is %.3f mm from %s's VCC pad (limit %.1f mm)"
                               % (cap_ref, d, chip_ref, DECOUPLE_MAX_MM))
    meas["worst_decoupler_mm"] = worst


def check_bulk(board, violations):
    for ref in BULK_CAPS:
        fp = _footprint(board, ref)
        if fp is None:
            violations.append("%s not on the board" % ref)
            continue
        net, origin = None, None
        for pad in fp.Pads():
            if pad.GetNumber() == "1":
                net = pad.GetNetname()
                origin = _mm(pad.GetPosition())
        if net is None:
            violations.append("%s has no pad 1" % ref)
            continue
        d, other = _nearest_other_pad_mm(board, ref, net, origin)
        if d is None:
            violations.append("%s has no other pad on %s to measure against" % (ref, net))
            continue
        if d > BULK_MAX_MM:
            violations.append("%s is %.3f mm from nearest %s pad %s (limit %.1f mm)"
                               % (ref, d, net, other, BULK_MAX_MM))


# --- ungated: 0R tie distances (spec rule 3, controller Ruling A) ----------

def measure_ties(board):
    tie_mm = {}
    for ref in TIES:
        fp = _footprint(board, ref)
        if fp is None:
            continue
        net, origin = None, None
        for pad in fp.Pads():
            if pad.GetNumber() == "2":
                net = pad.GetNetname()
                origin = _mm(pad.GetPosition())
        if net is None:
            continue
        d, _other = _nearest_other_pad_mm(board, ref, net, origin)
        tie_mm[ref] = d
    worst = max((v for v in tie_mm.values() if v is not None), default=None)
    return tie_mm, worst


# --- entry points -------------------------------------------------------------

def _evaluate(board):
    violations = []
    meas = {}
    check_com(board, violations, meas)
    check_audio_clearance(board, violations, meas)
    check_domain(board, violations)
    check_sr_clk_path(board, violations, meas)
    check_decoupling(board, violations, meas)
    check_bulk(board, violations)
    tie_mm, worst_tie = measure_ties(board)
    meas["tie_mm"] = tie_mm
    meas["worst_tie_mm"] = worst_tie
    return violations, meas


def run(pcb_path):
    board = kipcb.load(pcb_path)
    violations, _meas = _evaluate(board)
    return violations


def measurements(pcb_path):
    board = kipcb.load(pcb_path)
    _violations, meas = _evaluate(board)
    return meas


# --- --sabotage, Step 2's RED proof only -------------------------------------

def _remove_net(board, net_name):
    """Delete every track/via on `net_name` -- stands in for either a
    routing regression that drops a net's copper outright, or a net rename
    that leaves nothing on the board still answering to the old name.

    `board.Delete(item)`, not `board.Remove(item)`: `Remove()` hands
    ownership back to Python and waits for garbage collection to free the
    C++ object (probed: leaves it a dangling wrapper that reliably
    corrupts the very next `kipcb.save()`/`kipcb.load()` round-trip --
    `SaveBoard` writes something `LoadBoard` cannot parse back into a
    `pcbnew.BOARD`, so a later `board.GetTracks()` call fails with
    `AttributeError: 'SwigPyObject' object has no attribute 'GetTracks'`).
    `Delete()` frees the C++ object immediately and round-trips clean."""
    for t in list(board.GetTracks()):
        if t.GetNetname() == net_name:
            board.Delete(t)


def _sabotage(rule, pcb_path):
    """Copy `pcb_path` into a fresh scratch directory (`tempfile.mkdtemp`,
    never the repo copy) and perturb ONE thing in it, returning the copy's
    path. Exists only so this checker can be proven capable of going red;
    nothing in the normal build calls it.

    One mode per gated rule (`com`, `audio`, `sr_clk`, plus the pre-existing
    `decoupling`), and one zero-match mode per rule that gained a
    zero-match guard in this fix round (`com_missing`, `audio_missing`,
    `sr_clk_missing`, `domain_missing`) -- proof that a rule which measures
    nothing now reports a violation instead of a silent pass."""
    tmp_dir = tempfile.mkdtemp(prefix="check_layout_sabotage_")
    tmp_path = os.path.join(tmp_dir, os.path.basename(pcb_path))
    shutil.copy(pcb_path, tmp_path)
    board = kipcb.load(tmp_path)
    if rule == "decoupling":
        # Rule 5: move C_M16 10 mm from its VCC pad -- it stops being within
        # the 2.0 mm bound no matter which direction it moves.
        fp = board.FindFootprintByReference("C_M16")
        pos = fp.GetPosition()
        fp.SetPosition(pcbnew.VECTOR2I(pos.x + pcbnew.FromMM(10.0), pos.y))
    elif rule == "com":
        # Rule 1: MUX16_COM must be via-free. The via has to land ON an
        # existing MUX16_COM track endpoint, not just anywhere on the
        # board -- probed: an arbitrary point inside the filled AGND zone
        # (COM16 sits in the analog region) gets its net silently
        # reassigned to AGND by the save/reload round-trip, because an
        # unconnected via touching only a zone pour is read back as that
        # zone's net, not the net it was authored with.
        _n, (x0, y0), _end = _track_segments(board, lambda n: n == D.COM16)[0]
        kipcb.add_via(board, (x0, y0), D.COM16)
    elif rule == "com_missing":
        # Rule 1's new zero-match guard.
        _remove_net(board, D.COM16)
    elif rule == "audio":
        # Rule 2: drop a new SR_CLK stub 3 mm from an existing
        # outside-courtyard AUDIO_* segment's own endpoint -- well inside
        # the 10 mm bound. Not AT that point: probed, copper landing
        # exactly on an existing different-net track's coordinate gets
        # merged onto that track's net by the save/reload round-trip (the
        # same physical-overlap reassignment `com`'s via sabotage hit),
        # which would silently turn the stub into more AUDIO_* copper
        # instead of digital copper close to it.
        segs = [sub for _n, a, b in _track_segments(board, _is_audio)
                for sub in _clip_outside_shadow((a, b))]
        (ax, ay), _ = segs[0]
        kipcb.add_track(board, "F.Cu", 0.25, D.SR_CLK,
                         [(ax + 3.0, ay), (ax + 4.0, ay)])
    elif rule == "audio_missing":
        # Rule 2's new zero-match guard: remove every AUDIO_* item so the
        # segment-to-segment loop has nothing to iterate -- `worst` stays
        # `None` regardless of what the digital side looks like.
        for net in AUDIO_NETS:
            _remove_net(board, net)
    elif rule == "sr_clk":
        # Rule 4: a third segment at a point two SR_CLK segments already
        # share (degree 2, a straight-through joint) makes it a branch.
        degree = {}
        for _n, a, b in _track_segments(board, lambda n: n == D.SR_CLK):
            for pt in (a, b):
                key = (round(pt[0], 3), round(pt[1], 3))
                degree[key] = degree.get(key, 0) + 1
        joint = next(k for k, v in degree.items() if v == 2)
        kipcb.add_track(board, "F.Cu", 0.25, D.SR_CLK,
                         [joint, (joint[0] + 1.0, joint[1])])
    elif rule == "sr_clk_missing":
        # Rule 4's new zero-match guard -- THE blocking-defect proof: with
        # every SR_CLK item gone, `degree`/`branches` build from nothing,
        # exactly the shape a net rename or a routing regression produces.
        _remove_net(board, D.SR_CLK)
    elif rule == "domain_missing":
        # Rule 3's new missing-footprint guard: delete an analog-domain
        # part (RV1) outright, simulating a stale/hand-edited artifact.
        # `Delete()`, not `Remove()` -- see `_remove_net`'s docstring.
        board.Delete(board.FindFootprintByReference("RV1"))
    else:
        raise ValueError("unknown --sabotage rule: %s" % rule)
    kipcb.save(board, tmp_path)
    return tmp_path


SABOTAGE_MODES = ["decoupling", "com", "com_missing", "audio",
                   "audio_missing", "sr_clk", "sr_clk_missing",
                   "domain_missing"]


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("pcb_path")
    ap.add_argument("--sabotage", choices=SABOTAGE_MODES, default=None,
                     help="RED-proof only: perturb a scratch copy before "
                          "checking; never touches pcb_path itself")
    args = ap.parse_args()

    path = args.pcb_path
    if args.sabotage:
        path = _sabotage(args.sabotage, path)
        print("sabotaged copy: %s" % path)

    result = run(path)
    if result:
        for line in result:
            print("RED:", line)
        sys.exit(1)
    print("check_layout: 0 violations")
