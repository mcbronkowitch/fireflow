"""Reads a capture of the SD bring-up image (SHELL_SD=1; spec
docs/superpowers/specs/2026-10-31-sd-bring-up-design.md, bring-up section 11).

Record with read_play.py (it writes every line), then:

    python read_sd.py capture.txt

1. The card test: the last of each SHELL_SD line (mount, prev, write,
   verify), judged -- mount=0, verify=ok with fr=0 and the full 8 MiB.
2. The stream: every SHELL_SD_S line with on=1 -- KiB read, the read rate
   over the streaming passes, the worst single f_read, bad chunks, the
   sticky error.
3. Row 11.2: the pots' raw spread (SHELL_PLAY_R, read_play.py's parser) over
   the streaming windows against the idle windows, per sense pin. The image
   streams in raw windows w with (w // 10) odd; the first window of each
   phase can hold a chunk of the previous phase and is not judged, and a
   window counts only when every SHELL_SD_S line that started in it agrees
   with its phase (a stream that stopped on an error shows on=0).

Exit 0 when the card test passed and both classes have a judged window;
1 otherwise. The spread is reported, not judged: the row's verdict needs the
numbers beside the pin's band and Bastian's reading of them.
"""
import re
import sys

import read_play as rp

PHASE_WINDOWS = 10   # sd_probe.cpp kPhaseWin
FILE_KIB = 8192      # sd_probe.cpp kFileBytes

_SD = re.compile(r"^SHELL_SD (mount=|prev=|write |verify=)(.*)$")
_SD_S = re.compile(r"^SHELL_SD_S (.*)$")


def _kv(text):
    return dict(p.split("=", 1) for p in text.split() if "=" in p)


def parse_sd(lines):
    """The last SHELL_SD line of each kind: {"mount", "prev", "write", "verify"}
    -> field dict (the leading word's value under its own key)."""
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
    return out


def parse_stream(lines):
    """Every SHELL_SD_S line, in order, as an int field dict."""
    out = []
    for raw in lines:
        m = _SD_S.match(raw.strip())
        if m:
            out.append({k: int(v) for k, v in _kv(m.group(1)).items()})
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
    sd = parse_sd(lines)
    ok, why = card_verdict(sd)
    for key in ("mount", "prev", "write", "verify"):
        if key in sd:
            print("%-6s %s" % (key, " ".join("%s=%s" % kv for kv in sd[key].items())),
                  file=out)
    print("card test: %s" % ("PASS" if ok else "FAIL (" + "; ".join(why) + ")"), file=out)

    stream = parse_stream(lines)
    st = stream_stats(stream)
    print("stream: %(passes)d passes, %(kib)d KiB in %(ms)d ms = %(kib_s)d KiB/s, "
          "worst f_read %(worst_us)d us, bad chunks %(bad)d, fr %(fr)s" % st, file=out)

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
