"""Guard for read_coupon.py's parser. Runs as a plain script -- pytest is
not installed on this machine -- and is registered in CMakeLists.txt so
ctest actually runs it. A guard nobody runs is how the VCV panel guard
stood red for 23 days.
"""
import sys

from read_coupon import format_csv, parse_block

SAMPLE = [
    "noise before the block",
    "COUPON_BEGIN steps=2 hold_ms=5 fails=1 button=0 ret=255",
    "COUPON_CH step=0 group=0 addr=0 sense=0 raw=100 expect=3 pass=1",
    "COUPON_CH step=1 group=0 addr=1 sense=0 raw=60000 expect=1 pass=0",
    "COUPON_END",
]

# A COUPON_CH line cut short by the serial read timeout -- the exact case
# the module docstring says this reader must survive. "ra" has no "=" of its
# own, so _fields() would try int("") on it.
GARBLED = [
    "COUPON_BEGIN steps=2 hold_ms=5 fails=1 button=0 ret=255",
    "COUPON_CH step=0 group=0 addr=0 sense=0 raw=100 expect=3 pass=1",
    "COUPON_CH step=1 group=0 addr=1 sense=0 ra",
    "COUPON_END",
]


def check(name, got, want):
    if got != want:
        print("FAIL %s: got %r want %r" % (name, got, want), file=sys.stderr)
        return 1
    return 0


def main() -> int:
    bad = 0
    block = parse_block(SAMPLE)
    bad += check("steps", block["steps"], 2)
    bad += check("fails", block["fails"], 1)
    bad += check("button", block["button"], 0)
    bad += check("rows", len(block["rows"]), 2)
    bad += check("raw", block["rows"][1]["raw"], 60000)
    bad += check("pass", block["rows"][1]["pass"], 0)

    # A block that never ends is not a block. Returning a partial one would
    # report a board as clean because the tail never arrived.
    bad += check("incomplete", parse_block(SAMPLE[:-1]), None)
    bad += check("absent", parse_block(["nothing here"]), None)

    # A block that ends but lost a row in transit: COUPON_END arrived, so
    # only the row count can catch it. This is the case that makes the
    # count check load-bearing rather than decorative.
    short = SAMPLE[:3] + SAMPLE[4:]
    bad += check("row count short", parse_block(short), None)

    # A truncated COUPON_CH line must not raise out of the parser, and the
    # block it sits in must not be accepted as complete either.
    bad += check("garbled row", parse_block(GARBLED), None)

    csv = format_csv(block)
    bad += check("csv header", csv.splitlines()[0],
                 "step,group,addr,sense,raw,expect,pass")
    bad += check("csv rows", len(csv.splitlines()), 3)

    print("read_coupon guard: %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
