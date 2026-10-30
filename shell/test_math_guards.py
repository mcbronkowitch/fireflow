"""Host test for math_guards.py: fed canned `objdump -d -C` text, it must accept
an image whose guarded functions still compare |x| against FLT_MAX and refuse
each way the guard can go (folded away, function missing). Plain script, exit
code is the verdict. The real proof that it goes red on a folded guard is the
image build with SHELL_FAST_MATH=2 (recorded in the round's report)."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import math_guards as g

FAILS = []
def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond: FAILS.append(name)

GUARD = """\
24000000 <{name}>:
24000000:\tvabs.f32\ts14, s0
24000004:\tvldr\ts15, [pc, #8]\t; 24000010 <{name}+0x10>
24000008:\tvcmp.f32\ts14, s15
2400000c:\tbx\tlr
24000010:\t.word\t0x7f7fffff
"""
FOLDED = """\
24000000 <{name}>:
24000000:\tvmul.f32\ts0, s0, s0
24000004:\tbx\tlr
"""
NAMES = [n for n, _ in g.GUARDED]

def image(overrides=None, drop=()):
    parts = []
    for name in NAMES:
        if name in drop:
            continue
        tmpl = (overrides or {}).get(name, GUARD)
        parts.append(tmpl.format(name=name))
    return "\n".join(parts) + "\n"

check("every guarded function guarding: accepted", g.refusals(image()) == [])

for name, optional in g.GUARDED:
    folded = g.refusals(image({name: FOLDED}))
    check("guard folded in %s: refused" % name, any("folded" in r and name in r for r in folded))
    missing = g.refusals(image(drop=(name,)))
    if optional:
        check("optional %s absent: accepted" % name, missing == [])
    else:
        check("required %s absent: refused" % name, any("missing" in r and name in r for r in missing))

# The two marks are both needed: a vabs alone (or the literal alone) is not the guard.
VABS_ONLY = "24000000 <{name}>:\n24000000:\tvabs.f32\ts14, s0\n24000004:\tbx\tlr\n"
WORD_ONLY = "24000000 <{name}>:\n24000000:\tbx\tlr\n24000004:\t.word\t0x7f7fffff\n"
n0 = NAMES[0]
check("vabs without the literal: refused", g.refusals(image({n0: VABS_ONLY})) != [])
check("literal without vabs: refused", g.refusals(image({n0: WORD_ONLY})) != [])
# Another function's literal does not count for this one.
other = image({n0: FOLDED})
check("a neighbour's literal does not save a folded guard", g.refusals(other) != [])

sys.exit(1 if FAILS else 0)
