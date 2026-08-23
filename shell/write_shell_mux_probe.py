"""Schreibt den Mux-Sonden-Schalter als echten Header.

Gleiche Form und gleicher Grund wie write_shell_cpu_probe.py und
write_shell_selftest.py: ein blankes -D ist fuer Makes Abhaengigkeitsgraphen
unsichtbar, und SHELL_MUX_PROBE ist eine Variable, die zwischen DREI Werten
umgestellt wird. Ein bestehendes build/ wuerde beim Umstellen ein veraltetes
main.o weiterverwenden -- und im schlimmsten Fall eine Messung der falschen
Platzierung unter dem richtigen Namen ausliefern.

Unterschied zu den zwei anderen Schreibern: dieser definiert das Symbol
IMMER, auch in Stellung 0. `#if SHELL_MUX_PROBE == 1` braucht es in allen
drei Stellungen.

DIE ZEITSTEMPEL-KANTE REICHT NICHT, und das ist am 2026-08-23 auf diesem
Rechner passiert: der Header wurde 0,36 s NACH main.o geschrieben, also in
derselben Wanduhrsekunde, und Make hat "nicht neuer" geurteilt und nicht neu
uebersetzt. Die zwei Images zu den Stellungen 1 und 2 waren byte-identisch.
Deshalb loescht dieses Skript die abhaengigen Objekte selbst, sobald sich der
Inhalt aendert -- das haengt an keiner Zeitaufloesung. Die Objekte kommen als
weitere Argumente herein.
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1", "2"}:
        raise SystemExit(
            "usage: write_shell_mux_probe.py OUTPUT {0|1|2} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # Das Skript laeuft zur Parse-Zeit des Makefiles, also bevor irgendeine
    # Regel build/ angelegt hat.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_MUX_PROBE %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
