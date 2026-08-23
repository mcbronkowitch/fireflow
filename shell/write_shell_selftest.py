"""Schreibt den Selbsttest-Schalter als echten Header.

Warum nicht `-DSHELL_SELFTEST` auf der Compilerzeile: ein blankes -D ist fuer
Makes Abhaengigkeitsgraphen unsichtbar. SHELL_SELFTEST ist eine Variable, die
zwischen zwei Werten umgestellt wird, und ein bestehendes build/ wuerde beim
Umstellen ein veraltetes main.o weiterverwenden -- also entweder den
Selbsttest still verlieren oder ihn still in ein Image mitschleppen, dessen
CPU-Zahlen danach gemessen werden. Dieselbe Falle und dieselbe Loesung wie
bench/write_bench_transport.py und write_bench_board.py.

Nur schreiben, wenn sich der Inhalt aendert: dann sagt die mtime des Headers
aus, ob sich der WERT geaendert hat, und nicht bloss, ob make gelaufen ist.

UND DIE MTIME ALLEIN REICHT NICHT -- nachgewiesen am 2026-08-23 am Schalter
SHELL_MUX_PROBE: der Header wurde 0,36 s nach main.o geschrieben, also in
derselben Wanduhrsekunde, und Make hat "nicht neuer" geurteilt und das alte
Objekt behalten. Zwei Images mit verschiedenen Schalterstellungen waren
byte-identisch. Deshalb loescht dieses Skript die abhaengigen Objekte selbst,
sobald sich der Inhalt aendert; sie kommen als weitere Argumente herein.
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_selftest.py OUTPUT {0|1} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # Das Skript laeuft zur Parse-Zeit des Makefiles, also bevor irgendeine
    # Regel build/ angelegt hat.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = ""
    if sys.argv[2] == "1":
        content = "#define SHELL_SELFTEST 1\n"
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
