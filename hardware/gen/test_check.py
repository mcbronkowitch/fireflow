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
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
        try:
            rc = check.main(["--project", DEMO, "--out", out_dir] + args)
        except SystemExit as e:          # argparse's error(): a refused sabotage
            rc = e.code if isinstance(e.code, int) else 1
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
        expect(["--fast", "--sabotage", "panel_orphan"], 1, "[panel_ids]", out)
        expect(["--sheet", "no_such_sheet"], 1, "[error]", out)
        expect(["--sheet", "logic"], 0, "PASS sheet logic", out)
        if not os.path.exists(os.path.join(out, "logic.png")):
            failures.append("the sheet level wrote no logic.png")
        expect(["--sheet", "logic", "--sabotage", "overlap"], 1, "[overlap]", out)
        expect(["--sheet", "logic", "--sabotage", "netlist"], 1, "[netlist]", out)
        expect(["--sheet", "logic", "--sabotage", "sheet_edge"], 1, "[sheet_edge]", out)
        expect(["--sheet", "logic", "--sabotage", "netlist:empty"], 1,
               "[netlist]: examined nothing", out)
        expect(["--sheet", "logic", "--sabotage", "overlap:empty"], 1,
               "[overlap]: examined nothing", out)
        # a level must refuse a sabotage it does not run, never report PASS
        expect(["--fast", "--sabotage", "overlap"], 2, "not honoured", out)
        expect(["--fast", "--sabotage", "netlist:empty"], 2, "not honoured", out)
        expect(["--sheet", "logic", "--sabotage", "stability"], 2, "not honoured", out)
        expect(["--sheet", "logic", "--sabotage", "erc"], 2, "not honoured", out)
        expect(["--fast", "--sabotage", "no_such_thing"], 2, "unknown sabotage", out)
        expect(["--full"], 0, "PASS full", out)
        expect(["--full", "--sabotage", "netlist"], 1, "[netlist]", out)
        expect(["--full", "--sabotage", "sheet_edge"], 1, "[sheet_edge]", out)
        for name in ("demo-overview.png", "power.png", "logic.png", "leds.png",
                     "demo.pdf", "erc.json"):
            if not os.path.exists(os.path.join(out, name)):
                failures.append("the full level wrote no %s" % name)
        expect(["--full", "--sabotage", "erc"], 1, "[erc]", out)
        expect(["--full", "--sabotage", "stability"], 1, "[stability]", out)

    item = "Symbol U1 Pin 9 [Q7, Output, Line]"
    found = check.match_waivers([("pin_to_pin", [item])],
                                [("pin_to_pin", item, 1, "reason"),
                                 ("pin_not_driven", "Symbol U9 Pin 1 [A, Input, Line]", 1, "stale")])
    if [f.rule for f in found] != ["erc_waiver"]:
        failures.append("match_waivers: wanted one stale-waiver finding, got %s"
                        % [str(f) for f in found])
    found = check.match_waivers([("pin_to_pin", ["Symbol U2 Pin 1"])], [])
    if [f.rule for f in found] != ["erc"]:
        failures.append("match_waivers: an unwaived violation went unreported")
    found = check.match_waivers([("pin_to_pin", [item]), ("pin_to_pin", [item])],
                                [("pin_to_pin", item, 1, "reason")])
    if [f.rule for f in found] != ["erc_waiver"]:
        failures.append("match_waivers: a waiver absorbed more violations than its count")
    found = check.match_waivers([("pin_to_pin", ["Symbol U1 Pin 90 [X, Output, Line]"])],
                                [("pin_to_pin", "Symbol U1 Pin 9", 1, "prefix only")])
    if sorted(f.rule for f in found) != ["erc", "erc_waiver"]:
        failures.append("match_waivers: a waiver matched by substring, not exactly")

    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: every rule passes the demo, fires on its sabotage and guards "
          "against an empty input; sheet and full levels pass and fail as planned")
    return 0


if __name__ == "__main__":
    sys.exit(main())
