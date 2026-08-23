#!/usr/bin/env python3
"""Level at the audio block rate, relative to total RMS.

The 8 Aug diagnosis of the block-rate tone (shell/README.md, "Offener
Befund") was an ad-hoc numpy session. This file is the same measurement
with a name and a guard, because the round that prices the mux scan has to
compare against those numbers and a re-derived scaling would compare two
different quantities.

Why "relative to total RMS" and not dBFS alone: the damping between the
board's DAC and the interface input is unknown and irrelevant -- a ratio
is scale-invariant, an absolute level is not. Both are reported anyway,
because a clipped or near-silent capture has to be visible as such.

Capture side (the only DirectShow audio input on this machine):
    ffmpeg -f dshow -i audio="Line (Universal Audio Twin USB)" \
           -t 10 -ac 2 -ar 48000 -y out.wav

Usage:
    python blockrate_fft.py out.wav [--block-rate 500] [--half-width 5]
"""
import argparse
import wave

import numpy as np


def _read_mono(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit("expected 16-bit PCM, got %d bytes/sample"
                             % w.getsampwidth())
        sr = w.getframerate()
        ch = w.getnchannels()
        raw = w.readframes(w.getnframes())
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    # The channel count comes from the header and is not inferred from the
    # length -- a mono capture with an even sample count would otherwise be
    # folded in half, which looks like a plausible signal.
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr


def _dbfs(rms):
    # A silent capture is a real outcome, not a crash. -inf would poison
    # every arithmetic downstream, so it floors at -200.
    return 20.0 * np.log10(rms) if rms > 1e-10 else -200.0


def analyze(path, block_rate_hz=500.0, half_width_hz=5.0):
    """RMS in a narrow band around block_rate_hz, against the total RMS.

    The band level is computed by zeroing every bin outside the band and
    transforming back, NOT by reading a peak bin. A peak bin needs a window
    correction factor to become an RMS, and that factor is the single most
    common place for this measurement to be off by 3 dB.
    """
    x, sr = _read_mono(path)
    if x.size == 0:
        raise SystemExit("empty capture: %s" % path)

    total = float(np.sqrt(np.mean(x * x)))

    spec = np.fft.rfft(x)
    freq = np.fft.rfftfreq(x.size, 1.0 / sr)
    keep = np.abs(freq - block_rate_hz) <= half_width_hz
    band_spec = np.where(keep, spec, 0.0)
    band = np.fft.irfft(band_spec, n=x.size)
    band_rms = float(np.sqrt(np.mean(band * band)))

    return {
        "sample_rate": sr,
        "seconds": x.size / float(sr),
        "total_rms_dbfs": _dbfs(total),
        "band_rms_dbfs": _dbfs(band_rms),
        "excess_db": _dbfs(band_rms) - _dbfs(total),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("wav")
    ap.add_argument("--block-rate", type=float, default=500.0,
                    help="audio block rate in Hz; 48000/96 = 500")
    ap.add_argument("--half-width", type=float, default=5.0)
    a = ap.parse_args()
    r = analyze(a.wav, a.block_rate, a.half_width)
    print("%s  %.1f s @ %d Hz" % (a.wav, r["seconds"], r["sample_rate"]))
    print("  total RMS      %8.2f dBFS" % r["total_rms_dbfs"])
    print("  %.0f Hz band    %8.2f dBFS" % (a.block_rate, r["band_rms_dbfs"]))
    print("  excess         %8.2f dB (band minus total)" % r["excess_db"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
