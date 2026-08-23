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


def write_wav_lr(path, left, right, sr=48000):
    """Stereo with the two channels differing -- the rig's actual shape."""
    l = (np.clip(left, -1.0, 1.0) * 32767.0).astype("<i2")
    r = (np.clip(right, -1.0, 1.0) * 32767.0).astype("<i2")
    inter = np.empty(l.size * 2, dtype="<i2")
    inter[0::2] = l
    inter[1::2] = r
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(inter.tobytes())


def test_an_unpatched_channel_does_not_halve_the_level():
    # The 2026-08-23 trap. Only one side of the interface is patched, and
    # averaging the dead one in puts every absolute level 6 dB low -- which
    # was read as a moved operating point before it was read as arithmetic.
    sr, n = 48000, 48000 * 2
    t = np.arange(n) / sr
    live = 0.1 * np.sin(2 * np.pi * 500.0 * t)
    dead = np.zeros(n)
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "mono_rig.wav")
        write_wav_lr(p, live, dead, sr)
        auto = b.analyze(p, block_rate_hz=500.0)
        mixed = b.analyze(p, block_rate_hz=500.0, channel="mix")
        picked = b.analyze(p, block_rate_hz=500.0, channel="0")
    check(auto["channels_used"] == [0],
          "auto kept channels %s, expected [0]" % auto["channels_used"])
    check(abs(auto["total_rms_dbfs"] - (-23.01)) < 0.5,
          "auto total %.2f dBFS, expected the live channel's -23.0"
          % auto["total_rms_dbfs"])
    check(abs(picked["total_rms_dbfs"] - auto["total_rms_dbfs"]) < 0.01,
          "explicit channel 0 disagrees with auto")
    # And the old behaviour must still be reachable, and must still be wrong
    # by the 6.02 dB that arithmetic predicts -- otherwise this test would
    # pass for a tool that simply ignores --channel.
    check(abs((auto["total_rms_dbfs"] - mixed["total_rms_dbfs"]) - 6.02) < 0.1,
          "mix should sit 6.02 dB under auto, gap is %.2f dB"
          % (auto["total_rms_dbfs"] - mixed["total_rms_dbfs"]))
    # A real stereo image must NOT be thinned out by auto.
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "stereo.wav")
        write_wav_lr(p, live, 0.05 * np.sin(2 * np.pi * 500.0 * t), sr)
        both = b.analyze(p, block_rate_hz=500.0)
    check(both["channels_used"] == [0, 1],
          "auto dropped a channel only 6 dB down: %s" % both["channels_used"])


def test_series_and_rest_separate_the_artifact_from_the_music():
    # The finding the single band could not make: whether a change raises the
    # artifact or raises everything. Built so the two answers differ -- a
    # harmonic series at 500/1000/1500 Hz against a 700 Hz tone that is
    # louder than any single harmonic.
    sr, n = 48000, 48000 * 2
    t = np.arange(n) / sr
    series = sum(0.05 * np.sin(2 * np.pi * 500.0 * k * t) for k in (1, 2, 3))
    music = 0.2 * np.sin(2 * np.pi * 700.0 * t)
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "split.wav")
        write_wav_lr(p, series + music, np.zeros(n), sr)
        r = b.analyze(p, block_rate_hz=500.0)
    # three incoherent 0.05 sines -> RMS 0.05*sqrt(3/2) = 0.0612 = -24.26 dBFS
    check(abs(r["series_rms_dbfs"] - (-24.26)) < 0.3,
          "series RMS %.2f dBFS, expected -24.26" % r["series_rms_dbfs"])
    # the 700 Hz tone alone -> 0.2/sqrt(2) = -17.0 dBFS
    check(abs(r["rest_rms_dbfs"] - (-17.0)) < 0.5,
          "rest RMS %.2f dBFS, expected -17.0" % r["rest_rms_dbfs"])
    check(r["series_over_rest_db"] < 0.0,
          "the series is quieter than the music here; got %.2f dB"
          % r["series_over_rest_db"])
    # The single band sees only the fundamental and therefore cannot tell
    # this story -- that is why the split exists.
    check(r["band_rms_dbfs"] < r["series_rms_dbfs"] - 3.0,
          "one band should be well under three harmonics: %.2f vs %.2f"
          % (r["band_rms_dbfs"], r["series_rms_dbfs"]))


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
