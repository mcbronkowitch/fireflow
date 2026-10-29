"""Host test for remote_lib.py and run_stats.py (plan 2026-10-29-cpu-hunt-remote
Task 3). Hermetic: no serial port, no dfu-util, no ffmpeg -- every external
effect is a fake. Plain script, exit code is the verdict."""
import os, sys, tempfile
from pathlib import Path
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import serial
import remote_lib as rl
import run_stats as rs

FAILS = []
def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond: FAILS.append(name)


# ---- Port: reconnect ------------------------------------------------------
class FakeSerial:
    """readline() pops the script; an Exception instance in it is raised."""
    def __init__(self, script):
        self.script, self.written, self.closed = list(script), [], False
    def readline(self):
        item = self.script.pop(0) if self.script else b""
        if isinstance(item, Exception): raise item
        return item
    def write(self, data): self.written.append(data)
    def close(self): self.closed = True

opened = []
def make_opener(plan):
    """plan: one entry per open attempt -- a FakeSerial, or an exception."""
    plan = list(plan)
    def opener(name, baud, timeout=None):
        opened.append(name)
        item = plan.pop(0)
        if isinstance(item, Exception): raise item
        return item
    return opener

ticks = iter([10.0, 11.0, 9.0, 12.0, 13.0, 14.0, 15.0, 16.0])   # one step backwards
sleeps = []
first = FakeSerial([b"a\r\n", b"b\r\n", serial.SerialException("handle lost")])
second = FakeSerial([b"c\r\n"])
port = rl.Port("COM9",
               opener=make_opener([first,
                                   serial.SerialException("still gone"),   # port absent once
                                   second]),
               sleep=sleeps.append, clock=lambda: next(ticks))
seen = []
for stamp, text in port.lines(until=lambda: any(t == "c" for _, t in seen)):
    seen.append((stamp, text))
texts = [t for _, t in seen]
check("port: lines before and after the loss, with #reconnect between",
      texts[:2] == ["a", "b"] and texts[2].startswith("#reconnect") and texts[3] == "c")
check("port: stamps are monotonic even when the clock steps back",
      all(seen[i][0] <= seen[i + 1][0] for i in range(len(seen) - 1)))
check("port: polled every 0.5 s while the port was absent", sleeps == [0.5])
check("port: dead handle closed before reopening", first.closed)

def never_back(name, baud, timeout=None): raise serial.SerialException("gone")
port = rl.Port("COM9", opener=never_back, give_up_s=2.0, sleep=lambda s: None)
try:
    list(port.lines(until=lambda: False)); gave_up = False
except rl.PortGone:
    gave_up = True
check("port: gives up with PortGone after give_up_s", gave_up)

ser = FakeSerial([])
port = rl.Port(lambda: "COM3", opener=make_opener([ser]))
port.send("PING")
check("port: send writes the line, newline-terminated", ser.written == [b"PING\n"])

class Info:  # list_ports entry
    def __init__(self, dev, vid, pid): self.device, self.vid, self.pid = dev, vid, pid
check("find_port: matches 0483:5740, not DFU or an ST-Link",
      rl.find_port(lambda: [Info("COM1", 0x0483, 0xDF11), Info("COM2", 0x0483, 0x374B),
                            Info("COM7", 0x0483, 0x5740)]) == "COM7")
check("find_port: None when the board is absent",
      rl.find_port(lambda: [Info("COM1", 0x1234, 0x5740)]) is None)


# ---- flash_pair -----------------------------------------------------------
class Res:
    def __init__(self, rc=0, out=""): self.returncode, self.stdout = rc, out

def fake_run_factory(fail_on=None, dfu_after=0):
    calls, state = [], {"l": 0}
    def run(cmd, **kw):
        calls.append(cmd)
        if cmd[1] == "-l":
            state["l"] += 1
            return Res(0, "Found DFU: [0483:df11]" if state["l"] > dfu_after else "")
        img = cmd[cmd.index("-D") + 1]
        return Res(74 if fail_on and fail_on in img else 0)
    return run, calls

run, calls = fake_run_factory()
rl.flash_pair(Path("q.bin"), Path("s.bin"), run=run, sleep=lambda s: None)
writes = [c for c in calls if "-D" in c]
check("flash: QSPI before SRAM", [c[c.index("-D") + 1] for c in writes] == ["q.bin", "s.bin"])
check("flash: addresses, :leave only on SRAM",
      [c[c.index("-s") + 1] for c in writes] == ["0x90100000", "0x90040000:leave"])
check("flash: DFU device listed before the first write", calls[0][1] == "-l")

run, calls = fake_run_factory(dfu_after=3)
rl.flash_pair(Path("q.bin"), Path("s.bin"), run=run, sleep=lambda s: None)
check("flash: waits for the DFU device to appear", sum(1 for c in calls if c[1] == "-l") == 4)

run, calls = fake_run_factory(fail_on="q.bin")
try:
    rl.flash_pair(Path("q.bin"), Path("s.bin"), run=run, sleep=lambda s: None); raised = False
except rl.FlashError:
    raised = True
check("flash: failing QSPI step raises ...", raised)
check("... and SRAM is never written", [c for c in calls if "-D" in c and "s.bin" in c] == [])

run, calls = fake_run_factory(dfu_after=10**9)
try:
    rl.flash_pair(Path("q.bin"), Path("s.bin"), run=run, wait_s=2, sleep=lambda s: None); raised = False
except rl.FlashError:
    raised = True
check("flash: no DFU device in time raises, nothing written",
      raised and not [c for c in calls if "-D" in c])


# ---- capture --------------------------------------------------------------
with tempfile.TemporaryDirectory() as d:
    got = {}
    def fake_popen(cmd, **kw): got["cmd"] = cmd; return "proc"
    wav = Path(d) / "audio.wav"
    p = rl.start_capture(wav, "Line (Universal Audio Twin USB)", 12.5,
                         popen=fake_popen, ffmpeg="ffmpeg", clock=lambda: 1234.5)
    cmd = got["cmd"]
    check("capture: dshow device, 2 ch, 48 kHz, duration",
          "audio=Line (Universal Audio Twin USB)" in cmd and cmd[cmd.index("-ac") + 1] == "2"
          and cmd[cmd.index("-ar") + 1] == "48000" and cmd[cmd.index("-t") + 1] == "12.5"
          and cmd[-1] == str(wav) and p == "proc")
    check("capture: wall-clock start written beside the wav",
          float((Path(d) / "audio.wav.start").read_text()) == 1234.5)
check("find_ffmpeg: PATH first", rl.find_ffmpeg(which=lambda n: "C:/x/ffmpeg.exe",
                                                globber=lambda *a, **k: ["no"]) == "C:/x/ffmpeg.exe")
check("find_ffmpeg: WinGet glob second", rl.find_ffmpeg(which=lambda n: None,
                                                        globber=lambda *a, **k: ["g/ffmpeg.exe"]) == "g/ffmpeg.exe")


# ---- run_stats ------------------------------------------------------------
cb = list(range(1, 1001))                    # 1..1000
s = rs.summarize(cb)
check("summarize: exact percentiles of 1..1000",
      (s["n"], s["p50"], s["p99"], s["p999"], s["max"]) == (1000, 500, 990, 999, 1000))
cb = [900_000] * 97 + [930_000, 930_001, 966_806]
s = rs.summarize(cb)
check("summarize: over_930k is strictly above, max exact",
      s["over_930k"] == 2 and s["max"] == 966_806 and s["p50"] == 900_000 and s["p99"] == 930_001)
def raises(fn, *a):
    try: fn(*a)
    except ValueError: return True
    return False
check("summarize: empty input raises ValueError", raises(rs.summarize, []))

a = {"n": 100, "p50": 900, "p99": 927, "p999": 940, "max": 966, "over_930k": 3}
b = {"n": 100, "p50": 902, "p99": 925, "p999": 944, "max": 960, "over_930k": 1}
fl = rs.aa_floor(a, b)
check("aa_floor: absolute difference per key",
      fl == {"n": 0, "p50": 2, "p99": 2, "p999": 4, "max": 6, "over_930k": 2})

def with_(d, **kw): e = dict(d); e.update(kw); return e
floor = {"p99": 2, "max": 6}
base = {"p99": 927, "max": 966}
check("verdict: inside the floor is noise",
      rs.verdict(base, with_(base, p99=926, max=962), floor) == "noise")
check("verdict: exactly at the floor is noise (strict)",
      rs.verdict(base, with_(base, p99=925, max=960), floor) == "noise")
check("verdict: lower beyond both floors saves",
      rs.verdict(base, with_(base, p99=900, max=940), floor) == "saves")
check("verdict: higher beyond a floor costs",
      rs.verdict(base, with_(base, p99=940, max=966), floor) == "costs"
      and rs.verdict(base, with_(base, p99=927, max=980), floor) == "costs")
check("verdict: p99 down but max up is a cost, not a save",
      rs.verdict(base, with_(base, p99=900, max=980), floor) == "costs")
check("verdict: only one of p99/max lower beyond floor is noise",
      rs.verdict(base, with_(base, p99=900, max=965), floor) == "noise")

serial_txt = [
    "1000.001 SHELL_REMOTE on build=abc",
    "1000.100 SHELL_PLAY_V r=0 1 2 3",
    "1000.200 SHELL_PLAY_LAW cyc_last=1 cyc_max=2 led_last=3 led_max=4 cb_last=0 cb_max=0",
    "1000.700 SHELL_PLAY_LAW cyc_last=1 cyc_max=2 led_last=3 led_max=4 cb_last=900000 cb_max=910000",
    "1000.701 SHELL_PLAY_WIN win=912000 gap=960100 over=2 n=250",
    "1001.200 #reconnect gap>=1.5s",
    "1001.201 garbage ??? cb_last=notanumber",
    "1001.700 SHELL_PLAY_LAW cyc_last=1 cyc_max=2 led_last=3 led_max=4 cb_last=905000 cb_max=966806",
    "1001.701 SHELL_PLAY_WIN win=966806 gap=960150 over=5 n=250",
]
sm = rs.summarize_run(serial_txt)
check("summarize_run: cb_last drops the zero, frame_max from WIN",
      sm["cb_last"]["n"] == 2 and sm["cb_last"]["max"] == 905000
      and sm["frame_max"]["max"] == 966806 and sm["frame_max"]["n"] == 2)
check("summarize_run: totals, reconnects, unknown lines ignored",
      sm["frames"] == 3 and sm["reconnects"] == 1 and sm["cb_max_final"] == 966806
      and sm["gap_max"] == 960150 and sm["over_total"] == 7)
check("summarize_run: a run without WIN lines still summarizes",
      rs.summarize_run(serial_txt[:4])["frame_max"] is None)

print("\n%d failed" % len(FAILS) if FAILS else "\nall ok")
sys.exit(1 if FAILS else 0)
