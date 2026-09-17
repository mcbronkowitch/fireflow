"""Reads one COUPON_BEGIN..COUPON_END block from the board's USB-CDC port
and writes it as CSV.

The firmware repeats the block forever with a one-second gap and there is no
handshake, so this simply listens until a whole block has arrived. Unlike
read_probe.py it may NOT return on the first matching line: a partial block
would report a clean board because the failing tail never came.

Find the port first:

    python -c "from serial.tools import list_ports; \
print([p.device for p in list_ports.comports()])"

Call:
    python read_coupon.py COM7 [out.csv] [timeout_seconds]
"""
import sys
import time

import serial

FIELDS = ("step", "group", "addr", "sense", "raw", "expect", "pass")


def _fields(line, prefix):
    out = {}
    for token in line[len(prefix):].split():
        key, _, value = token.partition("=")
        out[key] = int(value)
    return out


def parse_block(lines):
    """The first complete block in `lines`, or None if there is not one."""
    block = None
    for line in lines:
        line = line.strip()
        if line.startswith("COUPON_BEGIN"):
            block = _fields(line, "COUPON_BEGIN")
            block["rows"] = []
        elif line.startswith("COUPON_CH") and block is not None:
            block["rows"].append(_fields(line, "COUPON_CH"))
        elif line.startswith("COUPON_END") and block is not None:
            if len(block["rows"]) != block["steps"]:
                block = None
                continue
            return block
    return None


def format_csv(block):
    rows = [",".join(FIELDS)]
    for row in block["rows"]:
        rows.append(",".join(str(row[f]) for f in FIELDS))
    return "\n".join(rows) + "\n"


def main() -> int:
    if len(sys.argv) not in (2, 3, 4):
        raise SystemExit("usage: read_coupon.py PORT [out.csv] [timeout_s]")
    port = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    limit = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0

    lines = []
    with serial.Serial(port, timeout=1.0) as ser:
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            lines.append(ser.readline().decode("utf-8", "replace"))
            block = parse_block(lines)
            if block is not None:
                break
        else:
            block = None

    if block is None:
        print("no complete COUPON block within %.0f s" % limit, file=sys.stderr)
        return 1

    csv = format_csv(block)
    if out:
        with open(out, "w", encoding="utf-8", newline="") as fh:
            fh.write(csv)
    else:
        sys.stdout.write(csv)
    print("fails=%d button=%d" % (block["fails"], block["button"]),
          file=sys.stderr)
    return 1 if block["fails"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
