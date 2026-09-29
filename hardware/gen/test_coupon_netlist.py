#!/usr/bin/env python3
"""The coupon's intent still equals its committed, KiCad-exported netlist.

Proof for the move of the shared tools into hardware/gen/ (P3 spec §3.1): if
ksexp, Part or nets_from changed behaviour in the move, a node moves here. The
committed hardware/coupon/fab/coupon.net was exported by KiCad from the
coupon's schematic, so it is an independent read of the same intent.

    python hardware/gen/test_coupon_netlist.py      # exit code is the verdict
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HW)
sys.path.insert(0, os.path.join(HW, "coupon", "scripts"))

from gen import check                      # noqa: E402
import netlist as coupon_netlist           # noqa: E402  (the coupon's own)

COMMITTED = os.path.join(HW, "coupon", "fab", "coupon.net")


def main():
    intended = {k: set(v) for k, v in coupon_netlist.nets_from(
        coupon_netlist.build(), include_virtual=False).items()}
    exported = check.parse_exported_netlist(COMMITTED)
    failures = []
    if len(intended) < 60:
        failures.append("only %d intended nets -- the coupon has 66" % len(intended))
    bad = check.compare(intended, exported)
    failures += bad
    # The comparison must be able to fail: a planted node has to be reported.
    victim = sorted(intended)[0]
    planted = dict(intended)
    planted[victim] = set(intended[victim]) | {("X_PLANTED", "1")}
    if not check.compare(planted, exported):
        failures.append("compare() missed a planted node on net %s" % victim)
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: %d coupon nets match the committed coupon.net node for node"
          % len(intended))
    return 0


if __name__ == "__main__":
    sys.exit(main())
