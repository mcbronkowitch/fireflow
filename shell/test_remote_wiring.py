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
Every check has a sabotage below that must turn it red.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

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


def remote_lines(text):
    """Line numbers (0-based) whose code compiles only under SHELL_REMOTE."""
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
        if any(under_remote(c, e) for c, e in stack):
            out.add(i)
    return out


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
    return bool(body) and "Delay" not in body and "Print" not in body \
        and "while(" not in body.replace(" ", "")


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
    return all(n in body for n in names) and "SCB->VTOR" in body and \
        re.search(r"(alignas\s*\(\s*1024\s*\)|aligned\s*\(\s*1024\s*\))", code) is not None


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


def includes_switch(text):
    return any(l.strip() == '#include "shell_remote.h"' for l in text.splitlines())


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


def run_checks(listen, main, scan, make):
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
    return r


LISTEN = read("remote_listen.cpp")
MAIN = read("main.cpp")
SCAN = read("panel_scan.cpp")
MAKE = read("Makefile")

BASE = run_checks(LISTEN, MAIN, SCAN, MAKE)
for name, ok in BASE.items():
    check(name, ok)


# --- sabotages: each must turn its check red --------------------------------
def sabotage(label, target, listen=LISTEN, main=MAIN, scan=SCAN, make=MAKE):
    r = run_checks(listen, main, scan, make)
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

print("%d failed" % len(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
