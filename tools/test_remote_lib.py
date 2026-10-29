"""Host test for remote_lib.py and run_stats.py (plan 2026-10-29-cpu-hunt-remote
Task 3). Hermetic: no serial port, no dfu-util, no ffmpeg -- every external
effect is a fake. Plain script, exit code is the verdict."""
import os, sys, tempfile
from pathlib import Path
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import remote_lib as rl
import run_stats as rs
import remote_run as rr

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
first = FakeSerial([b"a\r\n", b"b\r\n", rl.SerialException("handle lost")])
second = FakeSerial([b"c\r\n"])
port = rl.Port("COM9",
               opener=make_opener([first,
                                   rl.SerialException("still gone"),   # port absent once
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

def never_back(name, baud, timeout=None): raise rl.SerialException("gone")
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

# The port comes back under another COM name: the name is looked up at every
# open, not once in the constructor.
names = iter(["COM5", "COM7", "COM7"])
opened.clear()
a = FakeSerial([b"x\n", rl.SerialException("gone")])
b = FakeSerial([b"y\n"])
port = rl.Port(lambda: next(names), opener=make_opener([a, b]), sleep=lambda s: None)
seen = []
for stamp, text in port.lines(until=lambda: "y" in [t for _, t in seen]):
    seen.append((stamp, text))
check("port: re-resolves the name on reopen (COM5 then COM7)", opened == ["COM5", "COM7"])
check("port: ... and keeps reading from the new name", [t for _, t in seen][-1] == "y")

# A readline() that times out mid-line must not emit the torn half as a line.
ser = FakeSerial([b"SHELL_PLAY_LAW cb_la", b"", b"st=900000 cb_max=910000\r\n", b"next\n"])
port = rl.Port("COM1", opener=make_opener([ser]))
seen = []
for stamp, text in port.lines(until=lambda: len(seen) >= 2):
    seen.append(text)
check("port: a line torn by a read timeout is emitted whole, once",
      seen == ["SHELL_PLAY_LAW cb_last=900000 cb_max=910000", "next"])
ser = FakeSerial([b"torn", rl.SerialException("gone")])
ser2 = FakeSerial([b"whole\n"])
port = rl.Port("COM1", opener=make_opener([ser, ser2]), sleep=lambda s: None)
seen = []
for stamp, text in port.lines(until=lambda: "whole" in seen):
    seen.append(text)
check("port: the torn half of a dead handle is dropped, not glued to the next line",
      "whole" in seen and not any("tornwhole" in t for t in seen) and "torn" not in seen)

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

# ---- remote_run.cmd_run: failure paths keep what was measured -------------
import argparse, contextlib, io, json, subprocess

LAW = "SHELL_PLAY_LAW cyc_last=1 cyc_max=2 led_last=3 led_max=4 cb_last=%d cb_max=%d"
RUN = "SHELL_REMOTE run id=%d gen=%d counting=%d void=%d tripped=%d gest=%d"
LOADED = ("SHELL_REMOTE load id=%d gen=%d gest=%d name=BBD_BBD_top3 input=noise "
          "settle=500 excite=1,1 rec=0,0")
GESTED = "SHELL_REMOTE gest id=%d gen=%d name=pot_sweep base=144 steps=5 blocks=4000"

def run(rid, counting, void, gest, gen=7, tripped=0):
    """A board run line (shell/panel_scan.cpp); gen 7 is the fixtures' run."""
    return RUN % (rid, gen, counting, void, tripped, gest)
WIN = "SHELL_PLAY_WIN win=%d gap=%d over=%d n=250"

class FakeRunPort:
    """lines() yields the script, then raises if an exception is last."""
    def __init__(self, script): self.script, self.sent, self.closed = script, [], False
    def send(self, line): self.sent.append(line)
    def lines(self, until=None):
        for i, item in enumerate(self.script):
            if isinstance(item, Exception): raise item
            yield 1000.0 + i, item
    def close(self): self.closed = True

class FakeCapture:
    def __init__(self, wav=None, rc=0, hang=False):
        self.wav, self.rc, self.hang = wav, rc, hang
        self.terminated = self.killed = False
    def terminate(self): self.terminated = True
    def kill(self): self.killed = True
    def wait(self, timeout=None):
        if self.hang and not self.killed and not self.terminated:
            raise subprocess.TimeoutExpired("ffmpeg", timeout)
        if self.wav is not None and not (self.terminated or self.killed):
            Path(self.wav).write_bytes(b"RIFF")
        return self.rc

def run_cmd(port, capture_factory, record, load=1, gest=None):
    d = tempfile.mkdtemp()
    args = argparse.Namespace(load=load, gest=gest, tog=None, seconds=5, out=d,
                              record=record, port="COMx")
    with contextlib.redirect_stdout(io.StringIO()):
        rc = rr.cmd_run(args, port_factory=lambda a: port,
                        capture_fn=capture_factory, sleep=lambda s: None)
    s = json.loads((Path(d) / "summary.json").read_text())
    return rc, s, Path(d)

p = FakeRunPort([LOADED % (1, 7, -1), run(1, 1, 0, -1), LAW % (900000, 910000), run(1, 1, 0, -1),
                 LAW % (905000, 920000), rl.PortGone("lost for 60 s")])
cap = {}
def mk(wav, device, seconds):
    cap["c"] = FakeCapture(wav=wav); return cap["c"]
rc, s, d = run_cmd(p, mk, record=True)
check("run: PortGone still writes summary.json from what was read",
      s["cb_last"] is not None and s["cb_last"]["n"] == 2 and s["frames"] == 2)
check("run: ... records the error and exits non-zero",
      rc != 0 and s["error"] and "PortGone" in s["error"])
check("run: ... serial.txt kept, port closed, ffmpeg terminated",
      "cb_last=900000" in (d / "serial.txt").read_text() and p.closed and cap["c"].terminated)

p = FakeRunPort([LOADED % (1, 7, -1), run(1, 1, 0, -1), LAW % (900000, 910000)])
rc, s, d = run_cmd(p, lambda w, dev, sec: FakeCapture(wav=None), record=True)
check("run: --record with no audio.wav is reported and non-zero",
      rc != 0 and s["error"] is None and s["record"]["ok"] is False
      and s["record"]["wav_exists"] is False)

p = FakeRunPort([LOADED % (1, 7, -1), run(1, 1, 0, -1), LAW % (900000, 910000)])
rc, s, d = run_cmd(p, lambda w, dev, sec: FakeCapture(wav=w, rc=1), record=True)
check("run: non-zero ffmpeg exit is reported and non-zero",
      rc != 0 and s["record"]["ok"] is False and s["record"]["exit"] == 1)

p = FakeRunPort([LOADED % (1, 7, -1), run(1, 1, 0, -1), LAW % (900000, 910000)])
cap = {}
def mk_hang(wav, device, seconds):
    cap["c"] = FakeCapture(hang=True); return cap["c"]
rc, s, d = run_cmd(p, mk_hang, record=True)
check("run: ffmpeg that never finishes is killed and reported",
      cap["c"].killed and rc != 0 and s["record"]["ok"] is False)

p = FakeRunPort([LOADED % (1, 7, -1), run(1, 1, 0, -1), LAW % (900000, 910000)])
rc, s, d = run_cmd(p, lambda w, dev, sec: FakeCapture(wav=w), record=True)
check("run: clean recorded run is exit 0 with record ok",
      rc == 0 and s["error"] is None and s["record"]["ok"] is True)
rc, s, d = run_cmd(FakeRunPort([LOADED % (1, 7, -1), run(1, 1, 0, -1), LAW % (900000, 910000)]), None, record=False)
check("run: no --record means no record field and exit 0", rc == 0 and s["record"] is None)

# ---- the settle and void filter (Task 5) -----------------------------------
# The board acks the load (`SHELL_REMOTE load id=.. gen=..`), then prints
# `SHELL_REMOTE run id=.. gen=.. counting=.. void=.. tripped=.. gest=..` right
# before each frame's LAW line; counting=1 means the whole frame window lay
# after the load's settle. Only those frames of the asked-for run count.
settle_txt = [
    LOADED % (3, 7, -1),
    run(3, 0, 0, -1), LAW % (955000, 955000), WIN % (990000, 990000, 9),
    run(3, 1, 0, -1), LAW % (900000, 910000), WIN % (912000, 960100, 2),
    run(3, 1, 0, -1), LAW % (905000, 920000), WIN % (920000, 960150, 1),
]
sm = rs.summarize_run(settle_txt, load_id=3)
check("filter: settle frames are excluded (cb_last, frame_max, totals)",
      sm["frames"] == 2 and sm["skipped"] == 1 and sm["cb_last"]["max"] == 905000
      and sm["frame_max"]["max"] == 920000 and sm["over_total"] == 3
      and sm["gap_max"] == 960150 and sm["cb_max_final"] == 920000)
check("filter: a clean run is not void", sm["void"] is False)
sm = rs.summarize_run(settle_txt, load_id=4)
check("filter: frames of another id do not count",
      sm["frames"] == 0 and sm["cb_last"] is None and sm["frame_max"] is None)
void_txt = settle_txt + [run(3, 1, 1, -1), LAW % (901000, 920000), WIN % (915000, 960000, 0)]
check("filter: any void=1 run line makes the run void",
      rs.summarize_run(void_txt, load_id=3)["void"] is True)
check("filter: a void line of another id does not void this run",
      rs.summarize_run(void_txt, load_id=4)["void"] is False)
gest_txt = [GESTED % (2, 7), LOADED % (144, 7, 2),
            run(144, 0, 0, 2), LAW % (950000, 950000),
            run(144, 1, 0, 2), LAW % (930000, 940000)]
check("filter: a gesture run counts by its gest id",
      rs.summarize_run(gest_txt, gest_id=2)["frames"] == 1
      and rs.summarize_run(gest_txt, gest_id=1)["frames"] == 0)
check("filter: a gesture's base is not the plain LOAD of that id",
      rs.summarize_run(gest_txt, load_id=144)["frames"] == 0)
check("no filter: every frame counts, as before",
      rs.summarize_run(settle_txt)["frames"] == 3)

# ---- the run's gen and the overrun guard (Task 5 fix round 1) ----------------
# Frames count only after the ack of the asked-for run, and only while its run
# lines carry that ack's gen: a run of the same id from before (an earlier LOAD
# of it, its counting=1 frames still in the capture) is not this run.
stale_txt = [run(3, 1, 0, -1, gen=6), LAW % (700000, 700000),
             WIN % (700500, 960000, 0)] + settle_txt
sm = rs.summarize_run(stale_txt, load_id=3)
check("gen: a stale same-id counting=1 frame before the load id=3 ack does not count",
      sm["frames"] == 2 and sm["cb_last"]["n"] == 2 and sm["cb_last"]["p50"] == 900000
      and sm["frame_max"]["n"] == 2 and sm["gen"] == 7)
two_acks = [LOADED % (3, 6, -1), run(3, 1, 0, -1, gen=6), LAW % (700000, 700000),
            WIN % (700500, 960000, 0)] + settle_txt
sm = rs.summarize_run(two_acks, load_id=3)
check("gen: the last ack wins (a capture with two LOADs of the id)",
      sm["gen"] == 7 and sm["frames"] == 2 and sm["cb_last"]["p50"] == 900000)
nostale = [LOADED % (3, 7, -1), run(3, 1, 0, -1, gen=6), LAW % (700000, 700000),
           WIN % (700500, 960000, 0), run(3, 1, 0, -1), LAW % (900000, 910000)]
sm = rs.summarize_run(nostale, load_id=3)
check("gen: after the ack, a run line of another gen does not count",
      sm["frames"] == 1 and sm["cb_last"]["max"] == 900000)
check("gen: no ack for the asked-for run, nothing counts",
      rs.summarize_run(settle_txt[1:], load_id=3)["frames"] == 0
      and rs.summarize_run(settle_txt[1:], load_id=3)["gen"] is None)
sm = rs.summarize_run(settle_txt, load_id=3)
check("trip: a clean run is not tripped", sm["tripped"] is False)
trip_txt = settle_txt + ["SHELL_REMOTE guard tripped blk=4242",
                         run(3, 1, 0, -1), LAW % (1000, 950000), WIN % (1000, 960000, 0)]
sm = rs.summarize_run(trip_txt, load_id=3)
check("trip: a guard tripped line marks the run and no frame after it counts",
      sm["tripped"] is True and sm["frames"] == 2 and sm["cb_last"]["max"] == 905000
      and sm["frame_max"]["n"] == 2)
trip_run_txt = settle_txt + [run(3, 0, 0, -1, tripped=1), LAW % (1000, 950000),
                             run(3, 1, 0, -1), LAW % (1000, 950000)]
sm = rs.summarize_run(trip_run_txt, load_id=3)
check("trip: a tripped=1 run line marks the run; nothing counts from it on",
      sm["tripped"] is True and sm["frames"] == 2 and sm["cb_last"]["max"] == 905000)
before_ack = ["SHELL_REMOTE guard tripped blk=12"] + settle_txt
check("trip: a guard tripped line before the ack (the boot trip) is not this run's",
      rs.summarize_run(before_ack, load_id=3)["tripped"] is False
      and rs.summarize_run(before_ack, load_id=3)["frames"] == 2)
other = settle_txt + [run(3, 1, 0, -1, gen=8, tripped=1)]
check("trip: a tripped run line of another gen does not mark this run",
      rs.summarize_run(other, load_id=3)["tripped"] is False)

p = FakeRunPort(settle_txt)
rc, s, d = run_cmd(p, None, record=False, load=3)
check("run: sends LOAD and summarizes only the counting frames of that id",
      p.sent == ["LOAD 3"] and s["frames"] == 2 and s["cb_last"]["max"] == 905000
      and s["void"] is False and rc == 0)
p = FakeRunPort(void_txt)
rc, s, d = run_cmd(p, None, record=False, load=3)
check("run: summary.json records void", s["void"] is True)
p = FakeRunPort(trip_txt)
rc, s, d = run_cmd(p, None, record=False, load=3)
check("run: summary.json records tripped and the gen", s["tripped"] is True
      and s["gen"] == 7 and s["frames"] == 2)
p = FakeRunPort(gest_txt)
rc, s, d = run_cmd(p, None, record=False, load=None, gest=2)
check("run: --gest sends GEST alone (it loads its own base) and filters by it",
      p.sent == ["GEST 2"] and s["frames"] == 1)

# ---- a run that measured nothing is a failure (Task 6, deferred from Task 5) --
# No ack of the asked-for run (gen None: the board never loaded it, or another
# image answered) or not one counted frame (the run never settled inside the
# capture): summary.json says why, and the exit code is not 0, so a sweep
# script cannot record an empty run as a measurement.
p = FakeRunPort(settle_txt[1:])      # no load ack
rc, s, d = run_cmd(p, None, record=False, load=3)
check("run: no ack of the run (gen None) exits non-zero with an error",
      rc != 0 and s["gen"] is None and s["error"] and "gen" in s["error"])
p = FakeRunPort([LOADED % (3, 7, -1), run(3, 0, 0, -1), LAW % (950000, 950000)])
rc, s, d = run_cmd(p, None, record=False, load=3)
check("run: an ack but no counted frame exits non-zero with an error",
      rc != 0 and s["gen"] == 7 and s["frames"] == 0 and s["error"]
      and "frames" in s["error"])
p = FakeRunPort([LOADED % (1, 7, -1), rl.PortGone("lost for 60 s")])
rc, s, d = run_cmd(p, None, record=False, load=1)
check("run: an earlier error is kept, not replaced by the empty-run one",
      rc != 0 and "PortGone" in s["error"])

print("\n%d failed" % len(FAILS) if FAILS else "\nall ok")
sys.exit(1 if FAILS else 0)
