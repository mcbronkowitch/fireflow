"""Schreibt den CPU-Sonden-Schalter als echten Header.

Gleiche Form und gleicher Grund wie write_shell_selftest.py und wie die vier
Schalter-Skripte in bench/: ein blankes -D ist fuer Makes
Abhaengigkeitsgraphen unsichtbar, und SHELL_CPU_PROBE ist eine Variable, die
zwischen zwei Werten umgestellt wird. Ein bestehendes build/ wuerde beim
Umstellen ein veraltetes main.o weiterverwenden -- und im schlimmsten Fall
eine Messung ausliefern, die die Sonde selbst noch mitmisst.

DIE ZEITSTEMPEL-KANTE REICHT DAFUER NICHT -- nachgewiesen am 2026-08-23 am
Schwesterschalter SHELL_MUX_PROBE: der Header wurde 0,36 s nach main.o
geschrieben, also in derselben Wanduhrsekunde, und Make hat nicht neu
uebersetzt. Genau der Fall, vor dem der Kommentar oben warnt, nur eine Ebene
tiefer. Deshalb loescht dieses Skript die abhaengigen Objekte selbst, sobald
sich der Inhalt aendert; sie kommen als weitere Argumente herein.
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_cpu_probe.py OUTPUT {0|1} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # Das Skript laeuft zur Parse-Zeit des Makefiles, also bevor irgendeine
    # Regel build/ angelegt hat.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = ""
    if sys.argv[2] == "1":
        content = "#define SHELL_CPU_PROBE 1\n"
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
