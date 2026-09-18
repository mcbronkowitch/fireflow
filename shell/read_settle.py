"""Reads one SHELL_SETTLE_CFG..SHELL_SETTLE_END block from the board's
USB-CDC port and writes it out as two CSV files.

`out.csv` carries the grid points, one row per measured point. Everything
else the block says -- the configuration, the clock spans, the calibration
pass, the gate verdicts, and each pair's offset, knee, reference and band --
goes to `out.csv.meta.csv` beside it, as `scope,pair,key,value` rows. Both
files are needed to reproduce anything: the grid points alone cannot yield
"true settle = knee + offset" (that needs SHELL_SETTLE_KNEE's `d_settle_ns`
plus SHELL_SETTLE_OFFSET's `offset_ns`, both in the metadata file) nor the
gate verdict that says whether a settle time from this run may be quoted at
all. Those used to reach stderr only, which is not a record.

The long `scope,pair,key,value` shape is deliberate: it carries whatever
key=value fields the firmware prints without this reader having to know
their names, so a new field on a printed line lands in the file without a
code change here -- the same property `_fields()` already gives the parser.

The firmware repeats the block forever with a delay and there is no
handshake, so this listens until a whole block has arrived. It may NOT
return early: a partial block would report a clean instrument because the
failing tail never came.

The block format below is settle_probe.cpp's hw.PrintLine() calls, read
directly rather than assumed -- the bring-up plan's format is stale (Tasks 4
and 5 went through ten hardware fix rounds after it was written). Printed
once per pass, in this order: SHELL_SETTLE_CFG, SHELL_SETTLE_CLK, one
SHELL_SETTLE_OFFSET per pair, each pair's whole sweep of SHELL_SETTLE points
(grid_points of them, ascending when cfg's sweep_dir=0, descending when
sweep_dir=1), SHELL_SETTLE_CAL, one SHELL_SETTLE_KNEE per pair, one
SHELL_SETTLE_REF per pair, one SHELL_SETTLE_BAND per pair, SHELL_SETTLE_GATES,
SHELL_SETTLE_END. (SHELL_SETTLE_WARMUP is printed once at boot, outside this
repeating block, and is simply not one of the lines this parser looks for.)

A knee's d_settle_ns=-1 means two different things, and they are not
collapsed here: at_or_below_offset=1 means the pair settled at or below its
own instrument offset (the expected, correct result for the reference
pairs); at_or_below_offset=0 means the pair did not settle within the grid
at all, which is a finding about the board.

Find the port first:

    python -c "from serial.tools import list_ports; \
print([p.device for p in list_ports.comports()])"

Call:
    python read_settle.py COM4 [out.csv] [timeout_seconds]

Without an out path the grid points go to stdout and the metadata is not
written anywhere durable -- that mode is for eyeballing and piping, and a
second table in the same stream would break both.

Exit code is the verdict: 1 when the run's gates did not pass, because a
run that failed a gate may not have its settle times quoted.
"""
import sys
import time
from collections import Counter

# sweep_dir is carried on every row (not just in cfg): Task 5's central
# hardware finding rests on comparing an ascending block against a
# descending one, and a CSV that cannot tell them apart cannot reproduce it.
FIELDS = ("pair", "sense", "from", "to", "d_ns", "n", "mean", "min", "max",
          "sweep_dir")

# The metadata file's columns, and which parsed keys feed them. Block-level
# lines (one per block) leave the pair column empty; per-pair lines fill it.
# The `clk` line is listed although _is_complete() does not require it: if it
# arrived it is part of the record, and if it did not the rows are simply
# absent rather than faked.
META_FIELDS = ("scope", "pair", "key", "value")
_META_BLOCK_SCOPES = (("cfg", "cfg"), ("clk", "clk"), ("cal", "cal"),
                      ("gates", "gates"))
_META_PAIR_SCOPES = (("offset", "offsets"), ("knee", "knees"),
                     ("ref", "refs"), ("band", "bands"))

_GATE_NAMES = {"g1": "reference pairs", "g2": "floor",
               "g3": "settled-region agreement", "g4": "instrument jitter"}


def _fields(line, prefix):
    out = {}
    for token in line[len(prefix):].split():
        key, _, value = token.partition("=")
        out[key] = int(value)
    return out


def _new_block():
    return {"cfg": None, "clk": None, "offsets": [], "points": [],
            "cal": None, "knees": [], "refs": [], "bands": [], "gates": None}


def _one_per_pair(items, num_pairs):
    """True iff `items` (each carrying a "pair" field) has exactly one entry
    for every pair 0..num_pairs-1.

    This is the check the splice corruption needs: USB-CDC on this machine
    has been observed to lose a whole line to libDaisy's own logger overflow
    (a line ending "$$") or to splice two lines together
    ("S$$SHELL_SETTLE_END"). A block that loses one BAND line that way must
    be refused, not silently reported as clean -- and only a per-kind,
    per-pair count catches a missing line of a kind that still has other,
    present lines of the same kind for other pairs.
    """
    return Counter(item["pair"] for item in items) == Counter(range(num_pairs))


def _is_complete(block):
    """Every completeness check the block format calls for at
    SHELL_SETTLE_END. Anything short of this must be dropped rather than
    returned -- see the module docstring."""
    if block["cfg"] is None or block["cal"] is None or block["gates"] is None:
        return False
    if not block["knees"]:
        return False
    num_pairs = max(k["pair"] for k in block["knees"]) + 1

    want_points = Counter({p: block["cfg"]["grid_points"] for p in range(num_pairs)})
    if Counter(p["pair"] for p in block["points"]) != want_points:
        return False

    return all(_one_per_pair(items, num_pairs)
               for items in (block["offsets"], block["knees"], block["refs"],
                             block["bands"]))


def parse_block(lines):
    """The first complete block in `lines`, or None if there is not one."""
    block = None
    for raw in lines:
        line = raw.strip()
        try:
            if line.startswith("SHELL_SETTLE_CFG"):
                block = _new_block()
                block["cfg"] = _fields(line, "SHELL_SETTLE_CFG")
            elif block is None:
                continue
            elif line.startswith("SHELL_SETTLE_CLK"):
                block["clk"] = _fields(line, "SHELL_SETTLE_CLK")
            elif line.startswith("SHELL_SETTLE_OFFSET"):
                block["offsets"].append(_fields(line, "SHELL_SETTLE_OFFSET"))
            elif line.startswith("SHELL_SETTLE_CAL"):
                block["cal"] = _fields(line, "SHELL_SETTLE_CAL")
            elif line.startswith("SHELL_SETTLE_KNEE"):
                block["knees"].append(_fields(line, "SHELL_SETTLE_KNEE"))
            elif line.startswith("SHELL_SETTLE_REF"):
                block["refs"].append(_fields(line, "SHELL_SETTLE_REF"))
            elif line.startswith("SHELL_SETTLE_BAND"):
                block["bands"].append(_fields(line, "SHELL_SETTLE_BAND"))
            elif line.startswith("SHELL_SETTLE_GATES"):
                block["gates"] = _fields(line, "SHELL_SETTLE_GATES")
            elif line.startswith("SHELL_SETTLE_END"):
                if not _is_complete(block):
                    block = None
                    continue
                return block
            elif line.startswith("SHELL_SETTLE "):
                point = _fields(line, "SHELL_SETTLE ")
                point["sweep_dir"] = block["cfg"]["sweep_dir"]
                block["points"].append(point)
        except (ValueError, KeyError):
            # A line the serial read timeout cut in half, or one libDaisy's
            # own logger truncated and stamped with its "$$" overflow
            # marker. Drop the block and keep listening rather than crash on
            # the board's next breath; a later SHELL_SETTLE_CFG still gets
            # its own chance.
            block = None
    return None


def format_csv(block):
    rows = [",".join(FIELDS)]
    for row in block["points"]:
        rows.append(",".join(str(row[f]) for f in FIELDS))
    return "\n".join(rows) + "\n"


def format_meta_csv(block):
    """Everything in the block that is not a grid point, as
    `scope,pair,key,value` rows.

    Field order within a scope is the order the firmware printed it, so a
    reader diffing two captures sees them line up. The pair column is empty
    for block-level scopes rather than 0, which is a real pair number."""
    rows = [",".join(META_FIELDS)]
    for scope, key in _META_BLOCK_SCOPES:
        entry = block.get(key)
        if entry is None:
            continue
        for name, value in entry.items():
            rows.append("%s,,%s,%d" % (scope, name, value))
    for scope, key in _META_PAIR_SCOPES:
        for entry in sorted(block[key], key=lambda e: e["pair"]):
            for name, value in entry.items():
                if name == "pair":
                    continue
                rows.append("%s,%d,%s,%d" % (scope, entry["pair"], name, value))
    return "\n".join(rows) + "\n"


def main() -> int:
    # Imported here, not at module scope: parse_block()/format_csv() are the
    # pure parser the guard exercises, and that guard must not need pyserial
    # installed to import this module.
    import serial

    if len(sys.argv) not in (2, 3, 4):
        raise SystemExit("usage: read_settle.py PORT [out.csv] [timeout_s]")
    port  = sys.argv[1]
    out   = sys.argv[2] if len(sys.argv) > 2 else None
    limit = float(sys.argv[3]) if len(sys.argv) > 3 else 60.0

    lines = []
    with serial.Serial(port, timeout=1.0) as ser:
        deadline = time.monotonic() + limit
        block = None
        while time.monotonic() < deadline:
            lines.append(ser.readline().decode("utf-8", "replace"))
            block = parse_block(lines)
            if block is not None:
                break

    if block is None:
        print("no complete SHELL_SETTLE block within %.0f s" % limit,
              file=sys.stderr)
        return 1

    csv = format_csv(block)
    if out:
        with open(out, "w", encoding="utf-8", newline="") as fh:
            fh.write(csv)
        meta_out = out + ".meta.csv"
        with open(meta_out, "w", encoding="utf-8", newline="") as fh:
            fh.write(format_meta_csv(block))
        print("wrote %s (grid points) and %s (block metadata: gates, "
              "offsets, knees, refs, bands)" % (out, meta_out),
              file=sys.stderr)
    else:
        sys.stdout.write(csv)
        print("no output path given -- the block metadata (gates, offsets, "
              "knees, refs, bands) was not written; pass one to keep it",
              file=sys.stderr)

    cal = block["cal"]
    print("lat_mean_ns=%d b0=%d gates_ok=%d"
          % (cal["lat_mean_ns"], cal["b0"], cal["gates_ok"]), file=sys.stderr)

    offset_by_pair = {o["pair"]: o["offset_ns"] for o in block["offsets"]}
    for k in sorted(block["knees"], key=lambda k: k["pair"]):
        pair = k["pair"]
        tag = "  (reference)" if k["reference"] else ""
        if k["d_settle_ns"] < 0 and k["at_or_below_offset"]:
            print("pair=%d settled at or below its own offset "
                  "(offset_ns=%d)%s" % (pair, offset_by_pair[pair], tag),
                  file=sys.stderr)
        elif k["d_settle_ns"] < 0:
            print("pair=%d did not settle within the grid%s" % (pair, tag),
                  file=sys.stderr)
        else:
            # True settle is the knee plus this pair's own instrument
            # offset (SHELL_SETTLE_OFFSET) -- print that sum, not the raw
            # knee, per the block format's own accounting.
            settle_ns = k["d_settle_ns"] + offset_by_pair[pair]
            print("pair=%d settle_ns=%d predicted_ns=%d%s"
                  % (pair, settle_ns, k["predicted_ns"], tag), file=sys.stderr)

    # Not gates, and deliberately not folded into the exit code: these three
    # are the firmware's HAL return codes, and the four gates are the spec's,
    # so turning one of them into a fifth verdict here would be a spec change
    # made in the reader. They are loud on stderr instead, because a run whose
    # ADC was never initialised, never calibrated or had a channel
    # configuration rejected still prints plausible counts.
    #
    # .get(), not [...]: these arrive on SHELL_SETTLE_GATES from the
    # 2026-09-18 firmware onwards, and a block from an older image simply does
    # not carry them. Absent is not the same as 0 and must not read as a
    # failure.
    for flag, what in (("init_ok", "HAL_ADC_Init"),
                       ("cal_ok", "HAL_ADCEx_Calibration_Start"),
                       ("cfg_ok", "HAL_ADC_ConfigChannel")):
        if block["gates"].get(flag) == 0:
            print("WARNING: %s=0 -- the firmware saw %s fail on this run; "
                  "every count above came from an ADC that is not set up the "
                  "way the block header says" % (flag, what), file=sys.stderr)

    if not cal["gates_ok"]:
        failed = [_GATE_NAMES[g] for g in ("g1", "g2", "g3", "g4")
                  if not block["gates"][g]]
        print("GATES FAILED (%s) -- no settle time from this run may be "
              "quoted" % ", ".join(failed), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
