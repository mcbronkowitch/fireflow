"""Section window means from a Rev A serial log (plan 2026-10-30-cpu-engine-kernels
Task 1): the board's CBP_WIN* lines (shell/cb_profile.cpp, read by
shell/read_cbp.py) turned into one mean per section, so a base run and a
candidate run can be compared key by key.

`tools/remote_run.py run ... --out DIR` writes DIR/serial.txt, every line with a
host time in front ("12.345 CBP_WIN2 w=3 am=.. ae=.. ..."). A window is the set
of CBP_WIN* lines that share one `w=`. Keys are "<TAG>.<field>" with TAG the
line's name minus "CBP_" (CBP_WIN2 -> "WIN2.ae"); a line with a `d=` field is
keyed per deck, "WIN10.d1.m0". `w` and `d` themselves are dropped. Every other
line (SHELL_PLAY_*, CBP_REC*, garbage) is ignored.

    section_means.py BASE_DIR... -- CAND_DIR... [--keys PREFIX...]
"""
import re
import sys
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

_KV = re.compile(r"(\w+)=(-?\d+)")
# CBP_WIN, CBP_WIN2 ... CBP_WIN10; \s after the tag keeps CBP_WINDOW out.
_LINE = re.compile(r"\bCBP_(WIN\d*)\s(.*)$")


def parse_windows(lines: Iterable[str]) -> List[Dict[str, int]]:
    """One dict per window, in order of first appearance; the lines of one
    window are merged by their `w=`. A line without `w=` is skipped."""
    order: List[int] = []
    wins: Dict[int, Dict[str, int]] = {}
    for line in lines:
        m = _LINE.search(line)
        if not m:
            continue
        kv = {k: int(v) for k, v in _KV.findall(m.group(2))}
        if "w" not in kv:
            continue
        w = kv.pop("w")
        deck = kv.pop("d", None)
        prefix = m.group(1) if deck is None else "%s.d%d" % (m.group(1), deck)
        if w not in wins:
            wins[w] = {}
            order.append(w)
        for k, v in kv.items():
            wins[w]["%s.%s" % (prefix, k)] = v
    return [wins[w] for w in order]


def run_means(lines: Iterable[str], skip: int = 1) -> Dict[str, float]:
    """Mean per key over the run's windows minus the first `skip` (settle). A
    key is averaged over the windows that carry it."""
    sums: Dict[str, int] = {}
    counts: Dict[str, int] = {}
    for win in parse_windows(lines)[skip:]:
        for k, v in win.items():
            sums[k] = sums.get(k, 0) + v
            counts[k] = counts.get(k, 0) + 1
    return {k: sums[k] / counts[k] for k in sums}


def compare(base: Sequence[Dict[str, float]],
            cand: Sequence[Dict[str, float]]) -> Dict[str, Tuple[float, float, float, float]]:
    """Per key found in at least one base run and one candidate run:
    (mean of the base runs, mean of the cand runs, cand - base, spread), the
    spread being max - min over the base runs (the A/A noise to beat)."""
    out = {}
    keys = sorted({k for r in base for k in r} & {k for r in cand for k in r})
    for k in keys:
        b = [r[k] for r in base if k in r]
        c = [r[k] for r in cand if k in r]
        bm, cm = sum(b) / len(b), sum(c) / len(c)
        out[k] = (bm, cm, cm - bm, max(b) - min(b))
    return out


def _read_run(directory: str) -> Dict[str, float]:
    with open("%s/serial.txt" % directory, encoding="utf-8", errors="replace") as fh:
        return run_means(fh)


def main(argv: Sequence[str]) -> int:
    args = list(argv)
    prefixes: Optional[List[str]] = None
    if "--keys" in args:
        i = args.index("--keys")
        prefixes = args[i + 1:]
        args = args[:i]
    if "--" not in args:
        print(__doc__, file=sys.stderr)
        return 2
    i = args.index("--")
    base_dirs, cand_dirs = args[:i], args[i + 1:]
    if not base_dirs or not cand_dirs:
        print(__doc__, file=sys.stderr)
        return 2
    rows = compare([_read_run(d) for d in base_dirs], [_read_run(d) for d in cand_dirs])
    print("%-16s %12s %12s %12s %10s" % ("key", "base", "cand", "cand-base", "spread"))
    for k, (b, c, d, s) in rows.items():
        if prefixes and not any(k.startswith(p) for p in prefixes):
            continue
        print("%-16s %12.1f %12.1f %+12.1f %10.1f" % (k, b, c, d, s))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
