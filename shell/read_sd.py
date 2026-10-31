"""Reads a capture of the SD bring-up image (SHELL_SD=1; spec
docs/superpowers/specs/2026-10-31-sd-bring-up-design.md, bring-up section 11).

Record with read_play.py (it writes every line), then:

    python read_sd.py capture.txt

1. The card test: the last of each SHELL_SD line (mount, prev, write,
   verify), judged -- mount=0, verify=ok with fr=0 and the full 8 MiB, and no
   .bin file in the card root (the root line; the bootloader would start it).
2. The stream: every SHELL_SD_S line with on=1 -- KiB read, the read rate
   over the streaming passes, the worst single f_read, bad chunks, the
   sticky error.
3. Row 11.2: the pots' raw spread (SHELL_PLAY_R, read_play.py's parser) over
   the streaming windows against the idle windows, per sense pin. The image
   streams in raw windows w with (w // 10) odd; the first window of each
   phase can hold a chunk of the previous phase and is not judged, and a
   window counts only when every SHELL_SD_S line that started in it agrees
   with its phase (a stream that stopped on an error shows on=0).

4. Test A3 (spec 2026-10-31-sd-use-cases-tests-design.md): the stream mode
   alternates its transfer buffer between SDRAM and AXI SRAM (`buf=sdram|axi`
   on its on=1 lines), and the audio callback is priced by phase from the
   SHELL_PLAY_WIN frames (SHELL_CB_WINDOW images): each frame goes to the latest
   SHELL_SD_S line before it (idle, sdram, axi); per class the frame count, the
   mean of the per-frame means (sum / n), the p99 and the maximum of win. The
   verdict is printed, not part of the exit code (a miss prices the traffic):
   each buffer class's mean within 9 600 cycles of idle's, and no win over
   945 000. A frame with sum=0 n=0 is skipped, and so is the first frame after
   every phase change. The two arms read different chunks (chunk= on the line:
   32 KiB from SDRAM, 16 KiB from the AXI buffer, which is all AXI SRAM had
   room for).

A capture of the burst mode (SHELL_SD_PROBE=burst, spec
2026-10-31-sd-use-cases-tests-design.md section 5, A1) is told by `mode=burst`
on its SHELL_SD_S lines. Its windows go into classes keyed by the phase the
line names, (on, per, tail), each with its own windows only; per class and
sense pin it prints the worst and median spread of the readings the pot hold
kept, the pots over their pin's band and over kPotTouch, and the share of
blocks the hold dropped. A class whose lines carry an error (fr, bad) is
reported as `error`, not averaged. Exit 0 when the card test passed, no
class has an error, and the idle class and a transferring class have a
judged window.

Exit 0 when the card test passed and both classes have a judged window;
1 otherwise (stream mode). The spread is reported, not judged: the row's verdict needs the
numbers beside the pin's band and Bastian's reading of them.
"""
import re
import sys

import read_play as rp

PHASE_WINDOWS = 10   # sd_probe.cpp kPhaseWin
FILE_KIB = 8192      # sd_probe.cpp kFileBytes
BLOCKS_PER_PASS = 250   # a 500 ms pass (kPassMs) of 2 ms blocks: the base of the held share

_SD = re.compile(r"^SHELL_SD (mount=|prev=|write |verify=)(.*)$")
_SD_S = re.compile(r"^SHELL_SD_S (.*)$")
_HOST_TIME = re.compile(r"^\d+\.\d+ ")
_PLAY_WIN = re.compile(r"^SHELL_PLAY_WIN (.*)$")
_INT_KV = re.compile(r"(\w+)=(-?\d+)")
# Test A3 (spec 2026-10-31-sd-use-cases-tests-design.md): a stream mean within
# 1 % of the idle mean (9.6 k cycles, about half a grain); and the click line
# from the same spec.
CB_TOLERANCE = 9600
CB_CLICK = 945000
_ROOT = re.compile(r"^SHELL_SD root=(\d+) bin=(\d+) names=(.*)$")


def _kv(text):
    return dict(p.split("=", 1) for p in text.split() if "=" in p)


def strip_host_time(lines):
    """Drops the leading `<float> ` remote_run.py puts on every line it
    captures; other lines stay as they are."""
    return [_HOST_TIME.sub("", raw, count=1) for raw in lines]


def parse_sd(lines):
    """The last SHELL_SD line of each kind: {"mount", "prev", "write", "verify"}
    -> field dict (the leading word's value under its own key)."""
    lines = strip_host_time(lines)
    out = {}
    for raw in lines:
        m = _SD.match(raw.strip())
        if not m:
            continue
        head, rest = m.group(1), m.group(2)
        if head == "write ":
            out["write"] = _kv(rest)
        else:
            key = head[:-1]
            out[key] = _kv(key + "=" + rest)
    root = parse_root(lines)
    if root is not None:
        out["root"] = root
    return out


def parse_root(lines):
    """The last `SHELL_SD root=<n> bin=<k> names=<A,B,...>` line as
    {"n": int, "bin": int, "names": [str]}, or None. names is the first few
    root entries only (the firmware cuts the list to fit its log line), so
    `bin` is the count, not len() of the .BIN names."""
    out = None
    for raw in strip_host_time(lines):
        m = _ROOT.match(raw.strip())
        if m:
            out = {"n": int(m.group(1)), "bin": int(m.group(2)),
                   "names": [n for n in m.group(3).split(",") if n]}
    return out


def parse_stream(lines):
    """Every SHELL_SD_S line, in order, as a field dict: ints, except `mode`
    (stream or burst) and `buf` (sdram or axi, stream mode), which stay text.
    A line from an image older than the probe switch has no mode; one older
    than the buffer alternation has no buf."""
    out = []
    for raw in strip_host_time(lines):
        m = _SD_S.match(raw.strip())
        if m:
            out.append({k: (v if k in ("mode", "buf") else int(v))
                        for k, v in _kv(m.group(1)).items()})
    return out


def card_verdict(sd):
    """(ok, reasons)."""
    why = []
    if "mount" not in sd:
        return False, ["no SHELL_SD mount line"]
    if sd["mount"].get("mount") != "0":
        why.append("mount fr=%s" % sd["mount"].get("mount"))
    v = sd.get("verify")
    if v is None:
        why.append("no verify line")
    else:
        if v.get("verify") != "ok":
            why.append("verify=%s off=%s" % (v.get("verify"), v.get("off")))
        if v.get("fr") != "0":
            why.append("verify fr=%s" % v.get("fr"))
        if v.get("kib") != str(FILE_KIB):
            why.append("verify kib=%s, not %d" % (v.get("kib"), FILE_KIB))
    w = sd.get("write")
    if w is None or w.get("fr") != "0" or w.get("kib") != str(FILE_KIB):
        why.append("write %s" % (w,))
    # The bootloader scans the root for a .bin to start; one there (the test
    # file used to be FFSDTEST.BIN) fails the card. No root line: older image.
    root = sd.get("root")
    if root is not None and root["bin"] > 0:
        bins = [n for n in root["names"] if n.upper().endswith(".BIN")]
        why.append("%d .bin file(s) in the card root: %s"
                   % (root["bin"], ",".join(bins) or "see names"))
    return not why, why


def phase_on(w):
    return (w // PHASE_WINDOWS) % 2 == 1


def classify(windows, stream):
    """(streaming, idle) lists of judged raw windows.

    The callback publishes a window and THEN counts g_raw_win up, so the
    window printed as `SHELL_PLAY_R w=n` was collected while the counter --
    which SHELL_SD_S prints as w= and the stream's phase reads -- stood at
    n - 1.

    A window counts only if the report loop already ran in the window before
    it (c - 1 seen): the card test runs ahead of the first pass, and its 8 MiB
    write and read land in the window the first SHELL_SD_S line names (on the
    board, 2026-10-31: a stream-sized spread in an "idle" window)."""
    seen = {}
    for s in stream:
        seen.setdefault(s["w"], set()).add(s["on"])
    on, off = [], []
    for win in windows:
        c = win["w"] - 1
        if c < 1 or c % PHASE_WINDOWS == 0 or c not in seen or c - 1 not in seen:
            continue
        want = 1 if phase_on(c) else 0
        if seen[c] != {want}:
            continue
        (on if want else off).append(win)
    return on, off


def burst_key(s):
    """The class a burst line belongs to: the schedule phase it ran in
    (shell/sd_schedule.cpp burst_phase())."""
    return (s["on"], s["per"], s["tail"])


def burst_classes(windows, stream):
    """Burst mode: {(on, per, tail): class} where a class is
    {"windows", "lines", "error", "passes", "kib", "ms", "kib_s", "worst_us",
    "held_share", "hmax"}.

    A window is judged exactly as classify() does (counter c = w - 1; the
    first window of a phase and the window before the first pass are out; both
    its own and the previous counter have lines), and it goes to the class its
    lines name -- all of them must agree. Each class keeps only its own
    windows, so a spread is never averaged over another phase's.

    error: the first thing that makes the class unusable, or None. A failing
    f_read counts once, on the line where its fr first appears (fr is sticky
    and later lines repeat it); a chunk that failed the compare (bad) counts on
    its own line. A class with an error is reported, not averaged."""
    burst = [s for s in stream if s.get("mode") == "burst"]
    seen = {}
    for s in burst:
        seen.setdefault(s["w"], []).append(s)
    classes = {}

    def get(key):
        return classes.setdefault(key, {"windows": [], "lines": [], "error": None})

    prev_fr = 0
    for s in burst:
        c = get(burst_key(s))
        why = None
        if s["fr"] != 0 and s["fr"] != prev_fr:
            why = "fr=%d" % s["fr"]
        elif s["bad"]:
            why = "bad=%d" % s["bad"]
        prev_fr = s["fr"]
        if why and c["error"] is None:
            c["error"] = "%s at w=%d" % (why, s["w"])
    for win in windows:
        c = win["w"] - 1
        if c < 1 or c % PHASE_WINDOWS == 0 or c not in seen or c - 1 not in seen:
            continue
        keys = {burst_key(s) for s in seen[c]}
        if len(keys) != 1:
            continue
        cls = classes[keys.pop()]
        cls["windows"].append(win)
        cls["lines"].extend(seen[c])
    for cls in classes.values():
        lines = cls["lines"]
        cls["passes"] = len(lines)
        cls["kib"] = sum(l["kib"] for l in lines)
        cls["ms"] = sum(l["ms"] for l in lines)
        cls["kib_s"] = (cls["kib"] * 1000 // cls["ms"]) if cls["ms"] else 0
        cls["worst_us"] = max((l["worst_us"] for l in lines), default=0)
        cls["held_share"] = (sum(l["held"] for l in lines) / (BLOCKS_PER_PASS * len(lines))
                             if lines else 0.0)
        cls["hmax"] = max((l["hmax"] for l in lines), default=0)
    return classes


def burst_summary(windows, senses):
    """Sense pin -> {"rows", "worst", "median", "over_h", "over_touch"} of the
    rows' worst KEPT spread over the windows. A window the hold left a row
    without a reading in publishes lo 65535, hi 0; it says nothing about the
    row and is skipped, and a row with no reading in any window is left out of
    `rows`. over_h: rows whose worst is above their pin's band; over_touch:
    above kPotTouch."""
    from statistics import median
    worst = {}
    for r in range(len(senses)):
        vals = [w["hi"][r] - w["lo"][r] for w in windows if w["lo"][r] <= w["hi"][r]]
        if vals:
            worst[r] = max(vals)
    out = {}
    for p in range(rp.SENSES):
        vals = [worst[r] for r in worst if senses[r] == p]
        out[p] = {"rows": len(vals),
                  "worst": max(vals) if vals else None,
                  "median": median(vals) if vals else None,
                  "over_h": sum(1 for v in vals if v > rp.REVA_HYSTERESIS[p]),
                  "over_touch": sum(1 for v in vals if v > rp.POT_TOUCH)}
    return out


def burst_report(lines, stream, out, card_ok):
    """The per-class report of a burst capture. Exit code: 0 when the card
    test passed, no class has an error, the idle class and at least one
    transferring class have a judged window."""
    classes = burst_classes(rp.parse_raw_windows(lines), stream)
    senses = rp.row_senses()
    print("burst classes (spread of the pot readings that were kept, per sense pin):", file=out)
    failed = False
    for key in sorted(classes):
        c = classes[key]
        head = "class on=%d per=%d tail=%d" % key
        if c["error"]:
            print("%s: error (%s), not averaged" % (head, c["error"]), file=out)
            failed = True
            continue
        if not c["windows"]:
            print("%s: no judged window" % head, file=out)
            continue
        print("%s: %d windows, %d passes, %d KiB in %d ms = %d KiB/s, worst f_read %d us, "
              "held %.1f %% of the blocks, longest run %d"
              % (head, len(c["windows"]), c["passes"], c["kib"], c["ms"], c["kib_s"],
                 c["worst_us"], 100.0 * c["held_share"], c["hmax"]), file=out)
        sm = burst_summary(c["windows"], senses)
        print("  sense  band  rows  worst/median  over band  over %d" % rp.POT_TOUCH, file=out)
        for p in range(rp.SENSES):
            s = sm[p]
            med = "-" if s["median"] is None else "%.1f" % s["median"]
            print("  %5d  %4d  %4d  %6s/%-6s  %9d  %8d"
                  % (p, rp.REVA_HYSTERESIS[p], s["rows"], s["worst"], med,
                     s["over_h"], s["over_touch"]), file=out)
    idle = classes.get((0, 0, 0))
    has_idle = bool(idle and idle["windows"])
    has_burst = any(k[0] == 1 and c["windows"] for k, c in classes.items())
    print("judged: idle class %s, transferring class %s"
          % ("yes" if has_idle else "NO", "yes" if has_burst else "NO"), file=out)
    return 0 if card_ok and not failed and has_idle and has_burst else 1


def cb_by_phase(lines):
    """Test A3: the audio callback priced by transfer phase. {"idle", "sdram",
    "axi"} -> {"means": [...], "win": [...]}: per SHELL_PLAY_WIN frame its mean
    block (sum / n) and its worst block (win).

    A frame belongs to the latest stream-mode SHELL_SD_S line before it: on=0
    is idle, on=1 the buffer `buf=` names (a line from before the alternation
    has none and read into SDRAM). A frame before the first such line, or after
    a burst-mode line, belongs to no phase. A frame with n=0 (the board prints
    sum=0 n=0 when its torn-read retry gave up) says nothing about the callback
    and is skipped whole, not counted as a zero.

    The first frame after every phase change (idle to stream or back, or one
    buffer to the other) is dropped: it holds the last chunk of the phase
    before, as the raw windows' first window of a phase does (classify())."""
    out = {k: {"means": [], "win": []} for k in ("idle", "sdram", "axi")}
    cur = None
    fresh = False   # the next frame is the first of a new phase
    for raw in strip_host_time(lines):
        raw = raw.strip()
        m = _SD_S.match(raw)
        if m:
            kv = _kv(m.group(1))
            if kv.get("mode", "stream") != "stream":
                new = None
            elif kv.get("on") == "0":
                new = "idle"
            else:
                new = "axi" if kv.get("buf") == "axi" else "sdram"
            if new != cur:
                fresh = True
            cur = new
            continue
        m = _PLAY_WIN.match(raw)
        if m is None or cur is None:
            continue
        if fresh:
            fresh = False
            continue
        kv = {k: int(v) for k, v in _INT_KV.findall(m.group(1))}
        if kv.get("n", 0) <= 0 or "sum" not in kv or "win" not in kv:
            continue
        out[cur]["means"].append(kv["sum"] / kv["n"])
        out[cur]["win"].append(kv["win"])
    return out


def cb_stats(cls):
    """One class of cb_by_phase(): {"frames", "mean" (of the per-frame means),
    "p99" (nearest rank, of win), "max" (of win)}; mean, p99 and max are None
    for a class without frames."""
    n = len(cls["means"])
    if not n:
        return {"frames": 0, "mean": None, "p99": None, "max": None}
    wins = sorted(cls["win"])
    return {"frames": n, "mean": sum(cls["means"]) / n,
            "p99": wins[max(0, -(-99 * len(wins) // 100) - 1)], "max": wins[-1]}


def cb_verdict(by):
    """(ok, reasons): each buffer class's mean within CB_TOLERANCE cycles of
    the idle mean, in either direction, and no window maximum in any class
    above CB_CLICK. A class with no frames cannot be judged and fails."""
    st = {k: cb_stats(by[k]) for k in by}
    why = []
    for k in ("idle", "sdram", "axi"):
        if not st[k]["frames"]:
            why.append("no %s frames" % k)
    for k in ("sdram", "axi"):
        if st[k]["frames"] and st["idle"]["frames"]:
            d = st[k]["mean"] - st["idle"]["mean"]
            if abs(d) > CB_TOLERANCE:
                why.append("%s mean %+.0f cycles from idle (limit %d)" % (k, d, CB_TOLERANCE))
    for k in ("idle", "sdram", "axi"):
        if st[k]["frames"] and st[k]["max"] > CB_CLICK:
            why.append("%s win %d is over the click line %d" % (k, st[k]["max"], CB_CLICK))
    return not why, why


def cb_report(by, out):
    """Prints cb_by_phase()'s classes and the verdict, if any frame was found."""
    if not any(by[k]["means"] for k in by):
        return
    st = {k: cb_stats(by[k]) for k in by}
    print("callback by phase (SHELL_PLAY_WIN: mean of the per-frame sum / n; "
          "win: the frame's worst block):", file=out)
    for k in ("idle", "sdram", "axi"):
        s = st[k]
        if not s["frames"]:
            print("  %s: no frames" % k, file=out)
            continue
        extra = "" if k == "idle" or not st["idle"]["frames"] else \
            ", %+.0f from idle" % (s["mean"] - st["idle"]["mean"])
        print("  %s: %d frames, mean %.0f cycles%s, p99 win %d, max win %d"
              % (k, s["frames"], s["mean"], extra, s["p99"], s["max"]), file=out)
    ok, why = cb_verdict(by)
    print("callback verdict: %s" % ("PASS" if ok else "FAIL (" + "; ".join(why) + ")"), file=out)


def stream_stats(stream):
    s_on = [s for s in stream if s["on"] == 1]
    kib = sum(s["kib"] for s in s_on)
    ms = sum(s["ms"] for s in s_on)
    return {"passes": len(s_on), "kib": kib, "ms": ms,
            "kib_s": (kib * 1000 // ms) if ms else 0,
            "worst_us": max((s["worst_us"] for s in s_on), default=0),
            "bad": sum(s["bad"] for s in s_on),
            "fr": stream[-1]["fr"] if stream else None}


def report(lines, out=None):
    out = out or sys.stdout
    lines = strip_host_time(lines)   # read_play's parsers do not know the prefix
    sd = parse_sd(lines)
    ok, why = card_verdict(sd)
    for key in ("mount", "prev", "write", "verify"):
        if key in sd:
            print("%-6s %s" % (key, " ".join("%s=%s" % kv for kv in sd[key].items())),
                  file=out)
    if "root" in sd:
        r = sd["root"]
        print("root   n=%d bin=%d names=%s" % (r["n"], r["bin"], ",".join(r["names"])),
              file=out)
    print("card test: %s" % ("PASS" if ok else "FAIL (" + "; ".join(why) + ")"), file=out)

    stream = parse_stream(lines)
    st = stream_stats(stream)
    print("stream: %(passes)d passes, %(kib)d KiB in %(ms)d ms = %(kib_s)d KiB/s, "
          "worst f_read %(worst_us)d us, bad chunks %(bad)d, fr %(fr)s" % st, file=out)

    if any(s.get("mode") == "burst" for s in stream):
        return burst_report(lines, stream, out, ok)

    cb_report(cb_by_phase(lines), out)

    on, off = classify(rp.parse_raw_windows(lines), stream)
    print("raw windows judged: %d streaming, %d idle" % (len(on), len(off)), file=out)
    if not on or not off:
        return 1
    senses = rp.row_senses()
    s_on = rp.sense_summary(rp.raw_spreads(on), senses)
    s_off = rp.sense_summary(rp.raw_spreads(off), senses)
    print("  sense  band  rows  idle worst/median  stream worst/median  over band idle/stream",
          file=out)
    for p in range(rp.SENSES):
        a, b = s_off[p], s_on[p]
        print("  %5d  %4d  %4d  %10s/%-6s  %12s/%-6s  %6d/%d"
              % (p, rp.REVA_HYSTERESIS[p], b["rows"], a["worst"], a["median"],
                 b["worst"], b["median"], a["over_h"], b["over_h"]), file=out)
    return 0 if ok else 1


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: read_sd.py capture.txt")
    with open(argv[1], encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    return report(lines)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
