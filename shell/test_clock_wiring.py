#!/usr/bin/env python3
"""Source guard for the firmware's CLOCK and RESET wiring (spec
docs/superpowers/specs/2026-10-19-clock-and-reset-jacks-design.md sections 6
and 8.5). Plain script -- pytest is not installed here -- and its exit code is
the verdict.

What it holds, on the source text of shell/:
  - the gate latch (gate_latch.h / .cpp) is referenced only under
    !SHELL_COUPON_PROBE, and gate_latch.cpp is built only into the Rev A play
    image: the coupon's B9/B10 float, and random edges there would set wild
    tempos and restart songs (spec section 6);
  - main.cpp clears the pending bits (gate_latch_arm()) as the last statement
    before the Rev A play path's StartAudio(), so boot glitches never arrive
    as edges;
  - panel_scan_tick() runs the block in the brief's order: take the edges,
    step the shared law, pulse and restart, the control law with the measured
    tempo, the TEMPO release, the lamps;
  - control::ClockLaw is the only clock logic in shell/: nothing else calls
    clock_pulse() / restart() / reset_transport(), nothing polls GateIn, and
    no measured_bpm is computed (only the law's value or 0 is assigned).
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


# --- the C preprocessor, as far as these files need it --------------------
def excludes_coupon(cond, in_else):
    """Whether the branch of `#if cond` (or its #else) can only be compiled
    with SHELL_COUPON_PROBE == 0. Conservative: anything it does not
    recognise does not exclude the coupon."""
    cond = cond.strip()
    if "||" in cond:
        return False
    terms = [t.strip().strip("()").strip() for t in cond.split("&&")]
    if in_else:
        return cond == "SHELL_COUPON_PROBE"
    return "!SHELL_COUPON_PROBE" in terms


def coupon_free_lines(text):
    """Line numbers (0-based) whose code can only compile without the coupon."""
    stack = []          # [cond, in_else]
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
        if any(excludes_coupon(c, e) for c, e in stack):
            out.add(i)
    return out


def mentions(text, needle):
    return [i for i, l in enumerate(text.splitlines()) if needle in strip_comment(l)]


def all_coupon_free(text, needle):
    free = coupon_free_lines(text)
    hits = mentions(text, needle)
    return bool(hits) and all(i in free for i in hits)


# --- make conditionals ----------------------------------------------------
REVA_PLAY = "ifeq ($(SHELL_PANEL_SCAN)$(SHELL_COUPON_PROBE),10)"


def make_only_in_reva_play(text, needle):
    stack = []
    hits = 0
    for raw in text.splitlines():
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
            hits += 1
            if REVA_PLAY not in stack:
                return False
    return hits > 0


# --- order ----------------------------------------------------------------
def code_lines(text):
    """(index, stripped code) of every line that is not blank, a comment or a
    preprocessor directive."""
    out = []
    for i, raw in enumerate(text.splitlines()):
        s = strip_comment(raw).strip()
        if not s or s.startswith("#"):
            continue
        out.append((i, s))
    return out


def arm_right_before_start_audio(main_text):
    """In the SHELL_PANEL_SCAN path, gate_latch_arm() comes after
    panel_scan_init() and is the last statement before StartAudio()."""
    lines = code_lines(main_text)
    starts = [k for k, (_, s) in enumerate(lines) if "panel_scan_init(" in s]
    if len(starts) != 1:
        return False
    k = starts[0]
    tail = lines[k + 1:]
    arm = [j for j, (_, s) in enumerate(tail) if "gate_latch_arm()" in s]
    if len(arm) != 1:
        return False
    j = arm[0]
    if j + 1 >= len(tail) or "hw.StartAudio(AudioCallback)" not in tail[j + 1][1]:
        return False
    # Nothing between panel_scan_init() and the arm starts audio.
    return not any("StartAudio" in s for _, s in tail[:j])


def function_body(text, signature):
    i = text.find(signature)
    if i < 0:
        return ""
    j = text.find("{", i)
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[j:k + 1]
    return ""


TICK_ORDER = [
    "gate_latch_take()",
    "g_clock.step(",
    "inst.clock_pulse()",
    "inst.restart(",
    "control_tick(g_knobs, inst,",
    "apply_clock_events(",
    "led_tick(",
]


def tick_in_order(scan_text):
    body = "\n".join(strip_comment(l) for l in
                     function_body(scan_text, "void panel_scan_tick(").splitlines())
    pos = [body.find(n) for n in TICK_ORDER]
    return all(p >= 0 for p in pos) and pos == sorted(pos) and \
        all(body.count(n) == 1 for n in TICK_ORDER)


def step_uses_block_size(scan_text):
    body = function_body(scan_text, "void panel_scan_tick(")
    m = re.search(r"g_clock\.step\(([^,]*),", body)
    return bool(m) and not re.search(r"\b96\b", m.group(1)) and \
        "AudioBlockSize()" in body


# --- clock logic only in the law ------------------------------------------
CLOCK_CALLS = ("clock_pulse(", ".restart(", "reset_transport(",
               "gate_in_1", "gate_in_2", ".Trig()")
# A tempo is never computed in shell/: whatever is assigned to a measured_bpm
# is the clock law's value, the plain pass-through, or the coupon's 0.
BPM_SOURCES = ("clk.measured_bpm", "measured_bpm", "0.0f", "0.f", "0")


def bpm_assignments_ok(text):
    for line in text.splitlines():
        for m in re.finditer(r"measured_bpm\s*=(?!=)\s*([^;]*);", strip_comment(line)):
            if m.group(1).strip() not in BPM_SOURCES:
                return False
    return True


def shell_sources():
    out = {}
    for name in sorted(os.listdir(HERE)):
        if name.endswith((".cpp", ".h")) and name != "generated_panel_map.h":
            out[name] = read(name)
    return out


def clock_calls_only_in_tick(sources):
    """The engine's clock entries are called from panel_scan.cpp alone, and
    nothing in shell/ polls a GateIn or computes a tempo."""
    for name, text in sources.items():
        if not bpm_assignments_ok(text):
            return False
        for needle in CLOCK_CALLS:
            if not mentions(text, needle):
                continue
            if name == "panel_scan.cpp" and needle in ("clock_pulse(", ".restart("):
                continue
            return False
    return "control::ClockLaw" in sources.get("panel_scan.cpp", "")


def run_checks(scan, main, make, tick, sources):
    results = {}
    results["gate_latch is referenced in panel_scan.cpp only under !SHELL_COUPON_PROBE"] = \
        all_coupon_free(scan, "gate_latch")
    results["panel_scan.cpp inits and takes the latch"] = \
        bool(mentions(scan, "gate_latch_init()")) and bool(mentions(scan, "gate_latch_take()"))
    results["gate_latch is referenced in main.cpp only under !SHELL_COUPON_PROBE"] = \
        all_coupon_free(main, "gate_latch")
    results["gate_latch.cpp is built into the Rev A play image only"] = \
        make_only_in_reva_play(make, "gate_latch.cpp")
    results["gate_latch_arm() is the last statement before the play path's StartAudio()"] = \
        arm_right_before_start_audio(main)
    results["panel_scan_tick() runs take, law, pulse, restart, control, release, lamps in order"] = \
        tick_in_order(scan)
    results["the law steps by the board's block size, not a literal"] = \
        step_uses_block_size(scan)
    results["the shared law is the only clock logic in shell/"] = \
        clock_calls_only_in_tick(sources)
    results["the clock law is compiled only under !SHELL_COUPON_PROBE"] = \
        all_coupon_free(scan, "g_clock")
    results["control_tick() hands measured_bpm to the law's options"] = \
        "g_opt.measured_bpm = measured_bpm" in tick
    return results


SCAN = read("panel_scan.cpp")
MAIN = read("main.cpp")
MAKE = read("Makefile")
TICK = read("control_tick.cpp")
SOURCES = shell_sources()

BASE = run_checks(SCAN, MAIN, MAKE, TICK, SOURCES)
for name, ok in BASE.items():
    check(name, ok)


# --- sabotages: each must turn exactly its check red -----------------------
def sabotage(label, target, scan=SCAN, main=MAIN, make=MAKE, tick=TICK, sources=None):
    r = run_checks(scan, main, make, tick, sources if sources is not None else SOURCES)
    check("sabotage %s goes red" % label, BASE.get(target) and not r[target])


def take_out_of_guard(text):
    # The take moved in front of the guarded block.
    return text.replace("void panel_scan_tick(bench::Board& hw, spky::Instrument& inst)\n{",
                        "void panel_scan_tick(bench::Board& hw, spky::Instrument& inst)\n{\n"
                        "    (void)gate_latch_take();", 1)


sabotage("gate_latch_take() outside the coupon guard",
         "gate_latch is referenced in panel_scan.cpp only under !SHELL_COUPON_PROBE",
         scan=take_out_of_guard(SCAN))
sabotage("no gate_latch_init() call",
         "panel_scan.cpp inits and takes the latch",
         scan=SCAN.replace("gate_latch_init()", "gate_latch_nothing()"))
sabotage("gate_latch_arm() unguarded in main.cpp",
         "gate_latch is referenced in main.cpp only under !SHELL_COUPON_PROBE",
         main=MAIN.replace("#if !SHELL_COUPON_PROBE", "#if 1"))
sabotage("gate_latch.cpp in every image",
         "gate_latch.cpp is built into the Rev A play image only",
         make=MAKE.replace(REVA_PLAY, "ifeq (1,1)"))
sabotage("StartAudio() before gate_latch_arm()",
         "gate_latch_arm() is the last statement before the play path's StartAudio()",
         main=MAIN.replace("shell::gate_latch_arm();", "hw.Delay(1);\n    shell::gate_latch_arm();\n    hw.Delay(1);", 1))
sabotage("control_tick() before the law",
         "panel_scan_tick() runs take, law, pulse, restart, control, release, lamps in order",
         scan=SCAN.replace("g_clock.step(", "g_clock_late.step(", 1)
                  .replace("const control::Events ev = control_tick(",
                           "g_clock.step(0, false, false, 0.f, 0.f);\n"
                           "    const control::Events ev = control_tick(", 1))
sabotage("a literal 96 block",
         "the law steps by the board's block size, not a literal",
         scan=re.sub(r"g_clock\.step\([^,]*,", "g_clock.step(96,", SCAN, count=1))
sabotage("a second clock_pulse() caller",
         "the shared law is the only clock logic in shell/",
         sources=dict(SOURCES, **{"keys.cpp": SOURCES.get("keys.cpp", "") + "\nvoid x(){ inst.clock_pulse(); }\n"}))
sabotage("a GateIn poll",
         "the shared law is the only clock logic in shell/",
         sources=dict(SOURCES, **{"main.cpp": MAIN + "\nstatic bool p(){ return hw.gate_in_1.Trig(); }\n"}))
sabotage("a tempo computed in shell/",
         "the shared law is the only clock logic in shell/",
         scan=SCAN, sources=dict(SOURCES, **{"panel_scan.cpp": SCAN.replace(
             "const float measured_bpm = clk.measured_bpm;",
             "const float measured_bpm = 60.f * 48000.f / g_clk_samples;", 1)}))
sabotage("measured_bpm not passed",
         "control_tick() hands measured_bpm to the law's options",
         tick=TICK.replace("g_opt.measured_bpm = measured_bpm", "(void)measured_bpm"))
sabotage("the law outside the guard",
         "the clock law is compiled only under !SHELL_COUPON_PROBE",
         scan=SCAN.replace("namespace shell {",
                           "namespace shell {\nvoid z() { g_clock.init(1.f); }", 1))

print("%d failed" % len(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
