"""Writes the shell's git stamp as a real header, at Makefile PARSE time.

Same shape and same reason as write_shell_settle_probe.py: a rule with FORCE
did not hold on this machine, twice, because make has one-second mtime
resolution and a header written 0.36 s after the object landed in the same
wall-clock second counts as "not newer". This script runs while the Makefile
is read, so when the value changes the dependent object is gone before make
builds its graph.

The stamp is deliberately BOUNDED IN LENGTH: seven hex digits plus at most
one '+' for a dirty tree. libDaisy's log buffer is 128 bytes
(lib/libDaisy/src/hid/logger.h:29) and SHELL_XTALK_CFG is already near it, so
`git describe`'s variable-length output -- which grows a tag name and a
commit count when a tag is in reach -- would silently truncate the line and
stamp it "$$".

THE BOUND IS IMPOSED, NOT ASSUMED, and the difference is the whole point of
this file. `git rev-parse --short=7` is documented as a MINIMUM: git lengthens
the abbreviation as far as uniqueness in the repository requires, so on a big
enough tree it returns eight or nine characters without warning or error. A
file whose job is to bound a length must not state the bound in a docstring
and then take git's word for it -- that is the same shape of mistake as the
bare -D these generators exist to eliminate. Hence the explicit [:7] slice
below; truncating an already-unique abbreviation is harmless here because the
stamp identifies a build, it is not used to look a commit up.

'+' rather than '-dirty' for the same three bytes of reason. A dirty stamp is
not a failure here: this probe is expected to run from a working tree that
carries uncommitted hardware work. It only has to be VISIBLE, so a capture is
never mistaken for one taken at a clean commit.
"""
import subprocess
import sys
from pathlib import Path


def stamp() -> str:
    try:
        head = subprocess.run(
            ["git", "rev-parse", "--short=7", "HEAD"],
            capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        # No git, or not a repository. Seven characters of honesty: a blank
        # field would read as a clean build at an unknown commit.
        return "nogit00"
    try:
        dirty = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        dirty = "?"
    # [:7] because --short=7 is a floor, not a ceiling. See the docstring.
    return head[:7] + ("+" if dirty else "")


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit("usage: write_git_hash.py OUTPUT [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    content = '#define SHELL_GIT_HASH "%s"\n' % stamp()
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[2:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
