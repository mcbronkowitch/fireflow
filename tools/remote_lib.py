"""Host side of the remote CPU hunt (spec 2026-10-29-cpu-hunt-remote-design.md
section 5.4): a serial port that survives the COM port vanishing, DFU pair
flashing, and the ffmpeg capture of the audio interface.

Nothing here is board-specific beyond the USB ids and the DFU addresses; every
external effect (opening a port, running dfu-util, starting ffmpeg, sleeping,
reading the clock) is injectable so the guard test runs without hardware.
"""
import glob
import math
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Callable, Iterator, Optional, Union

# pyserial is optional for everything except talking to a real port: the guard
# test runs without it (repo convention, see bench/run.py list_ports). The
# fallback classes are OSErrors, which Port catches anyway.
try:
    from serial import SerialException, SerialTimeoutException
except ImportError:
    class SerialException(OSError):
        """Stand-in when pyserial is not installed."""

    class SerialTimeoutException(SerialException):
        """Stand-in when pyserial is not installed."""


def _open_serial(name, baud, timeout=None, write_timeout=None):
    import serial  # late: only a real port needs it
    return serial.Serial(name, baud, timeout=timeout, write_timeout=write_timeout)


# A write that blocks this long is a port that is gone: with the callback back
# to back the board's USB interrupt never runs, the CDC endpoint never takes
# the data, and without a write timeout send() would block for ever.
WRITE_TIMEOUT_S = 1.0


# libDaisy's application CDC (lib/libDaisy/src/usbd/usbd_desc.c: VID 1155,
# PID 22336) and the ROM/Daisy bootloader's DFU device.
CDC_VID, CDC_PID = 0x0483, 0x5740
DFU_ID = "0483:df11"

QSPI_ADDR = "0x90100000"
SRAM_ADDR = "0x90040000"

# memory fireflow-audio-measurement-rig
CAPTURE_DEVICE = "Line (Universal Audio Twin USB)"
_FFMPEG_GLOB = "~/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg*/**/ffmpeg.exe"


class PortGone(RuntimeError):
    """The port did not come back within the allowed time."""


class FlashError(RuntimeError):
    """A dfu-util step failed or the DFU device never appeared."""


class PartialFlash(FlashError):
    """A pair whose write began and never completed: the board may hold one
    image of one build and one of another (or a half-erased one)."""


class BuildMismatch(RuntimeError):
    """The board's `SHELL_REMOTE on build=` stamp is not the expected one, or
    it never printed one."""


# Where a begun, uncompleted pair is on record (final review I4), across
# runner invocations: written once the first dfu-util write starts, removed
# only when both steps of a pair succeed. The tests point it elsewhere.
PARTIAL_MARK = Path(tempfile.gettempdir()) / "fireflow-remote-partial-flash.txt"


def check_no_partial_flash(mark: Optional[Path] = None) -> None:
    """Raises PartialFlash while a partial pair is on record. Called before
    anything that resets the board (BOOT): a reset with a mismatched pair
    runs the other build's cold QSPI code (board_init()) before the ITCM
    stamp check can send it to the bootloader, and nothing promises that
    code faults (spec section 8)."""
    mark = PARTIAL_MARK if mark is None else mark
    if mark.exists():
        raise PartialFlash(
            "a partial flash is on record (%s): %s -- do not boot or reset the board; "
            "run `remote_run.py flash QSPI SRAM` again with the pair (a board still in "
            "DFU takes it). If the board has left DFU, it needs BOOT + RESET by hand."
            % (mark, mark.read_text(encoding="utf-8").strip()))


def find_port(lister: Optional[Callable] = None) -> Optional[str]:
    """The board's CDC port, by USB VID/PID (0483:5740) -- never a hard-coded
    COM number, which changes when the board re-enumerates. The DFU device and
    an ST-Link's virtual COM port (also VID 0483) are not matched."""
    if lister is None:
        from serial.tools import list_ports
        lister = list_ports.comports
    for p in lister():
        if p.vid == CDC_VID and p.pid == CDC_PID:
            return p.device
    return None


class Port:
    """Serial line with host timestamps and reconnection.

    `name` is a port name or a callable returning one (or None while the port
    is absent), so a COM number that changes on re-enumeration is looked up
    again at every reopen. `lines()` yields `(host_time, text)`; after a
    `SerialException` / `OSError` (the handle dies with the port) it polls for
    the port every `retry_s` up to `give_up_s`, reopens, and yields a
    `#reconnect` line. Stamps are clamped monotonic.
    """

    def __init__(self, name: Union[str, Callable[[], Optional[str]]], opener=None,
                 baud: int = 115200, retry_s: float = 0.5, give_up_s: float = 60.0,
                 sleep=time.sleep, clock=time.time):
        self._name = name
        self._opener = opener or _open_serial
        self._baud = baud
        self._retry_s = retry_s
        self._max_tries = max(1, math.ceil(give_up_s / retry_s))
        self._sleep = sleep
        self._clock = clock
        self._ser = None
        self._buf = bytearray()
        self._last = float("-inf")

    def _stamp(self) -> float:
        self._last = max(self._last, self._clock())
        return self._last

    def _resolve(self) -> Optional[str]:
        return self._name() if callable(self._name) else self._name

    def _open(self) -> None:
        """One attempt; leaves self._ser set on success, raises otherwise."""
        name = self._resolve()
        if not name:
            raise SerialException("port not present")
        self._ser = self._opener(name, self._baud, timeout=0.2,
                                 write_timeout=WRITE_TIMEOUT_S)

    def _close(self) -> None:
        ser, self._ser = self._ser, None
        self._buf.clear()   # the torn half of a dead handle's line is dropped
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass

    def _reopen(self) -> float:
        """Poll until the port opens; returns seconds spent waiting."""
        self._close()
        waited = 0.0
        for _ in range(self._max_tries):
            try:
                self._open()
                return waited
            except (SerialException, OSError):
                self._sleep(self._retry_s)
                waited += self._retry_s
        raise PortGone("serial port did not come back in %.0f s"
                       % (self._max_tries * self._retry_s))

    def _ensure(self) -> None:
        if self._ser is None:
            self._reopen()

    def send(self, line: str) -> None:
        """A write that fails or times out (SerialTimeoutException is a
        SerialException) is a vanished port: reopen and write once more. The
        retry goes behind a bare newline, which ends whatever part of the
        line the failed write may have delivered (the board drops an empty or
        unparsable line), so it cannot glue itself to the retry. A second
        failure raises PortGone."""
        data = (line.rstrip("\r\n") + "\n").encode("ascii")
        self._ensure()
        try:
            self._ser.write(data)
        except (SerialException, OSError):
            self._reopen()
            try:
                self._ser.write(b"\n" + data)
            except (SerialException, OSError) as e:
                self._close()
                raise PortGone("write failed twice (%s: %s)" % (type(e).__name__, e))

    def lines(self, until: Optional[Callable[[], bool]] = None) -> Iterator[tuple]:
        """Yield `(host_time, text)` until `until()` is true (checked before
        every read; the port's 0.2 s read timeout bounds the lag)."""
        self._ensure()
        while until is None or not until():
            try:
                raw = self._ser.readline()
            except (SerialException, OSError):
                waited = self._reopen()
                yield self._stamp(), "#reconnect gap>=%.1fs" % waited
                continue
            if not raw:
                continue
            # A read timeout can return half a line; emit only complete ones,
            # or a torn "cb_last=9..." becomes a fake tiny sample.
            self._buf += raw
            if not self._buf.endswith(b"\n"):
                continue
            text = bytes(self._buf).decode("utf-8", "replace").rstrip("\r\n")
            self._buf.clear()
            yield self._stamp(), text

    def close(self) -> None:
        self._close()


def dfu_present(run=subprocess.run) -> bool:
    listing = run(["dfu-util", "-l"], capture_output=True, text=True)
    return DFU_ID in (getattr(listing, "stdout", "") or "")


def wait_for_dfu(run=subprocess.run, wait_s: float = 30, sleep=time.sleep,
                 poll_s: float = 0.5) -> None:
    tries = max(1, math.ceil(wait_s / poll_s))
    for _ in range(tries):
        if dfu_present(run):
            return
        sleep(poll_s)
    raise FlashError("no DFU device %s after %.0f s" % (DFU_ID, wait_s))


def flash_pair(qspi: Path, sram: Path, run=subprocess.run, wait_s=30,
               sleep=time.sleep, retries: int = 2, mark: Optional[Path] = None) -> None:
    """QSPI first, then SRAM with :leave (CLAUDE.md: the pair, always in this
    order). Waits for the Daisy DFU device first; a failed QSPI step stops
    before SRAM is touched.

    The runner rule for a failed step (final review I4): the pair is written
    again, whole and QSPI first, up to `retries` more times while the DFU
    device is there -- never a reset or BOOT in between. From the first write
    on, the begun pair is on record (PARTIAL_MARK, check_no_partial_flash());
    only a pair whose two steps both succeed takes it off. A pair that never
    completes raises PartialFlash and stays on record. No DFU device at the
    start raises FlashError with nothing written and nothing recorded."""
    mark = PARTIAL_MARK if mark is None else mark
    steps = (("QSPI", qspi, QSPI_ADDR), ("SRAM", sram, SRAM_ADDR + ":leave"))
    failed = None
    for attempt in range(1 + max(0, retries)):
        try:
            wait_for_dfu(run, wait_s, sleep)
        except FlashError as e:
            if attempt == 0:
                raise   # nothing written yet
            raise PartialFlash(
                "dfu-util %s step failed (exit %d) and the DFU device did not come back for "
                "the retry (%s): the board holds a partial pair. Do not boot or reset it; "
                "it needs BOOT + RESET by hand, then `flash` (on record in %s)."
                % (failed[0], failed[1], e, mark))
        if attempt == 0:
            mark.write_text("pair %s + %s begun %s\n"
                            % (qspi, sram, time.strftime("%Y-%m-%d %H:%M:%S")),
                            encoding="utf-8")
        failed = None
        for label, image, addr in steps:
            r = run(["dfu-util", "-a", "0", "-s", addr, "-D", str(image),
                     "-d", "," + DFU_ID])
            if r.returncode != 0:
                failed = (label, r.returncode)
                break
        if failed is None:
            mark.unlink()
            return
    raise PartialFlash(
        "dfu-util %s step failed (exit %d) on all %d attempts: the board holds a partial "
        "pair. Do not boot or reset it; run `flash` again with the pair while it is in "
        "DFU (on record in %s)." % (failed[0], failed[1], 1 + max(0, retries), mark))


def confirm_build(port, expect: str, wait_s: float = 20.0, clock=time.time) -> str:
    """Reads the port until the board's `SHELL_REMOTE on build=<stamp>` line
    (printed as the log opens and every 5 s) and returns the stamp when it is
    `expect` -- exactly, `+` for a dirty tree included. Another stamp, or no
    such line within wait_s, raises BuildMismatch."""
    deadline = clock() + wait_s
    for _, text in port.lines(until=lambda: clock() >= deadline):
        m = re.search(r"SHELL_REMOTE on build=(\S+)", text)
        if m:
            if m.group(1) != expect:
                raise BuildMismatch("the board runs build %s, expected %s"
                                    % (m.group(1), expect))
            return m.group(1)
    raise BuildMismatch("no `SHELL_REMOTE on build=` line in %.0f s (expected %s)"
                        % (wait_s, expect))


def find_ffmpeg(which=shutil.which, globber=glob.glob) -> str:
    found = which("ffmpeg")
    if found:
        return found
    hits = sorted(globber(os.path.expanduser(_FFMPEG_GLOB), recursive=True))
    if hits:
        return hits[-1]
    raise FileNotFoundError("ffmpeg not on PATH and not under WinGet Gyan.FFmpeg*")


def start_capture(out_wav: Path, device: str = CAPTURE_DEVICE, seconds: float = 10,
                  popen=subprocess.Popen, ffmpeg: Optional[str] = None,
                  clock=time.time) -> subprocess.Popen:
    """Record both inputs of the interface: 2 ch, 48 kHz, for `seconds`. The
    dshow input is opened at the device's shared-mode format (48 kHz / 16 bit /
    2 ch) because without those options DirectShow hands over 44.1 kHz and
    ffmpeg would resample, smearing the 1-2 sample clicks; hence no `-ar`. The
    wall-clock time just before ffmpeg starts goes into `<wav>.start` (seconds
    since the epoch) so clicks can be aligned to host-stamped serial lines;
    ffmpeg's own start-up latency is not in it."""
    out_wav = Path(out_wav)
    start_file = out_wav.with_name(out_wav.name + ".start")
    cmd = [ffmpeg or find_ffmpeg(), "-hide_banner", "-loglevel", "error",
           "-f", "dshow", "-sample_rate", "48000", "-sample_size", "16", "-channels", "2",
           "-i", "audio=" + device,
           "-t", "%g" % seconds, "-ac", "2", "-y", str(out_wav)]
    start_file.write_text("%.6f\n" % clock(), encoding="ascii")
    return popen(cmd, stdin=subprocess.DEVNULL)
