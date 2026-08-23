"""Liest EINE SHELL_CPU-Zeile vom USB-CDC-Port des Boards.

Der Shell wiederholt sein Ergebnis alle 500 ms endlos und es gibt keinen
Handshake -- dieses Skript hoert also einfach zu, bis eine Zeile kommt.
Port vorher finden:

    python -c "from serial.tools import list_ports; \
print([p.device for p in list_ports.comports()])"

Aufruf:
    python read_probe.py COM7 [timeout_sekunden]
"""
import sys
import time

import serial


def main() -> int:
    if len(sys.argv) not in (2, 3):
        raise SystemExit("usage: read_probe.py PORT [timeout_seconds]")
    port = sys.argv[1]
    limit = float(sys.argv[2]) if len(sys.argv) == 3 else 30.0

    with serial.Serial(port, timeout=1.0) as ser:
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            line = ser.readline().decode("utf-8", "replace").strip()
            if line.startswith("SHELL_CPU"):
                print(line)
                return 0
    print("no SHELL_CPU line within %.0f s" % limit, file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
