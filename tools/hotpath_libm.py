"""libm calls on the audio hot path of a shell ELF (plan 2026-10-29-cpu-hunt-remote
Task 4, spec section 5.1).

    python tools/hotpath_libm.py shell/build/shell.elf
    python tools/hotpath_libm.py --dis shell.dis      # an objdump -d text instead

Runs `arm-none-eabi-objdump -d` and prints, per hot function, every direct call
to a libm symbol as `function → symbol × count`, most calls first.

"Hot" is the UNION of two sets, closed under direct calls:
  1. every function placed in `.itcm_hot` (shell/itcm_hot.ld's hand-picked set);
  2. every function reachable from `AudioCallback` (any symbol whose name
     contains "AudioCallback") through direct calls.
A direct call is `bl`, `blx <sym>`, or a `b`/`b.w` whose target is ANOTHER
function's entry (a tail call); a long-branch veneer `__X_veneer` counts as a
call to X. Indirect calls (`blx r3`, the engines' virtual process()) are not
followed: their targets are not in the disassembly. That is the gap set 1
closes for the code that was put in ITCM, and the reason the union is used.
Functions that are themselves libm symbols (the list, LIBM_OTHER, and libm's
__ieee754_* / __kernel_* / __math_* internals) are neither listed nor traversed.
A second block lists calls to LIBM_OTHER (sqrtf, lroundf, log10f, ...) from the
same hot set.
"""
import argparse, os, re, shutil, subprocess, sys
from collections import Counter, defaultdict, deque

LIBM = frozenset({"powf", "expf", "logf", "sinf", "cosf", "tanhf",
                  "pow", "exp", "log", "sin", "cos", "fmodf"})
# Other libm entry points, reported in a second block of the CLI output (not
# by report()): the plan's list above is the scan's contract, these are seen.
LIBM_OTHER = frozenset({"log10f", "log2f", "exp2f", "sqrtf", "tanf", "atanf", "atan2f",
                        "asinf", "acosf", "sinhf", "coshf", "floorf", "ceilf", "roundf",
                        "lroundf", "lrintf", "cbrtf", "fmod", "sqrt", "tan", "atan2"})
# libm's own internals: never traversed, never listed as a hot caller.
_LIBM_INTERNAL = re.compile(r"^__(ieee754|kernel|math|ieee754_rem_pio2)_")

_SECTION = re.compile(r"^Disassembly of section (\S+):")
_FUNC    = re.compile(r"^([0-9a-f]+) <([^>]+)>:\s*$")
# "  100:\tf02d fc94 \tbl\t2402e540 <powf>" -- mnemonic, then a target with a
# symbol. `<sym+0x10>` is a branch inside a function, not a call.
_INSN    = re.compile(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{4,8}\s?)+\s+(\S+)\s+[0-9a-f]+\s+<([^>]+)>")
_CALLS   = {"bl", "blx"}
_JUMPS   = {"b", "b.w", "b.n"}


def _target(sym):
    """A veneer's real target: __X_veneer -> X (a mangled X keeps its own _Z)."""
    if sym.startswith("__") and sym.endswith("_veneer"):
        return sym[2:-len("_veneer")]
    return sym


def _is_libm(name):
    return name in LIBM or name in LIBM_OTHER or bool(_LIBM_INTERNAL.match(name))


def scan(text, symbols=LIBM):
    """Parse objdump -d text. Returns (hot functions, {func: Counter(sym)}) for
    the calls to `symbols` (default: the plan's libm list).

    Raises ValueError when there is no root at all (no .itcm_hot function and
    no AudioCallback): an empty hot set would print nothing, which reads like
    a clean result."""
    section = None
    func = None
    in_itcm = set()
    funcs = set()
    edges = defaultdict(set)        # caller -> callees (direct)
    libm = defaultdict(Counter)     # caller -> libm symbol counts
    for line in text.splitlines():
        m = _SECTION.match(line)
        if m:
            section, func = m.group(1), None
            continue
        m = _FUNC.match(line)
        if m:
            func = m.group(2)
            funcs.add(func)
            if section == ".itcm_hot":
                in_itcm.add(func)
            continue
        if func is None:
            continue
        m = _INSN.match(line)
        if not m:
            continue
        mnem, sym = m.group(1), m.group(2)
        if "+" in sym:
            continue                 # a branch inside a function
        if mnem not in _CALLS and mnem not in _JUMPS:
            continue
        callee = _target(sym)
        if callee == func:
            continue
        if callee in symbols:
            libm[func][callee] += 1
        if not _is_libm(callee):
            edges[func].add(callee)

    roots = set(in_itcm) | {f for f in funcs if "AudioCallback" in f}
    if not roots:
        raise ValueError("no root: no function in .itcm_hot and none named *AudioCallback*")
    hot = set()
    todo = deque(r for r in roots if not _is_libm(r))
    while todo:
        f = todo.popleft()
        if f in hot:
            continue
        hot.add(f)
        for c in edges.get(f, ()):
            if c not in hot:
                todo.append(c)
    return hot, {f: libm[f] for f in hot if f in libm}


def report(result):
    """`function → symbol × count` lines, most calls first, then by name."""
    _hot, calls = result
    rows = [(n, f, s) for f, cnt in calls.items() for s, n in cnt.items()]
    rows.sort(key=lambda r: (-r[0], r[1], r[2]))
    return [f"{f} → {s} × {n}" for n, f, s in rows]


def _objdump():
    for name in ("arm-none-eabi-objdump", "arm-none-eabi-objdump.exe"):
        p = shutil.which(name)
        if p:
            return p
    p = r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-objdump.exe"
    if os.path.exists(p):
        return p
    sys.exit("arm-none-eabi-objdump not found (DaisyToolchain bin on PATH?)")


def _demangle(lines):
    """Readable names beside the mangled ones, via c++filt when it exists."""
    filt = shutil.which("arm-none-eabi-c++filt") or \
        r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-c++filt.exe"
    if not os.path.exists(filt) and not shutil.which(filt):
        return lines
    if not lines:
        return lines
    mangled = [l.split(" → ")[0] for l in lines]
    r = subprocess.run([filt], input="\n".join(mangled) + "\n", capture_output=True, text=True)
    names = r.stdout.splitlines() if r.returncode == 0 else []
    if len(names) != len(lines):
        return lines
    return [l if n == m else f"{l}    [{n}]" for l, m, n in zip(lines, mangled, names)]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("elf", nargs="?", help="the shell ELF (shell/build/shell.elf)")
    ap.add_argument("--dis", help="read this objdump -d text instead of running objdump")
    ap.add_argument("--no-demangle", action="store_true")
    a = ap.parse_args(argv)
    if bool(a.elf) == bool(a.dis):
        ap.error("give exactly one of ELF or --dis")
    if a.dis:
        with open(a.dis, encoding="utf-8", errors="replace") as f:
            text = f.read()
    else:
        r = subprocess.run([_objdump(), "-d", a.elf], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"objdump failed: {r.stderr.strip()}")
        text = r.stdout
    hot, calls = scan(text)
    lines = report((hot, calls))
    if not a.no_demangle:
        lines = _demangle(lines)
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except AttributeError:
        pass
    print(f"# hot functions: {len(hot)} (.itcm_hot plus direct-call closure from AudioCallback)")
    print(f"# hot functions with libm calls: {len(calls)}, libm call sites: "
          f"{sum(sum(c.values()) for c in calls.values())}")
    for l in lines:
        print(l)
    _hot2, other = scan(text, LIBM_OTHER)
    olines = report((_hot2, other))
    if not a.no_demangle:
        olines = _demangle(olines)
    print(f"# also: other libm entry points from the same hot set ({', '.join(sorted(LIBM_OTHER))})")
    for l in olines:
        print(l)
    return 0


if __name__ == "__main__":
    sys.exit(main())
