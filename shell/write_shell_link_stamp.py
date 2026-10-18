"""Writes a fresh link stamp for every make run, at Makefile PARSE time.

The ITCM code travels in shell-qspi.bin, the code that calls it in
shell-sram.bin. Flashed from two different builds, the SRAM image jumps into
another build's ITCM code and the board goes dark with no debugger to say
why (spec ../docs/superpowers/specs/2026-10-18-cpu-headroom-design.md
section 4.2). itcm_load.cpp keeps one copy of this stamp inside .itcm_hot and
compares it, after the copy, with the one compiled into its own code: a
mismatch is a blinking LED instead of a jump into garbage.

A random value, new on every run, so two builds never share one. Same shape
and same reason as write_git_hash.py: written while the Makefile is read, and
the dependent object deleted here, because a rule-written header can land in
the same wall-clock second as the object and not count as newer.

usage: write_shell_link_stamp.py OUTPUT [STALE_OBJECT...]
"""
import os
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit("usage: write_shell_link_stamp.py OUTPUT [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    value = int.from_bytes(os.urandom(4), "little") | 1   # never 0: erased flash reads 0xff..., zeroed RAM 0
    output.write_text("#define SHELL_LINK_STAMP 0x%08xu\n" % value, encoding="utf-8")
    for stale in sys.argv[2:]:
        Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
