#!/usr/bin/env python3
"""Generates shell/generated_boot_patch_noon.h: the all-noon patch of fault R4,
as the Rev A pots set it, so the firmware can boot straight into it
(C_USER_FLAGS=-DSHELL_BOOT_PATCH=3) instead of someone turning 73 pots.

The record: docs/hardware/reva-bring-up.md 10.1 and fault R4. With every pot
at noon the callback MEAN sits at the 945 000-cycle deadline (CBP_WIN windows
43-49 of the profile capture). Splitting that load knob by knob on the board
costs a hand and a two-second window per pot; booting the same knob vector
costs a flash.

Input, read, never written:
  docs/hardware/captures/reva-play-2026-10-24-r4-profile.txt
      the R4 profile capture. The frame taken is the last complete SHELL_PLAY
      frame (eight SHELL_PLAY_V lines, one SHELL_PLAY line) printed before the
      line `CBP_WIN w=WINDOW`, WINDOW below.

What a value is: SHELL_PLAY_V prints x1000 of the last EMITTED normalised pot
value, in kRevaControls row order (shell/read_play.py). pot_row_step() writes
exactly that value through knob_from_pot() into the knob the row targets
(knob_target(): ATTACK or STAGES by the deck's ENGINE), so applying the rows in
order through the same two functions reproduces the knob vector the board
played. The firmware only writes a knob once its pot has moved past kPotTouch
since boot; by window 43 every pot had been turned, so every row's value had
reached its knob.

    python shell/gen_boot_patch_noon.py           write the header
    python shell/gen_boot_patch_noon.py --check   exit 1 if the committed header is stale
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
CAPTURE_REL = "docs/hardware/captures/reva-play-2026-10-24-r4-profile.txt"
CAPTURE = os.path.join(ROOT, *CAPTURE_REL.split("/"))
OUT = os.path.join(HERE, "generated_boot_patch_noon.h")

# The first all-noon window of the profile (reva-bring-up.md 10.1: "All pots
# touched at noon (windows 43-49)").
WINDOW = 43
ROWS = 73                 # kRevaTable.count; the header's static_assert holds it
ROW_LINES = (0, 10, 20, 30, 40, 50, 60, 70)
MOD_KEY = 2               # presses=a,b,c,d: index of MODBTN (kRevaKeyMap)

_V_LINE = re.compile(r"^SHELL_PLAY_V r=(\d+)((?: -?\d+)+)\s*$")
_P_LINE = re.compile(r"^SHELL_PLAY .*\bkeys=(\d+) presses=([\d,]+)\s*$")


class GenError(Exception):
    pass


def load_inputs():
    with open(CAPTURE, encoding="utf-8", errors="replace") as f:
        return {"lines": f.read().splitlines(), "window": WINDOW}


def _frame_before(lines, stop):
    """The last complete frame whose SHELL_PLAY line sits before index `stop`:
    (values[73], keys, presses). Walks backwards over SHELL_PLAY lines and
    takes the first one whose eight V lines are all present above it."""
    for i in range(stop - 1, -1, -1):
        m = _P_LINE.match(lines[i])
        if not m:
            continue
        rows = {}
        j = i - 1
        # The V lines come right before the SHELL_PLAY line; CBP_* lines
        # printed by the foreground loop may sit between them.
        while j >= 0 and len(rows) < len(ROW_LINES):
            v = _V_LINE.match(lines[j])
            if v:
                r0 = int(v.group(1))
                if r0 in rows:
                    break                      # the previous frame's line
                rows[r0] = [int(x) for x in v.group(2).split()]
            elif lines[j].startswith("SHELL_PLAY"):
                break                          # a different frame's tail
            j -= 1
        if sorted(rows) != list(ROW_LINES):
            continue
        values = []
        for r0 in ROW_LINES:
            want = min(10, ROWS - r0)
            if len(rows[r0]) != want:
                raise GenError("frame before line %d: SHELL_PLAY_V r=%d has %d values, expected %d"
                               % (stop + 1, r0, len(rows[r0]), want))
            values.extend(rows[r0])
        return values, int(m.group(1)), [int(x) for x in m.group(2).split(",")]
    raise GenError("no complete SHELL_PLAY frame before line %d" % (stop + 1))


def build(lines, window):
    marker = "CBP_WIN w=%d " % window
    stop = next((i for i, l in enumerate(lines) if l.startswith(marker)), None)
    if stop is None:
        raise GenError("no %r line in the capture" % marker.strip())
    values, keys, presses = _frame_before(lines, stop)
    if len(values) != ROWS:
        raise GenError("%d rows, expected %d" % (len(values), ROWS))
    for r, v in enumerate(values):
        if v == -1000:
            raise GenError("row %d never emitted (-1000): its knob never left the init patch" % r)
        if not 0 <= v <= 1000:
            raise GenError("row %d value %d outside 0..1000" % (r, v))
    if keys != 0:
        raise GenError("a key is held in the frame (keys=%d)" % keys)
    if len(presses) <= MOD_KEY or presses[MOD_KEY] % 2 != 0:
        raise GenError("MOD latched in the frame (presses=%s): the pots would write depths, not knobs"
                       % ",".join(str(p) for p in presses))

    out = []
    out.append("// GENERATED by shell/gen_boot_patch_noon.py -- do not edit by hand.")
    out.append("// Source: %s," % CAPTURE_REL)
    out.append("// the last complete SHELL_PLAY frame before `CBP_WIN w=%d` (the first all-noon" % window)
    out.append("// window of fault R4, docs/hardware/reva-bring-up.md 10.1).")
    out.append("//")
    out.append("// x1000 of the emitted normalised pot value, one per row of kRevaControls")
    out.append("// (generated_panel_map.h), in row order. apply_boot_patch_rows() (boot_patch.h)")
    out.append("// turns them into the knob vector the board played, through the same")
    out.append("// knob_target() / knob_from_pot() the live scan uses.")
    out.append("#pragma once")
    out.append("")
    out.append("namespace shell {")
    out.append("")
    out.append("inline constexpr int kBootPatchNoonRowCount = %d;" % ROWS)
    out.append("inline constexpr int kBootPatchNoonRows[kBootPatchNoonRowCount] = {")
    for r0 in ROW_LINES:
        chunk = values[r0:r0 + 10]
        out.append("    %s,  // rows %d-%d" % (", ".join("%4d" % v for v in chunk), r0, r0 + len(chunk) - 1))
    out.append("};")
    out.append("")
    out.append("} // namespace shell")
    return "\n".join(out) + "\n"


def read_committed():
    with open(OUT, encoding="utf-8") as f:
        return f.read()


def check_text(committed, generated):
    return committed.replace("\r\n", "\n") == generated


def main(argv):
    try:
        text = build(**load_inputs())
    except GenError as e:
        print("gen_boot_patch_noon: %s" % e, file=sys.stderr)
        return 2
    if "--check" in argv:
        if os.path.exists(OUT) and check_text(read_committed(), text):
            print("shell/generated_boot_patch_noon.h is current")
            return 0
        print("shell/generated_boot_patch_noon.h is STALE: run python shell/gen_boot_patch_noon.py",
              file=sys.stderr)
        return 1
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote shell/generated_boot_patch_noon.h")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
