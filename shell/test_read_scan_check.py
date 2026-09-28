"""Guard for read_scan_check.py. Plain asserts, exit code is the verdict --
pytest is not installed here. ctest runs it as read_scan_check_guard.

Every fixture is built the way the firmware prints (scan_check.cpp), and
each one must drive the verdict it is named for.
"""
import io
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import read_scan_check as r

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


RAIL = 63485
LEVEL = {"H": RAIL, "L": 2, "M": RAIL // 2, "U": RAIL // 2}


def p_level(step):
    return LEVEL[r.EXPECT[step]]


def sample_lines(blk=0, s_of=None, zero_of=None, spread=6, ticks=None,
                 drop=None, p_of=None):
    """One block as the firmware prints it. Defaults: a clean run -- S reads
    exactly P, arm 0 reads its predecessor's P (the previous address)."""
    p_of = p_of or p_level
    s_of = s_of or p_of
    zero_of = zero_of or (lambda s: p_of(s - 1) if s > 0 else p_of(23))
    lines = ["SHELL_SCAN_CFG blk=%d block=96 sr=48000 steps=24 git=deadbee"
             % blk]
    for arm, fn in (("S", s_of), ("P", p_of), ("0", zero_of)):
        for s in range(r.STEPS):
            if drop == (arm, s):
                continue
            v = int(round(fn(s)))
            lines.append(
                "SHELL_SCAN_CH blk=%d arm=%s step=%d group=%d ch=%d n=64 "
                "sum=%d min=%d max=%d"
                % (blk, arm, s, r.group_of(s), s if s < 16 else s - 16,
                   64 * v, max(0, v - spread // 2),
                   min(65535, v + spread - spread // 2)))
    lines.append("SHELL_SCAN_HEALTH blk=%d ticks=%d expected=4729"
                 % (blk, r.RUN_BLOCKS if ticks is None else ticks))
    lines.append("SHELL_SCAN_END blk=%d" % blk)
    return lines


def one(lines):
    blocks = r.parse_blocks(lines)
    check(len(blocks) == 1, "expected one complete block, got %d"
          % len(blocks))
    return blocks[0] if blocks else None


def quiet_report(blocks):
    return r.report(blocks, out=io.StringIO())


def test_expect_matches_the_firmware_table():
    src = (HERE / "coupon_expect.cpp").read_text(encoding="utf-8")
    tokens = re.findall(r"Expect::(\w+)", src.split("coupon_expect(int")[0])
    letter = {"High": "H", "Low": "L", "Mid": "M", "Unchecked": "U"}
    check(tuple(letter[t] for t in tokens) == r.EXPECT,
          "read_scan_check.EXPECT drifted from coupon_expect.cpp's tables")


def test_clean_run_passes():
    b = one(sample_lines())
    if b is None:
        return
    check(r.g1_span(b)[0], "clean: G1 failed")
    check(r.g2_complete(b), "clean: G2 failed")
    ok, q = r.g3_control(b)
    check(ok, "clean: G3 failed")
    check(len(q) >= r.G3_MIN_QUALIFYING, "clean: too few qualifying steps")
    check(r.criterion(b), "clean: criterion failed")
    check(quiet_report([b]) == 0, "clean: exit code not 0")


def test_contaminated_run_fails_the_criterion():
    # S reads 10 % of the predecessor's value -- what an oversampling group
    # straddling the address change would do.
    def s_of(s):
        prev = p_level(s - 1) if s > 0 else p_level(23)
        return 0.9 * p_level(s) + 0.1 * prev
    b = one(sample_lines(s_of=s_of))
    if b is None:
        return
    check(not r.criterion(b), "contaminated: criterion passed")
    check(quiet_report([b]) == 1, "contaminated: exit code not 1")


def test_a_control_that_cannot_fail_is_refused():
    # Arm 0 reading exactly P: the comparison could never have gone red.
    b = one(sample_lines(zero_of=p_level))
    if b is None:
        return
    check(not r.g3_control(b)[0], "G3 passed with arm 0 equal to P")
    check(quiet_report([b]) == 1, "G3-can't-fail: exit code not 1")


def test_too_few_qualifying_steps_is_refused():
    # Every P level the same: nothing qualifies, so G3 cannot be asserted.
    flat = lambda s: RAIL // 2
    b = one(sample_lines(p_of=flat, s_of=flat, zero_of=flat))
    if b is None:
        return
    ok, q = r.g3_control(b)
    check(not ok and q == [], "flat board: G3 did not refuse")


def test_invalid_span_fails_g1():
    def p_of(s):
        return RAIL - 1000 if s == 1 else p_level(s)   # one rail tie off
    b = one(sample_lines(p_of=p_of))
    if b is None:
        return
    check(not r.g1_span(b)[0], "open tie: G1 passed")
    check(quiet_report([b]) == 1, "open tie: exit code not 1")


def test_short_run_fails_g2():
    b = one(sample_lines(ticks=4700))
    if b is None:
        return
    check(not r.g2_complete(b), "short run: G2 passed")


def test_incomplete_block_is_not_a_block():
    check(r.parse_blocks(sample_lines(drop=("P", 7))) == [],
          "a block with a missing line was accepted")
    check(quiet_report([]) == 1, "no blocks: exit code not 1")


def test_blk_mismatch_resets():
    # A blk-4 line inside block 3 (a host that lost lines mid-block) tears
    # block 3 off; the rest of block 3 is ignored, and block 4 still parses.
    a = sample_lines(blk=3)
    b = sample_lines(blk=4)
    mixed = a[:10] + [b[1]] + a[10:] + b
    blocks = r.parse_blocks(mixed)
    check(len(blocks) == 1 and blocks[0]["cfg"]["blk"] == 4,
          "a torn block leaked into the next one")


def test_hysteresis_rule():
    b23 = one(sample_lines(spread=23))
    b0 = one(sample_lines(spread=0))
    b32 = one(sample_lines(spread=32))
    if None in (b23, b0, b32):
        return
    check(r.hysteresis([b23]) == (23, 32), "23 counts did not round to 32")
    check(r.hysteresis([b0]) == (0, 16), "0 counts did not floor at 16")
    check(r.hysteresis([b32]) == (32, 32), "32 counts moved")
    check(r.hysteresis([b23, b32])[1] == 32, "H is not the widest block's")


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("read_scan_check guard OK")
