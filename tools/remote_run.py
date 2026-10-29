"""Remote runner for the CPU hunt (spec 2026-10-29-cpu-hunt-remote-design.md
section 5.4). The board stays on USB; this drives it from the PC.

    remote_run.py flash QSPI.bin SRAM.bin       DFU pair, QSPI first (board must be in DFU)
    remote_run.py boot                          send BOOT, wait for the DFU device
    remote_run.py cmd "LOAD 3"                  send one line, print the replies for a moment
    remote_run.py run --load 3 [--gest 1] [--tog 2=1 ...] --seconds 60 --out DIR [--record]

`run` writes DIR/serial.txt (every line prefixed with the host time), DIR/summary.json
(from the SHELL_PLAY_LAW / SHELL_PLAY_WIN frame lines, see run_stats.py) and, with
--record, DIR/audio.wav plus DIR/audio.wav.start. The COM port is found by USB
VID/PID 0483:5740 unless --port is given. Workload ids are integers for now.
"""
import argparse
import json
import sys
import time
from pathlib import Path

import remote_lib as rl
import run_stats as rs


def _port(args) -> rl.Port:
    name = args.port or rl.find_port
    return rl.Port(name)


def _drain(port: rl.Port, seconds: float, echo: bool = True, sink=None) -> None:
    deadline = time.time() + seconds
    for stamp, text in port.lines(until=lambda: time.time() >= deadline):
        if sink is not None:
            sink.write("%.3f %s\n" % (stamp, text))
        if echo:
            print(text)


def cmd_flash(args) -> int:
    rl.flash_pair(Path(args.qspi), Path(args.sram))
    print("flashed pair: QSPI then SRAM:leave")
    return 0


def cmd_boot(args) -> int:
    port = _port(args)
    port.send("BOOT")
    port.close()
    rl.wait_for_dfu(wait_s=args.wait)
    print("board is in DFU")
    return 0


def cmd_cmd(args) -> int:
    port = _port(args)
    port.send(args.line)
    _drain(port, args.listen)
    return 0


def cmd_run(args) -> int:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    port = _port(args)
    capture = None
    if args.record:
        # A little margin: ffmpeg starts late and the serial loop ends on time.
        capture = rl.start_capture(out / "audio.wav", rl.CAPTURE_DEVICE,
                                   args.seconds + 2.0)
        time.sleep(1.0)
    with open(out / "serial.txt", "w", encoding="utf-8") as sink:
        port.send("LOAD %d" % args.load)
        if args.gest is not None:
            port.send("GEST %d" % args.gest)
        for tog in args.tog or []:
            bit, val = tog.split("=")
            port.send("TOG %d %d" % (int(bit), int(val)))
        _drain(port, args.seconds, echo=False, sink=sink)
    port.close()
    if capture is not None:
        capture.wait(timeout=30)
    with open(out / "serial.txt", encoding="utf-8") as fh:
        summary = rs.summarize_run(fh)
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n",
                                      encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", help="COM port; default: found by VID/PID 0483:5740")
    sub = ap.add_subparsers(dest="verb", required=True)

    p = sub.add_parser("flash"); p.add_argument("qspi"); p.add_argument("sram")
    p.set_defaults(fn=cmd_flash)

    p = sub.add_parser("boot"); p.add_argument("--wait", type=float, default=30)
    p.set_defaults(fn=cmd_boot)

    p = sub.add_parser("cmd"); p.add_argument("line")
    p.add_argument("--listen", type=float, default=1.5)
    p.set_defaults(fn=cmd_cmd)

    p = sub.add_parser("run")
    p.add_argument("--load", type=int, required=True, help="workload id (integer)")
    p.add_argument("--gest", type=int)
    p.add_argument("--tog", action="append", metavar="BIT=V")
    p.add_argument("--seconds", type=float, required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--record", action="store_true")
    p.set_defaults(fn=cmd_run)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
