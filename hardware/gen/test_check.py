#!/usr/bin/env python3
"""The check tool can fail, and passes a clean fixture.

For every fast rule: the demo fixture passes it, its sabotage makes it fire,
and its ':empty' sabotage trips the examined-nothing guard. The sheet and
full levels then run on the demo through kicad-cli: clean passes, and the
'overlap', 'erc' and 'stability' sabotages each fail. Waiver bookkeeping is
checked directly. Needs kicad-cli; without it this fails, never skips.

    python hardware/gen/test_check.py
"""
import contextlib
import io
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..")))

from gen import check, ksexp  # noqa: E402

DEMO = os.path.join(HERE, "fixtures", "demo.py")


def run_cli(args, out_dir):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = check.main(["--project", DEMO, "--out", out_dir] + args)
    return rc, buf.getvalue()


def main():
    if not os.path.exists(ksexp.KICAD_CLI):
        print("FAIL kicad-cli not found at %s" % ksexp.KICAD_CLI)
        return 1
    failures = []

    def expect(args, rc_want, needle, out_dir):
        rc, text = run_cli(args, out_dir)
        ok = rc == rc_want and needle in text
        print("  %-44s rc=%d %s" % (" ".join(args), rc, "ok" if ok else "UNEXPECTED"))
        if not ok:
            failures.append("%s: rc=%d (wanted %d), %r %s in output:\n%s"
                            % (" ".join(args), rc, rc_want, needle,
                               "found" if needle in text else "missing", text))
        return text

    with tempfile.TemporaryDirectory() as out:
        expect(["--fast"], 0, "PASS fast", out)
        for rule in check.RULES:
            expect(["--fast", "--sabotage", rule], 1, "[%s]" % rule, out)
            expect(["--fast", "--sabotage", rule + ":empty"], 1,
                   "[%s]: examined nothing" % rule, out)
        expect(["--sheet", "logic"], 0, "PASS sheet logic", out)
        if not os.path.exists(os.path.join(out, "logic.png")):
            failures.append("the sheet level wrote no logic.png")
        expect(["--sheet", "logic", "--sabotage", "overlap"], 1, "[overlap]", out)
        expect(["--full"], 0, "PASS full", out)
        for name in ("demo-overview.png", "power.png", "logic.png", "leds.png",
                     "demo.pdf", "erc.json"):
            if not os.path.exists(os.path.join(out, name)):
                failures.append("the full level wrote no %s" % name)
        expect(["--full", "--sabotage", "erc"], 1, "[erc]", out)
        expect(["--full", "--sabotage", "stability"], 1, "[stability]", out)

    found = check.match_waivers(
        [("pin_to_pin", ["Symbol U1 Pin 9 [Q7, Output, Line]"])],
        [("pin_to_pin", "Symbol U1 Pin 9", "reason"),
         ("pin_not_driven", "Symbol U9", "stale")])
    if [f.rule for f in found] != ["erc_waiver"]:
        failures.append("match_waivers: wanted one stale-waiver finding, got %s"
                        % [str(f) for f in found])
    found = check.match_waivers([("pin_to_pin", ["Symbol U2 Pin 1"])], [])
    if [f.rule for f in found] != ["erc"]:
        failures.append("match_waivers: an unwaived violation went unreported")

    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: every rule passes the demo, fires on its sabotage and guards "
          "against an empty input; sheet and full levels pass and fail as planned")
    return 0


if __name__ == "__main__":
    sys.exit(main())
