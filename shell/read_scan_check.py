"""Reads the SHELL_SCAN_CHECK image's output and judges it.

Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
section 4.

Call:
    python read_scan_check.py PORT capture.txt [seconds]   # record, analyse
    python read_scan_check.py --file capture.txt           # analyse only

Recording writes every line the board sends, unedited, to capture.txt --
that file is what gets committed under docs/hardware/captures/. Default
recording time 60 s: ~6 run blocks of 9.46 s, minus the one the port opened
into.

Per complete block: G1 (span from arm P's ties, coupon_span()'s rules), G2
(ticks == 4729, every n == 64), G3 (arm 0 must miss P on every qualifying
step, and at least 3 must qualify), then the criterion |mean_S - mean_P| <= 8
on every step. Exit 1 on no complete block or any failure. H, the
hysteresis for the playing image, is printed across all complete blocks.

The line format was transcribed from scan_check.cpp's PrintLine calls.
"""
import re
import sys
import time

STEPS = 24
READS = 64
RUN_BLOCKS = 4729
ARMS = ("S", "P", "0")
CRITERION = 8            # counts: half an LSB of 12 bit
G3_NEIGHBOUR = 4096      # counts between a step's and its predecessor's P
G3_MIN_QUALIFYING = 3
H_QUANTUM = 16

# coupon_expect.h
TIE_SPREAD = 328
RAIL_FLOOR = 58982
RAIL_MARGIN = 1311

# coupon_expect.cpp's kMux16 then kMux8, in step order. H/L = 0 ohm tie to
# the rail / AGND, M = divider, U = pot. The guard parses the C++ file and
# fails when the two drift apart.
EXPECT = ("U", "H", "U", "L", "U", "H", "U", "L",
          "M", "M", "L", "L", "L", "L", "L", "L",
          "U", "H", "U", "L", "U", "H", "M", "L")
POT_STEPS = tuple(s for s in range(STEPS) if EXPECT[s] == "U")

_LINE = re.compile(r"^(SHELL_SCAN_(?:CFG|CH|HEALTH|END))\s+(.*)$")
_INT_KEYS = {"blk", "block", "sr", "steps", "step", "group", "ch", "n", "sum",
             "min", "max", "ticks", "expected"}


def group_of(step):
    return 0 if step < 16 else 1


def parse_line(line):
    m = _LINE.match(line.strip())
    if not m:
        return None
    fields = {}
    for tok in m.group(2).split():
        if "=" not in tok:
            return None
        k, v = tok.split("=", 1)
        if k in _INT_KEYS:
            try:
                v = int(v)
            except ValueError:
                return None
        fields[k] = v
    return m.group(1), fields


def _complete(block):
    return (block["health"] is not None
            and all((a, s) in block["ch"] for a in ARMS for s in range(STEPS)))


def parse_blocks(lines):
    """Every complete block in `lines`, in order. A block opens at _CFG and
    closes at _END; a line from another blk tears the open block off."""
    blocks, cur = [], None
    for line in lines:
        parsed = parse_line(line)
        if parsed is None:
            continue
        kind, f = parsed
        if kind == "SHELL_SCAN_CFG":
            cur = {"cfg": f, "ch": {}, "health": None}
            continue
        if cur is None:
            continue
        if f.get("blk") != cur["cfg"].get("blk"):
            cur = None
            continue
        if kind == "SHELL_SCAN_CH":
            cur["ch"][(f["arm"], f["step"])] = f
        elif kind == "SHELL_SCAN_HEALTH":
            cur["health"] = f
        elif kind == "SHELL_SCAN_END":
            if _complete(cur):
                blocks.append(cur)
            cur = None
    return blocks


def mean(block, arm, step):
    row = block["ch"][(arm, step)]
    return row["sum"] / row["n"] if row["n"] else float("nan")


def g1_span(block):
    hi = [mean(block, "P", s) for s in range(STEPS) if EXPECT[s] == "H"]
    lo = [mean(block, "P", s) for s in range(STEPS) if EXPECT[s] == "L"]
    zero, rail = sum(lo) / len(lo), sum(hi) / len(hi)
    ok = (max(hi) - min(hi) <= TIE_SPREAD and max(lo) - min(lo) <= TIE_SPREAD
          and rail >= RAIL_FLOOR and zero <= RAIL_MARGIN and rail > zero)
    return ok, zero, rail


def g2_complete(block):
    h = block["health"]
    return (h["ticks"] == RUN_BLOCKS and h["expected"] == RUN_BLOCKS
            and all(block["ch"][(a, s)]["n"] == READS
                    for a in ARMS for s in range(STEPS)))


def qualifying(block):
    out = []
    for s in range(1, STEPS):
        if group_of(s) != group_of(s - 1):
            continue
        if abs(mean(block, "P", s) - mean(block, "P", s - 1)) > G3_NEIGHBOUR:
            out.append(s)
    return out


def g3_control(block):
    q = qualifying(block)
    if len(q) < G3_MIN_QUALIFYING:
        return False, q
    return all(abs(mean(block, "0", s) - mean(block, "P", s)) > CRITERION
               for s in q), q


def deltas(block):
    return {s: mean(block, "S", s) - mean(block, "P", s) for s in range(STEPS)}


def criterion(block):
    return all(abs(d) <= CRITERION for d in deltas(block).values())


def hysteresis(blocks):
    widest = max(block["ch"][("S", s)]["max"] - block["ch"][("S", s)]["min"]
                 for block in blocks for s in POT_STEPS)
    h = -(-widest // H_QUANTUM) * H_QUANTUM
    return widest, max(h, H_QUANTUM)


def report(blocks, out=None):
    out = out or sys.stdout
    if not blocks:
        print("no complete SHELL_SCAN block", file=out)
        return 1
    failed = False
    for b in blocks:
        g1, zero, rail = g1_span(b)
        g2 = g2_complete(b)
        g3, q = g3_control(b)
        crit = criterion(b)
        print("block %d  git=%s  G1=%s (zero %.1f, rail %.1f)  G2=%s  "
              "G3=%s (%d qualifying)  criterion=%s"
              % (b["cfg"]["blk"], b["cfg"].get("git"), g1, zero, rail, g2, g3,
                 len(q), crit), file=out)
        print("  step g ch exp     P mean     S-P  S range     0-P  0-Pprev",
              file=out)
        d = deltas(b)
        for s in range(STEPS):
            row = b["ch"][("S", s)]
            prev = mean(b, "P", s - 1) if s > 0 else float("nan")
            print("  %4d %d %2d  %s  %9.1f  %+6.1f  %7d  %+6.1f  %+7.1f%s"
                  % (s, group_of(s), s if s < 16 else s - 16, EXPECT[s],
                     mean(b, "P", s), d[s], row["max"] - row["min"],
                     mean(b, "0", s) - mean(b, "P", s),
                     mean(b, "0", s) - prev,
                     "  <- over" if abs(d[s]) > CRITERION else ""), file=out)
        failed = failed or not (g1 and g2 and g3 and crit)
    widest, h = hysteresis(blocks)
    print("pot noise: widest S range %d counts over %d block(s) -> H=%d"
          % (widest, len(blocks), h), file=out)
    return 1 if failed else 0


def record(port, path, seconds):
    import serial   # imported here so the guard runs without pyserial
    with serial.Serial(port, timeout=1.0) as ser, \
            open(path, "w", encoding="utf-8", newline="\n") as dst:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            line = ser.readline().decode("utf-8", "replace")
            if line:
                dst.write(line if line.endswith("\n") else line + "\n")


def main(argv):
    if len(argv) == 3 and argv[1] == "--file":
        path = argv[2]
    elif len(argv) in (3, 4) and argv[1] != "--file":
        path = argv[2]
        record(argv[1], path, float(argv[3]) if len(argv) == 4 else 60.0)
    else:
        raise SystemExit("usage: read_scan_check.py PORT capture.txt "
                         "[seconds] | --file capture.txt")
    with open(path, encoding="utf-8", errors="replace") as f:
        blocks = parse_blocks(f.readlines())
    return report(blocks)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
