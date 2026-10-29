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
import shutil
import subprocess
import time
from pathlib import Path
from typing import Callable, Iterator, Optional, Union

import serial

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
        self._opener = opener or serial.Serial
        self._baud = baud
        self._retry_s = retry_s
        self._max_tries = max(1, math.ceil(give_up_s / retry_s))
        self._sleep = sleep
        self._clock = clock
        self._ser = None
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
            raise serial.SerialException("port not present")
        self._ser = self._opener(name, self._baud, timeout=0.2)

    def _close(self) -> None:
        ser, self._ser = self._ser, None
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
            except (serial.SerialException, OSError):
                self._sleep(self._retry_s)
                waited += self._retry_s
        raise PortGone("serial port did not come back in %.0f s"
                       % (self._max_tries * self._retry_s))

    def _ensure(self) -> None:
        if self._ser is None:
            self._reopen()

    def send(self, line: str) -> None:
        data = (line.rstrip("\r\n") + "\n").encode("ascii")
        self._ensure()
        try:
            self._ser.write(data)
        except (serial.SerialException, OSError):
            self._reopen()
            self._ser.write(data)

    def lines(self, until: Optional[Callable[[], bool]] = None) -> Iterator[tuple]:
        """Yield `(host_time, text)` until `until()` is true (checked before
        every read; the port's 0.2 s read timeout bounds the lag)."""
        self._ensure()
        while until is None or not until():
            try:
                raw = self._ser.readline()
            except (serial.SerialException, OSError):
                waited = self._reopen()
                yield self._stamp(), "#reconnect gap>=%.1fs" % waited
                continue
            if not raw:
                continue
            yield self._stamp(), raw.decode("utf-8", "replace").rstrip("\r\n")

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
               sleep=time.sleep) -> None:
    """QSPI first, then SRAM with :leave (CLAUDE.md: the pair, always in this
    order). Waits for the Daisy DFU device first; raises FlashError on any
    non-zero exit, and a failed QSPI step stops before SRAM is touched."""
    wait_for_dfu(run, wait_s, sleep)
    steps = (("QSPI", qspi, QSPI_ADDR), ("SRAM", sram, SRAM_ADDR + ":leave"))
    for label, image, addr in steps:
        r = run(["dfu-util", "-a", "0", "-s", addr, "-D", str(image),
                 "-d", "," + DFU_ID])
        if r.returncode != 0:
            raise FlashError("dfu-util %s step failed (exit %d)"
                             % (label, r.returncode))


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
    wall-clock time just before ffmpeg starts goes into `<wav>.start` (seconds
    since the epoch) so clicks can be aligned to host-stamped serial lines;
    ffmpeg's own start-up latency is not in it."""
    out_wav = Path(out_wav)
    start_file = out_wav.with_name(out_wav.name + ".start")
    cmd = [ffmpeg or find_ffmpeg(), "-hide_banner", "-loglevel", "error",
           "-f", "dshow", "-i", "audio=" + device,
           "-t", "%g" % seconds, "-ac", "2", "-ar", "48000", "-y", str(out_wav)]
    start_file.write_text("%.6f\n" % clock(), encoding="ascii")
    return popen(cmd, stdin=subprocess.DEVNULL)
