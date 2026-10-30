"""Writes the half-rate reverb's build state: the linker script the link reads.

usage: write_shell_reverb_half.py SOURCE_LD OUTPUT_LD STATE_FILE REVERB_HALF MEASURE BUILD_DIR
       REVERB_HALF {0|1}   SHELL_REVERB_HALF
       MEASURE     {0|1}   SHELL_REMOTE_MEASURE

The reverb tank is a template instance, Oliverb::ProcessImpl<Div>, and which
instances an image contains depends on the build (third_party/oliverb/
stmlib_shim.h):
  SHELL_REVERB_HALF=1      <2>          the half-rate tank, always (the default,
                                        every image, the measurement image too)
  SHELL_REVERB_HALF=0      <1>          the full-rate tank, nothing else
  ... and the measurement image <1> and <2>  toggle bit 9 picks at run time
itcm_placement.py refuses a name in the hot list that the image does not
contain (a stale name would silently undo the placement), so the list cannot
name all of them in every build. SOURCE_LD (shell/itcm_hot.ld) carries a marker
line in place of the tank's entries; this writes OUTPUT_LD = SOURCE_LD with the
marker line replaced by the entries for this build. The link and the placement
guard read OUTPUT_LD.

It runs while the Makefile is parsed, before any rule has created build/. The
other thing the state decides is which code every object holds (the member
layout of AmbientReverb changes with it), so when STATE_FILE changes this
deletes EVERY object of BUILD_DIR, as write_shell_remote_measure.py does; a
first write counts as a change.

A refusal (wrong arguments, a marker count other than one) deletes OUTPUT_LD
and STATE_FILE before it exits.
"""
import sys
from pathlib import Path

MARKER = "@REVERB_TANK@"
IMPL1 = "    *(.text._ZN6clouds7Oliverb11ProcessImplILi1EEEvPfS2_)              /* 2798 */\n"
IMPL2 = "    *(.text._ZN6clouds7Oliverb11ProcessImplILi2EEEvPfS2_)              /* 2850 */\n"


def run() -> int:
    if (len(sys.argv) != 7 or sys.argv[4] not in {"0", "1"}
            or sys.argv[5] not in {"0", "1"}):
        raise SystemExit(__doc__.strip().splitlines()[2])
    source, output, state = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    half, measure, build_dir = sys.argv[4] == "1", sys.argv[5] == "1", Path(sys.argv[6])
    entries = IMPL2 if half else (IMPL1 + IMPL2 if measure else IMPL1)
    text = source.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    out = []
    replaced = 0
    for line in lines:
        if MARKER in line:
            out.append(entries)
            replaced += 1
        else:
            out.append(line)
    if replaced != 1:
        raise SystemExit("%s must carry exactly one %s marker line (found %d)"
                         % (source, MARKER, replaced))
    content = "".join(out)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8", newline="")
    new_state = "reverb_half=%d measure=%d\n" % (half, measure)
    if not state.is_file() or state.read_text(encoding="utf-8") != new_state:
        for stale in build_dir.glob("*.o"):
            stale.unlink(missing_ok=True)
        state.write_text(new_state, encoding="utf-8")
    return 0


def main() -> int:
    try:
        return run()
    except SystemExit:
        # A refused run must not leave the previous build's linker script
        # (and state) in place: the Makefile ignores this script's exit status,
        # and the link and the placement guard would read the stale file and
        # pass. With the file gone the link fails on the missing script.
        if len(sys.argv) > 3:
            for stale in (sys.argv[2], sys.argv[3]):
                Path(stale).unlink(missing_ok=True)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
