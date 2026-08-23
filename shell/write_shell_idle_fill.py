"""Schreibt den Leerlauf-Fuellungs-Schalter als echten Header.

Gleiche Form und gleicher Grund wie die drei Geschwisterskripte: ein blankes
-D ist fuer Makes Abhaengigkeitsgraphen unsichtbar, und dieser Schalter wird
zwischen DREI Werten umgestellt.

Wozu er da ist: das Audio-Bild und das CPU-Sonden-Bild liegen im Pegel des
500-Hz-Artefakts 15,5 dB auseinander, obwohl in beiden dieselbe Engine mit
demselben Betriebspunkt laeuft und sich ihre Callbacks um zwei
Zykluszaehler-Lesungen unterscheiden. Was sich ausserdem unterscheidet, ist
die Leerlaufschleife: das Audio-Bild springt auf sich selbst und fasst keinen
Speicher an, das Sonden-Bild laedt bei jeder Iteration ein volatile. Die
Messung vom 8. August zeigt in dieselbe Richtung (Leerlauf mit nops fuellen
senkte den Ton um 7,5 dB). Dieser Schalter macht daraus eine Ablation im
SELBEN Bild, damit der Callback byte-gleich bleibt und nur der Leerlauf sich
aendert.

Das Symbol wird IMMER definiert -- `#if SHELL_IDLE_FILL == 1` braucht es in
allen drei Stellungen.

Die Zeitstempel-Kante allein reicht nicht (2026-08-23, siehe
write_shell_mux_probe.py), deshalb loescht dieses Skript die abhaengigen
Objekte selbst, sobald sich der Inhalt aendert.
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1", "2"}:
        raise SystemExit(
            "usage: write_shell_idle_fill.py OUTPUT {0|1|2} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_IDLE_FILL %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
