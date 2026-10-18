"""Refuses an image whose ITCM code could run before it has been copied there
(spec docs/superpowers/specs/2026-10-18-cpu-headroom-design.md section 4.2).

The per-sample audio path executes from ITCM (itcm_hot.ld). Nothing loads
ITCM on this board -- no SWD, and the Daisy bootloader copies one block into
AXI SRAM -- so shell::itcm_load() copies the section out of QSPI at the top of
main(), after board_init() has memory-mapped the QSPI. Until then ITCM holds
garbage, and a jump into it is a dark board with no debugger to say why.

The rules, all on the linked image:

(a) The section: __itcm_hot_start__ >= 0x100 (the first 256 bytes stay
    empty: a function at address 0 made the bench silent, 2026-07-30),
    __itcm_hot_end__ above the start and <= 0x10000, and its load address
    __itcm_hot_load__ inside QSPI_COLD, i.e. inside shell-qspi.bin.
(b) Nothing that can run before the copy reaches ITCM by direct calls:
    Reset_Handler (whose call into main is judged by rule c instead), the
    static constructors, and every exception and interrupt handler. Calls
    from SRAM reach ITCM through linker veneers; a veneer's name carries its
    target ("__<mangled target>_veneer"), so the graph follows it by name.
    The audio callback reaches ITCM only through the pointer StartAudio()
    registers, after the copy -- an indirect call this graph cannot see,
    which is exactly why the callback is not a root.
(c) In main(), the call to shell::itcm_load() comes before the first call
    to any function that reaches ITCM, and it is there at all.
(d) Every function itcm_hot.ld lists is in the image, inside the section. A
    stale name would silently move a function back to SRAM and undo the
    lever without a sound, so here it is a refusal.

usage: itcm_placement.py <nm -S output, not demangled> <objdump -d output> <itcm_hot.ld>
"""
import bisect, collections, fnmatch, re, sys

ITCM_RESERVED, ITCM_END = 0x100, 0x10000
QSPI_COLD_LO, QSPI_COLD_HI = 0x90110000, 0x90800000
LOADER = "_ZN5shell9itcm_loadEv"
ROOT_PATTERNS = ["Reset_Handler", "_GLOBAL__sub_I_*", "*_Handler", "*IRQHandler*"]

_INSN = re.compile(r"^\s*([0-9a-f]+):\s+([a-z][a-z0-9.]*)\s+(.*)$")
_TARGET = re.compile(r"(?:^|[\s,])([0-9a-f]+) <")
_LISTED = re.compile(r"\*\(\.text\.([A-Za-z0-9_.$]+)\)")
_VENEER = re.compile(r"^__(.+)_veneer$")


def _symbols(nm_text):
    """({name: value} for every symbol, [(addr, size, name)] for sized code)."""
    values, code = {}, []
    for line in nm_text.splitlines():
        parts = line.split()
        if len(parts) == 3:
            addr, typ, name = parts
            size = None
        elif len(parts) == 4:
            addr, size, typ, name = parts
        else:
            continue
        try:
            a = int(addr, 16)
        except ValueError:
            continue
        values[name] = a
        if size is not None and typ in ("T", "t", "W", "w"):
            code.append((a & ~1, int(size, 16), name))
    code.sort()
    return values, code


def refusals(nm_text, dis_text, ld_text):
    """Every reason to refuse the image; [] when it may be flashed."""
    out = []
    values, code = _symbols(nm_text)
    start = values.get("__itcm_hot_start__")
    end = values.get("__itcm_hot_end__")
    load = values.get("__itcm_hot_load__")
    if start is None or end is None or load is None:
        return ["the .itcm_hot section symbols are missing (__itcm_hot_start__, "
                "__itcm_hot_end__, __itcm_hot_load__)"]
    if start < ITCM_RESERVED:
        out.append("the section starts at 0x%x, inside the reserved first 256 bytes" % start)
    if end <= start:
        out.append("the section is empty")
    if end > ITCM_END:
        out.append("the section ends at 0x%x, past the 64 KB ITCM" % end)
    if not QSPI_COLD_LO <= load < QSPI_COLD_HI:
        out.append("the section loads from 0x%08x, not from QSPI_COLD" % load)

    def in_itcm(a):
        return start <= a < end

    starts = [c[0] for c in code]
    by_name = {c[2]: i for i, c in enumerate(code)}

    def owner(a):
        i = bisect.bisect_right(starts, a) - 1
        return i if i >= 0 and a < code[i][0] + code[i][1] else None

    edges = collections.defaultdict(list)      # caller -> callees, in address order
    for line in sorted(dis_text.splitlines(), key=lambda l: int(_INSN.match(l).group(1), 16)
                       if _INSN.match(l) else -1):
        m = _INSN.match(line)
        if not m or not (m.group(2).startswith("b") or m.group(2).startswith("cb")):
            continue
        t = _TARGET.search(m.group(3))
        if not t:
            continue
        src, dst = owner(int(m.group(1), 16)), owner(int(t.group(1), 16))
        if src is not None and dst is not None and src != dst:
            edges[src].append(dst)
    for i, (_, _, name) in enumerate(code):    # a veneer stands for its target
        v = _VENEER.match(name)
        if v and v.group(1) in by_name:
            edges[i].append(by_name[v.group(1)])

    def path_to_itcm(root, skip=()):
        """The call path from root to an ITCM function, or None."""
        parent = {root: None}
        todo = collections.deque([root])
        while todo:
            i = todo.popleft()
            if in_itcm(code[i][0]):
                path = []
                while i is not None:
                    path.append(code[i][2])
                    i = parent[i]
                return list(reversed(path))
            for j in edges[i]:
                if j not in parent and code[j][2] not in skip:
                    parent[j] = i
                    todo.append(j)
        return None

    # (b)
    for i, (_, _, name) in enumerate(code):
        if any(fnmatch.fnmatchcase(name, pat) for pat in ROOT_PATTERNS):
            path = path_to_itcm(i, skip=("main",))
            if path:
                out.append("reaches ITCM before the copy: " + " -> ".join(path))
    # (c)
    if "main" not in by_name:
        out.append("no main in the image")
    else:
        copied = False
        for j in edges[by_name["main"]]:
            if code[j][2] == LOADER:
                copied = True
                break
            path = path_to_itcm(j)
            if path:
                out.append("main reaches ITCM before shell::itcm_load(): " + " -> ".join(path))
        if not copied:
            out.append("main never calls shell::itcm_load()")
    # (d)
    listed = _LISTED.findall(ld_text)
    if not listed:
        out.append("itcm_hot.ld lists no function")
    for name in listed:
        a = values.get(name)
        if a is None:
            out.append("listed in itcm_hot.ld but not in the image: " + name)
        elif not in_itcm(a & ~1):
            out.append("listed in itcm_hot.ld but not in ITCM: " + name)
    return out


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        sys.exit(1)
    bad = refusals(open(sys.argv[1], encoding="utf-8").read(),
                   open(sys.argv[2], encoding="utf-8", errors="replace").read(),
                   open(sys.argv[3], encoding="utf-8").read())
    for b in bad:
        print("ITCM placement: " + b)
    sys.exit(1 if bad else 0)
