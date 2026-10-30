"""Refuses an image in which an engine NaN / isfinite guard has been compiled away.

SHELL_FAST_MATH=1 (-ffast-math -fno-finite-math-only on engine/) is only safe
while `std::isfinite` stays a real test. Spec
docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md section 4 lands a
fast-math switch only with "a test that goes red when a guard is folded":
this is it, and every image build runs it after the link (shell/Makefile, the
chain that also runs qspi_placement.py).

It reads the disassembly of the LINKED ELF (`objdump -d -C`), not of the
objects: an inline header function (Transport's, for one) is emitted into every
object that uses it, the linker keeps one copy, and only the linked image says
which copy runs. For each function that must still guard, the function's body
has to contain the guard's two marks:
  - a `vabs.f32`, and
  - the literal pool word `.word 0x7f7fffff` (FLT_MAX),
which is how arm-none-eabi-gcc compiles `std::isfinite(float)` (|x| <= FLT_MAX)
without -ffinite-math-only. With -ffinite-math-only the test is folded to true
and the literal is gone.

The guards (function: what it protects):
  Instrument::_apply_tempo      Transport::set_bpm, inlined via Center::set_tempo_bpm
                                (engine/center/transport.h:23)
  Instrument::set_tempo_bpm     engine/instrument.cpp:232
  Instrument::init              the same guard, inlined (set_tempo_bpm(_bpm))
  Instrument::set_pace          engine/instrument.cpp:238
  BbdEngine::process            BbdLine::SetClock, inlined (engine/fx/bbd.h:315)
  shell::panel_scan_tick        Transport::clock_pulse / clock_pulse_counted, inlined
                                via Instrument::clock_pulse* (transport.h:37, :52);
                                shell code, so not affected by the engine's flags --
                                checked anyway; only in images with SHELL_PANEL_SCAN=1
bbd_clock_hz (bbd.h:202) has no caller in the firmware and is not compiled in.

A required function that is missing from the image is a refusal too (an inline
that moved elsewhere would otherwise escape the check); only the optional ones
may be absent.

usage: math_guards.py <objdump -d -C output of the linked ELF>
"""
import re, sys

# (function name as `objdump -C` prints it, optional?)
GUARDED = [
    ("spky::Instrument::_apply_tempo()", False),
    ("spky::Instrument::set_tempo_bpm(float)", False),
    ("spky::Instrument::init(float, spky::FxMem const&)", False),
    ("spky::Instrument::set_pace(float)", False),
    ("spky::BbdEngine::process(float&, float&)", False),
    ("shell::panel_scan_tick(daisy::patch_sm::DaisyPatchSM&, spky::Instrument&)", True),
]

_HEADER = re.compile(r"^[0-9a-f]+ <(.+)>:$")
_FLT_MAX_WORD = re.compile(r"\.word\s+0x7f7fffff\b")
_VABS = re.compile(r"\bvabs\.f32\b")


def function_bodies(dis_text):
    """{demangled name: [lines of its body]} of an `objdump -d -C` listing."""
    bodies, name = {}, None
    for line in dis_text.splitlines():
        m = _HEADER.match(line)
        if m:
            name = m.group(1)
            bodies.setdefault(name, [])
        elif name is not None:
            bodies[name].append(line)
    return bodies


def refusals(dis_text, guarded=GUARDED):
    bodies = function_bodies(dis_text)
    out = []
    for name, optional in guarded:
        body = bodies.get(name)
        if body is None:
            if not optional:
                out.append("guarded function missing from the image: " + name)
            continue
        text = "\n".join(body)
        if not _VABS.search(text) or not _FLT_MAX_WORD.search(text):
            out.append("isfinite guard folded away in " + name)
    return out


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        sys.exit(1)
    bad = refusals(open(sys.argv[1], encoding="utf-8", errors="replace").read())
    for b in bad:
        print("math guard: " + b)
    sys.exit(1 if bad else 0)
