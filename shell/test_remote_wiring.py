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


def fault_vectors_installed(listen):
    body = code_only(function_body(listen, "void remote_install_fault_vectors("))
    code = code_only(listen)
    names = ("HardFault_IRQn", "MemoryManagement_IRQn", "BusFault_IRQn", "UsageFault_IRQn")
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
    feeds = any("g_guard.block(dc)" in s and i in end_remote for i, s in code_lines(end))
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
    tail = statements_after(main, "int main(void)")
    # The first line after the signature is the opening brace.
    if tail and tail[0][1] == "{":
        tail = tail[1:]
    if not tail or "shell::remote_install_fault_vectors();" not in tail[0][1]:
        return False
    return tail[0][0] in remote_lines(main)


def itcm_failure_enters_bootloader(main):
    lines = code_lines(main)
    remote = remote_lines(main)
    for k, (_, s) in enumerate(lines):
        if "if(!shell::itcm_load())" in s.replace(" ", ""):
            # The branch: from the brace after the if to its matching close.
            depth = 0
            for idx, c in lines[k + 1:]:
                depth += c.count("{") - c.count("}")
                if "shell::remote_enter_bootloader();" in c and idx in remote:
                    return True
                if depth <= 0 and "}" in c:
                    return False
            return False
    return False


def listen_follows_startlog(scan):
    body = function_body(scan, "void run_panel_scan_report(")
    tail = statements_after(body, "hw.StartLog(false);")
    if not tail or tail[0][1] != "remote_listen_start();":
        return False
    # Under SHELL_REMOTE, judged in the body's own line numbering.
    return tail[0][0] in remote_lines(body)


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
            if "ifeq ($(SHELL_REMOTE),1)" not in stack:
                return False
    return hits >= 1 and "remote_listen.cpp" in make and "remote_cmd.cpp" in make


def run_checks(listen, main, scan, make, tick, inst, engine, writer):
    r = {}
    r["on_rx() enters the bootloader on BOOT itself"] = on_rx_enters_bootloader(listen)
    r["on_rx() never delays, prints or spins"] = on_rx_never_delays(listen)
    r["on_rx() holds a whole packet of commands (kMaxCmdsPerPacket = 16)"] = \
        on_rx_holds_a_packet(listen)
    r["remote_enter_bootloader() asks for an infinite timeout without a delay"] = \
        entry_is_infinite_and_quiet(listen)
    r["the RAM vector table routes all four faults and sets a 1 KB-aligned VTOR"] = \
        fault_vectors_installed(listen)
    r["remote_install_fault_vectors() is main()'s first statement, under SHELL_REMOTE"] = \
        fault_vectors_first_in_main(main)
    r["a failed itcm_load() enters the bootloader under SHELL_REMOTE"] = \
        itcm_failure_enters_bootloader(main)
    r["remote_listen_start() follows StartLog() in run_panel_scan_report()"] = \
        listen_follows_startlog(scan)
    r["main.cpp includes shell_remote.h"] = includes_switch(main)
    r["panel_scan.cpp includes shell_remote.h"] = includes_switch(scan)
    r["the Makefile resolves SHELL_REMOTE at parse time"] = make_parse_time(make)
    r["the listener's sources build only under SHELL_REMOTE=1"] = make_sources_only_remote(make)
    r["the fault chain reaches remote_enter_bootloader()"] = fault_chain_whole(listen)
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
ENGINE = read_engine()
INST = ENGINE.get(os.path.join("engine", "instrument.cpp"), "")

SOURCES = dict(listen=LISTEN, main=MAIN, scan=SCAN, make=MAKE, tick=TICK, inst=INST,
               engine=ENGINE, writer=WRITER)
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
         "the RAM vector table routes all four faults and sets a 1 KB-aligned VTOR",
         listen=LISTEN.replace("UsageFault_IRQn", "SVCall_IRQn"))
sabotage("VTOR never set",
         "the RAM vector table routes all four faults and sets a 1 KB-aligned VTOR",
         listen=LISTEN.replace("SCB->VTOR", "g_unused_vtor"))
sabotage("a 256-byte alignment",
         "the RAM vector table routes all four faults and sets a 1 KB-aligned VTOR",
         listen=re.sub(r"(alignas|aligned)\s*\(\s*1024\s*\)", r"\1(256)", LISTEN))
sabotage("board_init() before the vectors",
         "remote_install_fault_vectors() is main()'s first statement, under SHELL_REMOTE",
         main=MAIN.replace("int main(void)\n{", "int main(void)\n{\n    bench::board_init(hw);", 1))
sabotage("the vectors unguarded",
         "remote_install_fault_vectors() is main()'s first statement, under SHELL_REMOTE",
         main=re.sub(r"#if SHELL_REMOTE((?:\s*//[^\n]*)*\s*shell::remote_install_fault_vectors\(\);)",
                     r"#if 1\1", MAIN, count=1))
sabotage("the ITCM branch blinks only",
         "a failed itcm_load() enters the bootloader under SHELL_REMOTE",
         main=MAIN.replace("shell::remote_enter_bootloader();", "hw.SetLed(true);"))
sabotage("the ITCM bootloader entry unguarded",
         "a failed itcm_load() enters the bootloader under SHELL_REMOTE",
         main=re.sub(r"#if SHELL_REMOTE(\s*\n\s*shell::remote_enter_bootloader\(\);)",
                     r"#if 1\1", MAIN))
sabotage("a statement between StartLog() and the listener",
         "remote_listen_start() follows StartLog() in run_panel_scan_report()",
         scan=SCAN.replace("    hw.StartLog(false);\n",
                           "    hw.StartLog(false);\n    hw.Delay(1);\n", 1))
sabotage("the listener never started",
         "remote_listen_start() follows StartLog() in run_panel_scan_report()",
         scan=SCAN.replace("remote_listen_start();", "(void)0;"))
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
         "the listener's sources build only under SHELL_REMOTE=1",
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
         "the RAM vector table routes all four faults and sets a 1 KB-aligned VTOR",
         listen=re.sub(r"aligned\s*\(\s*1024\s*\)(\)\)\s*uint32_t\s+g_ram_vectors)", r"aligned(256)\1",
                       LISTEN).replace("LineAssembler g_assembler;",
                                       "alignas(1024) uint32_t g_spare[4];\nLineAssembler g_assembler;"))
sabotage("VTOR pointed somewhere else",
         "the RAM vector table routes all four faults and sets a 1 KB-aligned VTOR",
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
         scan=SCAN.replace("g_guard.block(dc)", "false"))
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

print("%d failed" % len(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
