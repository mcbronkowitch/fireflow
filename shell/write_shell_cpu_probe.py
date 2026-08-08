"""Schreibt den CPU-Sonden-Schalter als echten Header.

Gleiche Form und gleicher Grund wie write_shell_selftest.py und wie die vier
Schalter-Skripte in bench/: ein blankes -D ist fuer Makes
Abhaengigkeitsgraphen unsichtbar, und SHELL_CPU_PROBE ist eine Variable, die
zwischen zwei Werten umgestellt wird. Ein bestehendes build/ wuerde beim
Umstellen ein veraltetes main.o weiterverwenden -- und im schlimmsten Fall
eine Messung ausliefern, die die Sonde selbst noch mitmisst.
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit("usage: write_shell_cpu_probe.py OUTPUT {0|1}")
    output = Path(sys.argv[1])
    content = ""
    if sys.argv[2] == "1":
        content = "#define SHELL_CPU_PROBE 1\n"
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
