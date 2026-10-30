"""Generator for the halfband coefficients in engine/fx/halfband.h.

The half-rate reverb runs its tank at 24 kHz between two identical halfband
converters (HalfbandDown / HalfbandUp). The filter is one linear-phase halfband
lowpass: 47 taps (L = 23), Kaiser window with beta 6, DC gain normalised to
exactly 1. Taps h[n], n = -23..23: h[0] = 0.5, h[even != 0] = 0,
h[odd n] = sin(pi n / 2) / (pi n) * kaiser(n), all divided by the sum of the
taps. The header stores kC0 = h[0] and kHH[i] = h[2 i + 1] as float32.

Pure Python, no dependencies, all arithmetic in double.

    python tools/gen_halfband.py            print the constants block for halfband.h
    python tools/gen_halfband.py --report   print the response figures quoted in halfband.h
    python tools/gen_halfband.py --check    compare against engine/fx/halfband.h, bit for
                                            bit in float32; exit 1 on any difference

To change the transition band, change L (odd; the header's loops and the
delay constant assume 23) and BETA here, regenerate, paste the block into the
header and adjust the loops. tools/test_gen_halfband.py holds --check in ctest.
"""
import math
import os
import re
import struct
import sys

L = 23
BETA = 6.0
HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "engine", "fx", "halfband.h")


def i0(x):
    """Modified Bessel function of the first kind, order 0 (power series)."""
    s, t, k = 1.0, 1.0, 1
    while True:
        t *= (x / (2 * k)) ** 2
        s += t
        if t < 1e-14 * s:
            return s
        k += 1


def design(length=L, beta=BETA):
    """{n: h[n]} for n = -length..length, normalised to a tap sum of 1."""
    h = {}
    for n in range(-length, length + 1):
        w = i0(beta * math.sqrt(max(0.0, 1.0 - (n / length) ** 2))) / i0(beta)
        if n == 0:
            h[n] = 0.5
        elif n % 2 == 0:
            h[n] = 0.0
        else:
            h[n] = math.sin(math.pi * n / 2) / (math.pi * n) * w
    total = sum(h.values())
    return {n: v / total for n, v in h.items()}


def f32(x):
    """x rounded to float32, as a Python float."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def f32_bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def coefficients(length=L, beta=BETA):
    """(kC0, [kHH...]) as doubles, before the float32 rounding."""
    h = design(length, beta)
    return h[0], [h[d] for d in range(1, length + 1, 2)]


def response_db(h, f_hz, fs=48000.0):
    w = 2 * math.pi * f_hz / fs
    re = sum(v * math.cos(w * n) for n, v in h.items())
    return 20 * math.log10(max(abs(re), 1e-12))


def block():
    c0, hh = coefficients()
    lines = ["constexpr float kC0 = %sf;" % ("%.9g" % c0), "constexpr float kHH[%d] = {" % len(hh)]
    lines += ["    %sf," % ("%.9g" % v) for v in hh]
    lines.append("};")
    return "\n".join(lines)


def report():
    h = design()
    passband = [response_db(h, f) for f in range(0, 10001, 50)]
    stop = [response_db(h, f) for f in range(14000, 24001, 50)]
    return ("passband 0..10 kHz   ripple %+.3f / %+.3f dB\n"
            "12 kHz               %.2f dB\n"
            "stopband 14..24 kHz  <= %.1f dB"
            % (max(passband), min(passband), response_db(h, 12000), max(stop)))


def header_constants(path=HEADER):
    """(kC0, [kHH...]) parsed from the header's float literals."""
    text = open(path, encoding="utf-8").read()
    c0 = re.search(r"constexpr float kC0\s*=\s*([-+0-9.eE]+)f;", text)
    hh = re.search(r"constexpr float kHH\[(\d+)\]\s*=\s*\{([^}]*)\}", text)
    if not c0 or not hh:
        raise SystemExit("halfband.h: kC0 / kHH not found")
    vals = [float(v) for v in re.findall(r"[-+]?\d[0-9.eE+-]*(?=f?\s*[,}]|f?\s*$)",
                                          hh.group(2).replace("f", ""))]
    if len(vals) != int(hh.group(1)):
        raise SystemExit("halfband.h: kHH has %d values, declared %s" % (len(vals), hh.group(1)))
    return float(c0.group(1)), vals


def check(path=HEADER):
    """List of differences between the generator and the header (empty = identical)."""
    c0, hh = coefficients()
    hc0, hhh = header_constants(path)
    bad = []
    if len(hh) != len(hhh):
        bad.append("tap count: generator %d, header %d" % (len(hh), len(hhh)))
    if f32_bits(hc0) != f32_bits(c0):
        bad.append("kC0: header %.9g (0x%08x), generator %.9g (0x%08x)"
                   % (hc0, f32_bits(hc0), f32(c0), f32_bits(c0)))
    for i, (a, b) in enumerate(zip(hhh, hh)):
        if f32_bits(a) != f32_bits(b):
            bad.append("kHH[%d]: header %.9g (0x%08x), generator %.9g (0x%08x)"
                       % (i, a, f32_bits(a), f32(b), f32_bits(b)))
    return bad


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "--check":
        diffs = check(sys.argv[2] if len(sys.argv) > 2 else HEADER)
        for d in diffs:
            print("halfband mismatch: " + d)
        if not diffs:
            print("halfband.h matches the generator bit for bit (float32)")
        sys.exit(1 if diffs else 0)
    elif mode == "--report":
        print(report())
    elif mode == "":
        print(block())
    else:
        print(__doc__.strip().splitlines()[0], file=sys.stderr)
        sys.exit(2)
