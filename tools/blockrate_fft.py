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

Only one channel of that interface is patched -- the rig is mono. Averaging
the two channels of the stereo capture therefore halves the amplitude and
puts every absolute level 6 dB low, which on 2026-08-23 got misread as a
moved operating point. Hence `--channel auto`: channels more than
`SILENT_CHANNEL_DB` under the loudest one are dropped before anything is
measured, and the line printed says which survived.

Usage:
    python blockrate_fft.py out.wav [--block-rate 500] [--half-width 5]
                            [--channel auto|mix|0|1|...] [--harmonics 40]
"""
import argparse
import wave

import numpy as np


# A channel this far under the loudest one carries no signal worth mixing in.
# 20 dB is not a threshold anything was optimized against -- it separates
# "unpatched input" (measured at 41 dB down on this rig) from "quieter side of
# a stereo image" with room on both sides.
SILENT_CHANNEL_DB = 20.0


def _read_mono(path, channel="auto"):
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
    used = [0]
    if ch > 1:
        x = x.reshape(-1, ch)
        rms = np.sqrt(np.mean(x * x, axis=0))
        if channel == "mix":
            used = list(range(ch))
        elif channel == "auto":
            loudest = float(np.max(rms))
            floor = loudest * (10.0 ** (-SILENT_CHANNEL_DB / 20.0))
            used = [c for c in range(ch) if rms[c] >= floor]
        else:
            c = int(channel)
            if not 0 <= c < ch:
                raise SystemExit("no channel %d in a %d-channel capture" % (c, ch))
            used = [c]
        x = x[:, used].mean(axis=1)
    return x, sr, used


def _dbfs(rms):
    # A silent capture is a real outcome, not a crash. -inf would poison
    # every arithmetic downstream, so it floors at -200.
    return 20.0 * np.log10(rms) if rms > 1e-10 else -200.0


def analyze(path, block_rate_hz=500.0, half_width_hz=5.0, channel="auto",
            harmonics=40):
    """RMS in a narrow band around block_rate_hz, against the total RMS.

    The band level is computed by zeroing every bin outside the band and
    transforming back, NOT by reading a peak bin. A peak bin needs a window
    correction factor to become an RMS, and that factor is the single most
    common place for this measurement to be off by 3 dB.
    """
    x, sr, used = _read_mono(path, channel)
    if x.size == 0:
        raise SystemExit("empty capture: %s" % path)

    total = float(np.sqrt(np.mean(x * x)))

    spec = np.fft.rfft(x)
    freq = np.fft.rfftfreq(x.size, 1.0 / sr)
    keep = np.abs(freq - block_rate_hz) <= half_width_hz
    band_spec = np.where(keep, spec, 0.0)
    band = np.fft.irfft(band_spec, n=x.size)
    band_rms = float(np.sqrt(np.mean(band * band)))

    # The single band answers "how much is at the block rate". It does not
    # answer "did the artifact grow or did everything grow", and on
    # 2026-08-23 that difference was the whole finding: one image raised the
    # series 7.3 dB and left the rest of the spectrum untouched, which the
    # single band showed as a rising floor. So the series and its complement
    # are reported too.
    series_keep = np.zeros(freq.shape, dtype=bool)
    for k in range(1, harmonics + 1):
        series_keep |= np.abs(freq - block_rate_hz * k) <= half_width_hz
    series = np.fft.irfft(np.where(series_keep, spec, 0.0), n=x.size)
    rest = np.fft.irfft(np.where(series_keep, 0.0, spec), n=x.size)
    series_rms = float(np.sqrt(np.mean(series * series)))
    rest_rms = float(np.sqrt(np.mean(rest * rest)))

    return {
        "sample_rate": sr,
        "seconds": x.size / float(sr),
        "channels_used": used,
        "total_rms_dbfs": _dbfs(total),
        "band_rms_dbfs": _dbfs(band_rms),
        "excess_db": _dbfs(band_rms) - _dbfs(total),
        "series_rms_dbfs": _dbfs(series_rms),
        "rest_rms_dbfs": _dbfs(rest_rms),
        "series_over_rest_db": _dbfs(series_rms) - _dbfs(rest_rms),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("wav")
    ap.add_argument("--block-rate", type=float, default=500.0,
                    help="audio block rate in Hz; 48000/96 = 500")
    ap.add_argument("--half-width", type=float, default=5.0)
    ap.add_argument("--channel", default="auto",
                    help="auto (drop channels >%.0f dB under the loudest), "
                         "mix, or a channel index" % SILENT_CHANNEL_DB)
    ap.add_argument("--harmonics", type=int, default=40,
                    help="how many multiples of the block rate the series "
                         "covers")
    a = ap.parse_args()
    r = analyze(a.wav, a.block_rate, a.half_width, a.channel, a.harmonics)
    print("%s  %.1f s @ %d Hz  channels %s"
          % (a.wav, r["seconds"], r["sample_rate"],
             ",".join(str(c) for c in r["channels_used"])))
    print("  total RMS      %8.2f dBFS" % r["total_rms_dbfs"])
    print("  %.0f Hz band    %8.2f dBFS" % (a.block_rate, r["band_rms_dbfs"]))
    print("  excess         %8.2f dB (band minus total)" % r["excess_db"])
    print("  %.0f Hz series  %8.2f dBFS  (%d harmonics)"
          % (a.block_rate, r["series_rms_dbfs"], a.harmonics))
    print("  rest           %8.2f dBFS" % r["rest_rms_dbfs"])
    print("  series - rest  %8.2f dB" % r["series_over_rest_db"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
