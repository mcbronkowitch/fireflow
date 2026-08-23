#!/usr/bin/env python3
"""Guard rails for the block-rate analyser.

No pytest here -- plain asserts, exit code says it all, same shape as
tools/test_count_panel_controls.py. Run from tools/:
    python test_blockrate_fft.py

Why a guard at all for forty lines of numpy: the 8 Aug finding is written
in ONE number ("the tone sits 4.9 dB under total RMS"), and a scaling slip
in that number is invisible -- it looks like a measurement either way.
"""
import os, sys, wave, tempfile
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blockrate_fft as b

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def write_wav(path, data, sr=48000):
    """Stereo 16-bit, the format ffmpeg's dshow capture writes."""
    x = np.clip(data, -1.0, 1.0)
    frames = (x * 32767.0).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(np.repeat(frames, 2).tobytes())


def test_pure_tone_reports_its_own_rms():
    # A sine of amplitude 0.1 has RMS 0.1/sqrt(2) = -23.01 dBFS. The band
    # RMS around its frequency must report that and not half of it, not
    # twice it -- window-gain bookkeeping is exactly where this goes wrong.
    sr, n = 48000, 48000 * 2
    t = np.arange(n) / sr
    sig = 0.1 * np.sin(2 * np.pi * 500.0 * t)
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "tone.wav")
        write_wav(p, sig, sr)
        r = b.analyze(p, block_rate_hz=500.0)
    check(abs(r["band_rms_dbfs"] - (-23.01)) < 0.5,
          "pure tone band RMS %.2f dBFS, expected -23.0" % r["band_rms_dbfs"])
    check(abs(r["excess_db"]) < 0.5,
          "a wav that IS the tone must have excess ~0 dB, got %.2f"
          % r["excess_db"])


def test_noise_only_has_no_block_rate_line():
    # The discriminating case. If this passes for noise too, the analyser
    # is measuring the band's width and not a tone in it.
    sr, n = 48000, 48000 * 2
    rng = np.random.default_rng(0)
    sig = 0.05 * rng.standard_normal(n)
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "noise.wav")
        write_wav(p, sig, sr)
        r = b.analyze(p, block_rate_hz=500.0)
    check(r["excess_db"] < -15.0,
          "noise should sit far below total RMS in a 10 Hz band, got %.2f dB"
          % r["excess_db"])


def test_the_8_aug_shape_is_reproduced():
    # Noise floor plus a line 5 dB ABOVE the total RMS is not physical, so
    # the built case is the real one: a tone a few dB under a broadband
    # signal, the shape the board actually showed.
    sr, n = 48000, 48000 * 2
    t = np.arange(n) / sr
    rng = np.random.default_rng(1)
    sig = 0.2 * rng.standard_normal(n) + 0.05 * np.sin(2 * np.pi * 500.0 * t)
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "mix.wav")
        write_wav(p, sig, sr)
        r = b.analyze(p, block_rate_hz=500.0)
    # tone RMS = 0.05/sqrt(2) = -29.0 dBFS; total ~ 0.2 -> -14.0 dBFS.
    check(abs(r["band_rms_dbfs"] - (-29.0)) < 0.7,
          "band RMS %.2f dBFS, expected -29.0" % r["band_rms_dbfs"])
    check(-17.0 < r["excess_db"] < -12.0,
          "excess %.2f dB outside the constructed -15 dB" % r["excess_db"])


for name, fn in sorted(list(globals().items())):
    if name.startswith("test_") and callable(fn):
        fn()

if FAILS:
    print("FAIL")
    for f in FAILS:
        print("  " + f)
    raise SystemExit(1)
print("ok: blockrate_fft guard")
