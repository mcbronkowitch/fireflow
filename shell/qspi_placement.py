"""Refuses an image whose QSPI-resident code includes anything on the audio
path, the engine-switch path or the control tick, or anything that runs
before the QSPI is memory-mapped (spec 2026-10-09-rev-a-p6b1 section 5). Code
in QSPI runs execute-in-place through the QSPI bus: fine at boot, not inside a
2 ms block, and not at all while the QSPI is being re-initialised.

Two checks, both run by shell/Makefile after every link:

1. By name, on `arm-none-eabi-nm -C -S`: nothing matching HOT_PATTERNS or
   WINDOW_PATTERNS may sit in QSPI.
2. By call graph, on `arm-none-eabi-objdump -d` plus the ELF itself: nothing
   in QSPI may be reachable from a hot or pre-QSPI root. The roots: every
   exception/interrupt handler and callback (ENTRY_PATTERNS), everything
   WINDOW_PATTERNS matches (startup, static constructors, System::Init, the
   QSPI and SDRAM drivers), every function whose address is stored anywhere
   in the image (vector table, vtables, callbacks, function pointers: an
   indirect call cannot be followed, so whatever it may reach counts), and
   every HOT_PATTERNS function the boot sequence does not reach. A
   HOT_PATTERNS function the boot sequence does reach (Part::init, called
   once from Instrument::init) is judged by the graph like any other: it is
   hot if a root reaches it. Only the boot sequence (BOOT_SEQUENCE) may call
   into QSPI. Calls from SRAM reach QSPI through linker veneers (`*_veneer`),
   whose literal word is their branch target: that word is an edge, not a
   stored address.

usage: qspi_placement.py <nm -C -S output> [<objdump -d output> <elf>]
"""
import bisect, collections, fnmatch, re, struct, sys

QSPI_LO, QSPI_HI = 0x90100000, 0x90800000
HOT_PATTERNS = [
    "spky::*::process*", "spky::*::tick*", "spky::*::follow*", "spky::*::_control_tick*",
    "spky::*Engine::*",          # every part engine: switched at runtime
    "spky::Part::*", "spky::Center::*", "spky::ModLane::*", "spky::SuperModulator::*",
    "spky::Instrument::set_*", "spky::Instrument::process*",
    "control::*", "spkymod::*", "spkyvcv::*",
    "shell::panel_scan_tick*", "shell::MuxScan::*", "shell::key_update*", "shell::pot_filter*",
    "*IRQHandler*", "*Callback*", "daisy::AudioHandle::*", "daisy::SaiHandle::*",
    "daisy::DmaHandle*", "HAL_DMA_*", "HAL_SAI_*",
    # Exception handlers and the HAL tick: SysTick is live from HAL_Init on and
    # fires during the QSPI re-init.
    "*_Handler", "HAL_IncTick", "HAL_GetTick",
    # The shell's per-block scan and the control table it drives, and the
    # foreground report loops (they run for ever, not once).
    "shell::apply_control*", "shell::find_control*", "shell::chain_word*",
    "shell::channel_at*", "shell::group_at*", "shell::group_of_step*",
    "shell::mux_channel*", "shell::scan_steps*", "shell::sense_live*",
    "shell::step_of*", "shell::step_pattern*", "shell::coupon_span*",
    "shell::panel_span*", "shell::run_*",
]

# Boot code that is not cold either: it runs before the board's own QSPI init
# has returned. DaisyPatchSM::Init() calls QSPIHandle::Init(), which takes the
# QSPI out of memory-mapped mode and puts it back; startup, the static
# constructors, the clock/MPU/cache setup in System::Init and the SDRAM init
# run before that, and DaisyPatchSM::Init and main are on the stack across it.
WINDOW_PATTERNS = [
    "Reset_Handler", "SystemInit", "__libc_init_array", "main", "_GLOBAL__sub_I_*",
    "daisy::System::*", "daisy::QSPIHandle::*", "HAL_QSPI_*", "QSPI_*",
    "SdramHandle::*", "HAL_SDRAM_*", "FMC_*",
    "HAL_Init", "HAL_MspInit", "HAL_InitTick", "HAL_RCC*", "HAL_PWR*", "HAL_MPU_*",
    "HAL_GPIO_Init",
    "daisy::patch_sm::DaisyPatchSM::Init*", "daisy::DaisySeed::Init*",
]

# The boot sequence: the only callers allowed to reach into QSPI. They stay in
# SRAM themselves (WINDOW_PATTERNS), but they are not graph roots: main calls
# the cold inits before StartAudio, DaisyPatchSM::Init calls the peripheral
# inits after its own QSPI init has returned.
BOOT_SEQUENCE = [
    "Reset_Handler", "main",
    "daisy::patch_sm::DaisyPatchSM::Init*", "daisy::DaisySeed::Init*",
]

# Entries of a hot context: always graph roots, whoever else calls them.
ENTRY_PATTERNS = ["*_Handler", "*IRQHandler*", "*Callback*"]

def _match(name, patterns):
    return any(fnmatch.fnmatchcase(name, p) for p in patterns)

def _reason(name):
    if _match(name, HOT_PATTERNS):
        return "hot code in QSPI"
    if _match(name, WINDOW_PATTERNS):
        return "runs before QSPI is memory-mapped"
    return None

def _code_symbols(nm_text):
    """(addr, size or None, name) for every code symbol of `nm -C [-S]`."""
    out = []
    for line in nm_text.splitlines():
        parts = line.split(None, 2)
        if len(parts) < 3:
            continue
        if len(parts[1]) == 1:                 # no size field
            addr, size, typ, name = parts[0], None, parts[1], parts[2]
        else:
            parts = line.split(None, 3)
            if len(parts) < 4:
                continue
            addr, size, typ, name = parts
        if typ not in ("T", "t", "W", "w"):
            continue
        try:
            out.append((int(addr, 16), int(size, 16) if size else None, name))
        except ValueError:
            continue
    return out

def violations(nm_text):
    return [name for addr, _, name in _code_symbols(nm_text)
            if QSPI_LO <= addr < QSPI_HI and _reason(name)]

_INSN = re.compile(r"^\s*([0-9a-f]+):\s+([a-z][a-z0-9.]*)\s+(.*)$")
_TARGET = re.compile(r"(?:^|[\s,])([0-9a-f]+) <")

def graph_violations(nm_text, dis_text, words):
    """[(cold function, [root, ..., cold function])] for every QSPI function a
    root reaches. `words`: (address, value) of every stored word that may be a
    code address (see elf_words)."""
    funcs = sorted((a & ~1, s, n) for a, s, n in _code_symbols(nm_text) if s)
    starts = [f[0] for f in funcs]
    by_start = {f[0]: i for i, f in enumerate(funcs)}

    def owner(addr):
        i = bisect.bisect_right(starts, addr) - 1
        return i if i >= 0 and addr < funcs[i][0] + funcs[i][1] else None

    veneer = [f[2].endswith("_veneer") for f in funcs]
    edges = collections.defaultdict(set)
    for line in dis_text.splitlines():
        m = _INSN.match(line)
        if not m or not (m.group(2).startswith("b") or m.group(2).startswith("cb")):
            continue
        t = _TARGET.search(m.group(3))
        if not t:
            continue
        src, dst = owner(int(m.group(1), 16)), owner(int(t.group(1), 16))
        if src is not None and dst is not None and src != dst:
            edges[src].add(dst)

    taken = {}
    for addr, value in words:
        tgt = by_start.get(value & ~1) if value & 1 else None
        if tgt is None:
            continue
        src = owner(addr)
        if src is not None and veneer[src]:
            edges[src].add(tgt)                 # the veneer's branch target
        else:
            taken.setdefault(tgt, "address stored at 0x%08x" % addr)

    def reach(start):
        parent = {r: None for r in start}
        queue = collections.deque(start)
        while queue:
            f = queue.popleft()
            for d in edges.get(f, ()):
                if d not in parent:
                    parent[d] = f
                    queue.append(d)
        return parent

    boot = [i for i, f in enumerate(funcs) if _match(f[2], BOOT_SEQUENCE)]
    from_boot = reach(boot)
    roots = {}                                  # index -> why
    for i, (_, _, name) in enumerate(funcs):
        if _match(name, BOOT_SEQUENCE):
            continue
        if _match(name, ENTRY_PATTERNS) or _match(name, WINDOW_PATTERNS):
            roots[i] = name
        elif _match(name, HOT_PATTERNS) and i not in from_boot:
            # A family pattern (spky::Part::*, spky::*Engine::*, ...) with no
            # caller on the boot path is called some way the graph cannot
            # see: hot. One the boot path does reach is judged by the graph.
            roots[i] = name
    for i, why in taken.items():
        if not _match(funcs[i][2], BOOT_SEQUENCE):
            roots.setdefault(i, why)

    parent = reach(list(roots))
    bad = []
    for i in sorted(parent):
        if QSPI_LO <= funcs[i][0] < QSPI_HI and not veneer[i]:
            path, f = [], i
            while f is not None:
                path.append(funcs[f][2])
                f = parent[f]
            path.reverse()
            why = roots[_root_of(parent, i)]
            if why != path[0]:
                path[0] = "%s [%s]" % (path[0], why)
            bad.append((funcs[i][2], path))
    return bad

def _root_of(parent, i):
    while parent[i] is not None:
        i = parent[i]
    return i

def elf_words(data):
    """(address, value) of every 4-aligned word that can hold a code address:
    all of every allocated non-code section, and the $d (data) stretches of
    the code sections -- literal pools, vtables, constant tables."""
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, _ = struct.unpack_from("<HHH", data, 0x2E)
    shs = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    marks = collections.defaultdict(list)       # section index -> [(addr, kind)]
    for sh in shs:
        if sh[1] != 2:                           # SHT_SYMTAB
            continue
        strtab = shs[sh[6]]
        for k in range(sh[5] // 16):
            st_name, value, _, _, _, shndx = struct.unpack_from("<IIIBBH", data, sh[4] + 16 * k)
            if data[strtab[4] + st_name:strtab[4] + st_name + 1] != b"$":
                continue
            kind = data[strtab[4] + st_name + 1:strtab[4] + st_name + 2]
            if kind in (b"a", b"t", b"d"):
                marks[shndx].append((value, kind))
    out = []
    for idx, sh in enumerate(shs):
        typ, flags, addr, off, size = sh[1], sh[2], sh[3], sh[4], sh[5]
        if not flags & 2 or typ == 8 or size < 4:      # not SHF_ALLOC, or NOBITS
            continue
        if flags & 4:                                   # SHF_EXECINSTR: $d only
            spans, ms = [], sorted(marks[idx]) + [(addr + size, b"end")]
            for (a, kind), (b, _) in zip(ms, ms[1:]):
                if kind == b"d":
                    spans.append((a, b))
        else:
            spans = [(addr, addr + size)]
        for a, b in spans:
            for w in range((a + 3) & ~3, b - 3, 4):
                out.append((w, struct.unpack_from("<I", data, off + w - addr)[0]))
    return out

if __name__ == "__main__":
    nm_text = open(sys.argv[1], encoding="utf-8").read()
    bad = violations(nm_text)
    for b in bad:
        print("QSPI placement: " + _reason(b) + ": " + b)
    reached = []
    if len(sys.argv) >= 4:
        dis_text = open(sys.argv[2], encoding="utf-8", errors="replace").read()
        words = elf_words(open(sys.argv[3], "rb").read())
        reached = graph_violations(nm_text, dis_text, words)
        for name, path in reached:
            print("QSPI placement: reached from hot or pre-QSPI code: " + name)
            print("    " + " -> ".join(path))
    sys.exit(1 if bad or reached else 0)
