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
   rotates its on-phases through three arms, sdram-16 (SDRAM buffer, 16 KiB
   chunks), axi-16 (AXI SRAM buffer, 16 KiB) and sdram-32 (SDRAM, 32 KiB) --
   `buf=` and `chunk=` on its on=1 lines name the arm. AXI SRAM had room for a
   16 KiB buffer only, so two arms alone would differ in location and chunk
   size at once; here the first two differ in location alone and the last two
   in chunk size alone. The audio callback is priced by phase from the
   SHELL_PLAY_WIN frames (SHELL_CB_WINDOW images): each frame goes to the latest
   SHELL_SD_S line before it (idle, sdram16, axi16, sdram32); per class the
   frame count, the mean of the per-frame means (sum / n), the p99 and the
   maximum of win. The verdict is printed, not part of the exit code (a miss
   prices the traffic): each arm's mean within 9 600 cycles of idle's, and no
   win over 945 000; every class needs frames. Two contrasts are reported, not
   judged: location (axi16 - sdram16) and chunk (sdram32 - sdram16). A frame
   with sum=0 n=0 is skipped, and so is the first frame after every phase
   change (each arm change is one).

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

A capture of the tail modes (SHELL_SD_PROBE=rtail or wtail, spec section 5, C1
and C2) is told by its `SHELL_SD_H kind=` lines. tails() merges the 10 s
intervals of each kind into a histogram (buckets [2^i, 2^(i+1)) x 64 us), the
worst call, p99.9 and the ring buffer the worst implies (2 x worst x rate, 384
KB/s reads, 288 KB/s writes, rounded up to 32 KB). An interval that carries an
error (fr, bad) is reported per kind, not merged. Exit 0 when the card test
passed, every kind of the mode has merged intervals and none has an error.

A capture of the bank, buffer or patch mode (SHELL_SD_PROBE=bank, buffer,
patch; spec section 5, D1, D2, D4) is told by its SHELL_SD_K, SHELL_SD_B or
SHELL_SD_P lines, one per operation. bank_stats(), buffer_stats() and
patch_stats() judge them: every bank load cmp=ok; both buffer formats saved
and loaded, every load cmp=ok (a bad one named with its offset); 100 of 100
patch round trips ok; in all three no fr and no stop line. The callback is
priced by operation (ops_by_phase()): the frame right after an operation line
is busy, every other frame idle, judged as A3's (printed, not in the exit
code). Exit 0 when the card test and the mode's stats pass.

A capture of the duplex mode (SHELL_SD_PROBE=duplex, spec section 5, D3) is
told by its SHELL_SD_D lines, one per pass. duplex_stats() totals the
underruns and overruns, keeps the read ring's lowest and the write ring's
highest fill and the slowest f_read / f_write, and summarises the pots (the
burst mode's kept spread per sense pin, over the windows after the first pass)
and the callback (the frames after the first pass's). Exit 0 when the card test
passed and the rings had 0 underruns, 0 overruns and no fr; the pot verdict
(A1's) and the callback's click line are printed, not in the exit code.

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
_SD_H = re.compile(r"^SHELL_SD_H (.*)$")
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


TAIL_BUCKETS = 16
TAIL_BUCKET_US = 64          # sd_probe.cpp: bucket i is [2^i, 2^(i+1)) x 64 us, 0 from 0, 15 open
READ_KINDS = ("seq", "rand", "randfs")    # rtail's phases
WRITE_KINDS = ("w16", "w24", "wmax")      # wtail's phases
SETUP_KINDS = ("setup", "fill")           # failures before the first phase (sd_probe.cpp tail_emit)
RING_RATE_KIB_S = {"read": 384, "write": 288}   # float stereo / 24-bit stereo, spec C1 / C2
RING_STEP_KIB = 32           # one transfer


def ring_kib(worst_us, rate_kib_s):
    """The ring buffer a worst call implies: 2 x worst x rate, rounded up to a
    multiple of 32 KB (spec C1: 2 x worst latency x 384 KB/s). Integer math: no
    float fuzz at an exact step."""
    need = 2 * worst_us * rate_kib_s            # us x KiB/s; / 1e6 is KiB
    return -(-need // (1000000 * RING_STEP_KIB)) * RING_STEP_KIB


def parse_tail_groups(lines):
    """SHELL_SD_H lines (rtail / wtail) joined into intervals. The board prints
    one interval as a stats line (kind seq n worst_us seek_worst_us fr bad pat)
    and two bucket lines, b0= (buckets 0-7) and b1= (8-15), all with the same
    kind and seq; a line with b= and 16 counts is read the same way. Lines
    without a seq each stand alone. Returns [(kind, seq or None, fields)] in
    order of first appearance; fields["b"] is {first bucket index: [counts]}.
    `SHELL_SD_H fill ...` and `SHELL_SD_H done` have no kind= and are skipped."""
    groups = {}
    order = []
    for raw in strip_host_time(lines):
        m = _SD_H.match(raw.strip())
        if not m:
            continue
        kv = _kv(m.group(1))
        kind = kv.get("kind")
        if kind is None:
            continue
        key = (kind, kv["seq"]) if "seq" in kv else (kind, len(order))
        g = groups.get(key)
        if g is None:
            g = groups[key] = {"b": {}}
            order.append(key)
        for k, v in kv.items():
            if k in ("b", "b0", "b1"):
                g["b"][{"b": 0, "b0": 0, "b1": 8}[k]] = [int(x) for x in v.split(",")]
            elif k not in ("kind", "seq", "pat"):
                g[k] = int(v)
            elif k == "pat":
                g[k] = v
        g["seq"] = kv.get("seq")
    return [(key[0], groups[key]["seq"], groups[key]) for key in order]


def _group_buckets(g):
    """The 16 counts of a group, or None when a bucket line is missing or
    their sum is not the group's n."""
    out = [None] * TAIL_BUCKETS
    for first, counts in g["b"].items():
        for i, c in enumerate(counts):
            if first + i < TAIL_BUCKETS:
                out[first + i] = c
    if any(c is None for c in out):
        return None
    return out if sum(out) == g.get("n", -1) else None


def _p999_us(buckets, worst):
    """The upper edge of the bucket the 99.9th-percentile call falls in, cut at
    the worst call (so it never reads above it); None without calls."""
    total = sum(buckets)
    if not total:
        return None
    rank = -(-total * 999 // 1000)
    run = 0
    for i, c in enumerate(buckets):
        run += c
        if run >= rank:
            if i == TAIL_BUCKETS - 1:
                return worst
            return min(worst, (2 ** (i + 1)) * TAIL_BUCKET_US)
    return worst


def tails(lines):
    """C1 / C2, the latency tails: {kind: {"n", "buckets" (16), "worst_us",
    "seek_worst_us", "p999_us", "ring_kib", "intervals", "incomplete", "error",
    "bad", "pat"}} for every kind in the SHELL_SD_H lines.

    Intervals are merged: buckets and n add up, worst is the max. An interval
    whose line says fr != 0 or bad != 0 is an error of its kind and is NOT
    merged; the first one is named in `error` (fr is sticky on the board and
    repeats, those lines say nothing new). An interval whose bucket lines are
    lost, or whose buckets do not add up to its n, is `incomplete`: its worst
    still counts (it can only raise the ring) but its buckets and n do not.
    `ring_kib` is ring_kib() of the merged worst at the kind's rate. `pat` is
    the transfer pattern the board said it used (burst or b2b), "mixed" if the
    capture has both."""
    out = {}
    for kind, seq, g in parse_tail_groups(lines):
        t = out.setdefault(kind, {"n": 0, "buckets": [0] * TAIL_BUCKETS, "worst_us": 0,
                                  "seek_worst_us": 0, "intervals": 0, "incomplete": 0,
                                  "error": None, "bad": 0, "pat": None})
        pat = g.get("pat")
        if pat is not None:
            t["pat"] = pat if t["pat"] in (None, pat) else "mixed"
        fr, bad = g.get("fr", 0), g.get("bad", 0)
        if fr != 0 or bad != 0:
            if t["error"] is None:
                t["error"] = "%s at seq=%s" % (
                    ("fr=%d" % fr) if fr != 0 else ("bad=%d" % bad), seq)
            t["bad"] += bad
            continue
        b = _group_buckets(g)
        t["worst_us"] = max(t["worst_us"], g.get("worst_us", 0))
        t["seek_worst_us"] = max(t["seek_worst_us"], g.get("seek_worst_us", 0))
        if b is None:
            t["incomplete"] += 1
            continue
        t["intervals"] += 1
        t["n"] += g["n"]
        t["buckets"] = [x + y for x, y in zip(t["buckets"], b)]
    for kind, t in out.items():
        t["p999_us"] = _p999_us(t["buckets"], t["worst_us"])
        t["ring_kib"] = ring_kib(t["worst_us"],
                                 RING_RATE_KIB_S["write" if kind in WRITE_KINDS else "read"])
    return out


def tail_report(lines, out, card_ok):
    """The report of an rtail / wtail capture. Exit code: 0 when the card test
    passed, every kind of the mode has merged intervals and none has an error.
    The mode is the family (READ_KINDS or WRITE_KINDS) the kinds in the
    capture belong to. The pass criterion of C1 / C2 -- two runs agree within
    2x on the worst call -- compares two captures, so it is not judged here."""
    t = tails(lines)
    # Only setup / fill errors: the run died before the first kind, so the
    # family is unknown (wtail and rtail both print them). Say the errors and
    # list no kind as missing, for either family.
    setup_only = bool(t) and all(k in SETUP_KINDS for k in t)
    family = WRITE_KINDS if any(k in WRITE_KINDS for k in t) else READ_KINDS
    if setup_only:
        family = ()
        print("latency tails (the run failed before the first kind):", file=out)
    else:
        print("latency tails (%s; buckets [2^i, 2^(i+1)) x %d us, bucket 0 from 0, bucket %d open):"
              % ("writes" if family is WRITE_KINDS else "reads", TAIL_BUCKET_US, TAIL_BUCKETS - 1),
              file=out)
    failed = False
    # A failure before the first kind: the rtail fill (and its early CLMT look)
    # prints as kind=fill, a missing card-test file (fr=-3) as kind=setup.
    for kind in SETUP_KINDS:
        s = t.get(kind)
        if s is not None:
            print("  %s: error (%s), before the first kind" % (kind, s["error"] or "no detail"),
                  file=out)
            failed = True
    for kind in family:
        s = t.get(kind)
        if s is None:
            print("  %s: missing (no SHELL_SD_H line)" % kind, file=out)
            failed = True
            continue
        if s["error"]:
            print("  %s: error (%s), %d intervals merged before it" % (kind, s["error"], s["intervals"]),
                  file=out)
            failed = True
            continue
        if not s["intervals"]:
            print("  %s: missing (no complete interval)" % kind, file=out)
            failed = True
            continue
        print("  %s: %d calls in %d intervals, worst %d us, p99.9 %d us, seek worst %d us, "
              "ring %d KB at %d KB/s, pattern %s%s"
              % (kind, s["n"], s["intervals"], s["worst_us"], s["p999_us"], s["seek_worst_us"],
                 s["ring_kib"], RING_RATE_KIB_S["write" if kind in WRITE_KINDS else "read"],
                 s["pat"], (", %d incomplete" % s["incomplete"]) if s["incomplete"] else ""),
              file=out)
        print("      buckets %s" % ",".join(str(c) for c in s["buckets"]), file=out)
    return 0 if card_ok and not failed else 1


CB_CLASSES = ("idle", "sdram16", "axi16", "sdram32")
CB_ARMS = CB_CLASSES[1:]


def cb_class(kv):
    """The A3 class of a stream-mode SHELL_SD_S line's fields: "idle" for on=0,
    else the arm (buf, chunk) names: sdram16, axi16 or sdram32. An older line
    without chunk= is from before the three arms: sdram32 if buf is sdram or
    missing, axi16 if buf=axi (the two arms of that image)."""
    if kv.get("on") == "0":
        return "idle"
    if kv.get("buf") == "axi":
        return "axi16"
    return "sdram16" if kv.get("chunk") == "16" else "sdram32"


def cb_by_phase(lines):
    """Test A3: the audio callback priced by transfer phase. {"idle", "sdram16",
    "axi16", "sdram32"} -> {"means": [...], "win": [...]}: per SHELL_PLAY_WIN
    frame its mean block (sum / n) and its worst block (win).

    A frame belongs to the latest stream-mode SHELL_SD_S line before it: on=0
    is idle, on=1 the arm `buf=` and `chunk=` name (see cb_class(); the arms
    differ in location alone, sdram16 against axi16, or in chunk size alone,
    sdram16 against sdram32). A frame before the first such line, or after
    a burst-mode line, belongs to no phase. A frame with n=0 (the board prints
    sum=0 n=0 when its torn-read retry gave up) says nothing about the callback
    and is skipped whole, not counted as a zero.

    Print order assumed (shell/panel_scan.cpp, the report loop): each
    iteration prints its SHELL_PLAY_WIN first and runs the stream pass after
    it, whose SHELL_SD_S line comes last. So a frame follows the SHELL_SD_S of
    the pass whose 500 ms it measured (the window since the previous frame),
    and "the latest line before it" is that pass.

    The first frame after every phase change (idle to stream or back, or one
    arm to the next) is dropped: it holds the last chunk of the phase before,
    as the raw windows' first window of a phase does (classify())."""
    out = {k: {"means": [], "win": []} for k in CB_CLASSES}
    cur = None
    fresh = False   # the next frame is the first of a new phase
    for raw in strip_host_time(lines):
        raw = raw.strip()
        m = _SD_S.match(raw)
        if m:
            kv = _kv(m.group(1))
            if kv.get("mode", "stream") != "stream":
                new = None
            else:
                new = cb_class(kv)
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
    """(ok, reasons): each of the three arms' mean within CB_TOLERANCE cycles of
    the idle mean, in either direction, and no window maximum in any class
    above CB_CLICK. A class with no frames cannot be judged and fails (the A3
    run uses the three-arm image, so a capture of the older two-arm image fails
    on its missing arm). The location and chunk contrasts are not judged, see
    cb_report()."""
    st = {k: cb_stats(by[k]) for k in by}
    why = []
    for k in CB_CLASSES:
        if not st[k]["frames"]:
            why.append("no %s frames" % k)
    for k in CB_ARMS:
        if st[k]["frames"] and st["idle"]["frames"]:
            d = st[k]["mean"] - st["idle"]["mean"]
            if abs(d) > CB_TOLERANCE:
                why.append("%s mean %+.0f cycles from idle (limit %d)" % (k, d, CB_TOLERANCE))
    for k in CB_CLASSES:
        if st[k]["frames"] and st[k]["max"] > CB_CLICK:
            why.append("%s win %d is over the click line %d" % (k, st[k]["max"], CB_CLICK))
    return not why, why


def cb_report(by, out):
    """Prints cb_by_phase()'s classes, the two contrasts (reported, not judged)
    and the verdict, if any frame was found. location is axi16 - sdram16 (the
    same 16 KB chunk, only the buffer's place differs); chunk is sdram32 -
    sdram16 (the same SDRAM buffer, only the chunk size differs)."""
    if not any(by[k]["means"] for k in by):
        return
    st = {k: cb_stats(by[k]) for k in by}
    print("callback by phase (SHELL_PLAY_WIN: mean of the per-frame sum / n; "
          "win: the frame's worst block):", file=out)
    for k in CB_CLASSES:
        s = st[k]
        if not s["frames"]:
            print("  %s: no frames" % k, file=out)
            continue
        extra = "" if k == "idle" or not st["idle"]["frames"] else \
            ", %+.0f from idle" % (s["mean"] - st["idle"]["mean"])
        print("  %s: %d frames, mean %.0f cycles%s, p99 win %d, max win %d"
              % (k, s["frames"], s["mean"], extra, s["p99"], s["max"]), file=out)
    for name, hi, lo in (("location (axi16 - sdram16)", "axi16", "sdram16"),
                         ("chunk (sdram32 - sdram16)", "sdram32", "sdram16")):
        if st[hi]["frames"] and st[lo]["frames"]:
            print("  %s: %+.0f cycles" % (name, st[hi]["mean"] - st[lo]["mean"]), file=out)
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


PATCH_RUNS = 100     # sd_probe.cpp kPatchRuns: spec D4's "100 of 100"
BUFFER_KEYS = (("f32", "save"), ("f32", "load"), ("s16", "save"), ("s16", "load"))
_OPS = re.compile(r"^SHELL_SD_([KBP]) (.*)$")
# The lines that report a card operation (the export, a load, a save, a patch
# round trip); `stop` and `done` lines report state, not work.
_OP_LINE = re.compile(r"^SHELL_SD_[KBP] (?:export |load_us=|fmt=|i=)")
_OP_TEXT = ("cmp", "fmt", "op")


def parse_ops(lines, tag):
    """Every `SHELL_SD_<tag>` line (K bank, B buffer, P patch), in order, as a
    field dict: ints, except cmp, fmt and op, which stay text. "kind" is the
    line's leading word when it is not a field (export, stop, done), else "op"."""
    out = []
    for raw in strip_host_time(lines):
        m = _OPS.match(raw.strip())
        if not m or m.group(1) != tag:
            continue
        words = m.group(2).split()
        d = {"kind": words[0] if words and "=" not in words[0] else "op"}
        for k, v in _kv(m.group(2)).items():
            d[k] = v if k in _OP_TEXT else int(v)
        out.append(d)
    return out


def _stops(rows, why):
    for r in rows:
        if r["kind"] == "stop":
            why.append("stopped, fr=%d" % r.get("fr", 0))
            return


def bank_stats(lines):
    """D1, the bank reload: {"loads", "ok", "load_us_max", "load_us_mean",
    "worst_us", "held_max", "slots", "export", "pass", "why"}. Pass: at least
    one load, every load cmp=ok with fr=0, the export (when the capture has
    it) fr=0, and no stop line."""
    rows = parse_ops(lines, "K")
    loads = [r for r in rows if r["kind"] == "op"]
    export = next((r for r in rows if r["kind"] == "export"), None)
    why = []
    if export is not None and export.get("fr", 0) != 0:
        why.append("export fr=%d" % export["fr"])
    if not loads:
        why.append("no load line")
    for n, r in enumerate(loads):
        if r.get("fr", 0) != 0:
            why.append("load %d fr=%d" % (n, r["fr"]))
            break
    for n, r in enumerate(loads):
        if r.get("cmp") != "ok":
            why.append("load %d cmp=%s (slot %d)" % (n, r.get("cmp"), r.get("slot", -1)))
            break
    _stops(rows, why)
    us = [r["load_us"] for r in loads]
    return {"loads": len(loads),
            "ok": sum(1 for r in loads if r.get("cmp") == "ok" and r.get("fr", 0) == 0),
            "load_us_max": max(us, default=0),
            "load_us_mean": (sum(us) / len(us)) if us else 0,
            "worst_us": max((r["worst_us"] for r in loads), default=0),
            "held_max": max((r["held"] for r in loads), default=0),
            "slots": sorted({r["slot"] for r in loads}),
            "export": export, "pass": not why, "why": why}


def buffer_stats(lines):
    """D2, the buffer save and load: {"ops": {(fmt, op): {"n", "ms_max",
    "ms_mean", "worst_us", "held_max"}}, "pass", "why"}. Pass: every one of
    f32/s16 x save/load is in the capture, every load says cmp=ok, nothing
    has fr != 0, and no stop line. A bad load is named with its format and
    the first bad byte (off=)."""
    rows = parse_ops(lines, "B")
    done = [r for r in rows if r["kind"] == "op"]
    why = []
    ops = {}
    for key in BUFFER_KEYS:
        rs = [r for r in done if (r.get("fmt"), r.get("op")) == key]
        if not rs:
            why.append("no %s %s" % key)
            continue
        ms = [r["ms"] for r in rs]
        ops[key] = {"n": len(rs), "ms_max": max(ms), "ms_mean": sum(ms) / len(ms),
                    "worst_us": max(r["worst_us"] for r in rs),
                    "held_max": max(r["held"] for r in rs)}
    for r in done:
        if r.get("fr", 0) != 0:
            why.append("%s %s fr=%d" % (r.get("fmt"), r.get("op"), r["fr"]))
            break
    for r in done:
        if r.get("op") == "load" and r.get("cmp") != "ok":
            why.append("%s load cmp=%s off=%d" % (r.get("fmt"), r.get("cmp"), r.get("off", 0)))
            break
    _stops(rows, why)
    return {"ops": ops, "pass": not why, "why": why}


def patch_stats(lines):
    """D4, the patch file: {"n", "ok", "write_us_max", "sync_us_max",
    "read_us_max", "seqs", "pass", "why"}. Pass: PATCH_RUNS (100) round trips
    or more, every one cmp=ok with fr=0 -- 100 of 100 -- and no stop line."""
    rows = parse_ops(lines, "P")
    its = [r for r in rows if r["kind"] == "op"]
    good = [r for r in its if r.get("cmp") == "ok" and r.get("fr", 0) == 0]
    why = []
    if len(its) < PATCH_RUNS:
        why.append("only %d of %d round trips" % (len(its), PATCH_RUNS))
    if len(good) != len(its):
        r = next(r for r in its if r not in good)
        why.append("%d of %d ok, first bad i=%d cmp=%s fr=%d"
                   % (len(good), len(its), r.get("i", -1), r.get("cmp"), r.get("fr", 0)))
    _stops(rows, why)
    return {"n": len(its), "ok": len(good),
            "write_us_max": max((r["write_us"] for r in its), default=0),
            "sync_us_max": max((r["sync_us"] for r in its), default=0),
            "read_us_max": max((r["read_us"] for r in its), default=0),
            "seqs": sorted({r["seq"] for r in its}), "pass": not why, "why": why}


def ops_by_phase(lines):
    """D1, D2, D4: the audio callback priced by operation. {"idle", "busy"} ->
    {"means", "win"} in cb_by_phase()'s shape. These modes print no SHELL_SD_S
    lines (cb_by_phase() finds nothing in them); a busy phase is a frame with
    an operation line in it.

    Every operation runs inside one report pass (sd_probe.cpp: the buffer's
    saves and loads take the seconds they take in that one pass), and the
    pass's line comes before the frame that measured it (cb_by_phase()'s print
    order). So the first frame after an operation line is busy and every other
    frame idle -- with no spill to drop, unlike the stream's phases. Frames
    before the first operation line (the card test) are nobody's; a frame with
    n=0 is skipped."""
    out = {k: {"means": [], "win": []} for k in ("idle", "busy")}
    started = busy = False
    for raw in strip_host_time(lines):
        raw = raw.strip()
        if _OP_LINE.match(raw):
            started = busy = True
            continue
        m = _PLAY_WIN.match(raw)
        if m is None or not started:
            continue
        cls = "busy" if busy else "idle"
        busy = False
        kv = {k: int(v) for k, v in _INT_KV.findall(m.group(1))}
        if kv.get("n", 0) <= 0 or "sum" not in kv or "win" not in kv:
            continue
        out[cls]["means"].append(kv["sum"] / kv["n"])
        out[cls]["win"].append(kv["win"])
    return out


def ops_cb_verdict(by):
    """(ok, reasons): the busy mean within CB_TOLERANCE of the idle mean (A3's
    1 %, spec D1 / D2), no window maximum over CB_CLICK, both classes with
    frames."""
    st = {k: cb_stats(by[k]) for k in by}
    why = ["no %s frames" % k for k in ("idle", "busy") if not st[k]["frames"]]
    if st["busy"]["frames"] and st["idle"]["frames"]:
        d = st["busy"]["mean"] - st["idle"]["mean"]
        if abs(d) > CB_TOLERANCE:
            why.append("busy mean %+.0f cycles from idle (limit %d)" % (d, CB_TOLERANCE))
    for k in ("idle", "busy"):
        if st[k]["frames"] and st[k]["max"] > CB_CLICK:
            why.append("%s win %d is over the click line %d" % (k, st[k]["max"], CB_CLICK))
    return not why, why


def ops_report(lines, out, card_ok):
    """The report of a bank, buffer or patch capture; the mode is the tag of
    its lines. Exit code: 0 when the card test and the mode's stats pass. The
    callback verdict is printed, not part of the exit code (as A3's)."""
    tags = {m.group(1) for m in (_OPS.match(l.strip()) for l in lines) if m}
    ok = card_ok
    if "K" in tags:
        st = bank_stats(lines)
        ex = st["export"]
        if ex is not None:
            print("bank export: %d KiB in %d us, worst f_write %d us, fr %d"
                  % (ex.get("kib", 0), ex.get("us", 0), ex.get("worst_us", 0), ex.get("fr", 0)),
                  file=out)
        print("bank: %d loads, %d ok, load %d us max / %.0f us mean, worst f_read %d us, "
              "held %d blocks max, slots %s"
              % (st["loads"], st["ok"], st["load_us_max"], st["load_us_mean"], st["worst_us"],
                 st["held_max"], st["slots"]), file=out)
        print("bank verdict: %s" % ("PASS" if st["pass"] else "FAIL (" + "; ".join(st["why"]) + ")"),
              file=out)
        ok = ok and st["pass"]
    if "B" in tags:
        st = buffer_stats(lines)
        for key in BUFFER_KEYS:
            o = st["ops"].get(key)
            if o:
                print("buffer %s %s: %d, %d ms max / %.0f ms mean, worst call %d us, held %d max"
                      % (key + (o["n"], o["ms_max"], o["ms_mean"], o["worst_us"], o["held_max"])),
                      file=out)
        print("buffer verdict: %s" % ("PASS" if st["pass"] else "FAIL (" + "; ".join(st["why"]) + ")"),
              file=out)
        ok = ok and st["pass"]
    if "P" in tags:
        st = patch_stats(lines)
        print("patch: %d of %d ok, worst write %d us, f_sync %d us, read %d us, record seq %s"
              % (st["ok"], st["n"], st["write_us_max"], st["sync_us_max"], st["read_us_max"],
                 ",".join(str(s) for s in st["seqs"]) or "-"), file=out)
        print("patch verdict: %s" % ("PASS" if st["pass"] else "FAIL (" + "; ".join(st["why"]) + ")"),
              file=out)
        ok = ok and st["pass"]
    by = ops_by_phase(lines)
    if any(by[k]["means"] for k in by):
        print("callback by operation (SHELL_PLAY_WIN: mean of the per-frame sum / n; "
              "win: the frame's worst block):", file=out)
        stats = {k: cb_stats(by[k]) for k in by}
        for k in ("idle", "busy"):
            s = stats[k]
            if not s["frames"]:
                print("  %s: no frames" % k, file=out)
                continue
            extra = "" if k == "idle" or not stats["idle"]["frames"] else \
                ", %+.0f from idle" % (s["mean"] - stats["idle"]["mean"])
            print("  %s: %d frames, mean %.0f cycles%s, p99 win %d, max win %d"
                  % (k, s["frames"], s["mean"], extra, s["p99"], s["max"]), file=out)
        cok, why = ops_cb_verdict(by)
        print("callback verdict: %s" % ("PASS" if cok else "FAIL (" + "; ".join(why) + ")"), file=out)
    return 0 if ok else 1


_SD_D = re.compile(r"^SHELL_SD_D (.*)$")
DUPLEX_READ_RING = 32 * 1024      # sd_probe.cpp kReadRing (C1)
DUPLEX_WRITE_RING = 1152 * 1024   # sd_probe.cpp kWriteRing (C2)


def parse_duplex(lines):
    """Every `SHELL_SD_D` line, in order, as an int field dict."""
    out = []
    for raw in strip_host_time(lines):
        m = _SD_D.match(raw.strip())
        if m:
            out.append({k: int(v) for k, v in _kv(m.group(1)).items()})
    return out


def duplex_frames(lines):
    """D3's callback frames, in cb_by_phase()'s class shape {"means", "win"}.
    The duplex mode streams without a break, so there is one class: every
    SHELL_PLAY_WIN frame after a SHELL_SD_D line (the print order of
    cb_by_phase(): a frame follows the line of the pass it measured), except
    the first, which measured the first pass -- the files opened and the read
    ring primed, not the stream. Frames before the first line (the card test)
    are nobody's; a frame with n=0 is skipped."""
    out = {"means": [], "win": []}
    started = skip = False
    for raw in strip_host_time(lines):
        raw = raw.strip()
        if _SD_D.match(raw):
            if not started:
                started = skip = True
            continue
        m = _PLAY_WIN.match(raw)
        if m is None or not started:
            continue
        if skip:
            skip = False
            continue
        kv = {k: int(v) for k, v in _INT_KV.findall(m.group(1))}
        if kv.get("n", 0) <= 0 or "sum" not in kv or "win" not in kv:
            continue
        out["means"].append(kv["sum"] / kv["n"])
        out["win"].append(kv["win"])
    return out


def duplex_stats(lines):
    """D3, the continuous stream and record: {"passes", "under", "over",
    "rfill_min", "wfill_max", "worst_r_us", "worst_w_us", "held_share",
    "windows", "pots", "pots_ok", "cb", "cb_ok", "pass", "why"}.

    under / over: the totals over every line (each line counts its own pass).
    rfill_min: the read ring's lowest fill, wfill_max the write ring's highest,
    in bytes, over all passes. pass: at least one line, 0 underruns, 0
    overruns, no fr -- the rings; the exit code. why names the first line with
    an fr (sticky on the board: later lines repeat it).

    pots (Task 5's burst_summary(), per sense pin): the kept spread of the raw
    windows collected after the first pass (counter w - 1 above the first
    line's w; the first pass opens the files and primes the read ring), with
    pots_ok A1's pass: no pot over its pin's band, none over kPotTouch. cb
    (Task 6's cb_stats()) over duplex_frames(), with cb_ok: no block over the
    click line. Both verdicts are printed, not in the exit code, as A1's and
    A3's are; A3's 1 % comparison needs an idle mean, which a capture that
    streams throughout does not hold."""
    lines = strip_host_time(lines)
    rows = parse_duplex(lines)
    why = []
    if not rows:
        why.append("no SHELL_SD_D line")
    under = sum(r.get("under", 0) for r in rows)
    over = sum(r.get("over", 0) for r in rows)
    if under:
        why.append("%d underruns" % under)
    if over:
        why.append("%d overruns" % over)
    bad = next((r for r in rows if r.get("fr", 0) != 0), None)
    if bad is not None:
        why.append("fr=%d at w=%d" % (bad["fr"], bad.get("w", -1)))
    windows = []
    if rows:
        first = rows[0].get("w", 0)
        windows = [win for win in rp.parse_raw_windows(lines) if win["w"] - 1 > first]
    pots = burst_summary(windows, rp.row_senses())
    pots_ok = bool(windows) and all(p["over_h"] == 0 and p["over_touch"] == 0 for p in pots.values())
    cb = cb_stats(duplex_frames(lines))
    cb_ok = cb["frames"] > 0 and cb["max"] <= CB_CLICK
    return {"passes": len(rows), "under": under, "over": over,
            "rfill_min": min((r["rfill"] for r in rows), default=0),
            "wfill_max": max((r["wfill"] for r in rows), default=0),
            "worst_r_us": max((r["worst_r_us"] for r in rows), default=0),
            "worst_w_us": max((r["worst_w_us"] for r in rows), default=0),
            "held_share": (sum(r["held"] for r in rows) / (BLOCKS_PER_PASS * len(rows))
                           if rows else 0.0),
            "windows": len(windows), "pots": pots, "pots_ok": pots_ok,
            "cb": cb, "cb_ok": cb_ok, "pass": not why, "why": why}


def duplex_report(lines, out, card_ok):
    """The report of a duplex capture. Exit code: 0 when the card test passed
    and duplex_stats() passes (the rings); the pot and callback verdicts are
    printed, not in the exit code."""
    st = duplex_stats(lines)
    print("duplex: %d passes, %d underruns, %d overruns, read ring min %d of %d B, "
          "write ring max %d of %d B" % (st["passes"], st["under"], st["over"], st["rfill_min"],
                                         DUPLEX_READ_RING, st["wfill_max"], DUPLEX_WRITE_RING),
          file=out)
    print("duplex: worst f_read %d us, worst f_write %d us, held %.1f %% of the blocks"
          % (st["worst_r_us"], st["worst_w_us"], 100.0 * st["held_share"]), file=out)
    print("duplex verdict: %s" % ("PASS" if st["pass"] else "FAIL (" + "; ".join(st["why"]) + ")"),
          file=out)
    print("pots (spread of the readings the hold kept, %d windows after the first pass):"
          % st["windows"], file=out)
    print("  sense  band  rows  worst/median  over band  over %d" % rp.POT_TOUCH, file=out)
    for p in range(rp.SENSES):
        s = st["pots"][p]
        med = "-" if s["median"] is None else "%.1f" % s["median"]
        print("  %5d  %4d  %4d  %6s/%-6s  %9d  %8d"
              % (p, rp.REVA_HYSTERESIS[p], s["rows"], s["worst"], med, s["over_h"], s["over_touch"]),
              file=out)
    over_h = sum(s["over_h"] for s in st["pots"].values())
    over_t = sum(s["over_touch"] for s in st["pots"].values())
    print("pots verdict: %s (%d over their band, %d over %d)"
          % ("PASS" if st["pots_ok"] else "FAIL", over_h, over_t, rp.POT_TOUCH), file=out)
    cb = st["cb"]
    if cb["frames"]:
        print("callback: %d frames, mean %.0f cycles, p99 win %d, max win %d"
              % (cb["frames"], cb["mean"], cb["p99"], cb["max"]), file=out)
    else:
        print("callback: no frames", file=out)
    print("callback verdict: %s (the click line %d; the 1 %% comparison against idle needs "
          "a run without the stream)" % ("PASS" if st["cb_ok"] else "FAIL", CB_CLICK), file=out)
    return 0 if card_ok and st["pass"] else 1


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

    if any(_SD_H.match(l.strip()) for l in lines):
        return tail_report(lines, out, ok)
    if any(_OPS.match(l.strip()) for l in lines):
        return ops_report(lines, out, ok)
    if any(_SD_D.match(l.strip()) for l in lines):
        return duplex_report(lines, out, ok)

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
