"""Remote runner for the CPU hunt (spec 2026-10-29-cpu-hunt-remote-design.md
section 5.4). The board stays on USB; this drives it from the PC.

    remote_run.py flash QSPI.bin SRAM.bin [--expect-build STAMP]
                                                DFU pair, QSPI first (board must be in DFU)
    remote_run.py boot [--expect-build STAMP]   send BOOT, wait for the DFU device
    remote_run.py cmd "LOAD 3"                  send one line, print the replies for a moment
    remote_run.py run (--load 3 | --gest 1) [--expect-tog HEX] [--tog 2=1 ...] --seconds 60
                      --out DIR [--record]

`run` writes DIR/serial.txt (every line prefixed with the host time), DIR/summary.json
(from the SHELL_PLAY_LAW / SHELL_PLAY_WIN frame lines, see run_stats.py) and, with
--record, DIR/audio.wav plus DIR/audio.wav.start. The COM port is found by USB
VID/PID 0483:5740 unless --port is given. Ids index shell/generated_workloads.h;
a gesture loads its own base workload. summary.json counts only the frames the
board reports settled for this run (`counting=1`, with the gen of the run's
load / gest ack, or the gen a TOG re-settled it under), up to a gesture's
`gest done`, and says `void` when a pot took a parameter over during it and
`tripped` when the overrun guard tripped at some time in it. The guard re-arms
itself (after 1 s of silence, and at a run's settle end), so the run counts
across trips: `trips` is how often it tripped since the ack, `tripped_frames`
the counted frames it was tripped in, and `untripped` repeats the statistics
over the counted frames it was not tripped in. It also names the image
(`build`, `measure`) and the workload (`name`).
--seconds includes the settle (1 s, or 5 s with a SAMPLER deck).

The toggle word: the board keeps it across runs (LOAD does not reset it), so
`run` sets the word it wants itself, before the LOAD -- `--expect-tog` (default
00000000) with every `--tog BIT=V` applied on top. It reads the board's current
word from a run line, sends `TOG b v` for each bit that differs (clears too),
at most 7 unanswered at a time (the board's command ring holds 8), checks the
acked word, and then requires that word on every counted frame
(ToggleMismatch otherwise).

The flash rule: a failed flash step is retried as a whole pair, and while a
begun pair is unfinished (on record, remote_lib.PARTIAL_MARK) `boot` and
`cmd BOOT` refuse: a reset would run a mismatched pair's cold code. With
--expect-build, `flash` confirms `SHELL_REMOTE on build=STAMP` after the flash
and `boot` confirms it before sending BOOT (STAMP as the board prints it:
7 hex digits, `+` for a dirty tree).
"""
import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

import remote_lib as rl
import run_stats as rs


class TogError(RuntimeError):
    """The toggle word could not be read, set or confirmed before the run."""


# The board's command ring holds 8 (shell/remote_cmd.h, CmdRing::kCapacity);
# one slot stays free for anything else in flight.
TOG_PER_CHUNK = 7
_RUN_TOG = re.compile(r"SHELL_REMOTE run .*\btog=([0-9a-fA-F]{8})\b")
_TOG_ACK = re.compile(r"SHELL_REMOTE tog bit=(\d+) v=(\d) word=([0-9a-fA-F]{8})")
_ERRORS = (rl.FlashError, rl.BuildMismatch, rl.PortGone, TogError, OSError)


def _port(args) -> rl.Port:
    name = args.port or rl.find_port
    return rl.Port(name)


def _fail(e) -> int:
    print("error: %s: %s" % (type(e).__name__, e), file=sys.stderr)
    return 1


def _log(sink, stamp, text) -> None:
    if sink is not None:
        sink.write("%.3f %s\n" % (stamp, text))


def wanted_toggles(expect_tog, togs) -> int:
    """The word a run plays: expect_tog (hex, None = 0) with each "BIT=V"
    of togs applied on top."""
    want = int(expect_tog or "0", 16)
    for tog in togs or []:
        bit, val = (int(x) for x in tog.split("="))
        if not 0 <= bit <= 31 or val not in (0, 1):
            raise ValueError("--tog %s: bit 0..31, value 0 or 1" % tog)
        want = (want | (1 << bit)) if val else (want & ~(1 << bit))
    return want & 0xFFFFFFFF


def set_toggles(port, want: int, sink=None, wait_s: float = 3.0, clock=time.time) -> int:
    """Brings the board's toggle word to `want` and returns it: reads the
    current word from the next `SHELL_REMOTE run ... tog=` line (one per
    frame), sends `TOG b v` for every differing bit, at most TOG_PER_CHUNK
    before their acks are back, and checks the last ack's word. Every line
    read goes to `sink`. Raises TogError (no run line, a refused TOG -- the
    listener image has none -- a missing ack, or another word)."""
    cur = None
    deadline = clock() + wait_s
    for stamp, text in port.lines(until=lambda: clock() >= deadline):
        _log(sink, stamp, text)
        m = _RUN_TOG.search(text)
        if m:
            cur = int(m.group(1), 16)
            break
    if cur is None:
        raise TogError("no `SHELL_REMOTE run ... tog=` line in %.0f s to read the board's "
                       "toggle word from" % wait_s)
    bits = [b for b in range(32) if ((cur ^ want) >> b) & 1]
    word = cur
    for i in range(0, len(bits), TOG_PER_CHUNK):
        chunk = bits[i:i + TOG_PER_CHUNK]
        for b in chunk:
            port.send("TOG %d %d" % (b, (want >> b) & 1))
        waiting = set(chunk)
        deadline = clock() + wait_s
        for stamp, text in port.lines(until=lambda: clock() >= deadline):
            _log(sink, stamp, text)
            if "SHELL_REMOTE error cmd=TOG" in text:
                raise TogError("the board refused TOG (not a measurement image): %s"
                               % text.split("SHELL_REMOTE ", 1)[-1])
            m = _TOG_ACK.search(text)
            if m and int(m.group(1)) in waiting:
                waiting.discard(int(m.group(1)))
                word = int(m.group(3), 16)
                if not waiting:
                    break
        if waiting:
            raise TogError("no ack for TOG bit(s) %s in %.0f s"
                           % (",".join(str(b) for b in sorted(waiting)), wait_s))
    if word != want:
        raise TogError("the board's toggle word is %08x after the TOGs, wanted %08x"
                       % (word, want))
    return word


def _drain(port: rl.Port, seconds: float, echo: bool = True, sink=None) -> None:
    deadline = time.time() + seconds
    for stamp, text in port.lines(until=lambda: time.time() >= deadline):
        if sink is not None:
            sink.write("%.3f %s\n" % (stamp, text))
        if echo:
            print(text)


def cmd_flash(args, flash_fn=rl.flash_pair, port_factory=_port) -> int:
    """The pair (flash_pair() retries a failed step as a whole pair), then,
    with --expect-build, the board's own stamp: DFU readback cannot verify
    what landed, the image's boot print is the only proof."""
    try:
        flash_fn(Path(args.qspi), Path(args.sram))
        print("flashed pair: QSPI then SRAM:leave")
        if getattr(args, "expect_build", None):
            port = port_factory(args)
            try:
                build = rl.confirm_build(port, args.expect_build)
            finally:
                port.close()
            print("board runs build %s" % build)
    except _ERRORS as e:
        return _fail(e)
    return 0


def cmd_boot(args, port_factory=_port, wait_fn=rl.wait_for_dfu) -> int:
    """BOOT, then wait for the DFU device. Refused while a partial flash is on
    record; with --expect-build, only a board that runs that build is sent it."""
    try:
        rl.check_no_partial_flash()
        port = port_factory(args)
        try:
            if getattr(args, "expect_build", None):
                build = rl.confirm_build(port, args.expect_build)
                print("board runs build %s" % build)
            port.send("BOOT")
        finally:
            port.close()
        wait_fn(wait_s=args.wait)
    except _ERRORS as e:
        return _fail(e)
    print("board is in DFU")
    return 0


def cmd_cmd(args, port_factory=_port) -> int:
    try:
        if args.line.strip().upper() == "BOOT":
            rl.check_no_partial_flash()
        port = port_factory(args)
        port.send(args.line)
        _drain(port, args.listen)
    except _ERRORS as e:
        return _fail(e)
    return 0


def _finish_capture(capture, wav: Path, abort: bool) -> dict:
    """Make sure ffmpeg is gone and say how it went. On an aborted run it is
    terminated first; a normal run waits for its -t to expire. Whatever
    happens, it is killed rather than left running."""
    exit_code, note = None, None
    try:
        if abort:
            capture.terminate()
        exit_code = capture.wait(timeout=5 if abort else 30)
    except subprocess.TimeoutExpired:
        note = "ffmpeg did not finish in time, killed"
        capture.kill()
        try:
            capture.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass
    exists = wav.exists() and wav.stat().st_size > 0
    return {"requested": True, "exit": exit_code, "wav_exists": exists,
            "note": note, "aborted": abort,
            "ok": bool(exit_code == 0 and exists and note is None and not abort)}


def cmd_run(args, port_factory=_port, capture_fn=rl.start_capture,
            sleep=time.sleep) -> int:
    """Always leaves summary.json behind, built from whatever was read, with
    `error` (the exception that ended the run, or null) and `record` (the
    capture's outcome, or null without --record). Exit 1 on either failing,
    on a run that measured nothing (no ack, or no counted frame): its
    `error` then starts with EmptyRun; on a run whose counted frames had
    toggle bit 30 or 31 set (`error` from run_stats, ProbeBits) or another
    word than the wanted one (ToggleMismatch); and when the word could not be
    set before the LOAD (TogError, the LOAD is then never sent)."""
    want = wanted_toggles(getattr(args, "expect_tog", None), args.tog)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    wav = out / "audio.wav"
    port, capture = None, None
    error, record, completed = None, None, False
    try:
        with open(out / "serial.txt", "w", encoding="utf-8") as sink:
            try:
                port = port_factory(args)
                # The word first, before the LOAD: its switch-over then lies
                # inside the run's own settle.
                set_toggles(port, want, sink=sink)
                if args.record:
                    # A little margin: ffmpeg starts late, the serial loop ends on time.
                    capture = capture_fn(wav, rl.CAPTURE_DEVICE, args.seconds + 2.0)
                    sleep(1.0)
                # A gesture loads its own base workload (shell/panel_scan.cpp),
                # so GEST goes alone; a LOAD before it would only be replaced.
                if args.gest is not None:
                    port.send("GEST %d" % args.gest)
                else:
                    port.send("LOAD %d" % args.load)
                _drain(port, args.seconds, echo=False, sink=sink)
                completed = True
            except Exception as e:  # e.g. PortGone: keep what was read
                error = "%s: %s" % (type(e).__name__, e)
    finally:
        if port is not None:
            port.close()
        if capture is not None:
            record = _finish_capture(capture, wav, abort=not completed)
        with open(out / "serial.txt", encoding="utf-8") as fh:
            # Only the settled frames of this run (the board's `SHELL_REMOTE
            # run ... counting=1` lines with its ack's gen); `void` once a pot
            # took over, `tripped` / `trips` / `untripped` for the guard (the
            # count goes on across trips).
            if args.gest is not None:
                summary = rs.summarize_run(fh, gest_id=args.gest, expect_tog=want)
            else:
                summary = rs.summarize_run(fh, load_id=args.load, expect_tog=want)
        # A run that measured nothing is a failed run, not an empty
        # measurement: no ack of it (gen None -- never loaded, or another
        # image answered) or not one counted frame. Nor is one whose counted
        # frames ran with toggle bit 30 or 31 (run_stats' ProbeBits error).
        # An earlier error stays.
        if error is None and summary.get("error"):
            error = summary["error"]
        elif error is None and summary.get("gen") is None:
            error = "EmptyRun: no ack of the run (gen None)"
        elif error is None and summary.get("frames", 0) == 0:
            error = "EmptyRun: no counted frames (the run never settled in the capture)"
        summary["error"] = error
        summary["record"] = record
        (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n",
                                          encoding="utf-8")
    print(json.dumps(summary, indent=2))
    failed = error is not None or (record is not None and not record["ok"])
    return 1 if failed else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", help="COM port; default: found by VID/PID 0483:5740")
    sub = ap.add_subparsers(dest="verb", required=True)

    p = sub.add_parser("flash"); p.add_argument("qspi"); p.add_argument("sram")
    p.add_argument("--expect-build", metavar="STAMP",
                   help="confirm `SHELL_REMOTE on build=STAMP` after the flash")
    p.set_defaults(fn=cmd_flash)

    p = sub.add_parser("boot"); p.add_argument("--wait", type=float, default=30)
    p.add_argument("--expect-build", metavar="STAMP",
                   help="send BOOT only to a board that prints `SHELL_REMOTE on build=STAMP`")
    p.set_defaults(fn=cmd_boot)

    p = sub.add_parser("cmd"); p.add_argument("line")
    p.add_argument("--listen", type=float, default=1.5)
    p.set_defaults(fn=cmd_cmd)

    p = sub.add_parser("run")
    p.add_argument("--load", type=int, help="workload id (shell/generated_workloads.h)")
    p.add_argument("--gest", type=int, help="gesture id; loads its own base, --load not needed")
    p.add_argument("--expect-tog", metavar="HEX", default="00000000",
                   help="the toggle word the run plays (default 00000000); --tog applies on top")
    p.add_argument("--tog", action="append", metavar="BIT=V")
    p.add_argument("--seconds", type=float, required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--record", action="store_true")
    p.set_defaults(fn=cmd_run)

    args = ap.parse_args(argv)
    if args.verb == "run" and args.load is None and args.gest is None:
        ap.error("run needs --load ID or --gest ID")
    if args.verb == "run":
        try:
            wanted_toggles(args.expect_tog, args.tog)
        except ValueError as e:
            ap.error("toggle word: %s" % e)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
