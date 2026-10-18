"""Find clicks in a recording of the board's output.

A dropped audio block -- the callback missed its deadline -- shows up as two
discontinuities about one block apart. This finds them by the second
difference of the signal against its local level (spec
docs/superpowers/specs/2026-10-18-cpu-headroom-design.md section 2.6): in
20 ms windows, a window whose peak |second difference| is more than K times
the median level of the surrounding windows is a hit; hits within two
windows merge into one click.

usage: click_detect.py FILE.wav [--channel N] [--k K] [--period S]

Reads 16-bit PCM WAV, including a file still being written (the samples are
read past the 'data' tag, whatever the header's size field says). --period
adds each click's phase within a repeating cycle of S seconds, which is how
the 2026-10-18 clicks were tied to the 48.305 s pattern. Prints one line per
click and a last line "clicks=N".
"""
import argparse
import struct
import sys

import numpy as np

# How many windows on each side form a window's neighbourhood.
_NEIGHBOURS = 5


def find_clicks(x, sr, k=12.0, win_s=0.02):
    """[(time_s, score)] for every click in the mono signal x."""
    d2 = np.abs(np.diff(np.asarray(x, dtype=np.float64), 2))
    win = int(win_s * sr)
    nw = len(d2) // win
    if nw == 0:
        return []
    seg = d2[: nw * win].reshape(nw, win)
    level = np.median(seg, axis=1)
    padded = np.pad(level, _NEIGHBOURS, mode="edge")
    ref = np.median(np.lib.stride_tricks.sliding_window_view(padded, 2 * _NEIGHBOURS + 1),
                    axis=1) + 1e-7
    score = seg.max(axis=1) / ref
    clicks = []          # [last window, best score, best window]
    for w in np.where(score > k)[0]:
        if clicks and w - clicks[-1][0] <= 2:
            last = clicks[-1]
            last[0] = w
            if score[w] > last[1]:
                last[1], last[2] = score[w], w
            continue
        clicks.append([w, score[w], w])
    # +1: the second difference at index i sits on sample i + 1.
    return [((w * win + int(np.argmax(seg[w])) + 1) / sr, float(s)) for _, s, w in clicks]


def read_wav(path, channel=0):
    """(mono float signal, sample rate) of a 16-bit PCM WAV."""
    blob = open(path, "rb").read()
    nch, sr, bits = struct.unpack("<HI", blob[22:28]) + (struct.unpack("<H", blob[34:36])[0],)
    if bits != 16:
        raise SystemExit("%s: %d-bit, only 16-bit PCM is read" % (path, bits))
    off = blob.find(b"data") + 8
    frame = 2 * nch
    raw = blob[off: off + (len(blob) - off) // frame * frame]
    x = np.frombuffer(raw, dtype="<i2").reshape(-1, nch)[:, channel]
    return x.astype(np.float64) / 32768.0, sr


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("wav")
    ap.add_argument("--channel", type=int, default=0)
    ap.add_argument("--k", type=float, default=12.0)
    ap.add_argument("--period", type=float, default=0.0)
    a = ap.parse_args(argv)
    x, sr = read_wav(a.wav, a.channel)
    clicks = find_clicks(x, sr, a.k)
    print("%s: %.1f s, channel %d" % (a.wav, len(x) / sr, a.channel))
    for t, s in clicks:
        phase = "  phase %7.3f of %g" % (t % a.period, a.period) if a.period else ""
        print("  t=%9.3f s  score=%7.1f%s" % (t, s, phase))
    print("clicks=%d" % len(clicks))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
