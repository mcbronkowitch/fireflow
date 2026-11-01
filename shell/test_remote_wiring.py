#!/usr/bin/env python3
"""Source guard for the SHELL_REMOTE listener (spec
docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md section 5.3, plan
docs/superpowers/plans/2026-10-29-cpu-hunt-remote.md Task 2). Plain script --
pytest is not installed here -- and its exit code is the verdict.

Rev A has no SWD pins. Once an image is flashed, software is the only way back
to the bootloader that does not need Bastian's hands on BOOT and RESET, so
every one of these is a dead board if it silently goes missing:

  - on_rx() (remote_listen.cpp) runs in the USB interrupt. It enters the
    bootloader itself, because a callback that runs back to back starves the
    foreground (the audio DMA and USB are both NVIC priority 0, and the DMA
    wins), and it never delays: SysTick is priority 14, so a HAL_Delay inside
    a priority-0 handler never returns. It never prints either; the
    foreground answers PING. Its command array holds a whole 64-byte packet
    of the shortest lines, so a BOOT anywhere in the packet is seen.
  - remote_enter_bootloader() asks for the Daisy bootloader with an infinite
    timeout and does not delay. It writes the request and resets itself
    rather than calling System::ResetToBootloader(), whose STM branch holds a
    HAL_Delay(10) and calls the QSPI-resident GPIO::Init (qspi_placement.py
    refuses a link where a handler can reach it).
  - main(): with SHELL_REMOTE, remote_install_fault_vectors() is its first
    statement (a fault in board_init() must already reach the bootloader),
    and a failed itcm_load() enters the bootloader instead of blinking.
  - The vector table copy points HardFault, MemManage, BusFault and
    UsageFault at the entry, and sets SCB->VTOR to a 1 KB-aligned table.
  - run_panel_scan_report(): remote_listen_start() is the statement right
    after StartLog(), under SHELL_REMOTE.
  - Both main.cpp and panel_scan.cpp include the generated shell_remote.h: an
    `#if SHELL_REMOTE` without it is silently 0, and the image looks fine.
  - The Makefile resolves SHELL_REMOTE at parse time and deletes main.o and
    panel_scan.o when it changes (memory fireflow-bench-stale-object-trap),
    and builds the listener's sources only under SHELL_REMOTE=1.
  - The fault chain is whole: the vectors point at the naked stub, the stub
    branches to shell_remote_fault_reset(), and that calls
    remote_enter_bootloader() (a `b .` in the stub is a hang, not an entry).
    The 1 KB alignment sits on g_ram_vectors itself.
The boot-only listener (SHELL_BOOTCMD, default 1; the Makefile writes 1 only
for a play image without SHELL_REMOTE). The fault vectors, the bootloader on a
failed itcm_load() and remote_listen_start() after StartLog() are checked in
both images that listen (SHELL_REMOTE=1, and SHELL_BOOTCMD=1 alone) and must be
absent in the image with neither; besides that the boot-only image:
  - prints `SHELL_REMOTE on build=<hash> cmds=boot` on the first report pass
    and every 10th (tools/remote_lib.py's confirm_build() regex matches it)
    and answers each PING with `SHELL_REMOTE pong`;
  - keeps the persistence save (persist_write() compiles, nothing suppresses);
  - compiles AudioCallback, and all of panel_scan.cpp outside the report
    loop, exactly as the image with neither (no guard, no command poll);
  - links remote_cmd.cpp and remote_listen.cpp, never workload_run.cpp, and
    the Makefile resolves SHELL_BOOTCMD at parse time (shell_bootcmd.h).
The workloads (plan Task 5):
  - AudioCallback writes silence instead of calling process() once the
    overrun guard has tripped, under SHELL_REMOTE, and panel_scan_block_end()
    feeds the guard each callback's cycles.
  - remote_poll() runs once per block: panel_scan_tick() calls remote_block()
    once, at its top level, and remote_block() polls once on either side of
    SHELL_PERSIST_ON, never in a loop.
  - persist_write() is called only where SHELL_REMOTE is 0, so a remote image
    does not even link the save (shell/check_images_build.py holds the same
    on the linked image's symbol table).
The measurement switch (plan Task 6):
  - engine/: probe::toggles() is read exactly once, in Instrument::process
    ahead of its sample loop, and in no other engine file (spec 5.3: never a
    global read inside a per-sample loop).
  - The Makefile resolves SHELL_REMOTE_MEASURE at parse time, refuses it
    without SHELL_REMOTE, and turns the probe defines on in that image alone;
    its writer deletes every object of the build directory on a flip, because
    the probe defines reach every engine object.
  - LATE's burn runs in AudioCallback before process(), under
    SHELL_REMOTE_MEASURE, and both main.cpp and panel_scan.cpp include its
    generated header.
The final review's fix wave:
  - C1: panel_scan_late() returns before its burn while the guard is
    tripped, so bit 30 cannot keep a tripped callback back to back.
  - I2: panel_scan_block_end() feeds the guard the entry-to-entry period
    (read from g_win_prev_entry before the WIN window overwrites it), the
    guard is built with kGuardPeriodCycles, and the trip line says by=.
  - M1: remote_enter_bootloader() cleans the D-cache between the boot_info
    write and the reset.
  - M8: NMI vectors to the fault entry too (in the vector-table check).
  - out_split: busy_window() counts saturated splits, SHELL_OUT prints neg=.
The reverb grain layer (SHELL_RGRAIN, spec 2026-10-31-sd-use-cases-tests-design.md
section 5, B1 and B2):
  - Instrument::process reads set_reverb_inject()'s two pointers once each,
    ahead of its per-sample loop; the hook exists only under
    SPKY_PROBE_REVERB_INJECT, which the Makefile defines in the SHELL_RGRAIN
    images alone, the switch resolved at parse time by its writer.
Every check has a sabotage below that must turn it red.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

FAILS = []


def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        FAILS.append(name)


def read(name):
    path = os.path.join(HERE, name)
    if not os.path.exists(path):
        return ""
    with open(path, encoding="utf-8") as f:
        return f.read()


def strip_comment(line):
    return line.split("//", 1)[0]


def code_only(text):
    """The text with // comments and /* */ blocks removed."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return "\n".join(strip_comment(l) for l in text.splitlines())


def function_body(text, signature):
    i = text.find(signature)
    if i < 0:
        return ""
    j = text.find("{", i)
    if j < 0:
        return ""
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[j:k + 1]
    return ""


# --- the C preprocessor, as far as these files need it --------------------
def under_remote(cond, in_else):
    """Whether the branch of `#if cond` (or its #else) compiles only with
    SHELL_REMOTE == 1. Conservative: anything not recognised does not count."""
    cond = cond.strip()
    if "||" in cond:
        return False
    terms = [t.strip().strip("()").strip() for t in cond.split("&&")]
    if in_else:
        return cond == "!SHELL_REMOTE"
    return "SHELL_REMOTE" in terms


def under_measure(cond, in_else):
    """The same for SHELL_REMOTE_MEASURE == 1."""
    cond = cond.strip()
    if "||" in cond or in_else:
        return False
    return "SHELL_REMOTE_MEASURE" in [t.strip().strip("()").strip() for t in cond.split("&&")]


def under_no_remote(cond, in_else):
    """Whether the branch compiles only with SHELL_REMOTE == 0: the #else of a
    bare `#if SHELL_REMOTE`, or an `#if` with a `!SHELL_REMOTE` term."""
    cond = cond.strip()
    if in_else:
        return cond == "SHELL_REMOTE"
    if "||" in cond:
        return False
    return "!SHELL_REMOTE" in [t.strip().strip("()").strip() for t in cond.split("&&")]


def remote_lines(text, pred=None):
    """Line numbers (0-based) whose code compiles only under SHELL_REMOTE (or
    under whatever `pred` says about one #if branch)."""
    pred = pred or under_remote
    stack = []
    out = set()
    for i, raw in enumerate(text.splitlines()):
        line = raw.strip()
        m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", line)
        if m:
            kind, rest = m.group(1), strip_comment(m.group(2))
            if kind == "if":
                stack.append([rest, False])
            elif kind in ("ifdef", "ifndef"):
                stack.append(["<%s %s>" % (kind, rest.strip()), False])
            elif kind == "elif" and stack:
                stack[-1] = [rest, False]
            elif kind == "else" and stack:
                stack[-1][1] = True
            elif kind == "endif" and stack:
                stack.pop()
            continue
        if any(pred(c, e) for c, e in stack):
            out.add(i)
    return out


def select_branch(text, macro, value):
    """The lines that compile with `macro` == value, as far as `#if macro`,
    `#if !macro` and their #else decide; every other conditional keeps both
    branches (so a check counting calls sees at least as many as compile)."""
    out = []
    stack = []   # True / False: decided by `macro`; None: another condition
    for raw in text.splitlines():
        m = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", raw)
        if m:
            kind, rest = m.group(1), strip_comment(m.group(2)).strip()
            if kind == "if":
                stack.append(bool(value) if rest == macro else
                             (not value) if rest == "!" + macro else None)
            elif kind in ("ifdef", "ifndef"):
                stack.append(None)
            elif kind == "elif" and stack:
                stack[-1] = None
            elif kind == "else" and stack and stack[-1] is not None:
                stack[-1] = not stack[-1]
            elif kind == "endif" and stack:
                stack.pop()
            continue
        if all(s is None or s for s in stack):
            out.append(raw)
    return "\n".join(out)


# The two images that listen, and the one that does not. SHELL_BOOTCMD here is
# the value the Makefile writes into shell_bootcmd.h: 1 only for a play image
# without SHELL_REMOTE, so the two are never 1 together.
REMOTE_IMAGE = {"SHELL_REMOTE": 1, "SHELL_BOOTCMD": 0}
BOOTCMD_IMAGE = {"SHELL_REMOTE": 0, "SHELL_BOOTCMD": 1}
NEITHER_IMAGE = {"SHELL_REMOTE": 0, "SHELL_BOOTCMD": 0}
LISTENING = (REMOTE_IMAGE, BOOTCMD_IMAGE)


def eval_cond(cond, values):
    """True / False when every name in an #if / #elif condition is in
    `values` and it uses only !, && and ||; None (both branches) otherwise."""
    c = strip_comment(cond).strip()
    names = re.findall(r"[A-Za-z_]\w*", c)
    if not names or any(n not in values for n in names):
        return None
    if re.search(r"[^\w\s!&|()]", c) or re.search(r"!=", c):
        return None
    expr = c.replace("&&", " and ").replace("||", " or ").replace("!", " not ")
    expr = re.sub(r"[A-Za-z_]\w*", lambda m: m.group(0) if m.group(0) in ("and", "or", "not")
                  else str(int(values[m.group(0)])), expr)
    try:
        return bool(eval(expr, {"__builtins__": {}}))
    except Exception:
        return None


def compiled(text, values):
    """The lines of `text` that compile with the macros in `values` set, as far
    as conditions over those macros alone decide; every other conditional
    keeps both branches. Directive lines (#if ... #endif) are dropped, so the
    result is code and #include / #define lines only."""
    out = []
    stack = []   # [state of this branch, taken by an earlier branch]: True / False / None
    for raw in text.splitlines():
        m = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", raw)
        if m:
            kind, rest = m.group(1), m.group(2)
            if kind == "if":
                s = eval_cond(rest, values)
                stack.append([s, s])
            elif kind in ("ifdef", "ifndef"):
                stack.append([None, None])
            elif kind == "elif" and stack:
                taken = stack[-1][1]
                if taken is True:
                    s = False
                else:
                    s = eval_cond(rest, values)
                    if taken is None and s is False:
                        s = False
                    elif taken is None:
                        s = None
                stack[-1] = [s, True if (taken is True or s is True) else
                             (None if (taken is None or s is None) else False)]
            elif kind == "else" and stack:
                taken = stack[-1][1]
                stack[-1] = [None if taken is None else not taken, True]
            elif kind == "endif" and stack:
                stack.pop()
            continue
        if all(s is not False for s, _ in stack):
            out.append(raw)
    return "\n".join(out)


def brace_depth_at(text, line_index):
    """The brace depth (comments stripped) in front of line `line_index`."""
    depth = 0
    for i, s in code_lines(text):
        if i >= line_index:
            break
        depth += s.count("{") - s.count("}")
    return depth


def code_lines(text):
    """(index, stripped code) of every line that is not blank, a comment or a
    preprocessor directive. Block comments are blanked line by line first."""
    lines = text.splitlines()
    cleaned = []
    in_block = False
    for raw in lines:
        s = raw
        out = ""
        while s:
            if in_block:
                end = s.find("*/")
                if end < 0:
                    s = ""
                else:
                    s = s[end + 2:]
                    in_block = False
            else:
                start = s.find("/*")
                cut = s.find("//")
                if cut >= 0 and (start < 0 or cut < start):
                    out += s[:cut]
                    s = ""
                elif start >= 0:
                    out += s[:start]
                    s = s[start + 2:]
                    in_block = True
                else:
                    out += s
                    s = ""
        cleaned.append(out)
    res = []
    for i, c in enumerate(cleaned):
        c = c.strip()
        if not c or c.startswith("#"):
            continue
        res.append((i, c))
    return res


def statements_after(text, opener):
    """The code lines that follow the first line containing `opener`."""
    lines = code_lines(text)
    for k, (_, s) in enumerate(lines):
        if opener in s:
            return lines[k + 1:]
    return []


# --- the checks -------------------------------------------------------------
def on_rx_enters_bootloader(listen):
    body = code_only(function_body(listen, "void on_rx("))
    return "remote_enter_bootloader()" in body and "CmdKind::Boot" in body


def on_rx_never_delays(listen):
    body = code_only(function_body(listen, "void on_rx("))
    flat = body.replace(" ", "").replace("\t", "")
    return bool(body) and "Delay" not in body and "Print" not in body \
        and "while(" not in flat and "for(;;)" not in flat \
        and re.search(r"\bdo\s*\{", body) is None


def on_rx_holds_a_packet(listen):
    """The assembler's output array is kMaxCmdsPerPacket long, and that is 16:
    the shortest valid line is three characters plus '\\n', and a 64-byte FS
    packet can also finish one line begun in the previous packet, so
    1 + 63 / 4 = 16 lines complete in one packet."""
    code = code_only(listen)
    m = re.search(r"constexpr\s+int\s+kMaxCmdsPerPacket\s*=\s*(\d+)\s*;", code)
    body = code_only(function_body(listen, "void on_rx("))
    return bool(m) and int(m.group(1)) == 16 and \
        re.search(r"Command\s+\w+\s*\[\s*kMaxCmdsPerPacket\s*\]", body) is not None and \
        re.search(r"feed\([^;]*kMaxCmdsPerPacket\s*\)", body) is not None


def entry_is_infinite_and_quiet(listen):
    """The DAISY_INFINITE_TIMEOUT path of System::ResetToBootloader(), written
    out: that function's STM branch holds a HAL_Delay(10) and calls the
    QSPI-resident GPIO::Init, so it is not called at all."""
    body = code_only(function_body(listen, "void remote_enter_bootloader("))
    return re.search(r"boot_info\.status\s*=\s*daisy::System::BootInfo::Type::INF_TIMEOUT\s*;",
                     body) is not None and "NVIC_SystemReset();" in body \
        and "ResetToBootloader(" not in body and "Delay" not in body


def entry_cleans_dcache(listen):
    """The request written to boot_info reaches memory before the reset: the
    D-cache is cleaned between the status write and NVIC_SystemReset() (a
    reset does not write a dirty line back). Inline CMSIS, so no QSPI and no
    SysTick."""
    body = flat(code_only(function_body(listen, "void remote_enter_bootloader(")))
    w = body.find("daisy::boot_info.status=daisy::System::BootInfo::Type::INF_TIMEOUT;")
    c = max(body.find("SCB_CleanDCache();"), body.find("SCB_CleanDCache_by_Addr("))
    r = body.find("NVIC_SystemReset();")
    return 0 <= w < c < r


def fault_vectors_installed(listen):
    body = code_only(function_body(listen, "void remote_install_fault_vectors("))
    code = code_only(listen)
    names = ("NonMaskableInt_IRQn", "HardFault_IRQn", "MemoryManagement_IRQn", "BusFault_IRQn",
             "UsageFault_IRQn")
    # The alignment on the table's own declaration, not anywhere in the file.
    decl = re.search(r"(alignas\s*\(\s*1024\s*\)|aligned\s*\(\s*1024\s*\))[^;{}]*"
                     r"\bg_ram_vectors\s*\[", code)
    return all(n in body for n in names) and \
        re.search(r"SCB->VTOR\s*=\s*reinterpret_cast<uint32_t>\(\s*g_ram_vectors\s*\)", body) \
        is not None and decl is not None


def fault_chain_whole(listen):
    """vectors -> shell_remote_fault_entry (naked: reset MSP, then `b` to the
    C half) -> shell_remote_fault_reset() -> remote_enter_bootloader()."""
    vec = code_only(function_body(listen, "void remote_install_fault_vectors("))
    stub = code_only(function_body(listen, "void shell_remote_fault_entry("))
    reset = code_only(function_body(listen, "void shell_remote_fault_reset("))
    return "&shell_remote_fault_entry" in vec \
        and re.search(r'"\s*b\s+shell_remote_fault_reset\s*\\n\s*"', stub) is not None \
        and re.search(r"\bshell::remote_enter_bootloader\(\);", reset) is not None


# --- Task 5: the workloads ----------------------------------------------------
def guard_silences_callback(main, scan):
    """AudioCallback: `if(shell::panel_scan_guard_tripped())` under
    SHELL_REMOTE, its silence, then `else` under SHELL_REMOTE right ahead of
    the next inst.process(); panel_scan_block_end() feeds the guard."""
    cb = function_body(main, "static void AudioCallback(")
    lines, remote = code_lines(cb), remote_lines(cb)
    k = next((n for n, (_, s) in enumerate(lines)
              if "if(shell::panel_scan_guard_tripped())" in s.replace(" ", "")), None)
    if k is None or lines[k][0] not in remote:
        return False
    prev = None
    fed = False
    for idx, s in lines[k + 1:]:
        if "inst.process(" in s:
            fed = prev is not None and prev[1] == "else" and prev[0] in remote
            break
        prev = (idx, s)
    end = function_body(scan, "void panel_scan_block_end(")
    end_remote = remote_lines(end)
    feeds = any(re.search(r"\bg_guard\.block\(\s*dc\b", s) and i in end_remote
                for i, s in code_lines(end))
    return fed and feeds


def poll_once_per_block(scan):
    tick = function_body(scan, "void panel_scan_tick(")
    calls = [(i, s) for i, s in code_lines(tick) if re.search(r"\bremote_block\s*\(", s)]
    if len(calls) != 1 or calls[0][1] != "remote_block();":
        return False
    i = calls[0][0]
    # Under SHELL_REMOTE, at the function's own level (depth 1: its brace).
    if i not in remote_lines(tick) or brace_depth_at(tick, i) != 1:
        return False
    # remote_block() is called from nowhere else.
    if len(re.findall(r"\bremote_block\s*\(\s*\)\s*;", code_only(scan))) != 1:
        return False
    rb = function_body(scan, "void remote_block(")
    if not rb:
        return False
    for v in (0, 1):
        body = code_only(select_branch(rb, "SHELL_PERSIST_ON", v))
        if len(re.findall(r"\bremote_poll\s*\(", body)) != 1:
            return False
        if re.search(r"\b(while|for)\s*\([^;{]*remote_poll", body):
            return False
    # And no remote_poll() outside remote_block().
    return len(re.findall(r"\bremote_poll\s*\(", code_only(scan))) == \
        len(re.findall(r"\bremote_poll\s*\(", code_only(rb)))


def saves_only_without_remote(sources):
    """Every persist_write() call in the shell sources compiles only with
    SHELL_REMOTE == 0, and panel_scan.cpp has one (the check is not vacuous)."""
    hits = 0
    for name, text in sources.items():
        no_remote = remote_lines(text, under_no_remote)
        for i, s in code_lines(text):
            if re.search(r"\bpersist_write\s*\(", s):
                if i not in no_remote:
                    return False
                if name == "panel_scan.cpp":
                    hits += 1
    return hits >= 1


# --- Task 6: the measurement switch -------------------------------------------
TOGGLES_CALL = re.compile(r"(?<![\w])toggles\s*\(\s*\)")


def toggles_latched_once(inst):
    """probe::toggles() exactly once in instrument.cpp, inside
    Instrument::process, ahead of its per-sample loop."""
    code = code_only(inst)
    if len(TOGGLES_CALL.findall(code)) != 1:
        return False
    body = code_only(function_body(inst, "void Instrument::process("))
    call = re.search(r"\bprobe::toggles\s*\(\s*\)", body)
    loop = re.search(r"\bfor\s*\(\s*size_t\s+i\s*=\s*0\s*;\s*i\s*<\s*n\s*;", body)
    return call is not None and loop is not None and call.start() < loop.start()


def toggles_nowhere_else(engine):
    """No engine file but instrument.cpp (and the header that defines it)
    calls toggles()."""
    for path, text in engine.items():
        p = path.replace("\\", "/")
        if p.endswith("engine/instrument.cpp") or p.endswith("engine/util/probe_toggles.h"):
            continue
        if TOGGLES_CALL.search(code_only(text)):
            return False
    return len(engine) > 0


# --- SHELL_RGRAIN: the reverb inject hook (spec 2026-10-31-sd-use-cases-tests,
# B1 and B2) --------------------------------------------------------------------
INJECT_MEMBER = re.compile(r"\b_inj_[lr]\b")
INJECT_ANY = re.compile(r"\b(set_reverb_inject|_inj_[lr]|inj_[lr])\b")
SAMPLE_LOOP = re.compile(r"\bfor\s*\(\s*size_t\s+i\s*=\s*0\s*;\s*i\s*<\s*n\s*;")


def inject_read_before_loop(inst):
    """set_reverb_inject()'s two pointers (the members _inj_l and _inj_r) are
    read in instrument.cpp once each, inside Instrument::process, ahead of its
    per-sample loop -- never per sample (the same rule as the toggle word)."""
    if sorted(INJECT_MEMBER.findall(code_only(inst))) != ["_inj_l", "_inj_r"]:
        return False
    body = code_only(function_body(inst, "void Instrument::process("))
    loop = SAMPLE_LOOP.search(body)
    reads = [m.start() for m in INJECT_MEMBER.finditer(body)]
    return loop is not None and len(reads) == 2 and all(p < loop.start() for p in reads)


def under_inject(cond, in_else):
    """Whether the branch compiles only with SPKY_PROBE_REVERB_INJECT defined."""
    return not in_else and cond.strip() in ("defined(SPKY_PROBE_REVERB_INJECT)",
                                            "<ifdef SPKY_PROBE_REVERB_INJECT>")


def inject_only_under_define(engine):
    """Every engine line that names the hook (set_reverb_inject, _inj_l/_r,
    inj_l/_r) compiles only under SPKY_PROBE_REVERB_INJECT, so without the
    define the hook does not exist; and both instrument.h and instrument.cpp
    carry it (the canary that the scan reads the right files)."""
    seen = set()
    for path, text in engine.items():
        guarded = remote_lines(text, under_inject)
        for i, s in code_lines(text):
            if INJECT_ANY.search(s):
                if i not in guarded:
                    return False
                seen.add(path.replace("\\", "/"))
    return {"engine/instrument.h", "engine/instrument.cpp"} <= seen


def make_inject_rgrain_only(make):
    """-DSPKY_PROBE_REVERB_INJECT is added once, under ifneq ($(SHELL_RGRAIN),0)
    alone, and the switch is resolved at parse time by its writer."""
    stacks = make_stack_lines(make, "-DSPKY_PROBE_REVERB_INJECT")
    writer = re.search(r"\$\(shell python write_shell_rgrain\.py "
                       r"\$\(BUILD_DIR\)/shell_rgrain\.h \$\(SHELL_RGRAIN\) "
                       r"\$\(BUILD_DIR\)\)", make)
    return stacks == [["ifneq ($(SHELL_RGRAIN),0)"]] and writer is not None


def make_stack_lines(make, needle):
    """The make conditional stacks of the lines containing `needle`."""
    stack, out = [], []
    for raw in make.splitlines():
        line = raw.split("#", 1)[0].rstrip()
        s = line.strip()
        if re.match(r"(ifeq|ifneq|ifdef|ifndef)\b", s):
            stack.append(s)
            continue
        if s == "endif" and stack:
            stack.pop()
            continue
        if s.startswith("else") and stack:
            stack[-1] = "else of " + stack[-1]
            continue
        if needle in line:
            out.append(list(stack))
    return out


def make_measure_switch(make):
    writer = re.search(r"\$\(shell python write_shell_remote_measure\.py "
                       r"\$\(BUILD_DIR\)/shell_remote_measure\.h \$\(SHELL_REMOTE_MEASURE\) "
                       r"\$\(BUILD_DIR\)\)", make)
    refuse = re.search(r"^ifeq \(\$\(SHELL_REMOTE_MEASURE\),1\)\s*\n"
                       r"ifneq \(\$\(SHELL_REMOTE\),1\)\s*\n"
                       r"\$\(error SHELL_REMOTE_MEASURE=1 needs SHELL_REMOTE=1", make, flags=re.M)
    return writer is not None and refuse is not None \
        and re.search(r"^SHELL_REMOTE_MEASURE \?= 0$", make, flags=re.M) is not None


def make_probe_defines_measure_only(make):
    stacks = make_stack_lines(make, "-DSPKY_PROBE_TOGGLES=1")
    marks = make_stack_lines(make, "-DSPKY_PROBE_MARKS=1")
    return len(stacks) == 1 and stacks == marks \
        and stacks[0] == ["ifeq ($(SHELL_REMOTE_MEASURE),1)"]


def late_before_process(main):
    cb = function_body(main, "static void AudioCallback(")
    lines = code_lines(cb)
    measure = remote_lines(cb, under_measure)
    late = [(n, i) for n, (i, s) in enumerate(lines) if s == "shell::panel_scan_late();"]
    proc = [n for n, (_, s) in enumerate(lines) if "inst.process(" in s]
    tick = [n for n, (_, s) in enumerate(lines)
            if s.replace(" ", "") == "shell::panel_scan_tick(hw,inst);"]
    if len(late) != 1 or late[0][1] not in measure or len(tick) != 1:
        return False
    # After the tick (the LATE command arrives there), before the process()
    # that follows it (the CPU probe's own process() call comes earlier).
    after = [n for n in proc if n > tick[0]]
    return bool(after) and tick[0] < late[0][0] < min(after)


def measure_writer_deletes_objects(script_text):
    """Runs the writer (from `script_text`) in a scratch build dir: a first
    write and a flip delete every object, a rewrite of the same value none."""
    tmp = tempfile.mkdtemp()
    try:
        script = os.path.join(tmp, "w.py")
        with open(script, "w", encoding="utf-8") as f:
            f.write(script_text)
        bd = os.path.join(tmp, "build")
        header = os.path.join(bd, "shell_remote_measure.h")

        def run(v):
            r = subprocess.run([sys.executable, script, header, v, bd],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            return r.returncode == 0

        def touch(name):
            os.makedirs(bd, exist_ok=True)
            open(os.path.join(bd, name), "w").close()

        def has(name):
            return os.path.exists(os.path.join(bd, name))

        touch("main.o"); touch("bbd.o")
        if not run("0") or has("main.o") or has("bbd.o"):
            return False   # the first write is a change too
        touch("main.o"); touch("lane.o"); touch("keep.d")
        if not run("0") or not has("main.o") or not has("lane.o"):
            return False   # unchanged: nothing deleted
        if not run("1") or has("main.o") or has("lane.o") or not has("keep.d"):
            return False   # the flip: every object, nothing else
        with open(header, encoding="utf-8") as f:
            return f.read() == "#define SHELL_REMOTE_MEASURE 1\n"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def fault_vectors_first_in_main(main):
    """In both listening images remote_install_fault_vectors() is main()'s first
    statement; in the image with neither it is not called at all."""
    for image in LISTENING:
        tail = statements_after(compiled(main, image), "int main(void)")
        # The first line after the signature is the opening brace.
        if tail and tail[0][1] == "{":
            tail = tail[1:]
        if not tail or "shell::remote_install_fault_vectors();" not in tail[0][1]:
            return False
    return "remote_install_fault_vectors(" not in code_only(compiled(main, NEITHER_IMAGE))


def itcm_branch_enters_bootloader(text):
    lines = code_lines(text)
    for k, (_, s) in enumerate(lines):
        if "if(!shell::itcm_load())" in s.replace(" ", ""):
            # The branch: from the brace after the if to its matching close.
            depth = 0
            for idx, c in lines[k + 1:]:
                depth += c.count("{") - c.count("}")
                if "shell::remote_enter_bootloader();" in c:
                    return True
                if depth <= 0 and "}" in c:
                    return False
            return False
    return None   # no such branch at all


def itcm_failure_enters_bootloader(main):
    """A failed itcm_load() enters the bootloader in both listening images, and
    only blinks in the image with neither."""
    return all(itcm_branch_enters_bootloader(compiled(main, image)) is True
               for image in LISTENING) \
        and itcm_branch_enters_bootloader(compiled(main, NEITHER_IMAGE)) is False


def listen_follows_startlog(scan):
    """In both listening images remote_listen_start() is the statement right
    after StartLog(); in the image with neither it is not called."""
    for image in LISTENING:
        body = function_body(compiled(scan, image), "void run_panel_scan_report(")
        tail = statements_after(body, "hw.StartLog(false);")
        if not tail or tail[0][1] != "remote_listen_start();":
            return False
    body = function_body(compiled(scan, NEITHER_IMAGE), "void run_panel_scan_report(")
    return bool(body) and "remote_listen_start(" not in code_only(body)


def includes_switch(text, header="shell_remote.h"):
    return any(l.strip() == '#include "%s"' % header for l in text.splitlines())


def make_parse_time(make):
    m = re.search(r"\$\(shell python write_shell_remote\.py \$\(BUILD_DIR\)/shell_remote\.h "
                  r"\$\(SHELL_REMOTE\)(.*)$", make, flags=re.M)
    if not m:
        return False
    objs = m.group(1)
    return "$(BUILD_DIR)/main.o" in objs and "$(BUILD_DIR)/panel_scan.o" in objs \
        and re.search(r"^SHELL_REMOTE \?= 0$", make, flags=re.M) is not None


def make_sources_only_remote(make):
    stack = []
    hits = 0
    for raw in make.splitlines():
        line = raw.split("#", 1)[0].rstrip()
        s = line.strip()
        if re.match(r"(ifeq|ifneq|ifdef|ifndef)\b", s):
            stack.append(s)
            continue
        if s == "endif" and stack:
            stack.pop()
            continue
        if s.startswith("else") and stack:
            stack[-1] = "else of " + stack[-1]
            continue
        if "remote_listen.cpp" in line or "remote_cmd.cpp" in line:
            hits += 1
            if "ifeq ($(SHELL_REMOTE),1)" not in stack and \
                    "ifeq ($(SHELL_BOOTCMD_ON),1)" not in stack:
                return False
        # The workloads and the overrun guard: the listener image alone.
        if "workload_run.cpp" in line and "ifeq ($(SHELL_REMOTE),1)" not in stack:
            return False
    return hits >= 1 and "remote_listen.cpp" in make and "remote_cmd.cpp" in make


def make_bootcmd_sources(make):
    """The boot-only image builds remote_cmd.cpp and remote_listen.cpp, and
    nothing else of the listener: one CPP_SOURCES line under the effective
    switch."""
    hits = [s for s in make_stack_lines(make, "CPP_SOURCES")
            if "ifeq ($(SHELL_BOOTCMD_ON),1)" in s]
    lines = [l.split("#", 1)[0].strip() for l in make.splitlines()]
    k = next((n for n, l in enumerate(lines) if l == "ifeq ($(SHELL_BOOTCMD_ON),1)"
              and n + 1 < len(lines) and lines[n + 1].startswith("CPP_SOURCES")), None)
    return len(hits) == 1 and k is not None \
        and lines[k + 1] == "CPP_SOURCES += remote_cmd.cpp remote_listen.cpp"


def make_bootcmd_switch(make):
    """SHELL_BOOTCMD defaults to 1, is validated like the other switches, is
    effective (SHELL_BOOTCMD_ON) only in a play image without SHELL_REMOTE, and
    that effective value is written at parse time, deleting main.o and
    panel_scan.o when it changes."""
    default = re.search(r"^SHELL_BOOTCMD \?= 1$", make, flags=re.M)
    valid = re.search(r"^ifneq \(\$\(words \$\(SHELL_BOOTCMD\)\)-\$\(filter \$\(SHELL_BOOTCMD\),0 1\),"
                      r"1-\$\(SHELL_BOOTCMD\)\)\s*\n\$\(error SHELL_BOOTCMD must be 0 or 1\)",
                      make, flags=re.M)
    eff = re.search(r"^ifeq \(\$\(SHELL_BOOTCMD\)\$\(SHELL_PANEL_SCAN\)\$\(SHELL_REMOTE\),110\)\s*\n"
                    r"SHELL_BOOTCMD_ON := 1\s*\nelse\s*\nSHELL_BOOTCMD_ON := 0\s*\nendif",
                    make, flags=re.M)
    m = re.search(r"\$\(shell python write_shell_bootcmd\.py \$\(BUILD_DIR\)/shell_bootcmd\.h "
                  r"\$\(SHELL_BOOTCMD_ON\)(.*)$", make, flags=re.M)
    objs = m.group(1) if m else ""
    edge = re.search(r"^\$\(BUILD_DIR\)/main\.o \$\(BUILD_DIR\)/panel_scan\.o: "
                     r"\$\(BUILD_DIR\)/shell_bootcmd\.h$", make, flags=re.M)
    return bool(default and valid and eff and m and edge) \
        and "$(BUILD_DIR)/main.o" in objs and "$(BUILD_DIR)/panel_scan.o" in objs


def bootcmd_announces_and_pongs(scan, remote_lib=None):
    """The boot-only image's report loop: `SHELL_REMOTE on build=%s cmds=boot`
    with remote_build() on the first pass and every 10th, and the pong; and
    the line still matches the host's confirm_build() regex."""
    body = function_body(compiled(scan, BOOTCMD_IMAGE), "void run_panel_scan_report(")
    f = flat(code_only(body))
    fmt = '"SHELL_REMOTE on build=%s cmds=boot"'
    announce = ('if(bootcmd_pass++%kBootcmdEvery==0)hw.PrintLine(' + flat(fmt) +
                ',remote_build());')
    if remote_lib is None:
        with open(os.path.join(REPO, "tools", "remote_lib.py"), encoding="utf-8") as fh:
            remote_lib = fh.read()
    host = re.search(r're\.search\(r"(SHELL_REMOTE on build=[^"]*)"', remote_lib)
    sample = fmt.strip('"') % "abc1234"
    hm = re.search(host.group(1), sample) if host else None
    return announce in f and "constexpruint32_tkBootcmdEvery=10;" in f \
        and "uint32_tbootcmd_pass=0;" in f \
        and 'if(remote_take_ping())hw.PrintLine("SHELL_REMOTEpong");' in f \
        and hm is not None and hm.group(1) == "abc1234"


def bootcmd_keeps_saves(scan):
    """The boot-only image saves the panel state as the play image does:
    persist_write() compiles there, and the suppression does not."""
    code = code_only(compiled(scan, BOOTCMD_IMAGE))
    return re.search(r"\bpersist_write\s*\(", code) is not None \
        and "save suppressed" not in code


def drop_function(text, signature):
    body = function_body(text, signature)
    return text.replace(body, "{}", 1) if body else text


def bootcmd_callback_unchanged(main, scan):
    """AudioCallback, and panel_scan.cpp outside run_panel_scan_report(),
    compile in the boot-only image exactly as in the image with neither (no
    guard, no command poll, no silence): the boot-only listener lives in the
    USB interrupt and the report loop alone."""
    def code(text):
        """Code lines only: comments, blank lines and #include lines dropped."""
        return "\n".join(l.rstrip() for l in code_only(text).splitlines()
                         if l.strip() and not l.strip().startswith("#include"))

    def cb(image):
        return code(function_body(compiled(main, image), "static void AudioCallback("))

    def rest(image):
        return code(drop_function(compiled(scan, image), "void run_panel_scan_report("))
    return bool(cb(NEITHER_IMAGE)) and cb(BOOTCMD_IMAGE) == cb(NEITHER_IMAGE) \
        and rest(BOOTCMD_IMAGE) == rest(NEITHER_IMAGE) \
        and "panel_scan_tick(" in rest(NEITHER_IMAGE)


# --- Task 6 fix round 1 -------------------------------------------------------
def flat(s):
    return re.sub(r"\s+", "", s)


def measure_toggle_case(scan):
    """The measurement branch's `case CmdKind::Toggle:` of remote_command(),
    up to the LATE case, comments stripped; "" when it is not under the switch."""
    body = function_body(scan, "void remote_command(")
    lines = body.splitlines()
    measure = remote_lines(body, under_measure)
    start = next((n for n, l in enumerate(lines)
                  if "case CmdKind::Toggle:" in strip_comment(l)), None)
    if start is None or start not in measure:
        return ""
    end = next((n for n in range(start + 1, len(lines))
                if "case CmdKind::Late:" in strip_comment(lines[n])), len(lines))
    return code_only("\n".join(lines[start:end]))


def tail_stamped_last(scan):
    """panel_scan_block_end() ends with the tail stamp and the block count,
    both under SHELL_REMOTE_MEASURE, after cb_profile_block_end(); the block
    count is bumped nowhere else."""
    body = function_body(scan, "void panel_scan_block_end(")
    measure = remote_lines(body, under_measure)
    stmts = [(i, flat(s)) for i, s in code_lines(body) if s not in ("{", "}")]
    if len(stmts) < 3:
        return False
    (ti, tail), (bi, blk) = stmts[-2], stmts[-1]
    prof = [i for i, s in stmts if "cb_profile_block_end(" in s]
    return tail == "g_tail_last=cycles_now()-c0-dc;" and blk == "g_blk_done=g_blk_done+1u;" \
        and ti in measure and bi in measure and len(prof) == 1 and prof[0] < ti \
        and flat(code_only(scan)).count("g_blk_done=g_blk_done+1u;") == 1


def busy_window_splits_tail(scan):
    """busy_window() reads the tail beside the cb, splits each block with
    out_split(), counts the blocks where a subtraction saturated, and SHELL_OUT
    prints tail_max, tail_p50, ext_max and that count (neg=)."""
    bw = [flat(s) for _, s in code_lines(function_body(scan, "OutStats busy_window("))]
    k = next((n for n, s in enumerate(bw) if s == "cb=g_cb_cycles_last;"), None)
    if k is None or k + 1 >= len(bw) or bw[k + 1] != "tail=g_tail_last;":
        return False
    if "constOutSplits=out_split(acc,cb,tail);" not in bw:
        return False
    s_at = bw.index("constOutSplits=out_split(acc,cb,tail);")
    if "if(s.neg)++o.neg;" not in bw[s_at + 1:]:
        return False
    code = flat(code_only(scan))
    fmt = ('"SHELL_OUTbusy_max=%ubusy_p50=%uout_max=%utail_max=%u""tail_p50=%uext_max=%uneg=%d"')
    i = code.find(fmt)
    args = code[i:i + 600] if i >= 0 else ""
    return i >= 0 and all(("out_stats.%s)" % f) in args
                          for f in ("busy_max", "busy_p50", "out_max", "tail_max",
                                    "tail_p50", "ext_max", "neg"))


def late_stops_when_tripped(scan):
    """panel_scan_late() returns before its burn while the guard holds the
    callback silent (final review C1): otherwise bit 30's repeated LATE keeps
    the callback back to back after the trip and USB never runs."""
    body = [flat(s) for _, s in code_lines(function_body(scan, "void panel_scan_late("))]
    stop = next((n for n, s in enumerate(body) if s == "if(panel_scan_guard_tripped())return;"),
                None)
    burn = next((n for n, s in enumerate(body) if s.startswith("while(cycles_now()-t0<n)")), None)
    return stop is not None and burn is not None and stop < burn


def guard_fed_period(scan):
    """panel_scan_block_end() feeds the guard the entry-to-entry period (final
    review I2): computed from g_win_prev_entry under SHELL_REMOTE before the WIN
    window overwrites it, passed beside dc; the guard is built with
    kGuardPeriodCycles, and the trip line says which criterion tripped."""
    end = function_body(scan, "void panel_scan_block_end(")
    lines = [(i, flat(s)) for i, s in code_lines(end)]
    remote = remote_lines(end)
    stmt = "constuint32_tperiod=g_win_prev_entry!=0u?c0-g_win_prev_entry:0u;"
    p = next((n for n, (_, s) in enumerate(lines) if s == stmt), None)
    f = next((n for n, (_, s) in enumerate(lines) if "g_guard.block(dc,period)" in s), None)
    w = next((n for n, (_, s) in enumerate(lines) if s == "g_win_prev_entry=c0;"), None)
    if p is None or f is None or w is None or not (p < f < w) or lines[p][0] not in remote:
        return False
    code = flat(code_only(scan))
    trip = code.find('"SHELL_REMOTEguardtrippedblk=%uby=%s"')
    return "OverrunGuardg_guard(kGuardLimitCycles,kGuardBlocks,kGuardPeriodCycles);" in code \
        and trip >= 0 and "g_guard_by" in code[trip:trip + 200] \
        and "g_guard_by=g_guard.cause();" in flat(code_only(end))


def tog_bit31_restarts_streak(scan):
    case = flat(measure_toggle_case(scan))
    return "if(((old^word)>>spky::probe::kBitShellGuardOff)&1u)g_guard.reset_streak();" in case \
        and "constuint32_told=spky::probe::toggles();" in case


def tog_resettles_run(scan):
    """A TOG arms the settle by block number and re-settles the run for as
    long; remote_block() ends the settle by block number; 200 blocks."""
    case = flat(measure_toggle_case(scan))
    rb = flat(code_only(function_body(scan, "void remote_block(")))
    code = flat(code_only(scan))
    return "g_tog_settle.arm(g_rblock);" in case \
        and "g_run.resettle(g_rblock,kTogSettleBlocks);" in case \
        and "if(g_tog_settle.ends(now))" in rb \
        and "constexpruint32_tkTogSettleBlocks=200;" in code \
        and "TogSettleg_tog_settle(kTogSettleBlocks);" in code


def tog_and_gen_printed(scan):
    """The run line carries the snapshot's toggle word, published per block;
    the settled ack carries the run's gen and its line prints it."""
    code = flat(code_only(scan))
    run_fmt = ('"SHELL_REMOTErunid=%dgen=%ucounting=%dvoid=%dtripped=%dtrips=%ugest=%d"'
               '"tog=%08x"')
    i = code.find(run_fmt)
    run_args = code[i:i + 400] if i >= 0 else ""
    pub = flat(code_only(function_body(scan, "void publish_run(")))
    rd = flat(code_only(function_body(scan, "RunSnap read_run(")))
    rb = flat(code_only(function_body(scan, "void remote_block(")))
    settled = '"SHELL_REMOTEtogsettledword=%08xgen=%urst"'
    j = code.find(settled)
    settled_args = code[j:j + 200] if j >= 0 else ""
    return i >= 0 and "static_cast<unsigned>(after.tog));" in run_args \
        and "g_rs_tog=spky::probe::toggles();" in pub and "s.tog=g_rs_tog;" in rd \
        and "kAckTogSettledBase+static_cast<int32_t>(g_run.gen()&kGenPrintMask)" in rb \
        and j >= 0 and "a.b-kAckTogSettledBase" in settled_args


# --- the guard re-arms itself (Bastian, 2026-10-29) ----------------------------
def guard_self_rearms(wl_h, wl_cpp):
    """OverrunGuard counts the silent blocks it is fed while tripped and clears
    itself on the 500th (kGuardRearmBlocks, the constructor's default); clear()
    and reset_streak() restart that count."""
    h = flat(code_only(wl_h))
    body = flat(code_only(function_body(wl_cpp, "bool OverrunGuard::block(")))
    clear = flat(code_only(function_body(wl_cpp, "void OverrunGuard::clear(")))
    reset = flat(code_only(function_body(wl_cpp, "void OverrunGuard::reset_streak(")))
    return "inlineconstexpruint32_tkGuardRearmBlocks=500;" in h \
        and "uint32_trearm_blocks=kGuardRearmBlocks" in h \
        and body.startswith("{if(tripped_){if(++silent_>=rearm_)clear();returntripped_;}") \
        and "silent_=0;" in clear and "silent_=0;" in reset


def guard_rearms_at_settle_end(scan):
    """remote_block(): inside `if(g_run.settle_ends(now))`, ahead of the
    gesture steps, rearm_at_settle_end() clears the guard exactly when counting
    starts, drops the flag and says so (note_guard_rearm(true))."""
    rb = flat(code_only(function_body(scan, "void remote_block(")))
    opener = "if(g_run.settle_ends(now)){"
    i = rb.find(opener)
    seg = ""
    if i >= 0:
        # The braces of that if: the segment ends at the one that closes it.
        depth = 0
        for k in range(i + len(opener) - 1, len(rb)):
            depth += (rb[k] == "{") - (rb[k] == "}")
            if depth == 0:
                seg = rb[i:k + 1]
                break
    return "if(rearm_at_settle_end(g_guard,g_run,now)){g_guard_flag=false;" \
        "note_guard_rearm(true);}" in seg


def guard_rearms_by_silence(scan):
    """panel_scan_block_end(), under SHELL_REMOTE: a guard that was tripped and
    is not any more after block() re-armed itself (note_guard_rearm(false)); a
    trip counts g_guard_trips; the trip's block and every silenced one count in
    g_guard_silent (the frames' tripped=)."""
    end = function_body(scan, "void panel_scan_block_end(")
    f = flat(code_only(end))
    lines = [(i, flat(s)) for i, s in code_lines(end)]
    remote = remote_lines(end)
    at = next((i for i, s in lines if s == "constboolnow_tripped=g_guard.block(dc,period);"), None)
    return at is not None and at in remote \
        and "elseif(was_tripped&&!now_tripped)note_guard_rearm(false);" in f \
        and "g_guard_trips=g_guard_trips+1u;" in f \
        and "if(now_tripped||was_tripped)g_guard_silent=g_guard_silent+1u;" in f


def guard_rearm_printed(scan):
    """The foreground prints the re-arm from its counter (once per re-arm, with
    the block and whether the settle end did it) and a trip from its counter
    too, so a trip a pass did not see is still said."""
    code = flat(code_only(scan))
    i = code.find('"SHELL_REMOTEguardrearmblk=%uby=%s"')
    args = code[i:i + 200] if i >= 0 else ""
    return i >= 0 and "g_guard_rearm_blk" in args and '"settle"' in args and '"silence"' in args \
        and "if(guard_rearms!=guard_rearms_said)" in code[max(0, i - 80):i] \
        and "guard_rearms_said=guard_rearms;" in code[i:i + 300] \
        and "if(g_guard_flag||guard_trips!=guard_trips_said)" in code \
        and "guard_trips_said=guard_trips;" in code \
        and "g_guard_rearms=g_guard_rearms+1u;" in code \
        and "g_guard_rearm_settle=by_settle;" in code


def run_line_counts_across_trips(scan):
    """The run line: counting no longer waits for an untripped guard, tripped=
    is the frame's (the difference of two g_guard_silent reads, taken beside
    the WIN window's), trips= the run's cumulative count, published per block."""
    code = flat(code_only(scan))
    pub = flat(code_only(function_body(scan, "void publish_run(")))
    rd = flat(code_only(function_body(scan, "RunSnap read_run(")))
    i = code.find('"SHELL_REMOTErunid=')
    args = code[i:i + 500] if i >= 0 else ""
    return "constboolcounted=remote_prev.counting&&remote_prev.gen==after.gen&&after.counting;" \
        in code \
        and "constbooltripped=guard_silent!=guard_silent_prev;" in code \
        and "constuint32_tguard_silent=g_guard_silent;" in code \
        and "guard_silent_prev=guard_silent;" in code \
        and "tripped?1:0" in args and "static_cast<unsigned>(after.trips)" in args \
        and "g_rs_trips=g_run.trips();" in pub and "s.trips=g_rs_trips;" in rd


FAULT_FIRST = ("remote_install_fault_vectors() is main()'s first statement in both listening "
               "images, absent otherwise")
ITCM_BOOT = "a failed itcm_load() enters the bootloader in both listening images, absent otherwise"
LISTEN_START = ("remote_listen_start() follows StartLog() in both listening images, absent "
                "otherwise")
SOURCES_ONLY = ("the listener's sources build only under SHELL_REMOTE=1 or SHELL_BOOTCMD_ON=1, "
                "the workloads only under SHELL_REMOTE=1")
BOOT_MAKE = "the Makefile resolves SHELL_BOOTCMD's effective value at parse time"
BOOT_SOURCES = "the boot-only image builds remote_cmd.cpp and remote_listen.cpp, nothing else"
BOOT_ANNOUNCE = "the boot-only image says `SHELL_REMOTE on build= cmds=boot` every 10th pass, pongs"
BOOT_SAVES = "the boot-only image keeps the persistence save"
BOOT_SAME_CB = ("the boot-only image compiles AudioCallback and panel_scan.cpp outside the report "
                "loop as the image with neither")


def run_checks(listen, main, scan, make, tick, inst, engine, writer, wl_h, wl_cpp):
    r = {}
    r["on_rx() enters the bootloader on BOOT itself"] = on_rx_enters_bootloader(listen)
    r["on_rx() never delays, prints or spins"] = on_rx_never_delays(listen)
    r["on_rx() holds a whole packet of commands (kMaxCmdsPerPacket = 16)"] = \
        on_rx_holds_a_packet(listen)
    r["remote_enter_bootloader() asks for an infinite timeout without a delay"] = \
        entry_is_infinite_and_quiet(listen)
    r["the RAM vector table routes NMI and all four faults and sets a 1 KB-aligned VTOR"] = \
        fault_vectors_installed(listen)
    r[FAULT_FIRST] = fault_vectors_first_in_main(main)
    r[ITCM_BOOT] = itcm_failure_enters_bootloader(main)
    r[LISTEN_START] = listen_follows_startlog(scan)
    r["main.cpp includes shell_remote.h"] = includes_switch(main)
    r["panel_scan.cpp includes shell_remote.h"] = includes_switch(scan)
    r["the Makefile resolves SHELL_REMOTE at parse time"] = make_parse_time(make)
    r[SOURCES_ONLY] = make_sources_only_remote(make)
    r["the fault chain reaches remote_enter_bootloader()"] = fault_chain_whole(listen)
    # The boot-only listener (SHELL_BOOTCMD).
    r["main.cpp includes shell_bootcmd.h"] = includes_switch(main, "shell_bootcmd.h")
    r["panel_scan.cpp includes shell_bootcmd.h"] = includes_switch(scan, "shell_bootcmd.h")
    r[BOOT_MAKE] = make_bootcmd_switch(make)
    r[BOOT_SOURCES] = make_bootcmd_sources(make)
    r[BOOT_ANNOUNCE] = bootcmd_announces_and_pongs(scan)
    r[BOOT_SAVES] = bootcmd_keeps_saves(scan)
    r[BOOT_SAME_CB] = bootcmd_callback_unchanged(main, scan)
    # Task 5.
    r["the tripped guard silences process() in AudioCallback, fed per callback"] = \
        guard_silences_callback(main, scan)
    r["remote_poll() runs once per block, from panel_scan_tick()'s top level"] = \
        poll_once_per_block(scan)
    r["persist_write() compiles only without SHELL_REMOTE"] = \
        saves_only_without_remote({"panel_scan.cpp": scan, "main.cpp": main,
                                   "control_tick.cpp": tick})
    # Task 6.
    r["probe::toggles() is latched once in Instrument::process, ahead of the sample loop"] = \
        toggles_latched_once(inst)
    r["no other engine file calls toggles()"] = toggles_nowhere_else(engine)
    r["the Makefile resolves SHELL_REMOTE_MEASURE at parse time and refuses it alone"] = \
        make_measure_switch(make)
    r["the probe defines are on in the measurement image alone"] = \
        make_probe_defines_measure_only(make)
    r["the measure writer deletes every object on a flip"] = \
        measure_writer_deletes_objects(writer)
    r["LATE burns in AudioCallback after the tick, before process(), under the switch"] = \
        late_before_process(main)
    r["main.cpp includes shell_remote_measure.h"] = includes_switch(main, "shell_remote_measure.h")
    r["panel_scan.cpp includes shell_remote_measure.h"] = \
        includes_switch(scan, "shell_remote_measure.h")
    # Task 6 fix round 1.
    r["the probe tail is stamped last in panel_scan_block_end(), under the switch"] = \
        tail_stamped_last(scan)
    r["busy_window() splits each block's tail out, SHELL_OUT prints tail and ext"] = \
        busy_window_splits_tail(scan)
    r["a TOG that flips bit 31 restarts the guard's streak"] = tog_bit31_restarts_streak(scan)
    r["a TOG re-settles the run and ends its settle by block number"] = tog_resettles_run(scan)
    r["the run line prints tog=, the settled line the run's gen"] = tog_and_gen_printed(scan)
    # The final review's fix wave.
    r["a tripped guard stops the LATE burn (C1)"] = late_stops_when_tripped(scan)
    r["the guard is fed the entry-to-entry period and says what tripped it (I2)"] = \
        guard_fed_period(scan)
    r["remote_enter_bootloader() cleans the D-cache before the reset (M1)"] = \
        entry_cleans_dcache(listen)
    # The guard re-arms itself.
    r["the guard re-arms itself after kGuardRearmBlocks (500) silent blocks"] = \
        guard_self_rearms(wl_h, wl_cpp)
    r["a run's settle end clears a tripped guard (rearm_at_settle_end)"] = \
        guard_rearms_at_settle_end(scan)
    r["panel_scan_block_end() notes the self re-arm, the trips and the silent blocks"] = \
        guard_rearms_by_silence(scan)
    r["the foreground prints `guard rearm blk= by=silence|settle` and every trip"] = \
        guard_rearm_printed(scan)
    r["the run line counts across trips: per-frame tripped=, cumulative trips="] = \
        run_line_counts_across_trips(scan)
    # SHELL_RGRAIN (SD use-case tests, B1 and B2).
    r["set_reverb_inject()'s pointers are read in Instrument::process, ahead of the sample loop"] = \
        inject_read_before_loop(inst)
    r["the reverb inject hook exists only under SPKY_PROBE_REVERB_INJECT"] = \
        inject_only_under_define(engine)
    r["the Makefile defines SPKY_PROBE_REVERB_INJECT in the SHELL_RGRAIN images alone"] = \
        make_inject_rgrain_only(make)
    return r


def read_engine():
    out = {}
    root = os.path.join(REPO, "engine")
    for d, _, files in os.walk(root):
        for f in files:
            if f.endswith((".h", ".hpp", ".cpp")):
                p = os.path.join(d, f)
                with open(p, encoding="utf-8", errors="replace") as fh:
                    out[os.path.relpath(p, REPO)] = fh.read()
    return out


LISTEN = read("remote_listen.cpp")
MAIN = read("main.cpp")
SCAN = read("panel_scan.cpp")
MAKE = read("Makefile")
TICK = read("control_tick.cpp")
WRITER = read("write_shell_remote_measure.py")
WL_H = read("workload_run.h")
WL_CPP = read("workload_run.cpp")
ENGINE = read_engine()
INST = ENGINE.get(os.path.join("engine", "instrument.cpp"), "")

SOURCES = dict(listen=LISTEN, main=MAIN, scan=SCAN, make=MAKE, tick=TICK, inst=INST,
               engine=ENGINE, writer=WRITER, wl_h=WL_H, wl_cpp=WL_CPP)
BASE = run_checks(**SOURCES)
for name, ok in BASE.items():
    check(name, ok)


# --- sabotages: each must turn its check red --------------------------------
def sabotage(label, target, **changed):
    src = dict(SOURCES)
    src.update(changed)
    r = run_checks(**src)
    check("sabotage %s goes red" % label, bool(BASE.get(target)) and not r[target])


def in_on_rx(text, new):
    """Inserts `new` as the first statement of on_rx()."""
    i = text.find("void on_rx(")
    j = text.find("{", i) if i >= 0 else -1
    if j < 0:
        return text
    return text[:j + 1] + "\n    " + new + text[j + 1:]


def in_body(text, signature, old, new):
    """Replaces `old` with `new` inside the body of the function `signature`."""
    body = function_body(text, signature)
    if not body:
        return text
    return text.replace(body, body.replace(old, new), 1)


sabotage("BOOT only queued",
         "on_rx() enters the bootloader on BOOT itself",
         listen=in_body(LISTEN, "void on_rx(", "remote_enter_bootloader();", "(void)0;"))
sabotage("hw.Delay(1) in on_rx()",
         "on_rx() never delays, prints or spins",
         listen=in_on_rx(LISTEN, "hw.Delay(1);"))
sabotage("a PrintLine in on_rx()",
         "on_rx() never delays, prints or spins",
         listen=in_on_rx(LISTEN, 'daisy::Logger<>::PrintLine("x");'))
sabotage("an 8-command array",
         "on_rx() holds a whole packet of commands (kMaxCmdsPerPacket = 16)",
         listen=re.sub(r"(constexpr\s+int\s+kMaxCmdsPerPacket\s*=\s*)16", r"\g<1>8", LISTEN))
sabotage("a literal array size",
         "on_rx() holds a whole packet of commands (kMaxCmdsPerPacket = 16)",
         listen=re.sub(r"\[\s*kMaxCmdsPerPacket\s*\]", "[4]", LISTEN))
sabotage("the default timeout",
         "remote_enter_bootloader() asks for an infinite timeout without a delay",
         listen=LISTEN.replace("Type::INF_TIMEOUT", "Type::SKIP_TIMEOUT"))
sabotage("libDaisy's ResetToBootloader() called",
         "remote_enter_bootloader() asks for an infinite timeout without a delay",
         listen=in_body(LISTEN, "void remote_enter_bootloader(", "NVIC_SystemReset();",
                        "daisy::System::ResetToBootloader("
                        "daisy::System::BootloaderMode::DAISY_INFINITE_TIMEOUT);\n"
                        "    NVIC_SystemReset();"))
sabotage("no reset",
         "remote_enter_bootloader() asks for an infinite timeout without a delay",
         listen=in_body(LISTEN, "void remote_enter_bootloader(", "NVIC_SystemReset();", "for(;;) {}"))
sabotage("a drain delay before the reset",
         "remote_enter_bootloader() asks for an infinite timeout without a delay",
         listen=LISTEN.replace("void remote_enter_bootloader()\n{",
                               "void remote_enter_bootloader()\n{\n    daisy::System::Delay(100);", 1))
sabotage("UsageFault left on the BKPT handler",
         "the RAM vector table routes NMI and all four faults and sets a 1 KB-aligned VTOR",
         listen=LISTEN.replace("UsageFault_IRQn", "SVCall_IRQn"))
sabotage("VTOR never set",
         "the RAM vector table routes NMI and all four faults and sets a 1 KB-aligned VTOR",
         listen=LISTEN.replace("SCB->VTOR", "g_unused_vtor"))
sabotage("a 256-byte alignment",
         "the RAM vector table routes NMI and all four faults and sets a 1 KB-aligned VTOR",
         listen=re.sub(r"(alignas|aligned)\s*\(\s*1024\s*\)", r"\1(256)", LISTEN))
LISTEN_IF = r"#if SHELL_REMOTE \|\| SHELL_BOOTCMD"
sabotage("board_init() before the vectors",
         FAULT_FIRST,
         main=MAIN.replace("int main(void)\n{", "int main(void)\n{\n    bench::board_init(hw);", 1))
sabotage("the vectors unguarded",
         FAULT_FIRST,
         main=re.sub(LISTEN_IF + r"((?:\s*//[^\n]*)*\s*shell::remote_install_fault_vectors\(\);)",
                     r"#if 1\1", MAIN, count=1))
sabotage("the vectors under SHELL_REMOTE alone",
         FAULT_FIRST,
         main=re.sub(LISTEN_IF + r"((?:\s*//[^\n]*)*\s*shell::remote_install_fault_vectors\(\);)",
                     r"#if SHELL_REMOTE\1", MAIN, count=1))
sabotage("the ITCM branch blinks only",
         ITCM_BOOT,
         main=MAIN.replace("shell::remote_enter_bootloader();", "hw.SetLed(true);"))
sabotage("the ITCM bootloader entry unguarded",
         ITCM_BOOT,
         main=re.sub(LISTEN_IF + r"(\s*\n\s*shell::remote_enter_bootloader\(\);)",
                     r"#if 1\1", MAIN))
sabotage("the ITCM bootloader entry under SHELL_REMOTE alone",
         ITCM_BOOT,
         main=re.sub(LISTEN_IF + r"(\s*\n\s*shell::remote_enter_bootloader\(\);)",
                     r"#if SHELL_REMOTE\1", MAIN))
sabotage("a statement between StartLog() and the listener",
         LISTEN_START,
         scan=SCAN.replace("    hw.StartLog(false);\n",
                           "    hw.StartLog(false);\n    hw.Delay(1);\n", 1))
sabotage("the listener never started",
         LISTEN_START,
         scan=SCAN.replace("remote_listen_start();", "(void)0;"))
BOOT_BLOCK = r"(#if SHELL_BOOTCMD\n(?:[ \t]*//[^\n]*\n)*[ \t]*)"
sabotage("the boot-only listener never started",
         LISTEN_START,
         scan=re.sub(BOOT_BLOCK + r"remote_listen_start\(\);", r"\1(void)0;", SCAN, count=1))
sabotage("the boot-only listener in every image",
         LISTEN_START,
         scan=re.sub(r"#if SHELL_BOOTCMD(\n(?:[ \t]*//[^\n]*\n)*[ \t]*remote_listen_start\(\);)",
                     r"#if 1\1", SCAN, count=1))
sabotage("main.cpp without the bootcmd header",
         "main.cpp includes shell_bootcmd.h",
         main=MAIN.replace('#include "shell_bootcmd.h"', ""))
sabotage("panel_scan.cpp without the bootcmd header",
         "panel_scan.cpp includes shell_bootcmd.h",
         scan=SCAN.replace('#include "shell_bootcmd.h"', ""))
sabotage("SHELL_BOOTCMD as a bare -D",
         BOOT_MAKE,
         make=re.sub(r"^.*write_shell_bootcmd\.py.*$", "C_DEFS += -DSHELL_BOOTCMD=$(SHELL_BOOTCMD)",
                     MAKE, flags=re.M))
sabotage("the raw switch written instead of the effective one",
         BOOT_MAKE,
         make=MAKE.replace("shell_bootcmd.h $(SHELL_BOOTCMD_ON)", "shell_bootcmd.h $(SHELL_BOOTCMD)"))
sabotage("panel_scan.o kept on a flip",
         BOOT_MAKE,
         make=re.sub(r"(write_shell_bootcmd\.py.*?) \$\(BUILD_DIR\)/panel_scan\.o", r"\1", MAKE))
sabotage("the boot-only listener in the SHELL_REMOTE image too",
         BOOT_MAKE,
         make=MAKE.replace("ifeq ($(SHELL_BOOTCMD)$(SHELL_PANEL_SCAN)$(SHELL_REMOTE),110)",
                           "ifeq ($(SHELL_BOOTCMD)$(SHELL_PANEL_SCAN),11)"))
sabotage("SHELL_BOOTCMD defaulting to 0",
         BOOT_MAKE,
         make=MAKE.replace("SHELL_BOOTCMD ?= 1", "SHELL_BOOTCMD ?= 0"))
sabotage("SHELL_BOOTCMD unvalidated",
         BOOT_MAKE,
         make=MAKE.replace("$(error SHELL_BOOTCMD must be 0 or 1)", "$(warning SHELL_BOOTCMD)"))
sabotage("the boot-only sources under the raw switch",
         SOURCES_ONLY,
         make=MAKE.replace("ifeq ($(SHELL_BOOTCMD_ON),1)\nCPP_SOURCES", "ifeq ($(SHELL_BOOTCMD),1)\nCPP_SOURCES", 1))
sabotage("the workloads in the boot-only image",
         SOURCES_ONLY,
         make=MAKE.replace("CPP_SOURCES += remote_cmd.cpp remote_listen.cpp\nendif",
                           "CPP_SOURCES += remote_cmd.cpp remote_listen.cpp\nCPP_SOURCES += workload_run.cpp\nendif"))
sabotage("the boot-only image without the listener sources",
         BOOT_SOURCES,
         make=MAKE.replace("ifeq ($(SHELL_BOOTCMD_ON),1)\nCPP_SOURCES += remote_cmd.cpp remote_listen.cpp",
                           "ifeq ($(SHELL_BOOTCMD_ON),1)\nCPP_SOURCES += remote_cmd.cpp"))
sabotage("no announce line in the boot-only image",
         BOOT_ANNOUNCE,
         scan=SCAN.replace('"SHELL_REMOTE on build=%s cmds=boot"', '"SHELL_BOOTCMD on build=%s"'))
sabotage("the announce line every pass",
         BOOT_ANNOUNCE,
         scan=SCAN.replace("if(bootcmd_pass++ % kBootcmdEvery == 0)", "if(true)"))
sabotage("the announce line every 20th pass",
         BOOT_ANNOUNCE,
         scan=SCAN.replace("kBootcmdEvery = 10;", "kBootcmdEvery = 20;"))
sabotage("a host that wants `build=` last",
         BOOT_ANNOUNCE,
         scan=SCAN.replace('"SHELL_REMOTE on build=%s cmds=boot"', '"SHELL_REMOTE on cmds=boot build=%s"'))
sabotage("no pong in the boot-only image",
         BOOT_ANNOUNCE,
         scan=re.sub(r"(#if SHELL_BOOTCMD\n(?:[^\n]*\n){0,12}?)[ \t]*if\(remote_take_ping\(\)\) "
                     r"hw\.PrintLine\(\"SHELL_REMOTE pong\"\);\n", r"\1", SCAN, count=1))
sabotage("the save suppressed in the boot-only image",
         BOOT_SAVES,
         scan=re.sub(r"#if SHELL_REMOTE(\s*\n\s*// Saves are suppressed)", r"#if SHELL_REMOTE || SHELL_BOOTCMD\1",
                     SCAN, count=1))
sabotage("a command poll in the boot-only callback",
         BOOT_SAME_CB,
         scan=SCAN.replace("void panel_scan_tick(bench::Board& hw, spky::Instrument& inst)\n{",
                           "void panel_scan_tick(bench::Board& hw, spky::Instrument& inst)\n{\n"
                           "#if SHELL_BOOTCMD\n    { Command c; (void)remote_poll(&c); }\n#endif", 1))
sabotage("a guard in the boot-only AudioCallback",
         BOOT_SAME_CB,
         main=MAIN.replace("#if SHELL_REMOTE\n    // The overrun guard",
                           "#if SHELL_REMOTE || SHELL_BOOTCMD\n    // The overrun guard", 1))
sabotage("main.cpp without the switch header",
         "main.cpp includes shell_remote.h",
         main=MAIN.replace('#include "shell_remote.h"', ""))
sabotage("panel_scan.cpp without the switch header",
         "panel_scan.cpp includes shell_remote.h",
         scan=SCAN.replace('#include "shell_remote.h"', ""))
sabotage("the switch as a bare -D",
         "the Makefile resolves SHELL_REMOTE at parse time",
         make=re.sub(r"^.*write_shell_remote\.py.*$", "C_DEFS += -DSHELL_REMOTE=$(SHELL_REMOTE)",
                     MAKE, flags=re.M))
sabotage("main.o not deleted on a flip",
         "the Makefile resolves SHELL_REMOTE at parse time",
         make=re.sub(r"(write_shell_remote\.py.*?) \$\(BUILD_DIR\)/main\.o", r"\1", MAKE))
sabotage("the listener in every image",
         SOURCES_ONLY,
         make=MAKE.replace("ifeq ($(SHELL_REMOTE),1)\nCPP_SOURCES", "ifeq (1,1)\nCPP_SOURCES", 1))

# The deferred review items of Task 2 (carried in Task 6).
sabotage("a `for(;;)` in on_rx()",
         "on_rx() never delays, prints or spins",
         listen=in_on_rx(LISTEN, "for(;;) {}"))
sabotage("a `for (;;)` in on_rx()",
         "on_rx() never delays, prints or spins",
         listen=in_on_rx(LISTEN, "for (;;) {}"))
sabotage("a do-loop in on_rx()",
         "on_rx() never delays, prints or spins",
         listen=in_on_rx(LISTEN, "do { } while(g_ping);"))
sabotage("a 1 KB alignment on another object, the table at 256",
         "the RAM vector table routes NMI and all four faults and sets a 1 KB-aligned VTOR",
         listen=re.sub(r"aligned\s*\(\s*1024\s*\)(\)\)\s*uint32_t\s+g_ram_vectors)", r"aligned(256)\1",
                       LISTEN).replace("LineAssembler g_assembler;",
                                       "alignas(1024) uint32_t g_spare[4];\nLineAssembler g_assembler;"))
sabotage("VTOR pointed somewhere else",
         "the RAM vector table routes NMI and all four faults and sets a 1 KB-aligned VTOR",
         listen=LISTEN.replace("reinterpret_cast<uint32_t>(g_ram_vectors)",
                               "reinterpret_cast<uint32_t>(g_pfnVectors)"))
sabotage("`b .` in the fault stub",
         "the fault chain reaches remote_enter_bootloader()",
         listen=re.sub(r'"b\s+shell_remote_fault_reset \\n"', r'"b    . \\n"', LISTEN))
sabotage("the fault reset spins instead of entering",
         "the fault chain reaches remote_enter_bootloader()",
         listen=in_body(LISTEN, "void shell_remote_fault_reset(",
                        "shell::remote_enter_bootloader();", "for(;;) {}"))
sabotage("the vectors on another handler",
         "the fault chain reaches remote_enter_bootloader()",
         listen=LISTEN.replace("&shell_remote_fault_entry", "&shell_remote_fault_reset"))

# Task 5's wiring.
sabotage("the guard never consulted in the callback",
         "the tripped guard silences process() in AudioCallback, fed per callback",
         main=MAIN.replace("if(shell::panel_scan_guard_tripped())", "if(false)"))
sabotage("the guard's silence in every image",
         "the tripped guard silences process() in AudioCallback, fed per callback",
         main=re.sub(r"#if SHELL_REMOTE(\s*\n\s*// The overrun guard)", r"#if 1\1", MAIN, count=1))
sabotage("process() after the silence, not in its else",
         "the tripped guard silences process() in AudioCallback, fed per callback",
         main=re.sub(r"\n(\s*)else\n(#endif\n\s*inst\.process\()", r"\n\1(void)0;\n\2", MAIN, count=1))
sabotage("the guard never fed",
         "the tripped guard silences process() in AudioCallback, fed per callback",
         scan=SCAN.replace("g_guard.block(dc, period)", "false"))
sabotage("remote_block() twice per tick",
         "remote_poll() runs once per block, from panel_scan_tick()'s top level",
         scan=SCAN.replace("    remote_block();\n", "    remote_block();\n    remote_block();\n", 1))
sabotage("remote_block() inside a branch",
         "remote_poll() runs once per block, from panel_scan_tick()'s top level",
         scan=SCAN.replace("    remote_block();\n", "    if(g_sweeps & 1u) { remote_block(); }\n", 1))
sabotage("remote_block() in every image",
         "remote_poll() runs once per block, from panel_scan_tick()'s top level",
         scan=re.sub(r"#if SHELL_REMOTE(\s*\n(?:\s*//[^\n]*\n)*\s*remote_block\(\);)", r"#if 1\1",
                     SCAN, count=1))
sabotage("remote_poll() drained in a loop",
         "remote_poll() runs once per block, from panel_scan_tick()'s top level",
         scan=SCAN.replace("if(remote_poll(&c)) remote_command(c);",
                           "while(remote_poll(&c)) remote_command(c);"))
sabotage("a second poll on the persistence side",
         "remote_poll() runs once per block, from panel_scan_tick()'s top level",
         scan=SCAN.replace("if(g_take.done && remote_poll(&c)) remote_command(c);",
                           "if(g_take.done && remote_poll(&c)) remote_command(c);\n"
                           "    if(remote_poll(&c)) remote_command(c);"))
sabotage("the save path in every image",
         "persist_write() compiles only without SHELL_REMOTE",
         scan=re.sub(r"#if SHELL_REMOTE(\s*\n\s*// Saves are suppressed)", r"#if 0\1", SCAN, count=1))
sabotage("a save on the suppressed side",
         "persist_write() compiles only without SHELL_REMOTE",
         scan=SCAN.replace('hw.PrintLine("SHELL_PERSIST save suppressed");',
                           "(void)persist_write(hw.qspi, PersistRecord{});"))

# Task 6: the measurement switch.
PROC_LOOP = "    for (size_t i = 0; i < n; ++i) {\n"
TOG_LINE = re.search(r"\n[^\n]*\bprobe::toggles\(\);[^\n]*", INST)
sabotage("the toggles read inside the sample loop",
         "probe::toggles() is latched once in Instrument::process, ahead of the sample loop",
         inst=(INST.replace(TOG_LINE.group(0), "", 1).replace(
             PROC_LOOP, PROC_LOOP + TOG_LINE.group(0).lstrip("\n") + "\n", 1)
             if TOG_LINE else INST))
sabotage("a second toggles read",
         "probe::toggles() is latched once in Instrument::process, ahead of the sample loop",
         inst=INST.replace("void Instrument::process(",
                           "static uint32_t g_t2 = probe::toggles();\nvoid Instrument::process(", 1))
sabotage("the toggles never read",
         "probe::toggles() is latched once in Instrument::process, ahead of the sample loop",
         inst=re.sub(r"\bprobe::toggles\(\)", "0u", INST))
LANE = os.path.join("engine", "mod", "lane.cpp")
sabotage("a toggles read in the lanes",
         "no other engine file calls toggles()",
         engine=dict(ENGINE, **{LANE: ENGINE.get(LANE, "") +
                                "\nstatic uint32_t g_t3 = spky::probe::toggles();\n"}))
sabotage("the measure switch as a bare -D",
         "the Makefile resolves SHELL_REMOTE_MEASURE at parse time and refuses it alone",
         make=re.sub(r"^.*write_shell_remote_measure\.py.*$",
                     "C_DEFS += -DSHELL_REMOTE_MEASURE=$(SHELL_REMOTE_MEASURE)", MAKE, flags=re.M))
sabotage("the measure switch without its SHELL_REMOTE error",
         "the Makefile resolves SHELL_REMOTE_MEASURE at parse time and refuses it alone",
         make=MAKE.replace("$(error SHELL_REMOTE_MEASURE=1 needs SHELL_REMOTE=1",
                           "$(warning SHELL_REMOTE_MEASURE=1 needs SHELL_REMOTE=1"))
sabotage("the probe defines in every image",
         "the probe defines are on in the measurement image alone",
         make=MAKE.replace("C_DEFS += -DSPKY_PROBE_MARKS=1 -DSPKY_PROBE_TOGGLES=1",
                           "endif\nC_DEFS += -DSPKY_PROBE_MARKS=1 -DSPKY_PROBE_TOGGLES=1\n"
                           "ifeq ($(SHELL_REMOTE_MEASURE),1)"))
sabotage("the measure writer keeping objects",
         "the measure writer deletes every object on a flip",
         writer=WRITER.replace(".unlink(missing_ok=True)", ".exists()"))
sabotage("the measure writer deleting only main.o",
         "the measure writer deletes every object on a flip",
         writer=WRITER.replace('glob("*.o")', 'glob("main.o")'))
sabotage("LATE after process()",
         "LATE burns in AudioCallback after the tick, before process(), under the switch",
         main=MAIN.replace("    shell::panel_scan_late();\n", "", 1).replace(
             "    shell::panel_scan_gate(out[0], out[1], size);\n",
             "    shell::panel_scan_late();\n    shell::panel_scan_gate(out[0], out[1], size);\n", 1))
sabotage("LATE in every image",
         "LATE burns in AudioCallback after the tick, before process(), under the switch",
         main=re.sub(r"#if SHELL_REMOTE_MEASURE(\s*\n(?:\s*//[^\n]*\n)*\s*shell::panel_scan_late\(\);)",
                     r"#if 1\1", MAIN, count=1))
sabotage("main.cpp without the measure header",
         "main.cpp includes shell_remote_measure.h",
         main=MAIN.replace('#include "shell_remote_measure.h"', ""))
sabotage("panel_scan.cpp without the measure header",
         "panel_scan.cpp includes shell_remote_measure.h",
         scan=SCAN.replace('#include "shell_remote_measure.h"', ""))

# Task 6 fix round 1.
TAIL_STMTS = ("    g_tail_last = cycles_now() - c0 - dc;\n"
              "    g_blk_done  = g_blk_done + 1u;\n")
sabotage("the tail stamped before cb_profile's accumulation",
         "the probe tail is stamped last in panel_scan_block_end(), under the switch",
         scan=SCAN.replace(TAIL_STMTS, "", 1).replace(
             "    sec_block(dc, processed);\n",
             "    sec_block(dc, processed);\n" + TAIL_STMTS, 1))
sabotage("the tail in every image",
         "the probe tail is stamped last in panel_scan_block_end(), under the switch",
         scan=re.sub(r"#if SHELL_REMOTE_MEASURE(\s*\n(?:\s*//[^\n]*\n)*\s*g_tail_last = )",
                     r"#if 1\1", SCAN, count=1))
sabotage("the block counted twice",
         "the probe tail is stamped last in panel_scan_block_end(), under the switch",
         scan=SCAN.replace("    sec_block(dc, processed);\n",
                           "    sec_block(dc, processed);\n    g_blk_done = g_blk_done + 1u;\n", 1))
sabotage("the tail never read in busy_window()",
         "busy_window() splits each block's tail out, SHELL_OUT prints tail and ext",
         scan=SCAN.replace("tail = g_tail_last;", "tail = 0;"))
sabotage("out_split() without the tail",
         "busy_window() splits each block's tail out, SHELL_OUT prints tail and ext",
         scan=SCAN.replace("out_split(acc, cb, tail)", "out_split(acc, cb, 0u)"))
sabotage("ext_max dropped from SHELL_OUT",
         "busy_window() splits each block's tail out, SHELL_OUT prints tail and ext",
         scan=SCAN.replace(" ext_max=%u neg=%d\"", " neg=%d\"").replace(
             "\n                             static_cast<unsigned>(out_stats.ext_max),", ""))
sabotage("the streak left frozen on a bit-31 flip",
         "a TOG that flips bit 31 restarts the guard's streak",
         scan=SCAN.replace("g_guard.reset_streak();", "(void)0;"))
sabotage("the streak reset on a bit-30 flip instead",
         "a TOG that flips bit 31 restarts the guard's streak",
         scan=SCAN.replace(">> spky::probe::kBitShellGuardOff) & 1u) g_guard.reset_streak();",
                           ">> spky::probe::kBitShellLateRepeat) & 1u) g_guard.reset_streak();"))
sabotage("a TOG that leaves the run counting",
         "a TOG re-settles the run and ends its settle by block number",
         scan=SCAN.replace("g_run.resettle(g_rblock, kTogSettleBlocks);", "(void)0;"))
sabotage("the settle a block short (the old countdown)",
         "a TOG re-settles the run and ends its settle by block number",
         scan=SCAN.replace("if(g_tog_settle.ends(now))", "if(g_tog_settle.ends(now + 1u))"))
sabotage("a 100-block settle",
         "a TOG re-settles the run and ends its settle by block number",
         scan=SCAN.replace("kTogSettleBlocks = 200;", "kTogSettleBlocks = 100;"))
sabotage("the run line without tog=",
         "the run line prints tog=, the settled line the run's gen",
         scan=SCAN.replace('gest=%d "\n                         "tog=%08x"', 'gest=%d"'))
sabotage("the toggle word never published",
         "the run line prints tog=, the settled line the run's gen",
         scan=SCAN.replace("g_rs_tog      = spky::probe::toggles();", "g_rs_tog      = 0;"))
sabotage("the settled ack without the gen",
         "the run line prints tog=, the settled line the run's gen",
         scan=SCAN.replace("kAckTogSettledBase + static_cast<int32_t>(g_run.gen() & kGenPrintMask)",
                           "kAckTogSettledBase"))

# The final review's fix wave.
sabotage("the burn ignoring the guard (C1)",
         "a tripped guard stops the LATE burn (C1)",
         scan=in_body(SCAN, "void panel_scan_late(", "    if(panel_scan_guard_tripped()) return;\n", ""))
sabotage("the guard asked after the burn (C1)",
         "a tripped guard stops the LATE burn (C1)",
         scan=in_body(SCAN, "void panel_scan_late(", "    if(panel_scan_guard_tripped()) return;\n",
                      "").replace("    while(cycles_now() - t0 < n) {}\n",
                                  "    while(cycles_now() - t0 < n) {}\n"
                                  "    if(panel_scan_guard_tripped()) return;\n", 1))
sabotage("the period never passed (I2)",
         "the guard is fed the entry-to-entry period and says what tripped it (I2)",
         scan=SCAN.replace("g_guard.block(dc, period)", "g_guard.block(dc, 0u)"))
sabotage("the period read after the WIN window overwrote it (I2)",
         "the guard is fed the entry-to-entry period and says what tripped it (I2)",
         scan=re.sub(r"(    g_win_prev_entry = c0;\n)",
                     r"\1    const uint32_t period = g_win_prev_entry != 0u ? c0 - g_win_prev_entry : 0u;\n",
                     SCAN.replace("    const uint32_t period = g_win_prev_entry != 0u ? c0 - g_win_prev_entry"
                                  " : 0u;\n", "", 1), count=1))
sabotage("the guard built without the period limit (I2)",
         "the guard is fed the entry-to-entry period and says what tripped it (I2)",
         scan=SCAN.replace("g_guard(kGuardLimitCycles, kGuardBlocks, kGuardPeriodCycles)",
                           "g_guard(kGuardLimitCycles, kGuardBlocks)"))
sabotage("the trip line without its criterion (I2)",
         "the guard is fed the entry-to-entry period and says what tripped it (I2)",
         scan=SCAN.replace('"SHELL_REMOTE guard tripped blk=%u by=%s"', '"SHELL_REMOTE guard tripped blk=%u"'))
sabotage("the reset without a cache clean (M1)",
         "remote_enter_bootloader() cleans the D-cache before the reset (M1)",
         listen=in_body(LISTEN, "void remote_enter_bootloader(", "SCB_CleanDCache();", "__DSB();"))
sabotage("the cache cleaned before the request is written (M1)",
         "remote_enter_bootloader() cleans the D-cache before the reset (M1)",
         listen=in_body(LISTEN, "void remote_enter_bootloader(", "SCB_CleanDCache();", "").replace(
             "    daisy::System::InitBackupSram();\n",
             "    SCB_CleanDCache();\n    daisy::System::InitBackupSram();\n", 1))
sabotage("NMI left on libDaisy's handler (M8)",
         "the RAM vector table routes NMI and all four faults and sets a 1 KB-aligned VTOR",
         listen=re.sub(r"\n[^\n]*NonMaskableInt_IRQn[^\n]*", "", LISTEN))
sabotage("the saturated subtractions never counted (out_split)",
         "busy_window() splits each block's tail out, SHELL_OUT prints tail and ext",
         scan=SCAN.replace("if(s.neg) ++o.neg;", "(void)s.neg;"))
sabotage("neg= dropped from SHELL_OUT (out_split)",
         "busy_window() splits each block's tail out, SHELL_OUT prints tail and ext",
         scan=SCAN.replace(" neg=%d\"", "\""))

# The guard re-arms itself (Bastian, 2026-10-29).
SELF = "the guard re-arms itself after kGuardRearmBlocks (500) silent blocks"
SETTLE = "a run's settle end clears a tripped guard (rearm_at_settle_end)"
BLOCKEND = "panel_scan_block_end() notes the self re-arm, the trips and the silent blocks"
PRINTED = "the foreground prints `guard rearm blk= by=silence|settle` and every trip"
TRIPLINE = "the run line counts across trips: per-frame tripped=, cumulative trips="
sabotage("a guard that re-arms one block late",
         SELF, wl_cpp=WL_CPP.replace("if(++silent_ >= rearm_) clear();",
                                     "if(++silent_ > rearm_) clear();"))
sabotage("a guard that latches again",
         SELF, wl_cpp=WL_CPP.replace("if(++silent_ >= rearm_) clear();", "++silent_;"))
sabotage("a re-arm after 1000 blocks",
         SELF, wl_h=WL_H.replace("kGuardRearmBlocks = 500;", "kGuardRearmBlocks = 1000;"))
sabotage("a constructor without the default",
         SELF, wl_h=WL_H.replace("uint32_t rearm_blocks = kGuardRearmBlocks", "uint32_t rearm_blocks"))
sabotage("clear() keeping the silence count",
         SELF, wl_cpp=in_body(WL_CPP, "void OverrunGuard::clear(", "    silent_  = 0;\n", ""))
sabotage("reset_streak() keeping the silence count",
         SELF, wl_cpp=in_body(WL_CPP, "void OverrunGuard::reset_streak(", "    silent_  = 0;\n", ""))
sabotage("the settle end never clears the guard",
         SETTLE, scan=SCAN.replace("if(rearm_at_settle_end(g_guard, g_run, now))", "if(false)"))
sabotage("the settle-end re-arm unsaid",
         SETTLE, scan=SCAN.replace("note_guard_rearm(true);", "(void)0;"))
sabotage("the settle-end re-arm leaving the flag up",
         SETTLE, scan=SCAN.replace("            g_guard_flag = false;\n            note_guard_rearm(true);",
                                   "            note_guard_rearm(true);"))
sabotage("the settle-end re-arm after the gesture steps",
         SETTLE, scan=SCAN.replace("if(g_gest_n > 0 && g_run.counting(now))",
                                   "if(rearm_at_settle_end(g_guard, g_run, now)) { g_guard_flag = false; "
                                   "note_guard_rearm(true); }\n    if(g_gest_n > 0 && g_run.counting(now))"
                                   ).replace("        if(rearm_at_settle_end(g_guard, g_run, now))\n"
                                             "        {\n            g_guard_flag = false;\n"
                                             "            note_guard_rearm(true);\n        }\n", ""))
sabotage("the self re-arm unsaid",
         BLOCKEND, scan=SCAN.replace("note_guard_rearm(false);", "(void)0;"))
sabotage("the silent blocks not counted",
         BLOCKEND, scan=SCAN.replace("g_guard_silent = g_guard_silent + 1u;", "(void)0;"))
sabotage("trips not counted",
         BLOCKEND, scan=SCAN.replace("g_guard_trips = g_guard_trips + 1u;", "(void)0;"))
sabotage("the rearm line dropped",
         PRINTED, scan=SCAN.replace('"SHELL_REMOTE guard rearm blk=%u by=%s"', '"SHELL_REMOTE guard rearm"'))
sabotage("the rearm line without its cause",
         PRINTED, scan=SCAN.replace('g_guard_rearm_settle ? "settle" : "silence"', '"silence"'))
sabotage("the rearm line printed every pass",
         PRINTED, scan=SCAN.replace("if(guard_rearms != guard_rearms_said)", "if(true)"))
sabotage("the trip line only while tripped",
         PRINTED, scan=SCAN.replace("if(g_guard_flag || guard_trips != guard_trips_said)",
                                    "if(g_guard_flag)"))
sabotage("a tripped frame no longer counting",
         TRIPLINE, scan=SCAN.replace("&& after.counting;", "&& after.counting && !tripped;"))
sabotage("the run line without trips=",
         TRIPLINE, scan=SCAN.replace("static_cast<unsigned>(after.trips), after.gest",
                                     "0u, after.gest"))
sabotage("trips never published",
         TRIPLINE, scan=SCAN.replace("g_rs_trips    = g_run.trips();", "g_rs_trips    = 0;"))
sabotage("tripped= never set",
         TRIPLINE, scan=SCAN.replace("const uint32_t guard_silent = g_guard_silent;",
                                     "const uint32_t guard_silent = 0;"))

# SHELL_RGRAIN: the reverb inject hook.
INJ_READ = "set_reverb_inject()'s pointers are read in Instrument::process, ahead of the sample loop"
INJ_GUARD = "the reverb inject hook exists only under SPKY_PROBE_REVERB_INJECT"
INJ_MAKE = "the Makefile defines SPKY_PROBE_REVERB_INJECT in the SHELL_RGRAIN images alone"
INST_H = os.path.join("engine", "instrument.h")
INJ_LATCH = re.search(r"\n[^\n]*=\s*_inj_l;[^\n]*", INST)
sabotage("the inject pointer read per sample",
         INJ_READ, inst=INST.replace("inj_l[i]", "_inj_l[i]"))
sabotage("the inject latch moved into the sample loop",
         INJ_READ,
         inst=(INST.replace(INJ_LATCH.group(0), "", 1).replace(
             PROC_LOOP, PROC_LOOP + INJ_LATCH.group(0).lstrip("\n") + "\n", 1)
             if INJ_LATCH else INST))
sabotage("the left pointer never read",
         INJ_READ, inst=re.sub(r"=\s*_inj_l;", "= nullptr;", INST))
sabotage("the hook unguarded in the header",
         INJ_GUARD,
         engine=dict(ENGINE, **{INST_H: ENGINE.get(INST_H, "").replace(
             "#if defined(SPKY_PROBE_REVERB_INJECT)", "#if 1", 1)}))
sabotage("the hook unguarded in process()",
         INJ_GUARD,
         engine=dict(ENGINE, **{os.path.join("engine", "instrument.cpp"):
                                INST.replace("#if defined(SPKY_PROBE_REVERB_INJECT)", "#if 1", 1)}))
sabotage("the inject define in every image",
         INJ_MAKE,
         make=MAKE.replace("C_DEFS += -DSPKY_PROBE_REVERB_INJECT=1",
                           "endif\nC_DEFS += -DSPKY_PROBE_REVERB_INJECT=1\n"
                           "ifneq ($(SHELL_RGRAIN),0)"))
sabotage("the rgrain switch as a bare -D",
         INJ_MAKE,
         make=re.sub(r"^.*write_shell_rgrain\.py.*$", "C_DEFS += -DSHELL_RGRAIN=$(SHELL_RGRAIN)",
                     MAKE, flags=re.M))

print("%d failed" % len(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
