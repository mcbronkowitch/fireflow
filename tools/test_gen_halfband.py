"""Host test for gen_halfband.py: the committed coefficients in
engine/fx/halfband.h are exactly what the generator produces (bit for bit in
float32), and the check goes red on a changed constant, a changed design
parameter or a missing table. Plain script, exit code is the verdict."""
import os, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import gen_halfband as g

FAILS = []
def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond: FAILS.append(name)

def cli(*args):
    r = subprocess.run([sys.executable, os.path.join(here, "gen_halfband.py")] + list(args),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return r.returncode, r.stdout

text = open(g.HEADER, encoding="utf-8").read()
c0, hh = g.header_constants()
check("header parsed: kC0 and 12 kHH values", len(hh) == 12 and abs(c0 - 0.5) < 1e-3)
check("committed coefficients == generator, bit for bit", g.check() == [])
rc, out = cli("--check")
check("CLI --check exits 0 on the committed header", rc == 0)

def header_with(old, new):
    assert text.count(old) == 1, old
    path = os.path.join(tmp, "halfband.h")
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text.replace(old, new))
    return path

with tempfile.TemporaryDirectory() as tmp:
    # one ulp-sized change in a float32 literal is still a bit difference
    p = header_with("0.316679666f", "0.316679693f")
    check("a one-ulp change in kHH[0] is caught", any("kHH[0]" in d for d in g.check(p)))
    rc, out = cli("--check", p)
    check("CLI --check exits 1 on it", rc == 1 and "kHH[0]" in out)
    p = header_with("0.500020805f", "0.5f")
    check("a changed kC0 is caught", any("kC0" in d for d in g.check(p)))
    p = header_with("-0.00020584901f", "-0.00020584901f, 0.0001f")
    rc, out = cli("--check", p)
    check("an extra tap (13 values for kHH[12]) is refused", rc != 0 and "kHH has 13 values" in out)

# A different design parameter moves the coefficients (the check is not vacuous).
c0_b7, hh_b7 = g.coefficients(beta=7.0)
check("beta 7 gives different coefficients", g.f32_bits(hh_b7[0]) != g.f32_bits(hh[0]))

# The response figures quoted in the header comment.
rep = g.report()
check("report: 12 kHz is -6.02 dB", "-6.02 dB" in rep)
check("report: stopband <= -62 dB", "<= -62" in rep)
check("report: ripple +0.007 / -0.006", "+0.007 / -0.006" in rep)

sys.exit(1 if FAILS else 0)
